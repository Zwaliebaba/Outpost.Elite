#include "pch.h"

#include "FlightUniverse.h"

#include "Arith.h"
#include "Canvas.h"
#include "Controls.h"
#include "Dashboard.h"
#include "LookupTables.h"
#include "ShipDraw.h"
#include "Charts.h"
#include "Commander.h"
#include "ExtendedTokens.h"
#include "PlanetDraw.h"
#include "Rng.h"
#include "ShipMove.h"
#include "Stardust.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "ShipSlot.h"
#include "LoaderScreen.h"
#include "ViewChange.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The screen the loader leaves (slice 3d-d-iii-a).
 *
 * The whole-canvas comparisons -- every screen set-up run on both machines from a screen full of
 * a marker byte, so that "wrote nothing" and "wrote a zero" stayed different answers -- went with
 * the oracle (M6-b-5). What is left is the colour: the loader's screen and the title screen both
 * come out in the cell colours they are supposed to, which is a property of the port's own
 * surfaces rather than of the original's memory.
 */
namespace GameLogicTests
{

  /*
   * The screen memory the C64's loader leaves behind, and what the game looks like without it.
   *
   * There is no oracle for this one and there cannot be: `elite-loader.asm` assembles to &4000,
   * which is the game's own screen bitmap, so the loader and the game are never in memory at the
   * same time and the image these tests would run against is the one the loader is busy
   * overwriting. What IS compared against the shipped build is the data -- `TableTests` holds
   * `sdump` and `cdump` against the assembled loader, byte for byte -- so what is left here is
   * where those bytes go and what the machine then makes of them.
   *
   * THE SECOND TEST IS THE ONE THAT WOULD HAVE CAUGHT THE BUG. Every routine this suite compares
   * was already right when the title screen came out black: the dashboard picture was copied, the
   * border box was drawn, the dials were drawn, and all of it was black ink on black paper. A test
   * that only compares the bitmap cannot see that, which is the same lesson as the multicolour
   * decode in ADR-002 section 4 -- assert on what `Resolve` produces, not only on what was written.
   */
  TEST_CLASS(TheScreenTheLoaderLeaves)
  {
  public:
    /*
     * 6502: the loader's parts 5 and 6, in the two blocks of screen RAM and in colour RAM.
     *
     * The marker matters: the canvas is filled with a byte that is neither of the ones the loader
     * writes, so "left alone" is a different answer from "written as black" everywhere.
     */
    TEST_METHOD(TheLoaderColoursEveryCellItShould)
    {
      Elite::Canvas canvas;
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        canvas.Write(offset, 0x1Du);
      }
      for (int cell = 0; cell < Elite::Canvas::CELL_COLUMNS * Elite::Canvas::CELL_ROWS; ++cell)
      {
        canvas.SetCellColour(cell, 0x1Du);
      }
      canvas.SetBackground(0x0Du);

      Elite::SetUpLoaderScreen(canvas);

      // The bitmap, which part 5 zeroes before it colours anything.
      for (std::uint16_t offset = 0; offset < Elite::Canvas::BITMAP_SIZE; ++offset)
      {
        Assert::AreEqual<std::uint32_t>(0u, canvas.Read(offset), L"the bitmap is cleared");
      }

      for (int row = 0; row < Elite::Canvas::CELL_ROWS; ++row)
      {
        for (int column = 0; column < Elite::Canvas::CELL_COLUMNS; ++column)
        {
          const int cell = row * Elite::Canvas::CELL_COLUMNS + column;
          const std::wstring where = WidenText("row " + std::to_string(row) + " column " + std::to_string(column));

          /*
           * The text view's block, &6000: the border box down cells 3 and 36 for all 25 rows, black
           * on black outside it, and the bottom row yellow so a text screen's box has a floor.
           * Everything else is the white `TTX66K` rewrites on every clear.
           */
          Elite::CellPalette expected = Elite::TEXT_COLOUR_WHITE;
          if (column < 3 || column > 36)
          {
            expected = Elite::SCREEN_BLACK_ON_BLACK;
          }
          else if (column == 3 || column == 36 || row == 24)
          {
            expected = Elite::SCREEN_YELLOW_ON_BLACK;
          }
          Assert::AreEqual<std::uint32_t>(expected.Byte(), canvas.Read(static_cast<std::uint16_t>(Elite::Canvas::SCREEN_CELLS + cell)),
                                          (L"screen RAM, " + where).c_str());

          /*
           * The space view's block, &6400: the same border box, but only as far as the dashboard --
           * whose seven rows are `sdump` instead, which is not a border, is not white, and reaches
           * all forty columns.
           */
          if (row >= Elite::Canvas::DASHBOARD_CELL_ROW)
          {
            const std::size_t index = static_cast<std::size_t>(cell) - Elite::Canvas::DASHBOARD_CELL_ROW * Elite::Canvas::CELL_COLUMNS;
            expected = Elite::CellPalette::Of(Elite::DASHBOARD_SCREEN_COLOURS[index]); // sdump's bytes are pairs too
          }
          Assert::AreEqual<std::uint32_t>(expected.Byte(), canvas.Read(static_cast<std::uint16_t>(Elite::Canvas::DASHBOARD_CELLS + cell)),
                                          (L"dashboard screen RAM, " + where).c_str());

          /*
           * Colour RAM: black everywhere except the dashboard's own 280 cells and cells 3 to 36 of
           * the top row. Cell 2 of that row is NOT yellow -- `LOOP15` ends on `BNE`, so it never
           * stores at Y = 0 -- and asserting that is what stops the port tidying the loop up.
           */
          std::uint8_t colour = 0u;
          if (row >= Elite::Canvas::DASHBOARD_CELL_ROW)
          {
            const std::size_t index = static_cast<std::size_t>(cell) - Elite::Canvas::DASHBOARD_CELL_ROW * Elite::Canvas::CELL_COLUMNS;
            colour = Elite::DASHBOARD_COLOUR_RAM[index];
          }
          else if (row == 0 && column >= 3 && column <= 36)
          {
            colour = Elite::COLOUR_RAM_YELLOW;
          }
          Assert::AreEqual<std::uint32_t>(colour, canvas.CellColour(cell), (L"colour RAM, " + where).c_str());
        }
      }

      Assert::AreEqual<std::uint32_t>(0u, Elite::ColourIndex(canvas.Background()), L"the background register is black");
    }

    /*
     * The title screen, resolved -- the test that fails if the colours are missing.
     *
     * `TT66` with view 13 is what `TITLE` clears to, and it tail-jumps to `wantdials`: the border
     * box, the dashboard picture, seven dials and the compass. Every one of those was already
     * correct while the screen was black, so this asserts the PICTURE instead -- the border box is
     * yellow, and the dashboard is a spread of colours rather than one.
     */
    TEST_METHOD(TheTitleScreenComesOutInColour)
    {
      Universe universe;
      Elite::SetUpLoaderScreen(universe.canvas);

      Elite::Ports ports = universe.Ports();
      Elite::SetUpScreen(universe, ports, 13u);

      // 6502: comirq1 reading `abraxas` -- the raster split, which the shell does once a frame.
      Assert::AreEqual<std::uint32_t>(Elite::COLOUR_BANK_DASHBOARD, universe.screen.colourBank, L"view 13 asks for the dashboard");
      universe.canvas.SetDashboardShown(true);

      std::array<std::uint8_t, static_cast<std::size_t>(Elite::Canvas::WIDTH) * Elite::Canvas::HEIGHT> resolved{};
      universe.canvas.Resolve(resolved);

      /*
       * The border box's left edge. `BOXS2` sets the two low bits of cell 3, so the pixels are x =
       * 30 and 31, and cell 3's palette is yellow over black -- so a set bit there is colour 7.
       * Before the loader ran, that cell was black over black and both bits resolved to zero.
       */
      for (int y = 8; y < 100; ++y)
      {
        Assert::AreEqual<std::uint32_t>(7u, resolved[static_cast<std::size_t>(y) * Elite::Canvas::WIDTH + 31u],
                                        L"the border box is yellow");
      }

      /*
       * And the dashboard, which is the half a black screen hid completely: count what is lit below
       * the split. The picture alone is thousands of pixels, so the threshold is not a measurement
       * of it -- it is far enough above zero to fail loudly and far enough below the real figure to
       * survive a dial moving.
       */
      std::size_t lit = 0;
      std::array<bool, 16> seen{};
      for (int y = Elite::Canvas::SPACE_VIEW_HEIGHT; y < Elite::Canvas::HEIGHT; ++y)
      {
        for (int x = 0; x < Elite::Canvas::WIDTH; ++x)
        {
          const std::uint8_t colour = resolved[static_cast<std::size_t>(y) * Elite::Canvas::WIDTH + x];
          seen[colour & 0x0Fu] = true;
          lit += (colour != 0u) ? 1u : 0u;
        }
      }

      Assert::IsTrue(lit > 2000u, L"the dashboard is on screen");

      std::size_t colours = 0;
      for (const bool used : seen)
      {
        colours += used ? 1u : 0u;
      }
      Assert::IsTrue(colours >= 4u, L"and it is drawn in more than one colour");

      /*
       * INK YOU CANNOT SEE IS A PLACEMENT ERROR, and it is the only thing that could catch §6.105.
       *
       * The dashboard picture is placed by an address nothing assembles, and for a month it was
       * placed three character cells out. No comparison against the shipped game could see that --
       * `wantdials` copies from wherever the image is, so both sides read the same misplaced bytes
       * -- and three cells of shift still renders as a dashboard. What it does NOT do is line up
       * with `sdump` and `cdump`, which give every cell its own palette: ink lands where the
       * palette says black, and disappears.
       *
       * So this counts the lit pixels (%01, %10, %11) below the split that resolve to colour zero.
       * At the shipped alignment it is 18.5% of them and at the right one 8.4%, which is the
       * minimum over every shift from -8 to +8 cells; the threshold sits between the two rather
       * than at either, because the dials drawn on top move the exact figure a little.
       */
      std::size_t ink = 0;
      std::size_t hidden = 0;
      const std::span<const std::uint8_t> planes = universe.canvas.Screen();

      for (int cellRow = Elite::Canvas::DASHBOARD_CELL_ROW; cellRow < Elite::Canvas::CELL_ROWS; ++cellRow)
      {
        for (int column = 0; column < Elite::Canvas::CELL_COLUMNS; ++column)
        {
          const int cell = cellRow * Elite::Canvas::CELL_COLUMNS + column;
          const std::uint8_t cellByte = planes[Elite::Canvas::DASHBOARD_CELLS + cell];
          const std::uint8_t palette[4] = {0u, static_cast<std::uint8_t>(cellByte >> 4), static_cast<std::uint8_t>(cellByte & 0x0Fu),
                                           static_cast<std::uint8_t>(universe.canvas.CellColour(cell) & 0x0Fu)};

          for (int sub = 0; sub < 8; ++sub)
          {
            const std::uint8_t bits = planes[cellRow * Elite::Canvas::ROW_BYTES + column * 8 + sub];
            for (int pixel = 0; pixel < 4; ++pixel)
            {
              const std::uint8_t code = static_cast<std::uint8_t>((bits >> (6 - 2 * pixel)) & 0x03u);
              if (code != 0u)
              {
                ++ink;
                hidden += (palette[code] == 0u) ? 1u : 0u;
              }
            }
          }
        }
      }

      Assert::IsTrue(ink > 1000u, L"there is ink on the dashboard to judge");
      Logger::WriteMessage(("dashboard ink: " + std::to_string(hidden) + " of " + std::to_string(ink) + " invisible").c_str());
      Assert::IsTrue(hidden * 100u < ink * 12u, L"the picture lines up with the colour map");
    }
  };

} // namespace GameLogicTests
