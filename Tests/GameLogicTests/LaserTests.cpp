#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Canvas.h"
#include "Dashboard.h"
#include "Lasers.h"
#include "Rng.h"

#include <array>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The player's laser (slice 3d-c).
 *
 * Compared on the bitmap and on every byte the routine leaves: the convergence point, the laser
 * temperature, and the generator's four state bytes -- because `LASLI` calls `DORND` twice and
 * both calls feed a carry into the addition that follows them.
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

  } // namespace

  TEST_CLASS(ThePlayersLaser)
  {
  public:
    /*
     * 6502: LASLI, which falls into LASLI2, which falls into las.
     *
     * Swept over generator states rather than over coordinates, because the coordinates ARE the
     * generator: `AND #7 / ADC #Y-4` has no `CLC`, so the carry `DORND` exits with is part of the
     * answer, and so is the carry the first addition hands the second. The entry carry is swept for
     * the same reason -- `DORND` reads it.
     */
    TEST_METHOD(FiringMatchesLASLI)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t lasli = oracle.Label("LASLI");
      const std::uint16_t lasx = oracle.Label("LASX");
      const std::uint16_t lasy = oracle.Label("LASY");
      const std::uint16_t gntmp = oracle.Label("GNTMP");
      const std::uint16_t qq11 = oracle.Label("QQ11");
      const std::uint16_t rand = oracle.Label("RAND");
      const std::uint16_t energyBanks = oracle.Label("ENERGY");

      const Cpu6502 image = oracle.Fresh();
      const std::uint16_t screenBase =
        static_cast<std::uint16_t>((image.memory[oracle.Label("ylookupl")] | (image.memory[oracle.Label("ylookuph")] << 8)) - 0x20);

      std::uint32_t compared = 0;
      std::uint32_t drawn = 0;
      std::set<std::uint8_t> heights;
      std::set<std::uint8_t> across;

      for (std::uint32_t seed = 0; seed < 120; ++seed)
      {
        for (const std::uint8_t view : {0u, 1u})
        {
          for (const bool carryIn : {false, true})
          {
            Cpu6502 cpu = oracle.Fresh();
            Elite::Canvas canvas;
            Elite::DrawWorkspace draw;
            Elite::Rng rng;
            Elite::LaserBurst burst;
            Elite::FlightStatus status;

            /*
             * 6502: JSR DENGY -- built in slice 3d-d-iii-b, so it is no longer trapped.
             *
             * The banks start at a value the drain can be seen in and are compared afterwards, which
             * is stronger than counting the calls was: a port that drained twice, or drained the
             * wrong byte, now differs rather than agreeing on a tally.
             */
            status.energy = 0x40u;
            cpu.memory[energyBanks] = 0x40u;

            std::uint32_t state = 0x3F17C5A9u ^ (seed * 0x9E3779B9u);
            std::array<std::uint8_t, 4> bytes{};
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              state = state * 1103515245u + 12345u;
              bytes[byte] = static_cast<std::uint8_t>(state >> 17);
              cpu.memory[static_cast<std::uint16_t>(rand + byte)] = bytes[byte];
            }
            rng.SetState(bytes);

            const std::uint8_t heat = static_cast<std::uint8_t>(seed * 2u);
            cpu.memory[gntmp] = heat;
            cpu.memory[qq11] = view;
            cpu.memory[lasx] = 0x5Au;
            cpu.memory[lasy] = 0x5Au;
            cpu.c = carryIn;

            status.laserTemperature = heat;
            burst.x = 0x5Au;
            burst.y = 0x5Au;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(lasli, 200'000);
            Assert::IsTrue(run.completed, L"LASLI returned");

            (void)Elite::FireLaser(canvas, draw, rng, burst, status, view, carryIn);

            const std::wstring where =
              Widen("LASLI seed " + std::to_string(seed) + " view " + std::to_string(view) + " carry " + std::to_string(carryIn ? 1 : 0));

            const std::span<const std::uint8_t> ours = canvas.Screen();
            for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
            {
              const std::uint8_t expected = cpu.memory[static_cast<std::uint16_t>(screenBase + offset)];
              if (expected != ours[offset])
              {
                Assert::Fail((where + L": screen differs at offset " + std::to_wstring(offset) + L" -- game has " +
                              std::to_wstring(expected) + L", port has " + std::to_wstring(ours[offset]))
                               .c_str());
              }
              drawn += (ours[offset] != 0u) ? 1u : 0u;
            }

            Assert::AreEqual(cpu.memory[lasx], burst.x, (where + L": LASX").c_str());
            Assert::AreEqual(cpu.memory[lasy], burst.y, (where + L": LASY").c_str());
            Assert::AreEqual(cpu.memory[gntmp], status.laserTemperature, (where + L": GNTMP").c_str());
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(rand + byte)], rng.State()[byte],
                               (where + L": RAND+" + std::to_wstring(byte)).c_str());
            }
            Assert::AreEqual(cpu.memory[energyBanks], status.energy, (where + L": ENERGY, after the drain DENGY does").c_str());

            /*
             * The heat added is always EIGHT, and that is worth asserting rather than assuming.
             *
             * `LDA GNTMP / ADC #8` has no `CLC` and reads the carry the x coordinate's addition
             * left -- but that addition is `AND #7` plus 124 plus at most one, which is 132 at the
             * most and cannot carry out. So the third uncleared `ADC` in this routine is the
             * constant kind and the first two are not (§6.68). This sweep is what settled that; the
             * first version of it asserted eight OR nine and failed.
             */
            const std::uint8_t added = static_cast<std::uint8_t>(status.laserTemperature - heat);
            Assert::AreEqual<std::uint8_t>(8u, added, (where + L": the heat a shot costs").c_str());

            heights.insert(burst.y);
            across.insert(burst.x);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(120u * 2u * 2u, compared, L"the whole sweep ran");
      Assert::IsTrue(drawn > 0u, L"and the beams were actually drawn");

      /*
       * NINE values, not eight, and that is the measurement that says `DORND`'s carry reaches the
       * coordinate. `AND #7` can only produce eight, so a ninth can only come from the `ADC` that
       * follows it having a carry in -- and both axes show it.
       */
      Assert::AreEqual<std::size_t>(9u, heights.size(), L"the y coordinate spans AND #7 plus a carry");
      Assert::AreEqual<std::size_t>(9u, across.size(), L"and so does the x");

      Logger::WriteMessage(("LASLI: " + std::to_string(compared) + " shots, " + std::to_string(heights.size()) + " convergence rows and " +
                            std::to_string(across.size()) + " columns")
                             .c_str());
    }
  };

} // namespace GameLogicTests
