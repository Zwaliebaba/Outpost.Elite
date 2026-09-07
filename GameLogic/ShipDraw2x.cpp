#include "pch.h"

#include "ShipDraw2x.h"

namespace Elite
{

  namespace
  {
    /// Far enough off the screen that no clip rectangle can tell one such vertex from another, and
    /// small enough that the arithmetic here stays in sixteen bits with room to spare.
    constexpr std::int32_t OFF_SCREEN_LIMIT = 30000;

    [[nodiscard]] std::int16_t Narrow(std::int32_t _value) noexcept
    {
      const std::int32_t low = (_value < -OFF_SCREEN_LIMIT) ? -OFF_SCREEN_LIMIT : _value;
      return static_cast<std::int16_t>((low > OFF_SCREEN_LIMIT) ? OFF_SCREEN_LIMIT : low);
    }

    /// Cohen-Sutherland's four edges, as the bits the algorithm names them by.
    constexpr int LEFT = 1;
    constexpr int RIGHT = 2;
    constexpr int ABOVE = 4;
    constexpr int BELOW = 8;

    [[nodiscard]] int Region(std::int32_t _x, std::int32_t _y, std::int32_t _bottom) noexcept
    {
      int code = 0;
      if (_x < 0)
      {
        code |= LEFT;
      }
      else if (_x >= SPACE_VIEW_WIDTH_2X)
      {
        code |= RIGHT;
      }
      if (_y < 0)
      {
        code |= ABOVE;
      }
      else if (_y > _bottom)
      {
        code |= BELOW;
      }
      return code;
    }
  } // namespace

  bool ClipLine2x(Line2x _line, int _bottomRow, Line2x& _outClipped) noexcept
  {
    std::int32_t x1 = _line.x1;
    std::int32_t y1 = _line.y1;
    std::int32_t x2 = _line.x2;
    std::int32_t y2 = _line.y2;
    const std::int32_t bottom = _bottomRow;

    int first = Region(x1, y1, bottom);
    int second = Region(x2, y2, bottom);

    // Bounded rather than `for (;;)`: each turn clears at least one region bit of one end, so four
    // is already generous, and a loop that cannot run away is one nobody has to reason about.
    for (int turn = 0; turn < 8; ++turn)
    {
      if ((first | second) == 0)
      {
        _outClipped = Line2x{Narrow(x1), Narrow(y1), Narrow(x2), Narrow(y2), true};
        return true;
      }
      if ((first & second) != 0)
      {
        return false; // both ends beyond the same edge, so none of it is inside
      }

      const int outside = (first != 0) ? first : second;
      std::int32_t x = 0;
      std::int32_t y = 0;

      if ((outside & BELOW) != 0)
      {
        x = x1 + (x2 - x1) * (bottom - y1) / ((y2 - y1) != 0 ? (y2 - y1) : 1);
        y = bottom;
      }
      else if ((outside & ABOVE) != 0)
      {
        x = x1 + (x2 - x1) * (0 - y1) / ((y2 - y1) != 0 ? (y2 - y1) : 1);
        y = 0;
      }
      else if ((outside & RIGHT) != 0)
      {
        y = y1 + (y2 - y1) * (SPACE_VIEW_WIDTH_2X - 1 - x1) / ((x2 - x1) != 0 ? (x2 - x1) : 1);
        x = SPACE_VIEW_WIDTH_2X - 1;
      }
      else
      {
        y = y1 + (y2 - y1) * (0 - x1) / ((x2 - x1) != 0 ? (x2 - x1) : 1);
        x = 0;
      }

      if (outside == first)
      {
        x1 = x;
        y1 = y;
        first = Region(x1, y1, bottom);
      }
      else
      {
        x2 = x;
        y2 = y;
        second = Region(x2, y2, bottom);
      }
    }

    return false;
  }

  void Bresenham2x(Picture& _picture, Line2x _line) noexcept
  {
    std::int32_t x = _line.x1;
    std::int32_t y = _line.y1;

    const std::int32_t dx = static_cast<std::int32_t>(_line.x2) - x;
    const std::int32_t dy = static_cast<std::int32_t>(_line.y2) - y;
    const std::int32_t across = (dx >= 0) ? dx : -dx;
    const std::int32_t down = (dy >= 0) ? dy : -dy;

    /*
     * HALF-OPEN, WHICH IS `LOIN`'S OWN SHAPE AND WAS MEASURED RATHER THAN ASSUMED.
     *
     * The faithful line drawer lights `max(|dx|, |dy|)` pixels: from (100, 50) to (104, 50) it
     * lights four, not five, and a line whose ends are the SAME point lights none at all. So the
     * start is drawn and the end is not, and the loop runs the span rather than the span plus one.
     *
     * Getting this wrong is almost invisible and permanently wrong. Every extra endpoint is an
     * exclusive-or, so at a vertex where an even number of edges meet the two mistakes cancel and
     * the picture looks right; the whole error surfaces as ONE stray pixel wherever an odd number
     * meet -- which is what a degenerate edge in the Cobra's blueprint produced, and the only
     * reason the shadow test caught it at all (section 13).
     */
    const std::int32_t steps = (across > down) ? across : down;
    if (steps == 0)
    {
      return;
    }

    const std::int32_t acrossStep = (dx >= 0) ? 1 : -1;
    const std::int32_t downStep = (dy >= 0) ? 1 : -1;
    std::int32_t error = across - down;

    for (std::int32_t step = 0; step < steps; ++step)
    {
      // The doubled left margin, added at the plot and nowhere else: everything above this line is
      // in the view's own coordinates, exactly as `LOIN`'s arithmetic is (see the header).
      _picture.PlotPoint(static_cast<int>(x) + Picture::SPACE_VIEW_MARGIN, static_cast<int>(y));

      const std::int32_t twice = 2 * error;
      if (twice > -down)
      {
        error -= down;
        x += acrossStep;
      }
      if (twice < across)
      {
        error += across;
        y += downStep;
      }
    }
  }

  void PushHeapLine2x(LineHeap2x& _heap, HeapOffset _at, Line2x _line) noexcept
  {
    _heap.Write(_at, _line);
  }

  void DrawShipLines2x(Picture& _picture, const LineHeap& _heap, const LineHeap2x& _wide, HeapOffset _run) noexcept
  {
    // `LL155`'s own walk: byte 0 is the run's length and under four there is not a whole line.
    const std::uint8_t length = _heap.Read(_run);
    if (length < 4u)
    {
      return;
    }

    std::uint8_t y = 1;
    do
    {
      const Line2x line = _wide.Read(_run.Byte(y));
      if (line.drawn)
      {
        Bresenham2x(_picture, line);
      }
      y = static_cast<std::uint8_t>(y + 4);
    } while (y < length);
  }

} // namespace Elite
