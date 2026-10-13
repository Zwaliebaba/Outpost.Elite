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

// The system control port: bit 0 the tone timer's gate, bit 1 the speaker's data, and bit 7, which the XT's keyboard is
// acknowledged by, its hold.
constexpr std::uint8_t TONE_GATE = 0x01;
constexpr std::uint8_t SPEAKER_BITS = 0x03;
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
    m_tone(m_clock, m_speaker),
    m_events(m_clock),
    m_gamePort(m_instructionCycles),
    m_cga(m_clock),
    m_bios(_state, m_cga, TimerTicksAt(_desc.startMoment)),
    m_mouse(_desc.mousePresent),
    m_files(_state, _files, _desc.startMoment),
    m_mouseDriverLoaded(_desc.mousePresent),
    m_flags(ENTRY_FLAGS),
    m_commandTail(_desc.commandTail)
{
  // The system control port's power-on value reaches the speaker like any other write; the tone timer's gate is low already.
  m_speaker.SetSystemControl(m_systemControl);
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

void Platform::SetGameHandler(Irq _irq, NativeRoutine _routine)
{
  HookedRoutine& handler = m_gameHandlers[static_cast<std::size_t>(_irq)];
  if (handler.routine)
  {
    throw std::logic_error("Platform: two native routines at one entry");
  }
  handler.routine = std::move(_routine);
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
  while (InterruptsEnabled() && m_events.InterruptPending())
  {
    // The acknowledge cycle, then the entry the 8088 makes, which the instruction clock counts.
    const Irq irq = m_events.Take();
    m_instructionCycles += HARDWARE_INTERRUPT_CYCLES;
    ++m_stepsSinceIdle;
    ++m_interruptsTaken;
    Interrupt(irq);
    if (Stopped() != StopReason::Reached)
    {
      throw ProgramStopped{};
    }
  }
}

void Platform::SetSystemControl(std::uint8_t _value)
{
  const auto changed = static_cast<std::uint8_t>(m_systemControl ^ _value);
  m_systemControl = _value;
  if ((changed & TONE_GATE) != 0)
  {
    m_tone.SetGate((_value & TONE_GATE) != 0);
  }
  if ((changed & SPEAKER_BITS) != 0)
  {
    m_speaker.SetSystemControl(_value);
  }
  m_events.HoldKeyboard((_value & KEYBOARD_CLEAR) != 0);
}

void Platform::CallAbsentMouse()
{
  // INT 33h, as the 8088 runs it, to what its vector names, the interrupt's frame in place and IF cleared until its IRET.
  ++m_stepsSinceIdle;
  if (m_mouseDriverLoaded)
  {
    throw std::logic_error("Platform: an interrupt to code no native routine stands in for");
  }
  m_instructionCycles += IRET_CYCLES; // the ROM's IRET, which int 33h names with no driver loaded
  if (Stopped() != StopReason::Reached)
  {
    throw ProgramStopped{};
  }
}

// As the 8088 enters a handler: IF cleared, the handler its vector names run, and its IRET putting the flags back as they were.
void Platform::Interrupt(Irq _irq)
{
  const Irq irq = _irq;
  const std::uint16_t flags = m_flags;
  m_flags = static_cast<std::uint16_t>(m_flags & ~FLAG_INTERRUPT);
  switch (Handler(irq))
  {
  case IrqHandler::Bios:
    if (irq == Irq::Timer)
    {
      RunBiosTimerHandler();
    }
    else
    {
      RunBiosKeyboardHandler();
    }
    break;
  case IrqHandler::Game:
  {
    HookedRoutine& handler = m_gameHandlers[static_cast<std::size_t>(irq)];
    if (!handler.routine)
    {
      throw std::logic_error("Platform: an interrupt to code no native routine stands in for");
    }
    ++handler.calls;
    handler.routine();
    break;
  }
  case IrqHandler::Stray:
    throw std::logic_error("Platform: an interrupt to code no native routine stands in for");
  }
  m_flags = flags;
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
  m_events.EndOfInterrupt();
  m_instructionCycles += MOVE_IMMEDIATE + PORT_ACCESS + 2 * POP_REGISTER + INTERRUPT_RETURN;
}

// F000:E040: reads the scan code and discards it, pulses the system control port's bit 7 to acknowledge it, and sends EOI.
void Platform::RunBiosKeyboardHandler()
{
  m_flags = static_cast<std::uint16_t>(m_flags | FLAG_INTERRUPT);
  (void)m_events.ScanCode();
  const auto cleared = static_cast<std::uint8_t>(m_systemControl | KEYBOARD_CLEAR);
  SetSystemControl(cleared);
  SetSystemControl(static_cast<std::uint8_t>(cleared & ~KEYBOARD_CLEAR));
  m_events.EndOfInterrupt();
  m_instructionCycles += STI + PUSH_REGISTER + 2 * PORT_ACCESS + LOGIC_IMMEDIATE + PORT_ACCESS + LOGIC_IMMEDIATE + PORT_ACCESS +
                         MOVE_IMMEDIATE + PORT_ACCESS + POP_REGISTER + INTERRUPT_RETURN;
}

// ── Paced time ──

void Platform::Idle(Cycles _limit)
{
  Cycles next = std::min({m_events.NextEventAt(), m_cga.NextStatusChangeAt(), _limit});
  if (next <= m_clock)
  {
    next = m_clock + 1;
  }
  m_clock = next;
  m_stepsSinceIdle = 0;
  m_events.Advance();
}

void Platform::Wait()
{
  Idle(m_runLimit);
  EndTurn();
}

void Platform::Spin()
{
  ++m_stepsSinceIdle;
  EndTurn();
}

// After a wait or a spin: the run's end if the clock reached it, and the interrupts now due.
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
