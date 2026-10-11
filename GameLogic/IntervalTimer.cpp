#include "pch.h"

#include "IntervalTimer.h"

#include "InterruptController.h"
#include "Speaker.h"

#include <algorithm>
#include <stdexcept>

namespace Elite
{

namespace
{

// A count of 0 is the largest: 65536 ticks.
constexpr std::uint32_t FULL_COUNT = 0x10000;

constexpr std::uint8_t SELECT_SHIFT = 6;
constexpr std::uint8_t READ_BACK = 3;
constexpr std::uint8_t ACCESS_SHIFT = 4;
constexpr std::uint8_t ACCESS_MASK = 3;
constexpr std::uint8_t ACCESS_BOTH_BYTES = 3;
constexpr std::uint8_t MODE_MASK = 7;
constexpr std::uint8_t SQUARE_WAVE = 3;
constexpr std::uint8_t SQUARE_WAVE_TOO = 7; // the 8253 takes 7 as 3

// The BIOS's programming, which is this timer's power-on state (IntervalTimer.h).
constexpr std::uint32_t BEEP_DIVISOR = 0x0533;

// Mode 3: the output is high for this many ticks at the start of each period, then low.
[[nodiscard]] constexpr std::uint32_t HighTicks(std::uint32_t _reload) noexcept
{
  return (_reload + 1) / 2;
}

// The position in the period of tick _tick, for _tick >= _origin.
[[nodiscard]] constexpr std::uint32_t Phase(std::int64_t _tick, std::int64_t _origin, std::uint32_t _reload) noexcept
{
  return static_cast<std::uint32_t>((_tick - _origin) % _reload);
}

// A stretch of ticks over which the output is either constant or one periodic wave.
struct Region
{
  bool periodic = false;
  bool high = false; // constant regions
  std::uint32_t reload = 0;
  std::int64_t origin = 0;
  std::int64_t endTick = IntervalTimer::NEVER; // exclusive
};

[[nodiscard]] Region Constant(bool _high, std::int64_t _endTick) noexcept
{
  Region region;
  region.high = _high;
  region.endTick = _endTick;
  return region;
}

[[nodiscard]] Region Wave(std::uint32_t _reload, std::int64_t _origin, std::int64_t _endTick) noexcept
{
  Region region;
  region.periodic = true;
  region.reload = _reload;
  region.origin = _origin;
  region.endTick = _endTick;
  return region;
}

// The region containing tick _tick.
[[nodiscard]] Region RegionAt(const IntervalTimer::Channel& _channel, std::int64_t _tick) noexcept
{
  if (!_channel.loaded || !_channel.gate)
  {
    return Constant(true, IntervalTimer::NEVER);
  }
  if (_tick < _channel.originTick)
  {
    return Constant(true, _channel.originTick);
  }
  return Wave(_channel.reload, _channel.originTick, IntervalTimer::NEVER);
}

// The exact integral of a periodic region's output over [_from, _to), both inside the region.
[[nodiscard]] std::uint64_t WaveIntegral(const Region& _region, std::uint64_t _from, std::uint64_t _to,
                                         std::uint64_t _unitsPerTick) noexcept
{
  const std::uint32_t high = HighTicks(_region.reload);
  const auto isHigh = [&](std::int64_t _tick) { return Phase(_tick, _region.origin, _region.reload) < high; };
  // High ticks in [origin, _tick).
  const auto highBefore = [&](std::int64_t _tick)
  {
    const std::int64_t elapsed = _tick - _region.origin;
    return (elapsed / _region.reload) * high + std::min<std::int64_t>(elapsed % _region.reload, high);
  };

  const auto first = static_cast<std::int64_t>(_from / _unitsPerTick);
  const auto last = static_cast<std::int64_t>(_to / _unitsPerTick);
  if (first == last)
  {
    return isHigh(first) ? _to - _from : 0;
  }
  const std::uint64_t firstEnd = static_cast<std::uint64_t>(first + 1) * _unitsPerTick;
  const std::uint64_t lastStart = static_cast<std::uint64_t>(last) * _unitsPerTick;
  std::uint64_t sum = isHigh(first) ? firstEnd - _from : 0;
  sum += static_cast<std::uint64_t>(highBefore(last) - highBefore(first + 1)) * _unitsPerTick;
  if (_to > lastStart && isHigh(last))
  {
    sum += _to - lastStart;
  }
  return sum;
}

} // namespace

// ── Channel ─────────────────────────────────────────────────────────────────────────────────

bool IntervalTimer::Channel::OutputHigh(std::int64_t _tick) const noexcept
{
  // High with no count loaded and before the count is, and forced high by a low gate.
  if (!loaded || !gate || _tick < originTick)
  {
    return true;
  }
  return Phase(_tick, originTick, reload) < HighTicks(reload);
}

std::int64_t IntervalTimer::Channel::NextRisingEdge(std::int64_t _tick) const noexcept
{
  if (!loaded || !gate)
  {
    return NEVER;
  }
  // The output rises at the end of every period: origin + k * reload, k >= 1. It is already high before the first, so
  // loading a count is not itself an edge.
  const auto firstEdgeAfter = [](std::uint32_t _reload, std::int64_t _origin, std::int64_t _after)
  {
    const std::uint32_t high = HighTicks(_reload);
    if (high == 0 || high >= _reload)
    {
      return NEVER; // never low, or never high: no edges
    }
    if (_after < _origin)
    {
      return _origin + _reload;
    }
    return _origin + ((_after - _origin) / _reload + 1) * _reload;
  };
  return firstEdgeAfter(reload, originTick, _tick);
}

double IntervalTimer::Channel::Integral(std::uint64_t _from, std::uint64_t _to, std::uint64_t _unitsPerTick,
                                        std::uint32_t _averageBelowTicks) const noexcept
{
  double total = 0.0;
  std::uint64_t position = _from;
  while (position < _to)
  {
    const Region region = RegionAt(*this, static_cast<std::int64_t>(position / _unitsPerTick));
    std::uint64_t end = _to;
    if (region.endTick != NEVER)
    {
      end = std::min(end, static_cast<std::uint64_t>(region.endTick) * _unitsPerTick);
    }
    if (!region.periodic)
    {
      total += region.high ? static_cast<double>(end - position) : 0.0;
    }
    else if (region.reload < _averageBelowTicks)
    {
      total += static_cast<double>(end - position) * HighTicks(region.reload) / region.reload;
    }
    else
    {
      total += static_cast<double>(WaveIntegral(region, position, end, _unitsPerTick));
    }
    position = end;
  }
  return total;
}

std::uint32_t IntervalTimer::Channel::CountRegister() const noexcept
{
  if (!loaded)
  {
    return 0;
  }
  return reload;
}

// ── IntervalTimer ───────────────────────────────────────────────────────────────────────────

IntervalTimer::IntervalTimer(const Cycles& _clock, InterruptController& _controller, Speaker& _speaker)
  : m_clock(_clock),
    m_controller(_controller),
    m_speaker(_speaker)
{
  Channel& timer = m_channels[0];
  timer.loaded = true;
  timer.reload = FULL_COUNT;

  Channel& tone = m_channels[2];
  tone.loaded = true;
  tone.reload = BEEP_DIVISOR;
  tone.gate = false;

  m_nextEdgeTick = timer.NextRisingEdge(Now());
  m_speaker.SetChannel2(tone);
}

std::int64_t IntervalTimer::Now() const noexcept
{
  return TickAt(m_clock);
}

void IntervalTimer::Advance() noexcept
{
  const std::int64_t now = Now();
  if (now < m_nextEdgeTick)
  {
    return;
  }
  m_controller.RaiseIrq(0);
  m_nextEdgeTick = m_channels[0].NextRisingEdge(now);
}

Cycles IntervalTimer::NextInterruptAt() const noexcept
{
  if (m_nextEdgeTick == NEVER)
  {
    return NO_EVENT;
  }
  return static_cast<Cycles>(m_nextEdgeTick) * CYCLES_PER_PIT_TICK;
}

bool IntervalTimer::OutputAt(std::size_t _channel, Cycles _cycle) const noexcept
{
  return m_channels[_channel].OutputHigh(TickAt(_cycle));
}

void IntervalTimer::Program(std::uint8_t _control, std::uint16_t _divisor)
{
  const std::size_t channel = _control >> SELECT_SHIFT;
  const auto mode = static_cast<std::uint8_t>((_control >> 1) & MODE_MASK);
  if (channel == READ_BACK || ((_control >> ACCESS_SHIFT) & ACCESS_MASK) != ACCESS_BOTH_BYTES ||
      (mode != SQUARE_WAVE && mode != SQUARE_WAVE_TOO))
  {
    throw std::invalid_argument("IntervalTimer::Program: the game programs a square wave by both bytes of its count");
  }
  // Channel 0's edges up to now count under its old programming first. The three writes come at one cycle, so the edges are
  // accounted once.
  Advance();
  WriteControl(channel);
  LoadCount(channel, _divisor);
}

void IntervalTimer::WriteControl(std::size_t _channel)
{
  Channel& channel = m_channels[_channel];
  const bool wasHigh = channel.OutputHigh(Now());
  channel.loaded = false;
  CheckWriteEdge(_channel, wasHigh);
  Changed(_channel);
}

void IntervalTimer::LoadCount(std::size_t _channel, std::uint32_t _count)
{
  Channel& channel = m_channels[_channel];
  const std::int64_t now = Now();
  const std::uint32_t reload = _count == 0 ? FULL_COUNT : _count;
  const bool wasHigh = channel.OutputHigh(now);
  channel.reload = reload;
  channel.originTick = now + 1; // loaded on the next tick
  channel.loaded = true;
  CheckWriteEdge(_channel, wasHigh);
  Changed(_channel);
}

void IntervalTimer::SetChannel2Gate(bool _high)
{
  Channel& channel = m_channels[2];
  if (channel.gate == _high)
  {
    return;
  }
  channel.gate = _high;
  if (_high && channel.loaded)
  {
    // A rising gate reloads the counter from the count register on the next tick.
    channel.reload = channel.CountRegister();
    channel.originTick = Now() + 1;
  }
  Changed(2);
}

void IntervalTimer::CheckWriteEdge(std::size_t _channel, bool _wasHigh) noexcept
{
  if (_channel != 0)
  {
    return;
  }
  const std::int64_t now = Now();
  if (!_wasHigh && m_channels[0].OutputHigh(now))
  {
    m_controller.RaiseIrq(0); // a control word sets a low output high at once, and that is an edge
  }
  m_nextEdgeTick = m_channels[0].NextRisingEdge(now);
}

void IntervalTimer::Changed(std::size_t _channel)
{
  if (_channel == 2)
  {
    m_speaker.SetChannel2(m_channels[2]);
  }
}

} // namespace Elite
