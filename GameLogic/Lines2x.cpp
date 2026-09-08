#include "pch.h"

#include "Lines2x.h"

#include "Canvas.h"
#include "EliteTypes.h"

#include <algorithm>

namespace Elite
{

  namespace
  {
    /// A mark is two hi-res pixels wide, which is the two bits `TWOS2` lights at half the size.
    constexpr int MARK_WIDTH = 2;

    /// `PIXEL`'s `CMP #80` -- closer than this and the mark gets a second row. A twin ports no
    /// label, so the number is named rather than marked.
    constexpr std::uint8_t NEAR_DISTANCE = 80;
  } // namespace

  void PlotPixel2x(Picture& _picture, int _x, int _y, std::uint8_t _distance) noexcept
  {
    const int x = _x + Picture::SPACE_VIEW_MARGIN;

    for (int pixel = 0; pixel < MARK_WIDTH; ++pixel)
    {
      _picture.PlotPoint(x + pixel, _y);
    }

    // `PIXEL`'s two comparisons pick between one mark and one mark and between one row and two, so
    // the only thing the distance decides here is whether there is a second row: its first test
    // selects between two identical outcomes and is not reproduced.
    if (_distance >= NEAR_DISTANCE)
    {
      return;
    }

    /*
     * The second row goes above -- `PIXEL`'s `DEY / BPL PX3 / LDY #1` -- and on the top row of a character
     * cell it goes BELOW instead, because decrementing the pixel row would leave the cell.
     *
     * The original does not clamp and neither does this; what differs is whose cell it is. The
     * canvas's rows are eight to a canvas character row and the picture's are eight to a WIDE one,
     * so the test is on this surface's grid -- which keeps the mark inside one palette here for the
     * same reason it keeps it inside one there.
     */
    const int second = ((_y & 7) == 0) ? (_y + 1) : (_y - 1);

    for (int pixel = 0; pixel < MARK_WIDTH; ++pixel)
    {
      _picture.PlotPoint(x + pixel, second);
    }
  }

  void PlotRelativePixel2x(Picture& _picture, std::uint8_t _across, std::uint8_t _down, std::uint8_t _acrossLow, std::uint8_t _downLow,
                           std::uint8_t _distance) noexcept
  {
    const SpaceViewPoint point = ToSpaceViewPoint(_across, _down);

    if (point.offScreen)
    {
      return; // the faithful routine took `PX4` and drew nothing, so neither does this (rule T1)
    }

    /*
     * The half-pixel, and its SIGN.
     *
     * Each axis is sign-magnitude: bit 7 of the high byte is the direction and the low byte is the
     * fraction below the magnitude. So a larger fraction is always further in the direction the
     * sign names -- right and down for a clear bit on x and a set bit on y, because `PIXEL2`
     * measures x rightwards from a negated magnitude and y downwards from a negated one.
     */
    const int acrossBit = static_cast<int>(_acrossLow >> 7);
    const int downBit = static_cast<int>(_downLow >> 7);

    const int x = 2 * static_cast<int>(point.x) + (((_across & 0x80u) != 0u) ? -acrossBit : acrossBit);
    const int y = 2 * static_cast<int>(point.y) + (((_down & 0x80u) != 0u) ? downBit : -downBit);

    PlotPixel2x(_picture, x, y, _distance);
  }

  void DrawCanvasRow2x(Picture& _picture, std::uint8_t _x1, std::uint8_t _x2, std::uint8_t _row) noexcept
  {
    // A line of no length is not drawn at all, and the ends are put the right way round: `HLOIN`'s
    // `CPX X2 / BEQ HL6` and `BCC HL5`, without the marker a twin may not carry.
    const int left = 2 * static_cast<int>((_x1 < _x2) ? _x1 : _x2) + Picture::SPACE_VIEW_MARGIN;
    const int right = 2 * static_cast<int>((_x1 < _x2) ? _x2 : _x1) + Picture::SPACE_VIEW_MARGIN;
    const int top = 2 * static_cast<int>(_row);

    for (int row = top; row < top + 2; ++row)
    {
      for (int x = left; x < right; ++x)
      {
        _picture.PlotPoint(x, row);
      }
    }
  }

  void WriteBitmapByte2x(Picture& _picture, std::uint16_t _canvasOffset, std::uint8_t _value, bool _exclusiveOr) noexcept
  {
    if (_canvasOffset >= Canvas::SCREEN_CELLS)
    {
      return; // screen RAM, not the bitmap -- the cell palettes have their own twins
    }

    // The canvas's layout, run backwards: `row * 320 + cell * 8 + subRow` (ADR-002 section 4).
    const int characterRow = _canvasOffset / Canvas::WIDTH;
    const int within = _canvasOffset % Canvas::WIDTH;
    const int y = characterRow * 8 + (within & 7);
    const int x = within & ~7;

    for (int bit = 0; bit < 8; ++bit)
    {
      const bool set = ((_value >> (7 - bit)) & 1u) != 0u;

      for (int down = 0; down < 2; ++down)
      {
        for (int across = 0; across < 2; ++across)
        {
          const int wideX = 2 * (x + bit) + across;
          const int wideY = 2 * y + down;

          if (_exclusiveOr)
          {
            if (set)
            {
              _picture.PlotPoint(wideX, wideY);
            }
          }
          else if (_picture.Point(wideX, wideY) != set)
          {
            _picture.PlotPoint(wideX, wideY);
          }
        }
      }
    }
  }

  void DrawLine2x(Picture& _picture, Line _line) noexcept
  {
    /*
     * `LOIN`'s own opening: the accumulator seeded at half, and the two spans as magnitudes. Every
     * decision below is the faithful routine's -- which half draws it, whether the ends are
     * exchanged, how many pixels there are, and the slope byte. What the twin does differently is
     * plot TWO pixels where the game plots one, half the size, at half the step.
     */
    const std::uint8_t s2 = 0x80;

    SubResult span = SubtractWithCarry(_line.x2, _line.x1, true);
    const std::uint8_t p2 = span.carry ? span.value : AddWithCarry(static_cast<std::uint8_t>(span.value ^ 0xFFu), 1u, false).value;

    span = SubtractWithCarry(_line.y2, _line.y1, true);
    const std::uint8_t q2 = span.carry ? span.value : AddWithCarry(static_cast<std::uint8_t>(span.value ^ 0xFFu), 1u, false).value;

    /*
     * THE ACCUMULATOR IS THE FAITHFUL ONE AT TWICE THE RATE, and the arithmetic is worth spelling
     * out because it is the whole claim of this routine. The faithful step adds the slope byte per
     * CANVAS column and carries out of 256 to move one canvas row; a wide column is half a canvas
     * column and a wide row half a canvas row, so the same byte added per WIDE column, carrying out
     * of the same 256, moves one wide row -- and the seed stays at half a canvas row because that is
     * where `LOIN` puts it. After two wide steps the wide row is the doubled faithful row or the one
     * beside it, which is what a one-pixel line inside a two-pixel one is.
     */
    int accumulator = s2;

    if (q2 < p2)
    {
      // `STPX`'s half: the shallow one, one pixel per column.
      bool swapped = false;
      if (_line.x1 >= _line.x2)
      {
        swapped = true;
        std::swap(_line.x1, _line.x2);
        std::swap(_line.y1, _line.y2);
      }

      const std::uint8_t step = LineSlope(q2, p2);
      const bool goingUp = _line.y1 >= _line.y2;

      // The swapped entry counts one more (`LDX P2 / INX / BEQ`), so a span of 255 counts 256, wraps
      // to zero and draws NOTHING. Ninety-six lines of the sweep are that case, and a twin that drew
      // them anyway put 510 pixels on a blank canvas.
      std::uint8_t count = p2;
      int skip = 0;
      if (swapped)
      {
        count = static_cast<std::uint8_t>(p2 + 1u);
        if (count == 0u)
        {
          return;
        }
        skip = 2; // the swapped entry plots nothing on its first pass, which is two wide pixels
      }
      else if (!goingUp && p2 == 0u)
      {
        return; // the downward entry checks for an empty line (`BEQ LIE0`) and the upward one does not
      }

      int x = 2 * static_cast<int>(_line.x1);
      int y = 2 * static_cast<int>(_line.y1);
      const int steps = 2 * static_cast<int>(count);

      for (int at = 0; at < steps; ++at)
      {
        if (skip > 0)
        {
          --skip;
        }
        else
        {
          _picture.PlotPoint(x + Picture::SPACE_VIEW_MARGIN, y);
        }

        accumulator += step;
        if (accumulator >= 256)
        {
          accumulator -= 256;
          y += goingUp ? -1 : 1;
        }
        ++x;
      }
      return;
    }

    // `STPY`'s half: the steep one, one pixel per row, transposed in every particular.
    bool swapped = false;
    if (_line.y1 < _line.y2)
    {
      swapped = true;
      std::swap(_line.x1, _line.x2);
      std::swap(_line.y1, _line.y2);
    }

    // A vertical line keeps a slope of zero rather than dividing, which is `LIfudge`.
    const std::uint8_t step = (p2 != 0u) ? LineSlope(p2, q2) : std::uint8_t{0};
    const bool goingRight = SubtractWithCarry(_line.x2, _line.x1, true).carry;

    // `SEC / LDX Q2 / INX`, and then the swapped entry plots and counts one fewer.
    std::uint8_t count = static_cast<std::uint8_t>(q2 + 1u);
    int skip = swapped ? 0 : 2;
    if (swapped)
    {
      --count;
    }

    int x = 2 * static_cast<int>(_line.x1);
    int y = 2 * static_cast<int>(_line.y1);
    const int steps = 2 * static_cast<int>(count);

    for (int at = 0; at < steps; ++at)
    {
      if (skip > 0)
      {
        --skip;
      }
      else
      {
        _picture.PlotPoint(x + Picture::SPACE_VIEW_MARGIN, y);
      }

      --y;
      accumulator += step;
      if (accumulator >= 256)
      {
        accumulator -= 256;
        x += goingRight ? 1 : -1;
      }
    }
  }

  void SetCellBlock2x(Picture& _picture, int _cellColumn, int _cellRow, CellPalette _palette) noexcept
  {
    for (int down = 0; down < 2; ++down)
    {
      for (int across = 0; across < 2; ++across)
      {
        _picture.SetCell(2 * _cellColumn + across, 2 * _cellRow + down, _palette);
      }
    }
  }

  void SetCellRun2x(Picture& _picture, int _firstCell, int _count, CellPalette _palette) noexcept
  {
    for (int cell = _firstCell; cell < _firstCell + _count; ++cell)
    {
      SetCellBlock2x(_picture, cell % Canvas::CELL_COLUMNS, cell / Canvas::CELL_COLUMNS, _palette);
    }
  }

} // namespace Elite
