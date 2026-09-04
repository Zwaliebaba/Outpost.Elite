#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Canvas.h"
#include "LookupTables.h"
#include "ShipSlot.h"
#include "ViewChange.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Setting up a screen (slice 3d-d-iii-a).
 *
 * Every routine here writes the bitmap, so every comparison is a whole-canvas compare from a
 * screen full of a marker byte. That is what separates "wrote nothing" from "wrote a zero", and
 * three of these routines exist to write zeros.
 */
namespace GameLogicTests
{

namespace
{
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
  const Cpu6502 image = _oracle.Fresh();
  return static_cast<std::uint16_t>(
    (image.memory[_oracle.Label("ylookupl")] | (image.memory[_oracle.Label("ylookuph")] << 8))
    - 0x20);
}

void FillScreens(Cpu6502& _cpu, Elite::Canvas& _canvas, std::uint16_t _base, std::uint8_t _marker)
{
  std::memset(&_cpu.memory[_base], _marker, Elite::Canvas::SCREEN_SIZE);
  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    _canvas.Write(offset, _marker);
  }
}

std::uint32_t CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas,
                             std::uint8_t _marker, const std::wstring& _context)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();
  std::uint32_t touched = 0;

  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset)
                    + L" -- game has " + std::to_wstring(expected) + L", port has "
                    + std::to_wstring(ours[offset])).c_str());
    }
    touched += (ours[offset] != _marker) ? 1u : 0u;
  }

  return touched;
}
} // namespace


TEST_CLASS(TheScreenPrimitives)
{
public:
  /*
   * 6502: ZES1k and ZES2k -- and the byte at offset zero is the whole point.
   *
   * `ZES2k` stores at Y and then counts DOWN, stopping when Y reaches zero, so byte 0 of the page
   * is never written. Entered through `ZES1k` with Y = 0 the first `DEY` wraps to 255 and the
   * whole page goes. Both entries are swept, and the marker is non-zero so that "left alone" and
   * "written as zero" are different answers.
   */
  TEST_METHOD(TheScreenClearMatchesZES1kAndZES2k)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t zes1k = oracle.Label("ZES1k");
    const std::uint16_t zes2k = oracle.Label("ZES2k");
    const std::uint16_t sc = oracle.Label("SC");
    const std::uint16_t screen = ScreenBase(oracle);
    const std::uint8_t screenPage = static_cast<std::uint8_t>(screen >> 8);

    for (const std::uint8_t page : { std::uint8_t{ 0 }, std::uint8_t{ 3 }, std::uint8_t{ 0x1F } })
    {
      // ---- the whole page, through ZES1k ----------------------------------------------------
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        FillScreens(cpu, canvas, screen, 0x7Eu);

        cpu.x = static_cast<std::uint8_t>(screenPage + page);
        Assert::IsTrue(cpu.CallSubroutine(zes1k, 5'000).completed, L"ZES1k returned");

        Elite::ZeroWholePage(canvas, static_cast<std::uint16_t>(page * 256u));

        const std::wstring where = Widen("ZES1k(page " + std::to_string(page) + ")");
        Assert::AreEqual<std::uint32_t>(256u, CompareScreens(cpu, screen, canvas, 0x7Eu, where),
                                        (where + L": the whole page went").c_str());
      }

      // ---- a partial page, through ZES2k ------------------------------------------------------
      for (const std::uint8_t first : { std::uint8_t{ 1 }, std::uint8_t{ 0x3F }, std::uint8_t{ 0xFF } })
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        FillScreens(cpu, canvas, screen, 0x7Eu);

        cpu.memory[sc] = 0u;
        cpu.x = static_cast<std::uint8_t>(screenPage + page);
        cpu.y = first;
        Assert::IsTrue(cpu.CallSubroutine(zes2k, 5'000).completed, L"ZES2k returned");

        Elite::ZeroPageDown(canvas, static_cast<std::uint16_t>(page * 256u), first);

        const std::wstring where =
          Widen("ZES2k(page " + std::to_string(page) + ", from " + std::to_string(first) + ")");
        const std::uint32_t cleared = CompareScreens(cpu, screen, canvas, 0x7Eu, where);
        Assert::AreEqual<std::uint32_t>(first, cleared,
                                        (where + L": bytes 1 to `first`, and byte 0 untouched").c_str());
      }
    }
  }

  /*
   * 6502: mvblockK and mvbllop -- the dashboard, copied from `DSTORE%` into the bitmap.
   *
   * The real source and the real destination, because both now exist: `DSTORE%` carries the
   * dashboard image and `DASHBOARD_IMAGE` is the same 2,240 bytes (§6.78). `wantdials` makes
   * these two calls back to back -- eight whole pages and then `mvbllop` for the remaining &C0 --
   * and the second continues where the first stopped, which is what `V` and `SC` being left
   * advanced buys the original and what the second call's arguments buy the port.
   */
  TEST_METHOD(TheBlockCopyMatchesMvblockK)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t mvblockK = oracle.Label("mvblockK");
    const std::uint16_t mvbllop = oracle.Label("mvbllop");
    const std::uint16_t sc = oracle.Label("SC");
    const std::uint16_t v = oracle.Label("V");
    const std::uint16_t screen = ScreenBase(oracle);

    const std::uint16_t store = 0xEF90u;                     // 6502: DSTORE%
    const std::uint16_t dashboard = 18u * 8u * 40u;          // 6502: DLOC%, as a canvas offset
    const std::uint16_t destination = static_cast<std::uint16_t>(screen + dashboard);

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    FillScreens(cpu, canvas, screen, 0x3Bu);

    // 6502: LDX #8 / V = DSTORE% / SC = DLOC% / JSR mvblockK.
    cpu.memory[v] = static_cast<std::uint8_t>(store & 0xFFu);
    cpu.memory[static_cast<std::uint16_t>(v + 1)] = static_cast<std::uint8_t>(store >> 8);
    cpu.memory[sc] = static_cast<std::uint8_t>(destination & 0xFFu);
    cpu.memory[static_cast<std::uint16_t>(sc + 1)] = static_cast<std::uint8_t>(destination >> 8);
    cpu.x = 8u;
    Assert::IsTrue(cpu.CallSubroutine(mvblockK, 60'000).completed, L"mvblockK returned");

    // 6502: LDY #&C0 / LDX #1 / JSR mvbllop -- and V and SC are where the last call left them.
    cpu.y = 0xC0u;
    cpu.x = 1u;
    Assert::IsTrue(cpu.CallSubroutine(mvbllop, 60'000).completed, L"mvbllop returned");

    Elite::CopyPagesDown(canvas, Elite::DASHBOARD_IMAGE.data(), dashboard, 8u, 0u);
    Elite::CopyPagesDown(canvas, Elite::DASHBOARD_IMAGE.data() + 8u * 256u,
                         static_cast<std::uint16_t>(dashboard + 8u * 256u), 1u, 0xC0u);

    CompareScreens(cpu, screen, canvas, 0x3Bu, L"mvblockK");

    /*
     * The hole and the overrun, asserted rather than described.
     *
     * 2,240 bytes are copied and they are not the first 2,240: `mvbllop` stores at Y and counts
     * DOWN to 1, so offset 2,048 is never written and offset 2,240 is. Both bytes of the image
     * are zero, so on screen this is invisible -- and the marker makes it visible here, which is
     * the whole reason a comparison starts from one.
     */
    Assert::AreEqual<std::uint32_t>(0x3Bu, canvas.Read(static_cast<std::uint16_t>(dashboard + 2048u)),
                                    L"offset 2048 is the hole and keeps the marker");
    Assert::AreEqual(Elite::DASHBOARD_IMAGE[2240],
                     canvas.Read(static_cast<std::uint16_t>(dashboard + 2240u)),
                     L"offset 2240 is copied, one past where seven rows end");
    Assert::AreEqual(Elite::DASHBOARD_IMAGE[2047],
                     canvas.Read(static_cast<std::uint16_t>(dashboard + 2047u)), L"the eighth page");
    Assert::AreEqual<std::uint32_t>(0x3Bu, canvas.Read(static_cast<std::uint16_t>(dashboard + 2241u)),
                                    L"and nothing past it");
  }

  /// 6502: BOXS -- the rule across the whole screen, at the two rows the game asks for and a
  /// spread of others.
  TEST_METHOD(TheScreenRuleMatchesBOXS)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t boxs = oracle.Label("BOXS");
    const std::uint16_t screen = ScreenBase(oracle);

    for (const std::uint8_t row : { std::uint8_t{ 0 }, std::uint8_t{ 1 }, std::uint8_t{ 7 },
                                    std::uint8_t{ 8 }, std::uint8_t{ 100 }, std::uint8_t{ 143 },
                                    std::uint8_t{ 199 } })
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      FillScreens(cpu, canvas, screen, 0x11u);

      cpu.x = row;
      Assert::IsTrue(cpu.CallSubroutine(boxs, 20'000).completed, L"BOXS returned");

      Elite::DrawWorkspace draw;
      Elite::DrawScreenRule(canvas, draw, row);

      const std::wstring where = Widen("BOXS(row " + std::to_string(row) + ")");
      Assert::IsTrue(CompareScreens(cpu, screen, canvas, 0x11u, where) > 0u,
                     (where + L": something was drawn").c_str());
    }
  }

  /*
   * 6502: BOXS2 -- the vertical edges, and it EORs.
   *
   * Swept from a screen full of a marker AND run twice on the same canvas, because an EOR that
   * put the screen back is the property the routine has and a STORE does not.
   */
  TEST_METHOD(TheVerticalEdgeMatchesBOXS2)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t boxs2 = oracle.Label("BOXS2");
    const std::uint16_t sc = oracle.Label("SC");
    const std::uint16_t screen = ScreenBase(oracle);

    struct Case
    {
      std::uint16_t cell;
      std::uint8_t pattern, rows;
    };

    const std::vector<Case> CASES = {
      { 3u * 8u, 0x03u, 18u },   // 6502: BOX2's left edge
      { 36u * 8u, 0xC0u, 18u },  // 6502: BOX2's right edge
      { 0u, 0xFFu, 1u },
      { 10u * 8u, 0x5Au, 7u },
    };

    for (const Case& item : CASES)
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      FillScreens(cpu, canvas, screen, 0x66u);

      const std::uint16_t address = static_cast<std::uint16_t>(screen + item.cell);
      cpu.memory[sc] = static_cast<std::uint8_t>(address & 0xFFu);
      cpu.a = item.pattern;
      cpu.y = static_cast<std::uint8_t>(address >> 8);
      cpu.x = item.rows;
      Assert::IsTrue(cpu.CallSubroutine(boxs2, 20'000).completed, L"BOXS2 returned");

      Elite::ToggleVerticalEdge(canvas, item.cell, item.pattern, item.rows);

      const std::wstring where =
        Widen("BOXS2(cell " + std::to_string(item.cell) + ", pattern "
              + std::to_string(item.pattern) + ", rows " + std::to_string(item.rows) + ")");
      CompareScreens(cpu, screen, canvas, 0x66u, where);

      // Twice puts it back, which is what an EOR is for.
      Elite::ToggleVerticalEdge(canvas, item.cell, item.pattern, item.rows);
      for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
      {
        Assert::AreEqual<std::uint32_t>(0x66u, canvas.Read(offset),
                                        (where + L": twice restores the screen").c_str());
      }
    }
  }

  /// 6502: BLUEBAND and BLUEBANDS -- the two coloured bands, which STORE rather than EOR.
  TEST_METHOD(TheColourBandsMatchBLUEBAND)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t blueband = oracle.Label("BLUEBAND");
    const std::uint16_t screen = ScreenBase(oracle);

    Cpu6502 cpu = oracle.Fresh();
    Elite::Canvas canvas;
    FillScreens(cpu, canvas, screen, 0x2Au);

    Assert::IsTrue(cpu.CallSubroutine(blueband, 40'000).completed, L"BLUEBAND returned");
    Elite::DrawColourBands(canvas);

    const std::uint32_t touched = CompareScreens(cpu, screen, canvas, 0x2Au, L"BLUEBAND");
    Assert::AreEqual<std::uint32_t>(2u * 18u * 24u, touched, L"two bands of eighteen by twenty-four");
  }


  /*
   * 6502: BOX2 -- the border, at both of its heights.
   *
   * THE HEIGHT IS A DATA BYTE. `BOX2` opens `LDX #18`, and `TTX66K` falls into it through
   * `LDX #25 / EQUB &2C` -- `BIT abs` swallowing that `LDX`, so the fall-through keeps 25 while a
   * `JSR BOX2` gets 18. This calls the routine both ways: at its label, and two bytes in, which
   * is exactly where the swallowed instruction ends and what the fall-through executes (§6.79).
   *
   * A port that took the height from a constant would agree with the game on one screen and draw
   * seven rows too few or too many on the other.
   */
  TEST_METHOD(TheBorderMatchesBOX2AtBothHeights)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t box2 = oracle.Label("BOX2");
    const std::uint16_t t2 = oracle.Label("T2");
    const std::uint16_t screen = ScreenBase(oracle);

    struct Case
    {
      const char* what;
      std::uint16_t entry;
      std::uint8_t rows;
      bool preset;
    };

    const std::vector<Case> CASES = {
      { "called at its label, which loads 18", box2, Elite::BORDER_ROWS_SPACE_VIEW, false },
      { "fallen into past the LDX, keeping 25", static_cast<std::uint16_t>(box2 + 2),
        Elite::BORDER_ROWS_TEXT_SCREEN, true },
      { "fallen into with something else entirely", static_cast<std::uint16_t>(box2 + 2), 7, true },
    };

    for (const Case& item : CASES)
    {
      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      FillScreens(cpu, canvas, screen, 0x4Du);

      cpu.memory[t2] = 0x99u;
      cpu.x = item.preset ? item.rows : std::uint8_t{ 0xA5u };
      Assert::IsTrue(cpu.CallSubroutine(item.entry, 60'000).completed, L"BOX2 returned");

      Elite::DrawWorkspace draw;
      draw.t2 = 0x99u;
      Elite::DrawBorder(canvas, draw, item.rows);

      const std::wstring where = Widen(std::string("BOX2 (") + item.what + ")");
      Assert::IsTrue(CompareScreens(cpu, screen, canvas, 0x4Du, where) > 0u,
                     (where + L": something was drawn").c_str());
      Assert::AreEqual(cpu.memory[t2], draw.t2, (where + L": T2").c_str());
    }
  }
  /*
   * 6502: zonkscanners -- clear "on the scanner" on every ship in the bubble.
   *
   * The sweep covers an empty bubble, a full one, a bubble whose first slot is empty (which stops
   * the walk before it starts), and one with the planet and the sun in it -- both negative types,
   * both skipped, and both with the bit set so that skipping them is visible.
   */
  TEST_METHOD(ForgettingTheBlipsMatchesZonkscanners)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t zonk = oracle.Label("zonkscanners");
    const std::uint16_t frin = oracle.Label("FRIN");
    const std::uint16_t kPercent = oracle.Label("K%");

    const std::vector<std::vector<std::uint8_t>> BUBBLES = {
      {},
      { 1, 2, 3 },
      { 129, 130, 5 },
      { 0, 7, 8 },
      { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 },
    };

    for (std::size_t index = 0; index < BUBBLES.size(); ++index)
    {
      const std::vector<std::uint8_t>& types = BUBBLES[index];

      Cpu6502 cpu = oracle.Fresh();
      Elite::Bubble bubble;

      for (std::size_t slot = 0; slot < bubble.slots.size(); ++slot)
      {
        const std::uint8_t type = (slot < types.size()) ? types[slot] : std::uint8_t{ 0 };
        bubble.slots[slot] = type;
        cpu.memory[static_cast<std::uint16_t>(frin + slot)] = type;
      }

      for (std::size_t slot = 0; slot < bubble.blocks.size(); ++slot)
      {
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          // Every byte marked, and byte 31 with bit 4 set, so clearing it is visible and
          // clearing anything else is a failure.
          const std::uint8_t value =
            (byte == 31u) ? 0xFFu : static_cast<std::uint8_t>(0x40u + byte + slot);
          bubble.blocks[slot][byte] = value;
          cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
        }
      }

      Assert::IsTrue(cpu.CallSubroutine(zonk, 40'000).completed, L"zonkscanners returned");
      Elite::ForgetScannerBlips(bubble);

      const std::wstring where = Widen("zonkscanners(case " + std::to_string(index) + ")");
      for (std::size_t slot = 0; slot < bubble.blocks.size(); ++slot)
      {
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          const std::uint16_t at =
            static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte);
          Assert::AreEqual(cpu.memory[at], bubble.blocks[slot][byte],
                           (where + L": K% slot " + std::to_wstring(slot) + L" byte "
                            + std::to_wstring(byte)).c_str());
        }
      }
    }
  }
};

} // namespace GameLogicTests
