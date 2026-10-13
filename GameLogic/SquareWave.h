// GameLogic/SquareWave.h
#pragma once

#include "Timing.h"

#include <cstdint>
#include <limits>

namespace Elite
{

/// A channel of the IBM PC's 8253 interval timer in mode 3, the square wave, the only mode the game programs (ADR-006 item 2,
/// ADR-041): its programming and phase, from which its output at any tick follows directly rather than by simulating the ticks
/// in between. The timer is clocked at a twelfth of the crystal: one tick every CYCLES_PER_PIT_TICK cycles, tick k at cycle
/// k * CYCLES_PER_PIT_TICK counted from power-on, so the ticks by a cycle are a division of the platform's own clock and nothing
/// drifts. A plain aggregate (R8): the scheduler keeps channel 0's, the tone timer channel 2's, and the speaker logs copies of
/// channel 2's.
///
/// A count is loaded into the counting element on the tick after it is written. With an odd count N the output is high for
/// (N+1)/2 ticks and low for (N-1)/2. A low gate holds the output high. BCD counting counts in binary; nothing the game does uses
/// it.
struct SquareWave
{
  /// A tick that never comes.
  static constexpr std::int64_t NEVER = std::numeric_limits<std::int64_t>::max();

  /// A count of 0 is the largest: 65536 ticks.
  static constexpr std::uint32_t FULL_COUNT = 0x10000;

  bool loaded = false;         // a count has been written since the last control word
  bool gate = true;            // the gate input
  std::uint32_t reload = 0;    // the count the counting element runs with, 1-65536
  std::int64_t originTick = 0; // the tick `reload` was loaded on: phase zero, output high

  /// Whether the output is high during tick _tick, the interval from that tick to the next.
  [[nodiscard]] bool OutputHigh(std::int64_t _tick) const noexcept;

  /// The first tick after _tick on which the output goes from low to high, or NEVER.
  [[nodiscard]] std::int64_t NextRisingEdge(std::int64_t _tick) const noexcept;

  /// The time-integral of the output (1 high, 0 low) over [_from, _to), in a time base of _unitsPerTick units to the tick,
  /// with tick t covering [t * _unitsPerTick, (t + 1) * _unitsPerTick). A wave whose period is shorter than
  /// _averageBelowTicks ticks counts as its mean level instead of its shape; 0 means always exact.
  [[nodiscard]] double Integral(std::uint64_t _from, std::uint64_t _to, std::uint64_t _unitsPerTick,
                                std::uint32_t _averageBelowTicks) const noexcept;

  /// The count register: the last count written, 1-65536, or 0 if none has been.
  [[nodiscard]] std::uint32_t CountRegister() const noexcept;

  /// The number of the tick in progress at _cycle.
  [[nodiscard]] static constexpr std::int64_t TickAt(Cycles _cycle) noexcept
  {
    return static_cast<std::int64_t>(_cycle / CYCLES_PER_PIT_TICK);
  }

  /// The count _divisor stands for: 0 is the largest.
  [[nodiscard]] static constexpr std::uint32_t Reload(std::uint16_t _divisor) noexcept
  {
    return _divisor == 0 ? FULL_COUNT : _divisor;
  }
};

} // namespace Elite
