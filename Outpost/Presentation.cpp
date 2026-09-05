#include "pch.h"

#include "Presentation.h"

#include "Canvas.h"

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
      const Colour& colour = C64_PALETTE[index];

      // R in the low byte, so the word is R8G8B8A8 in memory on a little-endian machine -- which is
      // what DXGI_FORMAT_R8G8B8A8_UNORM expects and what a shader reads as .rgba without a swizzle.
      packed[index] = static_cast<std::uint32_t>(colour.red) | (static_cast<std::uint32_t>(colour.green) << 8) |
                      (static_cast<std::uint32_t>(colour.blue) << 16) | 0xFF000000u;
    }
    return packed;
  }

  Viewport FitCanvas(int _clientWidth, int _clientHeight) noexcept
  {
    Viewport view{};
    if (_clientWidth <= 0 || _clientHeight <= 0)
    {
      return view;
    }

    const int horizontal = _clientWidth / Elite::Canvas::WIDTH;
    const int vertical = _clientHeight / Elite::Canvas::HEIGHT;

    view.scale = (horizontal < vertical) ? horizontal : vertical;
    if (view.scale < 1)
    {
      view.scale = 1;
    }

    view.width = Elite::Canvas::WIDTH * view.scale;
    view.height = Elite::Canvas::HEIGHT * view.scale;

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
    // Two entries, in ascending order of how full the bubble is, and flat above the last -- the
    // crowded end is not measured, so it is held rather than extrapolated (see the header).
    const FlightFrameCost* cost = &FLIGHT_FRAME_COSTS.front();

    for (const FlightFrameCost& point : FLIGHT_FRAME_COSTS)
    {
      if (_ships >= point.ships)
      {
        cost = &point;
      }
    }

    return static_cast<double>(cost->cycles) / NTSC_CLOCK_HZ;
  }

} // namespace Outpost
