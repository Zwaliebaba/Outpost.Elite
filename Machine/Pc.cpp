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

// Where an interrupt native code makes or takes returns to: an offset nothing is run at, because the call ends
// when the handler's IRET comes back there.
constexpr std::uint16_t CALL_RETURN_OFFSET = 0xFFFF;

// Thrown through native code when the program stops inside an interrupt the native code made or took: the step
// that started the native code catches it, and RunUntil reports the stop.
struct ProgramStopped
{
};

// Thrown through what the native thread is running when the machine is destroyed under it.
struct NativeAbandoned
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

Pc::Pc(FileStore& _files, const Desc& _desc)
  : m_speaker(m_clock),
    m_pit(m_clock, m_pic, m_speaker),
    m_keyboard(m_clock, m_pic, m_pit, m_speaker),
    m_gamePort(m_instructionCycles),
    m_cga(m_clock),
    m_services(m_memory, m_ports, _files, m_clock, _desc),
    m_processor(m_memory)
{
  MapPorts(Pic::COMMAND_PORT, Pic::DATA_PORT, m_pic);
  MapPorts(Pit::FIRST_PORT, Pit::LAST_PORT, m_pit);
  MapPorts(Keyboard::FIRST_PORT, Keyboard::LAST_PORT, m_keyboard);
  MapPorts(GamePort::PORT, GamePort::PORT, m_gamePort);
  MapPorts(CGA_FIRST_PORT, CGA_LAST_PORT, m_cga);
  m_processor.SetInterruptSource(&m_pic);
  m_processor.SetHostServices(&m_services);
  m_services.PowerOn();
  // The ROM's hardware interrupt handlers are code too, and nothing else would run them (ADR-011).
  HookNativeFirmware(*this);
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
  m_processor.Regs() = _program.registers;
  m_services.StartProgram(_program.pspSegment);
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
  if (m_nativeActive && !m_onNativeThread)
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
  const std::uint32_t cycles = m_processor.Step();
  m_clock += cycles;
  m_instructionCycles += cycles;
  m_pit.Advance();
  m_keyboard.Advance();
  if (m_processor.AtHook())
  {
    RunHook();
  }
}

// Paced, a step takes no time: what the Dispatcher runs, an interrupt's entry and the ROM's IRETs, counts only on the
// instruction clock, and the clock itself moves only in a wait.
void Pc::StepPaced()
{
  m_instructionCycles += m_processor.Step();
  ++m_stepsSinceIdle;
  if (m_processor.AtHook())
  {
    RunHook();
  }
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
  turn.registers = m_processor.Regs();
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
  m_processor.SetHookMap(&m_native.Map());
}

void Pc::ReturnNear(std::uint16_t _popBytes) noexcept
{
  Registers& regs = m_processor.Regs();
  regs.ip = m_memory.Read16(regs.ss, regs.sp);
  regs.sp = static_cast<std::uint16_t>(regs.sp + 2 + _popBytes);
}

void Pc::ReturnFar(std::uint16_t _popBytes) noexcept
{
  Registers& regs = m_processor.Regs();
  regs.ip = m_memory.Read16(regs.ss, regs.sp);
  regs.cs = m_memory.Read16(regs.ss, static_cast<std::uint16_t>(regs.sp + 2));
  regs.sp = static_cast<std::uint16_t>(regs.sp + 4 + _popBytes);
}

void Pc::ReturnInterrupt() noexcept
{
  Registers& regs = m_processor.Regs();
  regs.ip = m_memory.Read16(regs.ss, regs.sp);
  regs.cs = m_memory.Read16(regs.ss, static_cast<std::uint16_t>(regs.sp + 2));
  const std::uint16_t flags = m_memory.Read16(regs.ss, static_cast<std::uint16_t>(regs.sp + 4));
  regs.flags = static_cast<std::uint16_t>((flags & FLAGS_WRITABLE) | FLAGS_FIXED_ONES);
  regs.sp = static_cast<std::uint16_t>(regs.sp + 6);
}

void Pc::CallInterrupt(std::uint8_t _vector)
{
  if (m_services.ServiceInterrupt(m_processor.Regs(), _vector))
  {
    if (Stopped() != StopReason::Reached)
    {
      throw ProgramStopped{};
    }
    return;
  }
  Registers& regs = m_processor.Regs();
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
  RunToReturn(segment, CALL_RETURN_OFFSET, stackPointer);
}

void Pc::RunHook()
{
  const Registers& regs = m_processor.Regs();
  NativeCode::Hook* hook = m_native.At(Memory::Linear(regs.cs, regs.ip));
  if (hook == nullptr)
  {
    throw std::logic_error("Pc: the Dispatcher stopped at an entry no native routine is registered for");
  }
  if (hook->wait != NativeWait::Never && !m_onNativeThread)
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
    _hook.routine(*this);
  }
  catch (const ProgramStopped&)
  {
    // The program stopped inside an interrupt the native code made or took; RunUntil reports why.
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
  Registers& regs = m_processor.Regs();
  while (m_processor.InterruptDue())
  {
    // Park CS:IP where nothing runs, let the Dispatcher take the interrupt there, and run the handler
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
// above _stackPointer: a handler that discards its own return address returns past its caller, and the
// call is over there.
void Pc::RunToReturn(std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _stackPointer)
{
  const Registers& regs = m_processor.Regs();
  while ((regs.cs != _segment || regs.ip != _offset || regs.sp < _stackPointer) && regs.sp <= _stackPointer)
  {
    Step();
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
  if (m_processor.AtUnportedCode())
    return StopReason::Unported;
  if (m_timeMode == TimeMode::Paced && m_stepsSinceIdle >= m_spinLimit)
    return StopReason::Spinning;
  if (m_overran)
    return StopReason::Overran;
  return StopReason::Reached;
}

} // namespace Machine
