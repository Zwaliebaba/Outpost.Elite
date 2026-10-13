#include "pch.h"

#include "Platform.h"

#include "GameState.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Elite
{

namespace
{

// The bit of the 8088's FLAGS that the platform keeps: the interrupt flag. The rest of FLAGS is the processor's, and there is
// none: nothing sets the trap flag, and native code passes the direction flag itself (_backward).
constexpr std::uint16_t FLAG_INTERRUPT = 0x0200;
// DOS starts a program with FLAGS F202h: interrupts on and the direction flag clear (ADR-006 item 4).
constexpr std::uint16_t ENTRY_FLAGS = 0xF202;

// The vector table as the BIOS leaves it (ADR-006 item 3): every vector at its own IRET in the ROM, F000:FD00 + n, but the
// timer's and the keyboard's, which name the BIOS's handlers, DOS's terminate address, which names the ROM's halt, and the
// mouse's, which names the driver's entry when one is loaded.
constexpr std::uint16_t ROM_SEGMENT = 0xF000;
constexpr std::uint16_t TIMER_HANDLER_OFFSET = 0xE000;
constexpr std::uint16_t KEYBOARD_HANDLER_OFFSET = 0xE040;
constexpr std::uint16_t MOUSE_DRIVER_OFFSET = 0xE060;
constexpr std::uint16_t HALT_OFFSET = 0xE070;
constexpr std::uint16_t IRET_STUBS_OFFSET = 0xFD00;
constexpr std::uint16_t IRET_STUBS_END = 0xFE00;
constexpr std::uint16_t MODEL_OFFSET = 0xFFFE;
constexpr std::uint8_t MODEL_IBM_PC = 0xFF;
constexpr std::uint8_t OPCODE_NOP = 0x90;
constexpr std::uint8_t OPCODE_IRET = 0xCF;
constexpr std::uint32_t ROM_LINEAR = static_cast<std::uint32_t>(ROM_SEGMENT) << 4;
constexpr std::uint32_t ADDRESS_MASK = 0xFFFFF;

constexpr std::uint8_t VECTOR_TIMER = 0x08;
constexpr std::uint8_t VECTOR_KEYBOARD = 0x09;
constexpr std::uint8_t VECTOR_TERMINATE = 0x22;
constexpr std::uint8_t VECTOR_MOUSE = 0x33;

// What the 8088 takes over the ROM's IRET, as the interpreter charged it until D7: Intel's 8086 figure and three words popped
// over the 8-bit bus.
constexpr Cycles IRET_CYCLES = 24 + 3 * 4;

// What the 8088 takes over each instruction of the BIOS's two hardware handlers, as the interpreter charged them until D7.
constexpr Cycles STI = 2;
constexpr Cycles PUSH_SEGMENT = 14;
constexpr Cycles PUSH_REGISTER = 15;
constexpr Cycles MOVE_IMMEDIATE = 4; // MOV AX,imm16 and MOV AL,imm8
constexpr Cycles MOVE_TO_SEGMENT = 2;
constexpr Cycles INCREMENT_MEMORY = 29;
constexpr Cycles JUMP_TAKEN = 16;
constexpr Cycles JUMP_NOT_TAKEN = 4;
constexpr Cycles COMPARE_MEMORY = 20;
constexpr Cycles XOR_REGISTERS = 3;
constexpr Cycles STORE_WORD = 14;
constexpr Cycles STORE_IMMEDIATE_BYTE = 16;
constexpr Cycles SOFTWARE_INTERRUPT = 71;
constexpr Cycles INTERRUPT_RETURN = 36;
constexpr Cycles PORT_ACCESS = 10;    // IN AL,imm8 and OUT imm8,AL
constexpr Cycles LOGIC_IMMEDIATE = 4; // OR AL,imm8 and AND AL,imm8
constexpr Cycles POP_REGISTER = 12;   // POP AX and POP DS

// The system control port's bit 7, which the XT's keyboard is acknowledged by.
constexpr std::uint8_t KEYBOARD_CLEAR = 0x80;

// DOS's print string: its end.

constexpr std::uint32_t CENTISECONDS_PER_MINUTE = 60 * 100;

// Thrown through native code when the program stops inside a service or an interrupt: the native thread catches it, and
// RunUntil reports the stop.
struct ProgramStopped
{
};

// Thrown through what the native thread is running when the platform is destroyed under it.
struct NativeAbandoned
{
};

[[nodiscard]] constexpr std::uint32_t Linear(FarAddress _address) noexcept
{
  return ((static_cast<std::uint32_t>(_address.segment) << 4) + _address.offset) & ADDRESS_MASK;
}

// The BIOS's tick count for DOS's time of day.
[[nodiscard]] std::uint32_t TimerTicksAt(const DosDateTime& _moment) noexcept
{
  const std::uint32_t centiseconds = (std::uint32_t{_moment.hour} * 60u + _moment.minute) * CENTISECONDS_PER_MINUTE +
                                     std::uint32_t{_moment.second} * 100u + _moment.hundredths;
  return Bios::TimerTicksAt(centiseconds);
}

} // namespace

Platform::Platform(GameState& _state, FileStore& _files, const Desc& _desc)
  : m_state(_state),
    m_speaker(m_clock),
    m_timer(m_clock, m_controller, m_speaker),
    m_keyboard(m_clock, m_controller, m_timer, m_speaker),
    m_gamePort(m_instructionCycles),
    m_cga(m_clock),
    m_bios(_state, m_cga, TimerTicksAt(_desc.startMoment)),
    m_mouse(_desc.mousePresent),
    m_files(_state, _files, _desc.startMoment),
    m_flags(ENTRY_FLAGS),
    m_commandTail(_desc.commandTail)
{
  for (std::size_t vector = 0; vector < m_vectors.size(); ++vector)
  {
    m_vectors[vector] = FarAddress{ROM_SEGMENT, static_cast<std::uint16_t>(IRET_STUBS_OFFSET + vector)};
  }
  m_vectors[VECTOR_TIMER] = FarAddress{ROM_SEGMENT, TIMER_HANDLER_OFFSET};
  m_vectors[VECTOR_KEYBOARD] = FarAddress{ROM_SEGMENT, KEYBOARD_HANDLER_OFFSET};
  m_vectors[VECTOR_TERMINATE] = FarAddress{ROM_SEGMENT, HALT_OFFSET};
  if (_desc.mousePresent)
  {
    m_vectors[VECTOR_MOUSE] = FarAddress{ROM_SEGMENT, MOUSE_DRIVER_OFFSET};
  }
  m_powerOnVectors = m_vectors;
  // The BIOS's two hardware handlers are native, and run whenever the program has put the BIOS's vectors back and an
  // interrupt falls due (ADR-011 item 3).
  Hook(FarAddress{ROM_SEGMENT, TIMER_HANDLER_OFFSET}, [this] { RunBiosTimerHandler(); });
  Hook(FarAddress{ROM_SEGMENT, KEYBOARD_HANDLER_OFFSET}, [this] { RunBiosKeyboardHandler(); });
}

Platform::~Platform()
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

// ── The program ──

void Platform::StartProgram() noexcept
{
  m_files.StartProgram();
}

void Platform::SetEntry(NativeRoutine _routine)
{
  if (m_entry.routine)
  {
    throw std::logic_error("Platform: the program has a start routine already");
  }
  m_entry.routine = std::move(_routine);
}

void Platform::Hook(FarAddress _address, NativeRoutine _routine)
{
  // Made in place, in a node the map never moves.
  const auto [found, added] = m_hooks.try_emplace(Linear(_address));
  if (!added)
  {
    throw std::logic_error("Platform: two native routines at one entry");
  }
  found->second.routine = std::move(_routine);
}

// ── Running ──

StopReason Platform::RunUntil(Cycles _cycle)
{
  m_runLimit = _cycle;
  while (m_clock < _cycle)
  {
    if (!m_started)
    {
      StartNative();
    }
    else if (m_nativeActive)
    {
      ResumeNative();
    }
    if (const StopReason reason = Stopped(); reason != StopReason::Reached)
    {
      m_runLimit = NO_EVENT;
      return reason;
    }
    if (!m_nativeActive)
    {
      throw std::logic_error("Platform: the program's routine ended with no reason to stop");
    }
  }
  m_runLimit = NO_EVENT;
  return StopReason::Reached;
}

StopReason Platform::Stopped() const noexcept
{
  if (m_terminated)
    return StopReason::Terminated;
  if (m_stepsSinceIdle >= m_spinLimit)
    return StopReason::Spinning;
  return StopReason::Reached;
}

void Platform::StartNative()
{
  if (!m_entry.routine)
  {
    throw std::logic_error("Platform: no program to run");
  }
  m_started = true;
  m_nativeThread = std::thread([this] { NativeMain(); });
  m_nativeActive = true;
  ResumeNative();
}

void Platform::ResumeNative()
{
  HandToNative();
  if (m_nativeError)
  {
    std::rethrow_exception(std::exchange(m_nativeError, nullptr));
  }
}

void Platform::HandToNative() noexcept
{
  m_onNativeThread = true;
  m_toNative.release();
  m_toHost.acquire();
  m_onNativeThread = false;
}

void Platform::NativeMain()
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
      ++m_entry.calls;
      m_entry.routine();
      // The start routine's far return, to PSP:0000, is DOS's int 20h: the program has ended.
      m_terminated = true;
    }
    catch (const ProgramStopped&)
    {
      // The program stopped inside a service or an interrupt, its reason recorded on the way; RunUntil reports it.
      if (Stopped() == StopReason::Reached)
      {
        m_nativeError = std::make_exception_ptr(std::logic_error("Platform: the program stopped with no reason recorded"));
      }
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

void Platform::ReachedRunLimit()
{
  if (!m_onNativeThread)
  {
    throw std::logic_error("Platform: native code waited off the native thread");
  }
  m_toHost.release();
  m_toNative.acquire();
  if (m_abandon)
  {
    throw NativeAbandoned{};
  }
}

// ── Interrupts ──

void Platform::EnableInterrupts()
{
  m_flags = static_cast<std::uint16_t>(m_flags | FLAG_INTERRUPT);
  TakeDueInterrupts();
}

void Platform::DisableInterrupts() noexcept
{
  m_flags = static_cast<std::uint16_t>(m_flags & ~FLAG_INTERRUPT);
}

bool Platform::InterruptsEnabled() const noexcept
{
  return (m_flags & FLAG_INTERRUPT) != 0;
}

void Platform::TakeDueInterrupts()
{
  while (InterruptsEnabled() && m_controller.InterruptPending())
  {
    // The acknowledge cycle, then the entry the 8088 makes, which the instruction clock counts.
    const std::uint8_t vector = m_controller.Acknowledge();
    m_instructionCycles += HARDWARE_INTERRUPT_CYCLES;
    ++m_stepsSinceIdle;
    ++m_interruptsTaken;
    Interrupt(vector);
    if (Stopped() != StopReason::Reached)
    {
      throw ProgramStopped{};
    }
  }
}

void Platform::SoftwareInterrupt(std::uint8_t _vector)
{
  CallHandler(m_vectors[_vector]);
}

void Platform::CallHandler(FarAddress _handler)
{
  ++m_stepsSinceIdle;
  const std::uint16_t flags = m_flags;
  m_flags = static_cast<std::uint16_t>(m_flags & ~FLAG_INTERRUPT);
  RunHandler(_handler);
  m_flags = flags;
  if (Stopped() != StopReason::Reached)
  {
    throw ProgramStopped{};
  }
}

// As the 8088 enters a handler: IF cleared, the handler run, and its IRET putting the flags back as they were.
void Platform::Interrupt(std::uint8_t _vector)
{
  const std::uint16_t flags = m_flags;
  m_flags = static_cast<std::uint16_t>(m_flags & ~FLAG_INTERRUPT);
  RunHandler(m_vectors[_vector]);
  m_flags = flags;
}

void Platform::RunHandler(FarAddress _handler)
{
  const auto found = m_hooks.find(Linear(_handler));
  if (found != m_hooks.end())
  {
    ++found->second.calls;
    found->second.routine();
    return;
  }
  if (_handler.segment == ROM_SEGMENT && RomByte(_handler) == OPCODE_IRET)
  {
    m_instructionCycles += IRET_CYCLES; // the ROM's IRET, which the stubs nothing serves point to
    return;
  }
  throw std::logic_error("Platform: an interrupt to code no native routine stands in for");
}

void Platform::SetInterruptVector(std::uint8_t _vector, FarAddress _handler) noexcept
{
  const FarAddress old = m_vectors[_vector];
  const auto changed = [](std::uint16_t _from, std::uint16_t _to)
  { return static_cast<std::uint64_t>(((_from ^ _to) & 0x00FF) != 0) + static_cast<std::uint64_t>(((_from ^ _to) & 0xFF00) != 0); };
  m_vectorChanges += changed(old.offset, _handler.offset) + changed(old.segment, _handler.segment);
  m_vectors[_vector] = _handler;
}

std::uint8_t Platform::RomByte(FarAddress _address) const
{
  const std::uint32_t linear = Linear(_address);
  if (linear < ROM_LINEAR)
  {
    throw std::out_of_range("Platform::RomByte: an address outside the BIOS's ROM");
  }
  const auto offset = static_cast<std::uint16_t>(linear - ROM_LINEAR);
  if (offset >= IRET_STUBS_OFFSET && offset < IRET_STUBS_END)
  {
    return OPCODE_IRET;
  }
  if (offset == MOUSE_DRIVER_OFFSET)
  {
    return OPCODE_NOP; // anything but an IRET: IsMouseDriverInstalled takes it as a driver
  }
  if (offset == MOUSE_DRIVER_OFFSET + 1)
  {
    return OPCODE_IRET;
  }
  if (offset == MODEL_OFFSET)
  {
    return MODEL_IBM_PC;
  }
  return 0;
}

std::uint8_t Platform::HandlerOpcode(FarAddress _handler) const
{
  return RomByte(_handler);
}

// ── The BIOS's hardware interrupt handlers (ADR-011 item 3) ──

// F000:E000: counts the tick, starts again from 0 once a day has passed, calls int 1Ch and sends EOI; and the 8088's cycles
// over each of the ROM's instructions on the way. The ROM's PUSH and POP of DS and AX, and its own STI, act on the 8088's
// registers and stack, which the platform does not have: nothing reads them, and the handler's IRET puts the flags back.
void Platform::RunBiosTimerHandler()
{
  m_flags = static_cast<std::uint16_t>(m_flags | FLAG_INTERRUPT);
  Cycles cycles = STI + PUSH_SEGMENT + PUSH_REGISTER + MOVE_IMMEDIATE + MOVE_TO_SEGMENT + INCREMENT_MEMORY;
  const Bios::Tick tick = m_bios.CountTick();
  cycles += tick.carried ? JUMP_NOT_TAKEN + INCREMENT_MEMORY : JUMP_TAKEN;
  cycles += COMPARE_MEMORY;
  if (!tick.dayCheck)
  {
    cycles += JUMP_TAKEN;
  }
  else
  {
    cycles += JUMP_NOT_TAKEN + COMPARE_MEMORY;
    cycles += tick.dayEnded ? JUMP_NOT_TAKEN + XOR_REGISTERS + 2 * STORE_WORD + STORE_IMMEDIATE_BYTE : JUMP_TAKEN;
  }
  m_instructionCycles += cycles;
  // INT 1Ch: the ROM's IRET, as the program leaves the vector.
  m_instructionCycles += SOFTWARE_INTERRUPT + INTERRUPT_RETURN;
  // OUT 20h,20h.
  m_controller.EndOfInterrupt();
  ++m_portWrites;
  m_instructionCycles += MOVE_IMMEDIATE + PORT_ACCESS + 2 * POP_REGISTER + INTERRUPT_RETURN;
}

// F000:E040: reads the scan code and discards it, pulses the system control port's bit 7 to acknowledge it, and sends EOI.
void Platform::RunBiosKeyboardHandler()
{
  m_flags = static_cast<std::uint16_t>(m_flags | FLAG_INTERRUPT);
  (void)m_keyboard.Data();
  const auto cleared = static_cast<std::uint8_t>(m_keyboard.SystemControl() | KEYBOARD_CLEAR);
  m_keyboard.SetSystemControl(cleared);
  m_keyboard.SetSystemControl(static_cast<std::uint8_t>(cleared & ~KEYBOARD_CLEAR));
  m_controller.EndOfInterrupt();
  m_portWrites += 3;
  m_instructionCycles += STI + PUSH_REGISTER + 2 * PORT_ACCESS + LOGIC_IMMEDIATE + PORT_ACCESS + LOGIC_IMMEDIATE + PORT_ACCESS +
                         MOVE_IMMEDIATE + PORT_ACCESS + POP_REGISTER + INTERRUPT_RETURN;
}

// ── Paced time ──

void Platform::Idle(Cycles _limit)
{
  Cycles next = std::min({m_timer.NextInterruptAt(), m_keyboard.NextDeliveryAt(), m_cga.NextStatusChangeAt(), _limit});
  if (next <= m_clock)
  {
    next = m_clock + 1;
  }
  m_clock = next;
  m_stepsSinceIdle = 0;
  m_timer.Advance();
  m_keyboard.Advance();
}

void Platform::LoopTurn(std::span<const std::uint16_t> _signature)
{
  if (_signature.size() > MOST_TURN_WORDS)
  {
    throw std::logic_error("Platform::LoopTurn: a turn signature of more than MOST_TURN_WORDS words");
  }
  ++m_stepsSinceIdle;
  Turn turn;
  std::ranges::copy(_signature, turn.signature.begin());
  turn.signatureWords = _signature.size();
  turn.memoryChanges = MemoryChanges();
  turn.portWrites = PortWrites();
  turn.valid = true;
  NoteTurn(turn);
  EndTurn();
}

// A turn that changed nothing since the last idles: no byte, no port, and the same signature.
void Platform::NoteTurn(const Turn& _turn)
{
  const bool same = _turn.signatureWords == m_lastTurn.signatureWords &&
                    std::equal(_turn.signature.begin(), _turn.signature.begin() + static_cast<std::ptrdiff_t>(_turn.signatureWords),
                               m_lastTurn.signature.begin());
  const bool idle =
    m_lastTurn.valid && same && _turn.memoryChanges == m_lastTurn.memoryChanges && _turn.portWrites == m_lastTurn.portWrites;
  m_lastTurn = _turn;
  if (idle)
  {
    Idle(m_runLimit);
  }
}

// After a turn is noted: the run's end if the clock reached it, and the interrupts now due.
void Platform::EndTurn()
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

void Platform::Spend(Cycles _cycles)
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

std::uint64_t Platform::MemoryChanges() const noexcept
{
  return m_state.ChangeCount() + m_bios.ChangeCount() + m_vectorChanges;
}

std::uint64_t Platform::PortWrites() const noexcept
{
  return m_portWrites + m_bios.PortWrites();
}

// ── The picture ──

void Platform::PublishFlightSnapshot(const FlightSnapshot& _snapshot, bool _continues) noexcept
{
  m_flightSnapshots[0] = m_flightSnapshots[1];
  FlightSnapshot& latest = m_flightSnapshots[1];
  latest = _snapshot;
  latest.sequence = m_flightSnapshots[0].sequence + 1;
  latest.pacedTime = m_clock;
  m_spaceView.Finish(m_state);
  for (std::size_t index = 0; index < latest.objectCount; ++index)
  {
    latest.objects[index].model = m_spaceView.DrawnAsModel(latest.objects[index].slot);
  }
  latest.beams = m_beams;
  latest.beamCount = m_beamCount;
  if (_continues && m_flightSnapshots[0].sequence != 0)
  {
    ContinueFrom(m_flightSnapshots[0], latest);
  }
}

void Platform::PublishSpaceViewPicture(const FlightSnapshot& _snapshot) noexcept
{
  m_flightSnapshots[0] = m_flightSnapshots[1];
  FlightSnapshot& latest = m_flightSnapshots[1];
  latest = _snapshot;
  latest.sequence = m_flightSnapshots[0].sequence + 1;
  latest.pacedTime = m_clock;
  latest.objectCount = 0;
  latest.beamCount = 0;
  latest.picture = true;
  m_spaceView.FinishPicture(m_state);
}

void Platform::Render(FrameImage& _image) const noexcept
{
  m_cga.Render(m_state.VideoBytes(), _image);
}

} // namespace Elite
