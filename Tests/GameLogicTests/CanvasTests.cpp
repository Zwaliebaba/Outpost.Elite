#include "pch.h"

#include "OracleImage.h"

#include "Canvas.h"
#include "LookupTables.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Canvas;
using Elite::DrawWorkspace;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

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

bool OracleMissing()
{
  const OracleImage& oracle = OracleImage::Instance();
  if (oracle.Available())
  {
    return false;
  }
  Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
  return true;
}

std::wstring Widen(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// The zero-page bytes the drawing routines take their arguments in.
struct Scratch
{
  std::uint16_t x1 = 0;
  std::uint16_t y1 = 0;
  std::uint16_t x2 = 0;
  std::uint16_t col = 0;
  std::uint16_t zz = 0;
  std::uint16_t screen = 0;

  explicit Scratch(const OracleImage& _oracle)
    : x1(_oracle.Label("X1"))
    , y1(_oracle.Label("Y1"))
    , x2(_oracle.Label("X2"))
    , col(_oracle.Label("COL"))
    , zz(_oracle.Label("ZZ"))
  {
    const Cpu6502 cpu = _oracle.Fresh();
    const std::uint16_t low = _oracle.Label("ylookupl");
    const std::uint16_t high = _oracle.Label("ylookuph");
    screen = static_cast<std::uint16_t>((cpu.memory[low] | (cpu.memory[high] << 8)) - SPACE_VIEW_MARGIN);
  }
};

/// Compares the port's whole screen against the oracle's, and says where they first differ.
void CompareScreens(const Cpu6502& _cpu, std::uint16_t _screenBase, const Canvas& _canvas, const std::wstring& _context)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();

  for (std::uint16_t offset = 0; offset < Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_screenBase + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset) + L" -- game has "
                    + std::to_wstring(expected) + L", port has " + std::to_wstring(ours[offset]))
                     .c_str());
    }
  }
}

std::wstring Context(const wchar_t* _what, std::uint32_t _a, std::uint32_t _b, std::uint32_t _c = 0xFFFFFFFFu)
{
  std::wstring text = std::wstring(_what) + L" (" + std::to_wstring(_a) + L", " + std::to_wstring(_b);
  if (_c != 0xFFFFFFFFu)
  {
    text += L", " + std::to_wstring(_c);
  }
  return text + L")";
}
} // namespace

TEST_CLASS(CanvasAgainstTheShippedGame)
{
public:
  /*
   * The port computes bitmap addresses instead of reading ylookup, so the two had better agree.
   * All 256 entries, including the ones past the bottom of the bitmap -- the table carries them
   * and a port that clamped would put a stray write somewhere different from where the game
   * puts it.
   */
  TEST_METHOD(ComputedRowOffsetsMatchTheGamesTable)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);

    for (std::uint32_t y = 0; y < 256; ++y)
    {
      const std::uint16_t table = static_cast<std::uint16_t>(Elite::ROW_ADDRESS_LOW[y] | (Elite::ROW_ADDRESS_HIGH[y] << 8));
      const std::uint16_t computed = static_cast<std::uint16_t>(zp.screen + Canvas::RowOffset(static_cast<std::uint8_t>(y)));
      Assert::AreEqual<std::uint32_t>(table, computed, Context(L"row offset", y, 0).c_str());
    }
  }

  /// The same for the colour cells, whose three-cell offset is the one that looks like a bug.
  TEST_METHOD(ComputedCellOffsetsMatchTheGamesTable)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);

    for (int row = 0; row < Canvas::CELL_ROWS; ++row)
    {
      const std::uint16_t table =
        static_cast<std::uint16_t>(Elite::CELL_ADDRESS_LOW[row] | (Elite::CELL_ADDRESS_HIGH[row] << 8));
      const std::uint16_t computed = static_cast<std::uint16_t>(zp.screen + Canvas::CellRowOffset(row));
      Assert::AreEqual<std::uint32_t>(table, computed, Context(L"cell offset", static_cast<std::uint32_t>(row), 0).c_str());
    }
  }

  /*
   * 6502: PIXEL. Every x, a spread of y that covers each row within a character cell and the
   * top and bottom of the space view, and the distances either side of both size thresholds.
   */
  TEST_METHOD(PixelMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("PIXEL");

    for (const std::uint32_t zz : { 0u, 1u, 79u, 80u, 81u, 143u, 144u, 255u })
    {
      for (const std::uint32_t y : { 0u, 1u, 7u, 8u, 60u, 71u, 72u, 128u, 143u, 191u, 199u })
      {
        for (std::uint32_t x = 0; x < 256; ++x)
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.memory[zp.zz] = static_cast<std::uint8_t>(zz);
          cpu.a = static_cast<std::uint8_t>(y);
          cpu.x = static_cast<std::uint8_t>(x);
          cpu.y = 0;
          cpu.sp = 0xFD;

          const auto run = cpu.CallSubroutine(routine, 5'000);
          Assert::IsTrue(run.completed, L"PIXEL should return");

          Canvas canvas;
          DrawWorkspace work;
          work.zz = static_cast<std::uint8_t>(zz);
          Elite::PlotPixel(canvas, work, static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y));

          CompareScreens(cpu, zp.screen, canvas, Context(L"PIXEL", x, y, zz));
        }
      }
    }
  }

  /*
   * 6502: PIXEL2, which converts a sign-magnitude offset from the centre of the space view and
   * falls into PIXEL. Exhaustive in x, and every y including the ones it refuses to draw.
   */
  TEST_METHOD(RelativePixelMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("PIXEL2");

    for (std::uint32_t y1 = 0; y1 < 256; ++y1)
    {
      for (std::uint32_t x1 = 0; x1 < 256; x1 += 1)
      {
        Cpu6502 cpu = oracle.Fresh();
        cpu.memory[zp.x1] = static_cast<std::uint8_t>(x1);
        cpu.memory[zp.y1] = static_cast<std::uint8_t>(y1);
        cpu.memory[zp.zz] = 255;
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;

        const auto run = cpu.CallSubroutine(routine, 5'000);
        Assert::IsTrue(run.completed, L"PIXEL2 should return");

        Canvas canvas;
        DrawWorkspace work;
        work.x1 = static_cast<std::uint8_t>(x1);
        work.y1 = static_cast<std::uint8_t>(y1);
        work.zz = 255;
        Elite::PlotRelativePixel(canvas, work);

        CompareScreens(cpu, zp.screen, canvas, Context(L"PIXEL2", x1, y1));
      }
    }
  }

  /*
   * 6502: CPIX2 and CPIX4. Every x, the four colour constants the game actually uses, and the
   * rows either side of a character-cell boundary -- CPIX4 steps up a row and CPIX2's second
   * pixel steps into the next cell, so both boundaries matter.
   */
  TEST_METHOD(ColouredDashesMatchTheShippedRoutines)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);

    // 6502: RED, YELLOW, GREEN, WHITE -- four multicolour pixels each, not colour numbers.
    const std::array<std::uint8_t, 5> colours = { 0x55, 0xAA, 0xFF, 0x5A, 0x00 };

    for (const char* name : { "CPIX2", "CPIX4" })
    {
      const std::uint16_t routine = oracle.Label(name);
      const bool isBlock = std::string(name) == "CPIX4";

      for (const std::uint8_t colour : colours)
      {
        for (const std::uint32_t y : { 0u, 1u, 7u, 8u, 63u, 143u })
        {
          for (std::uint32_t x = 0; x < 256; ++x)
          {
            Cpu6502 cpu = oracle.Fresh();
            cpu.memory[zp.x1] = static_cast<std::uint8_t>(x);
            cpu.memory[zp.y1] = static_cast<std::uint8_t>(y);
            cpu.memory[zp.col] = colour;
            cpu.a = cpu.x = cpu.y = 0;
            cpu.sp = 0xFD;

            const auto run = cpu.CallSubroutine(routine, 5'000);
            Assert::IsTrue(run.completed, (Widen(name) + L" should return").c_str());

            Canvas canvas;
            DrawWorkspace work;
            work.x1 = static_cast<std::uint8_t>(x);
            work.y1 = static_cast<std::uint8_t>(y);
            work.col = colour;

            if (isBlock)
            {
              Elite::PlotBlock(canvas, work);
            }
            else
            {
              Elite::PlotDash(canvas, work);
            }

            CompareScreens(cpu, zp.screen, canvas, Context(Widen(name).c_str(), x, y, colour));

            // Y1 is left where the routine left it, and CPIX4's callers rely on that.
            Assert::AreEqual<std::uint32_t>(cpu.memory[zp.y1], work.y1, Context(L"Y1 afterwards", x, y, colour).c_str());
          }
        }
      }
    }
  }

  /*
   * 6502: HLOIN. Both ends swept across two character cells and beyond, so the single-byte case,
   * the two-byte case and runs with whole bytes between are all covered, along with the swap
   * when the ends arrive the wrong way round.
   */
  TEST_METHOD(HorizontalLineMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("HLOIN");

    for (const std::uint32_t y : { 0u, 3u, 7u, 8u, 100u, 143u })
    {
      for (std::uint32_t x1 = 0; x1 < 40; ++x1)
      {
        for (std::uint32_t x2 = 0; x2 < 64; ++x2)
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.memory[zp.x1] = static_cast<std::uint8_t>(x1);
          cpu.memory[zp.x2] = static_cast<std::uint8_t>(x2);
          cpu.memory[zp.y1] = static_cast<std::uint8_t>(y);
          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;

          const auto run = cpu.CallSubroutine(routine, 50'000);
          Assert::IsTrue(run.completed, L"HLOIN should return");

          Canvas canvas;
          DrawWorkspace work;
          work.x1 = static_cast<std::uint8_t>(x1);
          work.x2 = static_cast<std::uint8_t>(x2);
          work.y1 = static_cast<std::uint8_t>(y);
          Elite::DrawHorizontalLine(canvas, work);

          CompareScreens(cpu, zp.screen, canvas, Context(L"HLOIN", x1, x2, y));

          // The routine swaps its ends and decrements the right one, and leaves both that way.
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.x1], work.x1, Context(L"X1 afterwards", x1, x2, y).c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.x2], work.x2, Context(L"X2 afterwards", x1, x2, y).c_str());
        }
      }
    }
  }

  /*
   * 6502: LOIN. The one that matters, and the one the plan calls the densest code in the phase.
   *
   * The sweep is chosen to hit every branch the routine has rather than to be large: both
   * gradients either side of the shallow/steep split, both directions on each axis, the swapped
   * and unswapped entries, lines that start and end inside one character cell and lines that
   * cross several, and the degenerate cases where one span is zero.
   *
   * Whole-screen compare, so a line that is right for most of its length and steps a row late at
   * one cell boundary fails -- which is the failure this routine is most likely to have, because
   * the carry out of the pointer arithmetic feeds the next iteration's accumulator.
   */
  TEST_METHOD(LineMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("LOIN");
    const std::uint16_t y2Address = oracle.Label("Y2");

    std::uint32_t cases = 0;

    for (const std::uint32_t x1 : { 0u, 1u, 7u, 8u, 63u, 100u, 128u, 200u, 255u })
    {
      for (const std::uint32_t y1 : { 0u, 1u, 7u, 8u, 71u, 100u, 143u })
      {
        for (const std::uint32_t x2 : { 0u, 3u, 8u, 9u, 64u, 129u, 199u, 255u })
        {
          for (const std::uint32_t y2 : { 0u, 2u, 8u, 15u, 72u, 101u, 143u })
          {
            Cpu6502 cpu = oracle.Fresh();
            cpu.memory[zp.x1] = static_cast<std::uint8_t>(x1);
            cpu.memory[zp.y1] = static_cast<std::uint8_t>(y1);
            cpu.memory[zp.x2] = static_cast<std::uint8_t>(x2);
            cpu.memory[y2Address] = static_cast<std::uint8_t>(y2);
            cpu.a = cpu.x = cpu.y = 0;
            cpu.sp = 0xFD;

            const auto run = cpu.CallSubroutine(routine, 200'000);
            Assert::IsTrue(run.completed, L"LOIN should return");

            Canvas canvas;
            DrawWorkspace work;
            work.x1 = static_cast<std::uint8_t>(x1);
            work.y1 = static_cast<std::uint8_t>(y1);
            work.x2 = static_cast<std::uint8_t>(x2);
            work.y2 = static_cast<std::uint8_t>(y2);
            Elite::DrawLine(canvas, work);

            CompareScreens(cpu, zp.screen, canvas,
                           L"LOIN (" + std::to_wstring(x1) + L"," + std::to_wstring(y1) + L") to ("
                             + std::to_wstring(x2) + L"," + std::to_wstring(y2) + L")");
            ++cases;
          }
        }
      }
    }

    Logger::WriteMessage(("LOIN: " + std::to_string(cases) + " lines compared byte for byte\n").c_str());
  }

  /*
   * 6502: LOIN again, and this one exists because the grid above missed a defect for two months.
   *
   * The grid picks eight or nine values per axis to reach every BRANCH. That is the right way to
   * choose a small sweep and it is not sufficient here, because this routine's state is a carry
   * chain: whether a pixel lands one row early depends on the accumulator's phase, which depends
   * on the start position's low byte and the slope together. A grid samples that space at
   * sixty-odd points out of four billion, and the point it missed was a downward line whose
   * screen pointer had reached 248 before the setup's `SBC #247` -- one line in nine of those,
   * one pixel each (§6.47).
   *
   * So: four thousand lines from a fixed generator, over the whole coordinate space, compared
   * byte for byte. Not a substitute for the grid -- it is far worse at reaching rare branches --
   * but it covers the phase space the grid cannot, and between them they are what this routine
   * needs.
   */
  TEST_METHOD(LineMatchesTheShippedRoutineOverAWideSample)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("LOIN");
    const std::uint16_t y2Address = oracle.Label("Y2");

    std::uint32_t bits = 0x2C6A91B7u;
    const auto next = [&bits]() {
      bits = bits * 1664525u + 1013904223u;
      return static_cast<std::uint8_t>(bits >> 24);
    };

    std::uint32_t cases = 0;
    std::uint32_t drawn = 0;

    for (std::uint32_t which = 0; which < 4000u; ++which)
    {
      const std::uint8_t x1 = next();
      const std::uint8_t y1 = static_cast<std::uint8_t>(next() % 144u);
      const std::uint8_t x2 = next();
      const std::uint8_t y2 = static_cast<std::uint8_t>(next() % 144u);

      Cpu6502 cpu = oracle.Fresh();
      cpu.memory[zp.x1] = x1;
      cpu.memory[zp.y1] = y1;
      cpu.memory[zp.x2] = x2;
      cpu.memory[y2Address] = y2;
      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;

      const auto run = cpu.CallSubroutine(routine, 200'000);
      Assert::IsTrue(run.completed, L"LOIN should return");

      Canvas canvas;
      DrawWorkspace work;
      work.x1 = x1;
      work.y1 = y1;
      work.x2 = x2;
      work.y2 = y2;
      Elite::DrawLine(canvas, work);

      CompareScreens(cpu, zp.screen, canvas,
                     L"LOIN (" + std::to_wstring(x1) + L"," + std::to_wstring(y1) + L") to ("
                       + std::to_wstring(x2) + L"," + std::to_wstring(y2) + L")");

      for (const std::uint8_t byte : canvas.Screen())
      {
        drawn += (byte != 0u) ? 1u : 0u;
      }
      ++cases;
    }

    Assert::AreEqual<std::uint32_t>(4000u, cases, L"every sampled line");
    Assert::IsTrue(drawn > 0u, L"the lines were actually drawn");
    Logger::WriteMessage(("LOIN wide: " + std::to_string(cases) + " lines, "
                          + std::to_string(drawn) + " marked bytes\n")
                           .c_str());
  }

  /// Drawing anything twice puts the screen back exactly as it was. LL9 and SUN decide what to
  /// erase on the strength of this, so it is worth a test of its own rather than an assumption.
  TEST_METHOD(EveryPrimitiveErasesItself)
  {
    Canvas canvas;
    DrawWorkspace work;

    work.zz = 0;
    Elite::PlotPixel(canvas, work, 137, 61);
    work.x1 = 90;
    work.y1 = 44;
    work.col = 0xAA;
    Elite::PlotDash(canvas, work);
    work.x1 = 10;
    work.x2 = 55;
    work.y1 = 33;
    Elite::DrawHorizontalLine(canvas, work);

    bool anythingDrawn = false;
    for (const std::uint8_t byte : canvas.Screen())
    {
      anythingDrawn = anythingDrawn || byte != 0;
    }
    Assert::IsTrue(anythingDrawn, L"the setup should have drawn something to erase");

    work = DrawWorkspace{};
    work.zz = 0;
    Elite::PlotPixel(canvas, work, 137, 61);
    work.x1 = 90;
    work.y1 = 44;
    work.col = 0xAA;
    Elite::PlotDash(canvas, work);
    work.x1 = 10;
    work.x2 = 55;
    work.y1 = 33;
    Elite::DrawHorizontalLine(canvas, work);

    for (std::size_t offset = 0; offset < Canvas::SCREEN_SIZE; ++offset)
    {
      Assert::AreEqual<std::uint32_t>(0, canvas.Screen()[offset],
                                      (L"offset " + std::to_wstring(offset) + L" was not erased").c_str());
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

    DrawWorkspace work;
    work.zz = 255;
    Elite::PlotPixel(canvas, work, 66, Y);
    Elite::PlotPixel(canvas, work, 68, Y);

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
      Assert::AreEqual<std::uint32_t>(expected, row[pixel],
                                      (L"pixel " + std::to_wstring(pixel) + L" of %10110001").c_str());
    }
  }
};

} // namespace GameLogicTests
