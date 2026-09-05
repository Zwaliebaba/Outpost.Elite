#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "Commander.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "Market.h"
#include "PlanetDraw.h"
#include "ShipDraw.h"

#include <cstdint>
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

            Elite::CommanderBlock commander;
            const std::size_t hold = static_cast<std::size_t>(Elite::Field::CargoHold);
            commander.bytes[hold + 3u] = slaves;
            commander.bytes[hold + 6u] = narcotics;
            commander.bytes[hold + 10u] = firearms;

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
                           (std::wstring(L"HFS1 returned -- illegal ") + std::to_wstring(run.illegalOpcode) + L", instructions " +
                            std::to_wstring(run.instructions) + L", stoppedAt " + std::to_wstring(run.stoppedAt))
                             .c_str());

            Elite::DrawHyperspaceRings(world.canvas, world.heaps, draw, geometry, math, clip, nullptr);
          }

          const std::wstring where = WidenText("HFS1 (STP " + std::to_string(step) + ", " + std::to_string(pass + 1u) + " pass(es))");

          /*
       * One pass draws; TWO PASSES ERASE. Everything the effect touches is EORed, and the heap is
       * rewound to `LSP = 1` before every circle, so running it again puts the screen back byte
       * for byte -- which is how the game takes the rings off without remembering where they were.
       * The count is asserted both ways because "drew something" alone would pass for a routine
       * that drew and never cleaned up.
       */
          const std::uint32_t touched = CompareScreens(cpu, at.screen, world.canvas, 0x1Du, where);

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
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.lsx2 + index)], world.heaps.ball[index],
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
        World world;
        Seed(world, 0x71u);
        world.view = view;
        world.heaps.yx2M1 = 143u;
        world.heaps.lsp = 0u;

        /*
         * `STP` starts at something the launch must overwrite. Four is the value §6.95 had the app
         * seeding precisely because nothing on this path wrote one -- and `LAUN` is what writes it,
         * so the seed stops being load-bearing the moment this routine exists.
         */
        world.heaps.stp = 4u;

        Elite::ClipState clip;

        Cpu6502 cpu = oracle.Fresh();
        FillScreens(cpu, world.canvas, at.screen, 0x1Du);
        Mirror(world, cpu, at);
        cpu.memory[oracle.Label("Yx2M1")] = 143u;
        cpu.memory[oracle.Label("dontclip")] = 0u;
        cpu.memory[stp] = 4u;

        // The three the platform owns, plus §6.108's: `TT66` reaches `NOSPRITES`, and `NOSPRITES`
        // writes VIC registers that are the ship blueprint table in the oracle's flat memory.
        cpu.AddTrap(noise, Cpu6502::TrapExit::SetCarry);
        cpu.AddTrap(oracle.Label("SETL1"));
        cpu.AddTrap(oracle.Label("DOVDU19"));
        cpu.AddTrap(oracle.Label("NOSPRITES"));

        const Elite::Testing::RunResult run = cpu.CallSubroutine(laun, 40'000'000);
        Assert::IsTrue(run.completed, (std::wstring(L"LAUN returned -- illegal ") + std::to_wstring(run.illegalOpcode) + L", stoppedAt " +
                                       std::to_wstring(run.stoppedAt))
                                        .c_str());

        Elite::FlightScreen screen = world.Screen();
        Elite::DrawLaunchTunnel(screen, clip, nullptr);

        const std::wstring where = WidenText("LAUN (QQ11 " + std::to_string(view) + ")");

        // 6502: LDY #sfxwhosh / JSR NOISE -- effect 4, once, and its carry is dropped.
        std::uint32_t whooshes = 0;
        for (const Cpu6502::TrapHit& hit : cpu.trapHits)
        {
          whooshes += (hit.address == noise) ? 1u : 0u;
        }
        Assert::AreEqual<std::uint32_t>(1u, whooshes, (where + L": the shipped routine makes one noise").c_str());
        Assert::AreEqual<std::size_t>(1u, world.effects.sounds.size(), (where + L": and so does the port").c_str());
        Assert::AreEqual<std::uint8_t>(Elite::SOUND_MISSILE, world.effects.sounds.front(), (where + L": sfxwhosh").c_str());

        // 6502: LDA #8 / STA STP -- the step, which is the whole of §6.94's missing writer.
        Assert::AreEqual<std::uint8_t>(Elite::LAUNCH_TUNNEL_STEP, cpu.memory[stp], (where + L": the shipped STP").c_str());
        Assert::AreEqual(cpu.memory[stp], world.heaps.stp, (where + L": STP").c_str());

        // 6502: LDA QQ11 / PHA / ... / PLA / STA QQ11 -- the view survives the TT66 inside.
        Assert::AreEqual(cpu.memory[at.qq11], world.view, (where + L": QQ11").c_str());
        Assert::AreEqual<std::uint8_t>(view, world.view, (where + L": and it is the one that went in").c_str());

        Assert::IsTrue(CompareScreens(cpu, at.screen, world.canvas, 0x1Du, where) > 0u, (where + L": something was drawn").c_str());
        CompareState(cpu, world, at, where);
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
    TEST_METHOD(ThePacingIsOneFramePerCircle)
    {
      struct Counting final : Elite::TunnelEffects
      {
        std::uint32_t circles = 0;
        void ShowCircle() override
        {
          ++circles;
        }
      };

      for (const std::uint8_t step : {std::uint8_t{2}, std::uint8_t{4}, std::uint8_t{8}})
      {
        World world;
        Seed(world, 0x71u);
        world.heaps.yx2M1 = 143u;
        world.heaps.lsp = 0u;
        world.heaps.stp = step;

        Elite::ClipState clip;
        Counting counting;
        Elite::DrawHyperspaceRings(world.canvas, world.heaps, world.draw, world.geometry, world.math, clip, &counting);

        Assert::AreEqual<std::uint32_t>(34u, counting.circles, (WidenText("STP " + std::to_string(step)) + L": circles shown").c_str());
      }

      // And a null pacing is the same drawing with nobody watching, which is what the oracle
      // comparisons pass: the count above must not be reachable through a screen difference.
      World unpaced;
      Seed(unpaced, 0x71u);
      unpaced.heaps.yx2M1 = 143u;
      unpaced.heaps.lsp = 0u;
      unpaced.heaps.stp = 8u;

      World paced;
      Seed(paced, 0x71u);
      paced.heaps.yx2M1 = 143u;
      paced.heaps.lsp = 0u;
      paced.heaps.stp = 8u;

      Elite::ClipState clipA;
      Elite::ClipState clipB;
      Counting counting;
      Elite::DrawHyperspaceRings(unpaced.canvas, unpaced.heaps, unpaced.draw, unpaced.geometry, unpaced.math, clipA, nullptr);
      Elite::DrawHyperspaceRings(paced.canvas, paced.heaps, paced.draw, paced.geometry, paced.math, clipB, &counting);

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
    struct RecordingStart final : Elite::StartUpEffects
    {
      void ResetUniverse() override {}
      void ResetShip() override {}
      void ClearKeyLogger() override {}
      void StartTheme() override {}
      void StopTheme() override {}
      void ResetMissileIndicators() override {}
      /*
       * 6502: JSR RDKEY inside `TLL2` -- scripted, because the loop it drives is key-driven and
       * nothing else decides how many frames the title screen runs for.
       *
       * `quiet` passes answer "no key"; the one after it answers `key`, and sets `KY7` first when
       * `fire` is on so that the loop takes `BMI TL3` instead of `INC JSTK`. The oracle is driven
       * by a stub written over `RDKEY` that counts the same way.
       */
      std::uint32_t quiet = 0;
      std::uint8_t key = 0;
      bool fire = false;
      std::uint32_t scans = 0;

      [[nodiscard]] Elite::TitleKey ScanTitleKeys(Elite::KeyLogger& _keys) override
      {
        ++scans;
        if (scans <= quiet)
        {
          return {};
        }
        if (fire)
        {
          _keys[Elite::KEY_FIRE] = 0xFFu;
        }
        return {true, key};
      }

      void WaitFrames(std::uint8_t) override {}

      std::uint8_t ShowTitleScreen(std::uint8_t, std::uint8_t, std::uint8_t) override
      {
        return 0;
      }
    };

    struct RecordingLaunch final : Elite::FlightLoopEffects
    {
      std::vector<std::uint8_t> sounds;
      std::uint32_t musicStops = 0;

      bool PlaySound(std::uint8_t _effect) override
      {
        sounds.push_back(_effect);
        return true;
      }
      bool PlaySoundPitched(std::uint8_t _effect, std::uint8_t, std::uint8_t) override
      {
        sounds.push_back(_effect);
        return true;
      }
      void StopSound(std::uint8_t) override {}
      void MoveTrumbles() override {}
      void StartDockingMusic() override {}
      void StopDockingMusic() override
      {
        ++musicStops;
      }
      bool SpawnAhead(std::uint8_t) override
      {
        return true;
      }
      void Anger(std::uint8_t) override {}
      bool SpawnChild(std::uint8_t, std::uint8_t) override
      {
        return true;
      }
    };

    struct RecordingOutside final : Elite::ShipEffects, Elite::ShipDrawEffects
    {
      void RunTactics(Elite::ShipBlock&) override {}
      void DrawPlanetOrSun() override {}
      void DrawExplosion() override {}
      void SeedExplosionCloud(Elite::LineHeap&, std::uint16_t, std::uint16_t) override {}
    };

    /// Everything the launch works on, and the oracle's memory beside it.
    struct Leaving
    {
      World world;
      Elite::ControlState control;
      Elite::ControlOptions options;
      Elite::KeyLogger keys{};
      Elite::LaserBurst burst{};
      Elite::LineHeap heap;
      Elite::ClipState clip;
      Elite::Projection projection;
      Elite::CompassAxes axes{};
      RecordingOutside outside;
      RecordingLaunch effects;
      RecordingStart start;
    };

    /// The bytes `RES2`, `RESET` and `TT110` write that the shared `Where` does not name.
    struct LaunchWhere
    {
      std::uint16_t nostm, lsx2, lsy2, mstg, jstx, jsty, alp2Next, bet2, bet2Next;
      std::uint16_t col2, dontclip, yx2m1, slsp, bomb, qq12, qq22, hfx, autoByte;
      std::uint16_t inwk, fist, stp, res2, reset, tt110, stopbd, noise;

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
        stopbd = _oracle.Label("stopbd");
        noise = _oracle.Label("NOISE");
      }
    };

    /// A world with something in every byte the reset is supposed to clear.
    void Occupy(Leaving& _leaving, std::uint32_t _seed)
    {
      World& world = _leaving.world;
      Seed(world, _seed);

      world.commander.At(Elite::Field::Fuel) = world.fuel;
      world.message.token = 101u;
      world.message.column = 9u;
      world.message.append = 1u;
      world.message.delay = 12u;
      world.flight.blueprint = Elite::BlueprintAddress(11u);

      world.bubble.heapBottom = static_cast<std::uint16_t>(Elite::SHIP_HEAP_TOP - 64u);
      world.heaps.yx2M1 = 199u;
      world.heaps.lsp = 0x20u;
      world.status.ecmCountdown = 20u;
      world.status.ecmOurs = 0xFFu;
      world.status.hyperspaceCounter = 5u;
      world.status.hyperspaceCountdown = 9u;
      world.screen.hyperspaceEffect = 0xFFu;
      world.trumbles = 0x5Au;
      world.spaceView = 2u;
      world.explosions = 0x66u;

      _leaving.control.roll = 200u;
      _leaving.control.pitch = 40u;
      _leaving.control.dockingComputer = 0xFFu;
      _leaving.clip.dontclip = 0x80u;
      world.heaps.stp = 4u; // what the short-range chart's fuel circle leaves behind
    }

    /// Send everything `Mirror` does not, and everything the launch reads.
    void MirrorLeaving(const Leaving& _leaving, Cpu6502& _cpu, const Where& _at, const LaunchWhere& _to, std::uint8_t _docked)
    {
      const World& world = _leaving.world;

      _cpu.memory[_to.nostm] = world.dust.count;
      _cpu.memory[_to.mstg] = world.bubble.missileTarget;
      _cpu.memory[_to.jstx] = _leaving.control.roll;
      _cpu.memory[_to.jsty] = _leaving.control.pitch;
      _cpu.memory[_to.autoByte] = _leaving.control.dockingComputer;
      _cpu.memory[_to.alp2Next] = world.flight.alp2Next;
      _cpu.memory[_to.bet2] = world.flight.bet2;
      _cpu.memory[_to.bet2Next] = world.flight.bet2Next;
      _cpu.memory[_to.col2] = world.text.cellColour;
      _cpu.memory[_to.dontclip] = _leaving.clip.dontclip;
      _cpu.memory[_to.yx2m1] = world.heaps.yx2M1;
      _cpu.memory[_to.qq22] = world.status.hyperspaceCounter;
      _cpu.memory[_to.hfx] = world.screen.hyperspaceEffect;
      _cpu.memory[_to.qq12] = _docked;

      /*
       * 6502: STP -- and `TT110` does not set it either (§6.94, §6.95).
       *
       * `HFS1` needs a step to advance `CNT` with, `RES2` does not provide one, and the only
       * writer in the whole game is `CIRCLE` -- which the docked screens reach exactly once, in
       * the short-range chart's fuel radius. A four is what the chart leaves.
       */
      _cpu.memory[_to.stp] = world.heaps.stp;

      _cpu.memory[_to.slsp] = static_cast<std::uint8_t>(world.bubble.heapBottom & 0xFFu);
      _cpu.memory[static_cast<std::uint16_t>(_to.slsp + 1u)] = static_cast<std::uint8_t>(world.bubble.heapBottom >> 8);

      for (std::size_t index = 0; index < Elite::BALL_HEAP_SIZE * 2u; ++index)
      {
        _cpu.memory[static_cast<std::uint16_t>(_to.lsx2 + index)] = world.heaps.ball[index];
      }
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        _cpu.memory[static_cast<std::uint16_t>(_to.inwk + byte)] = world.work[byte];
      }
    }

    /// Compare the same.
    void CompareLeaving(const Cpu6502& _cpu, const Leaving& _leaving, const LaunchWhere& _to, std::uint8_t _docked,
                        const std::wstring& _context)
    {
      const World& world = _leaving.world;

      auto same = [&](std::uint16_t _address, std::uint8_t _ours, const std::wstring& _name)
      { Assert::AreEqual(_cpu.memory[_address], _ours, (_context + L": " + _name).c_str()); };

      same(_to.nostm, world.dust.count, L"NOSTM");
      same(_to.mstg, world.bubble.missileTarget, L"MSTG");
      same(_to.jstx, _leaving.control.roll, L"JSTX");
      same(_to.jsty, _leaving.control.pitch, L"JSTY");
      same(_to.autoByte, _leaving.control.dockingComputer, L"auto");
      same(_to.alp2Next, world.flight.alp2Next, L"ALP2+1");
      same(_to.bet2, world.flight.bet2, L"BET2");
      same(_to.bet2Next, world.flight.bet2Next, L"BET2+1");
      same(_to.col2, world.text.cellColour, L"COL2");
      same(_to.dontclip, _leaving.clip.dontclip, L"dontclip");
      same(_to.yx2m1, world.heaps.yx2M1, L"Yx2M1");
      same(_to.qq22, world.status.hyperspaceCounter, L"QQ22");
      same(_to.hfx, world.screen.hyperspaceEffect, L"HFX");
      same(_to.qq12, _docked, L"QQ12");
      same(_to.bomb, world.commander.At(Elite::Field::EnergyBomb), L"BOMB");
      same(_to.fist, world.commander.At(Elite::Field::LegalStatus), L"FIST");

      const std::uint16_t bottom =
        static_cast<std::uint16_t>(_cpu.memory[_to.slsp] | (_cpu.memory[static_cast<std::uint16_t>(_to.slsp + 1u)] << 8));
      Assert::AreEqual<std::uint32_t>(bottom, world.bubble.heapBottom, (_context + L": SLSP").c_str());

      for (std::size_t index = 0; index < Elite::BALL_HEAP_SIZE * 2u; ++index)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_to.lsx2 + index)], world.heaps.ball[index],
                         (_context + L": ball heap byte " + std::to_wstring(index)).c_str());
      }
      for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_to.inwk + byte)], world.work[byte],
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

        leaving.world.bubble.counts[Elite::SHIP_TYPE_STATION] = ((shape & 1u) != 0u) ? 1u : 0u;
        leaving.world.status.ecmCountdown = ((shape & 2u) != 0u) ? 20u : 0u;
        leaving.world.commander.At(Elite::Field::EnergyBomb) = ((shape & 4u) != 0u) ? 0xC0u : 0x40u;

        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(to.stopbd);
        cpu.AddTrap(to.noise, Cpu6502::TrapExit::SetCarry);
        FillScreens(cpu, leaving.world.canvas, at.screen, 0x1Du);
        Mirror(leaving.world, cpu, at);
        MirrorLeaving(leaving, cpu, at, to, 0xFFu);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(to.res2, 2'000'000);
        Assert::IsTrue(run.completed, L"RES2 returned");

        Elite::FlightScreen screen = leaving.world.Screen();
        Elite::FlightLoop loop{screen,       leaving.keys,       leaving.control, leaving.options, leaving.burst,   leaving.heap,
                               leaving.clip, leaving.projection, leaving.axes,    leaving.outside, leaving.outside, leaving.effects};
        Elite::ResetShipAndBubble(loop);

        const std::wstring where = WidenText("RES2 (shape " + std::to_string(shape) + ")");

        CompareScreens(cpu, at.screen, leaving.world.canvas, 0x1Du, where);
        CompareState(cpu, leaving.world, at, where);
        CompareLeaving(cpu, leaving, to, 0xFFu, where);

        Assert::AreEqual<std::uint32_t>(1u, leaving.effects.musicStops, (where + L": stopbd").c_str());

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

        leaving.world.bubble.counts[Elite::SHIP_TYPE_STATION] = ((shape & 1u) != 0u) ? 1u : 0u;
        leaving.world.status.ecmCountdown = ((shape & 2u) != 0u) ? 20u : 0u;

        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(to.stopbd);
        cpu.AddTrap(to.noise, Cpu6502::TrapExit::SetCarry);
        FillScreens(cpu, leaving.world.canvas, at.screen, 0x1Du);
        Mirror(leaving.world, cpu, at);
        MirrorLeaving(leaving, cpu, at, to, 0u);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(to.reset, 2'000'000);
        Assert::IsTrue(run.completed, L"RESET returned");

        Elite::FlightScreen screen = leaving.world.Screen();
        Elite::FlightLoop loop{screen,       leaving.keys,       leaving.control, leaving.options, leaving.burst,   leaving.heap,
                               leaving.clip, leaving.projection, leaving.axes,    leaving.outside, leaving.outside, leaving.effects};

        std::uint8_t docked = 0;
        Elite::ResetGame(loop, docked);

        const std::wstring where = WidenText("RESET (shape " + std::to_string(shape) + ")");

        CompareScreens(cpu, at.screen, leaving.world.canvas, 0x1Du, where);
        CompareState(cpu, leaving.world, at, where);
        CompareLeaving(cpu, leaving, to, docked, where);

        Assert::AreEqual<std::uint8_t>(0xFFu, docked, (where + L": QQ12 is the loop's leftover").c_str());
        Assert::AreEqual<std::uint8_t>(0xFFu, leaving.world.status.forwardShield, (where + L": FSH").c_str());
        Assert::AreEqual<std::uint8_t>(0xFFu, leaving.world.status.aftShield, (where + L": ASH").c_str());
        Assert::AreEqual<std::uint8_t>(0xFFu, leaving.world.status.energy, (where + L": ENERGY").c_str());
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
            leaving.world.techLevel = techLevel; // 6502: tek, which `Mirror` sends to the oracle

            const std::size_t hold = static_cast<std::size_t>(Elite::Field::CargoHold);
            leaving.world.commander.bytes[hold + 3u] = contraband;
            leaving.world.commander.bytes[hold + 10u] = contraband;
            leaving.world.commander.At(Elite::Field::LegalStatus) = 2u;
            leaving.world.commander.At(Elite::Field::EnergyBomb) = 0x40u;
            leaving.world.view = 1u;

            Cpu6502 cpu = oracle.Fresh();
            cpu.AddTrap(to.stopbd);
            cpu.AddTrap(to.noise, Cpu6502::TrapExit::SetCarry);
            cpu.AddTrap(oracle.Label("SETL1"));
            cpu.AddTrap(oracle.Label("DOVDU19"));

            /*
             * §6.108, and this is the second run to need it. `LAUN` calls `TT66`, `TT66` calls
             * `NOSPRITES`, and `NOSPRITES` stores into `VIC+&15` -- which is `XX21+&15` in the
             * oracle's flat memory, the high byte of ship type 11's blueprint. The launch creates
             * a planet and a station AFTER the tunnel, so an untrapped store here makes `NWSPS`
             * refuse and the two sides disagree about the bubble rather than about the drawing.
             */
            cpu.AddTrap(oracle.Label("NOSPRITES"));
            FillScreens(cpu, leaving.world.canvas, at.screen, 0x1Du);
            Mirror(leaving.world, cpu, at);
            MirrorLeaving(leaving, cpu, at, to, docked);

            const Elite::Testing::RunResult run = cpu.CallSubroutine(to.tt110, 8'000'000);
            Assert::IsTrue(run.completed, L"TT110 returned");

            Elite::FlightScreen screen = leaving.world.Screen();
            Elite::FlightLoop loop{screen,       leaving.keys,       leaving.control, leaving.options, leaving.burst,   leaving.heap,
                                   leaving.clip, leaving.projection, leaving.axes,    leaving.outside, leaving.outside, leaving.effects};

            std::uint8_t flag = docked;
            Elite::SystemSeeds selected{};
            Elite::Launch(loop, nullptr, flag, leaving.world.commander.At(Elite::Field::SystemX),
                          leaving.world.commander.At(Elite::Field::SystemY), techLevel, selected);

            const std::wstring where = WidenText("TT110 (" + std::string(docked != 0u ? "docked" : "in flight") + ", tek " +
                                                 std::to_string(techLevel) + ", contraband " + std::to_string(contraband) + ")");

            CompareScreens(cpu, at.screen, leaving.world.canvas, 0x1Du, where);
            CompareState(cpu, leaving.world, at, where);
            CompareLeaving(cpu, leaving, to, flag, where);

            Assert::AreEqual<std::uint8_t>(0u, flag, (where + L": QQ12 is cleared on both paths").c_str());

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

      for (const std::uint8_t shipType : {Elite::SHIP_COBRA_MK3, Elite::SHIP_ADDER})
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
                Occupy(leaving, shipType * 13u + distance + frames + (fire ? 1u : 0u) + authors);

                leaving.options.authorNames = authors;
                leaving.start.quiet = frames - 1u;
                leaving.start.key = 0x27u; // 6502: thiskey -- "Y", which is what `BR1` tests for
                leaving.start.fire = fire;

                Cpu6502 cpu = oracle.Fresh();
                cpu.AddTrap(oracle.Label("SETL1"));
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
                cpu.AddTrap(to.stopbd);
                cpu.AddTrap(to.noise, Cpu6502::TrapExit::SetCarry);

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

                FillScreens(cpu, leaving.world.canvas, at.screen, 0x1Du);
                Mirror(leaving.world, cpu, at);
                MirrorLeaving(leaving, cpu, at, to, 0xFFu);
                cpu.memory[patg] = authors;

                cpu.a = Elite::TITLE_START_TOKEN;
                cpu.x = shipType;
                cpu.y = distance;
                const Elite::Testing::RunResult run = cpu.CallSubroutine(title, 20'000'000);
                Assert::IsTrue(run.completed, L"TITLE returned");

                Elite::FlightScreen screen = leaving.world.Screen();
                Elite::FlightLoop loop{screen,        leaving.keys,    leaving.control, leaving.options,
                                       leaving.burst, leaving.heap,    leaving.clip,    leaving.projection,
                                       leaving.axes,  leaving.outside, leaving.outside, leaving.effects};

                std::uint8_t flag = 0xFFu;
                Elite::TitleScreen titleScreen{loop, leaving.start, leaving.world.extendedPrinter, leaving.options, leaving.keys, flag};
                const std::uint8_t answer = Elite::ShowTitleShip(titleScreen, Elite::TITLE_START_TOKEN, shipType, distance);

                const std::wstring where =
                  WidenText("TITLE (ship " + std::to_string(shipType) + ", distance " + std::to_string(distance) + ", " +
                            std::to_string(frames) + " frames, " + (fire ? "fire" : "key") + ", PATG " + std::to_string(authors) + ")");

                /*
                 * The block and the flags BEFORE the pixels, on purpose: a divergence in `INWK` and
                 * one in the bitmap have the same symptom through `CompareScreens` and completely
                 * different causes, and the cheaper assertion should be the one that fires.
                 */
                for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
                {
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(to.inwk + byte)], leaving.world.work[byte],
                                   (where + L": INWK+" + std::to_wstring(byte)).c_str());
                }

                Assert::AreEqual(cpu.a, answer, (where + L": thiskey").c_str());
                Assert::IsTrue(leaving.world.codes.ran.empty(), (where + L": no token reached a control code").c_str());

                CompareState(cpu, leaving.world, at, where);
                CompareLeaving(cpu, leaving, to, flag, where);
                CompareScreens(cpu, at.screen, leaving.world.canvas, 0x1Du, where);

                Assert::AreEqual(cpu.memory[jstk], leaving.options.joystick, (where + L": JSTK").c_str());
                Assert::AreEqual(cpu.memory[mulie], leaving.world.status.titleReset, (where + L": MULIE").c_str());

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
