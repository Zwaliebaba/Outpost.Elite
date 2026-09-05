#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "Charts.h"
#include "Flight.h"
#include "Commander.h"
#include "Hyperspace.h"
#include "Market.h"
#include "FlightLoop.h"
#include "Universe.h"

#include <array>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

namespace GameLogicTests
{

  /*
   * Slice 4c-b: the jump, witchspace, and the galactic hyperdrive.
   *
   * `hyp` decided WHETHER to jump back in slice 2d and left `JumpOutcome::Galactic` for something
   * that did not exist; `Main.cpp` has listed hyperspace among the actions it refuses BY NAME since
   * phase 3, so that a port which built it would get a compiler error here rather than a silent
   * omission. These are the routines that answer both.
   */
  TEST_CLASS(TheJump)
  {
    struct Labels
    {
      std::uint16_t hyp1 = 0, ghy = 0, zz = 0;
      std::uint16_t qq2 = 0, qq3 = 0, qq4 = 0, qq5 = 0, qq28 = 0, tek = 0, gov = 0;
      std::uint16_t qq0 = 0, qq1 = 0, qq9 = 0, qq10 = 0, qq15 = 0, qq21 = 0;
      std::uint16_t safehouse = 0, qq8 = 0, qq22 = 0, qq26 = 0, avl = 0;
      std::uint16_t ev = 0, rand = 0, gcnt = 0, ghyp = 0, fist = 0, cok = 0, qq14 = 0, patg = 0, qq11 = 0;

      explicit Labels(const OracleImage& _oracle)
      {
        hyp1 = _oracle.Label("hyp1");
        ghy = _oracle.Label("Ghy");
        zz = _oracle.Label("zZ");
        qq2 = _oracle.Label("QQ2");
        qq3 = _oracle.Label("QQ3");
        qq4 = _oracle.Label("QQ4");
        qq5 = _oracle.Label("QQ5");
        qq28 = _oracle.Label("QQ28");
        tek = _oracle.Label("tek");
        gov = _oracle.Label("gov");
        qq0 = _oracle.Label("QQ0");
        qq1 = _oracle.Label("QQ1");
        qq9 = _oracle.Label("QQ9");
        qq10 = _oracle.Label("QQ10");
        qq15 = _oracle.Label("QQ15");
        qq21 = _oracle.Label("QQ21");
        safehouse = _oracle.Label("safehouse");
        qq8 = _oracle.Label("QQ8");
        qq22 = _oracle.Label("QQ22");
        qq26 = _oracle.Label("QQ26");
        avl = _oracle.Label("AVL");
        ev = _oracle.Label("EV");
        rand = _oracle.Label("RAND");
        gcnt = _oracle.Label("GCNT");
        ghyp = _oracle.Label("GHYP");
        fist = _oracle.Label("FIST");
        cok = _oracle.Label("COK");
        qq14 = _oracle.Label("QQ14");
        patg = _oracle.Label("PATG");
        qq11 = _oracle.Label("QQ11");
      }
    };

  public:
    /*
     * 6502: TT18 -- the jump itself, from the fuel to whichever of three ends it reaches.
     *
     * STOPPED AT `TT110`, because that is where the routine ENDS: `INC QQ11` is its last
     * instruction and the launch below it is a fall-through the caller owns. Running on would
     * compare `TT110` twice, since its own slice already does.
     *
     * The `ptg` path cannot be reached from here. `JSR CTRL` reads the keyboard, which an
     * interpreter has no answer for, so the cheat is tested by calling `ptg` directly above. What
     * IS swept here is the fuel arithmetic, the one-in-256 witchspace roll, and the view mask --
     * `AND #%00111111`, which is not "is this a space view" but "are the low six bits clear", and
     * the two differ for exactly the screens the charts use.
     */
    TEST_METHOD(TheJumpMatchesTT18)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const Where where(oracle);
      const std::uint16_t tt18 = oracle.Label("TT18");
      const std::uint16_t tt110 = oracle.Label("TT110");

      // Four generator states, chosen so both sides of `CMP #253` are reached -- the assertion at
      // the end is what says they were.
      const std::array<std::uint8_t, 4> SEEDS[] = {
        {0x00u, 0x00u, 0x00u, 0x00u},
        {0x7Fu, 0x41u, 0x13u, 0x8Cu},
        {0xFEu, 0xFFu, 0xFDu, 0xFCu},
        {0x5Au, 0xA5u, 0x3Cu, 0xC3u},
      };

      std::uint32_t compared = 0;
      std::set<std::string> outcomes;

      for (const std::array<std::uint8_t, 4>& seed : SEEDS)
      {
        for (const std::uint8_t fuel : {std::uint8_t{0}, std::uint8_t{20}, std::uint8_t{70}})
        {
          for (const std::uint16_t distance : {std::uint16_t{0}, std::uint16_t{35}, std::uint16_t{100}})
          {
            for (const std::uint8_t view : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{64}, std::uint8_t{128}})
            {
              Cpu6502 cpu = oracle.Fresh();
              // `TT114` is the chart's own redraw, which `TT18` JUMPS to rather than calls -- the
              // port hands it back as an outcome for the caller, so here it is a trap.
              for (const char* seam : {"NOISE", "MESS", "NOISE2", "WSCAN", "DELAY", "TT114"})
              {
                std::uint16_t address = 0;
                if (oracle.TryLabel(seam, address))
                {
                  cpu.AddTrap(address);
                }
              }

              LoopWorld world;
              Seed(world.world, 5u);
              world.world.commander.At(Elite::Field::Fuel) = fuel;
              world.world.view = view;
              world.world.status.midJump = 0u;

              Elite::SystemSeeds galaxySeeds{};
              for (std::size_t byte = 0; byte < 6u; ++byte)
              {
                galaxySeeds.bytes[byte] = cpu.memory[static_cast<std::uint16_t>(at.qq21 + byte)];
              }

              Elite::JumpState jump;
              jump.distance = distance;
              for (std::size_t byte = 0; byte < 6u; ++byte)
              {
                const std::uint8_t value = static_cast<std::uint8_t>(0x3Bu + byte * 19u);
                jump.target.bytes[byte] = value;
                cpu.memory[static_cast<std::uint16_t>(at.safehouse + byte)] = value;
              }

              Mirror(world.world, cpu, where);
              cpu.memory[at.qq14] = fuel;
              cpu.memory[at.qq8] = static_cast<std::uint8_t>(distance & 0xFFu);
              cpu.memory[static_cast<std::uint16_t>(at.qq8 + 1u)] = static_cast<std::uint8_t>(distance >> 8u);
              cpu.memory[at.qq11] = view;
              cpu.memory[at.patg] = 0u; // the cheat needs the option AND the key; neither is set
              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(at.rand + byte)] = seed[byte];
              }

              Elite::SystemData described{};
              described.economy = cpu.memory[at.qq3];
              described.government = cpu.memory[at.qq4];
              described.techLevel = cpu.memory[at.qq5];

              const Elite::Testing::RunResult run = cpu.CallSubroutine(tt18, 40'000'000, tt110);
              Assert::IsTrue(run.completed, L"TT18 reached its end");

              Elite::FlightScreen screen = world.world.Screen();
              Elite::FlightLoop loop{screen,     world.keys,       world.control, world.options, world.burst,   world.heap,
                                     world.clip, world.projection, world.axes,    world.effects, world.effects, world.effects};

              Elite::Rng rng;
              rng.SetState(seed);
              screen.rng.SetState(seed);

              Elite::CurrentSystem current;
              Elite::SystemSeeds selected{};
              Elite::MarketState market;

              const Elite::JumpResult result = Elite::PerformJump(loop, current, selected, jump, described, market, world.effects, nullptr,
                                                                  cpu.memory[at.qq9], cpu.memory[at.qq10], galaxySeeds, false, false);

              const std::wstring context = WidenText("TT18 seed " + std::to_string(seed[0]) + " fuel " + std::to_string(fuel) + " dist " +
                                                     std::to_string(distance) + " view " + std::to_string(view));

              Assert::AreEqual(cpu.memory[at.qq14], world.world.commander.At(Elite::Field::Fuel), (context + L": QQ14").c_str());
              Assert::AreEqual(cpu.memory[where.mj], world.world.status.midJump, (context + L": MJ").c_str());
              Assert::AreEqual(cpu.memory[at.qq11], world.world.view, (context + L": QQ11").c_str());
              for (std::size_t byte = 0; byte < 4u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.rand + byte)], screen.rng.State()[byte],
                                 (context + L": RAND+" + std::to_wstring(byte)).c_str());
              }

              outcomes.insert(std::to_string(static_cast<int>(result)) + "/" + std::to_string(world.world.status.midJump));
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(4u * 3u * 3u * 4u, compared, L"the whole sweep ran");
      Assert::IsTrue(outcomes.size() >= 3u, L"and it arrived, missed, and jumped from a chart");
    }

    /*
     * 6502: ptg -- `LSR COK / SEC / ROL COK`, which is `ORA #1` and not a rotate.
     *
     * Compared over every byte value that matters, because the whole point is that bits 1 to 7
     * come back unchanged: the `LSR` drops them one place and the `ROL` puts them back, and only
     * bit 0 is forced. §6.126 found the mirror of this idiom (`ASL / SEC / ROR` is `ORA #128`)
     * ported as a shift twice over, so this one is swept rather than read.
     *
     * `ptg` falls into `MJP`, so the oracle is stopped there: what this compares is the three
     * instructions above the fall-through, and `WitchspaceMatchesMJP` covers the rest.
     */
    TEST_METHOD(TheCheatFlagMatchesPtg)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const std::uint16_t ptg = oracle.Label("ptg");
      const std::uint16_t mjp = oracle.Label("MJP");

      std::uint32_t compared = 0;

      for (std::uint32_t value = 0; value < 256u; ++value)
      {
        Cpu6502 cpu = oracle.Fresh();
        cpu.memory[at.cok] = static_cast<std::uint8_t>(value);

        const Elite::Testing::RunResult run = cpu.CallSubroutine(ptg, 10'000, mjp);
        Assert::IsTrue(run.completed, L"ptg reached MJP");

        Elite::CommanderBlock commander{};
        commander.At(Elite::Field::Competition) = static_cast<std::uint8_t>(value);
        commander.At(Elite::Field::Competition) = static_cast<std::uint8_t>(commander.At(Elite::Field::Competition) | 1u);

        Assert::AreEqual(cpu.memory[at.cok], commander.At(Elite::Field::Competition), (L"ptg COK " + std::to_wstring(value)).c_str());
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(256u, compared, L"every byte value was tried");
    }

    /*
     * 6502: Ghy -- the galactic hyperdrive.
     *
     * Both answers to `LDX GHYP / BEQ zZ+1`, because the false one is the interesting instruction
     * in the routine: `zZ` is `LDA #96`, assembled as `A9 60`, so `zZ+1` is the OPERAND and &60 is
     * `RTS`. With no drive fitted the branch jumps into the middle of an instruction and executes
     * its argument as a return. Swept with the drive fitted and without, so the port's early exit
     * is compared against the original's rather than assumed to match it.
     */
    TEST_METHOD(TheGalacticHyperdriveMatchesGhy)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const Where where(oracle);
      const std::uint16_t ghy = oracle.Label("Ghy");

      std::uint32_t compared = 0;
      std::set<std::string> galaxies;

      for (const std::uint32_t seedIndex : {2u, 11u})
      {
        for (const std::uint8_t fitted : {std::uint8_t{0}, std::uint8_t{255}})
        {
          for (const std::uint8_t galaxy : {std::uint8_t{0}, std::uint8_t{3}, std::uint8_t{7}})
          {
            Cpu6502 cpu = oracle.Fresh();
            for (const char* seam : {"NOISE", "MESS", "NOISE2", "WSCAN", "DELAY"})
            {
              std::uint16_t address = 0;
              if (oracle.TryLabel(seam, address))
              {
                cpu.AddTrap(address);
              }
            }

            LoopWorld world;
            Seed(world.world, seedIndex);
            world.world.commander.At(Elite::Field::GalacticDrive) = fitted;
            world.world.commander.At(Elite::Field::GalaxyNumber) = galaxy;
            world.world.commander.At(Elite::Field::LegalStatus) = 40u;

            Elite::SystemSeeds galaxySeeds{};
            for (std::size_t byte = 0; byte < 6u; ++byte)
            {
              galaxySeeds.bytes[byte] = cpu.memory[static_cast<std::uint16_t>(at.qq21 + byte)];
            }

            // The crosshairs, which `Ghy` only overwrites when a drive is fitted -- so the two
            // sides have to start with the same pair or the early exit compares nothing.
            Elite::ChartView chart;
            chart.cursorX = 10u;
            chart.cursorY = 20u;

            Mirror(world.world, cpu, where);
            cpu.memory[at.ghyp] = fitted;
            cpu.memory[at.gcnt] = galaxy;
            cpu.memory[at.fist] = 40u;
            cpu.memory[at.qq9] = chart.cursorX;
            cpu.memory[at.qq10] = chart.cursorY;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(ghy, 40'000'000);
            Assert::IsTrue(run.completed, L"Ghy returned");

            Elite::FlightScreen screen = world.world.Screen();
            Elite::FlightLoop loop{screen,     world.keys,       world.control, world.options, world.burst,   world.heap,
                                   world.clip, world.projection, world.axes,    world.effects, world.effects, world.effects};

            Elite::CurrentSystem current;
            Elite::SystemSeeds selected{};
            Elite::JumpState jump;

            Elite::GalacticJump(loop, current, galaxySeeds, selected, jump, chart, nullptr);

            const std::wstring context = WidenText("Ghy seed " + std::to_string(seedIndex) + (fitted != 0u ? " fitted" : " none") +
                                                   " galaxy " + std::to_string(galaxy));

            Assert::AreEqual(cpu.memory[at.ghyp], world.world.commander.At(Elite::Field::GalacticDrive), (context + L": GHYP").c_str());
            Assert::AreEqual(cpu.memory[at.fist], world.world.commander.At(Elite::Field::LegalStatus), (context + L": FIST").c_str());
            Assert::AreEqual(cpu.memory[at.gcnt], world.world.commander.At(Elite::Field::GalaxyNumber), (context + L": GCNT").c_str());
            for (std::size_t byte = 0; byte < 6u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.qq21 + byte)], galaxySeeds.bytes[byte],
                               (context + L": QQ21+" + std::to_wstring(byte)).c_str());
            }
            Assert::AreEqual(cpu.memory[at.qq9], chart.cursorX, (context + L": QQ9").c_str());
            Assert::AreEqual(cpu.memory[at.qq10], chart.cursorY, (context + L": QQ10").c_str());
            Assert::AreEqual(cpu.memory[at.qq0], world.world.commander.At(Elite::Field::SystemX), (context + L": QQ0").c_str());
            Assert::AreEqual(cpu.memory[at.qq1], world.world.commander.At(Elite::Field::SystemY), (context + L": QQ1").c_str());
            Assert::AreEqual(cpu.memory[at.qq22 + 1], jump.countdown, (context + L": QQ22+1").c_str());
            Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(cpu.memory[at.qq8] | (cpu.memory[at.qq8 + 1] << 8)), jump.distance,
                                            (context + L": QQ8").c_str());
            for (std::size_t byte = 0; byte < 6u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.safehouse + byte)], jump.target.bytes[byte],
                               (context + L": safehouse+" + std::to_wstring(byte)).c_str());
            }

            galaxies.insert(std::to_string(world.world.commander.At(Elite::Field::GalaxyNumber)) + "/" +
                            std::to_string(world.world.commander.At(Elite::Field::GalacticDrive)));
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(2u * 2u * 3u, compared, L"the whole sweep ran");
      Assert::IsTrue(galaxies.size() >= 4u, L"and both the fitted and the missing drive were reached");
    }

    /*
     * 6502: MJP -- witchspace, which is a jump that did not arrive.
     *
     * Run whole on both sides, screen included, because `TT66`, `LL164`, `RES2` and `LOOK1` all
     * draw and `Mirror`/`CompareState` already compare the canvas byte for byte. What this slice
     * adds on top of them is four bytes and a loop: `MJ`, `NOSTM`, `QQ1` and however many
     * Thargoids `MJP1` decided to make.
     *
     * **`MJ` IS 255, and no instruction in `MJP` says so.** `STY MJ` stores whatever `RES2` left
     * in Y, `RES2` falls into `ZINF`, and `ZINF`'s clearing loop ends `DEY / BPL ZI1` -- so Y is
     * &FF, three routines away from the store. A port that wrote 1 would behave identically
     * everywhere (`MJ` is only ever tested for non-zero) and would still be wrong in the commander
     * file and in every comparison here.
     */
    TEST_METHOD(WitchspaceMatchesMJP)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);
      const Where where(oracle);
      const std::uint16_t mjp = oracle.Label("MJP");

      std::uint32_t compared = 0;
      std::set<std::string> outcomes;

      for (const std::uint32_t seedIndex : {1u, 7u, 23u})
      {
        for (const std::uint8_t systemY : {std::uint8_t{0}, std::uint8_t{33}, std::uint8_t{200}, std::uint8_t{255}})
        {
          Cpu6502 cpu = oracle.Fresh();

          LoopWorld world;
          Seed(world.world, seedIndex);
          world.world.commander.At(Elite::Field::SystemY) = systemY;
          world.world.status.midJump = 0u;

          // The sound and the message are seams on the port's side, so they are traps on the
          // oracle's -- otherwise the comparison would be against a routine that made a noise.
          // `WSCAN` waits on the VIC-II raster, which never advances in an interpreter -- it is a
          // hardware wait and not code, so it is trapped like the sound seams (ChartTests does the
          // same). Without it `LL164`'s tunnel spins for ever on the first circle.
          for (const char* seam : {"NOISE", "MESS", "NOISE2", "WSCAN", "DELAY"})
          {
            std::uint16_t address = 0;
            if (oracle.TryLabel(seam, address))
            {
              cpu.AddTrap(address);
            }
          }

          Mirror(world.world, cpu, where);

          const Elite::Testing::RunResult run = cpu.CallSubroutine(mjp, 40'000'000);
          Assert::IsTrue(run.completed, (L"MJP returned, stopped at " + std::to_wstring(run.stoppedAt) + L" after " +
                                         std::to_wstring(run.instructions) + L" instructions")
                                          .c_str());

          Elite::FlightScreen screen = world.world.Screen();
          Elite::FlightLoop loop{screen,     world.keys,       world.control, world.options, world.burst,   world.heap,
                                 world.clip, world.projection, world.axes,    world.effects, world.effects, world.effects};

          Elite::EnterWitchspace(loop, world.world.commander, world.effects, nullptr);

          const std::wstring context = WidenText("MJP seed " + std::to_string(seedIndex) + " QQ1 " + std::to_string(systemY));

          CompareState(cpu, world.world, where, context);
          Assert::AreEqual(cpu.memory[at.qq1], world.world.commander.At(Elite::Field::SystemY), (context + L": QQ1").c_str());

          outcomes.insert(std::to_string(world.world.status.midJump) + "/" + std::to_string(world.world.dust.count) + "/" +
                          std::to_string(world.world.bubble.counts[Elite::SHIP_TYPE_THARGOID]));
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(3u * 4u, compared, L"the whole sweep ran");

      // Every case must end with the flag SET, three specks of dust and four Thargoids: `MJP1`
      // loops until the count passes three, so the answer is the same however it got there.
      Assert::AreEqual<std::size_t>(1u, outcomes.size(), L"and witchspace looks the same every time");
      Assert::AreEqual(std::string("255/3/4"), *outcomes.begin(), L"MJ is 255, NOSTM is 3, and there are four Thargoids");
    }

    /*
     * 6502: hyp1 -- arriving, which is six seed bytes copied, three cached values, `EV` reset and
     * a whole market generated by the `GVL` it falls into.
     *
     * BOTH ENTRY POINTS, because the game uses both: `hyp1` finds the system nearest the
     * crosshairs first, and `TT18` jumps to `hyp1+3` because the chart already chose. Three bytes
     * apart, one routine, and a port that had only the first would generate the market from the
     * right seeds and cache the wrong economy on every jump.
     */
    TEST_METHOD(ArrivingMatchesHyp1)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Labels at(oracle);

      const std::uint8_t CROSSHAIRS[][2] = {{20u, 173u}, {96u, 96u}, {0u, 0u}, {255u, 255u}, {144u, 33u}, {70u, 210u}};

      std::uint32_t compared = 0;
      std::set<std::string> economies;

      for (const auto& where : CROSSHAIRS)
      {
        for (const std::uint8_t skipFind : {std::uint8_t{0}, std::uint8_t{1}})
        {
          for (const std::array<std::uint8_t, 4> seed :
               {std::array<std::uint8_t, 4>{0x31u, 0x9Bu, 0x02u, 0xEEu}, std::array<std::uint8_t, 4>{0x00u, 0x00u, 0x00u, 0x00u}})
          {
            Cpu6502 cpu = oracle.Fresh();

            // The galaxy's seeds are the oracle image's own -- QQ21 as the game ships it.
            Elite::SystemSeeds galaxy{};
            for (std::size_t byte = 0; byte < 6u; ++byte)
            {
              galaxy.bytes[byte] = cpu.memory[static_cast<std::uint16_t>(at.qq21 + byte)];
            }

            // `safehouse` is what the countdown saved, and `hyp1` copies it into `QQ2` -- so it is
            // the input, and the crosshairs only matter on the entry that calls `TT111`.
            Elite::SystemSeeds target{};
            for (std::size_t byte = 0; byte < 6u; ++byte)
            {
              const std::uint8_t value = static_cast<std::uint8_t>(0x5Au + byte * 29u + where[0]);
              target.bytes[byte] = value;
              cpu.memory[static_cast<std::uint16_t>(at.safehouse + byte)] = value;
            }

            cpu.memory[at.qq9] = where[0];
            cpu.memory[at.qq10] = where[1];

            /*
             * `QQ3` to `QQ5` given DIFFERENT values per case, because on the `hyp1+3` entry nothing
             * recomputes them and a fixture that left them all alike could not tell "cached from
             * the described system" apart from "derived from the seeds in QQ2" -- which is exactly
             * the defect this sweep found.
             */
            cpu.memory[at.qq3] = static_cast<std::uint8_t>(where[0] & 7u);
            cpu.memory[at.qq4] = static_cast<std::uint8_t>((where[1] >> 2u) & 7u);
            cpu.memory[at.qq5] = static_cast<std::uint8_t>((where[0] + where[1]) & 15u);
            cpu.memory[at.ev] = 0x7Fu;
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              cpu.memory[static_cast<std::uint16_t>(at.rand + byte)] = seed[byte];
            }

            Elite::Rng rng;
            rng.SetState(seed);

            const std::uint16_t entry = static_cast<std::uint16_t>(at.hyp1 + (skipFind != 0u ? 3u : 0u));
            const Elite::Testing::RunResult run = cpu.CallSubroutine(entry, 400'000);
            Assert::IsTrue(run.completed, L"hyp1 returned");

            Elite::CommanderBlock commander{};
            Elite::CurrentSystem current;
            Elite::SystemSeeds selected{};
            Elite::MarketState market;
            std::uint8_t explosions = 0x7Fu;

            /*
             * `QQ3` to `QQ5` -- what the last `TT111` left, and NOT the system `QQ2` is about to
             * get. On the `hyp1+3` entry nothing recomputes them, so they are an input; on the
             * full entry `TT111` overwrites them. Seeded on both sides so the two agree about what
             * the chart was last looking at.
             */
            Elite::SystemData described{};
            described.economy = cpu.memory[at.qq3];
            described.government = cpu.memory[at.qq4];
            described.techLevel = cpu.memory[at.qq5];

            Elite::ArriveAtSystem(commander, current, selected, target, described, market, rng, explosions, where[0], where[1], galaxy,
                                  skipFind == 0u);

            const std::wstring context = WidenText("hyp1" + std::string(skipFind != 0u ? "+3" : "") + " at " + std::to_string(where[0]) +
                                                   "," + std::to_string(where[1]) + " seed " + std::to_string(seed[0]));

            for (std::size_t byte = 0; byte < 6u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.qq2 + byte)], current.seeds.bytes[byte],
                               (context + L": QQ2+" + std::to_wstring(byte)).c_str());
            }
            Assert::AreEqual(cpu.memory[at.qq28], current.economy, (context + L": QQ28").c_str());
            Assert::AreEqual(cpu.memory[at.tek], current.techLevel, (context + L": tek").c_str());
            Assert::AreEqual(cpu.memory[at.gov], current.government, (context + L": gov").c_str());
            Assert::AreEqual(cpu.memory[at.ev], explosions, (context + L": EV").c_str());
            Assert::AreEqual(cpu.memory[at.qq0], commander.At(Elite::Field::SystemX), (context + L": QQ0").c_str());
            Assert::AreEqual(cpu.memory[at.qq1], commander.At(Elite::Field::SystemY), (context + L": QQ1").c_str());

            // What `GVL` produced, which is the half of this routine that is not a copy.
            Assert::AreEqual(cpu.memory[at.qq26], market.randomiser, (context + L": QQ26").c_str());
            for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.avl + item)], market.availability[item],
                               (context + L": AVL+" + std::to_wstring(item)).c_str());
            }
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(at.rand + byte)], rng.State()[byte],
                               (context + L": RAND+" + std::to_wstring(byte)).c_str());
            }

            economies.insert(std::to_string(current.economy) + "/" + std::to_string(current.government));
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(6u * 2u * 2u, compared, L"the whole sweep ran");
      Assert::IsTrue(economies.size() >= 4u, L"and it arrived in systems of several kinds");
    }

    /*
     * 6502: ESCAPE -- abandon ship, which is ninety-seven frames of animation and then a docking.
     *
     * Run whole on both sides, canvas included, because `MVEIT` and `LL9` draw the ship receding
     * and `SCAN` puts a blip on the scanner. `GOIN` is trapped: it is the docking, which belongs to
     * slice 2d, and the port hands it back to the caller the way every other `JMP` out of a routine
     * has been.
     *
     * The Trumbles are swept from none to a hold full, because `ORA #1` is what stops the
     * population reaching zero -- one to eight always survive -- and a sweep that started with none
     * would never execute the branch that says so.
     */
    TEST_METHOD(AbandoningShipMatchesESCAPE)
    {
      const OracleImage& oracle = OracleImage::Instance();
      const Where where(oracle);
      const std::uint16_t escape = oracle.Label("ESCAPE");
      const std::uint16_t qq20 = oracle.Label("QQ20");
      const std::uint16_t escp = oracle.Label("ESCP");
      const std::uint16_t fist = oracle.Label("FIST");
      const std::uint16_t qq14 = oracle.Label("QQ14");
      const std::uint16_t tribble = oracle.Label("TRIBBLE");
      const std::uint16_t goin = oracle.Label("GOIN");

      struct Case
      {
        std::uint32_t seed;
        std::uint8_t tribbleLow, tribbleHigh, legal, fuel;
        std::vector<std::uint8_t> fleet;
      };

      const Case CASES[] = {
        {6u, 0u, 0u, 0u, 20u, {128u, 129u}},                   // no Trumbles at all
        {6u, 3u, 0u, 40u, 5u, {128u, 129u}},                   // a few, and an offender
        {9u, 200u, 90u, 200u, 0u, {128u, 129u}},               // a hold full, and a fugitive
        {9u, 0u, 1u, 0u, 70u, {128u, 129u}},                   // only the high byte set
        {12u, 17u, 2u, 10u, 33u, {128u, 129u, 11u, 11u, 11u}}, // a busy bubble
        // A FULL one, which is the branch `BCS ES1` exists for: the Cobra does not fit, so the
        // pirate Cobra is tried instead, and here neither fits. The animation runs regardless.
        {12u, 5u, 0u, 0u, 12u, {128u, 129u, 11u, 11u, 11u, 11u, 11u, 11u, 11u, 11u}},
      };

      std::uint32_t compared = 0;
      std::set<std::string> outcomes;

      for (const Case& one : CASES)
      {
        Cpu6502 cpu = oracle.Fresh();
        for (const char* seam : {"NOISE", "NOISE2", "MESS", "WSCAN", "DELAY", "BELL"})
        {
          std::uint16_t address = 0;
          if (oracle.TryLabel(seam, address))
          {
            cpu.AddTrap(address);
          }
        }
        cpu.AddTrap(goin); // 6502: JMP GOIN -- the docking, which is the caller's

        LoopWorld world;
        Seed(world.world, one.seed);
        world.world.commander.At(Elite::Field::Tribbles) = one.tribbleLow;
        world.world.commander.At(static_cast<Elite::Field>(static_cast<int>(Elite::Field::Tribbles) + 1)) = one.tribbleHigh;
        world.world.commander.At(Elite::Field::LegalStatus) = one.legal;
        world.world.commander.At(Elite::Field::EscapePod) = 0xFFu;
        world.world.commander.At(Elite::Field::Fuel) = one.fuel;
        for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
        {
          world.world.commander.At(static_cast<Elite::Field>(static_cast<int>(Elite::Field::CargoHold) + static_cast<int>(item))) =
            static_cast<std::uint8_t>(3u + item);
        }

        Mirror(world.world, cpu, where);
        cpu.memory[tribble] = one.tribbleLow;
        cpu.memory[static_cast<std::uint16_t>(tribble + 1u)] = one.tribbleHigh;
        cpu.memory[fist] = one.legal;
        cpu.memory[escp] = 0xFFu;
        cpu.memory[qq14] = one.fuel;
        for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
        {
          cpu.memory[static_cast<std::uint16_t>(qq20 + item)] = static_cast<std::uint8_t>(3u + item);
        }

        const Elite::Testing::RunResult run = cpu.CallSubroutine(escape, 40'000'000);
        Assert::IsTrue(run.completed, L"ESCAPE reached GOIN");

        Elite::FlightScreen screen = world.world.Screen();
        Elite::FlightLoop loop{screen,     world.keys,       world.control, world.options, world.burst,   world.heap,
                               world.clip, world.projection, world.axes,    world.effects, world.effects, world.effects};

        std::uint8_t fuel = one.fuel;
        Elite::AbandonShip(loop, fuel);

        const std::wstring context = WidenText("ESCAPE seed " + std::to_string(one.seed) + " trib " + std::to_string(one.tribbleHigh) +
                                               "/" + std::to_string(one.tribbleLow));

        CompareState(cpu, world.world, where, context);

        for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
        {
          Assert::AreEqual(
            cpu.memory[static_cast<std::uint16_t>(qq20 + item)],
            world.world.commander.At(static_cast<Elite::Field>(static_cast<int>(Elite::Field::CargoHold) + static_cast<int>(item))),
            (context + L": QQ20+" + std::to_wstring(item)).c_str());
        }
        Assert::AreEqual(cpu.memory[fist], world.world.commander.At(Elite::Field::LegalStatus), (context + L": FIST").c_str());
        Assert::AreEqual(cpu.memory[escp], world.world.commander.At(Elite::Field::EscapePod), (context + L": ESCP").c_str());
        Assert::AreEqual(cpu.memory[tribble], world.world.commander.At(Elite::Field::Tribbles), (context + L": TRIBBLE").c_str());
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(tribble + 1u)],
                         world.world.commander.At(static_cast<Elite::Field>(static_cast<int>(Elite::Field::Tribbles) + 1)),
                         (context + L": TRIBBLE+1").c_str());
        Assert::AreEqual(cpu.memory[qq14], fuel, (context + L": QQ14").c_str());

        outcomes.insert(std::to_string(world.world.commander.At(Elite::Field::Tribbles)) + "/" +
                        std::to_string(world.world.bubble.slots[0]));
        ++compared;
      }

      Assert::AreEqual<std::uint32_t>(6u, compared, L"the whole sweep ran");
      /*
       * TWO OUTCOMES, AND THE THIRD IS UNREACHABLE. `RES2` empties the bubble before `FRS1` runs,
       * so there is always a free slot and always heap: the Cobra never fails, and `BCS ES1`'s
       * alternative -- `LDX #CYL2 / JSR FRS1`, the PIRATE Cobra -- can never be the ship you left
       * behind. A ten-ship fleet was put in this sweep to reach it and `RES2` wiped it first.
       *
       * Dead on this build for the same kind of reason as part 1's `CMP #HER / BEQ TT100` (§6.135):
       * not because the code is wrong, but because a routine three calls up guarantees the test.
       * Transcribed anyway, and recorded here so the next reader does not have to prove it again.
       */
      Assert::AreEqual<std::size_t>(2u, outcomes.size(), L"the Trumbles survived and were absent");
      for (const std::string& one : outcomes)
      {
        Assert::IsTrue(one.find("/" + std::to_string(Elite::SHIP_TYPE_COBRA_MK3)) != std::string::npos,
                       L"and the ship left behind is always the Cobra, never the pirate");
      }
    }
  };

} // namespace GameLogicTests
