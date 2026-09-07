#include "pch.h"

#include "Cpu6502.h"
#include "FlightPort.h"
#include "FlightUniverse.h"
#include "OracleImage.h"

#include "Commander.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "Market.h"
#include "PlanetDraw.h"
#include "Combat.h"
#include "ShipDraw.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>
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

      const std::uint8_t AMOUNTS[] = {0u, 1u, 2u, 7u, 63u, 64u, 65u, 127u, 128u, 200u, 255u};

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

            Elite::Commander commander;
            commander.cargoHold[3] = slaves;
            commander.cargoHold[6] = narcotics;
            commander.cargoHold[10] = firearms;

            const std::wstring where = WidenText("BAD(slaves " + std::to_string(slaves) + ", narcotics " + std::to_string(narcotics) +
                                                 ", firearms " + std::to_string(firearms) + ")");

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
      for (const std::uint8_t step : {std::uint8_t{2}, std::uint8_t{4}, std::uint8_t{8}})
      {
        for (std::uint8_t pass = 0; pass < 2u; ++pass)
        {
          Universe universe;
          Seed(universe, 0x2Bu);
          universe.heaps.yx2M1 = 143u;
          universe.heaps.lsp = 0u;
          universe.heaps.stp = step;
          for (std::size_t index = 0; index < universe.heaps.ball.size(); ++index)
          {
            universe.heaps.ball[index] = 0xFFu;
          }

          Cpu6502 cpu = oracle.Fresh();
          FillScreens(cpu, universe.canvas, at.screen, 0x1Du);
          Mirror(universe, cpu, at);
          cpu.memory[yx2m1] = universe.heaps.yx2M1;
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
                           (std::wstring(L"HFS1 returned -- illegal ") + std::to_wstring(run.illegalOpcode) + L", instructions " +
                            std::to_wstring(run.instructions) + L", stoppedAt " + std::to_wstring(run.stoppedAt))
                             .c_str());

            Elite::DrawHyperspaceRings(universe.canvas, universe.heaps, geometry, math, clip, universe.unused);
          }

          const std::wstring where = WidenText("HFS1 (STP " + std::to_string(step) + ", " + std::to_string(pass + 1u) + " pass(es))");

          /*
       * One pass draws; TWO PASSES ERASE. Everything the effect touches is EORed, and the heap is
       * rewound to `LSP = 1` before every circle, so running it again puts the screen back byte
       * for byte -- which is how the game takes the rings off without remembering where they were.
       * The count is asserted both ways because "drew something" alone would pass for a routine
       * that drew and never cleaned up.
       */
          const std::uint32_t touched = CompareScreens(cpu, at.screen, universe.canvas, 0x1Du, where);

          if (pass == 0u)
          {
            Assert::IsTrue(touched > 0u, (where + L": something was drawn").c_str());
          }
          else
          {
            Assert::AreEqual<std::uint32_t>(0u, touched, (where + L": and drawing it twice erases it").c_str());
          }

          Assert::AreEqual(cpu.memory[at.lsp], universe.heaps.lsp, (where + L": LSP").c_str());
          for (std::size_t index = 0; index < universe.heaps.ball.size(); ++index)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.lsx2 + index)], universe.heaps.ball[index],
                             (where + L": ball heap byte " + std::to_wstring(index)).c_str());
          }
        }
      }
    }
  };

  /*
   * 6502: LAUN and the `HFS2` it falls into -- the tunnel a launch and an arrival both open with.
   *
   * The port had this behind a seam until this slice, so the LAST thing on the launch path that
   * was not compared against the shipped game is compared here (§6.109). Three things happen before
   * the rings and each one is asserted separately, because each is a different kind of mistake:
   * the whoosh is a call whose RESULT is dropped, the step is a store the port used to be missing
   * altogether, and the view type is saved and restored around a `TT66` that would otherwise
   * change it.
   */
  TEST_CLASS(TheLaunchTunnel)
  {
  public:
    TEST_METHOD(TheLaunchTunnelMatchesLAUN)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const std::uint16_t laun = oracle.Label("LAUN");
      const std::uint16_t stp = oracle.Label("STP");
      const std::uint16_t noise = oracle.Label("NOISE");

      // Every view the tunnel can be drawn over: the docked screens as well as the space view,
      // because `LAUN` is called from `DOENTRY` while a docked screen is still up.
      for (const std::uint8_t view : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{4}, std::uint8_t{13}, std::uint8_t{255}})
      {
        Universe universe;
        Seed(universe, 0x71u);
        universe.view = view;
        universe.heaps.yx2M1 = 143u;
        universe.heaps.lsp = 0u;

        /*
         * `STP` starts at something the launch must overwrite. Four is the value §6.95 had the app
         * seeding precisely because nothing on this path wrote one -- and `LAUN` is what writes it,
         * so the seed stops being load-bearing the moment this routine exists.
         */
        universe.heaps.stp = 4u;

        Cpu6502 cpu = oracle.Fresh();
        FillScreens(cpu, universe.canvas, at.screen, 0x1Du);
        Mirror(universe, cpu, at);
        cpu.memory[oracle.Label("Yx2M1")] = 143u;
        cpu.memory[oracle.Label("dontclip")] = 0u;
        cpu.memory[stp] = 4u;

        // The three the platform owns, plus §6.108's: `TT66` reaches `NOSPRITES`, and `NOSPRITES`
        // writes VIC registers that are the ship blueprint table in the oracle's flat memory.
        cpu.AddTrap(oracle.Label("DOVDU19"));
        cpu.AddTrap(oracle.Label("NOSPRITES"));

        const Elite::Testing::RunResult run = cpu.CallSubroutine(laun, 40'000'000);
        Assert::IsTrue(run.completed, (std::wstring(L"LAUN returned -- illegal ") + std::to_wstring(run.illegalOpcode) + L", stoppedAt " +
                                       std::to_wstring(run.stoppedAt))
                                        .c_str());

        Elite::Ports ports = universe.Ports();
        Elite::DrawLaunchTunnel(universe, ports);

        const std::wstring where = WidenText("LAUN (QQ11 " + std::to_string(view) + ")");

        // 6502: LDY #sfxwhosh / JSR NOISE -- effect 4, once, and its carry is dropped.
        // And the port's, which is the BUFFER since M3-b-2a: `SOFLG` holds the effect plus one
        // with bit 7 set for "new, not yet started", so the flag says which voice took `sfxwhosh`.
        constexpr std::uint8_t LAUNCH_FLAG =
          static_cast<std::uint8_t>(0x80u | (static_cast<std::uint8_t>(Elite::SoundEffect::Missile) + 1u));
        Assert::AreEqual<std::uint8_t>(LAUNCH_FLAG, universe.sound.flag[2],
                                       (where + L": and so does the port").c_str());

        // 6502: LDA #8 / STA STP -- the step, which is the whole of §6.94's missing writer.
        Assert::AreEqual<std::uint8_t>(Elite::LAUNCH_TUNNEL_STEP, cpu.memory[stp], (where + L": the shipped STP").c_str());
        Assert::AreEqual(cpu.memory[stp], universe.heaps.stp, (where + L": STP").c_str());

        // 6502: LDA QQ11 / PHA / ... / PLA / STA QQ11 -- the view survives the TT66 inside.
        Assert::AreEqual(cpu.memory[at.qq11], universe.view, (where + L": QQ11").c_str());
        Assert::AreEqual<std::uint8_t>(view, universe.view, (where + L": and it is the one that went in").c_str());

        Assert::IsTrue(CompareScreens(cpu, at.screen, universe.canvas, 0x1Du, where) > 0u, (where + L": something was drawn").c_str());
        CompareState(cpu, universe, at, where);
      }
    }

    /*
     * The pacing, which is the port's and not the game's -- so it is asserted against the SHAPE of
     * the 6502 loop rather than against the oracle, which has no present to count.
     *
     * Thirty-four circles: the ring starting at radius 8 doubles to 16, 32, 64 and 128 before the
     * `ASL` carries, the one at 9 does the same, and the six from 10 to 15 stop one earlier because
     * they cross 160 first. That count does not depend on `STP` -- the step is how many segments a
     * circle has, not how many circles a ring is -- which is why one number covers both callers.
     */
    /*
     * 6502: LL164 -- the hyperspace tunnel, compared against the shipped routine.
     *
     * `HYPNOISE` is trapped on both of its seams and its ORDER is what the comparison is really
     * about: two effects, a frame, and a third effect whose number has bit 7 set. The pixels are
     * `HFS2` at step 4, which is the same body the launch draws at step 8 -- so a port that had
     * copied the launch instead of sharing `HFS2` would pass the picture and fail the step.
     */
    TEST_METHOD(TheHyperspaceTunnelMatchesLL164)
    {
      if (OracleMissing())
      {
        return;
      }

      struct Counting final : Elite::Presenter
      {
        std::uint32_t frames = 0;
        void Present() override
        {
          ++frames;
        }
        void WaitFrames(std::uint8_t) override {}
        void HoldFlightFrame(std::uint8_t) override {}
        void HoldTitleFrame(std::uint8_t) override {}
      };

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const std::uint16_t ll164 = oracle.Label("LL164");
      const std::uint16_t stp = oracle.Label("STP");
      const std::uint16_t noise = oracle.Label("NOISE");
      const std::uint16_t noise2 = oracle.Label("NOISE2");
      const std::uint16_t delay = oracle.Label("DELAY");

      for (const std::uint8_t view : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{255}})
      {
        Universe universe;
        Seed(universe, 0x5Eu);
        universe.view = view;
        universe.heaps.yx2M1 = 143u;
        universe.heaps.lsp = 0u;
        universe.heaps.stp = 8u; // the launch's step, so a routine that forgot to store 4 is visible

        Cpu6502 cpu = oracle.Fresh();
        FillScreens(cpu, universe.canvas, at.screen, 0x1Du);
        Mirror(universe, cpu, at);
        cpu.memory[oracle.Label("Yx2M1")] = 143u;
        cpu.memory[oracle.Label("dontclip")] = 0u;
        cpu.memory[stp] = 8u;

        cpu.AddTrap(delay);
        cpu.AddTrap(oracle.Label("DOVDU19"));
        cpu.AddTrap(oracle.Label("NOSPRITES")); // §6.108, third time

        const Elite::Testing::RunResult run = cpu.CallSubroutine(ll164, 40'000'000);
        Assert::IsTrue(run.completed, L"LL164 returned");

        Counting counting;
        Elite::Ports ports = universe.PortsWith(universe.unused, universe.unused, counting);
        Elite::DrawHyperspaceTunnel(universe, ports);

        const std::wstring where = WidenText("LL164 (QQ11 " + std::to_string(view) + ")");

        // 6502: LDA #4 / JSR HFS2 -- the step, which is the whole reason this is not `LAUN`.
        Assert::AreEqual<std::uint8_t>(Elite::HYPERSPACE_TUNNEL_STEP, cpu.memory[stp], (where + L": the shipped STP").c_str());
        Assert::AreEqual(cpu.memory[stp], universe.heaps.stp, (where + L": STP").c_str());

        // 6502: LDY #1 / JSR DELAY -- one vertical sync, plus one frame for each circle drawn.
        std::uint32_t delays = 0;
        for (const Cpu6502::TrapHit& hit : cpu.trapHits)
        {
          delays += (hit.address == delay) ? 1u : 0u;
        }
        Assert::AreEqual<std::uint32_t>(1u, delays, (where + L": one DELAY").c_str());
        Assert::AreEqual<std::uint32_t>(35u, counting.frames, (where + L": 34 circles and the DELAY").c_str());

        /*
         * 6502: HYPNOISE's three calls -- pitched sfxhyp1, then sfxwhosh, then sfxhyp1 layered with
         * bit 7 set -- and the BUFFER is what says so since M3-b-2a.
         *
         * `NOISE` and `NOISE2` were trapped on the oracle and counted here; both run on both sides
         * now, so what is compared is which voice each effect took, at what priority, with what
         * frequency and envelope. A list of effect numbers could not tell a layered `sfxhyp1` from
         * a plain one that lost its voice.
         */
        CompareSound(cpu, universe, at, where);

        // 6502: QQ11 is saved across the TT66 inside HFS2, exactly as the launch's is.
        Assert::AreEqual(cpu.memory[at.qq11], universe.view, (where + L": QQ11").c_str());

        Assert::IsTrue(CompareScreens(cpu, at.screen, universe.canvas, 0x1Du, where) > 0u, (where + L": something was drawn").c_str());
      }
    }

    TEST_METHOD(ThePacingIsOneFramePerCircle)
    {
      struct Counting final : Elite::Presenter
      {
        std::uint32_t circles = 0;
        void Present() override
        {
          ++circles;
        }
        void WaitFrames(std::uint8_t) override {}
        void HoldFlightFrame(std::uint8_t) override {}
        void HoldTitleFrame(std::uint8_t) override {}
      };

      for (const std::uint8_t step : {std::uint8_t{2}, std::uint8_t{4}, std::uint8_t{8}})
      {
        Universe universe;
        Seed(universe, 0x71u);
        universe.heaps.yx2M1 = 143u;
        universe.heaps.lsp = 0u;
        universe.heaps.stp = step;

        Elite::ClipState clip;
        Counting counting;
        Elite::DrawHyperspaceRings(universe.canvas, universe.heaps, universe.geometry, universe.math, clip, counting);

        Assert::AreEqual<std::uint32_t>(34u, counting.circles, (WidenText("STP " + std::to_string(step)) + L": circles shown").c_str());
      }

      // And a null pacing is the same drawing with nobody watching, which is what the oracle
      // comparisons pass: the count above must not be reachable through a screen difference.
      Universe unpaced;
      Seed(unpaced, 0x71u);
      unpaced.heaps.yx2M1 = 143u;
      unpaced.heaps.lsp = 0u;
      unpaced.heaps.stp = 8u;

      Universe paced;
      Seed(paced, 0x71u);
      paced.heaps.yx2M1 = 143u;
      paced.heaps.lsp = 0u;
      paced.heaps.stp = 8u;

      Elite::ClipState clipA;
      Elite::ClipState clipB;
      Counting counting;
      Elite::DrawHyperspaceRings(unpaced.canvas, unpaced.heaps, unpaced.geometry, unpaced.math, clipA, unpaced.unused);
      Elite::DrawHyperspaceRings(paced.canvas, paced.heaps, paced.geometry, paced.math, clipB, counting);

      const std::span<const std::uint8_t> quiet = unpaced.canvas.Screen();
      const std::span<const std::uint8_t> watched = paced.canvas.Screen();
      for (std::size_t index = 0; index < quiet.size(); ++index)
      {
        Assert::AreEqual(quiet[index], watched[index], (L"pacing changes no pixel, byte " + std::to_wstring(index)).c_str());
      }
    }
  };

  namespace
  {
    /*
     * THE LAUNCH PATH HAS NO SEAMS LEFT, and this object is what is left of the ones it had.
     *
     * `LAUN` was the last: the docking tunnel needed the ball line heap, which arrived in 3c, and
     * the stub outlived its reason by two slices as every other one on this path did (§6.109). It
     * is ported now and runs for real on both sides, so the whole-bitmap compare below covers the
     * tunnel as well as everything after it. What remains here is `StartUpEffects` for the
     * routines that still take one; `Launch` no longer does.
     */
    struct RecordingStart final : Elite::StartUpEffects, Elite::Presenter, Elite::Keyboard
    {
      void Present() override {}
      void HoldFlightFrame(std::uint8_t) override {}
      void HoldTitleFrame(std::uint8_t) override {}
      void ClearKeyLogger() override {}

      /*
       * WHICH KEYS ARE DOWN, scripted, because the loop `RDKEY` drives is key-driven and nothing
       * else decides how many frames the title screen runs for.
       *
       * It answered `TitleKey` until M3-b-3d, when `RDKEY` stopped being a seam: the walk is
       * `Elite::ScanKeyboard`'s now and this is the one line of it that was ever the platform's.
       * `quiet` walks answer "nothing down"; the one after holds `key`, and `KY7` too when `fire`
       * is on so that the loop takes `BMI TL3` instead of `INC JSTK`. The oracle is driven by a
       * stub written over `RDKEY` that counts the same way.
       *
       * A WALK IS COUNTED AT ITS FIRST KEY. `ScanKeyboard` counts DOWN from the top of the logger,
       * so the highest index is where a scan begins and `scans` still counts scans.
       */
      static constexpr std::size_t WALK_START = std::tuple_size_v<Elite::KeyLogger> - 1u;

      std::uint32_t quiet = 0;
      std::uint8_t key = 0;
      bool fire = false;
      std::uint32_t scans = 0;

      [[nodiscard]] bool Held(std::size_t _key) override
      {
        if (_key == WALK_START)
        {
          ++scans;
        }
        if (scans <= quiet)
        {
          return false;
        }
        return (_key == key) || (fire && _key == Elite::KEY_FIRE);
      }

      [[nodiscard]] std::uint8_t NextKey() override { return 0; }
      void Flush() override {}

      void WaitFrames(std::uint8_t) override {}

      std::uint8_t ShowTitleScreen(std::uint8_t, Elite::ShipType, std::uint8_t) override
      {
        return 0;
      }
    };

    /*
     * `StopDockingMusic` WAS COUNTED HERE AND IS NOT ANY MORE (M3-b-2b).
     *
     * `RES2` opens with `JSR stopbd` and the seam counted it, with the oracle trapped at `stopbd`
     * so that neither machine ran it. Both run it now, and on this path it does nothing on either:
     * no tune is playing, so `stopbd` falls to `stopat` and `stopat`'s first test returns. That is
     * an assertion about SILENCE rather than about a call, and `CompareMusic` below is where it is
     * made -- against the oracle's own `MUPLA` and its own SID writes, which are also none.
     */
    /*
     * `RecordingLaunch` WAS HERE AND IS NOT ANY MORE (M4-a-1). It was `SFS1` answering "there was
     * room" for a launch that never spawns a child, so it was one seam this fixture had to declare
     * because `Ports` carried it.
     */

    struct RecordingOutside final : Elite::ShipDrawEffects
    {
      void DrawPlanetOrSun() override {}
      void DrawExplosion() override {}
    };

    /// Everything the launch works on, and the oracle's memory beside it.
    struct Leaving
    {
      Universe universe; ///< every byte of it, since M3-a
      RecordingOutside outside;
      RecordingStart start;

      // 6502: LSO -- `NWSPS` hands the station the sun's heap, and this launch creates one, so the
      // arena has to be lent that window or the station's lines go nowhere (§6.112).
      Leaving()
      {
        universe.LendSunHeap();
      }

      /// The seams a launch reaches: the AI and the drawing, the sounds, and `RESET`'s own.
      [[nodiscard]] Elite::Ports Ports() noexcept
      {
        return universe.PortsWith(outside, start, start, start);
      }
    };

    /// The bytes `RES2`, `RESET` and `TT110` write that the shared `Where` does not name.
    struct LaunchWhere
    {
      std::uint16_t nostm, lsx2, lsy2, mstg, jstx, jsty, alp2Next, bet2, bet2Next;
      std::uint16_t col2, dontclip, yx2m1, slsp, bomb, qq12, qq22, hfx, autoByte;
      std::uint16_t inwk, fist, stp, res2, reset, tt110, noise;

      explicit LaunchWhere(const OracleImage& _oracle)
      {
        nostm = _oracle.Label("NOSTM");
        lsx2 = _oracle.Label("LSX2");
        lsy2 = _oracle.Label("LSY2");
        mstg = _oracle.Label("MSTG");
        jstx = _oracle.Label("JSTX");
        jsty = _oracle.Label("JSTY");
        alp2Next = static_cast<std::uint16_t>(_oracle.Label("ALP2") + 1u);
        bet2 = _oracle.Label("BET2");
        bet2Next = static_cast<std::uint16_t>(_oracle.Label("BET2") + 1u);
        col2 = _oracle.Label("COL2");
        dontclip = _oracle.Label("dontclip");
        yx2m1 = _oracle.Label("Yx2M1");
        slsp = _oracle.Label("SLSP");
        bomb = _oracle.Label("BOMB");
        qq12 = _oracle.Label("QQ12");
        qq22 = _oracle.Label("QQ22");
        hfx = _oracle.Label("HFX");
        autoByte = _oracle.Label("auto");
        inwk = _oracle.Label("INWK");
        fist = _oracle.Label("FIST");
        stp = _oracle.Label("STP");
        res2 = _oracle.Label("RES2");
        reset = _oracle.Label("RESET");
        tt110 = _oracle.Label("TT110");
        noise = _oracle.Label("NOISE");
      }
    };

    /// A universe with something in every byte the reset is supposed to clear.
    void Occupy(Leaving& _leaving, std::uint32_t _seed)
    {
      Universe& universe = _leaving.universe;
      Seed(universe, _seed);

      universe.commander.fuel = universe.fuel;
      universe.message.token = 101u;
      universe.message.column = 9u;
      universe.message.append = 1u;
      universe.message.delay = 12u;
      universe.flight.blueprint = Elite::BlueprintOf(Elite::ShipType::CobraMk3);

      universe.bubble.heapBottom = Elite::HeapOffset::FromAddress(static_cast<std::uint16_t>(Elite::SHIP_HEAP_TOP - 64u));
      universe.heaps.yx2M1 = 199u;
      universe.heaps.lsp = 0x20u;
      universe.status.ecmCountdown = 20u;
      universe.status.ecmOurs = 0xFFu;
      universe.status.hyperspaceCounter = 5u;
      universe.status.hyperspaceCountdown = 9u;
      universe.screen.hyperspaceEffect = 0xFFu;
      universe.trumbles.count = 0x5Au;
      universe.spaceView = 2u;
      universe.explosions = 0x66u;

      _leaving.universe.control.roll = 200u;
      _leaving.universe.control.pitch = 40u;
      _leaving.universe.control.dockingComputer = 0xFFu;
      _leaving.universe.clip.dontclip = 0x80u;
      universe.heaps.stp = 4u; // what the short-range chart's fuel circle leaves behind
    }

    /// Send everything `Mirror` does not, and everything the launch reads.
    void MirrorLeaving(const Leaving& _leaving, Cpu6502& _cpu, const Where& _at, const LaunchWhere& _to)
    {
      const Universe& universe = _leaving.universe;

      _cpu.memory[_to.nostm] = universe.dust.count;
      _cpu.memory[_to.mstg] = universe.bubble.missileTarget;
      _cpu.memory[_to.jstx] = _leaving.universe.control.roll;
      _cpu.memory[_to.jsty] = _leaving.universe.control.pitch;
      _cpu.memory[_to.autoByte] = _leaving.universe.control.dockingComputer;
      _cpu.memory[_to.alp2Next] = universe.flight.alp2Next;
      _cpu.memory[_to.bet2] = universe.flight.bet2;
      _cpu.memory[_to.bet2Next] = universe.flight.bet2Next;
      _cpu.memory[_to.col2] = universe.text.cellColour;
      _cpu.memory[_to.dontclip] = _leaving.universe.clip.dontclip;
      _cpu.memory[_to.yx2m1] = universe.heaps.yx2M1;
      _cpu.memory[_to.qq22] = universe.status.hyperspaceCounter;
      _cpu.memory[_to.hfx] = universe.screen.hyperspaceEffect;
      _cpu.memory[_to.qq12] = universe.dockedFlag;

      /*
       * 6502: STP -- and `TT110` does not set it either (§6.94, §6.95).
       *
       * `HFS1` needs a step to advance `CNT` with, `RES2` does not provide one, and the only
       * writer in the whole game is `CIRCLE` -- which the docked screens reach exactly once, in
       * the short-range chart's fuel radius. A four is what the chart leaves.
       */
      _cpu.memory[_to.stp] = universe.heaps.stp;

      _cpu.memory[_to.slsp] = static_cast<std::uint8_t>(universe.bubble.heapBottom.Address() & 0xFFu);
      _cpu.memory[static_cast<std::uint16_t>(_to.slsp + 1u)] = static_cast<std::uint8_t>(universe.bubble.heapBottom.Address() >> 8);

      for (std::size_t index = 0; index < Elite::BALL_HEAP_SIZE * 2u; ++index)
      {
        _cpu.memory[static_cast<std::uint16_t>(_to.lsx2 + index)] = universe.heaps.ball[index];
      }
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        _cpu.memory[static_cast<std::uint16_t>(_to.inwk + byte)] = universe.work.ToBytes()[byte];
      }
    }

    /// Compare the same.
    void CompareLeaving(const Cpu6502& _cpu, const Leaving& _leaving, const LaunchWhere& _to, const std::wstring& _context)
    {
      const Universe& universe = _leaving.universe;

      auto same = [&](std::uint16_t _address, std::uint8_t _ours, const std::wstring& _name)
      { Assert::AreEqual(_cpu.memory[_address], _ours, (_context + L": " + _name).c_str()); };

      same(_to.nostm, universe.dust.count, L"NOSTM");
      same(_to.mstg, universe.bubble.missileTarget, L"MSTG");
      same(_to.jstx, _leaving.universe.control.roll, L"JSTX");
      same(_to.jsty, _leaving.universe.control.pitch, L"JSTY");
      same(_to.autoByte, _leaving.universe.control.dockingComputer, L"auto");
      same(_to.alp2Next, universe.flight.alp2Next, L"ALP2+1");
      same(_to.bet2, universe.flight.bet2, L"BET2");
      same(_to.bet2Next, universe.flight.bet2Next, L"BET2+1");
      same(_to.col2, universe.text.cellColour, L"COL2");
      same(_to.dontclip, _leaving.universe.clip.dontclip, L"dontclip");
      same(_to.yx2m1, universe.heaps.yx2M1, L"Yx2M1");
      same(_to.qq22, universe.status.hyperspaceCounter, L"QQ22");
      same(_to.hfx, universe.screen.hyperspaceEffect, L"HFX");
      same(_to.qq12, universe.dockedFlag, L"QQ12");
      same(_to.bomb, universe.commander.energyBomb, L"BOMB");
      same(_to.fist, universe.commander.legalStatus, L"FIST");

      const std::uint16_t bottom =
        static_cast<std::uint16_t>(_cpu.memory[_to.slsp] | (_cpu.memory[static_cast<std::uint16_t>(_to.slsp + 1u)] << 8));
      Assert::AreEqual<std::uint32_t>(bottom, universe.bubble.heapBottom.Address(), (_context + L": SLSP").c_str());

      for (std::size_t index = 0; index < Elite::BALL_HEAP_SIZE * 2u; ++index)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_to.lsx2 + index)], universe.heaps.ball[index],
                         (_context + L": ball heap byte " + std::to_wstring(index)).c_str());
      }
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_to.inwk + byte)], universe.work.ToBytes()[byte],
                         (_context + L": INWK byte " + std::to_wstring(byte)).c_str());
      }
    }
  } // namespace

  TEST_CLASS(TheResets)
  {
  public:
    /*
     * 6502: RES2 -- fifty instructions, and it was a seam until this slice.
     *
     * Everything in it is compared: the stardust count, both halves of the ball heap, the missile
     * lock, the two rate bytes it re-centres and the four it zeroes, the text colour, the clip
     * extent, the heap pointer and the whole of `INWK` -- because it falls into `ZINF`. The bulb
     * and the E.C.M. are swept both ways round, because `SPBLB` is a TOGGLE and `ECMOF` is only
     * reached when the E.C.M. is running.
     */
    TEST_METHOD(TheShipResetMatchesRES2)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LaunchWhere to(oracle);

      std::uint32_t compared = 0;
      std::uint32_t bulbs = 0;
      std::uint32_t bombs = 0;

      for (std::uint8_t shape = 0; shape < 8u; ++shape)
      {
        Leaving leaving;
        Occupy(leaving, shape * 17u + 3u);

        leaving.universe.bubble.Count(Elite::ShipType::Station) = ((shape & 1u) != 0u) ? 1u : 0u;
        leaving.universe.status.ecmCountdown = ((shape & 2u) != 0u) ? 20u : 0u;
        leaving.universe.commander.energyBomb = ((shape & 4u) != 0u) ? 0xC0u : 0x40u;

        Cpu6502 cpu = oracle.Fresh();
        FillScreens(cpu, leaving.universe.canvas, at.screen, 0x1Du);
        leaving.universe.dockedFlag = 0xFFu; // 6502: QQ12
        Mirror(leaving.universe, cpu, at);
        MirrorLeaving(leaving, cpu, at, to);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(to.res2, 2'000'000);
        Assert::IsTrue(run.completed, L"RES2 returned");

        Elite::Ports ports = leaving.Ports();
        Elite::ResetShipAndBubble(leaving.universe, ports);

        const std::wstring where = WidenText("RES2 (shape " + std::to_string(shape) + ")");

        CompareScreens(cpu, at.screen, leaving.universe.canvas, 0x1Du, where);
        CompareState(cpu, leaving.universe, at, where);
        CompareLeaving(cpu, leaving, to, where);

        bulbs += ((shape & 1u) != 0u) ? 1u : 0u;
        bombs += ((shape & 4u) != 0u) ? 1u : 0u;
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(8u, compared, L"the whole sweep ran");
      Assert::IsTrue(bulbs > 0u, L"the station bulb was lit on some passes");
      Assert::IsTrue(bombs > 0u, L"and the energy bomb was burning on some");
    }

    /*
     * 6502: RESET -- and the 255 it fills the shields with is a loop counter that ran off the end.
     *
     * The shields and `QQ12` are compared together because they are the same byte: a port that
     * set "docked" to 1 and the banks to 255 separately would agree with the game on both and be
     * a different routine.
     */
    TEST_METHOD(TheGameResetMatchesRESET)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LaunchWhere to(oracle);

      for (std::uint8_t shape = 0; shape < 4u; ++shape)
      {
        Leaving leaving;
        Occupy(leaving, shape * 31u + 11u);

        leaving.universe.bubble.Count(Elite::ShipType::Station) = ((shape & 1u) != 0u) ? 1u : 0u;
        leaving.universe.status.ecmCountdown = ((shape & 2u) != 0u) ? 20u : 0u;

        Cpu6502 cpu = oracle.Fresh();
        FillScreens(cpu, leaving.universe.canvas, at.screen, 0x1Du);
        leaving.universe.dockedFlag = 0u; // 6502: QQ12
        Mirror(leaving.universe, cpu, at);
        MirrorLeaving(leaving, cpu, at, to);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(to.reset, 2'000'000);
        Assert::IsTrue(run.completed, L"RESET returned");

        Elite::Ports ports = leaving.Ports();

        leaving.universe.dockedFlag = 0; // 6502: QQ12 -- `RESET` sets it, so it starts clear
        Elite::ResetGame(leaving.universe, ports);

        const std::wstring where = WidenText("RESET (shape " + std::to_string(shape) + ")");

        CompareScreens(cpu, at.screen, leaving.universe.canvas, 0x1Du, where);
        CompareState(cpu, leaving.universe, at, where);
        CompareLeaving(cpu, leaving, to, where);

        Assert::AreEqual<std::uint8_t>(0xFFu, leaving.universe.dockedFlag, (where + L": QQ12 is the loop's leftover").c_str());
        Assert::AreEqual<std::uint8_t>(0xFFu, leaving.universe.status.forwardShield, (where + L": FSH").c_str());
        Assert::AreEqual<std::uint8_t>(0xFFu, leaving.universe.status.aftShield, (where + L": ASH").c_str());
        Assert::AreEqual<std::uint8_t>(0xFFu, leaving.universe.status.energy, (where + L": ENERGY").c_str());
      }
    }
  };

  TEST_CLASS(TheLaunch)
  {
  public:
    /*
     * 6502: TT110 -- both of its paths, and the refusal is the interesting one.
     *
     * `LDX QQ12 / BEQ NLUNCH` means the key works in flight and does nothing there but change the
     * view, so a port that launched whenever "1" was pressed would put a second planet in the
     * bubble every time. The tech level is swept because it is what `SOS1` turns into the
     * planet's type, and the hold because the fine is ORed into `FIST` on the way out.
     */
    TEST_METHOD(TheLaunchMatchesTT110)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LaunchWhere to(oracle);

      std::uint32_t launched = 0;
      std::uint32_t refused = 0;

      for (const std::uint8_t docked : {std::uint8_t{0}, std::uint8_t{0xFF}})
      {
        /*
         * 6502: tek -- and TEN is in the sweep on purpose, because `NWSPS` picks a Dodo at ten and
         * above. Without it the station's blueprint entry would be the Coriolis in every case and
         * the branch that overwrites it would never run (§6.94's lesson, applied to a comparison).
         */
        for (const std::uint8_t techLevel : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{7}, std::uint8_t{10}, std::uint8_t{14}})
        {
          for (const std::uint8_t contraband : {std::uint8_t{0}, std::uint8_t{5}})
          {
            Leaving leaving;
            Occupy(leaving, docked + techLevel * 7u + contraband);
            leaving.universe.current.techLevel = techLevel; // 6502: tek, which `Mirror` sends to the oracle
            leaving.universe.commander.cargoHold[3] = contraband;
            leaving.universe.commander.cargoHold[10] = contraband;
            leaving.universe.commander.legalStatus = 2u;
            leaving.universe.commander.energyBomb = 0x40u;
            leaving.universe.view = 1u;

            Cpu6502 cpu = oracle.Fresh();
            cpu.AddTrap(oracle.Label("DOVDU19"));

            /*
             * §6.108, and this is the second run to need it. `LAUN` calls `TT66`, `TT66` calls
             * `NOSPRITES`, and `NOSPRITES` stores into `VIC+&15` -- which is `XX21+&15` in the
             * oracle's flat memory, the high byte of ship type 11's blueprint. The launch creates
             * a planet and a station AFTER the tunnel, so an untrapped store here makes `NWSPS`
             * refuse and the two sides disagree about the bubble rather than about the drawing.
             */
            cpu.AddTrap(oracle.Label("NOSPRITES"));
            FillScreens(cpu, leaving.universe.canvas, at.screen, 0x1Du);
            Mirror(leaving.universe, cpu, at);
            leaving.universe.dockedFlag = docked; // 6502: QQ12 -- the scenario, on the byte `LAUN` reads
            MirrorLeaving(leaving, cpu, at, to);

            const Elite::Testing::RunResult run = cpu.CallSubroutine(to.tt110, 8'000'000);
            Assert::IsTrue(run.completed, L"TT110 returned");

            Elite::Ports ports = leaving.Ports();

            Elite::SystemSeeds selected{};
            Elite::Launch(leaving.universe, ports, leaving.universe.commander.systemX,
                          leaving.universe.commander.systemY, selected);

            const std::wstring where = WidenText("TT110 (" + std::string(docked != 0u ? "docked" : "in flight") + ", tek " +
                                                 std::to_string(techLevel) + ", contraband " + std::to_string(contraband) + ")");

            CompareScreens(cpu, at.screen, leaving.universe.canvas, 0x1Du, where);
            CompareState(cpu, leaving.universe, at, where);
            CompareLeaving(cpu, leaving, to, where);

            Assert::AreEqual<std::uint8_t>(0u, leaving.universe.dockedFlag, (where + L": QQ12 is cleared on both paths").c_str());

            launched += (docked != 0u) ? 1u : 0u;
            refused += (docked == 0u) ? 1u : 0u;
          }
        }
      }

      Assert::IsTrue(launched > 0u, L"some cases launched");
      Assert::IsTrue(refused > 0u, L"and some were refused");
    }
  };

  /*
   * A LAUNCH PACES BOTH OF ITS TUNNELS, and only a mutation could have asked for this test.
   *
   * `TT110` draws the effect twice: once inside `LAUN`, over the docked screen, and once through
   * `HFS1` after the bubble has been rebuilt. Every oracle comparison here passes a null pacing,
   * because the 6502 has no present and the two sides must agree on pixels rather than on time --
   * so replacing the FIRST call's argument with `nullptr` changed no pixel, failed no assertion,
   * and quietly put half of §6.109 back: the tunnel instantly, the rings paced. Nothing in the
   * suite could see it. This is what sees it.
   *
   * Sixty-eight is thirty-four twice, and thirty-four is what `ThePacingIsOneFramePerCircle`
   * derives from the doubling: five circles each from the rings starting at radius 8 and 9,
   * four from each of the six starting at 10 to 15.
   */
  TEST_CLASS(TheLaunchPacing)
  {
  public:
    TEST_METHOD(ALaunchPacesBothOfItsTunnels)
    {
      struct Counting final : Elite::Presenter
      {
        std::uint32_t circles = 0;
        void Present() override
        {
          ++circles;
        }
        void WaitFrames(std::uint8_t) override {}
        void HoldFlightFrame(std::uint8_t) override {}
        void HoldTitleFrame(std::uint8_t) override {}
      };

      Leaving leaving;
      Occupy(leaving, 0x4Du);
      leaving.universe.view = 1u;

      Counting counting;
      Elite::Ports ports = leaving.universe.PortsWith(leaving.outside, leaving.start, counting);

      leaving.universe.dockedFlag = 0xFFu; // 6502: QQ12 -- docked, so the launch is not the refusal path
      Elite::SystemSeeds selected{};
      Elite::Launch(leaving.universe, ports, leaving.universe.commander.systemX,
                    leaving.universe.commander.systemY, selected);

      Assert::AreEqual<std::uint32_t>(68u, counting.circles, L"both tunnels are paced, not just the second");

      // And the refusal path draws nothing at all, so it asks the platform for nothing either.
      Leaving flying;
      Occupy(flying, 0x4Du);

      Counting none;
      Elite::Ports flyingPorts = flying.universe.PortsWith(flying.outside, flying.start, none);

      flying.universe.dockedFlag = 0u; // 6502: LDX QQ12 / BEQ NLUNCH
      Elite::SystemSeeds ignored{};
      Elite::Launch(flying.universe, flyingPorts, flying.universe.commander.systemX,
                    flying.universe.commander.systemY, ignored);

      Assert::AreEqual<std::uint32_t>(0u, none.circles, L"pressing 1 in flight is a view change and draws no tunnel");
    }
  };

  /*
   * 6502: DEATH -- the death screen, compared against the shipped routine.
   *
   * The interesting half is the setup rather than the animation: `DET1` is a bare `RTS` on this
   * build, so the view `TT66` is called with is whatever `RES2` left in A (§6.117), and this is
   * where that byte is pinned. The rest is the wreckage -- five pieces, a random type each, and a
   * laser count that decides how long the flight loop runs afterwards.
   */
  TEST_CLASS(TheDeathScreen)
  {
  public:
    TEST_METHOD(TheDeathScreenSetsUpLikeDEATH)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t death = oracle.Label("DEATH");
      const std::uint16_t tt66 = oracle.Label("TT66");
      const std::uint16_t det1 = oracle.Label("DET1");
      const std::uint16_t lasct = oracle.Label("LASCT");
      const std::uint16_t delta = oracle.Label("DELTA");

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("EXNO3"));
      cpu.AddTrap(tt66);
      cpu.memory[delta] = 3u; // 6502: what `RES2` leaves, and what the two `ASL`s work on

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      cpu.pc = death;

      // Step only as far as the `JSR TT66`, which is all this comparison is about: past it the
      // routine runs the whole flight loop sixty-four times and never returns.
      bool reached = false;
      std::uint8_t viewByte = 0;
      for (int step = 0; step < 200'000; ++step)
      {
        if (!cpu.trapHits.empty() && cpu.trapHits.back().address == tt66)
        {
          viewByte = cpu.trapHits.back().a;
          reached = true;
          break;
        }
        if (!cpu.Step())
        {
          break;
        }
      }
      Assert::IsTrue(reached, L"DEATH should reach its JSR TT66");

      /*
       * §6.117: the upstream comment says `LDX #24 / JSR DET1` hides the dashboard "and sets A to
       * 6 in the process". Both halves are the BBC's. This asserts what THIS build does.
       */
      Assert::AreEqual<std::uint8_t>(Elite::DEATH_VIEW, viewByte, L"the view DEATH clears to");

      // And `DET1` really is one byte: a bare RTS, so the LDX before it goes nowhere.
      Assert::AreEqual<std::uint8_t>(0x60u, cpu.memory[det1], L"DET1 is a bare RTS on this build");

      // 6502: ASL DELTA / ASL DELTA -- a SHIFT LEFT twice, whatever the comment says.
      Assert::AreEqual<std::uint8_t>(12u, cpu.memory[delta], L"DELTA is multiplied by four, not divided");

      (void)lasct;
    }

    /*
     * The SCENE, compared on the whole bitmap against the shipped routine.
     *
     * The oracle is run to `U%`, which is where `DEATH`'s own two halves meet, and `M%` never
     * happens on either side. Everything above that line is compared: the cleared screen, the
     * border rubbed off with its own EOR, the two bytes `BOX` stores rather than EORs, the fresh
     * stardust, "GAME OVER" in the middle of it, and the five pieces of wreckage in `FRIN`.
     */
    TEST_METHOD(TheDeathSceneMatchesDEATH)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const std::uint16_t death = oracle.Label("DEATH");
      const std::uint16_t uPercent = oracle.Label("U%");

      Leaving leaving;
      Occupy(leaving, 0x2Fu);
      leaving.universe.view = 0u;
      leaving.universe.heaps.stp = 4u;

      Cpu6502 cpu = oracle.Fresh();
      FillScreens(cpu, leaving.universe.canvas, at.screen, 0x1Du);
      leaving.universe.dockedFlag = 0u; // 6502: QQ12
      Mirror(leaving.universe, cpu, at);
      MirrorLeaving(leaving, cpu, at, LaunchWhere(oracle));

      cpu.AddTrap(oracle.Label("EXNO3"), Cpu6502::TrapExit::SetCarry);
      cpu.AddTrap(oracle.Label("DOVDU19"));
      cpu.AddTrap(oracle.Label("NOSPRITES")); // §6.108, fourth time

      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      cpu.pc = death;

      bool reached = false;
      for (int step = 0; step < 40'000'000; ++step)
      {
        if (cpu.pc == uPercent)
        {
          reached = true;
          break;
        }
        if (!cpu.Step())
        {
          break;
        }
      }
      Assert::IsTrue(reached, L"DEATH should reach its JSR U%");

      Elite::Ports ports = leaving.Ports();

      Elite::PrepareDeathScene(leaving.universe, ports);

      const std::wstring where = L"DEATH (the scene)";

      CompareScreens(cpu, at.screen, leaving.universe.canvas, 0x1Du, where);

      // 6502: FRIN -- five pieces of wreckage, and the same types in the same slots.
      for (std::size_t slot = 0; slot < 8u; ++slot)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.frin + slot)], leaving.universe.bubble.slots[slot],
                         (where + L": FRIN " + std::to_wstring(slot)).c_str());
      }

      /*
       * THE BLOCKS, not just the types. `FRIN` says a canister went into slot 3; `K%` says where it
       * is, which way it points, how fast it goes and whether it arrived dead -- and every one of
       * those is a byte `Ze`, `fq1` or `.D1` computed. The first version of this test compared the
       * types alone, and eleven mutations in that arithmetic walked through it.
       */
      for (std::size_t slot = 0; slot < 5u; ++slot)
      {
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)],
                           leaving.universe.bubble.blocks[slot].ToBytes()[byte],
                           (where + L": K% slot " + std::to_wstring(slot) + L" byte " + std::to_wstring(byte)).c_str());
        }
      }

      // 6502: STY QQ11 with Y = 0, once per piece of wreckage -- the view the scene ends on.
      Assert::AreEqual(cpu.memory[at.qq11], leaving.universe.view, (where + L": QQ11").c_str());

      // 6502: JSR EXNO3 -- one explosion, and it is the one the shipped routine makes.
      std::uint32_t explosions = 0;
      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        explosions += (hit.address == oracle.Label("EXNO3")) ? 1u : 0u;
      }
      /*
       * 6502: EXNO3's `JMP NOISE` -- and the BUFFER carries it since M3-b-2a.
       *
       * The count came off a trap and the port's off a recorded list; the routine runs on both
       * sides now, so `SOFLG` says which voice took `sfxexpl` and at what priority. `EXNO3` is
       * still trapped, which is why the game's side is a count of ITS hits and not of `NOISE`'s.
       */
      Assert::IsTrue(explosions > 0u, (where + L": the shipped routine explodes").c_str());
      Assert::AreEqual<std::uint8_t>(static_cast<std::uint8_t>(0x80u | (static_cast<std::uint8_t>(Elite::SoundEffect::Explosion) + 1u)),
                                     leaving.universe.sound.flag[2], (where + L": sfxexpl").c_str());

      Assert::AreEqual(cpu.memory[oracle.Label("LASCT")], leaving.universe.status.laserCount, (where + L": LASCT").c_str());
      Assert::AreEqual(cpu.memory[oracle.Label("MCNT")], leaving.universe.flight.mainLoopCounter, (where + L": MCNT").c_str());
      Assert::AreEqual(cpu.memory[oracle.Label("DELTA")], leaving.universe.flight.delta, (where + L": DELTA").c_str());
    }

    /*
     * 6502: BOX -- the whole-screen border with its floor, compared against the shipped routine.
     *
     * On its own rather than only through `DEATH`, because `DEATH` zeroes the two bytes `BOX`
     * stores straight after drawing them -- so a `BOX` that forgot the corner would pass the death
     * scene and fail every other caller. There are no other callers yet; this is what says the
     * routine is right regardless.
     */
    TEST_METHOD(TheFullBorderMatchesBOX)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const std::uint16_t box = oracle.Label("BOX");

      Universe universe;
      Seed(universe, 0x19u);

      Cpu6502 cpu = oracle.Fresh();
      FillScreens(cpu, universe.canvas, at.screen, 0x00u);
      Mirror(universe, cpu, at);

      const Elite::Testing::RunResult run = cpu.CallSubroutine(box, 2'000'000);
      Assert::IsTrue(run.completed, L"BOX returned");

      Elite::DrawFullBorder(universe.canvas);

      const std::uint32_t touched = CompareScreens(cpu, at.screen, universe.canvas, 0x00u, L"BOX");
      Assert::IsTrue(touched > 0u, L"BOX: something was drawn");

      // 6502: LDA #&FF / STA SCBASE+&1F1F -- the corner byte the rule cannot reach, STORED not EORed.
      Assert::AreEqual<std::uint8_t>(0xFFu, universe.canvas.Read(Elite::BOTTOM_RIGHT_CORNER), L"BOX: the bottom right corner");
    }

    /*
     * 6502: U% -- fifty-seven bytes and not sixty-five, compared against the shipped routine.
     *
     * `LDY #56 / .DKL3 STA KLO,Y / DEY / BNE DKL3 / STA KL` walks down to one and stores at zero
     * separately, so 0 to 56 are cleared and 57 to 64 are left alone. `ZEKTRAN` clears all
     * sixty-five, which is why this is its own routine and not a call to that one.
     */
    TEST_METHOD(TheFlightKeysClearLikeUPercent)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t uPercent = oracle.Label("U%");
      const std::uint16_t keylook = oracle.Label("KEYLOOK");

      Elite::KeyLogger keys{};
      Cpu6502 cpu = oracle.Fresh();
      for (std::size_t index = 0; index < keys.size(); ++index)
      {
        keys[index] = static_cast<std::uint8_t>(0xA0u + index);
        cpu.memory[static_cast<std::uint16_t>(keylook + index)] = keys[index];
      }

      const Elite::Testing::RunResult run = cpu.CallSubroutine(uPercent, 10'000);
      Assert::IsTrue(run.completed, L"U% returned");

      Elite::ClearFlightKeys(keys);

      for (std::size_t index = 0; index < keys.size(); ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(keylook + index)], keys[index],
                         (L"KEYLOOK+" + std::to_wstring(index)).c_str());
      }
      Assert::AreEqual<std::uint8_t>(0xA0u, keys[0], L"KLO+0 is below the loop and is left alone");
      Assert::AreEqual<std::uint8_t>(0xA0u + 57u, keys[57], L"and byte 57 is the first one above it left alone");
    }

    /*
     * `Die` after the scene: the keys, the speed and the count, by what is left when it returns.
     *
     * Not an oracle comparison, and it says so: the 6502 runs `M%` sixty-four times over the
     * wreckage and never comes back, so the two sides can only be compared frame by frame through
     * the whole-frame fixture, which is a different suite's. What THIS pins is the five
     * instructions between the scene and the loop -- `JSR U%`, `STA DELTA`, and a count that must
     * reach zero -- because a mutation that dropped any of them passed the scene comparison.
     */
    TEST_METHOD(DyingClearsTheKeysAndStopsTheShip)
    {
      Leaving leaving;
      Occupy(leaving, 0x2Fu);
      leaving.universe.heaps.stp = 4u;
      for (std::size_t index = 0; index < leaving.universe.keys.size(); ++index)
      {
        leaving.universe.keys[index] = 0xFFu;
      }

      Elite::Ports ports = leaving.Ports();

      Elite::Die(leaving.universe, ports);

      Assert::AreEqual<std::uint8_t>(0xFFu, leaving.universe.keys[0], L"KLO+0 is below U%'s range and is untouched");
      for (std::size_t index = 1; index <= Elite::FLIGHT_KEYS_CLEARED; ++index)
      {
        Assert::AreEqual<std::uint8_t>(0u, leaving.universe.keys[index], (L"U% cleared KLO+" + std::to_wstring(index)).c_str());
      }
      Assert::AreEqual<std::uint8_t>(0xFFu, leaving.universe.keys[64], L"and the byte above U%'s range is untouched");
      Assert::AreEqual<std::uint8_t>(0u, leaving.universe.status.laserCount, L"LASCT counted down to zero");
      Assert::IsTrue(leaving.universe.flight.delta <= 1u, L"STA DELTA stopped the ship before the loop ran");
    }

    /*
     * The wreckage stays inside the space view, on every one of the sixty-five frames.
     *
     * A `FlightPort`, because the fault this pins needed every routine real. Half the wreckage is
     * spawned dead and `DOEXP` runs on it -- and until 2026-09-06 the cloud it grew had never been
     * seeded: byte 2 of the heap read as zero, the vertex copy ran from index 0 down through 255 to
     * 7, and two hundred and fifty bytes of `XX3` landed across every line heap above the dying
     * piece's. The pieces owning those heaps drew them as lines, past the bottom of the bitmap into
     * screen RAM -- the coloured blocks in the border -- and their own lines were never erased, so
     * the screen filled with wreckage that did not move (§6.157). `Leaving` could not have shown
     * it: its explosion is a counter.
     *
     * So three things, every frame: no piece that is not exploding has more on its heap than its
     * blueprint gave it; after the first frame, nothing in screen RAM outside the space view's
     * thirty-two columns changes; and the dashboard's block of screen RAM does not change at all.
     */
    TEST_METHOD(TheWreckageStaysInsideTheSpaceView)
    {
      auto port = std::make_unique<FlightPort>();
      port->universe.commander = Elite::DefaultCommander();
      Elite::ResetGame(port->universe, port->Ports()); // 6502: RESET

      Elite::SystemSeeds selected{};
      Elite::Launch(port->universe, port->Ports(), port->universe.commander.systemX, port->universe.commander.systemY,
                    selected); // 6502: TT110

      // Some way out from the station at speed, so `ASL DELTA` twice has something to work on.
      for (int step = 0; step < 60; ++step)
      {
        port->held.fill(0u);
        port->held[Elite::KEY_SPEED_UP] = 1u;
        Assert::IsTrue(port->Step() == Elite::LoopOutcome::Continued, L"flying");
      }

      /*
       * The checks RECORD their first failure and the test asserts it after `Die` returns: this
       * callback is reached from `noexcept` code, and an assertion that throws from inside it ends
       * the process rather than the test -- which is what the mutation harness saw when the cloud's
       * vertex count was zeroed (the run aborted with no summary, on both runners' shims).
       */
      struct Watching final : Elite::Presenter
      {
        FlightPort& port;
        std::vector<std::uint8_t> cells;
        std::uint32_t frames = 0;
        std::wstring failure; // the first frame that went wrong, and how

        explicit Watching(FlightPort& _port) noexcept
          : port(_port)
        {
        }

      void WaitFrames(std::uint8_t) override {}
      void Present() override {}
      void HoldTitleFrame(std::uint8_t) override {}
      void HoldFlightFrame(std::uint8_t) override
        {
          ++frames;
          if (!failure.empty())
          {
            return; // the first failure is the one worth reading
          }
          const std::wstring where = L"death frame " + std::to_wstring(frames);

          for (std::size_t slot = 0; slot < port.universe.bubble.slots.size(); ++slot)
          {
            const std::uint8_t type = port.universe.bubble.slots[slot];
            if (type == 0u)
            {
              break;
            }
            const Elite::Ship& piece = port.universe.bubble.blocks[slot];
            if (Elite::Has(piece.state, Elite::ShipStateBit::Exploding))
            {
              continue; // a cloud's byte 0 is its size, not a line count
            }
            const std::uint8_t allowed = Elite::BlueprintOf(Elite::TypeOf(type))->heapBytes;
            if (port.universe.heap.Read(piece.heap) > allowed)
            {
              failure = where + L": slot " + std::to_wstring(slot) + L" has more on its heap than its blueprint allows";
              return;
            }
          }

          const auto screen = port.universe.canvas.Screen();
          if (frames == 1u)
          {
            cells.assign(screen.begin() + Elite::Canvas::SCREEN_CELLS, screen.end());
            return;
          }

          for (std::size_t offset = 0; offset < cells.size(); ++offset)
          {
            const std::size_t at = Elite::Canvas::SCREEN_CELLS + offset;
            const bool firstBlock = at < Elite::Canvas::DASHBOARD_CELLS;
            const std::size_t column = offset % Elite::Canvas::CELL_COLUMNS;
            const bool spaceView = firstBlock &&
                                   offset < static_cast<std::size_t>(Elite::Canvas::CELL_COLUMNS * Elite::Canvas::CELL_ROWS) &&
                                   column >= 4u && column < 36u;
            if (spaceView)
            {
              continue;
            }
            if (cells[offset] != screen[at])
            {
              failure = where + L": screen RAM byte " + std::to_wstring(at) + L" outside the space view changed -- was " +
                        std::to_wstring(cells[offset]) + L", is " + std::to_wstring(screen[at]);
              return;
            }
          }
        }
      };

      Watching watching(*port);
      port->watching = &watching;
      Elite::Die(port->universe, port->Ports());

      Assert::IsTrue(watching.failure.empty(), watching.failure.c_str());
      Assert::AreEqual<std::uint32_t>(Elite::DEATH_FRAMES + 1u, watching.frames, L"every frame of the sequence was shown");
    }
  };

  /*
   * 6502: TITLE -- the title screen, its rotating ship, and the key that dismisses it.
   *
   * THE ORACLE'S `RDKEY` IS PATCHED RATHER THAN TRAPPED, because the loop is key-driven and a trap
   * gives one answer for ever: `ClearCarry` never leaves `TLL2` and `SetCarry` leaves it after one
   * frame, and one frame does not show a ship rotating. The eleven bytes below are a counted
   * `RDKEY` -- `DEC` a counter, `BNE` to `CLC / RTS`, otherwise `LDA #key / SEC / RTS` -- and the
   * counter lives inside the stub so that nothing else in the compared memory moves.
   */
  TEST_CLASS(TheTitleScreen)
  {
  public:
    TEST_METHOD(TheTitleMatchesTITLE)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LaunchWhere to(oracle);
      const std::uint16_t title = oracle.Label("TITLE");
      const std::uint16_t rdkey = oracle.Label("RDKEY");
      const std::uint16_t jstk = oracle.Label("JSTK");
      const std::uint16_t patg = oracle.Label("PATG");
      const std::uint16_t mulie = oracle.Label("MULIE");
      const std::uint16_t keylook = oracle.Label("KEYLOOK");

      std::uint32_t compared = 0;
      std::uint32_t dismissed = 0;
      std::uint32_t fired = 0;

      for (const Elite::ShipType shipType : {Elite::ShipType::CobraMk3, Elite::ShipType::Adder})
      {
        for (const std::uint8_t distance : {Elite::TITLE_COBRA_DISTANCE, Elite::TITLE_ADDER_DISTANCE})
        {
          for (const std::uint32_t frames : {std::uint32_t{1}, std::uint32_t{4}, std::uint32_t{30}})
          {
            for (const bool fire : {false, true})
            {
              for (const std::uint8_t authors : {std::uint8_t{0}, std::uint8_t{0xFF}})
              {
                Leaving leaving;
                Occupy(leaving, Elite::Byte(shipType) * 13u + distance + frames + (fire ? 1u : 0u) + authors);

                leaving.universe.options.authorNames = authors;
                leaving.start.quiet = frames - 1u;
                leaving.start.key = 0x27u; // 6502: thiskey -- "Y", which is what `BR1` tests for
                leaving.start.fire = fire;

                Cpu6502 cpu = oracle.Fresh();
                cpu.AddTrap(oracle.Label("DOVDU19"));

                /*
                 * `NOSPRITES` IS TRAPPED, AND NOT BECAUSE IT IS A SEAM (§6.108).
                 *
                 * `XX21` is at &D000, which on a C64 is also where the VIC-II registers are: the
                 * ship data lives in RAM UNDER the I/O area and the game banks between them. The
                 * interpreter has flat memory and cannot, so `NOSPRITES`'s `STA VIC+&15` lands on
                 * `XX21+21` -- the high byte of ship type 11's blueprint pointer -- and zeroes it.
                 * `NWSHP` then reads a zero entry and refuses the ship, which is how this test
                 * first failed: the shipped game drew no Cobra at all.
                 *
                 * The port writes those registers through `SightEffects` and touches no memory, so
                 * trapping the routine is what makes the two sides agree rather than a convenience.
                 */
                cpu.AddTrap(oracle.Label("NOSPRITES"));
                
                /*
                 * The counted `RDKEY`, written over the real one.
                 *
                 * IT HAS TO WRITE `KY7` ITSELF. `TITLE` calls `ZEKTRAN` before the loop, so a fire
                 * key set up by the fixture is cleared before the loop ever reads it -- the press
                 * has to arrive from inside the scan, which is where a real press would arrive.
                 *
                 *    0  CE lo hi   DEC counter
                 *    3  D0 09      BNE quiet
                 *    5  A9 ff      LDA #(fire ? &FF : 0)
                 *    7  8D lo hi   STA KY7
                 *   10  A9 kk      LDA #key
                 *   12  38         SEC
                 *   13  60         RTS
                 *   14  18  quiet: CLC
                 *   15  60         RTS
                 *   16  nn         counter
                 */
                const std::uint16_t counter = static_cast<std::uint16_t>(rdkey + 16u);
                const std::uint16_t ky7 = static_cast<std::uint16_t>(keylook + Elite::KEY_FIRE);
                const std::uint8_t stub[17] = {0xCEu,
                                               static_cast<std::uint8_t>(counter & 0xFFu),
                                               static_cast<std::uint8_t>(counter >> 8),
                                               0xD0u,
                                               0x09u,
                                               0xA9u,
                                               fire ? std::uint8_t{0xFFu} : std::uint8_t{0u},
                                               0x8Du,
                                               static_cast<std::uint8_t>(ky7 & 0xFFu),
                                               static_cast<std::uint8_t>(ky7 >> 8),
                                               0xA9u,
                                               leaving.start.key,
                                               0x38u,
                                               0x60u,
                                               0x18u,
                                               0x60u,
                                               static_cast<std::uint8_t>(frames)};
                cpu.Load(rdkey, stub, sizeof(stub));

                FillScreens(cpu, leaving.universe.canvas, at.screen, 0x1Du);
                leaving.universe.dockedFlag = 0xFFu; // 6502: QQ12
                Mirror(leaving.universe, cpu, at);
                MirrorLeaving(leaving, cpu, at, to);
                cpu.memory[patg] = authors;

                cpu.a = Elite::TITLE_START_TOKEN;
                cpu.x = Elite::Byte(shipType);
                cpu.y = distance;
                const Elite::Testing::RunResult run = cpu.CallSubroutine(title, 20'000'000);
                Assert::IsTrue(run.completed, L"TITLE returned");

                Elite::Ports ports = leaving.Ports();

                /*
                 * 6502: DT3 -- and BOTH SIDES RUN IT SINCE M3-b-4b.
                 *
                 * This asserted `codes.ran.empty()` until the seam went: a recorder counted the
                 * control codes `TITLE`'s three tokens reached and the test said there were none.
                 * The oracle has always RUN them, so what that assertion was really claiming is
                 * that the two screens agree -- which `CompareScreens` below says directly, and
                 * says for the right reason. §6.73's corollary: the seam was what the suite
                 * counted, and the count goes with it.
                 */
                leaving.universe.RunCodesThrough(ports);

                // 6502: QQ12 -- docked, which is where `TITLE` is reached from. It is the
                // universe's own byte since M3-a, where `TitleScreen` held a reference to it.
                leaving.universe.dockedFlag = 0xFFu;
                const std::uint8_t answer = Elite::ShowTitleShip(leaving.universe, ports, Elite::TITLE_START_TOKEN, shipType, distance);

                const std::wstring where =
                  WidenText("TITLE (ship " + std::to_string(Elite::Byte(shipType)) + ", distance " + std::to_string(distance) + ", " +
                            std::to_string(frames) + " frames, " + (fire ? "fire" : "key") + ", PATG " + std::to_string(authors) + ")");

                /*
                 * The block and the flags BEFORE the pixels, on purpose: a divergence in `INWK` and
                 * one in the bitmap have the same symptom through `CompareScreens` and completely
                 * different causes, and the cheaper assertion should be the one that fires.
                 */
                for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
                {
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(to.inwk + byte)], leaving.universe.work.ToBytes()[byte],
                                   (where + L": INWK+" + std::to_wstring(byte)).c_str());
                }

                Assert::AreEqual(cpu.a, answer, (where + L": thiskey").c_str());

                CompareState(cpu, leaving.universe, at, where);
                CompareLeaving(cpu, leaving, to, where);
                CompareScreens(cpu, at.screen, leaving.universe.canvas, 0x1Du, where);

                Assert::AreEqual(cpu.memory[jstk], leaving.universe.options.joystick, (where + L": JSTK").c_str());
                Assert::AreEqual(cpu.memory[mulie], leaving.universe.status.titleReset, (where + L": MULIE").c_str());

                dismissed += fire ? 0u : 1u;
                fired += fire ? 1u : 0u;
                ++compared;
              }
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(48u, compared, L"the whole sweep ran");
      Assert::IsTrue(dismissed > 0u && fired > 0u, L"both ways out of the loop were taken");

      Logger::WriteMessage(
        ("TITLE: " + std::to_string(compared) + " title screens compared, " + std::to_string(fired) + " dismissed with fire\n").c_str());
    }
  };

} // namespace GameLogicTests
