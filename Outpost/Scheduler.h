#pragma once

#include <cstdint>

namespace Outpost
{

  /*
   * The machine the port keeps time against, and the scheduler that spends it
   * (Design/Platform.md §3.2, slice T-1).
   *
   * WHAT THIS REPLACES, and why it is one object where there were four. The executable held four
   * `double` accumulators over three `steady_clock` reads -- `Main.cpp`'s `accumulated` and
   * `dockedLeftover` through `PlanSteps`, and `Shell`'s `m_spinLeftover` and `m_flightFrameLeftover`
   * inside the two hold loops -- each with its own backlog rule, and `DELAY` counted none of them:
   * `WaitFrames` counted PRESENTS, so every docked pause, debounce and beep lasted a different time
   * on a 60 Hz, 144 Hz or 165 Hz panel (Design/Archive/InputTimer.md T-1, T-2). There is one supply
   * of time here, in integer 6510 cycles, and two counters drawn from it: whole game STEPS at the
   * cost model's price, and simulated VERTICAL BLANKS at the machine's frame rate. What the display
   * decides is when a frame is shown, and nothing else.
   *
   * IT IS INTEGER, WHICH IS WHY IT CAN BE TESTED. ADR-007 §2 kept the count of passes in the
   * executable because "the accumulator is floating point by construction"; it is not, and the
   * reason it stays here is the one that always mattered -- the CLOCK is the platform's
   * (AGENTS.md §5). Nothing in this file reads a clock, so both CI legs run it.
   */

  /*
   * The 6510 and the VIC-II, as a pair of numbers.
   *
   * NTSC IS THE MACHINE THIS PORT IS, and it is not a preference: ADR-001's context line records
   * the masters as `_VARIANT=1`, the GMA85 NTSC release, so the cost model, the sound interrupt and
   * this all run on the same clock the game was built for. PAL is carried because `wscan.asm` says
   * the machine decides and because a player may want it, and it is one line of `Settings.txt`.
   *
   * The two differ by more than they look: 3.8% in processor speed and 17% in frame rate, and it
   * is the second that a docked `DELAY` is measured in.
   */
  struct MachineTiming
  {
    std::uint32_t clockHz = 0;        ///< the 6510's own rate
    std::uint32_t cyclesPerFrame = 0; ///< what the VIC-II spends on one frame, which is one `WSCAN`

    /// 1,022,727 Hz, 65 cycles a line for 263 lines.
    [[nodiscard]] static constexpr MachineTiming Ntsc() noexcept
    {
      return {1'022'727u, 65u * 263u};
    }

    /// 985,248 Hz, 63 cycles a line for 312 lines.
    [[nodiscard]] static constexpr MachineTiming Pal() noexcept
    {
      return {985'248u, 63u * 312u};
    }

    [[nodiscard]] constexpr bool operator==(const MachineTiming&) const = default;
  };

  /*
   * How much game time one turn of the outer loop may deliver, in frames of the machine.
   *
   * THE CLAMP IS BY TIME AND NOT BY COUNT, and that is the change from `PlanSteps`. The old rule
   * was four STEPS per call, which is right for a cheap step and wrong for an expensive one: a
   * flight frame in a fight of eight costs 293,354 cycles, so four of them was 1.15 seconds of game
   * time run between two presents -- a lid closed and reopened made the game lurch rather than drop
   * what it had missed. Four FRAMES is 67 milliseconds however dear the step, so a stall costs a
   * frame and never a burst.
   *
   * The first affordable step is always taken, whatever it costs, or a bubble full of Anacondas
   * would never step at all. What the budget caps is the CATCH-UP after it -- which is what keeps
   * the one screen that runs at 228 passes a second (Data on System with the author names, whose
   * pass is 4,472 cycles and waits for nothing) running at 228 rather than at the display's rate.
   */
  inline constexpr int MAX_CATCH_UP_FRAMES = 4;

  /*
   * One supply of cycles, two counters, and the auto-pause.
   *
   * `Feed` once per turn of the outer loop -- from `Turn()`, so that every call depth feeds it and
   * a hold loop is paced by the same object as the loop that entered it. `TakeSteps` and
   * `TakeBlanks` then spend what has accumulated: the first at whatever a step costs right now
   * (which depends on how full the bubble is), the second at the machine's frame.
   *
   * AN INACTIVE WINDOW DROPS THE TIME RATHER THAN BANKING IT, which is what a windowed player
   * means by pause (Design/Platform.md §3.2, Design/Archive/InputTimer.md §5.9 item 2). Alt+Tab away
   * for a minute and the game is where it was, on a whole step, rather than running the minute out
   * at four frames a turn.
   */
  class Scheduler
  {
  public:
    explicit Scheduler(MachineTiming _timing) noexcept
      : m_timing(_timing)
    {
    }

    /*
     * The cycles that have elapsed since the last turn, and whether the window has the player's
     * attention. Negative or zero elapsed adds nothing -- a clock that went backwards is not this
     * object's to diagnose, and running the accumulators backwards would be worse than ignoring it.
     */
    void Feed(std::int64_t _elapsedCycles, bool _active) noexcept;

    /*
     * Whole steps the budget affords at `_stepCostCycles`, and the backlog dropped if it outlived
     * the clamp -- which is counted, so that `TakeStalls` can say it happened.
     *
     * A cost of zero answers zero rather than dividing by it: a caller that has no cost model yet
     * gets no steps, which is a visible state rather than an infinite loop.
     */
    [[nodiscard]] int TakeSteps(std::uint32_t _stepCostCycles) noexcept;

    /// Simulated vertical blanks since the last call -- what `DELAY` counts, on every panel.
    [[nodiscard]] int TakeBlanks() noexcept;

    /// How many stalls since the last call, for the log and the debug title bar. Reading clears it.
    [[nodiscard]] std::uint32_t TakeStalls() noexcept;

    /*
     * Both accumulators to zero.
     *
     * `Main.cpp` did this by hand across a dock -- "the leftover is dropped rather than carried
     * across, so the next flight starts on a whole step instead of on a fraction of one measured
     * before the market screen". It is one call now, and it is the only place a mode change touches
     * the clock.
     */
    void Reset() noexcept;

    [[nodiscard]] MachineTiming Timing() const noexcept
    {
      return m_timing;
    }

    /// What has accumulated and not yet been spent, for a test that wants to see the arithmetic.
    [[nodiscard]] std::int64_t PendingStepCycles() const noexcept
    {
      return m_stepCycles;
    }
    [[nodiscard]] std::int64_t PendingBlankCycles() const noexcept
    {
      return m_blankCycles;
    }

  private:
    MachineTiming m_timing;

    /*
     * TWO ACCUMULATORS FROM ONE SUPPLY, and they are not the same number spent twice: a step is
     * what the 6510 took to compute a frame of the game and a blank is what the VIC-II took to
     * scan one, and the whole point of the cost model is that those are different (§6.17 -- the
     * C64's main loop has no `WSCAN` in it). Both advance by the same elapsed cycles and are spent
     * independently, so a frame that costs seventeen blanks still delivers seventeen `DELAY`s.
     */
    std::int64_t m_stepCycles = 0;
    std::int64_t m_blankCycles = 0;

    std::uint32_t m_stalls = 0;
  };

} // namespace Outpost
