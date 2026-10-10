#include "pch.h"

#include "Pc.h"

#include "NativeFirmware.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Machine
{

namespace
{

constexpr std::uint16_t CGA_FIRST_PORT = 0x3D0;
constexpr std::uint16_t CGA_LAST_PORT = 0x3DF;

// The jumps a loop closes with: Jcc (70-7F, and their aliases 60-6F), LOOPNZ, LOOPZ, LOOP and JCXZ
// (E0-E3), JMP near and short (E9, EB).
[[nodiscard]] bool IsJump(std::uint8_t _opcode) noexcept
{
  return (_opcode >= 0x60 && _opcode <= 0x7F) || (_opcode >= 0xE0 && _opcode <= 0xE3) || _opcode == 0xE9 || _opcode == 0xEB;
}

// Where a call from native code returns to: an offset nothing is executed at, because the call ends
// when the program reaches it.
constexpr std::uint16_t CALL_RETURN_OFFSET = 0xFFFF;

// Thrown through native code when the program stops inside a call the native code made: the step
// that started the native code catches it, and RunUntil reports the stop.
struct ProgramStopped
{
};

// Thrown through what the native thread is running when the machine is destroyed under it.
struct NativeAbandoned
{
};

// Thrown through a native routine when code it called returned past it, as a routine that drops its
// own return address does. Dispatch catches it: the routine's frames are gone, and whatever called the
// routine looks at where the program now is, which may be past it too.
struct ReturnedPast
{
};

// Runs _fn when the scope ends, however it ends.
template <typename Fn> class OnExit
{
public:
  explicit OnExit(Fn _fn) noexcept
    : m_fn(std::move(_fn))
  {
  }

  OnExit(const OnExit&) = delete;
  OnExit& operator=(const OnExit&) = delete;

  ~OnExit()
  {
    m_fn();
  }

private:
  Fn m_fn;
};

} // namespace

Pc::Pc(FileStore& _files, const Desc& _desc, ProcessorFactory _makeProcessor)
  : m_speaker(m_clock),
    m_pit(m_clock, m_pic, m_speaker),
    m_keyboard(m_clock, m_pic, m_pit, m_speaker),
    m_gamePort(m_instructionCycles),
    m_cga(m_clock),
    m_services(m_memory, m_ports, _files, m_clock, _desc),
    m_processor(_makeProcessor(m_memory, m_ports))
{
  MapPorts(Pic::COMMAND_PORT, Pic::DATA_PORT, m_pic);
  MapPorts(Pit::FIRST_PORT, Pit::LAST_PORT, m_pit);
  MapPorts(Keyboard::FIRST_PORT, Keyboard::LAST_PORT, m_keyboard);
  MapPorts(GamePort::PORT, GamePort::PORT, m_gamePort);
  MapPorts(CGA_FIRST_PORT, CGA_LAST_PORT, m_cga);
  m_processor->SetInterruptSource(&m_pic);
  m_processor->SetHostServices(&m_services);
  m_services.PowerOn();
  if (!m_processor->Interprets())
  {
    // The ROM's hardware interrupt handlers are code too, and nothing else would run them (ADR-011).
    HookNativeFirmware(*this);
  }
}

Pc::~Pc()
{
  if (!m_nativeThread.joinable())
  {
    return;
  }
  if (m_nativeActive)
  {
    m_abandon = true;
    HandToNative();
  }
  m_shutdown = true;
  m_toNative.release();
  m_nativeThread.join();
}

LoadError Pc::Load(std::span<const std::uint8_t> _file, const ExeLoader::Desc& _desc, LoadedProgram& _program)
{
  const LoadError error = ExeLoader::Load(m_memory, _file, _desc, _program);
  if (error != LoadError::None)
    return error;
  m_processor->Regs() = _program.registers;
  m_services.StartProgram(_program.pspSegment);
  m_pacingAt = NOT_PACING;
  return LoadError::None;
}

void Pc::SetTimeMode(TimeMode _mode) noexcept
{
  m_timeMode = _mode;
  m_cga.SetRetraceSeenOnce(_mode == TimeMode::Paced);
  m_stepsSinceIdle = 0;
  m_lastTurn.valid = false;
}

void Pc::SetSpinLimit(std::uint64_t _steps) noexcept
{
  m_spinLimit = _steps;
}

void Pc::Step()
{
  if (m_nativeActive && !m_onNativeThread && !m_besideNative)
  {
    const Cycles limit = m_runLimit;
    m_runLimit = std::min(limit, m_clock + 1);
    ResumeNative();
    m_runLimit = limit;
    return;
  }
  if (m_timeMode == TimeMode::Paced)
  {
    StepPaced();
    return;
  }
  const std::uint32_t cycles = m_processor->Step();
  m_clock += cycles;
  m_instructionCycles += cycles;
  m_pit.Advance();
  m_keyboard.Advance();
  if (m_processor->AtHook())
  {
    RunHook();
  }
}

void Pc::StepPaced()
{
  if (m_processor->Halted() && (m_processor->Regs().flags & FLAG_INTERRUPT) != 0 && !m_pic.InterruptPending())
  {
    // HLT is a wait by definition.
    Idle(m_runLimit);
    return;
  }
  const std::uint16_t segment = m_processor->Regs().cs;
  const std::uint16_t offset = m_processor->Regs().ip;
  const std::uint32_t linear = Memory::Linear(segment, offset);
  if (!m_pacingCosts.empty() && PaceAt(linear))
  {
    return;
  }
  const std::uint8_t opcode = m_memory.Read8(segment, offset);
  const std::uint64_t interrupts = m_processor->HardwareInterruptCount();
  m_instructionCycles += m_processor->Step();
  ++m_stepsSinceIdle;
  if (linear == m_pacingAt && m_processor->HardwareInterruptCount() == interrupts)
  {
    // The instruction the cost was paid for has run: the next arrival pays again.
    m_pacingAt = NOT_PACING;
  }
  if (m_processor->AtHook())
  {
    RunHook();
    return;
  }
  const Registers& after = m_processor->Regs();
  if (after.cs == segment && after.ip <= offset && m_processor->HardwareInterruptCount() == interrupts && IsJump(opcode))
  {
    NoteBackwardJump();
  }
}

// Whether this step goes to paying the pacing cost at _linear (SetPacingCost) rather than to the instruction
// there. An interrupt that is due is taken first, as the CPU would take it, and the handler's IRET comes back
// to the same instruction, which then pays on: the cost is set at the first arrival and paid off as the
// clock moves, a wait's step at a time.
bool Pc::PaceAt(std::uint32_t _linear)
{
  if (!m_processor->Interprets() || m_processor->InterruptDue())
  {
    return false;
  }
  if (m_pacingAt != _linear)
  {
    const auto point = std::ranges::find(m_pacingCosts, _linear, &std::pair<std::uint32_t, Cycles>::first);
    if (point == m_pacingCosts.end())
    {
      return false;
    }
    m_pacingAt = _linear;
    m_pacingUntil = m_clock + point->second;
  }
  if (m_clock >= m_pacingUntil)
  {
    return false;
  }
  Idle(std::min(m_pacingUntil, m_runLimit));
  return true;
}

void Pc::SetPacingCost(std::uint32_t _linear, Cycles _cycles)
{
  const auto point = std::ranges::find(m_pacingCosts, _linear, &std::pair<std::uint32_t, Cycles>::first);
  if (point != m_pacingCosts.end())
  {
    point->second = _cycles;
    return;
  }
  m_pacingCosts.emplace_back(_linear, _cycles);
}

void Pc::Spend(Cycles _cycles)
{
  const Cycles until = m_clock + _cycles;
  while (m_clock < until)
  {
    Idle(std::min(until, m_runLimit));
    if (m_clock >= m_runLimit)
    {
      ReachedRunLimit();
    }
    TakeDueInterrupts();
  }
}

void Pc::NoteBackwardJump()
{
  Turn turn;
  turn.registers = m_processor->Regs();
  turn.memoryChanges = m_memory.ChangeCount();
  turn.portWrites = m_ports.WriteCount();
  turn.valid = true;
  NoteTurn(turn);
}

// A turn that changed nothing since the last idles: no byte, no port, and the same registers or the same signature.
void Pc::NoteTurn(const Turn& _turn)
{
  const bool same = _turn.fromSignature
                      ? m_lastTurn.fromSignature && _turn.signatureWords == m_lastTurn.signatureWords &&
                          std::equal(_turn.signature.begin(), _turn.signature.begin() + _turn.signatureWords, m_lastTurn.signature.begin())
                      : !m_lastTurn.fromSignature && _turn.registers == m_lastTurn.registers;
  const bool idle =
    m_lastTurn.valid && same && _turn.memoryChanges == m_lastTurn.memoryChanges && _turn.portWrites == m_lastTurn.portWrites;
  m_lastTurn = _turn;
  if (idle)
  {
    Idle(m_runLimit);
  }
}

void Pc::Idle(Cycles _limit)
{
  Cycles next = std::min({m_pit.NextInterruptAt(), m_keyboard.NextDeliveryAt(), m_cga.NextStatusChangeAt(), _limit});
  if (next <= m_clock)
  {
    next = m_clock + 1;
  }
  m_clock = next;
  m_stepsSinceIdle = 0;
  m_pit.Advance();
  m_keyboard.Advance();
}

StopReason Pc::RunUntil(Cycles _cycle)
{
  m_runLimit = _cycle;
  while (m_clock < _cycle)
  {
    if (m_nativeActive)
    {
      ResumeNative();
    }
    else
    {
      Step();
    }
    if (const StopReason reason = Stopped(); reason != StopReason::Reached)
    {
      m_runLimit = NO_EVENT;
      return reason;
    }
  }
  m_runLimit = NO_EVENT;
  return StopReason::Reached;
}

void Pc::Hook(std::uint16_t _segment, std::uint16_t _offset, std::string _name, NativeRoutine _routine, const NativeContract& _contract,
              NativeReturn _exit, NativeWait _wait)
{
  m_native.Add(_segment, _offset, std::move(_name), std::move(_routine), _contract, _exit, _wait);
  m_processor->SetHookMap(&m_native.Map());
  if (!m_comparison)
  {
    m_comparison = std::make_unique<Comparison>();
  }
}

void Pc::CallNear(std::uint16_t _offset)
{
  Registers& regs = m_processor->Regs();
  regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
  m_memory.Write16(regs.ss, regs.sp, CALL_RETURN_OFFSET);
  const auto returned = static_cast<std::uint16_t>(regs.sp + 2);
  regs.ip = _offset;
  const bool beside = EnterBesideNative();
  const OnExit leave(
    [this, beside]() noexcept
    {
      if (beside)
        m_besideNative = false;
    });
  RunToReturn(regs.cs, CALL_RETURN_OFFSET, returned);
  if (regs.sp > returned && (m_onNativeThread ? m_nativeRoutines : m_hostRoutines) > 0)
  {
    throw ReturnedPast{};
  }
}

void Pc::RunNative(NativeCode::Hook& _hook)
{
  std::uint32_t& running = m_onNativeThread ? m_nativeRoutines : m_hostRoutines;
  ++running;
  const OnExit done([&running]() noexcept { --running; });
  try
  {
    _hook.routine(*this);
  }
  catch (const ReturnedPast&)
  {
    // Code it called returned past it, to where its caller called it from or beyond: its caller looks
    // at where the program now is.
    return;
  }
}

void Pc::ReturnNear(std::uint16_t _popBytes) noexcept
{
  Registers& regs = m_processor->Regs();
  regs.ip = m_memory.Read16(regs.ss, regs.sp);
  regs.sp = static_cast<std::uint16_t>(regs.sp + 2 + _popBytes);
}

void Pc::ReturnFar(std::uint16_t _popBytes) noexcept
{
  Registers& regs = m_processor->Regs();
  regs.ip = m_memory.Read16(regs.ss, regs.sp);
  regs.cs = m_memory.Read16(regs.ss, static_cast<std::uint16_t>(regs.sp + 2));
  regs.sp = static_cast<std::uint16_t>(regs.sp + 4 + _popBytes);
}

void Pc::ReturnInterrupt() noexcept
{
  Registers& regs = m_processor->Regs();
  regs.ip = m_memory.Read16(regs.ss, regs.sp);
  regs.cs = m_memory.Read16(regs.ss, static_cast<std::uint16_t>(regs.sp + 2));
  const std::uint16_t flags = m_memory.Read16(regs.ss, static_cast<std::uint16_t>(regs.sp + 4));
  regs.flags = static_cast<std::uint16_t>((flags & FLAGS_WRITABLE) | FLAGS_FIXED_ONES);
  regs.sp = static_cast<std::uint16_t>(regs.sp + 6);
}

void Pc::CallInterrupt(std::uint8_t _vector)
{
  if (m_services.ServiceInterrupt(m_processor->Regs(), _vector))
  {
    if (Stopped() != StopReason::Reached)
    {
      throw ProgramStopped{};
    }
    return;
  }
  Registers& regs = m_processor->Regs();
  const std::uint16_t segment = regs.cs;
  const std::uint16_t stackPointer = regs.sp;
  const auto push = [&](std::uint16_t _value)
  {
    regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
    m_memory.Write16(regs.ss, regs.sp, _value);
  };
  push(regs.flags);
  push(regs.cs);
  push(CALL_RETURN_OFFSET);
  regs.flags = static_cast<std::uint16_t>(regs.flags & ~(FLAG_INTERRUPT | FLAG_TRAP));
  const std::uint32_t entry = static_cast<std::uint32_t>(_vector) * 4;
  regs.ip = m_memory.Read16(entry);
  regs.cs = m_memory.Read16(entry + 2);
  const bool beside = EnterBesideNative();
  const OnExit leave(
    [this, beside]() noexcept
    {
      if (beside)
        m_besideNative = false;
    });
  RunToReturn(segment, CALL_RETURN_OFFSET, stackPointer);
}

bool Pc::EnterBesideNative() noexcept
{
  // A call a host makes, from outside any native routine, while one waits suspended on the native
  // thread: it runs here, beside the waiting routine, which stays where it is.
  if (m_onNativeThread || m_hostRoutines > 0 || !m_nativeActive || m_besideNative)
  {
    return false;
  }
  m_besideNative = true;
  return true;
}

void Pc::RunHook()
{
  const Registers& regs = m_processor->Regs();
  NativeCode::Hook* hook = m_native.At(Memory::Linear(regs.cs, regs.ip));
  if (hook == nullptr)
  {
    throw std::logic_error("Pc: the CPU stopped at an entry no native routine is registered for");
  }
  if (hook->wait != NativeWait::Never && !m_onNativeThread && !m_besideNative)
  {
    StartNative(*hook);
    return;
  }
  Dispatch(*hook);
}

void Pc::Dispatch(NativeCode::Hook& _hook)
{
  ++_hook.calls;
  // A call that waited looped inside, where no turn of the caller's loop is seen; a turn seen before
  // it says nothing about the next one after it.
  const Cycles clock = m_clock;
  const OnExit forget(
    [this, clock]() noexcept
    {
      if (m_clock != clock)
        m_lastTurn.valid = false;
    });
  try
  {
    // A routine that waits as a rule is never compared: its original could not be undone, and might
    // never return. The routines it calls through their hooks are, each one a comparison of its own.
    if (m_native.Verifying() && !m_comparison->active && _hook.wait != NativeWait::Always)
    {
      Compare(_hook);
    }
    else
    {
      RunNative(_hook);
    }
  }
  catch (const ProgramStopped&)
  {
    // The program stopped inside a call the native code made; RunUntil reports why.
    if (m_overran)
    {
      m_native.SetOverran(_hook.name);
    }
    return;
  }
}

void Pc::StartNative(NativeCode::Hook& _hook)
{
  if (!m_nativeThread.joinable())
  {
    m_nativeThread = std::thread([this] { NativeMain(); });
  }
  m_nativeHook = &_hook;
  m_nativeActive = true;
  ResumeNative();
}

void Pc::ResumeNative()
{
  HandToNative();
  if (m_nativeError)
  {
    std::rethrow_exception(std::exchange(m_nativeError, nullptr));
  }
}

void Pc::HandToNative() noexcept
{
  m_onNativeThread = true;
  m_toNative.release();
  m_toHost.acquire();
  m_onNativeThread = false;
}

void Pc::NativeMain()
{
  for (;;)
  {
    m_toNative.acquire();
    if (m_shutdown)
    {
      return;
    }
    try
    {
      Dispatch(*m_nativeHook);
    }
    catch (const NativeAbandoned&)
    {
      m_abandon = false; // unwound, as the destructor asked
    }
    catch (...)
    {
      m_nativeError = std::current_exception();
    }
    m_nativeActive = false;
    m_toHost.release();
  }
}

void Pc::ReachedRunLimit()
{
  if (!m_onNativeThread)
  {
    // Native code cannot stop at the end of a run off the native thread, and what would end its wait
    // may only come between runs (ADR-010 item 8).
    m_overran = true;
    throw ProgramStopped{};
  }
  m_toHost.release();
  m_toNative.acquire();
  if (m_abandon)
  {
    throw NativeAbandoned{};
  }
}

void Pc::Wait()
{
  Idle(m_runLimit);
  if (m_clock >= m_runLimit)
  {
    ReachedRunLimit();
  }
  TakeDueInterrupts();
}

void Pc::LoopTurn()
{
  ++m_stepsSinceIdle;
  NoteBackwardJump();
  EndTurn();
}

void Pc::LoopTurn(std::span<const std::uint16_t> _signature)
{
  if (_signature.size() > MOST_TURN_WORDS)
  {
    throw std::logic_error("Pc::LoopTurn: a turn signature of more than MOST_TURN_WORDS words");
  }
  ++m_stepsSinceIdle;
  Turn turn;
  std::ranges::copy(_signature, turn.signature.begin());
  turn.signatureWords = _signature.size();
  turn.fromSignature = true;
  turn.memoryChanges = m_memory.ChangeCount();
  turn.portWrites = m_ports.WriteCount();
  turn.valid = true;
  NoteTurn(turn);
  EndTurn();
}

// After a turn is noted: the run's end if the clock reached it, and the interrupts now due.
void Pc::EndTurn()
{
  if (m_clock >= m_runLimit)
  {
    ReachedRunLimit();
  }
  TakeDueInterrupts();
  if (Stopped() != StopReason::Reached)
  {
    throw ProgramStopped{};
  }
}

void Pc::TakeDueInterrupts()
{
  Registers& regs = m_processor->Regs();
  while (m_processor->InterruptDue())
  {
    // Park CS:IP where nothing executes, let the CPU take the interrupt there, and run the handler
    // until its IRET comes back to it.
    const std::uint16_t segment = regs.cs;
    const std::uint16_t offset = regs.ip;
    const std::uint16_t stackPointer = regs.sp;
    regs.ip = CALL_RETURN_OFFSET;
    Step();
    if (Stopped() != StopReason::Reached)
    {
      throw ProgramStopped{};
    }
    RunToReturn(segment, CALL_RETURN_OFFSET, stackPointer);
    regs.ip = offset;
  }
}

// Runs until the code returns to _segment:_offset with SP back at _stackPointer, or as soon as SP is
// above _stackPointer: a routine that discards its own return address returns past its caller
// (TickEscapePod, RunPauseScreen's abort), and the call is over there. The original run in a
// comparison (_original) also records what it executes.
void Pc::RunToReturn(std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _stackPointer, Comparison* _original)
{
  const Registers& regs = m_processor->Regs();
  while ((regs.cs != _segment || regs.ip != _offset || regs.sp < _stackPointer) && regs.sp <= _stackPointer)
  {
    const std::uint16_t segment = regs.cs;
    const std::uint16_t offset = regs.ip;
    const std::uint64_t interrupts = m_processor->HardwareInterruptCount();
    const bool covered = _original != nullptr && segment == _original->segment;
    Step();
    if (covered && m_processor->HardwareInterruptCount() == interrupts)
    {
      _original->executed.push_back(offset);
    }
    if (Stopped() != StopReason::Reached)
    {
      throw ProgramStopped{};
    }
    if (m_clock >= m_runLimit)
    {
      ReachedRunLimit();
    }
  }
}

void Pc::Compare(NativeCode::Hook& _hook)
{
  if (!m_processor->Interprets())
  {
    throw std::logic_error("Pc: native code is compared with the original only on a processor that interprets it (ADR-011)");
  }
  Comparison& work = *m_comparison;
  Registers& regs = m_processor->Regs();
  const Registers entry = regs;
  // Where the original's run ends: the return address its caller, or the interrupt, pushed.
  const std::uint16_t returnOffset = m_memory.Read16(regs.ss, regs.sp);
  const std::uint16_t returnSegment =
    _hook.exit == NativeReturn::Near ? regs.cs : m_memory.Read16(regs.ss, static_cast<std::uint16_t>(regs.sp + 2));
  const std::uint16_t frameBytes = _hook.exit == NativeReturn::Near ? 2 : _hook.exit == NativeReturn::Far ? 4 : 6;
  const Cycles clock = m_clock;
  const std::uint64_t interrupts = m_processor->HardwareInterruptCount();
  const std::uint64_t services = m_services.CallCount();
  // What paced time sees before the call: each run starts from it.
  const Turn turnBefore = m_lastTurn;
  const std::uint64_t changesBefore = m_memory.ChangeCount();
  const std::uint64_t writesBefore = m_ports.WriteCount();
  const Cycles instructionCyclesBefore = m_instructionCycles;

  // The original, all of it: no hook runs inside it, so the native routines it reaches are compared
  // through it. Every byte it changes and every port access it makes is recorded.
  work.originalWrites.Clear();
  work.originalPorts.clear();
  work.segment = _hook.segment;
  work.executed.clear();
  {
    work.active = true;
    m_processor->SetHookMap(nullptr);
    m_memory.SetJournal(&work.originalWrites);
    m_ports.SetLog(&work.originalPorts);
    const OnExit stop(
      [&]() noexcept
      {
        m_memory.SetJournal(nullptr);
        m_ports.SetLog(nullptr);
        m_processor->SetHookMap(&m_native.Map());
        work.active = false;
      });
    RunToReturn(returnSegment, returnOffset, static_cast<std::uint16_t>(entry.sp + frameBytes), &work);
  }
  if (m_clock != clock || m_processor->HardwareInterruptCount() != interrupts || m_services.CallCount() != services ||
      work.originalWrites.Overflowed())
  {
    ++_hook.unverifiable; // what it did cannot be undone, so its outcome stands, and covers nothing
    return;
  }
  // From here the call is compared, so what the original ran counts towards its coverage.
  for (const std::uint16_t offset : work.executed)
  {
    _hook.executed.Insert(offset);
  }
  const Registers original = regs;
  const std::uint64_t changes = m_memory.ChangeCount();
  const std::uint64_t writes = m_ports.WriteCount();
  const Cycles instructionCycles = m_instructionCycles;
  const Turn turn = m_lastTurn;
  const std::span<const WriteJournal::Entry> written = work.originalWrites.Entries();
  work.originalAfter.resize(written.size());
  for (std::size_t index = 0; index < written.size(); ++index)
  {
    work.originalAfter[index] = m_memory.Read8(written[index].linear);
  }

  // Undone, and the native routine run from the same state over the same port accesses.
  m_memory.Undo(work.originalWrites);
  m_memory.SetChangeCount(changesBefore);
  m_ports.SetWriteCount(writesBefore);
  m_instructionCycles = instructionCyclesBefore;
  m_lastTurn = turnBefore;
  regs = entry;
  work.nativeWrites.Clear();
  {
    work.active = true;
    m_memory.SetJournal(&work.nativeWrites);
    m_ports.StartReplay(work.originalPorts);
    const OnExit stop(
      [&]() noexcept
      {
        m_memory.SetJournal(nullptr);
        m_ports.EndReplay();
        work.active = false;
      });
    RunNative(_hook);
  }

  std::string difference = m_native.Compare(_hook, original, regs, work.originalWrites, work.originalAfter, work.nativeWrites, m_memory);
  const auto add = [&](std::string_view _what)
  {
    difference += difference.empty() ? "" : "; ";
    difference += _what;
  };
  if (const std::string ports = m_ports.ReplayDifference(); !ports.empty())
  {
    add("ports: " + ports);
  }
  if (m_clock != clock || m_processor->HardwareInterruptCount() != interrupts)
  {
    add("the native routine waited or took an interrupt, and the original did neither");
  }
  if (m_services.CallCount() != services)
  {
    add("the native routine called the BIOS, DOS or mouse services, and the original did not");
  }
  if (work.nativeWrites.Overflowed())
  {
    add("the native routine changed more bytes than a journal holds");
  }
  // The run carries on from the original's outcome, whatever the comparison found: its memory, dead
  // stack included, its registers, and what paced time saw of it. A compared run is then the
  // interpreted run, observed, and one mismatch does not hide the next.
  m_memory.Undo(work.nativeWrites);
  const std::span<std::uint8_t> bytes = m_memory.Bytes();
  for (std::size_t index = 0; index < written.size(); ++index)
  {
    bytes[written[index].linear] = work.originalAfter[index];
  }
  m_memory.SetChangeCount(changes);
  m_ports.SetWriteCount(writes);
  m_instructionCycles = instructionCycles;
  m_lastTurn = turn;
  regs = original;
  if (difference.empty())
  {
    ++_hook.verified;
    return;
  }
  ++_hook.mismatches;
  m_native.AddMismatch(NativeCode::Mismatch{_hook.name, _hook.calls, clock, std::move(difference)});
}

void Pc::MapPorts(std::uint16_t _first, std::uint16_t _last, PortBus& _device)
{
  if (!m_ports.Map(_first, _last, _device))
    throw std::logic_error("Pc: two devices claim the same port");
}

StopReason Pc::Stopped() const noexcept
{
  if (m_services.Fault().has_value())
    return StopReason::Fault;
  if (m_services.Terminated())
    return StopReason::Terminated;
  if (m_processor->AtUnportedCode())
    return StopReason::Unported;
  if (m_processor->Halted() && (m_processor->Regs().flags & FLAG_INTERRUPT) == 0)
    return StopReason::Deadlocked;
  if (m_timeMode == TimeMode::Paced && m_stepsSinceIdle >= m_spinLimit)
    return StopReason::Spinning;
  if (m_overran)
    return StopReason::Overran;
  return StopReason::Reached;
}

} // namespace Machine
