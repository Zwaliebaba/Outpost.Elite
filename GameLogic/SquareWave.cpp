#include "pch.h"

#include "SquareWave.h"

#include <algorithm>

namespace Elite
{

namespace
{

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
  std::int64_t endTick = SquareWave::NEVER; // exclusive
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
[[nodiscard]] Region RegionAt(const SquareWave& _channel, std::int64_t _tick) noexcept
{
  if (!_channel.loaded || !_channel.gate)
  {
    return Constant(true, SquareWave::NEVER);
  }
  if (_tick < _channel.originTick)
  {
    return Constant(true, _channel.originTick);
  }
  return Wave(_channel.reload, _channel.originTick, SquareWave::NEVER);
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

bool SquareWave::OutputHigh(std::int64_t _tick) const noexcept
{
  // High with no count loaded and before the count is, and forced high by a low gate.
  if (!loaded || !gate || _tick < originTick)
  {
    return true;
  }
  return Phase(_tick, originTick, reload) < HighTicks(reload);
}

std::int64_t SquareWave::NextRisingEdge(std::int64_t _tick) const noexcept
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

double SquareWave::Integral(std::uint64_t _from, std::uint64_t _to, std::uint64_t _unitsPerTick,
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

std::uint32_t SquareWave::CountRegister() const noexcept
{
  if (!loaded)
  {
    return 0;
  }
  return reload;
}

} // namespace Elite
