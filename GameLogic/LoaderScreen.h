#pragma once

#include "Canvas.h"

#include <cstdint>

namespace Elite
{

  /*
   * The screen the game starts on, which nothing in the game puts there.
   *
   * 6502: the Elite loader, parts 5 and 6 -- "Configure the screen bitmap and copy colour data
   * into screen RAM" and "Copy colour data into colour RAM and configure more screen RAM".
   *
   * THE LOADER'S CODE IS NOT PORTED and this is not a port of it: it is what its two screen parts
   * LEAVE BEHIND, written straight into the canvas. The rest of the loader is a decryptor, a set
   * of memory moves and the VIC-II bank setup, none of which this port has an equivalent for
   * (`Design/Source-Inventory.md` §3 drops all seven parts). These two parts are different because
   * the game reads what they wrote and never writes it itself.
   *
   * WITHOUT IT EVERY PIXEL IS STILL RIGHT AND THE SCREEN IS BLACK. Colour on the C64 is not in the
   * bitmap; it is in screen RAM and colour RAM, one palette per 8x8 cell. `wantdials` copies all
   * 2,240 bytes of the dashboard picture into the bitmap and `BOX2` draws the border box down
   * cells 3 and 36 -- and with the colour memory left at zero, all of it is black ink on a black
   * background. That is the state this port was in until this existed: a title screen showing the
   * prompt, the box, and one green square at the bottom left, which was the missile indicator --
   * the only dashboard cell whose colour byte the GAME writes (`msblob`).
   *
   * IT IS RUN ONCE, BY THE COMPOSITION ROOT, BEFORE THE START SEQUENCE. Nothing the game does
   * afterwards puts these bytes back, and nothing needs to: `TTX66K`'s clear rewrites cells 4 to
   * 35 of rows 0 to 23 and deliberately leaves the four cells either side and the bottom row
   * alone, which is exactly the part the loader coloured.
   */
  void SetUpLoaderScreen(Canvas& _canvas) noexcept;

  /*
   * 6502: LDA #&70 -- foreground colour 7 (yellow) over background colour 0 (black).
   *
   * The border box's palette, and the reason the box is yellow. It is a SCREEN RAM byte, so the
   * high nibble is the colour a set bit takes and the low nibble the colour a clear one takes.
   */
  inline constexpr std::uint8_t SCREEN_YELLOW_ON_BLACK = 0x70;

  /*
   * 6502: LDA #&00 -- black on black, for the three cells outside the border box on each side.
   *
   * The game screen is 256 pixels wide and the screen mode is 320, so four cells each side are
   * margin. The innermost of the four carries the border box; the outer three show nothing at
   * all, and this is what "nothing at all" is spelled as -- not an absence of pixels, but a
   * palette in which both of a bit's two choices are black.
   */
  inline constexpr std::uint8_t SCREEN_BLACK_ON_BLACK = 0x00;

  /*
   * 6502: LDA #&07 -- colour 7, yellow, in the LOW nibble because colour RAM only has one.
   *
   * The top row of colour RAM, and the loader's comment is worth keeping: the top border is drawn
   * as bytes of %11111111, which in multicolour bitmap mode is four pixels of %11 -- the code that
   * reads colour RAM. The top row is in STANDARD mode and does not read it, so this matters only
   * if the raster interrupt that switches the mode back fires late, and then the row comes out
   * yellow instead of flickering. It is a hardware race being insured against, in data.
   */
  inline constexpr std::uint8_t COLOUR_RAM_YELLOW = 0x07;

} // namespace Elite
