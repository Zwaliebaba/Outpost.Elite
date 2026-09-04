#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

/*
 * The screen, held the way the C64 held it (ADR-002 section 4).
 *
 * This is not a framebuffer of colours. It is VIC-II bitmap memory, and the VIC-II is in TWO
 * MODES AT ONCE -- the screen is split by a raster interrupt, and `comirq1` reprograms register
 * &16 halfway down.
 *
 * STANDARD BITMAP MODE for the upper part, which is the space view and the whole of every text
 * view. `moonflower` is %11000000, and bit 4 -- the multicolour bit -- is CLEAR. A byte is eight
 * pixels of one bit each:
 *
 *   %1  the high nibble of that cell's byte in screen RAM
 *   %0  the low nibble of the same byte
 *
 * MULTICOLOUR BITMAP MODE for the lower part, and ONLY when the dashboard is on it. `wantdials`
 * sets bit 4 of `caravanserai` and points `abraxas` at the second block of screen RAM; a text
 * view leaves both alone, so its bottom rows are standard like the rest. A byte is then four
 * pixels of two bits each:
 *
 *   %00  the background colour
 *   %01  the high nibble of that cell's byte in screen RAM
 *   %10  the low nibble of the same byte
 *   %11  that cell's nibble of colour RAM
 *
 * GETTING THIS WRONG IS NOT SUBTLE, which is worth saying because the port did get it wrong: an
 * 8x8 font blitted into the bitmap and then decoded as multicolour comes out as half-width
 * stripes, because each PAIR of font bits is read as one two-bit code. Every glyph on screen was
 * unreadable and the cause was one line in `Resolve`.
 *
 * Either way "the colour of a pixel" is not something the game ever stores. It stores bits, and
 * it EORs whole bytes of them. ADR-002 section 7 has the measurement that settles this: three of
 * the eight masks PIXEL can plot set one bit of one multicolour pixel and one bit of the next,
 * which no colour-per-pixel representation can express at all -- and PIXEL draws on the scanner,
 * which is the part of the screen that really is multicolour.
 *
 * So the port keeps the bytes. Resolve() turns them into the 320x200 indexed image the
 * presenter uploads, and that is the only place a colour index appears.
 *
 * The screen array is laid out exactly as the original's memory is, contiguously from SCBASE,
 * because that makes an oracle comparison a byte compare rather than a translation -- and
 * because the game's own address tables run past the end of the bitmap for high y values, and a
 * port that bounds-checked them into a different place would quietly diverge.
 */
class Canvas
{
public:
  // ---- geometry, all of it measured from the game rather than assumed ----------------------

  /// The resolved image: one column per standard-mode pixel, and two per multicolour one, which
  /// is what makes 320 the right width for both halves of the split screen.
  static constexpr int WIDTH = 320;
  static constexpr int HEIGHT = 200;
  static constexpr int CELL_COLUMNS = 40;
  static constexpr int CELL_ROWS = 25;
  static constexpr int ROW_BYTES = CELL_COLUMNS * 8; ///< 320: one character row of the bitmap

  static constexpr std::uint16_t BITMAP_SIZE = 0x2000;
  /*
   * The two blocks of screen RAM, and which is which is NOT what their addresses suggest.
   *
   * 6502: `zebop` is always &81, so the upper part of the screen always takes its colours from
   * &6000 -- the space view and every text view alike. `abraxas` is &81 too until `wantdials`
   * makes it &91, which is the ONE case that uses &6400: the dashboard. So the first block
   * colours the game screen and the second colours the dashboard, and `celllook` -- the table
   * CHPR writes a glyph's colour through -- indexes the first.
   */
  static constexpr std::uint16_t SCREEN_CELLS = 0x2000;    ///< 6502: &6000, via zebop
  static constexpr std::uint16_t DASHBOARD_CELLS = 0x2400; ///< 6502: &6400, via abraxas = &91
  static constexpr std::uint16_t SCREEN_SIZE = 0x2800;

  /// 6502: the 0x20 that ylookup adds to every row -- the space view's left margin, four
  /// character cells. So x 0..255 covers cells 4..35 of 40, which is 128 multicolour pixels of
  /// the 160 across the screen.
  static constexpr std::uint16_t SPACE_VIEW_MARGIN = 0x20;

  /// 6502: DLOC% -- the dashboard starts at character row 18, so the space view is y 0..143.
  /// That is the 144 in the masters' "256 x 144 space view" note on Y = 72.
  static constexpr int DASHBOARD_CELL_ROW = 18;
  static constexpr int SPACE_VIEW_HEIGHT = DASHBOARD_CELL_ROW * 8;

  /// 6502: ylookup -- the bitmap offset of the character row containing screen row _y, left
  /// margin included. The table is extracted too (ROW_ADDRESS_LOW/HIGH) and a test proves this
  /// agrees with it for all 256 values, including the ones past the bottom of the bitmap that
  /// the table also carries.
  [[nodiscard]] static constexpr std::uint16_t RowOffset(std::uint8_t _y) noexcept
  {
    return static_cast<std::uint16_t>(SPACE_VIEW_MARGIN + (_y & 0xF8) * CELL_COLUMNS);
  }

  /// 6502: celllook -- the screen-RAM offset of a character row. The three cells are not a
  /// margin: CHPR writes a glyph's colour after advancing the cursor, so celllook + (XC + 1)
  /// lands on cell 4 + XC, which is where the glyph went.
  [[nodiscard]] static constexpr std::uint16_t CellRowOffset(int _row) noexcept
  {
    return static_cast<std::uint16_t>(SCREEN_CELLS + 3 + CELL_COLUMNS * _row);
  }

  // ---- the bytes ---------------------------------------------------------------------------

  void Clear() noexcept;

  /*
   * Every access is bounds-checked, and the check cannot fire for the routines that exist.
   *
   * The screen is exactly big enough for the addresses the game can form: ylookup's last entry
   * is 0x26E0, plus 248 for the byte within the row and 7 for the pixel row within the cell,
   * which is 0x27DF -- inside 0x2800 with a byte to spare. So this is a guard against a future
   * routine being wrong, not a clamp that changes behaviour today.
   *
   * It is NOT a bitmask. SCREEN_SIZE is 0x2800, which is not a power of two, and masking with
   * SCREEN_SIZE - 1 silently drops bit 11 of every address that has it -- a bug that puts a
   * pixel eight character rows from where it belongs and looks plausible on the way past.
   */
  [[nodiscard]] std::uint8_t Read(std::uint16_t _offset) const noexcept
  {
    return (_offset < SCREEN_SIZE) ? m_screen[_offset] : std::uint8_t{ 0 };
  }

  void Write(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    if (_offset < SCREEN_SIZE)
    {
      m_screen[_offset] = _value;
    }
  }

  /// 6502: EOR (SC),Y / STA (SC),Y -- the only way the drawing code puts anything on screen,
  /// and the reason drawing a thing twice erases it (plan section 4.6).
  void ExclusiveOr(std::uint16_t _offset, std::uint8_t _mask) noexcept
  {
    if (_offset < SCREEN_SIZE)
    {
      m_screen[_offset] ^= _mask;
    }
  }

  [[nodiscard]] std::span<const std::uint8_t> Screen() const noexcept { return m_screen; }
  [[nodiscard]] std::span<std::uint8_t> Screen() noexcept { return m_screen; }

  // ---- colour -----------------------------------------------------------------------------

  /// Colour RAM, which supplies %11. One nibble per cell on the hardware; a byte here, because
  /// the high nibble is never read and pretending otherwise would invent an invariant.
  [[nodiscard]] std::uint8_t CellColour(int _cell) const noexcept { return m_colourCells[_cell]; }
  void SetCellColour(int _cell, std::uint8_t _colour) noexcept { m_colourCells[_cell] = _colour; }

  /// 6502: the VIC-II background register, which supplies %00.
  [[nodiscard]] std::uint8_t Background() const noexcept { return m_background; }
  void SetBackground(std::uint8_t _colour) noexcept { m_background = _colour; }

  /*
   * 6502: DFLAG, and the `abraxas` / `caravanserai` pair it drives -- is the dashboard on screen?
   *
   * ONE FLAG, TWO EFFECTS, and they always move together: with the dashboard shown, character
   * rows 18 to 24 switch to multicolour AND to the second block of screen RAM. Without it the
   * whole screen is standard bitmap mode coloured from the first block, which is every screen
   * this port draws today -- the dashboard is phase 3's.
   */
  [[nodiscard]] bool DashboardShown() const noexcept { return m_dashboardShown; }
  void SetDashboardShown(bool _shown) noexcept { m_dashboardShown = _shown; }

  // ---- the seam ---------------------------------------------------------------------------

  /*
   * 6502: what the VIC-II did on its way to the screen.
   *
   * Writes WIDTH * HEIGHT colour indices, one byte each, which is what ADR-005's R8_UINT texture
   * uploads. Each multicolour pixel becomes two columns, because that is its real width.
   */
  void Resolve(std::span<std::uint8_t> _out) const noexcept;

  /*
   * A hash of the resolved image, for golden tests (ADR-003 section 2).
   *
   * FNV-1a over the 320x200 indices rather than over the raw planes, because what a golden is
   * asserting is what a person would see: a change of representation that produced the same
   * picture should not fail one, and a bitmap that resolves differently should.
   *
   * Deterministic by construction -- no pointers, no padding, no float -- which is what lets the
   * same value hold across Debug and Release and across machines (ADR-003 section 3).
   */
  [[nodiscard]] std::uint64_t Hash() const noexcept;

private:
  std::array<std::uint8_t, SCREEN_SIZE> m_screen{};
  std::array<std::uint8_t, CELL_COLUMNS * CELL_ROWS> m_colourCells{};
  std::uint8_t m_background = 0;
  bool m_dashboardShown = false;
};

/*
 * The zero-page bytes the drawing routines pass their arguments in.
 *
 * Same reasoning as MathWorkspace: the calling convention is part of the behaviour being
 * verified, and several of these are read back by the caller after the call.
 *
 * 6502: X1, Y1, X2, Y2, COL, ZZ, and the scratch the line routines use.
 */
struct DrawWorkspace
{
  std::uint8_t x1 = 0;
  std::uint8_t y1 = 0;
  std::uint8_t x2 = 0;
  std::uint8_t y2 = 0;

  /// 6502: COL -- the colour mask a coloured plot is ANDed with. RED, YELLOW, GREEN and WHITE
  /// are four multicolour pixels each rather than a colour number.
  std::uint8_t col = 0;

  /// 6502: ZZ -- how far away a point is, which is what decides whether PIXEL draws one mark,
  /// two, or a four-pixel square.
  std::uint8_t zz = 0;

  std::uint8_t t2 = 0;
  std::uint8_t r2 = 0;

  /*
   * 6502: SWAP -- did the last line come out with its ends the other way round?
   *
   * It is here rather than with the clipper because ONE byte at 1780 has two writers and two
   * readers, and they do not pair up: `LL145` and `LOIN` both write it, and `BLINE` reads what
   * `LL145` left while `WPLS2` reads what `LOIN` left. Slice 3b modelled it as the clipper's
   * report and `LOIN` kept its own copy in a local, which agreed with the game until `WPLS2`
   * asked `LOIN` for it (§6.46).
   *
   * It is 0 or 255 rather than a bool because `LOIN` writes it with `DEC` and `WPLS2` tests it
   * with `BNE`.
   */
  std::uint8_t swap = 0;

  /*
   * 6502: XX15+4 and XX15+5 (slice 3b).
   *
   * `X1`, `Y1`, `X2` and `Y2` are not four bytes the line drawing owns -- they are the first four
   * of `XX15`, which is SIX, and the geometry in `LL9` uses all six. `LL51` reads them as three
   * sign-magnitude pairs; `LL145` reads them as three sixteen-bit coordinates and returns four
   * eight-bit screen coordinates in the same place, so `XX15+1` is `x1_hi` going in and `Y1`
   * coming out. That is a calling convention, not storage reuse: there is no point between the
   * two meanings at which a copy could be made, so the six bytes are one workspace.
   *
   * They are fields rather than an array because nothing in `LL9`, `LL145` or the clipping ever
   * indexes `XX15` by a register -- every access is `XX15+n` with a literal n. `XX1`, `XX2`,
   * `XX3`, `XX12`, `XX16` and `XX18` are indexed and are arrays; these two are not (§6.37).
   *
   * The original has no separate names for them, so neither does this.
   */
  std::uint8_t xx15Plus4 = 0;
  std::uint8_t xx15Plus5 = 0;
};

// ---- the pixel primitives (slice 1d-a) ------------------------------------------------------

/// 6502: PIXEL -- plot at (_x, _y) with the size taken from the workspace's ZZ. Under 80 it is a
/// four-pixel square, under 144 a two-pixel dash, and beyond that a single mark.
void PlotPixel(Canvas& _canvas, DrawWorkspace& _work, std::uint8_t _x, std::uint8_t _y) noexcept;

/// 6502: PIXEL2 -- the same, for a point given in the space view's own sign-magnitude
/// coordinates relative to the centre. Falls through into PIXEL, so this is that whole path.
/*
 * Returns the exit carry, which one caller reads: `nWq` fills the stardust field with
 * `JSR PIXEL2 / DEY / BNE SAL4` and the next iteration opens with `JSR DORND`, so the generator
 * runs on whatever the plot left (§6.57). The eleventh dropped flag.
 *
 * The stardust's own movers do NOT read it -- they follow the plot with `JSR DV42`, and `DVID4`
 * opens with an `ASL` -- so they discard it explicitly rather than by accident.
 */
[[nodiscard]] bool PlotRelativePixel(Canvas& _canvas, DrawWorkspace& _work) noexcept;

/// 6502: CPIX2 -- a two-pixel dash at (X1, Y1) in the colour in COL. The second pixel can land
/// in the next character cell, and the routine detects that from the mask rather than from x.
void PlotDash(Canvas& _canvas, DrawWorkspace& _work) noexcept;

/// 6502: CPIX4 -- a two-by-two block: CPIX2, then the row above it.
void PlotBlock(Canvas& _canvas, DrawWorkspace& _work) noexcept;

/// 6502: LOIN / LL30 -- a line from (X1, Y1) to (X2, Y2), plotted one BIT at a time so that it
/// alternates between each cell's two colours. The shipped code unrolls it into thirty-two
/// copies reached through self-modifying jumps; this is the two loops those copies are.
void DrawLine(Canvas& _canvas, DrawWorkspace& _work) noexcept;

/// 6502: HLOIN -- a horizontal line from X1 to X2 (exclusive) on row Y1. The ends are masked
/// bytes and everything between is a whole byte, which is why a line's edge can come out a
/// different colour from its body.
void DrawHorizontalLine(Canvas& _canvas, DrawWorkspace& _work) noexcept;

} // namespace Elite
