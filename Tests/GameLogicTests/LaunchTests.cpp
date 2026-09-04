#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "Commander.h"
#include "Market.h"
#include "PlanetDraw.h"
#include "ShipDraw.h"

#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Leaving the station (the launch path).
 *
 * `TT110` is what the "1" key reaches from the docked screens, and everything under it was either
 * already ported or is here: the contraband fine, the hyperspace rings, and the two resets. None
 * of it needs the executable, which is why it is compared here rather than looked at.
 */
namespace GameLogicTests
{

TEST_CLASS(TheContrabandFine)
{
public:
  /*
   * 6502: BAD -- six instructions, and the doubling is a shift of the SUM.
   *
   * Swept over the three slots it reads rather than over the whole hold, because the other
   * fourteen are not in the routine at all -- and past 128 tonnes, where the `ASL` wraps and a
   * hold full of narcotics comes out innocent. The hold cannot hold that much; the sweep goes
   * there anyway, because "unreachable" is a claim about the caller and not about this routine.
   */
  TEST_METHOD(TheContrabandFineMatchesBAD)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t bad = oracle.Label("BAD");
    const std::uint16_t qq20 = oracle.Label("QQ20");

    const std::uint8_t AMOUNTS[] = { 0u, 1u, 2u, 7u, 63u, 64u, 65u, 127u, 128u, 200u, 255u };

    std::uint32_t compared = 0;
    std::uint32_t wrapped = 0;

    for (const std::uint8_t slaves : AMOUNTS)
    {
      for (const std::uint8_t narcotics : AMOUNTS)
      {
        for (const std::uint8_t firearms : AMOUNTS)
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.memory[static_cast<std::uint16_t>(qq20 + 3u)] = slaves;
          cpu.memory[static_cast<std::uint16_t>(qq20 + 6u)] = narcotics;
          cpu.memory[static_cast<std::uint16_t>(qq20 + 10u)] = firearms;

          const Elite::Testing::RunResult run = cpu.CallSubroutine(bad, 4000);
          Assert::IsTrue(run.completed, L"BAD returned");

          Elite::CommanderBlock commander;
          const std::size_t hold = static_cast<std::size_t>(Elite::Field::CargoHold);
          commander.bytes[hold + 3u] = slaves;
          commander.bytes[hold + 6u] = narcotics;
          commander.bytes[hold + 10u] = firearms;

          const std::wstring where =
            WidenText("BAD(slaves " + std::to_string(slaves) + ", narcotics "
                      + std::to_string(narcotics) + ", firearms " + std::to_string(firearms) + ")");

          Assert::AreEqual(cpu.a, Elite::ContrabandPenalty(commander), where.c_str());

          wrapped += (static_cast<std::uint16_t>(slaves) + narcotics >= 128u) ? 1u : 0u;
          ++compared;
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(11u * 11u * 11u, compared, L"the whole sweep ran");
    Assert::IsTrue(wrapped > 0u, L"the doubling wrapped on some passes");
  }
};

TEST_CLASS(TheHyperspaceRings)
{
public:
  /*
   * 6502: HFS1 -- eight rings, compared on the whole bitmap and on both heaps.
   *
   * The heap is what makes this worth comparing rather than the pixels alone: `LSP` is rewound to
   * one before every circle, so all eight rings share one run and each is EORed over the last.
   * A port that let the heap grow would draw the same picture on the first pass and a different
   * one on the second.
   */
  TEST_METHOD(TheHyperspaceRingsMatchHFS1)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Where at(oracle);
    const std::uint16_t hfs1 = oracle.Label("HFS1");
    const std::uint16_t yx2m1 = oracle.Label("Yx2M1");
    const std::uint16_t dontclip = oracle.Label("dontclip");

    const std::uint16_t stp = oracle.Label("STP");

    /*
     * 6502: STP -- and `HFS1` DOES NOT SET IT.
     *
     * Neither `HFS1` nor `HFS2` writes the step, so the rings are drawn at whatever coarseness
     * the last `CIRCLE` chose for the planet or the sun -- 8 for a small disc, 4 for a middling
     * one and 2 for a large one. A zero would make `CIRCLE2` loop for ever, which is what the
     * first version of this test did, and the game cannot reach a zero because `CIRCLE` is the
     * only writer and never stores one (§6.94).
     */
    for (const std::uint8_t step : { std::uint8_t{ 2 }, std::uint8_t{ 4 }, std::uint8_t{ 8 } })
    {
    for (std::uint8_t pass = 0; pass < 2u; ++pass)
    {
      World world;
      Seed(world, 0x2Bu);
      world.heaps.yx2M1 = 143u;
      world.heaps.lsp = 0u;
      world.heaps.stp = step;
      for (std::size_t index = 0; index < world.heaps.ball.size(); ++index)
      {
        world.heaps.ball[index] = 0xFFu;
      }

      Cpu6502 cpu = oracle.Fresh();
      FillScreens(cpu, world.canvas, at.screen, 0x1Du);
      Mirror(world, cpu, at);
      cpu.memory[yx2m1] = world.heaps.yx2M1;
      cpu.memory[dontclip] = 0u;
      cpu.memory[stp] = step;

      Elite::DrawWorkspace draw;
      Elite::GeometryWorkspace geometry;
      Elite::MathWorkspace math;
      Elite::ClipState clip;

      // The second pass runs the effect TWICE on both sides, which is what proves the heap is
      // rewound: a heap that grew would leave the first pass's rings on screen.
      for (std::uint8_t again = 0; again <= pass; ++again)
      {
        const Elite::Testing::RunResult run = cpu.CallSubroutine(hfs1, 40'000'000);
        Assert::IsTrue(run.completed,
                       (std::wstring(L"HFS1 returned -- illegal ")
                        + std::to_wstring(run.illegalOpcode) + L", instructions "
                        + std::to_wstring(run.instructions) + L", stoppedAt "
                        + std::to_wstring(run.stoppedAt)).c_str());

        Elite::DrawHyperspaceRings(world.canvas, world.heaps, draw, geometry, math, clip);
      }

      const std::wstring where =
        WidenText("HFS1 (STP " + std::to_string(step) + ", " + std::to_string(pass + 1u)
                  + " pass(es))");

      /*
       * One pass draws; TWO PASSES ERASE. Everything the effect touches is EORed, and the heap is
       * rewound to `LSP = 1` before every circle, so running it again puts the screen back byte
       * for byte -- which is how the game takes the rings off without remembering where they were.
       * The count is asserted both ways because "drew something" alone would pass for a routine
       * that drew and never cleaned up.
       */
      const std::uint32_t touched =
        CompareScreens(cpu, at.screen, world.canvas, 0x1Du, where);

      if (pass == 0u)
      {
        Assert::IsTrue(touched > 0u, (where + L": something was drawn").c_str());
      }
      else
      {
        Assert::AreEqual<std::uint32_t>(0u, touched, (where + L": and drawing it twice erases it").c_str());
      }

      Assert::AreEqual(cpu.memory[at.lsp], world.heaps.lsp, (where + L": LSP").c_str());
      for (std::size_t index = 0; index < world.heaps.ball.size(); ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.lsx2 + index)],
                         world.heaps.ball[index],
                         (where + L": ball heap byte " + std::to_wstring(index)).c_str());
      }
    }
    }
  }
};

} // namespace GameLogicTests
