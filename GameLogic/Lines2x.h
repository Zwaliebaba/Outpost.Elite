#pragma once

#include "Canvas.h"
#include "Picture.h"

#include <cstdint>

namespace Elite
{

  /*
   * The 640x400 picture's pixel primitives (Design/Resolution.md section 4.3, slice RS-3).
   *
   * `ShipDraw2x.h` draws the lines a ship is made of; this draws everything that is not one -- the
   * marks `PIXEL` and `PIXEL2` make, the runs `HLOIN` makes, and the fills and cell palettes the
   * borders and the screen clears work in. Its faithful twin is `Lines.cpp`, and it is one file for
   * the same reason that one is: the callers are seven and the primitives are four.
   *
   * TWO SIZES OF THING AND TWO RULES FOR THEM, which the building of RS-2 and RS-3 settled and the
   * design did not:
   *
   *   - A MARK OR A LINE THINS. `PIXEL` lights two adjacent bits out of `TWOS2`, so a speck of dust
   *     is two canvas pixels wide; doubled faithfully it would be four. The twin lights the same
   *     shape in HI-RES pixels -- two wide -- which is half the size at twice the placement
   *     accuracy, and it is what makes a distant star a point rather than a smear. RS-2 made the
   *     same trade for a wireframe edge.
   *
   *     AND IT DROPS AN ARTEFACT OF THE TABLE, which was measured rather than assumed. `TWOS2`'s
   *     entry `i` lights pixels `i - 1` and `i`, except entry 0 which lights 0 and 1 -- so the
   *     faithful mark sits to the LEFT of the point it was given at seven x values out of eight and
   *     to the right at the eighth. Swept over 107,778 (x, y, distance) triples the rule holds
   *     exactly, and it is the same multicolour alignment ADR-002 section 7 measured straddling
   *     from: the table is two bits wide because a multicolour pixel is, and the space view is not
   *     in that mode. The twin lights the two hi-res pixels that ARE canvas pixel x, so the mark is
   *     centred on the point at every x rather than at one in eight. Both surfaces always light
   *     canvas pixel x itself, which is what the shadow test compares.
   *   - AN AREA DOUBLES. The border's edges, the rules across the screen and the colour bands are
   *     not marks, they are the frame around the picture; a two-pixel border at 640 across is a
   *     thread. So a fill is drawn at twice the size and looks the same, and only the things drawn
   *     INSIDE the frame get finer.
   *
   * AND TWO COORDINATE SPACES, WHICH ARE NOT INTERCHANGEABLE. This is the distinction that kept the
   * borders out of RS-1 and RS-2 (section 13):
   *
   *   - VIEW coordinates, 0..511 across by 0..287 down, centred on (256, 144). Everything the space
   *     view draws works in them and `Picture::SPACE_VIEW_MARGIN` is added at the plot, exactly as
   *     `ylookup` adds the faithful margin. `PlotPixel2x`, `PlotRelativePixel2x` and
   *     `DrawHorizontalLine2x` take them.
   *   - CANVAS ADDRESSES. The border, the rules and the colour bands are drawn by writing bitmap
   *     BYTES, so there is no x and y in them to double at all. `WriteBitmapByte2x` doubles the
   *     byte instead, which is the only twin an address can honestly have.
   */

  /*
   * 2x of: PlotPixel -- a distance-graded mark at a point in the WIDE VIEW's coordinates.
   *
   * `_distance` is `ZZ` and is the faithful byte, unscaled: which of the two shapes is drawn is
   * `PIXEL`'s decision and not the twin's (rule T1). Only ONE of its two comparisons reaches this:
   * `CMP #144` and `CMP #80` pick between one mark and one mark, so the far case and the middle
   * case are the same drawing and the twin has one test where the faithful routine has two.
   *
   * The second row of a near mark goes ABOVE, except on the top row of a character cell where it
   * goes below -- `PIXEL`'s own `DEY / BPL PX3 / LDY #1`, on THIS surface's cell grid rather than
   * the canvas's, so that a mark stays inside one cell's palette here exactly as it does there.
   */
  /// 2x of: PlotPixel
  void PlotPixel2x(Picture& _picture, int _x, int _y, std::uint8_t _distance) noexcept;

  /*
   * 2x of: PlotRelativePixel -- `PIXEL2`'s mark, and the one twin in the whole track that recovers a
   * REAL bit rather than doubling one.
   *
   * The stardust's position is sixteen bits an axis -- `SX` over `SXL` -- and the faithful plot
   * throws the low byte away. So the wide position is `2 * faithful +/- (fraction >> 7)`, with the
   * sign the one `PIXEL2` reads out of bit 7: a positive offset moves right and down as the
   * magnitude grows, a negative one moves left and up, and the recovered bit has to follow it.
   * That is section 1's table's one honest half-pixel, and it is a byte the game already has rather
   * than an interpolation between two answers it gave (rule T2).
   *
   * `_across` and `_down` are `X1` and `Y1` as `PIXEL2` takes them; `_acrossLow` and `_downLow` are
   * `SXL` and `SYL` AT THE MOMENT OF THE PLOT. The movers overwrite both part way through their
   * loop, so the erase's caller has to stage them beside the position it stages -- which is the
   * whole of why this takes five bytes rather than three.
   */
  /// 2x of: PlotRelativePixel
  void PlotRelativePixel2x(Picture& _picture, std::uint8_t _across, std::uint8_t _down, std::uint8_t _acrossLow, std::uint8_t _downLow,
                           std::uint8_t _distance) noexcept;

  /*
   * 2x of: DrawHorizontalLine -- a run on both of the wide rows the canvas row covers, at twice its
   * ends.
   *
   * A RUN IS A FILL AND FILLS DOUBLE (the header's second rule). `HLOIN` draws the sun's rows and
   * the border's rules, and both are areas rather than marks: a sun drawn one hi-res row per canvas
   * row would be a comb, and a rule one row tall at 640 across is a thread. So the run keeps its
   * height and its width and gains only its ends.
   *
   * IT LIGHTS EXACTLY THE DOUBLED RUN, and it is worth saying because `HLOIN` looks as though it
   * does not. The faithful routine masks the two end bytes and stores whole bytes between, and
   * ADR-002 section 7 calls it "byte-granular" -- but what the byte boundary costs is COLOUR and not
   * extent: the masks put the ends exactly on x1 and x2 - 1, and it is the cell's palette that
   * changes under them, so a line's right edge can come out a different colour from its body while
   * lighting the pixels it was asked for. So there is nothing here to recover and nothing to lose:
   * the wide run is the faithful run doubled, twice as tall and twice as long.
   *
   * Half-open on the right, as `HLOIN`'s `DEC X2` makes it, and it draws nothing for an empty run.
   */
  /// 2x of: DrawHorizontalLine
  void DrawCanvasRow2x(Picture& _picture, std::uint8_t _x1, std::uint8_t _x2, std::uint8_t _row) noexcept;

  /*
   * 2x of: Canvas::Write -- the eight pixels one canvas bitmap byte holds, in the thirty-two the
   * picture holds for them.
   *
   * THE BORDERS AND THE WIPES WRITE ADDRESSES, NOT COORDINATES, and that is why this exists. `BOXS2`
   * exclusive-ors `%00000011` into all eight rows of a character cell, `BLUEBANDS` stores &FF over
   * twenty-four bytes, `BOX` pokes one byte into the bottom right corner, and `TTX66K` zeroes whole
   * pages: none of them has an x and a y to double. Doubling the BYTE is the only honest twin --
   * each of its eight bits becomes two pixels on each of the two wide rows the byte covers, which is
   * exactly what the canvas byte would look like upscaled, drawn natively.
   *
   * `_exclusiveOr` picks between `BOXS2`'s semantics and `BLUEBANDS`'s. An offset outside the bitmap
   * plane is ignored: screen RAM is the cell palettes' business and has its own twins below.
   */
  /// 2x of: Canvas::Write
  void WriteBitmapByte2x(Picture& _picture, std::uint16_t _canvasOffset, std::uint8_t _value, bool _exclusiveOr) noexcept;

  /*
   * 2x of: DrawLine -- one line, one hi-res pixel wide, exclusive-ored so that drawing it again
   * erases it.
   *
   * IT TAKES THE FAITHFUL LINE AND DOUBLES IT ITSELF, and that is the correction RS-3 made to RS-2
   * (Resolution.md section 4.1 and the journal). RS-2 built an exact Bresenham over a separately
   * doubled and re-clipped line, on the reasoning that a hi-res line needs none of `LOIN`'s mask
   * table. The table is indeed not needed. THE SLOPE IS.
   *
   * `LOIN` is not a Bresenham. It is a DDA whose step is `LineSlope(dy, dx)` -- a LOGARITHM-TABLE
   * lookup, the third routine in this track measured to be one -- so the line the game draws is not
   * the straight line between its endpoints, and over a long span it wanders from one. Measured over
   * 18,432 lines, an exact Bresenham at twice the scale put 41,000 wide pixels two canvas pixels
   * from the faithful line, 431 three away and one four; and on the 96 lines where `LOIN`'s count
   * wraps to zero and it draws NOTHING, the exact drawer drew 510 pixels each. Running `LOIN`'s own
   * accumulator instead takes all four of those figures to zero and leaves 1,234 pixels -- 0.03
   * percent -- two away.
   *
   * WHAT IS LEFT AT 0.03 PERCENT is the one thing a twin cannot have: `LOIN` threads the carry out
   * of its SCREEN POINTER arithmetic into the accumulator, so a line crossing a character row
   * advances one extra step (`Lines.cpp`, and section 6.47). That carry is an address's, and a
   * surface with its own geometry has no address to take it from. It moves one pixel of some lines
   * by one canvas pixel, which is inside the shadow test's slack, and it is named here rather than
   * left for somebody to rediscover.
   *
   * `_line` is in the VIEW's coordinates, 0..255 across, exactly as `LOIN` receives it; the doubling
   * and the doubled left margin both happen here.
   */
  void DrawLine2x(Picture& _picture, Line _line) noexcept;

  // ---- the cell palettes ----------------------------------------------------------------------

  /*
   * The four wide cells one canvas cell becomes, given the palette that canvas cell was given.
   *
   * SCREEN RAM IS THE ONE PIECE OF RASTER STATE THE PICTURE KEEPS ITS OWN COPY OF, and this is why.
   * Everything else the raster needs -- `moonflower`, the background register, colour RAM -- is read
   * from the canvas at resolve time, because there is one of each and no twin could disagree about
   * it (section 3.2). The cell palettes are different: RS-5 puts eighty columns of text on a grid
   * the canvas has forty of, and two wide cells that share a canvas cell will want different
   * colours. So the picture holds an 80x50 grid, and every routine that writes screen RAM needs a
   * twin that writes the four cells its one covers.
   */
  /// 2x of: Canvas::Write -- the palette half of it, which is screen RAM rather than the bitmap.
  void SetCellBlock2x(Picture& _picture, int _cellColumn, int _cellRow, CellPalette _palette) noexcept;

  /// The same for a run of canvas cells starting at `_firstCell`, counted in canvas cells across the
  /// forty-column grid, which is the shape every bulk writer has: `RES2`'s wipe, `TT66`'s two rows
  /// of colour, and the loader's border box.
  /// 2x of: ResetCellColours
  void SetCellRun2x(Picture& _picture, int _firstCell, int _count, CellPalette _palette) noexcept;

} // namespace Elite
