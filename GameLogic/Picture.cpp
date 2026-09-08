#include "pch.h"

#include "Picture.h"

#include "VideoState.h"

#include <vector>

/*
 * The 640x400 surface (Design/Resolution.md, slice RS-0).
 *
 * Everything here is integer and deterministic, as the rest of `GameLogic` is: this file draws a
 * picture and takes no decision the game can see. What it MAY NOT do is as important as what it
 * does -- no random number, no canvas write, no game byte moved -- because the whole claim that the
 * screen is a second rendering rather than a second game rests on it (Resolution.md rule T1).
 */
namespace Elite
{

  namespace
  {
    /// The 4x4 corner of a canvas cell one screen cell shows. A canvas cell is eight pixels square
    /// and doubles into a 2x2 block of screen cells, so each of the four takes a quarter of it.
    constexpr int QUARTER = 4;

    /// The canvas cell that covers a screen cell, and which quarter of it this one is.
    struct Quarter
    {
      int cellColumn = 0; ///< the canvas cell's column
      int cellRow = 0;    ///< and its row
      int acrossIn = 0;   ///< 0 or 4: where in that cell this quarter starts
      int downIn = 0;
    };

    [[nodiscard]] constexpr Quarter QuarterOf(int _column, int _row) noexcept
    {
      return Quarter{_column / 2, _row / 2, (_column & 1) * QUARTER, (_row & 1) * QUARTER};
    }
  } // namespace

  void Picture::Clear() noexcept
  {
    m_bitmap.fill(0);
    m_cells.fill(CellPalette{});
    m_dashboard.fill(0);
  }

  void Picture::Resolve(std::span<std::uint8_t> _out, const Canvas& _canvas) const noexcept
  {
    if (_out.size() < static_cast<std::size_t>(WIDTH) * HEIGHT)
    {
      return;
    }

    /*
     * `DFLAG`, through `Canvas::DashboardShown` -- the same flag that decides the canvas's own
     * raster split decides which region the lower rows belong to here.
     *
     * A DOCKED SCREEN HAS NO DASHBOARD REGION AT ALL, which is why this is a flag and not a row
     * comparison: with the dashboard off, all fifty character rows are text over the bitmap plane
     * and the space view's region governs every one of them. That is `Canvas::ResolveCell`'s `lower`
     * test, read from the same byte.
     */
    const bool dashboardShown = _canvas.DashboardShown();

    for (int row = 0; row < CELL_ROWS; ++row)
    {
      const bool dashboardRow = dashboardShown && (row >= DASHBOARD_CELL_ROW);
      const bool native = dashboardRow ? m_native.dashboard : m_native.spaceView;

      for (int column = 0; column < CELL_COLUMNS; ++column)
      {
        std::uint8_t* out = _out.data() + static_cast<std::size_t>(row) * 8 * WIDTH + static_cast<std::size_t>(column) * 8;

        if (!native)
        {
          UpscaleCell(out, _canvas, column, row);
        }
        else if (dashboardRow)
        {
          ResolveDashboardCell(out, column, row);
        }
        else
        {
          ResolveBitmapCell(out, _canvas, column, row);
        }
      }
    }
  }

  void Picture::Resolve(std::span<std::uint8_t> _out, const Canvas& _canvas, const VideoState& _video) const noexcept
  {
    Resolve(_out, _canvas);

    // The same eight hardware sprites over the same bitmap, at twice the coordinates and from the
    // same definitions (ADR-005 §1, Resolution.md §3.3 and §5.4). One blit serves both surfaces.
    CompositeSprites(_out, WIDTH, HEIGHT, SPACE_VIEW_HEIGHT, 2, _canvas, _video);
  }

  void Picture::UpscaleCell(std::uint8_t* _out, const Canvas& _canvas, int _column, int _row) const noexcept
  {
    /*
     * The region has no content of its own yet, so its pixels are the canvas's, doubled.
     *
     * THIS IS THE SCAFFOLDING AND IT IS TEMPORARY BY DESIGN. It is what lets the game be played at
     * 640x400 from the first slice while the twins are built region by region, and the last slice
     * deletes it once `NativeRegions::Complete()` holds (Resolution.md §10, Risk R27). Until then a
     * screenshot is part native and part upscaled, and the journal says which parts.
     *
     * It goes through `Canvas::ResolveCell` rather than decoding the bytes again, so that the thing
     * being doubled is the picture the oracle compares and not a second reading of it.
     */
    const Quarter quarter = QuarterOf(_column, _row);

    std::array<std::uint8_t, 64> cell{};
    _canvas.ResolveCell(quarter.cellColumn, quarter.cellRow, cell.data(), 8);

    for (int down = 0; down < QUARTER; ++down)
    {
      const std::uint8_t* source = cell.data() + static_cast<std::size_t>(quarter.downIn + down) * 8 + quarter.acrossIn;

      for (int repeat = 0; repeat < 2; ++repeat)
      {
        std::uint8_t* line = _out + static_cast<std::size_t>(down * 2 + repeat) * WIDTH;
        for (int across = 0; across < QUARTER; ++across)
        {
          line[across * 2] = source[across];
          line[across * 2 + 1] = source[across];
        }
      }
    }
  }

  void Picture::ResolveBitmapCell(std::uint8_t* _out, const Canvas& _canvas, int _column, int _row) const noexcept
  {
    const CellPalette palette = m_cells[static_cast<std::size_t>(_row) * CELL_COLUMNS + _column];
    const std::uint8_t high = ColourIndex(palette.High());
    const std::uint8_t low = ColourIndex(palette.Low());
    const std::uint8_t* bits = &m_bitmap[static_cast<std::size_t>(_row) * ROW_BYTES + static_cast<std::size_t>(_column) * 8];

    /*
     * `moonflower`'s bit 4, read from the canvas because that is where the game keeps it.
     *
     * The energy bomb reinterprets the SAME BYTES as two-bit pairs instead of one-bit pixels, which
     * is why the bomb scrambles the view rather than tinting it -- and it does that to this surface
     * at the moment it does it to the canvas, from one flag, because there is only one bomb. The
     * fourth colour is the canvas's colour RAM for the cell this one sits in: %11 is a game byte and
     * this surface has no second copy of it (§3.2).
     */
    if (_canvas.SpaceViewMulticolour())
    {
      const Quarter quarter = QuarterOf(_column, _row);
      const std::uint8_t colourRam = _canvas.CellColour(quarter.cellRow * Canvas::CELL_COLUMNS + quarter.cellColumn);
      const std::array<std::uint8_t, 4> colours = {ColourIndex(_canvas.SpaceViewBackground()), high, low,
                                                   static_cast<std::uint8_t>(colourRam & 0x0Fu)};

      for (int subRow = 0; subRow < 8; ++subRow)
      {
        std::uint8_t* line = _out + static_cast<std::size_t>(subRow) * WIDTH;
        for (int pixel = 0; pixel < 4; ++pixel)
        {
          const std::uint8_t colour = colours[(bits[subRow] >> (6 - 2 * pixel)) & 0x03u];
          line[pixel * 2] = colour;
          line[pixel * 2 + 1] = colour;
        }
      }
      return;
    }

    // Standard bitmap mode, which is every ordinary frame: one bit, one pixel, and the cell's two
    // colours. There is no background register in this mode and no colour RAM is read.
    for (int subRow = 0; subRow < 8; ++subRow)
    {
      std::uint8_t* line = _out + static_cast<std::size_t>(subRow) * WIDTH;
      for (int pixel = 0; pixel < 8; ++pixel)
      {
        line[pixel] = (((bits[subRow] >> (7 - pixel)) & 1u) != 0u) ? high : low;
      }
    }
  }

  void Picture::ResolveDashboardCell(std::uint8_t* _out, int _column, int _row) const noexcept
  {
    // The dashboard plane is already colour indices, one per pixel, so a cell of it is a copy.
    const std::size_t base = static_cast<std::size_t>(_row * 8 - SPACE_VIEW_HEIGHT) * WIDTH + static_cast<std::size_t>(_column) * 8;

    for (int subRow = 0; subRow < 8; ++subRow)
    {
      const std::uint8_t* source = &m_dashboard[base + static_cast<std::size_t>(subRow) * WIDTH];
      std::uint8_t* line = _out + static_cast<std::size_t>(subRow) * WIDTH;
      for (int pixel = 0; pixel < 8; ++pixel)
      {
        line[pixel] = source[pixel];
      }
    }
  }

  std::uint64_t Picture::Hash(const Canvas& _canvas) const
  {
    constexpr std::uint64_t OFFSET_BASIS = 14695981039346656037ull;
    constexpr std::uint64_t PRIME = 1099511628211ull;

    /*
     * A hash of what `Resolve` PRODUCES, for `Canvas::Hash`'s reason: what a golden asserts is what
     * a person would see, so a change of representation that produced the same picture should not
     * fail one and a surface that resolves differently should.
     *
     * On the heap and not the stack, which is the one way this differs from `Canvas::Hash`: 256,000
     * bytes is a quarter of a default Windows stack, and a test that overflowed it would report
     * something other than the golden it was checking.
     */
    std::vector<std::uint8_t> resolved(static_cast<std::size_t>(WIDTH) * HEIGHT, std::uint8_t{0});
    Resolve(resolved, _canvas);

    std::uint64_t hash = OFFSET_BASIS;
    for (const std::uint8_t index : resolved)
    {
      hash ^= index;
      hash *= PRIME;
    }
    return hash;
  }

} // namespace Elite
