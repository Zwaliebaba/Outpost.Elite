#pragma once

#include "Colours.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

  struct VideoState; // VideoState.h -- the sprite registers, which `Resolve` composites from.

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
      return (_offset < SCREEN_SIZE) ? m_screen[_offset] : std::uint8_t{0};
    }

    void Write(std::uint16_t _offset, std::uint8_t _value) noexcept
    {
      if (_offset < SCREEN_SIZE)
      {
        m_screen[_offset] = _value;
      }
    }

    /// A cell's palette is a screen RAM byte, and this is the store that says which byte is one.
    void Write(std::uint16_t _offset, CellPalette _palette) noexcept
    {
      Write(_offset, _palette.Byte());
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

    /// 6502: EOR #BULBCOL / STA (SC),Y -- the bulbs toggle a PALETTE in and out of screen RAM.
    void ExclusiveOr(std::uint16_t _offset, CellPalette _palette) noexcept
    {
      ExclusiveOr(_offset, _palette.Byte());
    }

    [[nodiscard]] std::span<const std::uint8_t> Screen() const noexcept
    {
      return m_screen;
    }
    [[nodiscard]] std::span<std::uint8_t> Screen() noexcept
    {
      return m_screen;
    }

    // ---- colour -----------------------------------------------------------------------------

    /*
     * Colour RAM, which supplies %11. One nibble per cell on the hardware; a byte here, because the
     * high nibble is never read and pretending otherwise would invent an invariant.
     *
     * AND A BYTE RATHER THAN A `Colour` BECAUSE THIS IS MEMORY AND NOT A REGISTER (slice 5a). The
     * oracle compares colour RAM address by address, the loader dumps a table straight into it, and
     * `sdump`'s bytes are what they are. A register is where the chip takes four bits and a value
     * becomes a colour, and that is where `Colour` starts -- `ResolveCell` masks this one on the way
     * out, exactly as the VIC-II does on the way in.
     */
    [[nodiscard]] std::uint8_t CellColour(int _cell) const noexcept
    {
      return m_colourCells[_cell];
    }
    void SetCellColour(int _cell, std::uint8_t _colour) noexcept
    {
      m_colourCells[_cell] = _colour;
    }

    /*
     * 6502: VIC+&21, the background register, which supplies %00 -- and there are TWO of it.
     *
     * One register, rewritten twice a frame by `COMIRQ1` from `welcome,X`: the LOWER half gets
     * `welcome+1`, which nothing in the game ever changes, and the upper half gets `welcome`, which
     * the interrupt itself increments while the energy bomb burns. So the port keeps the pair the
     * split makes of it rather than the one byte the chip has, for the same reason it keeps two
     * bitmap modes (§6.155).
     *
     * This one is the lower half's, and it is the one the loader sets before any interrupt exists.
     *
     * THE SETTER TAKES A BYTE AND THE GETTER ANSWERS A `Colour`, WHICH IS THE LATCH (slice 5a).
     * `STA VIC+&21` puts eight bits on the bus and the chip keeps four; the game relies on it,
     * because `COMIRQ1` increments `welcome` on every pass while the energy bomb burns and stores
     * the running count straight into the register. After eight frames of bomb that byte is past
     * 15, and this port resolves the canvas into COLOUR INDICES that the presenter looks up in a
     * sixteen-entry palette -- so the mask is not tidiness, it is the register. It was missing
     * until slice 5a and nothing measured it: the oracle holds the same unlatched byte the port
     * did (`TheRasterInterruptMatchesCOMIRQ1` compares `welcome` at &9C and &FF and agrees), and
     * no test had ever resolved a canvas with a bomb-flashed background.
     */
    [[nodiscard]] Colour Background() const noexcept
    {
      return m_background;
    }
    void SetBackground(std::uint8_t _stored) noexcept
    {
      m_background = ColourOf(_stored);
    }

    /// 6502: welcome -- the SPACE VIEW's background, and only visible while `moonflower` has put
    /// the upper half into multicolour, which is the energy bomb and nothing else.
    [[nodiscard]] Colour SpaceViewBackground() const noexcept
    {
      return m_spaceViewBackground;
    }
    void SetSpaceViewBackground(std::uint8_t _stored) noexcept
    {
      m_spaceViewBackground = ColourOf(_stored);
    }

    /*
     * 6502: moonflower's bit 4 -- is the SPACE VIEW in multicolour mode?
     *
     * Clear for every ordinary frame, and the energy bomb is the one thing that sets it: flight
     * loop part 3 stores %11010000 and `BOMBOFF` puts %11000000 back. The same bytes then decode as
     * four two-bit pixels instead of eight one-bit ones, which is why the bomb scrambles the view
     * rather than tinting it -- the effect is a reinterpretation of the bitmap, not a filter over
     * it, and no colour-per-pixel model could express that (§6.155).
     */
    [[nodiscard]] bool SpaceViewMulticolour() const noexcept
    {
      return m_spaceViewMulticolour;
    }
    void SetSpaceViewMulticolour(bool _multicolour) noexcept
    {
      m_spaceViewMulticolour = _multicolour;
    }

    /*
     * 6502: VIC+&1C and VIC+&28 as `COMIRQ1` leaves them -- index 0 is the space view's pass and
     * index 1 the dashboard's.
     *
     * `santana` is which sprites are multicolour and `lotus` is sprite 1's colour, and the pair
     * exists to do ONE thing: keep the explosion inside the space view. Above the split sprite 1 is
     * multicolour and red; below it, single-colour in colour 0, which draws nothing. The port takes
     * both per screen row, because a burst near the bottom of the view straddles the split and the
     * VIC-II decides this as it scans (§6.155).
     *
     * The defaults are the values `santana` and `lotus` hold, so a canvas nobody has wired to the
     * raster tick still composites the way the hardware does.
     */
    void SetSpriteMulticolour(std::uint8_t _spaceView, std::uint8_t _dashboard) noexcept
    {
      m_spriteMulticolour[0] = _spaceView;
      m_spriteMulticolour[1] = _dashboard;
    }
    void SetExplosionColour(std::uint8_t _spaceView, std::uint8_t _dashboard) noexcept
    {
      m_explosionColour[0] = ColourOf(_spaceView);
      m_explosionColour[1] = ColourOf(_dashboard);
    }

    /*
     * 6502: DFLAG, and the `abraxas` / `caravanserai` pair it drives -- is the dashboard on screen?
     *
     * ONE FLAG, TWO EFFECTS, and they always move together: with the dashboard shown, character
     * rows 18 to 24 switch to multicolour AND to the second block of screen RAM. Without it the
     * whole screen is standard bitmap mode coloured from the first block, which is every docked
     * screen; the flight half sets it every frame through `FlightSession::SyncVideoRegisters`.
     */
    [[nodiscard]] bool DashboardShown() const noexcept
    {
      return m_dashboardShown;
    }
    void SetDashboardShown(bool _shown) noexcept
    {
      m_dashboardShown = _shown;
    }

    // ---- the seam ---------------------------------------------------------------------------

    /*
     * 6502: what the VIC-II did on its way to the screen.
     *
     * Writes WIDTH * HEIGHT colour indices, one byte each, which is what ADR-005's R8_UINT texture
     * uploads. Each multicolour pixel becomes two columns, because that is its real width.
     */
    void Resolve(std::span<std::uint8_t> _out) const noexcept;

    /*
     * The same image with the hardware SPRITES composited over it (ADR-005 section 1).
     *
     * An OVERLOAD rather than a defaulted argument, because the two callers mean different things
     * and both are right. Every docked screen and every golden hash wants the bitmap alone -- that
     * is what the game drew and what the oracle can be compared against -- while the presenter
     * wants what a person would see, which on a C64 includes eight sprites the bitmap knows
     * nothing about. Making sprites the default would quietly change what a golden asserts.
     *
     * `Canvas` supplies two of the three inputs already: the sprite POINTERS are screen-RAM bytes
     * it holds and has compared since section 6.73, and the bitmap underneath is its own. The
     * third is `VideoState`, which is the registers, and it is a parameter because it belongs to
     * the game universe rather than to the screen memory.
     *
     * This is the first drawing in the port with NO oracle behind it, and the honest reason is in
     * `VideoState.h`: the game never rendered a composited image into memory, so there is nothing
     * to compare one against. Everything upstream is compared; the blit rule is documented VIC-II
     * behaviour, and a golden hash plus a hand-checked screenshot is the whole of its coverage.
     */
    void Resolve(std::span<std::uint8_t> _out, const VideoState& _video) const noexcept;

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
    Colour m_background = Colour::Black;
    Colour m_spaceViewBackground = Colour::Black;
    bool m_spaceViewMulticolour = false;

    /// 6502: santana and lotus -- see `SetSpriteMulticolour`. Initialised to what the game holds.
    std::array<std::uint8_t, 2> m_spriteMulticolour = {0xFEu, 0xFCu};
    std::array<Colour, 2> m_explosionColour = {Colour::Red, Colour::Black};
    bool m_dashboardShown = false;
  };

  /*
   * 6502: X1, Y1, X2, Y2 -- a line, as `LOIN` takes it: the first four bytes of `XX15`.
   *
   * A value since M2-c: the line routines take one and the clipper hands one back. `LOIN` may
   * swap its ends on the way, and `DrawnLine` is what it leaves.
   */
  struct Line
  {
    std::uint8_t x1 = 0;
    std::uint8_t y1 = 0;
    std::uint8_t x2 = 0;
    std::uint8_t y2 = 0;
  };

  /*
   * 6502: SC(1 0) -- the screen pointer, and all that is left of the drawing workspace.
   *
   * Everything else that lived here is a value since M2-c: `X1`, `Y1`, `X2` and `Y2` are a `Line`,
   * `XX15+4` and `XX15+5` its two extra bytes in the clipper's `Line16`, `SWAP` what `LOIN` and the
   * clipper return, `COL` and `ZZ` the plot's colour and distance, and `T2` and `R2` were the
   * port's own invention over the kernel's `T` and `R` (§8, M2-c-1).
   *
   * `SC` STAYS, because the DASHBOARD keeps it between calls (slice 3d-b): `DIALS` sets the screen
   * pointer once and `DIL`/`DIL2` advance it seven calls running, so where the next dial goes is
   * what the last one left. The line drawing keeps its own local copy because nothing reads
   * `LOIN`'s, and `CPIX2` returns its one rather than storing it, because `SCAN` reads it
   * immediately -- but a value seven calls live is state, not a return. M4 names it.
   */
  struct DrawWorkspace
  {
    std::uint16_t sc = 0; ///< 6502: SC(1 0)
  };

  // ---- the pixel primitives (slice 1d-a) ------------------------------------------------------

  /// 6502: PIXEL -- plot at (_x, _y) with the size taken from `_distance`, which is `ZZ`: under 80
  /// it is a four-pixel square, under 144 a two-pixel dash, and beyond that a single mark.
  void PlotPixel(Canvas& _canvas, std::uint8_t _across, std::uint8_t _down, std::uint8_t _distance) noexcept;

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
  [[nodiscard]] bool PlotRelativePixel(Canvas& _canvas, std::uint8_t _across, std::uint8_t _down, std::uint8_t _distance) noexcept;

  /*
   * 6502: what `CPIX2` leaves in SC(1 0), Y and X, and `SCAN` is the caller that reads all three.
   *
   * The scanner's stick is drawn by walking on from where the dot finished rather than by plotting
   * (x, y) pairs, so it needs the pointer and the row the dash left behind -- and the mask index,
   * because the stick has the same pixel pattern as the dot's right-hand pixel. Nothing else reads
   * them, but they are the routine's outputs whether or not anybody asks.
   *
   * `address` is ONE sixteen-bit value rather than the two bytes the original keeps, and that is a
   * different judgement from `ScreenPointer`'s in `Lines.cpp`. There the split is load-bearing:
   * `LOIN` runs `SBC #247` for its own borrow and reads the result. Here both moves are a full
   * sixteen-bit add or subtract of 320 with the carry into the high byte spelled out (`SEC` on the
   * way up, a `CPY` that has just set it on the way down) and no exit carry read -- so the pair and
   * the sixteen-bit value are the same number, not merely the same number in practice.
   */
  struct CellCursor
  {
    std::uint16_t address = 0; ///< 6502: SC(1 0) -- the character block, INCLUDING the wrap below
    std::uint8_t row = 0;      ///< 6502: Y -- the pixel row within it
    std::uint8_t pixel = 0;    ///< 6502: X -- x AND 7, the index into CTWOS2
  };

  /// 6502: CPIX2 -- a two-pixel dash at (X1, Y1) in the colour mask `COL` (RED, YELLOW, GREEN and
  /// WHITE are four multicolour pixels each rather than a colour number). The second pixel can
  /// land in the next character cell, and the routine detects that from the mask rather than from
  /// x -- which is why the cursor it returns can point one cell to the right of (X1, Y1)'s own.
  CellCursor PlotDash(Canvas& _canvas, std::uint8_t _across, std::uint8_t _down, PixelPattern _pattern) noexcept;

  /// 6502: CPIX4 -- a two-by-two block: CPIX2, then the row above it. The cursor is the SECOND
  /// call's, which is the row `SCAN` starts its stick from. The original leaves `Y1` decremented;
  /// nothing reads it, and since M2-c nothing can.
  CellCursor PlotBlock(Canvas& _canvas, std::uint8_t _across, std::uint8_t _down, PixelPattern _pattern) noexcept;

  /*
   * What `LOIN` leaves behind: the four bytes as it left them -- the other way round from the line
   * it was given when it drew right to left or bottom to top -- and `SWAP`, which says so.
   *
   * `SWAP` is 0 or 255 in the original, written with a `DEC`; `WPLS2` tests it with `BNE` and reads
   * `X1` and `Y1` after it, and that is the one caller that reads either (§6.46).
   */
  struct DrawnLine
  {
    Line ends;
    bool swapped = false; ///< 6502: SWAP
  };

  /// 6502: LOIN / LL30 -- a line from (X1, Y1) to (X2, Y2), plotted one BIT at a time so that it
  /// alternates between each cell's two colours. The shipped code unrolls it into thirty-two
  /// copies reached through self-modifying jumps; this is the two loops those copies are.
  DrawnLine DrawLine(Canvas& _canvas, Line _line) noexcept;

  /// 6502: HLOIN -- a horizontal line from `_x1` to `_x2` (exclusive) on row `_y`. The ends are
  /// masked bytes and everything between is a whole byte, which is why a line's edge can come out
  /// a different colour from its body. The original swaps the ends and decrements `X2` in place;
  /// no caller reads either afterwards, and `T` and `R` are its own (M2-c).
  void DrawHorizontalLine(Canvas& _canvas, std::uint8_t _x1, std::uint8_t _x2, std::uint8_t _row) noexcept;

} // namespace Elite
