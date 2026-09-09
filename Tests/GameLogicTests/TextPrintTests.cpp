#include "pch.h"


#include "Canvas.h"
#include "TextPrint.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Canvas;
using Elite::TextPrinter;
using Elite::TextState;

/*
 * The cell colours a printed glyph leaves behind (slice 1d-b).
 *
 * The whole-screen comparisons against the game that drew them went with the oracle (M6-b-5),
 * `CHPR`'s ordering among them -- it advances the column before writing the colour, which is the
 * only reason `celllook`'s three-cell offset lands on the same cell as the glyph. What is left is
 * the assertion that stands without it: resetting the cell colours makes the picture visible,
 * which is a property of the port's own two surfaces.
 */
namespace GameLogicTests
{

  namespace
  {
  } // namespace

  TEST_CLASS(TheTextPrintersCellColours)
  {
  public:
    /*
     * 6502: TTX66K's BOL3 / BOL4 -- and the reason the game is visible at all.
     *
     * THIS IS A REGRESSION TEST FOR A BLANK SCREEN. The bitmap holds two-bit codes, not colours,
     * and a code of %01 or %10 reads its colour out of the cell's byte in screen RAM. Every screen
     * routine in this suite passed with those bytes left at zero, because they all compared the
     * BITMAP against the original and the bitmap was right -- the whole picture just resolved to
     * colour 0 on a colour 0 background. Nothing in a per-routine comparison looked at the resolved
     * image, so nothing could notice, and the shell drew perfect black frames for as long as this
     * was missing.
     *
     * So this asserts the two halves separately: the cells `ResetCellColours` fills, and the fact
     * that a character printed after it RESOLVES to something other than black.
     */
    TEST_METHOD(ResetCellColoursMakesThePictureVisible)
    {
      Canvas canvas;
      Elite::ResetCellColours(canvas);

      // 24 rows of 32 cells, from `celllook` + 1 -- the same cells CHPR's colour write lands in.
      for (int row = 0; row < 24; ++row)
      {
        const std::uint16_t base = static_cast<std::uint16_t>(Canvas::CellRowOffset(row) + 1);
        for (int cell = 0; cell < 32; ++cell)
        {
          Assert::AreEqual<std::uint32_t>(Elite::TEXT_COLOUR_WHITE.Byte(), canvas.Read(static_cast<std::uint16_t>(base + cell)),
                                          (L"cell " + std::to_wstring(cell) + L" of row " + std::to_wstring(row)).c_str());
        }
      }

      // The four-cell margins are not the game's screen and TTX66K does not fill them.
      Assert::AreEqual<std::uint32_t>(0, canvas.Read(Canvas::CellRowOffset(0)), L"the left margin should stay unfilled");

      // And the point of all of it: a glyph that resolves to a colour somebody can see.
      TextState state;
      state.column = 1;
      state.row = 1;
      state.palette = Elite::TEXT_COLOUR_WHITE;
      TextPrinter printer(canvas, state);
      printer.Print('A');

      std::array<std::uint8_t, static_cast<std::size_t>(Canvas::WIDTH) * Canvas::HEIGHT> resolved{};
      canvas.Resolve(resolved);

      std::size_t lit = 0;
      for (const std::uint8_t index : resolved)
      {
        lit += (index != 0) ? 1u : 0u;
      }
      Assert::AreNotEqual<std::size_t>(0, lit, L"a printed character should resolve to a non-black pixel");
    }

  };

} // namespace GameLogicTests
