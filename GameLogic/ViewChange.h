#pragma once

#include <cstdint>

#include "Arith.h"
#include "Canvas.h"
#include "Controls.h"
#include "Dashboard.h"
#include "ShipDraw.h"
#include "ExtendedTokens.h"
#include "Rng.h"
#include "ShipSlot.h"
#include "Stardust.h"
#include "TextPrint.h"
#include "Tokens.h"

namespace Elite
{

  /*
   * Setting up a screen (slice 3d-d-iii-a).
   *
   * `TT66` clears the screen and redraws its furniture, and `LOOK1` is what a view key reaches. The
   * text-state half of `TT66` was ported in slice 2e; this is the pixels, which that slice left as
   * a seam because the dashboard, the sprites, the border and the colour bands are phase 3's
   * (§6.77).
   *
   * The routines below are the leaves. Each has one caller and all the callers are in this file,
   * which is why they live here rather than with the drawing primitives in `Canvas.h` -- the same
   * question §6.45 settled for the stardust wrappers.
   */

  /*
   * 6502: ZES2k -- zero bytes `_first` down to 1 of the page at `_page`.
   *
   * NOT THE WHOLE PAGE, and the order is the point. `STA (SC),Y / DEY / BNE` stores at `_first`
   * FIRST and then counts down, stopping when Y reaches zero -- so byte 0 of the page is never
   * touched. `TTX66K` follows the call with its own `STA (SC),Y` at Y = 0 to finish the job, and a
   * port that zeroed the page would agree with the game everywhere except that one byte.
   *
   * `ZES1k` is the entry above it: `LDY #0 / STY SC` and then straight in, which makes `_first`
   * zero and so wraps the count all the way round -- 0, then 255 down to 1, the whole page.
   *
   * `_pageBase` is a CANVAS OFFSET and the original's `X` is a page number: `TTX66K` walks X from
   * `HI(SCBASE)` to `HI(DLOC%)`, which is offsets 0, &100, &200 ... here. The translation is the
   * canvas's own -- it is addressed from `SCBASE` rather than from zero -- and doing it at the call
   * site keeps this routine about the loop rather than about the memory map.
   */
  void ZeroPageDown(Canvas& _canvas, std::uint16_t _pageBase, std::uint8_t _first) noexcept;

  /// 6502: ZES1k -- the entry that zeroes a whole page, by entering `ZES2k` with Y = 0 so that the
  /// first `DEY` wraps to 255.
  inline void ZeroWholePage(Canvas& _canvas, std::uint16_t _pageBase) noexcept
  {
    ZeroPageDown(_canvas, _pageBase, 0);
  }

  /*
   * 6502: mvblockK -- copy `_pages` whole pages from `_from` to `_to`, and then `mvbllop`'s tail.
   *
   * The same count-down shape as `ZES2k`: `LDY #0` and then `LDA (V),Y / STA (SC),Y / DEY / BNE`,
   * so a page is copied in the order 0, 255, 254 ... 1. The result is a copied page either way and
   * the trace is not, which matters to a port that compares intermediate state.
   *
   * `mvbllop` is the second entry, with Y already set, and `wantdials` uses it to copy the last
   * &C0 bytes of the dashboard after eight whole pages.
   */
  void CopyPagesDown(Canvas& _canvas, const std::uint8_t* _from, std::uint16_t _to, std::uint8_t _pages, std::uint8_t _first) noexcept;

  /// 6502: BOXS -- a horizontal line right across the screen on row `_row`, through `HLOIN`.
  /// `X1 = 0` and `X2 = 255`, which is the whole 256-pixel width and not the 32 cells of text.
  void DrawScreenRule(Canvas& _canvas, DrawWorkspace& _draw, std::uint8_t _row) noexcept;

  /*
   * 6502: BOXS2 -- EOR one byte into all eight rows of a character cell, eighteen cells down.
   *
   * It EORs rather than stores, so calling it twice puts the screen back -- which is how the
   * border comes and goes without the routine knowing whether it is drawing or rubbing out.
   * `_cell` is `SC` and it steps by &140, one character row.
   */
  void ToggleVerticalEdge(Canvas& _canvas, std::uint16_t _cell, std::uint8_t _pattern, std::uint8_t _rows) noexcept;

  /// 6502: BLUEBANDS -- 24 bytes of &FF at `_cell`, eighteen character rows down. Two of these make
  /// the coloured bands either side of the space view, and unlike `BOXS2` it STORES.
  void DrawColourBand(Canvas& _canvas, std::uint16_t _cell) noexcept;

  /// 6502: BLUEBAND -- both bands, the left at `SCBASE` and the right 37 cells along.
  void DrawColourBands(Canvas& _canvas) noexcept;

  /*
   * 6502: abraxas, caravanserai and DFLAG -- what the screen is currently set up as.
   *
   * The first two READ LIKE REGISTERS AND ARE NOT. `abraxas` is the value the raster interrupt
   * pokes into `VIC+&18` on its next pass and `caravanserai` the one for `VIC+&11`, so both are
   * ordinary bytes here and only the handler that reads them is hardware. §6.73 made the opposite
   * mistake about `SIGHT`; this is the same question with the answer the other way round.
   *
   * `abraxas` says which block of screen RAM colours the bottom of the screen -- &81 for the one at
   * &6000 and &91 for the one at &6400, which is the dashboard's. `caravanserai` chooses standard
   * or multicolour bitmap for the same half. `DFLAG` is the cheap half of it: non-zero means the
   * dashboard is already on screen, so `wantdials` can skip copying it in again.
   */
  struct ScreenState
  {
    std::uint8_t colourBank = 0x81;  ///< 6502: abraxas
    std::uint8_t bitmapMode = 0xC0;  ///< 6502: caravanserai -- the LOWER half of the screen
    std::uint8_t dashboardShown = 0; ///< 6502: DFLAG

    /// 6502: moonflower -- `caravanserai`'s twin for the upper half, and the energy bomb's whole
    /// effect: flight loop part 3 drops it to %11010000 and the space view goes to standard bitmap
    /// mode for as long as the bomb burns.
    std::uint8_t upperBitmapMode = 0xC0;

    /*
     * 6502: welcome -- the border colour the raster handler cycles while the bomb burns.
     *
     * A table the interrupt indexes rather than a flag: `COMIRQ1` does `LDA welcome,X` and writes
     * VIC register &21, so a non-zero first byte is what makes the background flash. `BOMBOFF`
     * puts it back to zero and `COMIRQ1` increments it, which is the only place it grows.
     */
    std::uint8_t backgroundFlash = 0;

    /*
     * 6502: HFX -- the hyperspace effect's own flag, which the RASTER HANDLER reads.
     *
     * `comirq1` checks it once a frame and scrambles the screen's row addresses while it is set,
     * which is the tearing a jump ends with. Nothing in `GameLogic` reads it; `ZERO` clears it
     * and `LL164` sets it, so it is state the port has to carry even though the thing that acts
     * on it is behind the presentation seam.
     */
    std::uint8_t hyperspaceEffect = 0;
  };

  /// 6502: the two values `wantdials` writes -- screen RAM at &6400 and multicolour with the
  /// extra bit the dashboard's bottom half needs.
  inline constexpr std::uint8_t COLOUR_BANK_DASHBOARD = 0x91;
  inline constexpr std::uint8_t BITMAP_MODE_DASHBOARD = 0xD0;

  /// 6502: NOSPRITES -- switch every sprite off, bracketed by the two raster-mode changes like
  /// `SIGHT`. Six instructions, and all six are the seam.
  void HideAllSprites(SightEffects& _effects) noexcept;

  /*
   * 6502: BOX2 -- the border: two vertical edges, a byte in the top right, and a rule across row 0.
   *
   * `_rows` IS SPELLED AS AN ASSEMBLER DIRECTIVE. The routine opens `LDX #18 / STX T2`, and
   * `TTX66K` reaches it by falling off its own end through `LDX #25 / EQUB &2C` -- the `&2C` is
   * `BIT abs`, whose two operand bytes ARE the `LDX #18`, so the fall-through keeps 25 and a `JSR
   * BOX2` gets 18. A text screen is 25 character rows tall and the space view is 18, and that
   * whole distinction is one byte of data standing in for an instruction (§6.79).
   *
   * `T2` carries the count from the first edge to the second, which is why the port writes it
   * rather than using `_rows` twice: they are the same number and the original reads them from
   * different places.
   */
  void DrawBorder(Canvas& _canvas, DrawWorkspace& _draw, std::uint8_t _rows) noexcept;

  /// 6502: LDX #18 -- what a `JSR BOX2` gets, which is the space view's height in character rows.
  inline constexpr std::uint8_t BORDER_ROWS_SPACE_VIEW = 18;

  /// 6502: LDX #25 -- what falling through from `TTX66K` keeps, which is the whole screen.
  inline constexpr std::uint8_t BORDER_ROWS_TEXT_SCREEN = 25;

  /*
   * 6502: zonkscanners -- clear bit 4 of byte 31 in every ship in the bubble.
   *
   * Bit 4 is "this ship is on the scanner", so this is the bookkeeping half of wiping the scanner:
   * the pixels go when the screen is cleared and this is what stops `SCAN` trying to rub out a
   * blip that is no longer there. It skips empty slots and negative types, which is the planet and
   * the sun -- neither of which has a blip to forget.
   */
  void ForgetScannerBlips(Bubble& _bubble) noexcept;

  /*
   * 6502: wantdials -- put the dashboard on screen, or leave it there if it already is.
   *
   * IT TAKES EVERYTHING `DIALS` TAKES, and that is the routine being honest rather than the port
   * being clumsy: `wantdials` draws the border, copies the dashboard picture in, forgets every
   * blip and then draws all seven dials, so a caller has to hand it the whole flight state. The
   * only thing it adds of its own is `DFLAG`.
   *
   * `DFLAG` SKIPS THE EXPENSIVE HALF AND NOT THE CHEAP ONE. With the dashboard already on screen it
   * still redraws the border, still rewrites `abraxas` and `caravanserai`, still draws the bands
   * and still hides the sprites -- what it skips is the 2,240-byte copy, the blip clearing and
   * `DIALS`. So a port that treated the flag as "do nothing" would agree on the pixels the second
   * time and differ on the first.
   */
  void ShowDashboard(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, GeometryWorkspace& _geometry, ScreenState& _screen,
                     Bubble& _bubble, const FlightState& _flight, const FlightStatus& _status, std::uint8_t _fuel, Compass& _compass,
                     SightEffects& _effects) noexcept;

  /*
   * 6502: TTX66K -- clear the screen and draw whichever furniture this view wants.
   *
   * It takes what `wantdials` takes because on two of its paths it IS `wantdials`: `LDA QQ11 / BEQ
   * wantSTEP / CMP #13 / BNE P%+5` tail-jumps there for the space view and for view 13, and the
   * rest of the routine is the text screens' version of the same job.
   *
   * THREE SEPARATE CLEARS, in three different shapes. Screen RAM's colour bytes go first, 32 cells
   * a row for 24 rows in steps of 40. Then the BITMAP up to `DLOC%`, page by page through `ZES1k`,
   * with a partial page and a hand-written last byte to finish it -- the byte `ZES2k` cannot reach
   * (see above). And then, on the text path only, the rest of the bitmap.
   *
   * IT ENDS BY FALLING INTO `BOX2` PAST ITS FIRST INSTRUCTION, so the border it draws is 25
   * character rows and not 18 (§6.79). `_view` is `QQ11`, and views 2, 64 and 128 are the ones
   * that get one band of colour cells rather than two.
   */
  void SetUpScreenPixels(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, GeometryWorkspace& _geometry, TextState& _text,
                         ScreenState& _screen, Bubble& _bubble, const FlightState& _flight, const FlightStatus& _status, std::uint8_t _fuel,
                         Compass& _compass, SightEffects& _effects, std::uint8_t _view) noexcept;

  /// What `LOOK1` and `WARP` reach that is neither memory nor the canvas.
  class ViewEffects
  {
  public:
    virtual ~ViewEffects() = default;

    /// 6502: LDA #0 / JSR DOVDU19 -- a palette change, which on this build writes a VIC-II colour
    /// register. `LOOK1` makes it the first thing it does, before it has even looked at the view.
    virtual void SetPalette(std::uint8_t _colour) = 0;

    /// 6502: LDY #sfxboop / JMP NOISE -- the refusal noise `WARP` makes when it will not warp.
    /// Returns the carry, as `DashboardEffects::PlaySound` does; `WARP` tail-calls and drops it.
    virtual bool PlaySound(std::uint8_t _effect) = 0;
  };

  /// 6502: sfxboop -- the effect number `WARP` asks for when it refuses.
  inline constexpr std::uint8_t SOUND_BOOP = 6;

  /*
   * Everything a screen change works on.
   *
   * One struct for the reason `TradeScreen`, `SaveScreen` and `GameStart` are structs: the
   * alternative is a function with eighteen arguments, written four times. `TTX66` genuinely
   * touches all of this -- the line heaps, the token printer, the message counters, the laser, the
   * stardust and the dashboard -- because clearing the screen means forgetting everything drawn on
   * it, and everything drawn on it belongs to somebody different.
   *
   * The references are what the original's globals are. Nothing here is aggregated state the game
   * does not have: each field is one 6502 label, and the struct is the argument list.
   */
  struct FlightScreen
  {
    Canvas& canvas;
    DrawWorkspace& draw;
    MathWorkspace& math;
    GeometryWorkspace& geometry;

    Stardust& dust;
    PlanetSunState& heaps;
    Bubble& bubble;
    ShipBlock& work; ///< 6502: INWK

    ScreenState& screen;
    TextState& text;
    ExtendedTextState& extended;
    TokenPrinter& printer;
    TextSink& sink;
    MessageState& message;

    FlightState& flight;
    FlightStatus& status;
    Compass& compass;
    Rng& rng;

    CommanderBlock& commander;    ///< 6502: TP -- `SIGHT` only reads it, the flight loop
                                  ///< writes `NOMSL`, `QQ14`, `QQ20`, `FIST` and `BOMB`
    std::uint8_t& trumbleSprites; ///< 6502: TRIBCT

    SightEffects& sight;
    ViewEffects& effects;

    std::uint8_t& view;       ///< 6502: QQ11 -- which screen is up
    std::uint8_t& spaceView;  ///< 6502: VIEW -- which way the player is looking, 0 to 3
    std::uint8_t& explosions; ///< 6502: EV

    /*
     * 6502: tek -- the current system's tech level, which the flight loop READS.
     *
     * Here because part 14 spawns the station and `NWSPS` picks a Coriolis or a Dodo by this byte.
     * It belongs to the docked half -- `CurrentSystem` carries it and arriving writes it -- so this
     * is a reference to that byte and not a second copy of it, the same arrangement as `QQ11` and
     * the commander block.
     */
    std::uint8_t& techLevel;
  };

  /*
   * 6502: TT66, which is `STA QQ11` and then falls into TTX66 -- change to a screen and clear it.
   *
   * The port has had HALF of this since slice 2e: `SetUpTextScreen` is the text state and the
   * pixels were left behind `TradeScreenEffects::ClearToView`, because the dashboard, the sprites,
   * the border and the colour bands were phase 3's (§6.77). This is the whole routine.
   *
   * FOUR THINGS IT FORGETS, and they are the reason it reaches so far: the ball line heap (`LSP`),
   * the sun's (`FLFLLS`), the laser (`LAS2`) and any message on screen (`DLY`, `de`). A screen
   * change wipes the bitmap, so everything that remembers what it drew there has to be told.
   *
   * `QQ17` IS WRITTEN TWICE AND THE SECOND ONE WINS -- 128 near the top and 0 five bytes from the
   * end, so a caller sees ALL CAPS while `DTW2` keeps the 128. §6.29 records the port nearly
   * shipping the first reading.
   *
   * The view's name is printed only on the space view, at column 11 of row 1: `LDA VIEW / ORA #&60`
   * turns 0 to 3 into tokens 96 to 99, then a space, then token 175 -- "VIEW".
   */
  void SetUpScreen(FlightScreen& _screen, std::uint8_t _view) noexcept;

  /*
   * 6502: LOOK1 -- change the view, with `LQ` and `LO2` as its other two paths.
   *
   * THREE EXITS AND THEY DO DIFFERENT AMOUNTS OF WORK. On a non-space screen it sets the view,
   * clears, draws the sights and tail-jumps to `NWSTARS`. On the space view with the SAME view
   * already showing it returns having done nothing but the palette. Otherwise it clears, flips the
   * stardust, wipes the ships and falls into `SIGHT` -- and does NOT reseed the dust, which is why
   * switching views mirrors the field in the diagonal rather than replacing it.
   */
  void ChangeView(FlightScreen& _screen, std::uint8_t _to) noexcept;

  /*
   * 6502: WARP -- the "J" key, which jumps you a long way towards the planet or the sun.
   *
   * It refuses in four cases and the first three are one `ORA` chain: any junk in the slot above
   * the junk count, a space station in the bubble, or witchspace. The fourth is distance -- both
   * the planet and the sun have to be at least two of `MAS2`'s units away, and a NEGATIVE sign
   * byte skips that test for whichever body it belongs to, because a body behind you cannot be
   * flown into.
   *
   * The jump itself is `ADD` with `S`, `R` and `P` all set to &81, which is -1 in sign-magnitude
   * with the low bit set: it subtracts a fixed amount from each body's z. Then the view is reset
   * through `LOOK1` and the main loop counter is forced so the next pass does a full update.
   */
  void Warp(FlightScreen& _screen) noexcept;

} // namespace Elite
