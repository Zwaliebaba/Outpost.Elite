#include "pch.h"

#include "Pit.h"

#include "Pic.h"
#include "Speaker.h"

#include <algorithm>

namespace Machine
{

namespace
{

// A count of 0 is the largest: 65536 ticks.
constexpr std::uint32_t FULL_COUNT = 0x10000;

constexpr std::size_t CONTROL_INDEX = 3;
constexpr std::uint8_t SELECT_SHIFT = 6;
constexpr std::uint8_t ACCESS_SHIFT = 4;
constexpr std::uint8_t ACCESS_MASK = 3;
constexpr std::uint8_t ACCESS_LATCH = 0;
constexpr std::uint8_t ACCESS_LOW = 1;
constexpr std::uint8_t ACCESS_HIGH = 2;
constexpr std::uint8_t MODE_MASK = 7;
constexpr std::uint8_t BYTE_MASK = 0xFF;
constexpr std::uint8_t OPEN_BUS = 0xFF;

// The BIOS's programming, which is this PIT's power-on state (Pit.h).
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
  std::int64_t endTick = Pit::NEVER; // exclusive
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
[[nodiscard]] Region RegionAt(const Pit::Channel& _channel, std::int64_t _tick) noexcept
{
  if (!_channel.loaded)
  {
    return Constant(IdleHigh(_channel.mode), Pit::NEVER);
  }
  if (!_channel.gate)
  {
    // Frozen: valid from the moment the gate fell, which is all a snapshot is asked about.
    return Constant(_channel.OutputHigh(_tick), Pit::NEVER);
  }
  if (_tick < _channel.originTick)
  {
    return Constant(IdleHigh(_channel.mode), _channel.originTick);
  }
  if (_channel.mode == 0)
  {
    const std::int64_t terminal = _channel.originTick + _channel.reload;
    return _tick < terminal ? Constant(false, terminal) : Constant(true, Pit::NEVER);
  }
  if (!Periodic(_channel.mode))
  {
    return Constant(true, Pit::NEVER);
  }
  if (_tick < _channel.switchTick)
  {
    return Wave(_channel.reload, _channel.originTick, _channel.switchTick);
  }
  return Wave(_channel.nextReload, _channel.nextOriginTick, Pit::NEVER);
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

bool Pit::Channel::OutputHigh(std::int64_t _tick) const noexcept
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

std::uint16_t Pit::Channel::Count(std::int64_t _tick) const noexcept
{
  if (!loaded)
  {
    return heldCount;
  }
  std::int64_t tick = _tick;
  if (!gate)
  {
    tick = std::min(tick, std::max(pausedTick, originTick));
  }
  if (tick < originTick)
  {
    return heldCount;
  }
  if (!Periodic(mode))
  {
    // Modes 0, 1, 4 and 5 count down by one a tick and wrap through 0 to FFFF.
    return static_cast<std::uint16_t>((static_cast<std::uint64_t>(reload) - static_cast<std::uint64_t>(tick - originTick)) & 0xFFFFu);
  }
  const bool next = tick >= switchTick;
  const std::uint32_t period = next ? nextReload : reload;
  const std::uint32_t phase = Phase(tick, next ? nextOriginTick : originTick, period);
  if (mode == 2)
  {
    return static_cast<std::uint16_t>((period - phase) & 0xFFFFu); // N, N-1 ... 1
  }
  if (period % 2 == 0)
  {
    // Mode 3, even: N, N-2 ... 2 in each half.
    return static_cast<std::uint16_t>((period - 2 * (phase % (period / 2))) & 0xFFFFu);
  }
  // Mode 3, odd: N-1, N-3 ... 0 in the high half, N-1 ... 2 in the low half.
  const std::uint32_t high = HighTicks(mode, period);
  const std::uint32_t step = phase < high ? phase : phase - high;
  return static_cast<std::uint16_t>((period - 1 - 2 * step) & 0xFFFFu);
}

std::int64_t Pit::Channel::NextRisingEdge(std::int64_t _tick) const noexcept
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
  // Modes 2 and 3 rise at the end of every period: origin + k * reload, k >= 1. The output is
  // already high before the first, so loading a count is not itself an edge.
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

double Pit::Channel::Integral(std::uint64_t _from, std::uint64_t _to, std::uint64_t _unitsPerTick,
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

std::uint32_t Pit::Channel::CountRegister() const noexcept
{
  if (!loaded)
  {
    return 0;
  }
  return switchTick != NEVER ? nextReload : reload;
}

// ── Pit ─────────────────────────────────────────────────────────────────────────────────────

Pit::Pit(const Cycles& _clock, Pic& _pic, Speaker& _speaker)
  : m_clock(_clock),
    m_pic(_pic),
    m_speaker(_speaker)
{
  Channel& timer = m_channels[0];
  timer.mode = 3;
  timer.access = 3;
  timer.loaded = true;
  timer.reload = FULL_COUNT;

  Channel& refresh = m_channels[1];
  refresh.mode = 2;
  refresh.access = ACCESS_LOW;
  refresh.loaded = true;
  refresh.reload = REFRESH_DIVISOR;

  Channel& tone = m_channels[2];
  tone.mode = 3;
  tone.access = 3;
  tone.loaded = true;
  tone.reload = BEEP_DIVISOR;
  tone.gate = false;

  m_nextEdgeTick = timer.NextRisingEdge(Now());
  m_speaker.SetChannel2(tone);
}

std::int64_t Pit::Now() const noexcept
{
  return TickAt(m_clock);
}

void Pit::Advance() noexcept
{
  const std::int64_t now = Now();
  if (now < m_nextEdgeTick)
  {
    return;
  }
  m_pic.RaiseIrq(0);
  m_nextEdgeTick = m_channels[0].NextRisingEdge(now);
}

Cycles Pit::NextInterruptAt() const noexcept
{
  if (m_nextEdgeTick == NEVER)
  {
    return NO_EVENT;
  }
  return static_cast<Cycles>(m_nextEdgeTick) * CYCLES_PER_PIT_TICK;
}

bool Pit::OutputAt(std::size_t _channel, Cycles _cycle) const noexcept
{
  return m_channels[_channel].OutputHigh(TickAt(_cycle));
}

std::uint8_t Pit::In8(std::uint16_t _port) noexcept
{
  const std::size_t index = _port & 3u;
  if (index == CONTROL_INDEX)
  {
    return OPEN_BUS; // the control register is write-only
  }
  return ReadCount(index);
}

void Pit::Out8(std::uint16_t _port, std::uint8_t _value)
{
  // Account for channel 0's edges up to now under its old programming first.
  Advance();
  const std::size_t index = _port & 3u;
  if (index == CONTROL_INDEX)
  {
    WriteControl(_value);
  }
  else
  {
    WriteCount(index, _value);
  }
}

void Pit::WriteControl(std::uint8_t _value)
{
  const std::size_t index = _value >> SELECT_SHIFT;
  if (index == CONTROL_INDEX)
  {
    return; // the 8254's read-back command; the 8253 has none
  }
  Channel& channel = m_channels[index];
  Access& access = m_access[index];
  const std::int64_t now = Now();
  const auto rw = static_cast<std::uint8_t>((_value >> ACCESS_SHIFT) & ACCESS_MASK);
  if (rw == ACCESS_LATCH)
  {
    if (!access.latched)
    {
      access.latch = channel.Count(now);
      access.latched = true;
    }
    return;
  }

  const bool wasHigh = channel.OutputHigh(now);
  channel.heldCount = channel.Count(now);
  auto mode = static_cast<std::uint8_t>((_value >> 1) & MODE_MASK);
  if (mode >= 6)
  {
    mode = static_cast<std::uint8_t>(mode - 4); // 6 and 7 are 2 and 3
  }
  channel.mode = mode;
  channel.access = rw;
  channel.loaded = false;
  channel.switchTick = NEVER;
  access = Access{};
  CheckWriteEdge(index, wasHigh);
  Changed(index);
}

void Pit::WriteCount(std::size_t _channel, std::uint8_t _value)
{
  Access& access = m_access[_channel];
  switch (m_channels[_channel].access)
  {
  case ACCESS_LOW:
    LoadCount(_channel, _value);
    break;
  case ACCESS_HIGH:
    LoadCount(_channel, static_cast<std::uint32_t>(_value) << 8);
    break;
  default:
    if (!access.writeHigh)
    {
      access.lowByte = _value;
      access.writeHigh = true;
    }
    else
    {
      access.writeHigh = false;
      LoadCount(_channel, access.lowByte | (static_cast<std::uint32_t>(_value) << 8));
    }
    break;
  }
}

void Pit::LoadCount(std::size_t _channel, std::uint32_t _count)
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
    channel.heldCount = channel.Count(now);
    channel.reload = reload;
    channel.originTick = now + 1; // loaded on the next tick
    channel.loaded = true;
    channel.switchTick = NEVER;
  }
  CheckWriteEdge(_channel, wasHigh);
  Changed(_channel);
}

std::uint8_t Pit::ReadCount(std::size_t _channel) noexcept
{
  const Channel& channel = m_channels[_channel];
  Access& access = m_access[_channel];
  const std::uint16_t value = access.latched ? access.latch : channel.Count(Now());
  const auto low = static_cast<std::uint8_t>(value & BYTE_MASK);
  const auto high = static_cast<std::uint8_t>(value >> 8);
  switch (channel.access)
  {
  case ACCESS_LOW:
    access.latched = false;
    return low;
  case ACCESS_HIGH:
    access.latched = false;
    return high;
  default:
    if (!access.readHigh)
    {
      access.readHigh = true;
      return low;
    }
    access.readHigh = false;
    access.latched = false;
    return high;
  }
}

void Pit::SetChannel2Gate(bool _high)
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
    const std::uint16_t frozenCount = channel.Count(now);
    channel.gate = true;
    if (channel.loaded)
    {
      channel.heldCount = frozenCount;
      channel.reload = channel.CountRegister();
      channel.originTick = now + 1;
      channel.switchTick = NEVER;
    }
  }
  Changed(2);
}

void Pit::CheckWriteEdge(std::size_t _channel, bool _wasHigh) noexcept
{
  if (_channel != 0)
  {
    return;
  }
  const std::int64_t now = Now();
  if (!_wasHigh && m_channels[0].OutputHigh(now))
  {
    m_pic.RaiseIrq(0); // a control word sets a low output high at once, and that is an edge
  }
  m_nextEdgeTick = m_channels[0].NextRisingEdge(now);
}

void Pit::Changed(std::size_t _channel)
{
  if (_channel == 2)
  {
    m_speaker.SetChannel2(m_channels[2]);
  }
}

} // namespace Machine
