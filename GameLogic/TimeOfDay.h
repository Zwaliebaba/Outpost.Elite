// GameLogic/TimeOfDay.h
#pragma once

#include <cstdint>

namespace Elite
{

/// What the clock gives for int 1Ah AH=0.
struct ClockReading
{
  std::uint32_t ticks;     ///< CX:DX: the timer's ticks since midnight, about 18.2 a second
  std::uint8_t rolledOver; ///< AL: non-zero when midnight has passed since the clock was last read
};

/// The time of day the IBM PC's BIOS kept (ADR-041): its tick count, which the BIOS's timer handler counts while it serves the
/// timer's interrupt, and its rollover flag, as int 1Ah reads and sets them. It starts at the time of day DOS was given.
class TimeOfDay
{
public:
  /// The tick count the BIOS takes for 24 hours, at which its timer handler starts again from 0.
  static constexpr std::uint32_t TICKS_PER_DAY = 0x1800B0;

  /// What one tick of the BIOS's timer handler did to its count, which sets the cycles its code took.
  struct Tick
  {
    bool carried;  ///< the low word wrapped, and the high word was counted too
    bool dayCheck; ///< the high word was 18h, so the low word was compared with B0h as well
    bool dayEnded; ///< a day had passed: the count started again from 0, and the rollover flag was set
  };

  explicit TimeOfDay(std::uint32_t _ticks) noexcept;

  /// AH=00h: the tick count and the rollover flag, which is then cleared.
  [[nodiscard]] ClockReading ReadClock() noexcept;

  /// AH=01h: the tick count set, and the rollover flag cleared.
  void SetClock(std::uint32_t _ticks) noexcept;

  /// The timer handler's count of one tick, as the ROM's code does it: the low word counted, the high word when it carried,
  /// and both words zeroed and the rollover flag set once a day has passed.
  Tick CountTick() noexcept;

  /// The tick count.
  [[nodiscard]] std::uint32_t Ticks() const noexcept
  {
    return (std::uint32_t{m_ticksHigh} << 16) | m_ticksLow;
  }

  /// The tick count for a time of day: the PC's timer runs at 14.31818 MHz / 12 / 65,536, about 18.2 Hz.
  [[nodiscard]] static std::uint32_t TicksAt(std::uint32_t _centisecondsSinceMidnight) noexcept;

private:
  std::uint16_t m_ticksLow;      // TIMER_LOW, 0040:006C
  std::uint16_t m_ticksHigh;     // TIMER_HIGH, 0040:006E
  std::uint8_t m_rolledOver = 0; // TIMER_OFL, 0040:0070
};

} // namespace Elite
