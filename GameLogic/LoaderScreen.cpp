#include "pch.h"

#include "LoaderScreen.h"

#include "Lines2x.h"
#include "LookupTables.h"
#include "TextPrint.h"

namespace Elite
{

  namespace
  {
    /// 6502: &2D0 -- the offset of the dashboard within a block of screen RAM or colour RAM, which
    /// is 18 * 40: the first cell of character row 18, where `DLOC%` starts.
    constexpr std::uint16_t DASHBOARD_CELL_OFFSET = Canvas::DASHBOARD_CELL_ROW * Canvas::CELL_COLUMNS;

    /// 6502: the two cells the border box is drawn down.
    constexpr int BORDER_LEFT_CELL = 3;
    constexpr int BORDER_RIGHT_CELL = 36;

    /*
     * 6502: LOOP10 and LOOP11 -- the border box's palette down one block of screen RAM.
     *
     * One routine because the two loops are the same instructions with a different block and a
     * different count, which is also why they are not one loop in the original: `LOOP10` does 25
     * rows of the text view's block and `LOOP11` does 18 of the space view's, and the seven rows
     * of difference are the dashboard, whose colours come from `sdump` instead.
     */
    void ColourBorderBox(Canvas& _canvas, std::uint16_t _block, int _rows, Picture* _picture) noexcept
    {
      for (int row = 0; row < _rows; ++row)
      {
        const std::uint16_t base = static_cast<std::uint16_t>(_block + row * Canvas::CELL_COLUMNS);

        // 6502: the right edge FIRST, because the left one is reached by decrementing the index
        // from it into the loop below.
        // because the left one is reached by decrementing Y from it into the loop below.
        _canvas.Write(static_cast<std::uint16_t>(base + BORDER_RIGHT_CELL), SCREEN_YELLOW_ON_BLACK);
        _canvas.Write(static_cast<std::uint16_t>(base + BORDER_LEFT_CELL), SCREEN_YELLOW_ON_BLACK);

        /*
         * The picture keeps ONE cell grid and it mirrors the FIRST block (ADR-002 section 4): that
         * is the block `zebop` points at for the whole screen, and the dashboard's own block is the
         * index plane's business rather than a palette's. So `LOOP11`'s pass over the second block
         * has no twin, and only `LOOP10`'s over the first does.
         */
        const int cellRow = (_block == Canvas::SCREEN_CELLS) ? row : -1;
        if (_picture != nullptr && cellRow >= 0)
        {
          SetCellBlock2x(*_picture, BORDER_RIGHT_CELL, cellRow, SCREEN_YELLOW_ON_BLACK);
          SetCellBlock2x(*_picture, BORDER_LEFT_CELL, cellRow, SCREEN_YELLOW_ON_BLACK);
        }

        // 6502: frogl -- cells 2, 1 and 0, counting down.
        for (int cell = BORDER_LEFT_CELL - 1; cell >= 0; --cell)
        {
          _canvas.Write(static_cast<std::uint16_t>(base + cell), SCREEN_BLACK_ON_BLACK);
          if (_picture != nullptr && cellRow >= 0)
          {
            SetCellBlock2x(*_picture, cell, cellRow, SCREEN_BLACK_ON_BLACK);
          }
        }

        // 6502: cells 37, 38 and 39, written out three times rather than looped -- the original's
        // shape and not a transcription slip: A is still black from the loop above and the index
        // is counting the other way.
        // three times rather than looped, which is the original's shape and not a transcription
        // slip: A is still black from the loop above and Y is counting the other way.
        for (int cell = BORDER_RIGHT_CELL + 1; cell < Canvas::CELL_COLUMNS; ++cell)
        {
          _canvas.Write(static_cast<std::uint16_t>(base + cell), SCREEN_BLACK_ON_BLACK);
          if (_picture != nullptr && cellRow >= 0)
          {
            SetCellBlock2x(*_picture, cell, cellRow, SCREEN_BLACK_ON_BLACK);
          }
        }
      }
    }
  } // namespace

  void SetUpLoaderScreen(Canvas& _canvas, Picture* _picture) noexcept
  {
    /*
     * 6502: part 5's first loop, which runs the pages from &40 up to &60.
     *
     * The bitmap, &4000 to &5FFF, which is the canvas's first 0x2000 bytes. A fresh `Canvas` is
     * already zero; this is here because the routine is what the loader does and not what the
     * caller happens to need, and because the composition root is free to call it twice.
     */
    for (std::uint16_t offset = 0; offset < Canvas::BITMAP_SIZE; ++offset)
    {
      _canvas.Write(offset, 0u);
      if (_picture != nullptr)
      {
        WriteBitmapByte2x(*_picture, offset, 0u, false);
      }
    }

    /*
     * 6502: .LOOP3 / .LOOP4 with A = &10 and X counting to &68 -- BOTH blocks of screen RAM,
     * &6000 to &67FF, filled with white ink over a black background.
     *
     * The same byte `COL2` holds and `CHPR` writes through, so the default for a cell nothing has
     * coloured is the colour text prints in. Everything below overwrites part of this.
     */
    for (std::uint16_t cell = Canvas::SCREEN_CELLS; cell < Canvas::SCREEN_SIZE; ++cell)
    {
      _canvas.Write(cell, TEXT_COLOUR_WHITE); // 6502: white on black
    }
    if (_picture != nullptr)
    {
      SetCellRun2x(*_picture, 0, Canvas::CELL_COLUMNS * Canvas::CELL_ROWS, TEXT_COLOUR_WHITE);
    }

    // 6502: ZP = SCBASE+&2400+&2D0, (A ZP2) = sdump, JSR mvsm -- 280 bytes, the dashboard's own
    // seven rows of the SPACE VIEW's block. The text views never show them, which is why only
    // this block gets them.
    for (std::size_t index = 0; index < DASHBOARD_SCREEN_COLOURS.size(); ++index)
    {
      _canvas.Write(static_cast<std::uint16_t>(Canvas::DASHBOARD_CELLS + DASHBOARD_CELL_OFFSET + index), DASHBOARD_SCREEN_COLOURS[index]);
    }

    // 6502: LOOP10 -- the border box down the text view's block, all 25 rows.
    ColourBorderBox(_canvas, Canvas::SCREEN_CELLS, Canvas::CELL_ROWS, _picture);

    // 6502: LOOP11 -- and down the space view's block, which stops at the dashboard.
    ColourBorderBox(_canvas, Canvas::DASHBOARD_CELLS, Canvas::DASHBOARD_CELL_ROW, _picture);

    /*
     * 6502: LOOP16 -- thirty-two cells of the bottom row, counting down.
     *
     * The bottom row of the text view, and the reason a text screen's border box has a bottom at
     * all: `TTX66K` refills rows 0 to 23 with white on every clear and does not touch row 24, so
     * the yellow the loader put there is what the rule at y = 199 is still drawn in.
     */
    const std::uint16_t bottomRow = static_cast<std::uint16_t>(Canvas::SCREEN_CELLS + 24u * Canvas::CELL_COLUMNS + 4u);
    for (int offset = 31; offset >= 0; --offset)
    {
      _canvas.Write(static_cast<std::uint16_t>(bottomRow + offset), SCREEN_YELLOW_ON_BLACK);
    }
    if (_picture != nullptr)
    {
      SetCellRun2x(*_picture, 24 * Canvas::CELL_COLUMNS + 4, 32, SCREEN_YELLOW_ON_BLACK);
    }

    /*
     * 6502: part 6's .LOOP19 -- four pages of colour RAM, &D800 to &DBFF, zeroed.
     *
     * The canvas holds 1,000 cells and the hardware 1,024; the last 24 are past the bottom of the
     * screen and the VIC-II never fetches them, so there is nothing there to model.
     */
    for (int cell = 0; cell < Canvas::CELL_COLUMNS * Canvas::CELL_ROWS; ++cell)
    {
      _canvas.SetCellColour(cell, 0u);
    }

    // 6502: ZP = COLMEM+&2D0, (A ZP2) = cdump, JSR mvsm -- the same 280 cells again, in the other
    // half of what a multicolour cell is coloured from.
    for (std::size_t index = 0; index < DASHBOARD_COLOUR_RAM.size(); ++index)
    {
      _canvas.SetCellColour(static_cast<int>(DASHBOARD_CELL_OFFSET + index), DASHBOARD_COLOUR_RAM[index]);
    }

    /*
     * 6502: LOOP15 -- thirty-four cells from the top row's third, counting down.
     *
     * Cells 3 to 36 of the top row, and cell 2 is NOT one of them: the loop ends on `BNE`, so Y
     * never reaches zero and the cell at `COLMEM+2` keeps the black the zeroing left. The
     * upstream comment says "characters 3 to 36" and the code agrees; a `BPL` here would have
     * been an easy and invisible improvement on it.
     */
    for (int cell = BORDER_LEFT_CELL; cell <= BORDER_RIGHT_CELL; ++cell)
    {
      _canvas.SetCellColour(cell, COLOUR_RAM_YELLOW);
    }

    /*
     * 6502: part 4's store to the background register, which is multicolour %00.
     *
     * The one byte this port takes from part 4, because it is the fourth colour of every
     * dashboard cell and the other stores in that part are the screen bank, the raster and the
     * border of the display itself, which the canvas has no room for and the presenter does not
     * draw.
     */
    _canvas.SetBackground(0u);
  }

  void SetUpLoaderVideo(VideoState& _video) noexcept
  {
    _video.enabled = 0u;     // 6502: all eight sprites off
    _video.expanded = 0xFFu; // 6502: all eight double height and double width

    // 6502: the six Trumbles' colours, all nine.
    _video.colour[2] = Colour::Brown;
    _video.colour[3] = Colour::Grey;
    _video.colour[4] = Colour::Blue;
    _video.colour[5] = Colour::White;
    _video.colour[6] = Colour::Green;
    _video.colour[7] = Colour::Brown;

    // 6502: bit 9 of every x cleared, which the sixteen-bit x carries.
    // 6502: the sights, in the centre of the view.
    _video.x[0] = 161u;
    _video.y[0] = 101u;

    // 6502: the first Trumble at 18 and each one after it DOUBLED -- 18, 36, 72, 144 -- then a
    // fresh 14 and the same doubling again: 14, 28, 56. All seven share one y.
    _video.x[1] = 18u;
    _video.x[2] = 36u;
    _video.x[3] = 72u;
    _video.x[4] = 144u;
    _video.x[5] = 14u;
    _video.x[6] = 28u;
    _video.x[7] = 56u;
    for (std::size_t sprite = 1; sprite < SPRITE_COUNT; ++sprite)
    {
      _video.y[sprite] = 12u;
    }
  }

} // namespace Elite
