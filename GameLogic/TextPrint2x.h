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
   * How a view's 40x25 grid sits on the 80x50 one.
   *
   * `rowStride` is the one field that is not an offset, and the space view is why: a message there
   * belongs over the 3D scene at the height the original put it, so its rows are SPREAD over the
   * doubled ones rather than packed at the top. A text screen packs them, because a text screen is
   * text all the way down.
   */
  struct TextLayout
  {
    std::uint8_t columnOffset = 0;
    std::uint8_t rowOffset = 0;
    std::uint8_t rowStride = 1;

    [[nodiscard]] constexpr WideCell Map(std::uint8_t _column, std::uint8_t _row) const noexcept
    {
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
   * 2x of: TextPrinter::PrintGlyph -- one glyph, on the cell the layout names.
   *
   * `_glyph` is the eight bytes the faithful printer has just read out of `FONT_DATA`, handed over
   * rather than looked up again: one font lookup in the tree means the two surfaces cannot disagree
   * about WHICH glyph, only about where it goes. Exclusive-or, as the canvas does, so that a
   * message printed a second time takes itself off both surfaces.
   */
  void PrintGlyph2x(Picture& _picture, TextLayout _layout, std::uint8_t _column, std::uint8_t _row,
                    std::span<const std::uint8_t, 8> _glyph, CellPalette _palette) noexcept;

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
