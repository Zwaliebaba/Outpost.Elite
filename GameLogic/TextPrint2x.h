#pragma once

#include "Colours.h"
#include "Picture.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

  /*
   * The text layer of the 640x400 picture (Design/Resolution.md section 6, slice RS-1).
   *
   * THE GLYPH DOES NOT CHANGE SIZE. The font is 8x8 and stays 8x8 (Resolution.md ruling 3), so what
   * the resolution buys text is not sharper letters -- it is twice as many of them across the
   * screen, and room to put them somewhere sensible. The faithful printer writes a 40x25 grid; this
   * layer writes the same glyphs, from the same bytes, onto an 80x50 one.
   *
   * SO THE WHOLE OF IT IS A MAPPING, and `TextLayout` is that mapping. Nothing here decides what to
   * print, when, or in what colour: `CHPR` has already decided all three by the time a twin is
   * called, which is rule T1 -- the faithful routine decides and the twin computes (section 4).
   *
   * THE WIDE CURSOR IS NOT STORED, and that is a correction the building made. Resolution.md
   * section 6.2 gave `TextState` a second cursor advanced and returned alongside the first, which
   * would need every one of the forty-odd `text.column = ...` stores in the library to be hooked --
   * and a plain field assignment cannot be hooked in C++ without changing every site. It does not
   * need to be: at the moment a glyph is drawn, the faithful cursor holds exactly the cell the
   * printer is about to use, so the wide cell is a PURE FUNCTION of the faithful cell and the
   * layout, computed where it is needed and kept nowhere. What section 6.2 wanted a stored cursor
   * for is the re-wrapping of RS-5, where the wide column stops being a function of the faithful
   * one; that slice can store what it needs when it needs it, and until then there is nothing to
   * keep in step.
   */

  /// Where one faithful cell lands on the wide grid.
  struct WideCell
  {
    int column = 0;
    int row = 0;
  };

  /*
   * THE PART OF A SCREEN THAT GOES SOMEWHERE ELSE -- what a re-flow is made of (slice RS-5).
   *
   * Everything a `TextLayout` does with its three offsets is one rigid transform applied to every
   * cell, which can MOVE a screen and cannot RE-ARRANGE one. Re-arranging is what Resolution.md
   * section 6.3 asks for: the status screen's equipment list belongs in a second column and the
   * market screen's prices further right, and neither is an offset, because only SOME of the
   * screen moves.
   *
   * A RECTANGLE AND NOT A CELL, and that is the correction the building made. Section 6.2 gives an
   * anchor as `(fieldColumn, fieldRow) -> (wideColumn, wideRow)`, one cell to one cell -- and a
   * field is not a cell. `Map` is a pure function of the faithful cell, so nothing remembers that
   * the E of "EQUIPMENT:" was moved by the time the Q is printed; a per-cell table would have to
   * name all ten, and the equipment list's eleven rows of up to twenty-four characters would be
   * some two hundred and fifty entries kept in step by hand. A rectangle moved to a wide origin
   * says the same thing in one row of a table, and the status screen's whole re-flow is ONE of
   * them.
   *
   * `rowStride` is the anchor's own, because a block that moves usually wants spreading too: the
   * equipment list at twice the row spacing fills the right-hand half of a fifty-row screen that
   * its eleven packed rows would leave two thirds empty.
   *
   * THE SCREEN ROUTINE IS NOT TOLD, which is the whole point of doing it this way: `STATUS` still
   * stores `XC = 6` and still prints down consecutive rows, the faithful character stream stays
   * the stream the fixtures compare, and the table alone decides that those rows land on the
   * right-hand half of a wider screen.
   */
  struct Anchor
  {
    std::uint8_t firstColumn = 0; ///< the faithful cells this covers, inclusive, in canvas cells
    std::uint8_t lastColumn = 0;
    std::uint8_t firstRow = 0;
    std::uint8_t lastRow = 0;
    std::uint8_t wideColumn = 0; ///< where its top-left cell goes instead
    std::uint8_t wideRow = 0;
    std::uint8_t rowStride = 1; ///< and how the block's own rows are spread once it is there

    [[nodiscard]] constexpr bool Covers(std::uint8_t _column, std::uint8_t _row) const noexcept
    {
      return _column >= firstColumn && _column <= lastColumn && _row >= firstRow && _row <= lastRow;
    }
  };

  /*
   * How a view's 40x25 grid sits on the 80x50 one.
   *
   * `rowStride` is the one field that is not an offset, and the space view is why: a message there
   * belongs over the 3D scene at the height the original put it, so its rows are SPREAD over the
   * doubled ones rather than packed at the top. A text screen packs them, because a text screen is
   * text all the way down.
   *
   * `anchors` is CONSULTED FIRST and the offsets are the fallback, so a table says only what moves.
   * The search is linear and stops at the first match, because these tables are a handful of
   * rectangles and a glyph costs eight bitmap writes either way. Overlapping rectangles are legal
   * and the earlier one wins, which is how an exception inside a moved block is written.
   *
   * THE TABLE MUST NOT SEND TWO FAITHFUL CELLS TO ONE WIDE CELL. Nothing here can enforce it -- an
   * anchor is free to name a wide cell the offsets already reach -- so it is a property of each
   * table, swept over the whole 40x25 grid by `PictureTextTests`. It is Risk R24's tripwire and it
   * bites: a table anchored INTO the centred layout's own footprint always collides, because a
   * 40-column screen placed at column 20 already covers columns 20 to 59. A re-flowed screen has
   * to move its offsets as well as its blocks.
   *
   * AND THE CHEAP WAY TO KEEP IT IS OPPOSITE ROW PARITIES, which is worth knowing before writing a
   * table rather than after the sweep rejects three of them. With `rowStride = 2`, every unanchored
   * faithful row lands on a wide row of the same parity as `rowOffset`; give every anchor the other
   * parity and an anchored block can never share a wide row with an unanchored one, so the only
   * collisions left to think about are between anchors, which are a handful of column ranges on one
   * row. The status screen anchors odd and offsets even; the trade screens do the reverse.
   *
   * THERE IS NO `wrapWidth` AND THERE WILL NOT BE, which section 6.2 records with the measurement.
   * The design asked the wide sink to re-wrap the data screen and the briefings at 64 columns, and
   * `DA11` does not merely break a justified line -- it PADS it, widening the gaps until the
   * thirtieth character is a space. The stream the canvas gets already carries thirty-column
   * padding, so re-wrapping it wide means re-justifying, and re-justifying means the picture
   * carrying a different number of SPACE characters from the canvas. That is the one thing no twin
   * here does. The description gets a column of its own instead, which is a table.
   */
  struct TextLayout
  {
    std::uint8_t columnOffset = 0;
    std::uint8_t rowOffset = 0;
    std::uint8_t rowStride = 1;

    /*
     * NO `columnStride`, AND THE BUILDING IS WHAT SETTLED IT (RS-5-e). The short-range chart needs
     * a label's ORIGIN to double, because `TT23` puts a name beside its disc and the discs are at
     * twice their coordinates. A stride cannot do that: `Map` runs per GLYPH, so a column stride of
     * 2 doubles the gap between the letters as well and prints "O r r e r e". A label is a RUN
     * whose start moves and whose letters do not, which is what `TextPrinter::SetLabelRun` is.
     */
    std::span<const Anchor> anchors{};

    [[nodiscard]] constexpr WideCell Map(std::uint8_t _column, std::uint8_t _row) const noexcept
    {
      for (const Anchor& anchor : anchors)
      {
        if (anchor.Covers(_column, _row))
        {
          return WideCell{static_cast<int>(anchor.wideColumn) + static_cast<int>(_column) - static_cast<int>(anchor.firstColumn),
                          static_cast<int>(anchor.wideRow) +
                            (static_cast<int>(_row) - static_cast<int>(anchor.firstRow)) * static_cast<int>(anchor.rowStride)};
        }
      }

      return WideCell{static_cast<int>(columnOffset) + static_cast<int>(_column),
                      static_cast<int>(rowOffset) + static_cast<int>(_row) * static_cast<int>(rowStride)};
    }
  };

  /*
   * A TEXT SCREEN's layout: the faithful grid centred on the wide one, in CANVAS CELLS.
   *
   * Every offset here is measured in canvas cells -- 0 to 39, margins included -- and not in `XC`,
   * which counts from the view's left edge and is four cells further in. The clears count canvas
   * cells because the game's own loops do, and a layout that mixed the two would put a glyph and
   * the clear that is supposed to reach it on different cells (see `SPACE_VIEW_LAYOUT`).
   *
   * Twenty and twelve are (80 - 40) / 2 and (50 - 25) / 2, so every docked screen is the screen it
   * is today, in the middle of a larger one, at half the size. That is deliberately the PROVISIONAL
   * arrangement rather than the finished one -- Resolution.md section 6.3 re-flows each screen for
   * the wider grid, one sub-slice at a time, and until a screen has its own table this is what it
   * gets. It is correct on the day it lands, which is what a default is for.
   */
  inline constexpr TextLayout CENTRED_LAYOUT{20, 12, 1};

  /*
   * The SPACE VIEW's layout, and it is not a re-flow.
   *
   * The rows are what differ: the 3D scene is twice as tall in pixels and a glyph is still eight,
   * so the faithful rows are SPREAD over the doubled ones rather than packed at the top, and a
   * message at row 16 lands on wide row 32 where the original put it.
   *
   * THE COLUMN OFFSET IS THE SAME TWENTY, and the building is what settled that. Resolution.md
   * section 6.2 gives it as twenty-FOUR, and both numbers are right about different things: 24 is
   * measured in `XC`, which starts at the view's own left edge, and 20 is the same place measured
   * in CANVAS CELLS, which start at the screen's. The two clears below work in canvas cells,
   * because that is what the game's own loops count, so a layout measured in `XC` would have put
   * every glyph sixteen cells right of the cell that clears it. Written out: a glyph at `XC` sits
   * on canvas cell `4 + XC` and belongs on wide cell `24 + XC`, and `24 + XC` is `(4 + XC) + 20`.
   */
  inline constexpr TextLayout SPACE_VIEW_LAYOUT{20, 0, 2};

  static_assert(SPACE_VIEW_LAYOUT.Map(4, 16).column == 24, "XC 0 of the space view is wide cell 24");
  static_assert(SPACE_VIEW_LAYOUT.Map(4, 16).row == 32, "the message row is wide row 32");
  static_assert(CENTRED_LAYOUT.Map(4, 1).column == 24, "a text screen's XC 0 is the same column");
  static_assert(CENTRED_LAYOUT.Map(4, 1).row == 13, "and its rows are packed, not spread");

  /*
   * THE STATUS SCREEN, the first screen with a table of its own (slice RS-5-a, sketch accepted
   * 2026-09-08).
   *
   * Two columns where the original had one, which is what ruling 1's "room to put them somewhere
   * sensible" buys a screen whose content is fixed: the seven label lines down the left, the
   * equipment list down the right, both at twice the row spacing so the screen reads as a page
   * rather than as a small block in the middle of a large one.
   *
   * ONE ANCHOR MOVES THE WHOLE EQUIPMENT LIST, heading and items together, because `STATUS` prints
   * them on consecutive canvas rows 12 to 23 and a rectangle covers exactly that. Its stride is 2
   * and the layout's is 2, so the two columns stay in step; what differs is where they start.
   *
   * THE TITLE IS ON WIDE ROW 3 AND NOT 4, and the rule is why. `NLIN3`'s rule is drawn at canvas
   * row 19, so its twin is a wide line at row 38 -- inside the glyphs of wide row 4, which spans
   * rows 32 to 39. Row 3 puts the title above it with the seven pixels of clearance the original
   * has four of. Nothing draws that rule today: `NLIN3`'s is "the canvas's, so a caller draws it"
   * and only the two charts have a caller that does, which is a gap in the docked text screens
   * older than this track and not a re-flow's to close.
   *
   * The offsets carry the rest: canvas column 1 -- where every label starts -- lands on wide column
   * 5, and canvas rows 4 to 10 on wide rows 12 to 24, which puts the first label line level with
   * the equipment heading exactly as the sketch has it.
   */
  inline constexpr std::array<Anchor, 2> STATUS_ANCHORS{{
    {0, 39, 1, 1, 24, 3, 1},    // the title, moved right to sit over both columns
    {0, 39, 12, 23, 36, 12, 2}, // "EQUIPMENT:" and the eleven lines under it, as one block
  }};

  inline constexpr TextLayout STATUS_LAYOUT{4, 4, 2, STATUS_ANCHORS};

  static_assert(STATUS_LAYOUT.Map(11, 1).column == 35, "the title clears the left column");
  static_assert(STATUS_LAYOUT.Map(11, 1).row == 3, "and sits above where the rule would be");
  static_assert(STATUS_LAYOUT.Map(5, 4).column == 9, "the first label, just inside the frame");
  static_assert(STATUS_LAYOUT.Map(5, 4).row == 12, "level with the equipment heading");
  static_assert(STATUS_LAYOUT.Map(5, 12).column == 41, "which the anchor puts in the right column");
  static_assert(STATUS_LAYOUT.Map(5, 12).row == 12);
  static_assert(STATUS_LAYOUT.Map(10, 23).column == 46, "the last line, at its indent");
  static_assert(STATUS_LAYOUT.Map(10, 23).row == 34, "and the last row the list can reach");

  /*
   * THE MARKET SCREENS, which are one layout for two entries (slice RS-5-b, sketch accepted
   * 2026-09-08): `TT167`'s price list and `TT219`'s buy screen print the same table and get the
   * same table here.
   *
   * WHAT 80 COLUMNS BUYS THIS SCREEN IS ONE HEADING ROW INSTEAD OF TWO. The game splits its header
   * across canvas rows 1 and 2 because "UNIT PRICE" and "QUANTITY FOR SALE" do not fit over their
   * columns in forty: row 1 carries "UNIT" and "QUANTITY", row 2 carries "PRODUCT UNIT PRICE FOR
   * SALE". Six anchors put both rows on ONE wide row, interleaved so that each phrase lands over the
   * column it heads. Not a word of it is new -- the tokens and their order are the game's, and only
   * the cells are this table's.
   *
   * The four data anchors are the four fields, each a column range moved as a block, so the price
   * stays right-aligned exactly as `PrintNumber` left it and the units stay under "UNIT".
   */
  inline constexpr std::array<Anchor, 10> BUY_ANCHORS{{
    {0, 14, 2, 2, 4, 4, 1},    // "PRODUCT"
    {15, 19, 2, 2, 28, 4, 1},  // "UNIT", the column of t / kg / g
    {20, 24, 1, 1, 34, 4, 1},  // "UNIT" from the row above ...
    {20, 25, 2, 2, 39, 4, 1},  // ... and "PRICE", so the two read as one phrase
    {25, 34, 1, 1, 46, 4, 1},  // "QUANTITY" from the row above ...
    {26, 39, 2, 2, 56, 4, 1},  // ... and "FOR SALE"
    {0, 17, 4, 20, 4, 8, 2},   // the item's name
    {18, 19, 4, 20, 29, 8, 2}, // its unit
    {20, 24, 4, 20, 39, 8, 2}, // its price, right-aligned as the canvas has it
    {25, 39, 4, 20, 55, 8, 2}, // and how much of it is for sale
  }};

  inline constexpr TextLayout BUY_LAYOUT{4, 1, 2, BUY_ANCHORS};

  static_assert(BUY_LAYOUT.Map(6, 2).column == 10, "PRODUCT over the item names");
  static_assert(BUY_LAYOUT.Map(5, 4).column == 9, "which start at wide column 6");
  static_assert(BUY_LAYOUT.Map(21, 1).row == 4, "the second heading row is folded onto the first");
  static_assert(BUY_LAYOUT.Map(21, 1).column == 35, "UNIT ...");
  static_assert(BUY_LAYOUT.Map(21, 2).column == 40, "... PRICE, one cell along, so the two read as one");
  static_assert(BUY_LAYOUT.Map(24, 4).column == 43, "the price still ends where PrintNumber left it");
  static_assert(BUY_LAYOUT.Map(5, 20).row == 40, "and the seventeenth item is the last row");

  /*
   * THE INVENTORY SCREEN (slice RS-5-b), which is the status screen's shape for the same reason:
   * a short left column of numbers and a list that wants a column of its own.
   *
   * The hold's anchor starts at wide row 4 so that its first ITEM -- canvas row 7, since row 6 is
   * the blank `TT69` leaves -- lands on wide row 6, level with the fuel line. It reaches canvas row
   * 23 because seventeen goods plus the large-cargo-bay line is as far down as `TT210` can print.
   */
  inline constexpr std::array<Anchor, 3> INVENTORY_ANCHORS{{
    {0, 39, 1, 1, 24, 2, 1},  // "INVENTORY", over both columns
    {0, 39, 4, 5, 4, 6, 2},   // the fuel and cash lines
    {0, 39, 6, 23, 36, 4, 2}, // the hold itself, in the right-hand column
  }};

  inline constexpr TextLayout INVENTORY_LAYOUT{4, 1, 2, INVENTORY_ANCHORS};

  static_assert(INVENTORY_LAYOUT.Map(5, 4).row == 6, "the fuel line");
  static_assert(INVENTORY_LAYOUT.Map(5, 4).column == 9, "on the left");
  static_assert(INVENTORY_LAYOUT.Map(5, 7).row == 6, "and the first item, level with it");
  static_assert(INVENTORY_LAYOUT.Map(5, 7).column == 41, "in the right-hand column");

  /*
   * THE EQUIP SHIP SCREEN (slice RS-5-b), whose two columns are the item and its price, and whose
   * fourth anchor is the interesting one.
   *
   * `EQSHP` asks its question on `CLYNS`'s row 21, which is where every message goes and is nowhere
   * near the list at twice the row spacing -- the offsets would put it on wide row 43, thirty rows
   * below an item list that ends at 34. The anchor brings it back under the list. This is the first
   * table to move something for a reason that is not "the screen is wider": it is wider AND taller,
   * and a row number that meant "just under the text" at 25 rows does not at 50.
   */
  inline constexpr std::array<Anchor, 4> EQUIP_ANCHORS{{
    {0, 39, 1, 1, 24, 2, 1},   // "EQUIP SHIP"
    {0, 24, 3, 16, 8, 8, 2},   // the number and the item, thirteen of them at most
    {25, 39, 3, 16, 39, 8, 2}, // the price, right-aligned as the canvas has it
    {0, 39, 20, 23, 8, 38, 2}, // and the prompt, which the offsets would strand at the bottom
  }};

  inline constexpr TextLayout EQUIP_LAYOUT{4, 1, 2, EQUIP_ANCHORS};

  static_assert(EQUIP_LAYOUT.Map(7, 3).column == 15, "the first item's number");
  static_assert(EQUIP_LAYOUT.Map(34, 3).column == 48, "and the price, right-aligned to wide 52");
  static_assert(EQUIP_LAYOUT.Map(7, 16).row == 34, "the thirteenth item is the last one sold");
  static_assert(EQUIP_LAYOUT.Map(5, 21).row == 40, "and the prompt sits under the list, not at 43");


  /*
   * THE DATA ON SYSTEM SCREEN (slice RS-5-d, sketch accepted 2026-09-08), and it is the one screen
   * whose `rowStride` is ONE.
   *
   * `TT25` already double-spaces itself -- every label/value pair is on an odd canvas row with a
   * blank one under it, because `TT68` prints a newline after each -- so a stride of 2 would space
   * it FOUR wide rows apart and leave a screen that is mostly gaps. The offsets keep the game's own
   * spacing and the anchors do the re-flow.
   *
   * THE PAIRS ARE NOT SPLIT, and section 6.3's "columns 4 and 28" cannot be built: `TT25` prints
   * the colon at canvas column 8 in "Economy:Poor Industrial" and 19 in "Gross Productivity:11520 M
   * CR", so there is no column RANGE that is "the label" on every line and no rectangle that could
   * move one. They stay whole on the left.
   *
   * THE DESCRIPTION GETS A COLUMN AND NOT A WIDTH, which is section 6.2's `wrapWidth` measurement:
   * `DA11` pads a justified line as well as breaking it, so the stream already carries thirty-column
   * spacing and re-wrapping it wide would mean the picture printing different space characters from
   * the canvas. Thirty characters is a good measure; eighty columns is room to stand it beside the
   * pairs instead of under them.
   */
  inline constexpr std::array<Anchor, 2> DATA_ANCHORS{{
    {0, 39, 1, 1, 24, 5, 1},    // "DATA ON <system>", over both columns
    {0, 39, 19, 23, 36, 11, 2}, // the description, beside the pairs and level with the first
  }};

  inline constexpr TextLayout DATA_LAYOUT{4, 8, 1, DATA_ANCHORS};

  static_assert(DATA_LAYOUT.Map(5, 3).column == 9, "the first pair, on the left");
  static_assert(DATA_LAYOUT.Map(5, 3).row == 11, "and the description's first line is level with it");
  static_assert(DATA_LAYOUT.Map(5, 19).column == 41, "the description, in a column of its own");
  static_assert(DATA_LAYOUT.Map(5, 19).row == 11);
  static_assert(DATA_LAYOUT.Map(34, 19).column == 70, "whose thirtieth character is still inside the frame");
  static_assert(DATA_LAYOUT.Map(5, 17).row == 25, "the last pair keeps the screen's own spacing");


  /*
   * THE TWO CHARTS (slice RS-5-e), and between them they are the only screens whose TEXT has to
   * know where the DRAWING went.
   *
   * NEITHER MAP MOVES, and that is what the measurement found: `ToSpaceViewPoint` already supplies
   * the space view's 64-pixel margin, so the long-range map is drawn at 512x256 from cell (8, 4)
   * and the short-range one fills the screen -- exactly what section 6.3 asked a translation to
   * produce. There is nothing to translate. What was wrong is that the TEXT was on the centred
   * layout, which put "GALACTIC CHART 1" at wide row 13, in the middle of the map.
   *
   * The long-range chart's title goes above the top rule -- canvas row 23 is picture row 46, so
   * wide row 4 clears it -- and everything else it prints, which is the system name and distance a
   * crosshair move leaves at the bottom, goes below the bottom rule at picture row 304.
   */
  inline constexpr std::array<Anchor, 1> LONG_RANGE_ANCHORS{{
    {0, 39, 1, 1, 24, 4, 1}, // the title, centred over the map and above the rule at picture row 46
  }};

  inline constexpr TextLayout LONG_RANGE_LAYOUT{4, 19, 1, LONG_RANGE_ANCHORS};

  static_assert(LONG_RANGE_LAYOUT.Map(11, 1).column == 35, "the title, centred over a map that runs from cell 8");
  static_assert(LONG_RANGE_LAYOUT.Map(11, 1).row == 4, "and above the top rule");
  static_assert(LONG_RANGE_LAYOUT.Map(11, 20).row == 39, "and the bottom line clears the lower rule");

  /*
   * THE SHORT-RANGE CHART, whose labels must follow their discs.
   *
   * `TT23` derives a label's cell from its disc: the column is the disc's x divided by eight, the
   * row its y divided by eight. The discs are drawn at twice their coordinates, so the ROW doubles
   * with a stride of 2 and every name keeps its disc's height. The COLUMN cannot: a stride runs per
   * glyph and would space the letters as well, so the label's origin is placed by
   * `TextPrinter::SetLabelRun`, which `TT23` calls with the wide cell it has just computed the disc
   * at -- the one thing that knows where the drawing went.
   *
   * THE TITLE IS ANCHORED, because the offsets alone would leave it where the centred layout put it
   * -- and the rule it belongs above is at picture row 38.
   *
   * WHAT IS NOT DONE HERE is section 6.3's "labels no longer collide", and it was measured before it
   * was declined. `TT23` gives a name the row it wants, else the row below, else the row above, and
   * drops it if all three are taken; over all 256 charts of galaxy one that costs 142 names out of
   * 2,668 systems in range -- 5.3%, or 0.4 names on an average chart, the worst chart losing four. A
   * twin that re-ran the test on fifty rows would recover about a hundred across all 256 charts, and
   * would be DECIDING which systems are named, which is rule T1's line; the picture would carry ink
   * the canvas has not, which section 8.1's third clause forbids; and `TT23` feeds "was it named"
   * back into the disc's SIZE through the carry `cpl` leaves, so a differently-named chart is a
   * differently-DRAWN one. Declined, and cheaply: half a name a chart.
   */
  inline constexpr std::array<Anchor, 1> SHORT_RANGE_ANCHORS{{
    {0, 39, 1, 1, 24, 2, 1}, // the title, above the rule at picture row 38
  }};

  inline constexpr TextLayout SHORT_RANGE_LAYOUT{4, 0, 2, SHORT_RANGE_ANCHORS};

  static_assert(SHORT_RANGE_LAYOUT.Map(15, 1).row == 2, "the title clears the rule");
  static_assert(SHORT_RANGE_LAYOUT.Map(15, 1).column == 39, "and is not spaced out by anything");
  static_assert(SHORT_RANGE_LAYOUT.Map(12, 11).row == 22, "a label's row doubles, so it stays on its disc");


  /*
   * `TTX66K`'s own test for which of the two kinds of screen is up, borrowed rather than invented:
   * it branches on `QQ11` being 0 or 13.
   *
   * No origin marker on it, and on nothing else in this file: a twin ports no routine, so a marker
   * would put it in the ledger and claim a label it does not have (Resolution.md rule T4).
   *
   * `TTX66K` uses it to decide whether to draw the dashboard or the text screen's furniture, which
   * is the same division this needs: view 0 and view 13 have the 3D scene under the text and every
   * other view is text to the edges.
   */
  [[nodiscard]] constexpr TextLayout LayoutForView(std::uint8_t _view) noexcept
  {
    return (_view == 0u || _view == 13u) ? SPACE_VIEW_LAYOUT : CENTRED_LAYOUT;
  }

  /// The cells `CHPR` and the two clears work in: the 32 of the 40 that are not margin.
  inline constexpr std::uint8_t TEXT_FIRST_COLUMN = 4;
  inline constexpr std::uint8_t TEXT_LAST_COLUMN = 35;

  /*
   * AND THE WIDE CELLS THEY MAY LAND ON, which is a constraint the frame imposes and not the grid
   * (slice RS-5-f).
   *
   * `TTX66K` draws the screen's border on BOTH surfaces, and on the wide one it fills columns 0 to
   * 6 and 73 to 79 with colour band and puts the vertical rules on 7 and 72 -- measured, not
   * reasoned about. So a table that puts text outside 8..71 puts it under the frame, where the
   * shadow test sees a cell that carries the border instead of the glyph.
   *
   * The interior is 64 columns and the canvas's own text area is 32, so ONE column of text at
   * `columnOffset = 4` occupies 8..39 and leaves 40..71 for a second. That is why every two-column
   * table here anchors its right-hand block to wide column 36: canvas cell 4 lands on 40.
   */
  inline constexpr int WIDE_FIRST_COLUMN = 8;
  inline constexpr int WIDE_LAST_COLUMN = 71;

  /*
   * 2x of: TextPrinter::PrintGlyph -- one glyph, on the cell the layout names.
   *
   * `_glyph` is the eight bytes the faithful printer has just read out of `FONT_DATA`, handed over
   * rather than looked up again: one font lookup in the tree means the two surfaces cannot disagree
   * about WHICH glyph, only about where it goes. Exclusive-or, as the canvas does, so that a
   * message printed a second time takes itself off both surfaces.
   */
  void PrintGlyph2x(Picture& _picture, TextLayout _layout, std::uint8_t _column, std::uint8_t _row,
                    std::span<const std::uint8_t, 8> _glyph, CellPalette _palette) noexcept;

  /// The same, on a wide cell already chosen. `PrintGlyph2x` is this with the layout's answer, and
  /// a caller that has a label run to apply as well works the cell out for itself (`TextPrinter::
  /// WideCellFor`).
  void PrintGlyphAt2x(Picture& _picture, WideCell _cell, std::span<const std::uint8_t, 8> _glyph,
                      CellPalette _palette) noexcept;

  /*
   * 2x of: TextPrinter::PrintGlyph's delete path -- blank the cell outright.
   *
   * The one place the text code does not exclusive-or: character 127 zeroes the cell to the left
   * rather than drawing over it, and the twin does the same to the same cell.
   */
  void EraseCell2x(Picture& _picture, TextLayout _layout, std::uint8_t _column, std::uint8_t _row) noexcept;

  /// Blank a rectangle of the wide grid, which is what both clears below are made of. Inclusive at
  /// both ends, because the rows and columns the game names are.
  void ClearCells2x(Picture& _picture, TextLayout _layout, std::uint8_t _firstColumn, std::uint8_t _lastColumn,
                    std::uint8_t _firstRow, std::uint8_t _lastRow) noexcept;

  /// 2x of: ClearTextArea -- rows 1 to 23, cells 4 to 35, with row 0 and the dashboard left alone.
  void ClearTextArea2x(Picture& _picture, TextLayout _layout) noexcept;

  /// 2x of: ClearMessageRows -- the three rows `CLYNS` wipes, from `MESSAGE_ROW` down.
  void ClearMessageRows2x(Picture& _picture, TextLayout _layout) noexcept;

} // namespace Elite
