#pragma once

#include "Ports.h"
#include "Universe.h"

#include <cstdint>

#include "Arith.h"
#include "Canvas.h"
#include "Picture.h"
#include "Controls.h"
#include "Dashboard.h"
#include "ShipDraw.h"
#include "ExtendedTokens.h"
#include "Rng.h"
#include "ShipSlot.h"
#include "Stardust.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Trumbles.h"
#include "Universe.h"

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
   * NOT THE WHOLE PAGE, and the order is the point. The loop stores at `_first`
   * FIRST and then counts down, stopping when Y reaches zero -- so byte 0 of the page is never
   * touched. `TTX66K` follows the call with a store of its own at Y = 0 to finish the job, and a
   * port that zeroed the page would agree with the game everywhere except that one byte.
   *
   * `ZES1k` is the entry above it: Y and the pointer's low byte zeroed and then straight in, which
   * makes `_first` zero and so wraps the count all the way round -- 0, then 255 down to 1, the
   * whole page.
   *
   * `_pageBase` is a CANVAS OFFSET and the original's `X` is a page number: `TTX66K` walks X from
   * `HI(SCBASE)` to `HI(DLOC%)`, which is offsets 0, &100, &200 ... here. The translation is the
   * canvas's own -- it is addressed from `SCBASE` rather than from zero -- and doing it at the call
   * site keeps this routine about the loop rather than about the memory map.
   */
  void ZeroPageDown(Canvas& _canvas, std::uint16_t _pageBase, std::uint8_t _first, Picture* _picture = nullptr) noexcept;

  /// 6502: ZES1k -- the entry that zeroes a whole page, by entering `ZES2k` with Y = 0 so that the
  /// first `DEY` wraps to 255.
  inline void ZeroWholePage(Canvas& _canvas, std::uint16_t _pageBase, Picture* _picture = nullptr) noexcept
  {
    ZeroPageDown(_canvas, _pageBase, 0, _picture);
  }

  /*
   * 6502: mvblockK -- copy `_pages` whole pages from `_from` to `_to`, and then `mvbllop`'s tail.
   *
   * The same count-down shape as `ZES2k`: Y starts at zero and the loop reads and stores through
   * it, so a page is copied in the order 0, 255, 254 ... 1. The result is a copied page either way
   * and the trace is not, which matters to a port that compares intermediate state.
   *
   * `mvbllop` is the second entry, with Y already set, and `wantdials` uses it to copy the last
   * &C0 bytes of the dashboard after eight whole pages.
   */
  void CopyPagesDown(Canvas& _canvas, const std::uint8_t* _from, std::uint16_t _to, std::uint8_t _pages, std::uint8_t _first) noexcept;

  /// 6502: BOXS -- a horizontal line right across the screen on row `_row`, through `HLOIN`.
  /// `X1 = 0` and `X2 = 255`, which is the whole 256-pixel width and not the 32 cells of text.
  void DrawScreenRule(Canvas& _canvas, std::uint8_t _row, Picture* _picture = nullptr) noexcept;

  /*
   * 6502: BOXS2 -- EOR one byte into all eight rows of a character cell, eighteen cells down.
   *
   * It EORs rather than stores, so calling it twice puts the screen back -- which is how the
   * border comes and goes without the routine knowing whether it is drawing or rubbing out.
   * `_cell` is `SC` and it steps by &140, one character row.
   */
  void ToggleVerticalEdge(Canvas& _canvas, std::uint16_t _cell, std::uint8_t _pattern, std::uint8_t _rows,
                          Picture* _picture = nullptr) noexcept;

  /// 6502: BLUEBANDS -- 24 bytes of &FF at `_cell`, eighteen character rows down. Two of these make
  /// the coloured bands either side of the space view, and unlike `BOXS2` it STORES.
  void DrawColourBand(Canvas& _canvas, std::uint16_t _cell, Picture* _picture = nullptr) noexcept;

  /// 6502: BLUEBAND -- both bands, the left at `SCBASE` and the right 37 cells along.
  void DrawColourBands(Canvas& _canvas, Picture* _picture = nullptr) noexcept;

  // 6502: abraxas, caravanserai, DFLAG, moonflower, welcome and HFX -- `ScreenState` moved to
  // `Universe.h` with M3-a, because it is state and that is where the state lives now.

  /// 6502: the two values `wantdials` writes -- screen RAM at &6400 and multicolour with the
  /// extra bit the dashboard's bottom half needs.
  inline constexpr std::uint8_t COLOUR_BANK_DASHBOARD = 0x91;
  inline constexpr std::uint8_t BITMAP_MODE_DASHBOARD = 0xD0;

  /// 6502: NOSPRITES -- switch every sprite off, bracketed by the two raster-mode changes like
  /// `SIGHT`. Six instructions, and all six are the seam.
  void HideAllSprites(VideoState& _video, MemoryMap& _map) noexcept;

  /*
   * 6502: BOX2 -- the border: two vertical edges, a byte in the top right, and a rule across row 0.
   *
   * `_rows` IS SPELLED AS AN ASSEMBLER DIRECTIVE. The routine opens by loading 18 into X, and
   * `TTX66K` reaches it by falling off its own end through a load of 25 followed by `EQUB &2C` --
   * the `&2C` is `BIT abs`, whose two operand bytes ARE that load of 18, so the fall-through keeps
   * 25 and a call to `BOX2` gets 18. A text screen is 25 character rows tall and the space view is
   * 18, and that whole distinction is one byte of data standing in for an instruction (§6.79).
   *
   * `T` carries the count from the first edge to the second -- stored and reloaded, because
   * `BOXS2` leaves X at zero -- and it is the kernel's byte, a local since M2-b. The port wrote
   * `T2` until M2-c and nothing read it (§8).
   */
  void DrawBorder(Canvas& _canvas, std::uint8_t _rows, Picture* _picture = nullptr) noexcept;

  /*
   * 6502: BOX -- the whole-screen border, which is `BOX2` with a floor under it.
   *
   * A load of 199 and a call to `BOXS` rules a line across the bottom pixel row, a store puts the
   * byte the rule cannot reach into the bottom right corner, and then a load of 25 with the same
   * `EQUB &2C` after it falls into `BOX2` -- the `BIT abs` trick §6.79 records, so the border gets
   * 25 rows and not 18.
   *
   * `TT66` does not call this; `DEATH` does, once, over the screen `TT66` has just cleared.
   */
  void DrawFullBorder(Canvas& _canvas, Picture* _picture = nullptr) noexcept;

  /// 6502: what a call to `BOX2` gets, which is the space view's height in character rows.
  inline constexpr std::uint8_t BORDER_ROWS_SPACE_VIEW = 18;

  /// 6502: what falling through from `TTX66K` keeps, which is the whole screen.
  inline constexpr std::uint8_t BORDER_ROWS_TEXT_SCREEN = 25;

  /// 6502: the bottom pixel row, which `BOX` rules across before it draws the edges.
  inline constexpr std::uint8_t BOTTOM_RULE_ROW = 199;

  /// 6502: SCBASE+&1F1F -- the bottom right byte, which `BOXS` leaves out because `HLOIN` draws
  /// x 0 to 255 and the screen is 320 wide.
  inline constexpr std::uint16_t BOTTOM_RIGHT_CORNER = 0x1F1F;

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
  void ShowDashboard(Canvas& _canvas, DrawWorkspace& _draw, ScreenState& _screen, Bubble& _bubble, const FlightState& _flight,
                     const FlightStatus& _status, LightYearsTenths _fuel, Compass& _compass, VideoState& _video, MemoryMap& _map,
                     Picture* _picture = nullptr) noexcept;

  /*
   * 6502: TTX66K -- clear the screen and draw whichever furniture this view wants.
   *
   * It takes what `wantdials` takes because on two of its paths it IS `wantdials`: it tail-jumps
   * there for the space view and for view 13, and the rest of the routine is the text screens'
   * version of the same job.
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
  void SetUpScreenPixels(Canvas& _canvas, DrawWorkspace& _draw, TextState& _text, ScreenState& _screen, Bubble& _bubble,
                         const FlightState& _flight, const FlightStatus& _status, LightYearsTenths _fuel, Compass& _compass,
                         VideoState& _video, MemoryMap& _map, std::uint8_t _view, Picture* _picture = nullptr) noexcept;

  /// What `LOOK1` and `WARP` reach that is neither memory nor the canvas.
  /*
   * `ViewEffects` WAS HERE AND IS NOT ANY MORE (M3-b-2b).
   *
   * It ended with one method, `SetPalette`, and the method was in front of NOTHING. `DOVDU19` on
   * the 6502 Second Processor rewrites `VNT3+1` in the interrupt handler to choose a mode 1
   * palette; on THIS build the upstream source assembles the label and the `RTS` and skips the
   * store -- "this subroutine has no effect in this version of Elite", in as many words. So the
   * two `JSR`s are two `JSR`s to an `RTS`, and the port keeps them as the comments below, which is
   * the whole of what they are.
   *
   * THE HEADER SAID OTHERWISE AND WAS WRONG. The comment on the method claimed the call "on this
   * build writes a VIC-II colour register", which is the Master's reading; `Outpost`'s empty
   * implementation had the right one all along and said so. A seam is a claim that something is
   * outside the library, and this one was never true (§6.73's rule, arriving from the other side:
   * a seam scoped before the thing behind it was READ).
   *
   * `PlaySound` went in M3-b-2a: it was one load and a jump into `NOISE`, the refusal noise `WARP`
   * makes when it will not warp, and the SECOND declaration of one routine -- `DashboardEffects`
   * having the other. Both are `PlaySoundEffect` over `Universe::sound` now. `WARP` tail-calls and
   * drops the carry both ways, which is why its caller passed false and discarded the answer
   * (§6.99).
   */

  // `FlightScreen` was the argument list a screen change took -- twenty-seven references, each one
  // 6502 label. Every byte of it is `Universe`'s since M3-a and the four seams are `Ports`'.

  /*
   * 6502: TT66, which stores the view and then falls into TTX66 -- change to a screen and clear it.
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
   * The view's name is printed only on the space view, at column 11 of row 1: the view number ORed
   * with &60 turns 0 to 3 into tokens 96 to 99, then a space, then token 175 -- "VIEW".
   */
  void SetUpScreen(Universe& _universe, Ports& _ports, std::uint8_t _view) noexcept;

  /*
   * The same, with the 640x400 surface's layout for the screen being started (slice RS-5-a).
   *
   * TWO OVERLOADS AND NOT A DEFAULT ARGUMENT, because the default is a function OF `_view` and C++
   * cannot write one parameter's default in terms of another: the three-argument form is
   * `LayoutForView(_view)`, which is what every screen without a table of its own wants.
   *
   * The layout is the CALLER's because `QQ11` does not name a screen -- `STATUS` and `TT213` are
   * both view 8 -- so the one place that knows which screen is starting is the one that says so.
   * It is stored beside the view, in the same breath, and read per glyph from there.
   */
  void SetUpScreen(Universe& _universe, Ports& _ports, std::uint8_t _view, TextLayout _layout) noexcept;

  /*
   * 6502: LOOK1 -- change the view, with `LQ` and `LO2` as its other two paths.
   *
   * THREE EXITS AND THEY DO DIFFERENT AMOUNTS OF WORK. On a non-space screen it sets the view,
   * clears, draws the sights and tail-jumps to `NWSTARS`. On the space view with the SAME view
   * already showing it returns having done nothing but the palette. Otherwise it clears, flips the
   * stardust, wipes the ships and falls into `SIGHT` -- and does NOT reseed the dust, which is why
   * switching views mirrors the field in the diagonal rather than replacing it.
   */
  void ChangeView(Universe& _universe, Ports& _ports, std::uint8_t _to) noexcept;

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
  void Warp(Universe& _universe, Ports& _ports) noexcept;

} // namespace Elite
