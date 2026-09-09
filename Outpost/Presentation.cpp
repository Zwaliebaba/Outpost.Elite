#include "pch.h"

#include "Presentation.h"

#include "Picture.h"

namespace Outpost
{

  namespace
  {
    /*
     * Half, rounded DOWN, rather than the language's halving.
     *
     * `/ 2` truncates towards zero, so it rounds a positive difference down and a negative one up --
     * and the difference here is negative exactly when the client area is smaller than the canvas.
     * Truncating there would put the odd clipped column on the left in one case and on the right in
     * the other, which is a discontinuity at the one window size a person is most likely to drag
     * through. Flooring keeps "the odd pixel goes right and down" true at every size.
     */
    constexpr int FloorHalf(int _value) noexcept
    {
      return (_value >= 0) ? (_value / 2) : -((-_value + 1) / 2);
    }
  } // namespace

  std::array<std::uint32_t, 16> PaletteAsRgba() noexcept
  {
    std::array<std::uint32_t, 16> packed{};
    for (std::size_t index = 0; index < C64_PALETTE.size(); ++index)
    {
      const Rgb& colour = C64_PALETTE[index];

      // R in the low byte, so the word is R8G8B8A8 in memory on a little-endian machine -- which is
      // what DXGI_FORMAT_R8G8B8A8_UNORM expects and what a shader reads as .rgba without a swizzle.
      packed[index] = static_cast<std::uint32_t>(colour.red) | (static_cast<std::uint32_t>(colour.green) << 8) |
                      (static_cast<std::uint32_t>(colour.blue) << 16) | 0xFF000000u;
    }
    return packed;
  }

  Viewport FitPicture(int _clientWidth, int _clientHeight) noexcept
  {
    Viewport view{};
    if (_clientWidth <= 0 || _clientHeight <= 0)
    {
      return view;
    }

    const int horizontal = _clientWidth / Elite::Picture::WIDTH;
    const int vertical = _clientHeight / Elite::Picture::HEIGHT;

    view.scale = (horizontal < vertical) ? horizontal : vertical;
    if (view.scale < 1)
    {
      view.scale = 1;
    }

    view.width = Elite::Picture::WIDTH * view.scale;
    view.height = Elite::Picture::HEIGHT * view.scale;

    // Centred, and an odd remainder leaves the extra column on the right rather than splitting a
    // pixel, which is the only choice that keeps the scale integral.
    view.x = FloorHalf(_clientWidth - view.width);
    view.y = FloorHalf(_clientHeight - view.height);
    return view;
  }

  StepPlan PlanSteps(double _elapsedSeconds, double _accumulatedSeconds, double _stepsPerSecond) noexcept
  {
    StepPlan plan{};
    plan.leftoverSeconds = _accumulatedSeconds;

    if (!(_stepsPerSecond > 0.0))
    {
      return plan;
    }

    // A negative elapsed time is a clock that went backwards, which is not this function's problem
    // to diagnose -- but adding it would run the accumulator backwards, so it is ignored.
    if (_elapsedSeconds > 0.0)
    {
      plan.leftoverSeconds += _elapsedSeconds;
    }

    const double period = 1.0 / _stepsPerSecond;

    while (plan.leftoverSeconds >= period && plan.steps < MAX_STEPS_PER_CALL)
    {
      plan.leftoverSeconds -= period;
      ++plan.steps;
    }

    /*
     * The backlog outlived the clamp, so the rest is dropped rather than run.
     *
     * Dropping it is the only option that keeps the game responsive, and saying so is what stops
     * it being invisible: ADR-005 section 3 asks for a stall to be logged, and a caller cannot log
     * what it was not told.
     */
    if (plan.leftoverSeconds >= period)
    {
      plan.stalled = true;
      plan.leftoverSeconds = 0.0;
    }

    return plan;
  }

  double TitleTurnSeconds(std::uint8_t _distanceHigh) noexcept
  {
    /*
     * The table is in descending order of distance and the walk goes with it, so the first entry
     * the argument is at or above is the far side of the pair it falls between. Outside the table
     * the cost is flat: nothing calls this with a distance above 96, because `TITLE` starts there,
     * and 1 is where the ship stops.
     */
    const TitleTurnCost* above = &TITLE_TURN_COSTS.front();

    for (const TitleTurnCost& point : TITLE_TURN_COSTS)
    {
      if (_distanceHigh >= point.distanceHigh)
      {
        break;
      }
      above = &point;
    }

    double cycles = static_cast<double>(above->cycles);

    /*
     * The pair `_distanceHigh` falls between, if it falls between two at all.
     *
     * `above` and `below` rather than the obvious `far` and `near`: both of those are still MACROS
     * after `<windows.h>`, so `const TitleTurnCost& near = ...` compiles as a declaration with no
     * name. AGENTS.md section 6 records the same trap costing a CI leg with `bool near`.
     */
    const std::size_t index = static_cast<std::size_t>(above - TITLE_TURN_COSTS.data());
    if (index + 1 < TITLE_TURN_COSTS.size() && _distanceHigh < above->distanceHigh)
    {
      const TitleTurnCost& below = TITLE_TURN_COSTS[index + 1];
      const double span = static_cast<double>(above->distanceHigh - below.distanceHigh);
      const double along = static_cast<double>(above->distanceHigh - _distanceHigh) / span;
      cycles = static_cast<double>(above->cycles) + along * (static_cast<double>(below.cycles) - static_cast<double>(above->cycles));
    }

    return cycles / NTSC_CLOCK_HZ;
  }

  double FlightFrameSeconds(std::uint8_t _ships) noexcept
  {
    // Ascending in occupied slots, linear between rows, flat outside: below the first row is the
    // empty bubble's cost and above the last is the fullest bubble the game can hold.
    const FlightFrameCost* below = &FLIGHT_FRAME_COSTS.front();
    const FlightFrameCost* above = &FLIGHT_FRAME_COSTS.back();

    for (const FlightFrameCost& point : FLIGHT_FRAME_COSTS)
    {
      if (_ships >= point.ships)
      {
        below = &point;
      }
    }
    for (std::size_t index = FLIGHT_FRAME_COSTS.size(); index-- > 0u;)
    {
      if (_ships <= FLIGHT_FRAME_COSTS[index].ships)
      {
        above = &FLIGHT_FRAME_COSTS[index];
      }
    }

    double cycles = static_cast<double>(below->cycles);
    if (above->ships > below->ships)
    {
      const double span = static_cast<double>(above->ships - below->ships);
      const double along = static_cast<double>(_ships - below->ships) / span;
      cycles += along * (static_cast<double>(above->cycles) - static_cast<double>(below->cycles));
    }

    return cycles / NTSC_CLOCK_HZ;
  }

  double DockedPassSeconds(std::uint8_t _syncs) noexcept
  {
    // The work between the waits, and the waits the library asked for -- how many syncs a docked
    // pass waits is `RunLoopTail`'s to decide, and it says two or none.
    const double work = static_cast<double>(DOCKED_PASS_CYCLES) / NTSC_CLOCK_HZ;
    return work + static_cast<double>(_syncs) * NTSC_FRAME_CYCLES / NTSC_CLOCK_HZ;
  }

  // ---- the same three in cycles (Design/Platform.md T-1) ------------------------------------------

  namespace
  {
    /*
     * One row's worth of straight line, in integers, rounded to nearest.
     *
     * `_along` over `_span` of the way from `_from` to `_to`, computed in 64 bits so that the
     * product of a quarter-million cycles and a span of ninety-five cannot overflow, and rounded
     * rather than truncated so that the integer curve does not sit systematically below the
     * `double` one it replaces. The difference between the two is under a cycle everywhere, which
     * is a millionth of a frame.
     */
    [[nodiscard]] std::uint32_t Between(std::uint32_t _from, std::uint32_t _to, std::int64_t _along, std::int64_t _span) noexcept
    {
      if (_span <= 0)
      {
        return _from;
      }
      const std::int64_t rise = static_cast<std::int64_t>(_to) - static_cast<std::int64_t>(_from);
      const std::int64_t scaled = rise * _along;

      // Rounded away from zero, so that a rise and a fall of the same size move by the same amount.
      const std::int64_t half = _span / 2;
      const std::int64_t step = (scaled >= 0) ? ((scaled + half) / _span) : ((scaled - half) / _span);
      return static_cast<std::uint32_t>(static_cast<std::int64_t>(_from) + step);
    }
  } // namespace

  std::uint32_t TitleTurnCycles(std::uint8_t _distanceHigh) noexcept
  {
    // The walk is `TitleTurnSeconds`', line for line: the table descends in distance, so the first
    // entry the argument is at or above is the far side of the pair it falls between.
    const TitleTurnCost* above = &TITLE_TURN_COSTS.front();

    for (const TitleTurnCost& point : TITLE_TURN_COSTS)
    {
      if (_distanceHigh >= point.distanceHigh)
      {
        break;
      }
      above = &point;
    }

    const std::size_t index = static_cast<std::size_t>(above - TITLE_TURN_COSTS.data());
    if (index + 1 < TITLE_TURN_COSTS.size() && _distanceHigh < above->distanceHigh)
    {
      const TitleTurnCost& below = TITLE_TURN_COSTS[index + 1];
      return Between(above->cycles, below.cycles, static_cast<std::int64_t>(above->distanceHigh) - _distanceHigh,
                     static_cast<std::int64_t>(above->distanceHigh) - below.distanceHigh);
    }
    return above->cycles;
  }

  std::uint32_t FlightFrameCycles(std::uint8_t _ships) noexcept
  {
    // Ascending in occupied slots, linear between rows, flat outside -- `FlightFrameSeconds`' walk.
    const FlightFrameCost* below = &FLIGHT_FRAME_COSTS.front();
    const FlightFrameCost* above = &FLIGHT_FRAME_COSTS.back();

    for (const FlightFrameCost& point : FLIGHT_FRAME_COSTS)
    {
      if (_ships >= point.ships)
      {
        below = &point;
      }
    }
    for (std::size_t index = FLIGHT_FRAME_COSTS.size(); index-- > 0u;)
    {
      if (_ships <= FLIGHT_FRAME_COSTS[index].ships)
      {
        above = &FLIGHT_FRAME_COSTS[index];
      }
    }

    if (above->ships <= below->ships)
    {
      return below->cycles;
    }
    return Between(below->cycles, above->cycles, static_cast<std::int64_t>(_ships) - below->ships,
                   static_cast<std::int64_t>(above->ships) - below->ships);
  }

  std::uint32_t DockedPassCycles(std::uint8_t _syncs, MachineTiming _timing) noexcept
  {
    // The work, and the syncs the pass asked `DELAY` for at the machine's own frame -- which is
    // where the seconds form was wrong on a PAL machine and this one is not.
    return DOCKED_PASS_CYCLES + static_cast<std::uint32_t>(_syncs) * _timing.cyclesPerFrame;
  }

} // namespace Outpost
