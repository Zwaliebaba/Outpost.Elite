#include "pch.h"

#include "TextPrint2x.h"

#include "TextPrint.h"

namespace Elite
{

  namespace
  {
    /// The eight bitmap bytes of one wide cell, or nothing when the layout puts it off the grid.
    /// Off the grid is possible and is not an error: a layout is a proposal about where a screen's
    /// text goes, and a screen that prints outside its own 40x25 grid would take it there.
    [[nodiscard]] bool OnGrid(WideCell _cell) noexcept
    {
      return _cell.column >= 0 && _cell.column < Picture::CELL_COLUMNS && _cell.row >= 0 && _cell.row < Picture::CELL_ROWS;
    }

    [[nodiscard]] std::size_t CellBytes(WideCell _cell) noexcept
    {
      return static_cast<std::size_t>(_cell.row) * Picture::ROW_BYTES + static_cast<std::size_t>(_cell.column) * 8u;
    }
  } // namespace

  void PrintGlyph2x(Picture& _picture, TextLayout _layout, std::uint8_t _column, std::uint8_t _row,
                    std::span<const std::uint8_t, 8> _glyph, CellPalette _palette) noexcept
  {
    PrintGlyphAt2x(_picture, _layout.Map(_column, _row), _glyph, _palette);
  }

  void PrintGlyphAt2x(Picture& _picture, WideCell _cell, std::span<const std::uint8_t, 8> _glyph,
                      CellPalette _palette) noexcept
  {
    const WideCell cell = _cell;
    if (!OnGrid(cell))
    {
      return;
    }

    const std::size_t base = CellBytes(cell);
    for (std::size_t row = 0; row < 8; ++row)
    {
      _picture.ExclusiveOrBitmap(base + row, _glyph[row]);
    }

    // The palette is a STORE and the bits are an exclusive-or, which is the canvas's own split:
    // `CHPR` EORs the glyph into the bitmap and writes `COL2` into screen RAM beside it.
    _picture.SetCell(cell.column, cell.row, _palette);
  }

  void EraseCell2x(Picture& _picture, TextLayout _layout, std::uint8_t _column, std::uint8_t _row) noexcept
  {
    const WideCell cell = _layout.Map(_column, _row);
    if (!OnGrid(cell))
    {
      return;
    }

    const std::size_t base = CellBytes(cell);
    for (std::size_t row = 0; row < 8; ++row)
    {
      _picture.WriteBitmap(base + row, 0);
    }
  }

  void ClearCells2x(Picture& _picture, TextLayout _layout, std::uint8_t _firstColumn, std::uint8_t _lastColumn,
                    std::uint8_t _firstRow, std::uint8_t _lastRow) noexcept
  {
    /*
     * CELL BY CELL THROUGH THE LAYOUT rather than by wide rows, and that is what makes it impossible
     * for a clear and a glyph to disagree about where a cell is: both ask the same `Map`. A clear
     * written in the wide grid's own coordinates would have to reproduce the mapping, and would
     * stop matching the moment a screen got a layout of its own (Resolution.md section 6.3).
     *
     * The rows the stride skips are left alone, and nothing is lost: only a mapped cell is ever
     * drawn on, so only a mapped cell can need clearing.
     */
    for (std::uint8_t row = _firstRow; row <= _lastRow; ++row)
    {
      for (std::uint8_t column = _firstColumn; column <= _lastColumn; ++column)
      {
        EraseCell2x(_picture, _layout, column, row);
        const WideCell cell = _layout.Map(column, row);
        if (OnGrid(cell))
        {
          _picture.SetCell(cell.column, cell.row, CellPalette{});
        }
      }
    }
  }

  void ClearTextArea2x(Picture& _picture, TextLayout _layout) noexcept
  {
    // `T6SL1`'s outer loop visits character rows 1 to 23, and its inner one covers the 32 cells the
    // screen uses. Row 0 keeps the border and the dashboard rows are not its business.
    ClearCells2x(_picture, _layout, TEXT_FIRST_COLUMN, TEXT_LAST_COLUMN, 1, 23);
  }

  void ClearMessageRows2x(Picture& _picture, TextLayout _layout) noexcept
  {
    // `CLYLOOP`'s three passes, each one character row, from `MESSAGE_ROW` downwards.
    ClearCells2x(_picture, _layout, TEXT_FIRST_COLUMN, TEXT_LAST_COLUMN, MESSAGE_ROW, static_cast<std::uint8_t>(MESSAGE_ROW + 2u));
  }

} // namespace Elite
