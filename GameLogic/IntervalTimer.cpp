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

// The BIOS's programming, which is this timer's power-on state (IntervalTimer.h).
constexpr std::uint32_t REFRESH_DIVISOR = 18;
constexpr std::uint32_t BEEP_DIVISOR = 0x0533;

[[nodiscard]] constexpr bool Periodic(std::uint8_t _mode) noexcept
{
  return _mode == 2 || _mode == 3;
}

// The output's level before a count is loaded, and after a control word: low in mode 0, high otherwise.
[[nodiscard]] constexpr bool IdleHigh(std::uint8_t _mode) noexcept
{
  return _mode != 0;
}

// Modes 2 and 3: the output is high for this many ticks at the start of each period, then low.
[[nodiscard]] constexpr std::uint32_t HighTicks(std::uint8_t _mode, std::uint32_t _reload) noexcept
{
  return _mode == 2 ? _reload - 1 : (_reload + 1) / 2;
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
  if (!_channel.loaded)
  {
    return Constant(IdleHigh(_channel.mode), IntervalTimer::NEVER);
  }
  if (!_channel.gate)
  {
    // Frozen: valid from the moment the gate fell, which is all a snapshot is asked about.
    return Constant(_channel.OutputHigh(_tick), IntervalTimer::NEVER);
  }
  if (_tick < _channel.originTick)
  {
    return Constant(IdleHigh(_channel.mode), _channel.originTick);
  }
  if (_channel.mode == 0)
  {
    const std::int64_t terminal = _channel.originTick + _channel.reload;
    return _tick < terminal ? Constant(false, terminal) : Constant(true, IntervalTimer::NEVER);
  }
  if (!Periodic(_channel.mode))
  {
    return Constant(true, IntervalTimer::NEVER);
  }
  if (_tick < _channel.switchTick)
  {
    return Wave(_channel.reload, _channel.originTick, _channel.switchTick);
  }
  return Wave(_channel.nextReload, _channel.nextOriginTick, IntervalTimer::NEVER);
}

// The exact integral of a periodic region's output over [_from, _to), both inside the region.
[[nodiscard]] std::uint64_t WaveIntegral(std::uint8_t _mode, const Region& _region, std::uint64_t _from, std::uint64_t _to,
                                         std::uint64_t _unitsPerTick) noexcept
{
  const std::uint32_t high = HighTicks(_mode, _region.reload);
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
  if (!loaded)
  {
    return IdleHigh(mode);
  }
  std::int64_t tick = _tick;
  if (!gate)
  {
    if (Periodic(mode))
    {
      return true; // a low gate forces the output high in modes 2 and 3
    }
    tick = std::min(tick, std::max(pausedTick, originTick));
  }
  if (tick < originTick)
  {
    return IdleHigh(mode);
  }
  if (mode == 0)
  {
    return tick - originTick >= reload;
  }
  if (!Periodic(mode))
  {
    return true;
  }
  const bool next = tick >= switchTick;
  const std::uint32_t period = next ? nextReload : reload;
  return Phase(tick, next ? nextOriginTick : originTick, period) < HighTicks(mode, period);
}

std::int64_t IntervalTimer::Channel::NextRisingEdge(std::int64_t _tick) const noexcept
{
  if (!loaded || !gate)
  {
    return NEVER;
  }
  if (mode == 0)
  {
    const std::int64_t terminal = originTick + reload;
    return terminal > _tick ? terminal : NEVER;
  }
  if (!Periodic(mode))
  {
    return NEVER;
  }
  // Modes 2 and 3 rise at the end of every period: origin + k * reload, k >= 1. The output is already high before the first,
  // so loading a count is not itself an edge.
  const auto firstEdgeAfter = [this](std::uint32_t _reload, std::int64_t _origin, std::int64_t _after)
  {
    const std::uint32_t high = HighTicks(mode, _reload);
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
  const std::int64_t edge = firstEdgeAfter(reload, originTick, _tick);
  if (edge != NEVER && edge <= switchTick)
  {
    return edge;
  }
  if (switchTick == NEVER)
  {
    return NEVER;
  }
  return firstEdgeAfter(nextReload, nextOriginTick, std::max(_tick, switchTick));
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
      total += static_cast<double>(end - position) * HighTicks(mode, region.reload) / region.reload;
    }
    else
    {
      total += static_cast<double>(WaveIntegral(mode, region, position, end, _unitsPerTick));
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
  return switchTick != NEVER ? nextReload : reload;
}

// ── IntervalTimer ───────────────────────────────────────────────────────────────────────────

IntervalTimer::IntervalTimer(const Cycles& _clock, InterruptController& _controller, Speaker& _speaker)
  : m_clock(_clock),
    m_controller(_controller),
    m_speaker(_speaker)
{
  Channel& timer = m_channels[0];
  timer.mode = 3;
  timer.loaded = true;
  timer.reload = FULL_COUNT;

  Channel& refresh = m_channels[1];
  refresh.mode = 2;
  refresh.loaded = true;
  refresh.reload = REFRESH_DIVISOR;

  Channel& tone = m_channels[2];
  tone.mode = 3;
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
  if (channel == READ_BACK || ((_control >> ACCESS_SHIFT) & ACCESS_MASK) != ACCESS_BOTH_BYTES)
  {
    throw std::invalid_argument("IntervalTimer::Program: the game programs a channel's count by both its bytes");
  }
  // Channel 0's edges up to now count under its old programming first. The three writes come at one cycle, so the edges are
  // accounted once.
  Advance();
  WriteControl(channel, static_cast<std::uint8_t>((_control >> 1) & MODE_MASK));
  LoadCount(channel, _divisor);
}

void IntervalTimer::WriteControl(std::size_t _channel, std::uint8_t _mode)
{
  Channel& channel = m_channels[_channel];
  const bool wasHigh = channel.OutputHigh(Now());
  channel.mode = _mode >= 6 ? static_cast<std::uint8_t>(_mode - 4) : _mode; // 6 and 7 are 2 and 3
  channel.loaded = false;
  channel.switchTick = NEVER;
  CheckWriteEdge(_channel, wasHigh);
  Changed(_channel);
}

void IntervalTimer::LoadCount(std::size_t _channel, std::uint32_t _count)
{
  Channel& channel = m_channels[_channel];
  const std::int64_t now = Now();
  const std::uint32_t reload = _count == 0 ? FULL_COUNT : _count;
  const bool wasHigh = channel.OutputHigh(now);

  if (channel.switchTick != NEVER && now >= channel.switchTick)
  {
    channel.reload = channel.nextReload;
    channel.originTick = channel.nextOriginTick;
    channel.switchTick = NEVER;
  }
  const bool running = channel.loaded && channel.gate && now >= channel.originTick && Periodic(channel.mode);
  if (running)
  {
    // Modes 2 and 3 pick a new count up at the end of the period or half-period in progress.
    const std::uint32_t phase = Phase(now, channel.originTick, channel.reload);
    const std::uint32_t high = HighTicks(channel.mode, channel.reload);
    if (channel.mode == 3 && phase < high)
    {
      channel.switchTick = now + (high - phase);
      channel.nextOriginTick = channel.switchTick - HighTicks(channel.mode, reload); // it starts in its low half
    }
    else
    {
      channel.switchTick = now + (channel.reload - phase);
      channel.nextOriginTick = channel.switchTick;
    }
    channel.nextReload = reload;
  }
  else
  {
    channel.reload = reload;
    channel.originTick = now + 1; // loaded on the next tick
    channel.loaded = true;
    channel.switchTick = NEVER;
  }
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
  const std::int64_t now = Now();
  if (channel.switchTick != NEVER && now >= channel.switchTick)
  {
    channel.reload = channel.nextReload;
    channel.originTick = channel.nextOriginTick;
    channel.switchTick = NEVER;
  }
  if (!_high)
  {
    channel.gate = false;
    channel.pausedTick = now; // the ticks after this one are not counted
  }
  else if (channel.mode == 0 || channel.mode == 4)
  {
    // Counting resumes where it stopped: the ticks spent waiting shift the origin.
    channel.gate = true;
    const std::int64_t frozen = std::max(channel.pausedTick, channel.originTick);
    if (now > frozen)
    {
      channel.originTick += now - frozen;
    }
  }
  else
  {
    // Modes 1, 2, 3 and 5: a rising gate reloads the counter from the count register on the next tick.
    channel.gate = true;
    if (channel.loaded)
    {
      channel.reload = channel.CountRegister();
      channel.originTick = now + 1;
      channel.switchTick = NEVER;
    }
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
