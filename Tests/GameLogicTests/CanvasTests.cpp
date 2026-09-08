#include "pch.h"


#include "Canvas.h"
#include "LookupTables.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Canvas;

/*
 * The pixel primitives against the game that drew them (slice 1d-a).
 *
 * The comparison here is stronger than the arithmetic slices got. Those compared a return value;
 * this compares the WHOLE SCREEN -- all 0x2800 bytes of bitmap and both cell-colour planes --
 * after every call. So a routine that draws the right pixel and also scribbles somewhere else
 * fails, which is the failure the drawing code is most likely to have.
 *
 * That is only possible because the canvas holds the same bytes in the same order the original's
 * memory does (ADR-002 section 4). If it held resolved colours this would be a translation with
 * its own bugs, and it could not represent what the game writes at all (ADR-002 section 7).
 */
namespace GameLogicTests
{

  namespace
  {
    /// 6502: SCBASE. Not a label -- it is an assembler constant -- so it is derived from ylookup's
    /// first entry, which is SCBASE plus the space view's four-cell left margin.
    constexpr std::uint16_t SPACE_VIEW_MARGIN = 0x20;

    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    std::wstring Context(const wchar_t* _what, std::uint32_t _first, std::uint32_t _second, std::uint32_t _third = 0xFFFFFFFFu)
    {
      std::wstring text = std::wstring(_what) + L" (" + std::to_wstring(_first) + L", " + std::to_wstring(_second);
      if (_third != 0xFFFFFFFFu)
      {
        text += L", " + std::to_wstring(_third);
      }
      return text + L")";
    }
  } // namespace

  TEST_CLASS(CanvasAgainstTheShippedGame)
  {
  public:
    /// Drawing anything twice puts the screen back exactly as it was. LL9 and SUN decide what to
    /// erase on the strength of this, so it is worth a test of its own rather than an assumption.
    TEST_METHOD(EveryPrimitiveErasesItself)
    {
      Canvas canvas;

      Elite::PlotPixel(canvas, 137, 61, 0);
      (void)Elite::PlotDash(canvas, 90, 44, Elite::PixelPattern::Yellow);
      Elite::DrawHorizontalLine(canvas, 10, 55, 33);

      bool anythingDrawn = false;
      for (const std::uint8_t byte : canvas.Screen())
      {
        anythingDrawn = anythingDrawn || byte != 0;
      }
      Assert::IsTrue(anythingDrawn, L"the setup should have drawn something to erase");

      Elite::PlotPixel(canvas, 137, 61, 0);
      (void)Elite::PlotDash(canvas, 90, 44, Elite::PixelPattern::Yellow);
      Elite::DrawHorizontalLine(canvas, 10, 55, 33);

      for (std::size_t offset = 0; offset < Canvas::SCREEN_SIZE; ++offset)
      {
        Assert::AreEqual<std::uint32_t>(0, canvas.Screen()[offset], (L"offset " + std::to_wstring(offset) + L" was not erased").c_str());
      }
    }

    /*
     * Resolve turns the bits into colours, and the case worth pinning is the one ADR-002 section 7
     * is about: two marks that share no bit still light three pixels in three colours, and the
     * middle one is a colour neither of them drew.
     *
     * IT IS PINNED ON THE DASHBOARD, because that is the only part of the screen the VIC-II shows
     * in multicolour: `wantdials` sets bit 4 of `caravanserai` for the rows below the raster split
     * and nothing else ever does. The same two marks in the space view are two separate one-bit
     * pixels, which is the test below.
     */
    TEST_METHOD(ResolveGivesEachPixelPairItsCellColourOnTheDashboard)
    {
      constexpr int Y = Canvas::DASHBOARD_CELL_ROW * 8 + 4;

      Canvas canvas;
      canvas.SetBackground(0);
      canvas.SetDashboardShown(true);

      Elite::PlotPixel(canvas, 66, Y, 255);
      Elite::PlotPixel(canvas, 68, Y, 255);

      // 6502: a screen RAM byte -- high nibble is the colour for %01, low nibble for %10.
      const int cell = (Y / 8) * Canvas::CELL_COLUMNS + (Canvas::SPACE_VIEW_MARGIN + 64) / 8;
      canvas.Screen()[Canvas::DASHBOARD_CELLS + cell] = 0x27; // red for %01, yellow for %10
      canvas.SetCellColour(cell, 5);                          // green for %11

      std::array<std::uint8_t, Canvas::WIDTH * Canvas::HEIGHT> image{};
      canvas.Resolve(image);

      const std::uint8_t* row = image.data() + Y * Canvas::WIDTH + ((Canvas::SPACE_VIEW_MARGIN + 64) / 8) * 8;

      Assert::AreEqual<std::uint32_t>(2, row[0], L"the first pixel is %01, so red");
      Assert::AreEqual<std::uint32_t>(2, row[1], L"and it is two columns wide");
      Assert::AreEqual<std::uint32_t>(5, row[2], L"the pixel between the two marks is %11, so green");
      Assert::AreEqual<std::uint32_t>(5, row[3], L"and it too is two columns wide");
      Assert::AreEqual<std::uint32_t>(7, row[4], L"the third is %10, so yellow");
      Assert::AreEqual<std::uint32_t>(0, row[6], L"the fourth was never drawn, so background");
    }

    /*
     * The game screen is STANDARD bitmap mode, and this is the regression test for the day it was
     * not: `moonflower` and `caravanserai` both ship with bit 4 clear, so one bit is one pixel and
     * a set bit takes the cell's high nibble while a clear one takes its low nibble.
     *
     * Decoding it as multicolour instead reads every PAIR of bits as one two-bit code, which halves
     * the horizontal resolution and turns the 8x8 font into stripes. It is worth asserting on an
     * asymmetric byte -- %10110001 reads the same forwards as a pair of codes and differently as
     * eight bits, and a symmetric one would have let the wrong decode pass.
     */
    TEST_METHOD(ResolveDrawsTheGameScreenInStandardBitmapMode)
    {
      constexpr int Y = 60;
      constexpr int CELL_COLUMN = (Canvas::SPACE_VIEW_MARGIN + 64) / 8;
      constexpr std::uint8_t BITS = 0xB1u; // %10110001

      Canvas canvas;
      canvas.Write(static_cast<std::uint16_t>((Y / 8) * Canvas::ROW_BYTES + CELL_COLUMN * 8 + (Y % 8)), BITS);

      const int cell = (Y / 8) * Canvas::CELL_COLUMNS + CELL_COLUMN;
      canvas.Screen()[Canvas::SCREEN_CELLS + cell] = 0x27; // red for a 1, yellow for a 0

      // Colour RAM and the background are not read in this mode, so a lurid value in either must
      // not appear anywhere in the row.
      canvas.SetCellColour(cell, 5);
      canvas.SetBackground(13);

      std::array<std::uint8_t, Canvas::WIDTH * Canvas::HEIGHT> image{};
      canvas.Resolve(image);

      const std::uint8_t* row = image.data() + Y * Canvas::WIDTH + CELL_COLUMN * 8;
      for (int pixel = 0; pixel < 8; ++pixel)
      {
        const std::uint32_t expected = (((BITS >> (7 - pixel)) & 1u) != 0u) ? 2u : 7u;
        Assert::AreEqual<std::uint32_t>(expected, row[pixel], (L"pixel " + std::to_wstring(pixel) + L" of %10110001").c_str());
      }
    }
  };

} // namespace GameLogicTests
