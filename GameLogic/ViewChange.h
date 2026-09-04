#pragma once

#include <cstdint>

#include "Canvas.h"
#include "ShipSlot.h"

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
void CopyPagesDown(Canvas& _canvas, const std::uint8_t* _from, std::uint16_t _to,
                   std::uint8_t _pages, std::uint8_t _first) noexcept;

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
void ToggleVerticalEdge(Canvas& _canvas, std::uint16_t _cell, std::uint8_t _pattern,
                        std::uint8_t _rows) noexcept;

/// 6502: BLUEBANDS -- 24 bytes of &FF at `_cell`, eighteen character rows down. Two of these make
/// the coloured bands either side of the space view, and unlike `BOXS2` it STORES.
void DrawColourBand(Canvas& _canvas, std::uint16_t _cell) noexcept;

/// 6502: BLUEBAND -- both bands, the left at `SCBASE` and the right 37 cells along.
void DrawColourBands(Canvas& _canvas) noexcept;

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

} // namespace Elite
