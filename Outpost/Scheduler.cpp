#include "pch.h"

#include "Scheduler.h"

namespace Outpost
{

  void Scheduler::Feed(std::int64_t _elapsedCycles, bool _active) noexcept
  {
    /*
     * The auto-pause, and it DROPS rather than banks (Design/Platform.md §3.2).
     *
     * The alternative -- carrying the time and running it out when the window comes back -- is what
     * the four-step clamp was protecting against by hand, and it is the wrong shape: a player who
     * alt-tabs away for a minute has not asked for a minute of Elite to happen at once. Zeroing
     * both accumulators means the game resumes on a whole step, where it was.
     */
    if (!_active)
    {
      Reset();
      return;
    }

    if (_elapsedCycles <= 0)
    {
      return;
    }

    m_stepCycles += _elapsedCycles;
    m_blankCycles += _elapsedCycles;
  }

  int Scheduler::TakeSteps(std::uint32_t _stepCostCycles) noexcept
  {
    if (_stepCostCycles == 0u)
    {
      return 0;
    }

    const std::int64_t cost = static_cast<std::int64_t>(_stepCostCycles);
    const std::int64_t budget = static_cast<std::int64_t>(MAX_CATCH_UP_FRAMES) * static_cast<std::int64_t>(m_timing.cyclesPerFrame);

    /*
     * The first affordable step is unconditional and the rest are on the budget.
     *
     * Without the first clause a step dearer than the budget -- which is every flight frame with
     * more than two ships in it, at 82,236 cycles against a budget of 68,380 -- would never be
     * taken at all, and the game would stop the moment a fight started. With it, the catch-up is
     * what the clamp bounds, which is the thing that was ever dangerous.
     */
    int steps = 0;
    std::int64_t taken = 0;
    while (m_stepCycles >= cost && (steps == 0 || (taken + cost) <= budget))
    {
      m_stepCycles -= cost;
      taken += cost;
      ++steps;
    }

    /*
     * A whole step still owed after the clamp is a backlog that outlived it, so it is dropped and
     * SAID: ADR-005 §3 asks for a stall to be logged, and a caller cannot log what it was not told.
     * `(void)plan.stalled` is what this replaces.
     */
    if (m_stepCycles >= cost)
    {
      m_stepCycles = 0;
      ++m_stalls;
    }

    return steps;
  }

  int Scheduler::TakeBlanks() noexcept
  {
    const std::int64_t period = static_cast<std::int64_t>(m_timing.cyclesPerFrame);
    if (period <= 0)
    {
      return 0;
    }

    int blanks = 0;
    while (m_blankCycles >= period && blanks < MAX_CATCH_UP_FRAMES)
    {
      m_blankCycles -= period;
      ++blanks;
    }

    /*
     * The blanks past the clamp are dropped rather than carried, and the difference matters to
     * `DELAY`. Carried, a two-second stall would leave a hundred and twenty blanks dribbling out
     * four to a turn, and every `DELAY` for the next half-second would return at once -- a beep
     * with no pause after it, which is the failure the clamp exists to stop rather than to cause.
     *
     * No stall is counted here: it is the same stall the step accumulator has already reported.
     */
    if (m_blankCycles >= period)
    {
      m_blankCycles = 0;
    }

    return blanks;
  }

  std::uint32_t Scheduler::TakeStalls() noexcept
  {
    const std::uint32_t stalls = m_stalls;
    m_stalls = 0;
    return stalls;
  }

  void Scheduler::Reset() noexcept
  {
    m_stepCycles = 0;
    m_blankCycles = 0;
  }

} // namespace Outpost
