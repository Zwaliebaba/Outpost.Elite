#include "pch.h"

#include "Cpu6502.h"
#include "FlightUniverse.h"
#include "OracleImage.h"

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
      const std::uint16_t screenPointer = oracle.Label("SC");
      const std::uint16_t screen = ScreenBase(oracle);
      const std::uint8_t screenPage = static_cast<std::uint8_t>(screen >> 8);

      for (const std::uint8_t page : {std::uint8_t{0}, std::uint8_t{3}, std::uint8_t{0x1F}})
      {
        // ---- the whole page, through ZES1k ----------------------------------------------------
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::Canvas canvas;
          FillScreens(cpu, canvas, screen, 0x7Eu);

          cpu.x = static_cast<std::uint8_t>(screenPage + page);
          Assert::IsTrue(cpu.CallSubroutine(zes1k, 5'000).completed, L"ZES1k returned");

          Elite::ZeroWholePage(canvas, static_cast<std::uint16_t>(page * 256u));

          const std::wstring where = WidenText("ZES1k(page " + std::to_string(page) + ")");
          Assert::AreEqual<std::uint32_t>(256u, CompareScreens(cpu, screen, canvas, 0x7Eu, where),
                                          (where + L": the whole page went").c_str());
        }

        // ---- a partial page, through ZES2k ------------------------------------------------------
        for (const std::uint8_t first : {std::uint8_t{1}, std::uint8_t{0x3F}, std::uint8_t{0xFF}})
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::Canvas canvas;
          FillScreens(cpu, canvas, screen, 0x7Eu);

          cpu.memory[screenPointer] = 0u;
          cpu.x = static_cast<std::uint8_t>(screenPage + page);
          cpu.y = first;
          Assert::IsTrue(cpu.CallSubroutine(zes2k, 5'000).completed, L"ZES2k returned");

          Elite::ZeroPageDown(canvas, static_cast<std::uint16_t>(page * 256u), first);

          const std::wstring where = WidenText("ZES2k(page " + std::to_string(page) + ", from " + std::to_string(first) + ")");
          const std::uint32_t cleared = CompareScreens(cpu, screen, canvas, 0x7Eu, where);
          Assert::AreEqual<std::uint32_t>(first, cleared, (where + L": bytes 1 to `first`, and byte 0 untouched").c_str());
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
      const std::uint16_t screenPointer = oracle.Label("SC");
      const std::uint16_t v = oracle.Label("V");
      const std::uint16_t screen = ScreenBase(oracle);

      const std::uint16_t store = 0xEF90u;            // 6502: DSTORE%
      const std::uint16_t dashboard = 18u * 8u * 40u; // 6502: DLOC%, as a canvas offset
      const std::uint16_t destination = static_cast<std::uint16_t>(screen + dashboard);

      Cpu6502 cpu = oracle.Fresh();
      Elite::Canvas canvas;
      FillScreens(cpu, canvas, screen, 0x3Bu);

      // 6502: LDX #8 / V = DSTORE% / SC = DLOC% / JSR mvblockK.
      cpu.memory[v] = static_cast<std::uint8_t>(store & 0xFFu);
      cpu.memory[static_cast<std::uint16_t>(v + 1)] = static_cast<std::uint8_t>(store >> 8);
      cpu.memory[screenPointer] = static_cast<std::uint8_t>(destination & 0xFFu);
      cpu.memory[static_cast<std::uint16_t>(screenPointer + 1)] = static_cast<std::uint8_t>(destination >> 8);
      cpu.x = 8u;
      Assert::IsTrue(cpu.CallSubroutine(mvblockK, 60'000).completed, L"mvblockK returned");

      // 6502: LDY #&C0 / LDX #1 / JSR mvbllop -- and V and SC are where the last call left them.
      cpu.y = 0xC0u;
      cpu.x = 1u;
      Assert::IsTrue(cpu.CallSubroutine(mvbllop, 60'000).completed, L"mvbllop returned");

      Elite::CopyPagesDown(canvas, Elite::DASHBOARD_IMAGE.data(), dashboard, 8u, 0u);
      Elite::CopyPagesDown(canvas, Elite::DASHBOARD_IMAGE.data() + 8u * 256u, static_cast<std::uint16_t>(dashboard + 8u * 256u), 1u, 0xC0u);

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
      Assert::AreEqual(Elite::DASHBOARD_IMAGE[2240], canvas.Read(static_cast<std::uint16_t>(dashboard + 2240u)),
                       L"offset 2240 is copied, one past where seven rows end");
      Assert::AreEqual(Elite::DASHBOARD_IMAGE[2047], canvas.Read(static_cast<std::uint16_t>(dashboard + 2047u)), L"the eighth page");
      Assert::AreEqual<std::uint32_t>(0x3Bu, canvas.Read(static_cast<std::uint16_t>(dashboard + 2241u)), L"and nothing past it");
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

      for (const std::uint8_t row :
           {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{7}, std::uint8_t{8}, std::uint8_t{100}, std::uint8_t{143}, std::uint8_t{199}})
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        FillScreens(cpu, canvas, screen, 0x11u);

        cpu.x = row;
        Assert::IsTrue(cpu.CallSubroutine(boxs, 20'000).completed, L"BOXS returned");

        Elite::DrawScreenRule(canvas, row);

        const std::wstring where = WidenText("BOXS(row " + std::to_string(row) + ")");
        Assert::IsTrue(CompareScreens(cpu, screen, canvas, 0x11u, where) > 0u, (where + L": something was drawn").c_str());
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
      const std::uint16_t screenPointer = oracle.Label("SC");
      const std::uint16_t screen = ScreenBase(oracle);

      struct Case
      {
        std::uint16_t cell;
        std::uint8_t pattern, rows;
      };

      const std::vector<Case> CASES = {
        {3u * 8u, 0x03u, 18u},  // 6502: BOX2's left edge
        {36u * 8u, 0xC0u, 18u}, // 6502: BOX2's right edge
        {0u, 0xFFu, 1u},
        {10u * 8u, 0x5Au, 7u},
      };

      for (const Case& item : CASES)
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        FillScreens(cpu, canvas, screen, 0x66u);

        const std::uint16_t address = static_cast<std::uint16_t>(screen + item.cell);
        cpu.memory[screenPointer] = static_cast<std::uint8_t>(address & 0xFFu);
        cpu.a = item.pattern;
        cpu.y = static_cast<std::uint8_t>(address >> 8);
        cpu.x = item.rows;
        Assert::IsTrue(cpu.CallSubroutine(boxs2, 20'000).completed, L"BOXS2 returned");

        Elite::ToggleVerticalEdge(canvas, item.cell, item.pattern, item.rows);

        const std::wstring where = WidenText("BOXS2(cell " + std::to_string(item.cell) + ", pattern " + std::to_string(item.pattern) +
                                             ", rows " + std::to_string(item.rows) + ")");
        CompareScreens(cpu, screen, canvas, 0x66u, where);

        // Twice puts it back, which is what an EOR is for.
        Elite::ToggleVerticalEdge(canvas, item.cell, item.pattern, item.rows);
        for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
        {
          Assert::AreEqual<std::uint32_t>(0x66u, canvas.Read(offset), (where + L": twice restores the screen").c_str());
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
        {"called at its label, which loads 18", box2, Elite::BORDER_ROWS_SPACE_VIEW, false},
        {"fallen into past the LDX, keeping 25", static_cast<std::uint16_t>(box2 + 2), Elite::BORDER_ROWS_TEXT_SCREEN, true},
        {"fallen into with something else entirely", static_cast<std::uint16_t>(box2 + 2), 7, true},
      };

      for (const Case& item : CASES)
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::Canvas canvas;
        FillScreens(cpu, canvas, screen, 0x4Du);

        cpu.memory[t2] = 0x99u;
        cpu.x = item.preset ? item.rows : std::uint8_t{0xA5u};
        Assert::IsTrue(cpu.CallSubroutine(item.entry, 60'000).completed, L"BOX2 returned");

        Elite::DrawBorder(canvas, item.rows);

        const std::wstring where = WidenText(std::string("BOX2 (") + item.what + ")");
        Assert::IsTrue(CompareScreens(cpu, screen, canvas, 0x4Du, where) > 0u, (where + L": something was drawn").c_str());

        // `T` carries the row count from one edge to the other and `HLOIN` overwrites it on the way
        // out; it is the kernel's byte and a local since M2-b, so there is nothing left to compare
        // (§8, M2-c -- the port used to write `T2` here, which the game does not).
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
        {}, {1, 2, 3}, {129, 130, 5}, {0, 7, 8}, {3, 3, 3, 3, 3, 3, 3, 3, 3, 3},
      };

      for (std::size_t index = 0; index < BUBBLES.size(); ++index)
      {
        const std::vector<std::uint8_t>& types = BUBBLES[index];

        Cpu6502 cpu = oracle.Fresh();
        Elite::Bubble bubble;

        for (std::size_t slot = 0; slot < bubble.slots.size(); ++slot)
        {
          const std::uint8_t type = (slot < types.size()) ? types[slot] : std::uint8_t{0};
          bubble.slots[slot] = type;
          cpu.memory[static_cast<std::uint16_t>(frin + slot)] = type;
        }

        for (std::size_t slot = 0; slot < bubble.blocks.size(); ++slot)
        {
          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = bubble.blocks[slot].ToBytes();
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            // Every byte marked, and byte 31 with bit 4 set, so clearing it is visible and
            // clearing anything else is a failure.
            const std::uint8_t value = (byte == 31u) ? 0xFFu : static_cast<std::uint8_t>(0x40u + byte + slot);
            shipBytes[byte] = value;
            cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
          }
          bubble.blocks[slot] = Elite::Ship::FromBytes(shipBytes);
        }

        Assert::IsTrue(cpu.CallSubroutine(zonk, 40'000).completed, L"zonkscanners returned");
        Elite::ForgetScannerBlips(bubble);

        const std::wstring where = WidenText("zonkscanners(case " + std::to_string(index) + ")");
        for (std::size_t slot = 0; slot < bubble.blocks.size(); ++slot)
        {
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            const std::uint16_t at = static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte);
            Assert::AreEqual(cpu.memory[at], bubble.blocks[slot].ToBytes()[byte],
                             (where + L": K% slot " + std::to_wstring(slot) + L" byte " + std::to_wstring(byte)).c_str());
          }
        }
      }
    }
  };

  TEST_CLASS(TheDashboardScreen)
  {
  public:
    /*
     * 6502: wantdials -- the dashboard arriving, and the dashboard already being there.
     *
     * The whole routine including `DIALS`, so this is the widest comparison in the slice: the
     * border, the 2,240-byte copy, every blip forgotten, seven dials, the compass, two colour
     * bands and the sprites switched off, against the game doing all of it.
     *
     * BOTH VALUES OF `DFLAG`. The flag skips the expensive half and not the cheap one -- with the
     * dashboard already up, the border, the two mode bytes, the bands and the sprites still happen
     * and only the copy, the blips and `DIALS` are skipped. A port that read the flag as "return
     * early" would agree on the second frame and differ on everything drawn on top of it.
     */
    TEST_METHOD(TheDashboardScreenMatchesWantdials)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t wantdials = oracle.Label("wantdials");
      const std::uint16_t screen = ScreenBase(oracle);

      struct At
      {
        std::uint16_t abraxas, caravanserai, dflag, speed, rollMagnitude, rollSign, pitchRate, pitchMagnitude;
        std::uint16_t energy, fsh, ash, qq14, cabtmp, gntmp, altit, mcnt, flh, qq11;
        std::uint16_t comx, comy, comc, many, kPercent, frin, t2;
      } at{};

      at.abraxas = oracle.Label("abraxas");
      at.caravanserai = oracle.Label("caravanserai");
      at.dflag = oracle.Label("DFLAG");
      at.speed = oracle.Label("DELTA");
      at.rollMagnitude = oracle.Label("ALP1");
      at.rollSign = oracle.Label("ALP2");
      at.pitchRate = oracle.Label("BETA");
      at.pitchMagnitude = oracle.Label("BET1");
      at.energy = oracle.Label("ENERGY");
      at.fsh = oracle.Label("FSH");
      at.ash = oracle.Label("ASH");
      at.qq14 = oracle.Label("QQ14");
      at.cabtmp = oracle.Label("CABTMP");
      at.gntmp = oracle.Label("GNTMP");
      at.altit = oracle.Label("ALTIT");
      at.mcnt = oracle.Label("MCNT");
      at.flh = oracle.Label("FLH");
      at.qq11 = oracle.Label("QQ11");
      at.comx = oracle.Label("COMX");
      at.comy = oracle.Label("COMY");
      at.comc = oracle.Label("COMC");
      at.many = oracle.Label("MANY");
      at.kPercent = oracle.Label("K%");
      at.frin = oracle.Label("FRIN");
      at.t2 = oracle.Label("T2");

      std::uint32_t compared = 0;
      std::uint32_t copies = 0;

      for (const std::uint8_t already : {std::uint8_t{0}, std::uint8_t{0xFF}, std::uint8_t{1}})
      {
        for (const std::uint8_t counter : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{9}})
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::Canvas canvas;

          FillScreens(cpu, canvas, screen, 0x00u);

          const std::uint8_t READINGS[] = {14u, 5u, 128u, 200u, 3u, 180u, 90u, 60u, 40u, 100u, 70u, 120u, 0xFFu};
          const std::uint16_t WHERE[] = {at.speed, at.rollMagnitude, at.rollSign,   at.pitchRate,  at.pitchMagnitude,  at.energy, at.fsh,
                                         at.ash,   at.qq14, at.cabtmp, at.gntmp, at.altit, at.flh};
          for (std::size_t index = 0; index < 13u; ++index)
          {
            cpu.memory[WHERE[index]] = READINGS[index];
          }
          cpu.memory[at.mcnt] = counter;
          cpu.memory[at.qq11] = 0u;
          cpu.memory[at.dflag] = already;
          cpu.memory[at.abraxas] = 0x81u;
          cpu.memory[at.caravanserai] = 0xC0u;
          cpu.memory[at.t2] = 0x77u;

          Elite::Bubble bubble;
          bubble.Count(Elite::ShipType::Station) = 1u;
          cpu.memory[static_cast<std::uint16_t>(at.many + Elite::Byte(Elite::ShipType::Station))] = 1u;

          // Two ships in the bubble, both with bit 4 of byte 31 set, so `zonkscanners` has
          // something to forget -- and the DFLAG case that skips it has to leave it alone.
          const std::uint8_t TYPES[] = {3u, 5u};
          for (std::size_t slot = 0; slot < 2u; ++slot)
          {
            bubble.slots[slot] = TYPES[slot];
            cpu.memory[static_cast<std::uint16_t>(at.frin + slot)] = TYPES[slot];
          }

          std::uint32_t state = 0x77C1A305u ^ (counter * 0x9E3779B9u) ^ already;
          for (std::size_t slot = 0; slot < 2u; ++slot)
          {
            std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = bubble.blocks[slot].ToBytes();
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              state = state * 1103515245u + 12345u;
              const std::uint8_t value = (byte == 31u) ? 0xFFu : static_cast<std::uint8_t>(state >> 17);
              shipBytes[byte] = value;
              cpu.memory[static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
            }
            bubble.blocks[slot] = Elite::Ship::FromBytes(shipBytes);
          }

          Elite::Compass compass{0xC3u, 0x9Cu, Elite::COMPASS_AHEAD};
          cpu.memory[at.comx] = compass.x;
          cpu.memory[at.comy] = compass.y;
          cpu.memory[at.comc] = Elite::PatternByte(compass.pattern);

          const Elite::Testing::RunResult run = cpu.CallSubroutine(wantdials, 400'000);
          Assert::IsTrue(run.completed, L"wantdials returned");

          Elite::DrawWorkspace draw;
          Elite::ScreenState screenState;
          screenState.dashboardShown = already;

          Elite::FlightState flight;
          flight.speed = READINGS[0];
          flight.rollMagnitude = READINGS[1];
          flight.rollSign = READINGS[2];
          flight.pitchRate = READINGS[3];
          flight.pitchMagnitude = READINGS[4];
          flight.mainLoopCounter = counter;

          Elite::FlightStatus status;
          status.energy = READINGS[5];
          status.forwardShield = READINGS[6];
          status.aftShield = READINGS[7];
          status.cabinTemperature = READINGS[9];
          status.laserTemperature = READINGS[10];
          status.altitude = READINGS[11];
          status.damageFlash = READINGS[12];

          // 6502: VIC+&15 and l1 -- seeded so that "NOSPRITES ran" is a byte and not a call count.
          Elite::VideoState video{};
          Elite::MemoryMap map;
          video.enabled = 0xA7u;
          map.port = 0xE7u;

          Elite::ShowDashboard(canvas, draw, screenState, bubble, flight, status, Elite::LightYearsTenths{READINGS[8]}, compass, video, map);

          const std::wstring where = WidenText("wantdials(DFLAG " + std::to_string(already) + ", MCNT " + std::to_string(counter) + ")");

          const std::uint32_t touched = CompareScreens(cpu, screen, canvas, 0x00u, where);

          Assert::AreEqual(cpu.memory[at.abraxas], screenState.colourBank, (where + L": abraxas").c_str());
          Assert::AreEqual(cpu.memory[at.caravanserai], screenState.bitmapMode, (where + L": caravanserai").c_str());
          Assert::AreEqual(cpu.memory[at.dflag], screenState.dashboardShown, (where + L": DFLAG").c_str());
          Assert::AreEqual(cpu.memory[at.comx], compass.x, (where + L": COMX").c_str());
          Assert::AreEqual(cpu.memory[at.comy], compass.y, (where + L": COMY").c_str());
          Assert::AreEqual(cpu.memory[at.comc], Elite::PatternByte(compass.pattern), (where + L": COMC").c_str());

          for (std::size_t slot = 0; slot < 2u; ++slot)
          {
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              const std::uint16_t address = static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte);
              Assert::AreEqual(cpu.memory[address], bubble.blocks[slot].ToBytes()[byte],
                               (where + L": K% slot " + std::to_wstring(slot) + L" byte " + std::to_wstring(byte)).c_str());
            }
          }

          // `NOSPRITES` runs either way, so the sprites are off and the map is back whatever
          // `DFLAG` is (M3-b-3a: the state where three counted calls used to be).
          Assert::AreEqual<std::uint32_t>(0u, video.enabled, (where + L": NOSPRITES switched every sprite off").c_str());
          Assert::AreEqual<std::uint32_t>(0xE4u, map.port, (where + L": and put the memory map back").c_str());

          Assert::IsTrue(touched > 0u, (where + L": something was drawn").c_str());
          copies += (already == 0u) ? 1u : 0u;
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(9u, compared, L"the whole sweep ran");
      Assert::IsTrue(copies > 0u && copies < compared, L"both halves of the DFLAG test were taken");
    }

    /*
     * 6502: TTX66K -- the whole screen set up, for every shape of view it distinguishes.
     *
     * Views 0 and 13 tail-jump into `wantdials` and never reach anything below that test, so those
     * two cases are the dashboard again through a different door. Views 2, 64 and 128 get one band
     * of colour cells and everything else gets two. And every path ends by falling into `BOX2` past
     * its `LDX #18`, so the border is 25 rows rather than 18 (§6.79).
     *
     * Three separate clears in three different shapes, and the marker is what makes them
     * distinguishable: a byte the routine zeroed and a byte it never touched are the same thing on
     * a screen that started at zero.
     */
    TEST_METHOD(TheWholeScreenMatchesTTX66K)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t ttx66k = oracle.Label("TTX66K");
      const std::uint16_t screen = ScreenBase(oracle);
      const std::uint16_t abraxas = oracle.Label("abraxas");
      const std::uint16_t caravanserai = oracle.Label("caravanserai");
      const std::uint16_t dflag = oracle.Label("DFLAG");
      const std::uint16_t comc = oracle.Label("COMC");
      const std::uint16_t xc = oracle.Label("XC");
      const std::uint16_t yc = oracle.Label("YC");
      const std::uint16_t qq11 = oracle.Label("QQ11");
      const std::uint16_t frin = oracle.Label("FRIN");
      const std::uint16_t kPercent = oracle.Label("K%");
      const std::uint16_t many = oracle.Label("MANY");
      const std::uint16_t comx = oracle.Label("COMX");
      const std::uint16_t comy = oracle.Label("COMY");
      const std::uint16_t t2 = oracle.Label("T2");
      const std::uint16_t mcnt = oracle.Label("MCNT");

      std::uint32_t compared = 0;
      std::uint32_t dashboards = 0;
      std::uint32_t oneBand = 0;

      for (const std::uint8_t view : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{6}, std::uint8_t{13},
                                      std::uint8_t{64}, std::uint8_t{128}, std::uint8_t{255}})
      {
        for (const std::uint8_t already : {std::uint8_t{0}, std::uint8_t{0xFF}})
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::Canvas canvas;

          FillScreens(cpu, canvas, screen, 0x1Du);

          cpu.memory[qq11] = view;
          cpu.memory[dflag] = already;
          cpu.memory[abraxas] = 0x33u;
          cpu.memory[caravanserai] = 0x44u;
          cpu.memory[comc] = 0x55u;
          cpu.memory[xc] = 0x66u;
          cpu.memory[yc] = 0x77u;
          cpu.memory[t2] = 0x88u;
          cpu.memory[mcnt] = 0u;
          cpu.memory[comx] = 0xC3u;
          cpu.memory[comy] = 0x9Cu;

          Elite::Bubble bubble;
          bubble.Count(Elite::ShipType::Station) = 1u;
          cpu.memory[static_cast<std::uint16_t>(many + Elite::Byte(Elite::ShipType::Station))] = 1u;

          const std::uint8_t TYPES[] = {3u, 5u};
          for (std::size_t slot = 0; slot < 2u; ++slot)
          {
            bubble.slots[slot] = TYPES[slot];
            cpu.memory[static_cast<std::uint16_t>(frin + slot)] = TYPES[slot];
          }

          std::uint32_t state = 0x2B91D6C5u ^ (view * 0x9E3779B9u) ^ already;
          for (std::size_t slot = 0; slot < 2u; ++slot)
          {
            std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = bubble.blocks[slot].ToBytes();
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              state = state * 1103515245u + 12345u;
              const std::uint8_t value = (byte == 31u) ? 0xFFu : static_cast<std::uint8_t>(state >> 17);
              shipBytes[byte] = value;
              cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
            }
            bubble.blocks[slot] = Elite::Ship::FromBytes(shipBytes);
          }

          const Elite::Testing::RunResult run = cpu.CallSubroutine(ttx66k, 400'000);
          Assert::IsTrue(run.completed, L"TTX66K returned");

          Elite::DrawWorkspace draw;
          Elite::TextState textState;
          Elite::ScreenState screenState;
          Elite::Compass compass{0xC3u, 0x9Cu, Elite::PixelPattern::Red};
          Elite::FlightState flight;
          Elite::FlightStatus status;
          Elite::VideoState video{};
          Elite::MemoryMap map;
          video.enabled = 0xA7u;
          map.port = 0xE7u;

          screenState.colourBank = 0x33u;
          screenState.bitmapMode = 0x44u;
          screenState.dashboardShown = already;
          textState.column = 0x66u;
          textState.row = 0x77u;

          Elite::SetUpScreenPixels(canvas, draw, textState, screenState, bubble, flight, status, Elite::LightYearsTenths{}, compass, video, map, view);

          const std::wstring where = WidenText("TTX66K(QQ11 " + std::to_string(view) + ", DFLAG " + std::to_string(already) + ")");

          Assert::IsTrue(CompareScreens(cpu, screen, canvas, 0x1Du, where) > 0u, (where + L": something was drawn").c_str());
          Assert::AreEqual(cpu.memory[abraxas], screenState.colourBank, (where + L": abraxas").c_str());
          Assert::AreEqual(cpu.memory[caravanserai], screenState.bitmapMode, (where + L": caravanserai").c_str());
          Assert::AreEqual(cpu.memory[dflag], screenState.dashboardShown, (where + L": DFLAG").c_str());
          Assert::AreEqual(cpu.memory[comc], Elite::PatternByte(compass.pattern), (where + L": COMC").c_str());
          Assert::AreEqual(cpu.memory[xc], textState.column, (where + L": XC").c_str());
          Assert::AreEqual(cpu.memory[yc], textState.row, (where + L": YC").c_str());

          for (std::size_t slot = 0; slot < 2u; ++slot)
          {
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              const std::uint16_t address = static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte);
              Assert::AreEqual(cpu.memory[address], bubble.blocks[slot].ToBytes()[byte],
                               (where + L": K% slot " + std::to_wstring(slot) + L" byte " + std::to_wstring(byte)).c_str());
            }
          }

          // Every path reaches `NOSPRITES`, so both sides end with the sprites off and the map back.
          Assert::AreEqual<std::uint32_t>(0u, video.enabled, (where + L": every sprite off").c_str());
          Assert::AreEqual<std::uint32_t>(0xE4u, map.port, (where + L": and the memory map back").c_str());

          dashboards += (view == 0u || view == 13u) ? 1u : 0u;
          oneBand += (view == 2u || view == 64u || view == 128u) ? 1u : 0u;
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(8u * 2u, compared, L"the whole sweep ran");
      Assert::IsTrue(dashboards > 0u, L"the tail call into wantdials was taken");
      Assert::IsTrue(oneBand > 0u, L"and the one-band views were covered");
    }
  };

  TEST_CLASS(TheScreenChange)
  {
  public:
    /*
     * 6502: TT66, which is `STA QQ11` and falls into TTX66 -- the whole routine, at last.
     *
     * `CHPR` IS NOT TRAPPED. The view's name and the hyperspace countdown are printed onto the
     * screen the routine has just cleared, so they belong in the whole-canvas compare with
     * everything else; trapping them would turn the one part of this routine that produces TEXT
     * into a character stream compared separately from the pixels around it.
     *
     * The sweep covers the space view (where the name is printed and the dashboard comes back),
     * view 13 (which takes the same tail call), and four text screens; both settings of `DFLAG`;
     * every one of the four space views, because the name is `VIEW ORA #&60`; and a countdown of
     * zero, which skips `ee3` entirely, against two that do not.
     */
    TEST_METHOD(TheScreenChangeMatchesTT66)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const std::uint16_t tt66 = oracle.Label("TT66");

      std::uint32_t compared = 0;
      std::uint32_t named = 0;
      std::uint32_t counted = 0;

      for (const std::uint8_t view :
           {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{6}, std::uint8_t{13}, std::uint8_t{64}, std::uint8_t{128}})
      {
        for (std::uint8_t spaceView = 0; spaceView < 4u; ++spaceView)
        {
          for (const std::uint8_t countdown : {std::uint8_t{0}, std::uint8_t{3}, std::uint8_t{15}})
          {
            Universe universe;
            Seed(universe, view * 97u + spaceView * 13u + countdown);
            universe.spaceView = spaceView;
            universe.view = 0xEEu; // whatever was up before, which `TT66` overwrites
            universe.status.hyperspaceCountdown = countdown;

            Cpu6502 cpu = oracle.Fresh();
            FillScreens(cpu, universe.canvas, at.screen, 0x1Du);
            Mirror(universe, cpu, at);

            cpu.a = view;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(tt66, 900'000);
            Assert::IsTrue(run.completed, L"TT66 returned");

            Elite::Ports ports = universe.Ports();
            Elite::SetUpScreen(universe, ports, view);

            const std::wstring where = WidenText("TT66(QQ11 " + std::to_string(view) + ", VIEW " + std::to_string(spaceView) + ", QQ22+1 " +
                                                 std::to_string(countdown) + ")");

            CompareScreens(cpu, at.screen, universe.canvas, 0x1Du, where);
            CompareState(cpu, universe, at, where);

            named += (view == 0u) ? 1u : 0u;
            counted += (countdown != 0u) ? 1u : 0u;
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(6u * 4u * 3u, compared, L"the whole sweep ran");
      Assert::IsTrue(named > 0u, L"the view's name was printed on some passes");
      Assert::IsTrue(counted > 0u, L"and the countdown on some");
    }

    /*
     * 6502: LOOK1, with LQ and LO2 -- all three exits.
     *
     * The one that is easy to get wrong is `LO2`: on the space view, asked for the view already
     * showing, the routine does the palette change and NOTHING else. A port that fell through to
     * the clear would redraw the screen on every press of a view key that changed nothing, which
     * looks almost right.
     */
    TEST_METHOD(TheViewChangeMatchesLOOK1)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const std::uint16_t look1 = oracle.Label("LOOK1");

      std::uint32_t compared = 0;
      std::uint32_t unchanged = 0;
      std::uint32_t reseeded = 0;

      for (const std::uint8_t view : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{64}})
      {
        for (std::uint8_t from = 0; from < 4u; ++from)
        {
          for (std::uint8_t to = 0; to < 4u; ++to)
          {
            Universe universe;
            Seed(universe, view * 31u + from * 7u + to);
            universe.view = view;
            universe.spaceView = from;

            Cpu6502 cpu = oracle.Fresh();
            cpu.AddTrap(oracle.Label("DOVDU19"));
            FillScreens(cpu, universe.canvas, at.screen, 0x1Du);
            Mirror(universe, cpu, at);

            cpu.x = to;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(look1, 900'000);
            Assert::IsTrue(run.completed, L"LOOK1 returned");

            Elite::Ports ports = universe.Ports();
            Elite::ChangeView(universe, ports, to);

            const std::wstring where =
              WidenText("LOOK1(QQ11 " + std::to_string(view) + ", VIEW " + std::to_string(from) + " -> " + std::to_string(to) + ")");

            CompareScreens(cpu, at.screen, universe.canvas, 0x1Du, where);
            CompareState(cpu, universe, at, where);

            /*
             * 6502: LDA #0 / JSR DOVDU19 -- ASSERTED HERE UNTIL M3-b-2b, AND IT ASSERTED NOTHING.
             *
             * `DOVDU19` is a bare `RTS` on this build; the two lines that stood here counted the
             * port's calls to a seam whose every implementation was empty, on both sides of a
             * comparison that could not see the difference. `CompareScreens` and `CompareState`
             * above are what the routine's palette change is worth, which is nothing.
             */

            if (view == 0u && to == from)
            {
              ++unchanged;
            }
            if (view != 0u)
            {
              ++reseeded;
            }
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(3u * 4u * 4u, compared, L"the whole sweep ran");
      Assert::IsTrue(unchanged > 0u, L"LO2's do-nothing path was taken");
      Assert::IsTrue(reseeded > 0u, L"and LQ's reseeding path");
    }

    /*
     * 6502: WARP -- the "J" key, and it refuses more often than it agrees.
     *
     * Four refusals and one jump. The sweep covers each refusal on its own -- junk in the slot two
     * above the junk count, a station in the bubble, witchspace, and either body closer than two
     * units -- and the negative-sign case for each body, which skips its distance test because a
     * body behind you cannot be flown into.
     */
    TEST_METHOD(TheWarpMatchesWARP)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const std::uint16_t warp = oracle.Label("WARP");

      struct Case
      {
        const char* what;
        std::uint8_t junk, occupant, station, midJump;
        std::uint8_t planetSign, planetHigh, sunSign, sunHigh;
      };

      const std::vector<Case> CASES = {
        {"clear space, both bodies far", 1, 0, 0, 0, 0x30, 0x40, 0x30, 0x40},
        {"a ship above the junk", 1, 7, 0, 0, 0x30, 0x40, 0x30, 0x40},
        {"a station in the bubble", 1, 0, 1, 0, 0x30, 0x40, 0x30, 0x40},
        {"witchspace", 1, 0, 0, 0xFF, 0x30, 0x40, 0x30, 0x40},
        {"the planet too close", 1, 0, 0, 0, 0x01, 0x00, 0x30, 0x40},
        {"the sun too close", 1, 0, 0, 0, 0x30, 0x40, 0x01, 0x00},
        {"the planet behind you", 1, 0, 0, 0, 0x81, 0x00, 0x30, 0x40},
        {"the sun behind you", 1, 0, 0, 0, 0x30, 0x40, 0x81, 0x00},
        {"both behind you", 1, 0, 0, 0, 0x81, 0x00, 0x81, 0x00},
        {"no junk at all", 0, 0, 0, 0, 0x30, 0x40, 0x30, 0x40},
        {"junk right up to the slot", 3, 0, 0, 0, 0x30, 0x40, 0x30, 0x40},

        /*
         * The `CMP #2 / BCC WA1` boundary, from both sides and on both bodies. `MAS2` ORs the three
         * sign bytes and drops bit 7, so a sign byte of 2 IS a largest axis of 2 -- which is the
         * smallest distance the routine will still warp from. Without these four the comparison
         * cannot tell `< 2` from `< 3`, which is what the mutation sweep found.
         */
        {"the planet at exactly two", 1, 0, 0, 0, 0x02, 0x00, 0x30, 0x40},
        {"the planet at one below two", 1, 0, 0, 0, 0x01, 0x00, 0x30, 0x40},
        {"the sun at exactly two", 1, 0, 0, 0, 0x30, 0x40, 0x02, 0x00},
        {"the sun at one below two", 1, 0, 0, 0, 0x30, 0x40, 0x01, 0x00},
        {"both at exactly two", 1, 0, 0, 0, 0x02, 0x00, 0x02, 0x00},
      };

      std::uint32_t jumped = 0;
      std::uint32_t refused = 0;

      for (const Case& item : CASES)
      {
        Universe universe;
        Seed(universe, 0x5Au);
        universe.view = 0u;
        universe.spaceView = 1u;
        universe.bubble.junk = item.junk;
        universe.bubble.Count(Elite::ShipType::Station) = item.station;
        universe.status.midJump = item.midJump;

        for (std::size_t slot = 0; slot < universe.bubble.slots.size(); ++slot)
        {
          universe.bubble.slots[slot] = 0u;
        }
        universe.bubble.slots[0] = 128u; // the planet
        universe.bubble.slots[1] = 129u; // the sun
        const std::size_t above = static_cast<std::size_t>(item.junk) + 2u;
        if (above < universe.bubble.slots.size())
        {
          universe.bubble.slots[above] = item.occupant;
        }

        universe.bubble.blocks[0].x.sgn = 0u;
        universe.bubble.blocks[0].y.sgn = 0u;
        universe.bubble.blocks[0].z.sgn = item.planetSign;
        universe.bubble.blocks[0].z.hi = item.planetHigh;
        universe.bubble.blocks[1].x.sgn = 0u;
        universe.bubble.blocks[1].y.sgn = 0u;
        universe.bubble.blocks[1].z.sgn = item.sunSign;
        universe.bubble.blocks[1].z.hi = item.sunHigh;

        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(oracle.Label("DOVDU19"));
        FillScreens(cpu, universe.canvas, at.screen, 0x1Du);
        Mirror(universe, cpu, at);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(warp, 900'000);
        Assert::IsTrue(run.completed, L"WARP returned");

        Elite::Ports ports = universe.Ports();
        Elite::Warp(universe, ports);

        const std::wstring where = WidenText(std::string("WARP (") + item.what + ")");

        CompareScreens(cpu, at.screen, universe.canvas, 0x1Du, where);
        CompareState(cpu, universe, at, where);

        /*
         * 6502: LDY #sfxboop / JMP NOISE -- the refusal, and `CompareState` above already carries
         * it since M3-b-2a.
         *
         * It was `ViewEffects::PlaySound`, trapped on the oracle and recorded here, and this
         * counted the two lists against each other. Both machines run `NOISE` now, so the refusal
         * is `SOFLG` on the voice `sfxboop` took -- compared byte for byte with the rest of the
         * universe rather than as a tally, and the coverage counters read it back the same way.
         */
        const bool refusedThis = universe.sound.flag[2] != 0u;
        if (!refusedThis)
        {
          ++jumped;
        }
        else
        {
          ++refused;
        }
      }

      Assert::IsTrue(jumped > 0u, L"some warps were allowed");
      Assert::IsTrue(refused > 0u, L"and some refused");
    }
  };

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
