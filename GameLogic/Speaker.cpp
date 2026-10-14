#include "pch.h"

#include "Speaker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Elite
{

namespace
{

// The tone is the interval timer's channel 2, clocked as on the IBM PC: its own time base, private to the speaker until the synth
// replaces it. A millisecond of the platform's clock starts at cycle CyclesAt of an 8088 clocked at a third of the crystal, and a
// timer tick is four such cycles.
constexpr std::uint64_t CRYSTAL_HZ = 14'318'180;
constexpr std::uint64_t CPU_CLOCK_DIVISOR = 3;
constexpr std::uint64_t CYCLES_PER_PIT_TICK = 4;

[[nodiscard]] constexpr std::uint64_t CyclesAt(Milliseconds _time) noexcept
{
  return _time * CRYSTAL_HZ / (CPU_CLOCK_DIVISOR * 1'000);
}

[[nodiscard]] constexpr std::int64_t PitTickAt(Milliseconds _time) noexcept
{
  return static_cast<std::int64_t>(CyclesAt(_time) / CYCLES_PER_PIT_TICK);
}

// Rendering counts time in units of 1 / (CPU_CLOCK_DIVISOR * rate) of a cycle. In those units a
// cycle is CPU_CLOCK_DIVISOR * rate long and a sample exactly CRYSTAL_HZ, so every boundary, a
// cycle's, a timer tick's or a sample's, falls on a whole unit and nothing is rounded.
constexpr std::uint64_t UNITS_PER_SAMPLE = CRYSTAL_HZ;

// The BIOS's programming of the tone, which is its power-on state (Speaker.h).
constexpr std::uint16_t BEEP_DIVISOR = 0x0533;

[[nodiscard]] constexpr std::uint64_t UnitsPerCycle(std::uint32_t _sampleRate) noexcept
{
  return CPU_CLOCK_DIVISOR * _sampleRate;
}

// The shortest period, in timer ticks, that is rendered as a wave rather than as its mean. A wave
// is averaged when its fundamental, CRYSTAL_HZ / (12 * period), is above the limit of hearing or
// the output's Nyquist frequency, whichever is lower.
[[nodiscard]] std::uint32_t AverageBelowTicks(std::uint32_t _sampleRate) noexcept
{
  const std::uint64_t limitHz = std::min<std::uint64_t>(Speaker::AUDIBLE_LIMIT_HZ, _sampleRate / 2);
  if (limitHz == 0)
  {
    return std::numeric_limits<std::uint32_t>::max();
  }
  const std::uint64_t ticksHz = limitHz * CPU_CLOCK_DIVISOR * CYCLES_PER_PIT_TICK;
  return static_cast<std::uint32_t>((CRYSTAL_HZ + ticksHz - 1) / ticksHz); // period < CRYSTAL_HZ / ticksHz
}

} // namespace

Speaker::Speaker(const Milliseconds& _clock)
  : m_clock(_clock)
{
  Event initial;
  initial.at = m_clock;
  initial.tone.loaded = true;
  initial.tone.reload = BEEP_DIVISOR;
  initial.tone.gate = false;
  m_events.push_back(initial);
}

void Speaker::SetBits(bool _gate, bool _data)
{
  const Event& last = m_events.back();
  if (last.gate == _gate && last.data == _data)
  {
    return;
  }
  Event event = last;
  event.at = m_clock;
  if (event.tone.gate != _gate)
  {
    event.tone.gate = _gate;
    if (_gate && event.tone.loaded)
    {
      event.tone.reload = event.tone.CountRegister();
      event.tone.originTick = PitTickAt(m_clock) + 1;
    }
  }
  event.gate = _gate;
  event.data = _data;
  Append(event);
}

void Speaker::SetTone(std::uint16_t _divisor)
{
  // The control word, which leaves the tone unloaded until its count, then the count: both at one cycle, so one event.
  Event event = m_events.back();
  event.at = m_clock;
  event.tone.reload = SquareWave::Reload(_divisor);
  event.tone.originTick = PitTickAt(m_clock) + 1;
  event.tone.loaded = true;
  Append(event);
}

void Speaker::Append(const Event& _event)
{
  if (m_events.back().at >= _event.at)
  {
    m_events.back() = _event; // the same cycle: the later change is the one in force
    return;
  }
  m_events.push_back(_event);
}

bool Speaker::ToneOutputAt(Milliseconds _time) const noexcept
{
  return m_events.back().tone.OutputHigh(PitTickAt(_time));
}

std::size_t Speaker::SampleCount(Milliseconds _from, Milliseconds _to, std::uint32_t _sampleRate) noexcept
{
  if (_sampleRate == 0 || _to <= _from)
  {
    return 0;
  }
  const std::uint64_t unitsPerCycle = UnitsPerCycle(_sampleRate);
  return static_cast<std::size_t>(CyclesAt(_to) * unitsPerCycle / UNITS_PER_SAMPLE - CyclesAt(_from) * unitsPerCycle / UNITS_PER_SAMPLE);
}

Milliseconds Speaker::FirstSampleStart(Milliseconds _from, std::uint32_t _sampleRate) noexcept
{
  if (_sampleRate == 0)
  {
    return _from;
  }
  const std::uint64_t unitsPerCycle = UnitsPerCycle(_sampleRate);
  const std::uint64_t sample = CyclesAt(_from) * unitsPerCycle / UNITS_PER_SAMPLE;
  const std::uint64_t cycle = sample * UNITS_PER_SAMPLE / unitsPerCycle;
  return cycle * CPU_CLOCK_DIVISOR * 1'000 / CRYSTAL_HZ;
}

std::size_t Speaker::RenderSamples(Milliseconds _from, Milliseconds _to, std::uint32_t _sampleRate, std::span<std::int16_t> _out) const
{
  const std::size_t count = std::min(SampleCount(_from, _to, _sampleRate), _out.size());
  if (count == 0)
  {
    return 0;
  }
  const std::uint64_t unitsPerCycle = UnitsPerCycle(_sampleRate);
  const std::uint64_t unitsPerTick = unitsPerCycle * CYCLES_PER_PIT_TICK;
  const std::uint32_t averageBelowTicks = AverageBelowTicks(_sampleRate);
  const std::uint64_t firstSample = CyclesAt(_from) * unitsPerCycle / UNITS_PER_SAMPLE;
  const auto startOf = [&](std::size_t _event) { return CyclesAt(m_events[_event].at) * unitsPerCycle; };

  // The event in force at the first sample's start (the first event, if the log begins later).
  std::size_t current = 0;
  for (std::size_t index = 0; index < count; ++index)
  {
    const std::uint64_t sampleStart = (firstSample + index) * UNITS_PER_SAMPLE;
    const std::uint64_t sampleEnd = sampleStart + UNITS_PER_SAMPLE;
    while (current + 1 < m_events.size() && startOf(current + 1) <= sampleStart)
    {
      ++current;
    }

    double area = 0.0;
    std::uint64_t position = sampleStart;
    for (std::size_t event = current; position < sampleEnd; ++event)
    {
      const std::uint64_t end = event + 1 < m_events.size() ? std::min(sampleEnd, startOf(event + 1)) : sampleEnd;
      const Event& state = m_events[event];
      if (state.data && !state.gate)
      {
        area += static_cast<double>(end - position);
      }
      else if (state.data)
      {
        area += state.tone.Integral(position, end, unitsPerTick, averageBelowTicks);
      }
      position = end;
    }
    const double level = area / static_cast<double>(UNITS_PER_SAMPLE);
    _out[index] = static_cast<std::int16_t>(std::lround(level * FULL_LEVEL));
  }
  return count;
}

void Speaker::DiscardBefore(Milliseconds _time)
{
  std::size_t keep = 0;
  while (keep + 1 < m_events.size() && m_events[keep + 1].at <= _time)
  {
    ++keep;
  }
  m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(keep));
}

} // namespace Elite
