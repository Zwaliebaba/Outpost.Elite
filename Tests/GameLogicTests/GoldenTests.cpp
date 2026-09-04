#include "pch.h"

#include "GoldenCanvas.h"
#include "OracleImage.h"

#include "Canvas.h"
#include "TextPrint.h"

#include <array>
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Canvas;
using Elite::DrawWorkspace;
using Elite::TextPrinter;
using Elite::TextState;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Golden canvases (slice 1d-c).
 *
 * Two checks per scene, and they catch different things.
 *
 * The first builds the scene TWICE -- once by calling the shipped routines in the oracle and
 * decoding its screen memory, once by calling the port -- and compares the resolved 320x200
 * images pixel for pixel. That is the exact comparison ADR-003 section 2 was amended to after
 * the emulator run was cancelled, and it is what makes a golden here something other than a
 * picture somebody once agreed with.
 *
 * The second is a committed hash. A self-comparison cannot notice a change that moved both sides
 * together -- a different resolve, a different palette walk, a canvas laid out another way -- and
 * the hash can. There are two of them and they are named for what they prove (Risk R10).
 *
 * BOTH HASHES WERE RE-RECORDED when the resolve was corrected from multicolour to the standard
 * bitmap mode the game screen is really in, and that is exactly the case the second check exists
 * for: the pixel comparisons went on passing throughout, because a decode change moves the port
 * and the oracle together. The new pictures were looked at before the numbers were written down
 * -- a white frame with a diagonal across it and "OUTPOST ELITE" in MAG2 purple, which is what
 * the scene says it draws and what the old numbers had never actually shown.
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

    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    std::uint16_t ScreenBase(const OracleImage& _oracle)
    {
      const Cpu6502 cpu = _oracle.Fresh();
      return static_cast<std::uint16_t>((cpu.memory[_oracle.Label("ylookupl")] | (cpu.memory[_oracle.Label("ylookuph")] << 8)) -
                                        SPACE_VIEW_MARGIN);
    }

    /// 6502: MAG2 -- purple for %01 on a black %10, which is what the text view sets.
    constexpr std::uint8_t TEXT_CELL_COLOUR = 0x40;

    /*
     * Colour RAM, which supplies %11, is a STAND-IN and these goldens say so rather than implying
     * otherwise. The game's value comes from cdump, the loader's colour map, which is a phase-3
     * extraction; until it lands there is no right answer to use here.
     *
     * It costs the goldens nothing, and now rather less than it used to: %11 is a MULTICOLOUR code,
     * and these scenes are drawn in the standard bitmap mode the game screen actually uses, where
     * colour RAM is not read at all. It is still set on both sides so that the day a scene moves to
     * the dashboard the stand-in is already where it belongs.
     */
    constexpr std::uint8_t COLOUR_RAM = 5;

    /// 6502: the &10 the loader fills both blocks of screen RAM with, and TTX66K refreshes -- white
    /// for a set bit, black for a clear one.
    constexpr std::uint8_t SCREEN_PALETTE = 0x10;

    /*
     * Give both sides the palette plane the loader leaves behind.
     *
     * WITHOUT THIS A GOLDEN PROVES ALMOST NOTHING, which is worth stating because these two spent a
     * slice in that state: a set bit takes its colour from the cell's byte in screen RAM, so with
     * that plane left at zero every line in the scene resolves to black on black and the pixel
     * comparison passes over an image that is uniformly empty. The bitmaps agreed; there was just no
     * picture in them. CHPR overwrites the cells it prints into with COL2, so this is what colours
     * everything else -- which in these scenes is the whole frame.
     */
    void FillPalettePlane(std::array<std::uint8_t, 65536>& _memory, std::uint16_t _base, Canvas& _canvas)
    {
      for (int cell = 0; cell < Canvas::CELL_COLUMNS * Canvas::CELL_ROWS; ++cell)
      {
        const std::uint16_t offset = static_cast<std::uint16_t>(Canvas::SCREEN_CELLS + cell);
        _memory[static_cast<std::uint16_t>(_base + offset)] = SCREEN_PALETTE;
        _canvas.Write(offset, SCREEN_PALETTE);
      }
    }
  } // namespace

  TEST_CLASS(GoldenCanvases)
  {
  public:
    /*
     * A frame of lines with text inside it -- the shape every docked screen has, and the smallest
     * scene that exercises the line routine in all four of its directions at once along with the
     * character printer and the cell-colour plane.
     */
    TEST_METHOD(FramedTextScreen)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t base = ScreenBase(oracle);

      const std::uint16_t loin = oracle.Label("LOIN");
      const std::uint16_t chpr = oracle.Label("CHPR");
      const std::uint16_t x1 = oracle.Label("X1");
      const std::uint16_t y1 = oracle.Label("Y1");
      const std::uint16_t x2 = oracle.Label("X2");
      const std::uint16_t y2 = oracle.Label("Y2");

      struct Line
      {
        std::uint8_t x1, y1, x2, y2;
      };
      const Line frame[] = {{8, 8, 247, 8}, {247, 8, 247, 120}, {247, 120, 8, 120}, {8, 120, 8, 8}, {8, 8, 247, 120}};
      const char* text = "OUTPOST ELITE";

      // ---- the shipped routines ----
      Cpu6502 cpu = oracle.Fresh();
      cpu.memory[oracle.Label("QQ17")] = 0;
      cpu.memory[oracle.Label("COL2")] = TEXT_CELL_COLOUR;

      Canvas actual;
      FillPalettePlane(cpu.memory, base, actual);

      for (const Line& line : frame)
      {
        cpu.memory[x1] = line.x1;
        cpu.memory[y1] = line.y1;
        cpu.memory[x2] = line.x2;
        cpu.memory[y2] = line.y2;
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        Assert::IsTrue(cpu.CallSubroutine(loin, 200'000).completed, L"LOIN should return");
      }

      cpu.memory[oracle.Label("XC")] = 4;
      cpu.memory[oracle.Label("YC")] = 5;
      for (const char* character = text; *character != 0; ++character)
      {
        cpu.a = static_cast<std::uint8_t>(*character);
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        Assert::IsTrue(cpu.CallSubroutine(chpr, 50'000).completed, L"CHPR should return");
      }

      Canvas expected;
      Elite::Testing::LoadScreenFromOracle(cpu.memory, base, COLOUR_RAM, expected);

      // ---- the port ----
      for (int cell = 0; cell < Canvas::CELL_COLUMNS * Canvas::CELL_ROWS; ++cell)
      {
        actual.SetCellColour(cell, COLOUR_RAM);
      }

      DrawWorkspace draw;
      for (const Line& line : frame)
      {
        draw.x1 = line.x1;
        draw.y1 = line.y1;
        draw.x2 = line.x2;
        draw.y2 = line.y2;
        Elite::DrawLine(actual, draw);
      }

      TextState state;
      state.column = 4;
      state.row = 5;
      state.cellColour = TEXT_CELL_COLOUR;
      TextPrinter printer(actual, state, nullptr);
      for (const char* character = text; *character != 0; ++character)
      {
        printer.Print(static_cast<std::uint8_t>(*character));
      }

      // ---- the two checks ----
      const std::string difference = Elite::Testing::CompareCanvasImages(expected, actual, "FramedTextScreen");
      if (!difference.empty())
      {
        Assert::Fail(Widen(difference).c_str());
      }

      Logger::WriteMessage(("golden FramedTextScreen hash = " + std::to_string(actual.Hash()) + "\n").c_str());
      Assert::AreEqual<std::uint64_t>(6270663083276661605ull, actual.Hash(),
                                      L"the picture changed. Look at it before re-recording: run the test, open the PNG "
                                      L"the failure names, and diff it with tools/golden_diff.py (Risk R10)");
    }

    /*
     * A cleared screen with nothing on it. Dull on purpose: it pins the border the clear leaves
     * behind, which is the part of TT66simp a person would not think to check and the part a
     * "tidier" clear would remove.
     */
    TEST_METHOD(ClearedScreenKeepsItsMargins)
    {
      if (OracleMissing())
      {
        return;
      }
      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t base = ScreenBase(oracle);

      Cpu6502 cpu = oracle.Fresh();
      Canvas actual;
      FillPalettePlane(cpu.memory, base, actual);
      for (std::uint16_t offset = 0; offset < Canvas::BITMAP_SIZE; ++offset)
      {
        const std::uint8_t value = static_cast<std::uint8_t>(offset * 11u + 3u);
        cpu.memory[static_cast<std::uint16_t>(base + offset)] = value;
        actual.Write(offset, value);
      }
      for (int cell = 0; cell < Canvas::CELL_COLUMNS * Canvas::CELL_ROWS; ++cell)
      {
        actual.SetCellColour(cell, COLOUR_RAM);
      }

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      Assert::IsTrue(cpu.CallSubroutine(oracle.Label("TT66simp"), 200'000).completed, L"TT66simp should return");

      Canvas expected;
      Elite::Testing::LoadScreenFromOracle(cpu.memory, base, COLOUR_RAM, expected);

      TextState state;
      Elite::ClearTextArea(actual, state);

      const std::string difference = Elite::Testing::CompareCanvasImages(expected, actual, "ClearedScreen");
      if (!difference.empty())
      {
        Assert::Fail(Widen(difference).c_str());
      }

      Logger::WriteMessage(("golden ClearedScreen hash = " + std::to_string(actual.Hash()) + "\n").c_str());
      Assert::AreEqual<std::uint64_t>(8933034668988025208ull, actual.Hash(), L"the picture changed -- see Risk R10");
    }
  };

} // namespace GameLogicTests
