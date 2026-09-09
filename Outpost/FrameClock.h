#pragma once

#include "Scheduler.h"

#include <chrono>
#include <cstdint>

namespace Outpost
{

  /*
   * The one reader of the wall clock in the whole program (Design/Platform.md §3.1, slice T-1).
   *
   * There were three -- `Main.cpp`'s loop and the two hold loops in `Shell.cpp` -- each sampling
   * `steady_clock` and keeping its own `double` of what was left over, with three different rules
   * for a backlog. This is the sample; `Scheduler` is the rules; and between them there is no
   * floating point at all.
   *
   * IT ANSWERS CYCLES BECAUSE THE GAME IS PRICED IN CYCLES. The cost model measures a flight frame
   * at 47,784 to 293,354 cycles of a 6510 and a vertical blank at 65 by 263 of them; seconds are a
   * unit neither the game nor the scheduler has any use for, and converting once here is what keeps
   * the rest of the loop integer.
   */
  class FrameClock
  {
  public:
    explicit FrameClock(MachineTiming _timing) noexcept
      : m_timing(_timing)
    {
    }

    /*
     * The cycles that have passed since the previous `Tick`, and zero on the first.
     *
     * ZERO ON THE FIRST because a default-constructed time point is the epoch, and the elapsed time
     * from it is decades: `Shell`'s title-screen accumulator relied on the clamp to absorb exactly
     * that, and it is better not to produce the number than to have something else discard it.
     *
     * THE FRACTION IS CARRIED, so a long run does not drift. A frame at 60 Hz is 17,045.45 cycles
     * of a PAL 6510, and truncating that every turn loses four cycles a frame -- a quarter of a
     * second an hour, which nothing would notice and which there is no reason to accept.
     *
     * A gap longer than `MAX_ELAPSED` is reported as `MAX_ELAPSED`: the scheduler drops a backlog
     * that large anyway, and multiplying a machine-suspend's worth of nanoseconds by a megahertz is
     * how an integer overflow gets into a loop that had no floating point left to blame.
     */
    [[nodiscard]] std::int64_t Tick() noexcept;

    /// The longest gap this will report, whatever the clock says. One minute, which is far past
    /// anything the scheduler will not have dropped.
    static constexpr std::chrono::nanoseconds MAX_ELAPSED{std::chrono::seconds{60}};

  private:
    MachineTiming m_timing;
    std::chrono::steady_clock::time_point m_last{};
    bool m_started = false;

    /// The nanoseconds of the last conversion that did not make a whole cycle.
    std::int64_t m_carryNanoseconds = 0;
  };

} // namespace Outpost
