#include "pch.h"

#include "Speaker.h"

#include <algorithm>
#include <cmath>

namespace Elite
{

namespace
{

// The BIOS's programming of the tone, which is its power-on state (Speaker.h).
constexpr std::uint16_t BEEP_DIVISOR = 0x0533;
constexpr std::uint32_t FULL_DIVISOR = 0x10000;
constexpr std::uint64_t MILLISECONDS_PER_SECOND = 1'000;

// The first sample whose interval ends after _time: the one _time falls in.
[[nodiscard]] constexpr std::uint64_t SampleAt(Milliseconds _time, std::uint32_t _sampleRate) noexcept
{
  return _time * _sampleRate / MILLISECONDS_PER_SECOND;
}

// The time-integral, in periods, of a square wave high for the first half of each period, from phase 0 to phase _phase.
[[nodiscard]] double HighPhase(double _phase) noexcept
{
  const double periods = std::floor(_phase);
  return periods * 0.5 + std::min(_phase - periods, 0.5);
}

} // namespace

Speaker::Speaker(const Milliseconds& _clock)
  : m_clock(_clock)
{
  Event initial;
  initial.at = m_clock;
  initial.toneHz = ToneHz(BEEP_DIVISOR);
  initial.toneStart = m_clock;
  m_events.push_back(initial);
}

double Speaker::ToneHz(std::uint16_t _divisor) noexcept
{
  return TIMER_HZ / static_cast<double>(_divisor == 0 ? FULL_DIVISOR : _divisor);
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
  if (_gate && !last.gate)
  {
    event.toneStart = m_clock;
  }
  event.gate = _gate;
  event.data = _data;
  Append(event);
}

void Speaker::SetTone(std::uint16_t _divisor)
{
  Event event = m_events.back();
  event.at = m_clock;
  event.toneHz = ToneHz(_divisor);
  event.toneStart = m_clock;
  Append(event);
}

void Speaker::Append(const Event& _event)
{
  if (m_events.back().at >= _event.at)
  {
    m_events.back() = _event; // the same millisecond: the later change is the one in force
    return;
  }
  m_events.push_back(_event);
}

std::size_t Speaker::SampleCount(Milliseconds _from, Milliseconds _to, std::uint32_t _sampleRate) noexcept
{
  if (_sampleRate == 0 || _to <= _from)
  {
    return 0;
  }
  return static_cast<std::size_t>(SampleAt(_to, _sampleRate) - SampleAt(_from, _sampleRate));
}

Milliseconds Speaker::FirstSampleStart(Milliseconds _from, std::uint32_t _sampleRate) noexcept
{
  if (_sampleRate == 0)
  {
    return _from;
  }
  return SampleAt(_from, _sampleRate) * MILLISECONDS_PER_SECOND / _sampleRate;
}

std::size_t Speaker::RenderSamples(Milliseconds _from, Milliseconds _to, std::uint32_t _sampleRate, std::span<std::int16_t> _out) const
{
  const std::size_t count = std::min(SampleCount(_from, _to, _sampleRate), _out.size());
  if (count == 0)
  {
    return 0;
  }
  const double rate = _sampleRate;
  const double audibleHz = std::min<double>(AUDIBLE_LIMIT_HZ, rate / 2.0);
  const std::uint64_t firstSample = SampleAt(_from, _sampleRate);
  const auto seconds = [](Milliseconds _time) { return static_cast<double>(_time) / MILLISECONDS_PER_SECOND; };

  // The event in force at the first sample's start (the first event, if the log begins later).
  std::size_t current = 0;
  for (std::size_t index = 0; index < count; ++index)
  {
    const std::uint64_t sample = firstSample + index;
    const double sampleStart = static_cast<double>(sample) / rate;
    const double sampleEnd = static_cast<double>(sample + 1) / rate;
    while (current + 1 < m_events.size() && seconds(m_events[current + 1].at) <= sampleStart)
    {
      ++current;
    }

    double high = 0.0; // seconds of the sample at level 1
    double position = sampleStart;
    for (std::size_t event = current; position < sampleEnd; ++event)
    {
      const double end = event + 1 < m_events.size() ? std::min(sampleEnd, seconds(m_events[event + 1].at)) : sampleEnd;
      const Event& state = m_events[event];
      if (state.data && !state.gate)
      {
        high += end - position;
      }
      else if (state.data && state.toneHz > audibleHz)
      {
        high += (end - position) * 0.5;
      }
      else if (state.data)
      {
        const double start = seconds(state.toneStart);
        high += (HighPhase((end - start) * state.toneHz) - HighPhase((position - start) * state.toneHz)) / state.toneHz;
      }
      position = std::max(position, end);
    }
    _out[index] = static_cast<std::int16_t>(std::lround(high * rate * FULL_LEVEL));
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
