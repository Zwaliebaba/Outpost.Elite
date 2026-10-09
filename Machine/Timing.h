// Machine/Timing.h
#pragma once

#include <cstdint>

namespace Machine
{

/// Time inside the machine is counted in CPU clock cycles since power-on, and nothing else: no
/// device reads the host's clock, so a run is a function of its inputs (ADR-005).
using Cycles = std::uint64_t;

/// The IBM PC derives every clock from one 14.31818 MHz crystal. The 8088 runs at a third of it.
inline constexpr std::uint64_t CRYSTAL_HZ = 14'318'180;
inline constexpr std::uint64_t CPU_CLOCK_DIVISOR = 3;

/// The 8253 timer is clocked at a twelfth of the crystal, so one timer tick is four CPU cycles.
inline constexpr Cycles CYCLES_PER_PIT_TICK = 4;

/// The CGA draws 912 dots per scan line at the crystal rate, 262 lines per frame: 304 CPU cycles a
/// line and 79,648 a frame, about 59.92 frames a second. 200 of the lines are visible.
inline constexpr Cycles CGA_CYCLES_PER_LINE = 304;
inline constexpr std::uint32_t CGA_LINES_PER_FRAME = 262;
inline constexpr std::uint32_t CGA_VISIBLE_LINES = 200;
inline constexpr Cycles CGA_CYCLES_PER_FRAME = CGA_CYCLES_PER_LINE * CGA_LINES_PER_FRAME;

/// Converts a duration in microseconds to CPU cycles, rounding down.
[[nodiscard]] constexpr Cycles MicrosecondsToCycles(std::uint64_t _microseconds) noexcept
{
  return _microseconds * CRYSTAL_HZ / (CPU_CLOCK_DIVISOR * 1'000'000);
}

} // namespace Machine
