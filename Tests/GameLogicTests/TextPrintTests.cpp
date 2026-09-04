#include "pch.h"

#include "OracleImage.h"

#include "Canvas.h"
#include "TextPrint.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Canvas;
using Elite::TextPrinter;
using Elite::TextState;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The character printer against the game that drew it (slice 1d-b).
 *
 * Whole-screen compare, same as the pixel primitives, plus the cursor and the cell colour --
 * because CHPR's most interesting property is the ORDER it does things in. It advances the
 * column before writing the colour, which is the only reason celllook's three-cell offset lands
 * on the same cell as the glyph.
 */
namespace GameLogicTests
{

namespace
{
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

struct Scratch
{
  std::uint16_t xc = 0;
  std::uint16_t yc = 0;
  std::uint16_t qq17 = 0;
  std::uint16_t col2 = 0;
  std::uint16_t screen = 0;

  explicit Scratch(const OracleImage& _oracle)
    : xc(_oracle.Label("XC"))
    , yc(_oracle.Label("YC"))
    , qq17(_oracle.Label("QQ17"))
    , col2(_oracle.Label("COL2"))
  {
    const Cpu6502 cpu = _oracle.Fresh();
    screen = static_cast<std::uint16_t>((cpu.memory[_oracle.Label("ylookupl")] | (cpu.memory[_oracle.Label("ylookuph")] << 8))
                                        - SPACE_VIEW_MARGIN);
  }
};

void CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Canvas& _canvas, const std::wstring& _context)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();
  for (std::uint16_t offset = 0; offset < Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset) + L" -- game has "
                    + std::to_wstring(expected) + L", port has " + std::to_wstring(ours[offset]))
                     .c_str());
    }
  }
}
} // namespace

TEST_CLASS(TextPrinterAgainstTheShippedGame)
{
public:
  /*
   * Every printable character at every column of two rows, in two cell colours.
   *
   * Characters at or above 128 are excluded and counted rather than skipped silently: the
   * original's font pointer arithmetic sends them below the font, into loader memory the port
   * does not carry, and nothing in the game prints one. If that count ever changes, the
   * assumption behind this exclusion has changed with it.
   */
  TEST_METHOD(PrintableCharactersMatchTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("CHPR");

    std::uint32_t compared = 0;

    for (const std::uint8_t colour : { std::uint8_t{ 0x40 }, std::uint8_t{ 0x27 } })
    {
      for (const std::uint32_t row : { 0u, 1u, 11u, 23u })
      {
        for (const std::uint32_t column : { 0u, 1u, 15u, 29u, 30u, 31u, 32u })
        {
          for (std::uint32_t character = 32; character < 128; ++character)
          {
            Cpu6502 cpu = oracle.Fresh();
            cpu.memory[zp.xc] = static_cast<std::uint8_t>(column);
            cpu.memory[zp.yc] = static_cast<std::uint8_t>(row);
            cpu.memory[zp.qq17] = 0;
            cpu.memory[zp.col2] = colour;
            cpu.a = static_cast<std::uint8_t>(character);
            cpu.x = cpu.y = 0;
            cpu.sp = 0xFD;

            const auto run = cpu.CallSubroutine(routine, 50'000);
            Assert::IsTrue(run.completed, L"CHPR should return");

            Canvas canvas;
            TextState state;
            state.column = static_cast<std::uint8_t>(column);
            state.row = static_cast<std::uint8_t>(row);
            state.cellColour = colour;
            TextPrinter printer(canvas, state, nullptr);
            const std::uint8_t returned = printer.Print(static_cast<std::uint8_t>(character));

            const std::wstring context = L"CHPR '" + std::to_wstring(character) + L"' at (" + std::to_wstring(column)
                                         + L"," + std::to_wstring(row) + L") colour " + std::to_wstring(colour);

            CompareScreens(cpu, zp.screen, canvas, context);
            Assert::AreEqual<std::uint32_t>(cpu.memory[zp.xc], state.column, (context + L": XC").c_str());
            Assert::AreEqual<std::uint32_t>(cpu.memory[zp.yc], state.row, (context + L": YC").c_str());
            Assert::AreEqual<std::uint32_t>(cpu.a, returned, (context + L": returned character").c_str());
            ++compared;
          }
        }
      }
    }

    Logger::WriteMessage(("CHPR: " + std::to_string(compared) + " printable characters compared\n").c_str());
    Assert::AreEqual<std::uint32_t>(2u * 4u * 7u * 96u, compared, L"the sweep should not have been narrowed");
  }

  /*
   * The control codes, where the printer moves the cursor instead of drawing. Three behaviours
   * out of two branches: 13 returns the column without moving down, 10 moves down without
   * returning the column, everything else does both.
   */
  TEST_METHOD(ControlCodesMoveTheCursorAsTheShippedRoutineDoes)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("CHPR");

    for (const std::uint32_t row : { 0u, 5u, 23u })
    {
      for (const std::uint32_t column : { 0u, 12u, 31u })
      {
        for (std::uint32_t code = 0; code < 32; ++code)
        {
          if (code == 7)
          {
            continue; // the bell, which is a sound event and a declared seam
          }

          Cpu6502 cpu = oracle.Fresh();
          cpu.memory[zp.xc] = static_cast<std::uint8_t>(column);
          cpu.memory[zp.yc] = static_cast<std::uint8_t>(row);
          cpu.memory[zp.qq17] = 0;
          cpu.a = static_cast<std::uint8_t>(code);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;

          const auto run = cpu.CallSubroutine(routine, 50'000);
          Assert::IsTrue(run.completed, L"CHPR should return");

          Canvas canvas;
          TextState state;
          state.column = static_cast<std::uint8_t>(column);
          state.row = static_cast<std::uint8_t>(row);
          TextPrinter printer(canvas, state, nullptr);
          printer.Print(static_cast<std::uint8_t>(code));

          const std::wstring context = L"code " + std::to_wstring(code) + L" at (" + std::to_wstring(column) + L","
                                       + std::to_wstring(row) + L")";
          CompareScreens(cpu, zp.screen, canvas, context);
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.xc], state.column, (context + L": XC").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[zp.yc], state.row, (context + L": YC").c_str());
        }
      }
    }
  }

  /*
   * 6502: TT66simp -- the clear that CHPR's off-the-bottom path needs, which closes one of the
   * two seams slice 1d-b declared. Drawn over first, so that a clear which only zeroed part of
   * the screen would show up rather than passing on an already-blank canvas.
   */
  TEST_METHOD(ClearingTheTextAreaMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);

    Cpu6502 cpu = oracle.Fresh();
    Canvas canvas;

    // Scribble the same pattern into both, so the clear has something to remove and the margins
    // it must NOT remove are visible.
    for (std::uint16_t offset = 0; offset < Canvas::BITMAP_SIZE; ++offset)
    {
      const std::uint8_t value = static_cast<std::uint8_t>(offset * 7u + 1u);
      cpu.memory[static_cast<std::uint16_t>(zp.screen + offset)] = value;
      canvas.Write(offset, value);
    }

    cpu.memory[zp.xc] = 17;
    cpu.memory[zp.yc] = 9;
    cpu.a = cpu.x = cpu.y = 0;
    cpu.sp = 0xFD;

    const auto run = cpu.CallSubroutine(oracle.Label("TT66simp"), 200'000);
    Assert::IsTrue(run.completed, L"TT66simp should return");

    TextState state;
    state.column = 17;
    state.row = 9;
    Elite::ClearTextArea(canvas, state);

    CompareScreens(cpu, zp.screen, canvas, L"TT66simp");
    Assert::AreEqual<std::uint32_t>(cpu.memory[zp.xc], state.column, L"XC after the clear");
    Assert::AreEqual<std::uint32_t>(cpu.memory[zp.yc], state.row, L"YC after the clear");

    // The four-cell margins are not the screen's business and must survive.
    Assert::AreNotEqual<std::uint32_t>(0, canvas.Screen()[Canvas::ROW_BYTES * 4], L"the left margin should survive");
  }

  /*
   * 6502: TTX66K's BOL3 / BOL4 -- and the reason the game is visible at all.
   *
   * THIS IS A REGRESSION TEST FOR A BLANK SCREEN. The bitmap holds two-bit codes, not colours,
   * and a code of %01 or %10 reads its colour out of the cell's byte in screen RAM. Every screen
   * routine in this suite passed with those bytes left at zero, because they all compare the
   * BITMAP against the oracle and the bitmap was right -- the whole picture just resolved to
   * colour 0 on a colour 0 background. Nothing in a per-routine test looks at the resolved image,
   * so nothing could notice, and the shell drew perfect black frames for as long as this was
   * missing.
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
        Assert::AreEqual<std::uint32_t>(Elite::TEXT_COLOUR_WHITE, canvas.Read(static_cast<std::uint16_t>(base + cell)),
                                        (L"cell " + std::to_wstring(cell) + L" of row " + std::to_wstring(row)).c_str());
      }
    }

    // The four-cell margins are not the game's screen and TTX66K does not fill them.
    Assert::AreEqual<std::uint32_t>(0, canvas.Read(Canvas::CellRowOffset(0)), L"the left margin should stay unfilled");

    // And the point of all of it: a glyph that resolves to a colour somebody can see.
    TextState state;
    state.column = 1;
    state.row = 1;
    state.cellColour = Elite::TEXT_COLOUR_WHITE;
    TextPrinter printer(canvas, state, nullptr);
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

  /// 6502: QQ17 = 255 -- the token printer's "measure, do not print" state. Nothing at all
  /// should reach the screen, and the cursor should not move either.
  TEST_METHOD(SuppressedOutputDrawsNothing)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Scratch zp(oracle);
    const std::uint16_t routine = oracle.Label("CHPR");

    for (std::uint32_t character = 0; character < 128; ++character)
    {
      Cpu6502 cpu = oracle.Fresh();
      cpu.memory[zp.xc] = 4;
      cpu.memory[zp.yc] = 6;
      cpu.memory[zp.qq17] = 0xFF;
      cpu.a = static_cast<std::uint8_t>(character);
      cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;

      const auto run = cpu.CallSubroutine(routine, 50'000);
      Assert::IsTrue(run.completed, L"CHPR should return");

      Canvas canvas;
      TextState state;
      state.column = 4;
      state.row = 6;
      state.caseFlags = 0xFF;
      TextPrinter printer(canvas, state, nullptr);
      printer.Print(static_cast<std::uint8_t>(character));

      CompareScreens(cpu, zp.screen, canvas, L"suppressed " + std::to_wstring(character));
      Assert::AreEqual<std::uint32_t>(4, state.column, L"the column should not move");
      Assert::AreEqual<std::uint32_t>(6, state.row, L"the row should not move");
    }
  }
};

} // namespace GameLogicTests
