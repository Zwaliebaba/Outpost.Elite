#include "pch.h"

#include "Speaker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Machine
{

namespace
{

// Rendering counts time in units of 1 / (CPU_CLOCK_DIVISOR * rate) of a cycle. In those units a
// cycle is CPU_CLOCK_DIVISOR * rate long and a sample exactly CRYSTAL_HZ, so every boundary, a
// cycle's, a timer tick's or a sample's, falls on a whole unit and nothing is rounded.
constexpr std::uint64_t UNITS_PER_SAMPLE = CRYSTAL_HZ;

constexpr std::uint8_t PORT_B_GATE = 0x01;
constexpr std::uint8_t PORT_B_DATA = 0x02;

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

Speaker::Speaker(const Cycles& _clock)
  : m_clock(_clock)
{
  Event initial;
  initial.at = m_clock;
  m_events.push_back(initial);
}

void Speaker::SetPortB(std::uint8_t _value)
{
  const bool gate = (_value & PORT_B_GATE) != 0;
  const bool data = (_value & PORT_B_DATA) != 0;
  const Event& last = m_events.back();
  if (last.gate == gate && last.data == data)
  {
    return;
  }
  Event event = last;
  event.at = m_clock;
  event.gate = gate;
  event.data = data;
  Append(event);
}

void Speaker::SetChannel2(const Pit::Channel& _channel)
{
  Event event = m_events.back();
  event.at = m_clock;
  event.channel2 = _channel;
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

std::size_t Speaker::SampleCount(Cycles _from, Cycles _to, std::uint32_t _sampleRate) noexcept
{
  if (_sampleRate == 0 || _to <= _from)
  {
    return 0;
  }
  const std::uint64_t unitsPerCycle = UnitsPerCycle(_sampleRate);
  return static_cast<std::size_t>(_to * unitsPerCycle / UNITS_PER_SAMPLE - _from * unitsPerCycle / UNITS_PER_SAMPLE);
}

Cycles Speaker::FirstSampleStart(Cycles _from, std::uint32_t _sampleRate) noexcept
{
  if (_sampleRate == 0)
  {
    return _from;
  }
  const std::uint64_t unitsPerCycle = UnitsPerCycle(_sampleRate);
  const std::uint64_t sample = _from * unitsPerCycle / UNITS_PER_SAMPLE;
  return sample * UNITS_PER_SAMPLE / unitsPerCycle;
}

std::size_t Speaker::RenderSamples(Cycles _from, Cycles _to, std::uint32_t _sampleRate, std::span<std::int16_t> _out) const
{
  const std::size_t count = std::min(SampleCount(_from, _to, _sampleRate), _out.size());
  if (count == 0)
  {
    return 0;
  }
  const std::uint64_t unitsPerCycle = UnitsPerCycle(_sampleRate);
  const std::uint64_t unitsPerTick = unitsPerCycle * CYCLES_PER_PIT_TICK;
  const std::uint32_t averageBelowTicks = AverageBelowTicks(_sampleRate);
  const std::uint64_t firstSample = _from * unitsPerCycle / UNITS_PER_SAMPLE;
  const auto startOf = [&](std::size_t _event) { return m_events[_event].at * unitsPerCycle; };

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
        area += state.channel2.Integral(position, end, unitsPerTick, averageBelowTicks);
      }
      position = end;
    }
    const double level = area / static_cast<double>(UNITS_PER_SAMPLE);
    _out[index] = static_cast<std::int16_t>(std::lround(level * FULL_LEVEL));
  }
  return count;
}

void Speaker::DiscardBefore(Cycles _cycle)
{
  std::size_t keep = 0;
  while (keep + 1 < m_events.size() && m_events[keep + 1].at <= _cycle)
  {
    ++keep;
  }
  m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(keep));
}

} // namespace Machine
