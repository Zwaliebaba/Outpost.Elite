#include "pch.h"

#include "Host.h"

#include "GameState.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Elite
{

namespace
{

// The bit of the 8088's FLAGS that the platform keeps: the interrupt flag. The rest of FLAGS is the processor's, and there is
// none: nothing sets the trap flag, and native code runs its string work forwards (ADR-046).
constexpr std::uint16_t FLAG_INTERRUPT = 0x0200;
// DOS starts a program with FLAGS F202h: interrupts on and the direction flag clear (ADR-006 item 4).
constexpr std::uint16_t ENTRY_FLAGS = 0xF202;

// The system control port: bit 0 the tone's gate, bit 1 the speaker's data.
constexpr std::uint8_t TONE_GATE = 0x01;
constexpr std::uint8_t SPEAKER_DATA = 0x02;
constexpr std::uint8_t SPEAKER_BITS = 0x03;

constexpr Milliseconds MILLISECONDS_PER_CENTISECOND = 10;

// Thrown through the game's coroutines when the program stops inside a service or an interrupt: RunUntil catches it where the
// program's coroutine ends, and reports the stop.
struct ProgramStopped
{
};

} // namespace

Host::Host(GameState& _state, FileStore& _files, const Desc& _desc)
  : m_state(_state),
    m_speaker(m_clock),
    m_events(m_clock),
    m_cga(m_clock),
    m_mouse(_desc.mousePresent),
    m_files(_files),
    m_startMoment(_desc.startMoment),
    m_flags(ENTRY_FLAGS),
    m_cheat(_desc.cheat)
{
}

Host::~Host() = default;

// ── The program ──

void Host::SetEntry(ProgramRoutine _routine)
{
  if (m_entry.routine)
  {
    throw std::logic_error("Host: the program has a start routine already");
  }
  m_entry.routine = std::move(_routine);
}

void Host::SetGameHandler(Irq _irq, NativeRoutine _routine)
{
  HookedRoutine& handler = m_gameHandlers[static_cast<std::size_t>(_irq)];
  if (handler.routine)
  {
    throw std::logic_error("Host: two native routines at one entry");
  }
  handler.routine = std::move(_routine);
}

// ── Running ──

StopReason Host::RunUntil(Milliseconds _time)
{
  m_runLimit = _time;
  while (m_clock < _time)
  {
    if (!m_terminated)
    {
      Resume();
    }
    if (const StopReason reason = Stopped(); reason != StopReason::Reached)
    {
      m_runLimit = NO_EVENT;
      return reason;
    }
    if (!m_suspended)
    {
      throw std::logic_error("Host: the program's routine ended with no reason to stop");
    }
  }
  m_runLimit = NO_EVENT;
  return StopReason::Reached;
}

StopReason Host::Stopped() const noexcept
{
  if (m_terminated)
    return StopReason::Terminated;
  if (m_stepsSinceIdle >= m_spinLimit)
    return StopReason::Spinning;
  return StopReason::Reached;
}

// The program's coroutine started, or resumed in the wait it is suspended in, until it suspends again or ends. Its end is
// the program's: its far return to PSP:0000 is DOS's int 20h; a stop on the way ends it by ProgramStopped; any other exception
// is thrown on to the host.
void Host::Resume()
{
  if (!m_program)
  {
    if (!m_entry.routine)
    {
      throw std::logic_error("Host: no program to run");
    }
    ++m_entry.calls;
    m_program.emplace(m_entry.routine());
    m_program->Handle().resume();
  }
  else if (m_suspended)
  {
    std::exchange(m_suspended, nullptr).resume();
  }
  if (!m_program->Done())
  {
    return;
  }
  try
  {
    m_program->Rethrow();
    m_terminated = true;
  }
  catch (const ProgramStopped&)
  {
    // The program stopped inside a service or an interrupt, its reason recorded on the way; RunUntil reports it.
    if (Stopped() == StopReason::Reached)
    {
      throw std::logic_error("Host: the program stopped with no reason recorded");
    }
  }
}

// ── Interrupts ──

void Host::EnableInterrupts()
{
  m_flags = static_cast<std::uint16_t>(m_flags | FLAG_INTERRUPT);
  TakeDueInterrupts();
}

void Host::DisableInterrupts() noexcept
{
  m_flags = static_cast<std::uint16_t>(m_flags & ~FLAG_INTERRUPT);
}

bool Host::InterruptsEnabled() const noexcept
{
  return (m_flags & FLAG_INTERRUPT) != 0;
}

void Host::TakeDueInterrupts()
{
  while (InterruptsEnabled() && m_events.InterruptPending())
  {
    const Irq irq = m_events.Take();
    ++m_stepsSinceIdle;
    ++m_interruptsTaken;
    Interrupt(irq);
    if (Stopped() != StopReason::Reached)
    {
      throw ProgramStopped{};
    }
  }
}

void Host::SetTickRunning(bool _running)
{
  m_events.SetTickRunning(_running);
  TakeDueInterrupts();
}

void Host::EndOfInterrupt()
{
  m_events.EndOfInterrupt();
  TakeDueInterrupts();
}

void Host::SetSystemControl(std::uint8_t _value)
{
  const auto changed = static_cast<std::uint8_t>(m_systemControl ^ _value);
  m_systemControl = _value;
  if ((changed & SPEAKER_BITS) != 0)
  {
    m_speaker.SetBits((_value & TONE_GATE) != 0, (_value & SPEAKER_DATA) != 0);
  }
}

// As the 8088 enters a handler: IF cleared, the handler its vector names run, and its IRET putting the flags back as they were.
void Host::Interrupt(Irq _irq)
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
      throw std::logic_error("Host: an interrupt to code no native routine stands in for");
    }
    ++handler.calls;
    handler.routine();
    break;
  }
  case IrqHandler::Stray:
    throw std::logic_error("Host: an interrupt to code no native routine stands in for");
  }
  m_flags = flags;
}

// ── The host's defaults, where the game has no handler of its own ──

// A tick that came before the game stopped it: the interrupt ended.
void Host::RunBiosTimerHandler()
{
  m_events.EndOfInterrupt();
}

// A key before the game's handler is in place, or after it is taken out: dropped, and the interrupt ended.
void Host::RunBiosKeyboardHandler()
{
  (void)m_events.TakeKey();
  m_events.EndOfInterrupt();
}

// ── Paced time ──

void Host::Idle(Milliseconds _limit)
{
  Milliseconds next = std::min({m_events.NextEventAt(), m_cga.NextStatusChangeAt(), _limit});
  if (next <= m_clock)
  {
    next = m_clock + 1;
  }
  m_clock = next;
  m_stepsSinceIdle = 0;
  m_events.Advance();
}

Host::RunAwaiter Host::Wait()
{
  Idle(m_runLimit);
  return RunAwaiter(*this);
}

void Host::Spin()
{
  // The clock stands still, below the run's end, where the last wait left it.
  ++m_stepsSinceIdle;
  EndTurn();
}

// After a wait or a spin, and the game resumed if the wait reached the run's end: the interrupts now due.
void Host::EndTurn()
{
  TakeDueInterrupts();
  if (Stopped() != StopReason::Reached)
  {
    throw ProgramStopped{};
  }
}

Task<void> Host::Spend(Milliseconds _duration)
{
  const Milliseconds until = m_clock + _duration;
  while (m_clock < until)
  {
    Idle(std::min(until, m_runLimit));
    if (m_clock >= m_runLimit)
    {
      co_await RunAwaiter(*this);
      continue;
    }
    TakeDueInterrupts();
  }
}

FileStamp Host::FileStampNow() const
{
  return StampOf(MomentAfter(m_startMoment, m_clock / MILLISECONDS_PER_CENTISECOND));
}

// ── The picture ──

void Host::PublishFlightSnapshot(const FlightSnapshot& _snapshot, bool _continues) noexcept
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

void Host::PublishSpaceViewPicture(const FlightSnapshot& _snapshot) noexcept
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

void Host::Render(FrameImage& _image) const noexcept
{
  m_cga.Render(m_state.VideoBytes(), _image);
}

} // namespace Elite
