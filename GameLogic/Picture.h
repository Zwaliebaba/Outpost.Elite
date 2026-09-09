#pragma once

#include "Canvas.h"
#include "Colours.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

  struct VideoState; // VideoState.h -- the sprite registers, which `Resolve` composites from.

  /*
   * The picture a person sees, at 640x400 (Design/Resolution.md).
   *
   * `Canvas` is the C64's screen memory and is what the oracle, the goldens and the recorded
   * fixtures compare. This is the SECOND surface, drawn beside it from the same decisions at twice
   * the geometry, and it is the only one the executable presents. Neither replaces the other: the
   * faithful routines fill the canvas exactly as they always have, and the twins of Resolution.md
   * section 4 fill this one from the same inputs at the same call sites.
   *
   * WHY IT IS A VIC-II-SHAPED SURFACE AND NOT AN ARRAY OF COLOURS. The game draws by exclusive-or
   * and erases by drawing again -- `LL9`, `WPLS2`, the stardust, the laser beam, a message printed a
   * second time -- and the whole space view depends on it. An array of colour indices exclusive-ored
   * together produces nonsense where two lines cross; a plane of BITS produces exactly what the C64
   * produces there, which is the alternation between a cell's two colours that a player of the
   * original knows. So the upper surface is one bit per pixel with a palette per character cell,
   * which is standard bitmap mode at twice the width, and erase-by-redraw is exact on it for the
   * same reason ADR-002 section 7 measured it exact on the canvas.
   *
   * THE DASHBOARD IS THE EXCEPTION AND IS AN INDEX PLANE. A scanner cell holds a red blip, a yellow
   * one and the ellipse's own colour at once, which two colours a cell cannot express; the original
   * solves it with multicolour mode, whose four colours per cell at half the horizontal resolution
   * are the whole reason the dashboard is chunky. An index plane is the 2x answer to the same
   * constraint, and its exclusive-or is on the index -- so two blips overlapping produce a third
   * colour exactly as two `PIXEL` masks do on the canvas.
   *
   * WHAT IT DELIBERATELY DOES NOT HOLD is any of the raster state. `moonflower`, `welcome`, `DFLAG`
   * and the sprite registers are game bytes the faithful code already maintains on the canvas, and
   * `Resolve` reads them from there -- so there is no second copy for a twin to keep in step, and
   * the energy bomb reinterprets this surface's bits at the moment it reinterprets the canvas's.
   * Design/Resolution.md section 3.2 listed a background of its own; building it found the field had
   * no writer that the canvas did not already have, and the design is corrected to match.
   */
  class Picture
  {
  public:
    // ---- geometry, every number the canvas's doubled -----------------------------------------
    //
    // Derived from `Canvas` rather than restated, so that "twice" is a fact the compiler keeps
    // rather than a second set of numbers somebody has to keep in step (AGENTS.md section 6).

    static constexpr int WIDTH = Canvas::WIDTH * 2;                ///< 640
    static constexpr int HEIGHT = Canvas::HEIGHT * 2;              ///< 400
    static constexpr int CELL_COLUMNS = Canvas::CELL_COLUMNS * 2;  ///< 80
    static constexpr int CELL_ROWS = Canvas::CELL_ROWS * 2;        ///< 50
    static constexpr int ROW_BYTES = CELL_COLUMNS * 8;             ///< 640: one character row of the bitmap

    /// The space view's left margin, eight character cells -- twice the four `ylookup` gives the
    /// canvas. A MARGIN and not view: the field of view is the same, at twice the resolution
    /// (Resolution.md ruling 2), so nothing is visible here that was not visible there.
    static constexpr int SPACE_VIEW_MARGIN = Canvas::SPACE_VIEW_MARGIN * 2; ///< 64

    static constexpr int DASHBOARD_CELL_ROW = Canvas::DASHBOARD_CELL_ROW * 2; ///< 36
    static constexpr int SPACE_VIEW_HEIGHT = Canvas::SPACE_VIEW_HEIGHT * 2;   ///< 288
    static constexpr int DASHBOARD_HEIGHT = HEIGHT - SPACE_VIEW_HEIGHT;       ///< 112

    static constexpr std::size_t BITMAP_SIZE = static_cast<std::size_t>(ROW_BYTES) * CELL_ROWS;
    static constexpr std::size_t CELL_COUNT = static_cast<std::size_t>(CELL_COLUMNS) * CELL_ROWS;
    static constexpr std::size_t DASHBOARD_SIZE = static_cast<std::size_t>(WIDTH) * DASHBOARD_HEIGHT;

    static_assert(SPACE_VIEW_HEIGHT == DASHBOARD_CELL_ROW * 8, "the dashboard starts on a character row");
    static_assert(BITMAP_SIZE == 32'000u, "the bitmap plane is 640x400 bits");
    static_assert(DASHBOARD_SIZE == 71'680u, "the dashboard plane is 640x112 indices");

    // ---- the bitmap plane --------------------------------------------------------------------

    /// The byte holding pixel (_x, _y), laid out as the canvas's is at twice the width: character
    /// rows of `ROW_BYTES`, eight bytes to a cell, one byte per pixel row within it.
    [[nodiscard]] static constexpr std::size_t BitmapOffset(int _x, int _y) noexcept
    {
      return static_cast<std::size_t>(_y >> 3) * ROW_BYTES + static_cast<std::size_t>(_x >> 3) * 8u + static_cast<std::size_t>(_y & 7);
    }

    /// The bit within that byte. The high bit is the leftmost pixel, as the VIC-II reads it.
    [[nodiscard]] static constexpr std::uint8_t BitmapMask(int _x) noexcept
    {
      return static_cast<std::uint8_t>(0x80u >> (_x & 7));
    }

    /*
     * Every access is bounds-checked and out of range reads as zero, for the reason `Canvas`'s are:
     * a guard against a future twin being wrong rather than a clamp that changes a picture. Unlike
     * the canvas there is no address table here that runs past the end on purpose, so a check that
     * fires is a defect every time.
     */
    [[nodiscard]] std::uint8_t ReadBitmap(std::size_t _offset) const noexcept
    {
      return (_offset < BITMAP_SIZE) ? m_bitmap[_offset] : std::uint8_t{0};
    }

    void WriteBitmap(std::size_t _offset, std::uint8_t _value) noexcept
    {
      if (_offset < BITMAP_SIZE)
      {
        m_bitmap[_offset] = _value;
      }
    }

    void ExclusiveOrBitmap(std::size_t _offset, std::uint8_t _mask) noexcept
    {
      if (_offset < BITMAP_SIZE)
      {
        m_bitmap[_offset] ^= _mask;
      }
    }

    /// The twins' primitive: one pixel, exclusive-ored, so that drawing it twice takes it away.
    void PlotPoint(int _x, int _y) noexcept
    {
      if (_x >= 0 && _x < WIDTH && _y >= 0 && _y < HEIGHT)
      {
        ExclusiveOrBitmap(BitmapOffset(_x, _y), BitmapMask(_x));
      }
    }

    [[nodiscard]] bool Point(int _x, int _y) const noexcept
    {
      if (_x < 0 || _x >= WIDTH || _y < 0 || _y >= HEIGHT)
      {
        return false;
      }
      return (ReadBitmap(BitmapOffset(_x, _y)) & BitmapMask(_x)) != 0u;
    }

    [[nodiscard]] std::span<const std::uint8_t> Bitmap() const noexcept
    {
      return m_bitmap;
    }

    // ---- the cell palettes -------------------------------------------------------------------

    /// What a set bit and a clear bit of that cell draw in -- the screen's own `celllook`, written
    /// by the glyph twin and by whatever else colours a cell.
    [[nodiscard]] CellPalette Cell(int _column, int _row) const noexcept
    {
      const std::size_t cell = Index(_column, _row);
      return (cell < CELL_COUNT) ? m_cells[cell] : CellPalette{};
    }

    void SetCell(int _column, int _row, CellPalette _palette) noexcept
    {
      const std::size_t cell = Index(_column, _row);
      if (cell < CELL_COUNT)
      {
        m_cells[cell] = _palette;
      }
    }

    // ---- the dashboard's index plane ---------------------------------------------------------
    //
    // `_y` is a SCREEN row throughout -- 288 to 399 -- rather than a row within the plane, so that
    // a twin works in one coordinate system and cannot be one region's height out.

    [[nodiscard]] std::uint8_t Dot(int _x, int _y) const noexcept
    {
      const std::size_t at = DashboardIndex(_x, _y);
      return (at < DASHBOARD_SIZE) ? m_dashboard[at] : std::uint8_t{0};
    }

    void SetDot(int _x, int _y, std::uint8_t _index) noexcept
    {
      const std::size_t at = DashboardIndex(_x, _y);
      if (at < DASHBOARD_SIZE)
      {
        m_dashboard[at] = static_cast<std::uint8_t>(_index & 0x0Fu);
      }
    }

    /// The blips and the compass dot, which erase by being drawn again exactly as they do on the
    /// canvas -- so the exclusive-or is on the colour INDEX, and two marks crossing make a third
    /// colour the way two multicolour masks do.
    void ExclusiveOrDot(int _x, int _y, std::uint8_t _index) noexcept
    {
      const std::size_t at = DashboardIndex(_x, _y);
      if (at < DASHBOARD_SIZE)
      {
        m_dashboard[at] ^= static_cast<std::uint8_t>(_index & 0x0Fu);
      }
    }

    [[nodiscard]] std::span<const std::uint8_t> Dashboard() const noexcept
    {
      return m_dashboard;
    }

    // ---- the twin switch ----------------------------------------------------------------------

    /*
     * Are the twins drawing? True always, except inside the one test that proves they need not be
     * (Resolution.md section 8.4, `TheReplayIsTheSameWithNoTwins`).
     *
     * WHAT THE TEST IS FOR. Rule T1 says the faithful routine decides and the twin only computes
     * where -- so a twin may consume nothing the game would notice: no random number, no heap
     * pointer, no canvas byte. That was asserted from RS-0 to 2026-09-09 and never measured; the
     * design named a compile-time define for it and the define was never written. Switching the
     * twins off at runtime and requiring the same replay digest is the measurement.
     *
     * WHY THE FLAG LIVES HERE AND NOT ON `Universe`. It is not game state and must never become
     * any: `Universe` is folded by `HashState` and walked cell by cell by `StateCells`, so a flag
     * there would need an exclusion of its own and a row saying why. This surface is ALREADY the
     * field both of them skip (ADR-008 section 4), so a flag inside it inherits that treatment and
     * adds no new hole. It is also why the flag cannot lie: a test that switched the twins off and
     * moved the digest would have proved the opposite of what it set out to.
     *
     * IT DOES NOT GATE THE STORES BELOW, and that is deliberate. The gate is at the CALL SITES,
     * through `DrawingTwins`, so that a twin switched off does not run at all rather than running
     * and writing nowhere -- a twin that rolled the generator and then wrote nothing would still
     * have rolled it, and a test that only blocked the write could not see that.
     */
    [[nodiscard]] bool Drawing() const noexcept
    {
      return m_drawing;
    }
    void SetDrawing(bool _drawing) noexcept
    {
      m_drawing = _drawing;
    }

    // ---- the picture -------------------------------------------------------------------------

    /// Blank every plane. The regions are NOT reset: which slices have landed is a fact about the
    /// program, and clearing the picture is not one of them.
    void Clear() noexcept;

    /*
     * Write `WIDTH * HEIGHT` colour indices -- what the presenter uploads.
     *
     * `_canvas` is here for the RASTER STATE this surface does not hold, and since RS-6 for nothing
     * else: the dashboard-shown flag that decides which region the lower rows are, the energy
     * bomb's mode and background, the colour RAM a multicolour pixel's %11 comes from, and the
     * sprite pointers. Every PIXEL is this surface's own.
     *
     * IT USED TO SUPPLY PIXELS TOO. Between RS-0 and RS-4 a region with no twins yet was drawn from
     * the canvas doubled, which is what let the game be played at 640x400 from the first slice while
     * the twins were built region by region; `NativeRegions` said which regions those were and
     * `UpscaleCell` did the doubling. Both are gone (Resolution.md section 10, Risk R27) -- the
     * scaffolding came down when the last region was taken over, which is the whole point of having
     * written it as scaffolding.
     */
    void Resolve(std::span<std::uint8_t> _out, const Canvas& _canvas) const noexcept;

    /// The same with the hardware sprites over it, at twice their coordinates from the same
    /// definitions. An overload rather than a defaulted argument for `Canvas::Resolve`'s reason:
    /// a golden wants the picture the game drew and a presenter wants what a person would see.
    void Resolve(std::span<std::uint8_t> _out, const Canvas& _canvas, const VideoState& _video) const noexcept;

    /*
     * FNV-1a over the resolved indices, for the screen goldens (Resolution.md section 8.3).
     *
     * NOT `noexcept`, and `Canvas::Hash` is: 640x400 is 256,000 bytes, which is a quarter of a
     * default Windows stack and not something to put on it. The buffer is heap and the allocation
     * is the one thing here that can fail.
     */
    [[nodiscard]] std::uint64_t Hash(const Canvas& _canvas) const;

  private:
    /// One character cell of the picture, each writing eight rows of eight indices `WIDTH` apart.
    /// Three and not one because a cell is one of exactly three things: the canvas doubled, this
    /// surface's own bits through its palette, or a copy out of the dashboard's index plane.
    void ResolveBitmapCell(std::uint8_t* _out, const Canvas& _canvas, int _column, int _row) const noexcept;
    void ResolveDashboardCell(std::uint8_t* _out, int _column, int _row) const noexcept;

    [[nodiscard]] static constexpr std::size_t Index(int _column, int _row) noexcept
    {
      if (_column < 0 || _column >= CELL_COLUMNS || _row < 0 || _row >= CELL_ROWS)
      {
        return CELL_COUNT;
      }
      return static_cast<std::size_t>(_row) * CELL_COLUMNS + static_cast<std::size_t>(_column);
    }

    [[nodiscard]] static constexpr std::size_t DashboardIndex(int _x, int _y) noexcept
    {
      if (_x < 0 || _x >= WIDTH || _y < SPACE_VIEW_HEIGHT || _y >= HEIGHT)
      {
        return DASHBOARD_SIZE;
      }
      return static_cast<std::size_t>(_y - SPACE_VIEW_HEIGHT) * WIDTH + static_cast<std::size_t>(_x);
    }

    std::array<std::uint8_t, BITMAP_SIZE> m_bitmap{};
    std::array<CellPalette, CELL_COUNT> m_cells{};
    std::array<std::uint8_t, DASHBOARD_SIZE> m_dashboard{};

    /// See `Drawing` -- true except inside section 8.4's replay. Not game state and never folded.
    bool m_drawing = true;
  };

  /*
   * Should the twin beside this call site run? -- the ONE condition every twin is guarded by.
   *
   * Two things at once, and they are the same question asked of the two ways a twin can be absent:
   * there may be no surface at all, which is every test and every caller that passes the default
   * `nullptr`, or there may be a surface with the twins switched off, which is section 8.4's replay.
   * A guard that tested only the first would leave the second unreachable; one function tests both
   * and no call site has to remember there are two.
   *
   * It replaced a bare `_picture != nullptr` at forty-seven sites when section 8.4 was finally
   * built, and the replacement is null-safe by construction -- the pointer test is still first and
   * still short-circuits, so a site that was only ever a null check still is one.
   */
  [[nodiscard]] inline bool DrawingTwins(const Picture* _picture) noexcept
  {
    return _picture != nullptr && _picture->Drawing();
  }

} // namespace Elite
