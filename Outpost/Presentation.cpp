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
