#include "pch.h"

#include "TimeOfDay.h"

#include "Timing.h"

namespace Elite
{

namespace
{

// The timer handler's day: 1800B0h ticks, its high word 18h and its low B0h.
constexpr std::uint16_t TICKS_PER_DAY_HIGH = 0x18;
constexpr std::uint16_t TICKS_PER_DAY_LOW = 0xB0;

// The PC's timer input is the 14.31818 MHz crystal divided by 12, and the BIOS counts one tick per 65,536 of those.
constexpr std::uint64_t TIMER_DIVISOR = 65536;
constexpr std::uint64_t CENTISECONDS_PER_SECOND = 100;

} // namespace

TimeOfDay::TimeOfDay(std::uint32_t _ticks) noexcept
  : m_ticksLow(static_cast<std::uint16_t>(_ticks & 0xFFFF)),
    m_ticksHigh(static_cast<std::uint16_t>(_ticks >> 16))
{
}

ClockReading TimeOfDay::ReadClock() noexcept
{
  const ClockReading clock{Ticks(), m_rolledOver};
  m_rolledOver = 0;
  return clock;
}

void TimeOfDay::SetClock(std::uint32_t _ticks) noexcept
{
  m_ticksLow = static_cast<std::uint16_t>(_ticks & 0xFFFF);
  m_ticksHigh = static_cast<std::uint16_t>(_ticks >> 16);
  m_rolledOver = 0;
}

TimeOfDay::Tick TimeOfDay::CountTick() noexcept
{
  Tick tick{false, false, false};
  m_ticksLow = static_cast<std::uint16_t>(m_ticksLow + 1);
  if (m_ticksLow == 0)
  {
    m_ticksHigh = static_cast<std::uint16_t>(m_ticksHigh + 1);
    tick.carried = true;
  }
  if (m_ticksHigh == TICKS_PER_DAY_HIGH)
  {
    tick.dayCheck = true;
    if (m_ticksLow == TICKS_PER_DAY_LOW)
    {
      // A day has passed: the count starts again from 0, high word first, and the rollover flag that int 1Ah reports is set.
      m_ticksHigh = 0;
      m_ticksLow = 0;
      m_rolledOver = 1;
      tick.dayEnded = true;
    }
  }
  return tick;
}

std::uint32_t TimeOfDay::TicksAt(std::uint32_t _centisecondsSinceMidnight) noexcept
{
  const std::uint64_t ticks = std::uint64_t{_centisecondsSinceMidnight} * CRYSTAL_HZ /
                              (CPU_CLOCK_DIVISOR * CYCLES_PER_PIT_TICK * TIMER_DIVISOR * CENTISECONDS_PER_SECOND);
  return ticks < TICKS_PER_DAY ? static_cast<std::uint32_t>(ticks) : TICKS_PER_DAY - 1;
}

} // namespace Elite
