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

// The speaker's two bits: bit 0 the tone's gate, bit 1 its data.
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
    m_cga(m_clock),
    m_mouse(_desc.mousePresent),
    m_files(_files),
    m_startMoment(_desc.startMoment),
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

void Host::SetTickHandler(TickRoutine _routine)
{
  if (m_tickHandler.routine)
  {
    throw std::logic_error("Host: the game has a tick handler already");
  }
  m_tickHandler.routine = std::move(_routine);
}

void Host::SetKeyHandler(KeyRoutine _routine)
{
  if (m_keyHandler.routine)
  {
    throw std::logic_error("Host: the game has a key handler already");
  }
  m_keyHandler.routine = std::move(_routine);
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

// ── The tick, the keys and the speaker ──

void Host::SetTickRunning(bool _running) noexcept
{
  m_nextTickAt = _running ? m_clock + TICK_MILLISECONDS : NO_EVENT;
}

void Host::Inject(const KeyEvent& _event)
{
  m_keys.push_back(_event);
  if (m_nextKeyAt == NO_EVENT)
  {
    m_nextKeyAt = m_clock + 1;
  }
}

void Host::SetSpeakerBits(std::uint8_t _bits)
{
  const auto bits = static_cast<std::uint8_t>(_bits & SPEAKER_BITS);
  if (bits != m_speakerBits)
  {
    m_speakerBits = bits;
    m_speaker.SetBits((bits & TONE_GATE) != 0, (bits & SPEAKER_DATA) != 0);
  }
}

// ── Paced time ──

void Host::Idle(Milliseconds _limit)
{
  Milliseconds next = std::min({m_nextTickAt, m_nextKeyAt, m_cga.NextStatusChangeAt(), _limit});
  if (next <= m_clock)
  {
    next = m_clock + 1;
  }
  m_clock = next;
  m_stepsSinceIdle = 0;
  if (m_clock >= m_nextTickAt)
  {
    m_tickDue = true;
    m_nextTickAt = m_clock + TICK_MILLISECONDS;
  }
  if (m_clock >= m_nextKeyAt && !m_keyDue)
  {
    m_keyDue = m_keys.front();
    m_keys.pop_front();
    m_nextKeyAt = m_keys.empty() ? NO_EVENT : m_clock + 1;
  }
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
  if (Stopped() != StopReason::Reached)
  {
    throw ProgramStopped{};
  }
}

// After a wait, and the game resumed if the wait reached the run's end: the tick's work, then a key (D91).
void Host::RunDue()
{
  if (m_tickDue)
  {
    m_tickDue = false;
    ++m_ticksRun;
    if (m_tickHandler.routine)
    {
      ++m_tickHandler.calls;
      m_tickHandler.routine();
    }
  }
  if (m_keyDue)
  {
    const KeyEvent key = *m_keyDue;
    m_keyDue.reset();
    if (m_keysToGame && m_keyHandler.routine)
    {
      ++m_keyHandler.calls;
      m_keyHandler.routine(key);
    }
  }
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
    RunDue();
  }
}

FileStamp Host::FileStampNow() const
{
  return StampOf(MomentAfter(m_startMoment, m_clock / MILLISECONDS_PER_CENTISECOND));
}

// ── The picture ──

void Host::ShowMarket(MarketView _view)
{
  _view.sequence = m_marketView.sequence + 1;
  m_marketView = std::move(_view);
  m_marketShown = true;
}

void Host::HideMarket() noexcept
{
  m_marketShown = false;
  m_clicks.clear();
}

void Host::ShowStatus(StatusView _view)
{
  _view.sequence = m_statusView.sequence + 1;
  m_statusView = std::move(_view);
  m_statusShown = true;
}

void Host::HideStatus() noexcept
{
  m_statusShown = false;
}

void Host::Click(const MarketClick& _click)
{
  if (m_marketShown)
  {
    m_clicks.push_back(_click);
  }
}

std::optional<MarketClick> Host::TakeClick() noexcept
{
  if (m_clicks.empty())
  {
    return std::nullopt;
  }
  const MarketClick click = m_clicks.front();
  m_clicks.pop_front();
  return click;
}

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
