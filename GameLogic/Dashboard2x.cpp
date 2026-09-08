#include "pch.h"

#include "Dashboard2x.h"

#include "Arith.h"
#include "Dashboard.h"
#include "EliteTypes.h"

#include <array>

namespace Elite
{

  namespace
  {
    /// A multicolour pixel is two canvas pixels wide, and therefore four hi-res ones.
    constexpr int FAT_PIXEL = 2;

    /// A dial's four character cells hold sixteen fat pixels, so the twin's step is one canvas
    /// pixel: thirty-two of them across the same width.
    constexpr int BAR_STEPS = 32;

    /// Where one canvas pixel of the dashboard lands on the picture, and how big it is there.
    void FillCanvasPixel(Picture& _picture, int _canvasX, int _canvasY, std::uint8_t _index) noexcept
    {
      for (int down = 0; down < 2; ++down)
      {
        for (int across = 0; across < 2; ++across)
        {
          _picture.SetDot(2 * _canvasX + across, 2 * _canvasY + down, _index);
        }
      }
    }

    /// One hi-res pixel, exclusive-ored: the blips and the compass dot erase by being drawn again,
    /// and both are placed finer than a canvas pixel, so neither writes one.
    void ToggleWidePixel(Picture& _picture, int _wideX, int _wideY, std::uint8_t _index) noexcept
    {
      _picture.ExclusiveOrDot(_wideX, _wideY, _index);
    }

    /// `_sc` as `DIL` and `DIL2` receive it: the character row, the leftmost cell and the row
    /// within it. A dial is four cells wide from there.
    struct DialPlace
    {
      int canvasX = 0;   ///< the leftmost canvas pixel of the four cells
      int canvasTop = 0; ///< the canvas row the first store lands on
    };

    [[nodiscard]] DialPlace PlaceOf(std::uint16_t _screenPointer, int _firstRow) noexcept
    {
      const int characterRow = _screenPointer / Canvas::ROW_BYTES;
      const int within = _screenPointer % Canvas::ROW_BYTES;
      return DialPlace{(within / 8) * 8, characterRow * 8 + _firstRow};
    }
  } // namespace

  std::uint8_t PatternIndex2x(const Canvas& _canvas, int _canvasX, int _canvasY, PixelPattern _pattern) noexcept
  {
    const int cell = (_canvasY / 8) * Canvas::CELL_COLUMNS + (_canvasX / 8);
    const std::array<std::uint8_t, 4> choices = _canvas.DashboardChoices(cell);

    // Which of the byte's four fat pixels this canvas pixel belongs to, and therefore which pair of
    // bits of the pattern selects its colour. `Striped` is the one pattern where that matters.
    const int fat = (_canvasX % 8) / FAT_PIXEL;
    const int code = (PatternByte(_pattern) >> (6 - 2 * fat)) & 0x03;
    return choices[static_cast<std::size_t>(code)];
  }

  void ResolveDashboardCell2x(Picture& _picture, const Canvas& _canvas, int _cellColumn, int _cellRow) noexcept
  {
    std::array<std::uint8_t, 64> cell{};
    _canvas.ResolveCell(_cellColumn, _cellRow, cell.data(), 8);

    for (int subRow = 0; subRow < 8; ++subRow)
    {
      for (int pixel = 0; pixel < 8; ++pixel)
      {
        FillCanvasPixel(_picture, _cellColumn * 8 + pixel, _cellRow * 8 + subRow, cell[static_cast<std::size_t>(subRow) * 8u + static_cast<std::size_t>(pixel)]);
      }
    }
  }

  void CopyDashboardPicture2x(Picture& _picture) noexcept
  {
    /*
     * The table, unpacked a nibble at a time. `SetDot` takes a SCREEN row, so the plane's row 0 is
     * `Picture::SPACE_VIEW_HEIGHT` -- the one coordinate mistake this loop could make, and the
     * reason the offset is added here rather than carried in the table.
     */
    for (int y = 0; y < Picture::DASHBOARD_HEIGHT; ++y)
    {
      const std::uint8_t* row = &DASHBOARD_PICTURE_2X[static_cast<std::size_t>(y) * DASHBOARD_PICTURE_2X_ROW_BYTES];
      const int screenRow = Picture::SPACE_VIEW_HEIGHT + y;

      for (int x = 0; x < Picture::WIDTH; x += 2)
      {
        const std::uint8_t pair = row[x / 2];
        _picture.SetDot(x, screenRow, static_cast<std::uint8_t>(pair >> 4));
        _picture.SetDot(x + 1, screenRow, static_cast<std::uint8_t>(pair & 0x0Fu));
      }
    }
  }

  void DrawBar2x(Picture& _picture, const Canvas& _canvas, std::uint16_t _screenPointer, int _steps, PixelPattern _ink) noexcept
  {
    // `DIL` writes rows 2 to 4 of four character cells, so a bar is three canvas rows tall and six
    // hi-res ones. Doubled and not deepened: ruling 1 keeps the dashboard the same share of the
    // screen, so an area doubles (Lines2x.h's second rule).
    const DialPlace place = PlaceOf(_screenPointer, 2);
    const std::uint8_t background = _canvas.DashboardChoices((place.canvasTop / 8) * Canvas::CELL_COLUMNS + place.canvasX / 8)[0];

    for (int step = 0; step < BAR_STEPS; ++step)
    {
      const int canvasX = place.canvasX + step;
      const std::uint8_t index = (step < _steps) ? PatternIndex2x(_canvas, canvasX, place.canvasTop, _ink) : background;

      for (int row = 0; row < 3; ++row)
      {
        FillCanvasPixel(_picture, canvasX, place.canvasTop + row, index);
      }
    }
  }

  void DrawIndicator2x(Picture& _picture, const Canvas& _canvas, std::uint16_t _screenPointer, int _position) noexcept
  {
    // `DIL2` writes rows 1 to 4, so this bar is four canvas rows tall where `DIL`'s is three.
    const DialPlace place = PlaceOf(_screenPointer, 1);
    const std::uint8_t background = _canvas.DashboardChoices((place.canvasTop / 8) * Canvas::CELL_COLUMNS + place.canvasX / 8)[0];

    for (int step = 0; step < BAR_STEPS; ++step)
    {
      const int canvasX = place.canvasX + step;
      const std::uint8_t index =
        (step == _position) ? PatternIndex2x(_canvas, canvasX, place.canvasTop, DIAL_NORMAL) : background;

      for (int row = 0; row < 4; ++row)
      {
        FillCanvasPixel(_picture, canvasX, place.canvasTop + row, index);
      }
    }
  }

  void SetMissileIndicator2x(Picture& _picture, const Canvas& _canvas, std::uint8_t _missile) noexcept
  {
    // The same cell `MSBAR` chose -- its `DEX / TXA / INX / EOR #3` -- as a column and a row.
    const int indicator = static_cast<int>(static_cast<std::uint8_t>(static_cast<std::uint8_t>(_missile - 1u) ^ 3u));
    const int cell = static_cast<int>(MISSILE_CELL - Canvas::DASHBOARD_CELLS) + indicator;
    ResolveDashboardCell2x(_picture, _canvas, cell % Canvas::CELL_COLUMNS, cell / Canvas::CELL_COLUMNS);
  }

  void ToggleBulb2x(Picture& _picture, const Canvas& _canvas, std::uint16_t _cell) noexcept
  {
    for (int step = 0; step < 2; ++step)
    {
      const int cell = static_cast<int>(_cell - Canvas::DASHBOARD_CELLS) + step * Canvas::CELL_COLUMNS;
      ResolveDashboardCell2x(_picture, _canvas, cell % Canvas::CELL_COLUMNS, cell / Canvas::CELL_COLUMNS);
    }
  }

  void DrawScannerBlip2x(Picture& _picture, const Canvas& _canvas, std::uint8_t _across, int _acrossBit, std::uint8_t _row,
                         std::uint8_t _stickHeight, bool _stickUp, PixelPattern _pattern) noexcept
  {
    /*
     * Where the dot goes, in HI-RES pixels, and it is four times finer than the canvas's.
     *
     * `CPIX2` indexes the ALIGNED mask table, so the faithful dot snaps to a fat pixel -- two canvas
     * pixels, four hi-res ones -- and `X1`'s bottom bit is lost on the way. The twin keeps that bit
     * and adds the top bit of `x_lo` under it, which is a byte the game maintains on every ship and
     * `SCAN` never reads.
     *
     * `Canvas::SPACE_VIEW_MARGIN` is here because `ylookup` is: `X1` is measured from the view's
     * left edge and the bitmap starts four cells in.
     */
    const int wideX = 2 * (Canvas::SPACE_VIEW_MARGIN + static_cast<int>(_across)) + _acrossBit;
    const int wideY = 2 * static_cast<int>(_row);

    // The colour the pattern takes in the cell the faithful dot lands in, which is the cell the
    // canvas would have coloured it from.
    const std::uint8_t index =
      PatternIndex2x(_canvas, Canvas::SPACE_VIEW_MARGIN + static_cast<int>(_across), static_cast<int>(_row), _pattern);

    /*
     * A 4x4 hi-res block, where the canvas draws eight by four (section 5.2). A blip is a MARK, and
     * a mark thins: one fat pixel of width instead of two, at four times the placement accuracy,
     * which is what makes two ships a few units apart two blips rather than one.
     *
     * `CPIX4` draws row `Y1` and then the row above it, so the canvas dot is rows `Y1 - 1` and
     * `Y1`; the twin's four wide rows are those two doubled.
     */
    for (int down = -2; down <= 1; ++down)
    {
      for (int across = 0; across < 4; ++across)
      {
        ToggleWidePixel(_picture, wideX + across, wideY + down, index);
      }
    }

    if (_stickHeight == 0u)
    {
      return; // a ship on the plane of flight has a dot and no stick, which is `SCAN`'s own test
    }

    /*
     * The stick, two hi-res pixels wide where the canvas draws four, from the dot's edge back to
     * the ellipse's line. `SCAN` walks it a canvas row at a time and the twin walks two.
     *
     * It starts one canvas row clear of the dot in both directions -- `VLL1` reaches that with one
     * `DEY` and `VL3` with two `INY`s, which is the symmetry the two different step counts hide.
     */
    const int from = _stickUp ? (wideY - 3) : (wideY + 2);
    const int steps = 2 * static_cast<int>(_stickHeight);

    for (int step = 0; step < steps; ++step)
    {
      const int at = _stickUp ? (from - step) : (from + step);
      ToggleWidePixel(_picture, wideX + 2, at, index);
      ToggleWidePixel(_picture, wideX + 3, at, index);
    }
  }

  void DrawCompassDot2x(Picture& _picture, const Canvas& _canvas, std::uint8_t _across, std::uint8_t _row, PixelPattern _pattern) noexcept
  {
    const int canvasX = Canvas::SPACE_VIEW_MARGIN + static_cast<int>(_across);
    const std::uint8_t index = PatternIndex2x(_canvas, canvasX, static_cast<int>(_row), _pattern);

    const int wideX = 2 * canvasX;
    const int wideY = 2 * static_cast<int>(_row);

    // `DOT`'s `CMP #YELLOW / BNE CPIX2`: a target ahead is a four-pixel block and one behind a
    // two-pixel dash, and the shape is the colour read twice rather than a second decision.
    // Halved as the blip is: one fat pixel of width instead of two.
    const int rows = (_pattern == COMPASS_AHEAD) ? 4 : 2;

    for (int down = 2 - rows; down <= 1; ++down)
    {
      for (int across = 0; across < 4; ++across)
      {
        ToggleWidePixel(_picture, wideX + across, wideY + down, index);
      }
    }
  }

} // namespace Elite
