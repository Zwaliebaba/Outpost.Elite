#include "pch.h"

#include "Cpu6502.h"
#include "FlightUniverse.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "Rng.h"
#include "Dashboard.h"
#include "ShipBlueprint.h"
#include "ShipSlot.h"
#include "Tactics.h"

#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * What the flight loop calls but does not need (slice 3d-d-i).
 *
 * All four distance helpers are small enough to sweep properly: `MAS2` and `MAS4` are exhaustive
 * in the byte they OR into, `MAS3` over every high byte a block can hold, and `MAS1` over the
 * coordinates that make its sixteen-bit doubling overflow -- which is the case its third byte
 * exists for. `cntr` is exhaustive outright, in the reading and in both flags.
 */
namespace GameLogicTests
{

  TEST_CLASS(TheFlightLoopDistanceHelpers)
  {
  public:
    /// 6502: MAS2 and `m` -- both entry points, every byte ORed in, both blocks the loop asks about.
    TEST_METHOD(TheLargestAxisMatchesMAS2)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t mas2 = oracle.Label("MAS2");
      const std::uint16_t m = oracle.Label("m");
      const std::uint16_t kPercent = oracle.Label("K%");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;

      for (const std::uint8_t slot : {std::uint8_t{0}, std::uint8_t{1}})
      {
        for (std::uint32_t seed = 0; seed < 24; ++seed)
        {
          Elite::Bubble bubble;
          std::uint32_t state = 0x2C7B41A5u ^ (seed * 0x9E3779B9u) ^ slot;
          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = bubble.blocks[slot].ToBytes();
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            state = state * 1103515245u + 12345u;
            const std::uint8_t value = static_cast<std::uint8_t>(state >> 17);
            shipBytes[byte] = value;
            cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
          }
          bubble.blocks[slot] = Elite::Ship::FromBytes(shipBytes);

          for (std::uint32_t seedByte = 0; seedByte < 256; ++seedByte)
          {
            for (const bool viaM : {false, true})
            {
              cpu.a = static_cast<std::uint8_t>(seedByte);
              cpu.y = static_cast<std::uint8_t>(slot * Elite::SHIP_BLOCK_SIZE);

              const Elite::Testing::RunResult run = cpu.CallSubroutine(viaM ? m : mas2, 500);
              Assert::IsTrue(run.completed, L"MAS2 returned");

              const std::uint8_t ours =
                viaM ? Elite::LargestAxis(bubble, slot) : Elite::LargestAxisFrom(bubble, slot, static_cast<std::uint8_t>(seedByte));

              const std::wstring where =
                WidenText(std::string(viaM ? "m" : "MAS2") + "(slot " + std::to_string(slot) + ", A " + std::to_string(seedByte) + ")");
              Assert::AreEqual(cpu.a, ours, where.c_str());
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(2u * 24u * 256u * 2u, compared, L"the whole sweep ran");
    }

    /// 6502: MAS4 -- the same shape without the mask, over INWK.
    TEST_METHOD(TheLargestShipAxisMatchesMAS4)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t mas4 = oracle.Label("MAS4");
      const std::uint16_t inwk = oracle.Label("INWK");

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;

      for (std::uint32_t seed = 0; seed < 16; ++seed)
      {
        Elite::Ship work;
        std::uint32_t state = 0x71A3C25Fu ^ (seed * 0x85EBCA6Bu);
        std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          state = state * 1103515245u + 12345u;
          const std::uint8_t value = static_cast<std::uint8_t>(state >> 17);
          shipBytes[byte] = value;
          cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = value;
        }
        work = Elite::Ship::FromBytes(shipBytes);

        for (std::uint32_t seedByte = 0; seedByte < 256; ++seedByte)
        {
          cpu.a = static_cast<std::uint8_t>(seedByte);

          const Elite::Testing::RunResult run = cpu.CallSubroutine(mas4, 500);
          Assert::IsTrue(run.completed, L"MAS4 returned");

          Assert::AreEqual(cpu.a, Elite::LargestShipAxis(work, static_cast<std::uint8_t>(seedByte)),
                           WidenText("MAS4(A " + std::to_string(seedByte) + ")").c_str());
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(16u * 256u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: MAS3 -- and the sweep has to reach the SATURATION, twice over: once when the first two
     * squares already overflow and the routine leaves through `MA30`, and once when only the third
     * pushes it over. A sweep of small coordinates would exercise neither.
     */
    TEST_METHOD(TheSumOfSquaresMatchesMAS3)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t mas3 = oracle.Label("MAS3");
      const std::uint16_t kPercent = oracle.Label("K%");

      const std::uint8_t VALUES[] = {0, 1, 2, 15, 16, 63, 64, 100, 127, 128, 180, 200, 254, 255};

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;
      std::uint32_t saturated = 0;
      std::set<std::uint8_t> answers;

      for (const std::uint8_t x : VALUES)
      {
        for (const std::uint8_t y : VALUES)
        {
          for (const std::uint8_t z : VALUES)
          {
            Elite::Bubble bubble;
            const std::uint8_t BYTES[3] = {x, y, z};
            std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = bubble.blocks[0].ToBytes();
            for (int axis = 0; axis < 3; ++axis)
            {
              const std::size_t at = static_cast<std::size_t>(axis) * 3u + 1u;
              shipBytes[at] = BYTES[axis];
              cpu.memory[static_cast<std::uint16_t>(kPercent + at)] = BYTES[axis];
            }
            bubble.blocks[0] = Elite::Ship::FromBytes(shipBytes);

            cpu.y = 0;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(mas3, 5'000);
            Assert::IsTrue(run.completed, L"MAS3 returned");

            const std::uint8_t ours = Elite::SumOfSquares(bubble, 0);

            const std::wstring where = WidenText("MAS3(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
            Assert::AreEqual(cpu.a, ours, where.c_str());

            saturated += (ours == 0xFFu) ? 1u : 0u;
            answers.insert(ours);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(14u * 14u * 14u, compared, L"the whole sweep ran");
      Assert::IsTrue(saturated > 0u, L"and the saturation was reached");
      Assert::IsTrue(answers.size() > 20u, L"and the answers are not all the same");
      Logger::WriteMessage(("MAS3: " + std::to_string(compared) + " sums, " + std::to_string(saturated) + " saturated, " +
                            std::to_string(answers.size()) + " distinct answers")
                             .c_str());
    }

    /*
     * 6502: MAS1 -- the doubling, the add and the write-back.
     *
     * Swept over coordinates that make the sixteen-bit `ASL`/`ROL` overflow, because that overflow
     * is what the third byte exists to catch: `LDA #0 / ROR A` turns it into a sign rather than
     * losing it, and a port that dropped the byte would agree everywhere the top bit is clear.
     */
    TEST_METHOD(DoublingAndAddingMatchesMAS1)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t mas1 = oracle.Label("MAS1");
      const std::uint16_t inwk = oracle.Label("INWK");

      const std::uint8_t VALUES[] = {0, 1, 0x40, 0x7F, 0x80, 0x81, 0xC0, 0xFF};

      Cpu6502 cpu = oracle.Fresh();
      std::uint32_t compared = 0;
      std::uint32_t overflowed = 0;

      for (const std::uint8_t low : VALUES)
      {
        for (const std::uint8_t high : VALUES)
        {
          for (const std::uint8_t sign : VALUES)
          {
            for (const std::uint8_t target : VALUES)
            {
              Elite::Ship work;
              std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                shipBytes[byte] = 0;
              }
              work = Elite::Ship::FromBytes(shipBytes);

              // The source coordinate at INWK+9, and the destination at INWK+0.
              work.nose.x.lo = low;
              work.nose.x.hi = high;
              work.x.lo = target;
              work.x.hi = static_cast<std::uint8_t>(target ^ 0x5Au);
              work.x.sgn = sign;

              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = work.ToBytes()[byte];
              }

              cpu.y = 9;
              cpu.x = 0;
              const Elite::Testing::RunResult run = cpu.CallSubroutine(mas1, 5'000);
              Assert::IsTrue(run.completed, L"MAS1 returned");

              const std::uint8_t ours = Elite::DoubleAndAddCoordinate(work, 9, 0);

              const std::wstring where = WidenText("MAS1(low " + std::to_string(low) + ", high " + std::to_string(high) + ", sign " +
                                                   std::to_string(sign) + ", target " + std::to_string(target) + ")");

              Assert::AreEqual(cpu.a, ours, (where + L": the returned magnitude").c_str());
              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + byte)], work.ToBytes()[byte],
                                 (where + L": INWK+" + std::to_wstring(byte)).c_str());
              }

              overflowed += ((high & 0x80u) != 0u) ? 1u : 0u;
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(8u * 8u * 8u * 8u, compared, L"the whole sweep ran");
      Assert::IsTrue(overflowed > 0u, L"and the doubling overflowed into the third byte");
    }

    /*
     * 6502: cntr -- every reading, against every combination of the two flags.
     *
     * Two hundred and fifty-six values by three settings of `auto` and three of `DAMP` is 2,304
     * calls, which is the whole input space with the "non-zero" tests given a 1 and an &FF each --
     * `DAMP` only ever holds those two, but `auto` is a countdown in the docking computer and the
     * routine tests it rather than comparing it.
     *
     * AND IT PROVES THE DEAD TAIL. A trap on `REDU` is armed for the whole sweep: if any input
     * reached it the trap would fire, and the count at the end is zero, so the port is entitled to
     * leave `.REDU DEX / BEQ BUMP` out. The trap also returns early rather than running those two
     * instructions, so an input that reached it would diverge here as well -- two ways to catch
     * the same mistake, because "this instruction cannot run" is exactly the claim a port should
     * not be allowed to make on its own authority (§6.71).
     */
    TEST_METHOD(TheControlDampingMatchesCntr)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t cntr = oracle.Label("cntr");
      const std::uint16_t autoPilot = oracle.Label("auto");
      const std::uint16_t damp = oracle.Label("DAMP");

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("REDU"));

      std::uint32_t compared = 0;
      std::uint32_t moved = 0;
      std::uint32_t stood = 0;

      for (const std::uint8_t docking : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0xFF}})
      {
        for (const std::uint8_t damping : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0xFF}})
        {
          for (std::uint32_t reading = 0; reading < 256; ++reading)
          {
            const std::uint8_t value = static_cast<std::uint8_t>(reading);

            cpu.memory[autoPilot] = docking;
            cpu.memory[damp] = damping;
            cpu.x = value;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(cntr, 200);
            Assert::IsTrue(run.completed, L"cntr returned");

            const std::uint8_t ours = Elite::DampTowardsCentre(value, docking, damping);

            const std::wstring where = WidenText("cntr(" + std::to_string(reading) + ", auto " + std::to_string(docking) + ", DAMP " +
                                                 std::to_string(damping) + ")");

            Assert::AreEqual(cpu.x, ours, where.c_str());
            Assert::AreEqual(cpu.memory[autoPilot], docking, (where + L": auto is left alone").c_str());
            Assert::AreEqual(cpu.memory[damp], damping, (where + L": DAMP is left alone").c_str());

            if (cpu.x == value)
            {
              ++stood;
            }
            else
            {
              ++moved;
            }
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(3u * 3u * 256u, compared, L"the whole sweep ran");
      Assert::IsTrue(moved > 0u, L"some readings were damped");
      Assert::IsTrue(stood > 0u, L"and some were not");
      Assert::AreEqual<std::size_t>(0u, cpu.trapHits.size(), L"REDU was never reached");
    }

    /*
     * 6502: SPIN and SPIN2 -- the wreckage a destroyed ship leaves, and the loop that places it.
     *
     * `SFS1` is trapped, so what is compared is how many times the game asks for a child ship and
     * with what -- which is the whole of what these two routines decide. The trap also keeps the
     * ship slots out of it: `SFS1` would fill the bubble on the first case and then start failing,
     * and a routine that ignores its carry would look identical either way.
     *
     * `SPIN2` IS ENTERED WITH A FLAG, not just a value. `STA CNT` sets nothing, so the `BEQ` at the
     * top of its loop reads the caller's Z -- and this sets `cpu.z` from the count on purpose, to
     * match what every real caller has just done with an `AND`. Setting it the other way is the one
     * input the port cannot reproduce, because it takes the count and infers the flag.
     *
     * The blueprint sweep is what makes `SPIN` worth testing at all: byte 0 caps the count, so a
     * port that dropped the `AND (XX0),Y` would still pass on any ship whose byte 0 has all four
     * low bits set. Every type this build carries is swept, and the assertion at the end is that
     * they did not all behave the same way.
     */
    TEST_METHOD(TheWreckageMatchesSPINAndSPIN2)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t spin = oracle.Label("SPIN");
      const std::uint16_t spin2 = oracle.Label("SPIN2");
      const std::uint16_t sfs1 = oracle.Label("SFS1");
      const std::uint16_t cnt = oracle.Label("CNT");
      const std::uint16_t xx0 = oracle.Label("XX0");
      const std::uint16_t rand = oracle.Label("RAND");

      /*
       * `SFS1`'s exit carry is `NWSHP`'s -- `NW3`'s `CLC` for a full bubble, `NWL3`'s `SEC` for a
       * ship that was made -- because everything between the `JSR NWSHP` and `SFS1`'s `RTS` is
       * pulls and stores. `SPIN` hands that flag back to `MA47`, whose second `JSR SPIN` runs on
       * it (M2-d), so the trap ends `SEC` and the seam here answers the same.
       */
      struct Recorder final : Elite::SpawnChildEffects
      {
        std::vector<std::uint8_t> flags;
        std::vector<std::uint8_t> types;
        bool SpawnChild(std::uint8_t _aiFlag, Elite::ShipType _type) override
        {
          flags.push_back(_aiFlag);
          types.push_back(Elite::Byte(_type));
          return true;
        }
      };

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(sfs1, Cpu6502::TrapExit::SetCarry);

      // ---- SPIN2 on its own --------------------------------------------------------------------
      std::uint32_t placed = 0;
      for (std::uint32_t count = 0; count < 256; ++count)
      {
        for (const std::uint8_t type : {std::uint8_t{3}, std::uint8_t{5}, std::uint8_t{17}})
        {
          // `oh` is a bare `RTS`, so a count of zero hands the caller's own carry straight back --
          // which only a sweep that varies it can see.
          const bool carryIn = (count & 1u) != 0u;
          cpu.ClearTrapHits();
          cpu.memory[cnt] = 0xEEu;
          cpu.a = static_cast<std::uint8_t>(count);
          cpu.x = type;
          cpu.z = (count == 0u); // what the caller's own `AND` has just left behind
          cpu.c = carryIn;

          const Elite::Testing::RunResult run = cpu.CallSubroutine(spin2, 20'000);
          Assert::IsTrue(run.completed, L"SPIN2 returned");

          Recorder effects;
          const bool exit = Elite::SpawnItems(effects, Elite::TypeOf(type), static_cast<std::uint8_t>(count), carryIn);

          const std::wstring where = WidenText("SPIN2(count " + std::to_string(count) + ", type " + std::to_string(type) + ")");

          Assert::AreEqual<std::size_t>(cpu.trapHits.size(), effects.types.size(), (where + L": how many were spawned").c_str());
          for (std::size_t hit = 0; hit < cpu.trapHits.size(); ++hit)
          {
            Assert::AreEqual(cpu.trapHits[hit].a, effects.flags[hit], (where + L": the AI flag of #" + std::to_wstring(hit)).c_str());
            Assert::AreEqual(cpu.trapHits[hit].x, effects.types[hit], (where + L": the type of #" + std::to_wstring(hit)).c_str());
          }
          // `CNT` is `SPIN2`'s loop counter and its own since M2-c-3. How many times it went round
          // is what the seam above records, spawn for spawn.
          Assert::AreEqual(cpu.c, exit, (where + L": the exit carry").c_str());

          placed += static_cast<std::uint32_t>(cpu.trapHits.size());
        }
      }
      Assert::IsTrue(placed > 0u, L"SPIN2 actually spawned things");

      // ---- SPIN, over every blueprint this build carries ----------------------------------------
      std::set<std::size_t> counts;
      std::uint32_t rolled = 0;
      std::uint32_t refused = 0;
      std::uint32_t cappedByBlueprint = 0;

      for (std::uint8_t type = 1; type <= Elite::SHIP_TYPE_COUNT; ++type)
      {
        const Elite::Blueprint* blueprint = Elite::BlueprintOf(Elite::TypeOf(type));
        if (blueprint == nullptr)
        {
          continue;
        }

        for (std::uint32_t seed = 0; seed < 12; ++seed)
        {
          for (const bool carry : {false, true})
          {
            std::array<std::uint8_t, 4> bytes{};
            std::uint32_t state = 0x1F3A55C7u ^ (seed * 0x9E3779B9u) ^ (type * 131u);
            for (std::size_t byte = 0; byte < bytes.size(); ++byte)
            {
              state = state * 1103515245u + 12345u;
              bytes[byte] = static_cast<std::uint8_t>(state >> 19);
              cpu.memory[static_cast<std::uint16_t>(rand + byte)] = bytes[byte];
            }

            cpu.ClearTrapHits();
            cpu.memory[cnt] = 0xEEu;
            cpu.memory[xx0] = static_cast<std::uint8_t>(blueprint->address & 0xFFu);
            cpu.memory[static_cast<std::uint16_t>(xx0 + 1)] = static_cast<std::uint8_t>(blueprint->address >> 8);
            cpu.y = type;
            cpu.c = carry;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(spin, 20'000);
            Assert::IsTrue(run.completed, L"SPIN returned");

            Recorder effects;
            Elite::Rng rng;
            rng.SetState(bytes);
            const bool exit = Elite::SpawnDebris(rng, effects, *blueprint, Elite::TypeOf(type), carry);

            const std::wstring where = WidenText("SPIN(type " + std::to_string(type) + ", seed " + std::to_string(seed) + ", carry " +
                                                 std::to_string(carry ? 1 : 0) + ")");

            Assert::AreEqual<std::size_t>(cpu.trapHits.size(), effects.types.size(), (where + L": how many were spawned").c_str());
            for (std::size_t hit = 0; hit < cpu.trapHits.size(); ++hit)
            {
              Assert::AreEqual(cpu.trapHits[hit].a, effects.flags[hit], (where + L": the AI flag of #" + std::to_wstring(hit)).c_str());
              Assert::AreEqual(cpu.trapHits[hit].x, effects.types[hit], (where + L": the type of #" + std::to_wstring(hit)).c_str());
            }
            // `CNT` is `SPIN2`'s own since M2-c-3; the seam records every spawn it made.
            Assert::AreEqual(cpu.c, exit, (where + L": the exit carry").c_str());
            for (std::size_t byte = 0; byte < bytes.size(); ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(rand + byte)], rng.State()[byte],
                               (where + L": RAND+" + std::to_wstring(byte)).c_str());
            }

            counts.insert(cpu.trapHits.size());
            rolled += cpu.trapHits.empty() ? 0u : 1u;
            refused += cpu.trapHits.empty() ? 1u : 0u;
            if (!cpu.trapHits.empty() && cpu.trapHits.size() != (type & 0x0Fu))
            {
              ++cappedByBlueprint;
            }
          }
        }
      }

      Assert::IsTrue(rolled > 0u, L"some rolls dropped cargo");
      Assert::IsTrue(refused > 0u, L"and some dropped none");
      Assert::IsTrue(counts.size() > 3u, L"and the blueprints did not all cap it the same way");

      // Without this the `AND (XX0),Y` could be dropped entirely and the sweep would still pass on
      // any build whose blueprints all have the low nibble of byte 0 set.
      Assert::IsTrue(cappedByBlueprint > 0u, L"and at least one blueprint held the count down");
    }
  };

  TEST_CLASS(TheLoopArithmetic)
  {
  public:
    /*
     * 6502: SHD, which falls into DENGY -- and the fall-through is the whole finding.
     *
     * Both are swept exhaustively in the shield AND across the energy banks, because `SHD` is not
     * the saturating increment its four instructions look like: anything below 255 is incremented
     * and then PAYS a unit of energy, and only a full shield escapes without one (§6.83). A port
     * that stopped at the `BEQ` would agree on every shield value and be wrong about the banks
     * every time.
     *
     * `DENGY`'s own answer is the flag its `PHP` saved, from the DECREMENT rather than from the
     * `INC` that undoes it -- so the caller hears "the banks hit zero" on the pass where they are
     * put back to one.
     */
    TEST_METHOD(TheShieldAndTheBanksMatchSHDAndDENGY)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t shd = oracle.Label("SHD");
      const std::uint16_t dengy = oracle.Label("DENGY");
      const std::uint16_t energy = oracle.Label("ENERGY");

      Cpu6502 cpu = oracle.Fresh();

      std::uint32_t compared = 0;
      std::uint32_t paid = 0;
      std::uint32_t free = 0;
      std::uint32_t emptied = 0;

      for (std::uint32_t banks = 0; banks < 256u; ++banks)
      {
        // ---- DENGY on its own ------------------------------------------------------------------
        {
          cpu.memory[energy] = static_cast<std::uint8_t>(banks);
          Assert::IsTrue(cpu.CallSubroutine(dengy, 200).completed, L"DENGY returned");

          Elite::FlightStatus status;
          status.energy = static_cast<std::uint8_t>(banks);
          const bool ours = Elite::DrainEnergy(status);

          const std::wstring where = WidenText("DENGY(" + std::to_string(banks) + ")");
          Assert::AreEqual(cpu.memory[energy], status.energy, (where + L": ENERGY").c_str());
          Assert::AreEqual(cpu.z, ours, (where + L": the flag PHP saved").c_str());
          emptied += ours ? 1u : 0u;
        }

        // ---- SHD, which spends it ----------------------------------------------------------------
        for (const std::uint8_t shield : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{128}, std::uint8_t{254}, std::uint8_t{255}})
        {
          cpu.memory[energy] = static_cast<std::uint8_t>(banks);
          cpu.x = shield;
          Assert::IsTrue(cpu.CallSubroutine(shd, 200).completed, L"SHD returned");

          Elite::FlightStatus status;
          status.energy = static_cast<std::uint8_t>(banks);
          const std::uint8_t ours = Elite::RechargeShield(status, shield);

          const std::wstring where = WidenText("SHD(shield " + std::to_string(shield) + ", banks " + std::to_string(banks) + ")");
          Assert::AreEqual(cpu.x, ours, (where + L": the shield").c_str());
          Assert::AreEqual(cpu.memory[energy], status.energy, (where + L": ENERGY").c_str());

          if (status.energy == banks)
          {
            ++free;
          }
          else
          {
            ++paid;
          }
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(256u * 5u, compared, L"the whole sweep ran");
      Assert::IsTrue(paid > 0u, L"most shields cost a unit of energy");
      Assert::IsTrue(free > 0u, L"and a full one costs nothing");
      Assert::AreEqual<std::uint32_t>(1u, emptied, L"exactly one starting value empties the banks");
    }

    /// 6502: FAROF and FAROF2 -- every limit against a spread of ship positions, and the three
    /// compares in the order the routine makes them.
    TEST_METHOD(TheRangeTestMatchesFAROF)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t farof = oracle.Label("FAROF");
      const std::uint16_t farof2 = oracle.Label("FAROF2");
      const std::uint16_t inwk = oracle.Label("INWK");

      Cpu6502 cpu = oracle.Fresh();

      std::uint32_t compared = 0;
      std::uint32_t inside = 0;

      const std::vector<std::uint8_t> AXES = {0, 1, 223, 224, 225, 255};

      for (const std::uint8_t x : AXES)
      {
        for (const std::uint8_t y : AXES)
        {
          for (const std::uint8_t z : AXES)
          {
            Elite::Ship work{};
            work.x.hi = x;
            work.y.hi = y;
            work.z.hi = z;
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = work.ToBytes()[byte];
            }

            // `FAROF` first, which is `LDA #224` and then the body.
            Assert::IsTrue(cpu.CallSubroutine(farof, 200).completed, L"FAROF returned");
            const std::wstring where = WidenText("FAROF(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
            Assert::AreEqual(cpu.c, Elite::WithinLoopRange(work), where.c_str());

            // Then the body on its own, at limits either side of the one `FAROF` chooses.
            for (const std::uint8_t limit : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{192}, std::uint8_t{224}, std::uint8_t{255}})
            {
              cpu.a = limit;
              Assert::IsTrue(cpu.CallSubroutine(farof2, 200).completed, L"FAROF2 returned");
              const bool ours = Elite::WithinRange(work, limit);

              Assert::AreEqual(cpu.c, ours, (where + L" at limit " + std::to_wstring(limit)).c_str());
              inside += ours ? 1u : 0u;
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(6u * 6u * 6u * 5u, compared, L"the whole sweep ran");
      Assert::IsTrue(inside > 0u, L"some ships were inside the box");
      Assert::IsTrue(inside < compared, L"and some outside it");
    }

    /*
     * 6502: HITCH -- five ways to say no, then the arithmetic.
     *
     * Swept over every blueprint this build carries, because the answer's last step compares
     * against the blueprint's own target area at `(XX0),0` and `(XX0),1` -- a port that used a
     * fixed radius would agree on the Cobra and miss the Thargoid.
     *
     * The overflow path is covered on purpose: `BCS TN10` reaches a `CLC / RTS` of its own, which
     * is the same answer as the near misses by a different route, and a sum that big needs offsets
     * near 255 to produce.
     */
    TEST_METHOD(TheHitTestMatchesHITCH)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t hitch = oracle.Label("HITCH");
      const std::uint16_t inwk = oracle.Label("INWK");
      const std::uint16_t xx0 = oracle.Label("XX0");
      const std::uint16_t type = oracle.Label("TYPE");

      Cpu6502 cpu = oracle.Fresh();

      std::uint32_t compared = 0;
      std::uint32_t hits = 0;

      const std::vector<std::uint8_t> OFFSETS = {0, 1, 8, 20, 63, 128, 200, 255};

      for (std::uint8_t shipType = 1; shipType <= Elite::SHIP_TYPE_COUNT; ++shipType)
      {
        const Elite::Blueprint* blueprint = Elite::BlueprintOf(Elite::TypeOf(shipType));
        if (blueprint == nullptr)
        {
          continue;
        }

        for (const std::uint8_t across : OFFSETS)
        {
          for (const std::uint8_t down : OFFSETS)
          {
            for (const std::uint8_t behind : {std::uint8_t{0}, std::uint8_t{1}})
            {
              for (const std::uint8_t exploding : {std::uint8_t{0}, std::uint8_t{0x20}})
              {
                Elite::Ship work{};
                work.x.lo = across;
                work.y.lo = down;
                work.z.sgn = behind;
                work.state = exploding;

                for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
                {
                  cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = work.ToBytes()[byte];
                }
                cpu.memory[xx0] = static_cast<std::uint8_t>(blueprint->address & 0xFFu);
                cpu.memory[static_cast<std::uint16_t>(xx0 + 1)] = static_cast<std::uint8_t>(blueprint->address >> 8);
                cpu.memory[type] = shipType;

                Assert::IsTrue(cpu.CallSubroutine(hitch, 5'000).completed, L"HITCH returned");

                const bool ours = Elite::IsHit(work, *blueprint, Elite::TypeOf(shipType));

                const std::wstring where =
                  WidenText("HITCH(type " + std::to_string(shipType) + ", x " + std::to_string(across) + ", y " + std::to_string(down) +
                            ", z-sign " + std::to_string(behind) + ", exploding " + std::to_string(exploding) + ")");
                Assert::AreEqual(cpu.c, ours, where.c_str());

                hits += ours ? 1u : 0u;
                ++compared;
              }
            }
          }
        }
      }

      Assert::IsTrue(compared > 3'000u, L"the sweep is worth its name");
      Assert::IsTrue(hits > 0u, L"and some of them were hits");
    }
  };

  /*
   * The frame's opening (slice 3d-d-iii-b, parts 1 to 3).
   *
   * `M%` is entered by `CallSubroutine` and never returns: it runs into part 4 at `MA3`, or leaves
   * for `ESCAPE`. Both are trapped, so the trap's own RTS lands on the fake return address and the
   * run finishes where the port's `LoopOutcome` says it does.
   *
   * Five of the routines it reaches are seams and are trapped rather than run: `MVTRIBS` cannot be
   * (it is entered by `JMP` and leaves by `JMP NOMVETR`, so a trap's RTS would unwind the run) and
   * is patched with a jump straight back instead. Everything else -- `ABORT`, `MSBAR`, `WARP`,
   * `ECBLB2`, `LASLI`, `MESS` -- is already ported and runs for real, so the whole-canvas compare
   * covers what they draw.
   */
  namespace
  {
    /// Flat memory the game never touches, where a patched exit counts its own arrivals. Three of
    /// them, for `DEATH`, `DOENTRY` and `ESCAPE` in that order.
    constexpr std::uint16_t EXIT_PROBE = 0xFFF1;

    /// The first byte of the arena that is heap rather than ship block -- `Mirror` owns everything
    /// below it, because in the original the two are one region and in the port they are two.
    constexpr std::uint16_t HEAP_START = Elite::SHIP_BLOCK_BASE + Elite::MAX_SHIPS * Elite::SHIP_BLOCK_SIZE;

    /// Where the oracle keeps what the frame's opening reads and writes, beyond the shared `Where`.
    struct LoopWhere
    {
      std::uint16_t jstx, jsty, autoByte, damp, djd, jstk;
      std::uint16_t alpha, alp2Next, bet2, bet2Next, delt4;
      std::uint16_t las, lasct, lasx, lasy, msar, mstg, ecmp, moonflower;
      std::uint16_t klo, tp, mch, messxc, gntmp, energy;

      std::uint16_t ma3, ma18, escape, startbd, stopbd;
      std::uint16_t mainLoop, death, doentry, doexp, planet, sfs1;
      std::uint16_t setl1, dovdu19, slsp;

      explicit LoopWhere(const OracleImage& _oracle)
      {
        jstx = _oracle.Label("JSTX");
        jsty = _oracle.Label("JSTY");
        autoByte = _oracle.Label("auto");
        damp = _oracle.Label("DAMP");
        djd = _oracle.Label("DJD");
        jstk = _oracle.Label("JSTK");
        alpha = _oracle.Label("ALPHA");
        alp2Next = static_cast<std::uint16_t>(_oracle.Label("ALP2") + 1u);
        bet2 = _oracle.Label("BET2");
        bet2Next = static_cast<std::uint16_t>(_oracle.Label("BET2") + 1u);
        delt4 = _oracle.Label("DELT4");
        las = _oracle.Label("LAS");
        lasct = _oracle.Label("LASCT");
        lasx = _oracle.Label("LASX");
        lasy = _oracle.Label("LASY");
        msar = _oracle.Label("MSAR");
        mstg = _oracle.Label("MSTG");
        ecmp = _oracle.Label("ECMP");
        moonflower = _oracle.Label("moonflower");
        klo = _oracle.Label("KLO");
        tp = _oracle.Label("TP");
        mch = _oracle.Label("MCH");
        messxc = _oracle.Label("messXC");
        gntmp = _oracle.Label("GNTMP");
        energy = _oracle.Label("ENERGY");

        ma3 = _oracle.Label("MA3");
        escape = _oracle.Label("ESCAPE");
        startbd = _oracle.Label("startbd");
        stopbd = _oracle.Label("stopbd");
        mainLoop = _oracle.Label("M%");
        ma18 = _oracle.Label("MA18");
        death = _oracle.Label("DEATH");
        doentry = _oracle.Label("DOENTRY");
        doexp = _oracle.Label("DOEXP");
        planet = _oracle.Label("PLANET");
        sfs1 = _oracle.Label("SFS1");
        setl1 = _oracle.Label("SETL1");
        dovdu19 = _oracle.Label("DOVDU19");
        slsp = _oracle.Label("SLSP");
      }
    };

    /*
     * The flight loop's own seams, recording into the universe's sound list.
     *
     * One list, because `NOISE` is one routine: the loop reaches it through `FlightLoopEffects` and
     * `WARP` reaches it through `ViewEffects`, and a frame that boops for a refused warp and then
     * whooshes for a missile has to compare in that order against the oracle's trap hits.
     */
    struct RecordingLoop final : Elite::FlightLoopEffects
    {
      struct Pitched
      {
        std::uint8_t effect, sustain, frequency;
      };


      /// The universe's carry list, not this object's: the 6502 has ONE `NOISE` and the port reaches
      /// it through two interfaces, so a comparison against one list needs both to write to it.
      std::uint32_t musicStarts = 0;
      std::uint32_t musicStops = 0;

      /// The bubble the sounds are recorded beside. `FRS1` and `ANGRY` were seams on this object
      /// until M3-b-1d and are calls into `GameLogic` now, so what they write is compared through
      /// the ship blocks like everything else.
      Universe& universe;

      explicit RecordingLoop(Universe& _universe) noexcept
        : universe(_universe)
      {
      }

      void StartDockingMusic() override
      {
        ++musicStarts;
      }
      void StopDockingMusic() override
      {
        ++musicStops;
      }
      bool SpawnChild(std::uint8_t _aiFlag, Elite::ShipType _type) override
      {
        children.push_back({_aiFlag, Elite::Byte(_type)});
        return childSucceeds;
      }

      struct Child
      {
        std::uint8_t aiFlag, type;
      };

      std::vector<Child> children;
      bool childSucceeds = true;
    };

    /// What `MVEIT` and `LL9` reach that this slice does not build.
    struct RecordingUniverse final : Elite::ShipDrawEffects
    {
      std::uint32_t planets = 0;
      std::uint32_t explosions = 0;

      void DrawPlanetOrSun() override
      {
        ++planets;
      }
      void DrawExplosion() override
      {
        ++explosions;
      }
    };

    /// Everything one frame needs that the shared `Universe` does not carry.
    struct Frame
    {
      Universe universe; ///< every byte of it, since M3-a -- the controls, the keys, the burst,
                         ///< the heap, the clipper's flag, the projection and the axes were eight
                         ///< members here while `FlightLoop` held references to them
      RecordingUniverse outside;
      RecordingLoop effects{universe};

      explicit Frame(std::uint32_t _seed)
      {
        Seed(universe, _seed);

        /*
         * 6502: TRIBCT -- ZERO here, and `Seed` leaves it at 90 (slice 4d-a).
         *
         * `MVTRIBS` writes the six Trumble sprites' coordinate registers, and in the oracle's flat
         * image those registers ARE `XX21`, the ship blueprint pointer table (§6.108). On the real
         * machine `SETL1` banks the video chip in over that RAM and the table survives; there is no
         * bank here, so a frame that moves a Trumble and then draws a ship reads a corrupted
         * blueprint on one side of the comparison and a good one on the other. Every fixture that
         * wants Trumbles has to stop before the ships, and `TheControlRatesMatchM` is the one that
         * does.
         */
        universe.trumbles.count = 0u;

        // 6502: LSO -- the station draws into the SUN's heap, which lives in the universe (§6.112).
        universe.LendSunHeap();

        /*
         * A message already up, and a REAL one.
         *
         * `Seed` leaves `MCH` at zero, and a message that jams sends `MESS` down `me1` to erase
         * whatever is showing -- so a zero would be printed, and `TT27` opens `TAX / BEQ csh`, which
         * prints the player's cash. The game cannot reach that: `MCH` is written only by `MESS`
         * itself, and the erase only runs when `DLY` is non-zero, which only `MESS` makes it.
         */
        universe.message.token = 101u;
        universe.message.column = 9u;
        universe.message.append = 1u;
        universe.message.delay = 12u;

        /*
         * 6502: XX0 -- a REAL blueprint, because zero is not a state the game reaches.
         *
         * Part 4 leaves `XX0` alone for the planet and the sun, so a body inherits whatever the last
         * ship put there (§6.90) -- and by the time the flight loop runs, something always has. With
         * zero the game reads `(XX0),15` out of its own zero page and the port reads a guarded zero,
         * which is a disagreement about an address neither would ever form.
         */
        universe.flight.blueprint = Elite::BlueprintOf(Elite::ShipType::CobraMk3);
        universe.screen.upperBitmapMode = 0xC0u;
        universe.status.laserCount = 0u;
        universe.status.laserPower = 0u;
        universe.status.missileArmed = 0u;
        universe.status.ecmOurs = 0u;
        universe.bubble.missileTarget = 0xFFu;
        universe.flight.delt4 = 0u;
        universe.flight.delt4Next = 0u;
        universe.control.roll = 128u;
        universe.control.pitch = 128u;
      }
    };

    /// The port's universe into the oracle, for everything past what `Mirror` covers.
    void MirrorFrame(const Frame& _frame, Cpu6502& _cpu, const Where& _at, const LoopWhere& _loop)
    {
      const Universe& universe = _frame.universe;

      for (std::size_t slot = 0; slot < _frame.universe.keys.size(); ++slot)
      {
        _cpu.memory[static_cast<std::uint16_t>(_loop.klo + slot)] = _frame.universe.keys[slot];
      }

      _cpu.memory[_loop.jstx] = _frame.universe.control.roll;
      _cpu.memory[_loop.jsty] = _frame.universe.control.pitch;
      _cpu.memory[_loop.autoByte] = _frame.universe.control.dockingComputer;
      _cpu.memory[_loop.damp] = _frame.universe.options.dampingDisabled;
      _cpu.memory[_loop.djd] = _frame.universe.options.recentreDisabled;
      _cpu.memory[_loop.jstk] = _frame.universe.options.joystick;

      _cpu.memory[_loop.alpha] = universe.flight.alpha;
      _cpu.memory[_loop.alp2Next] = universe.flight.alp2Next;
      _cpu.memory[_loop.bet2] = universe.flight.bet2;
      _cpu.memory[_loop.bet2Next] = universe.flight.bet2Next;
      _cpu.memory[_loop.delt4] = universe.flight.delt4;
      _cpu.memory[static_cast<std::uint16_t>(_loop.delt4 + 1u)] = universe.flight.delt4Next;

      _cpu.memory[_loop.las] = universe.status.laserPower;
      _cpu.memory[_loop.lasct] = universe.status.laserCount;
      _cpu.memory[_loop.msar] = universe.status.missileArmed;
      _cpu.memory[_loop.mstg] = universe.bubble.missileTarget;
      _cpu.memory[_loop.ecmp] = universe.status.ecmOurs;
      _cpu.memory[_loop.moonflower] = universe.screen.upperBitmapMode;

      _cpu.memory[_loop.lasx] = _frame.universe.burst.x;
      _cpu.memory[_loop.lasy] = _frame.universe.burst.y;
    }

    /// Every byte the frame's opening can write, beyond what `CompareState` already covers.
    void CompareFrame(const Cpu6502& _cpu, const Frame& _frame, const LoopWhere& _loop, const std::wstring& _context)
    {
      const Universe& universe = _frame.universe;

      auto same = [&](std::uint16_t _address, std::uint8_t _ours, const wchar_t* _name)
      { Assert::AreEqual(_cpu.memory[_address], _ours, (_context + L": " + _name).c_str()); };

      same(_loop.jstx, _frame.universe.control.roll, L"JSTX");
      same(_loop.jsty, _frame.universe.control.pitch, L"JSTY");
      same(_loop.autoByte, _frame.universe.control.dockingComputer, L"auto");

      same(_loop.alpha, universe.flight.alpha, L"ALPHA");
      same(_loop.alp2Next, universe.flight.alp2Next, L"ALP2+1");
      same(_loop.bet2, universe.flight.bet2, L"BET2");
      same(_loop.bet2Next, universe.flight.bet2Next, L"BET2+1");
      same(_loop.delt4, universe.flight.delt4, L"DELT4");
      same(static_cast<std::uint16_t>(_loop.delt4 + 1u), universe.flight.delt4Next, L"DELT4+1");

      same(_loop.las, universe.status.laserPower, L"LAS");
      same(_loop.lasct, universe.status.laserCount, L"LASCT");
      same(_loop.lasx, _frame.universe.burst.x, L"LASX");
      same(_loop.lasy, _frame.universe.burst.y, L"LASY");
      same(_loop.msar, universe.status.missileArmed, L"MSAR");
      same(_loop.mstg, universe.bubble.missileTarget, L"MSTG");
      same(_loop.ecmp, universe.status.ecmOurs, L"ECMP");
      same(_loop.moonflower, universe.screen.upperBitmapMode, L"moonflower");

      same(_loop.mch, universe.message.token, L"MCH");
      same(_loop.messxc, universe.message.column, L"messXC");
      same(_loop.gntmp, universe.status.laserTemperature, L"GNTMP");
      same(_loop.energy, universe.status.energy, L"ENERGY");

      for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_loop.tp + index)], universe.commander.ToBytes()[index],
                         (_context + L": commander byte " + std::to_wstring(index)).c_str());
      }
    }

    /*
     * A bubble the per-ship loop has something to do with: the planet, the sun, and three ships.
     *
     * Every coordinate high byte is `_distance`, so one number moves the whole bubble towards the
     * player -- which is what parts 7 to 12 branch on. `Seed`'s own blocks are random, and random is
     * exactly wrong here: a ship at a random distance is almost always too far to do anything.
     */
    void PopulateBubble(Frame& _frame, std::uint8_t _distance, std::uint8_t _state, bool _empty)
    {
      Universe& universe = _frame.universe;

      for (std::size_t slot = 0; slot < universe.bubble.slots.size(); ++slot)
      {
        universe.bubble.slots[slot] = 0u;
      }
      for (std::size_t type = 0; type < universe.bubble.counts.size(); ++type)
      {
        universe.bubble.counts[type] = 0u;
      }

      if (_empty)
      {
        universe.bubble.junk = 0u;
        return;
      }

      const std::uint8_t TYPES[] = {128u, 129u, 3u, 5u, 11u};

      for (std::size_t slot = 0; slot < 5u; ++slot)
      {
        universe.bubble.slots[slot] = TYPES[slot];
        if (TYPES[slot] < 34u)
        {
          ++universe.bubble.counts[TYPES[slot]];
        }

        Elite::Ship& block = universe.bubble.blocks[slot];
        std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = block.ToBytes();
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          shipBytes[byte] = 0u;
        }
        block = Elite::Ship::FromBytes(shipBytes);

        block.x.hi = _distance; // x high
        block.y.hi = _distance; // y high

        /*
         * The z high byte separates the ships EXCEPT at zero, where it must not: parts 7 to 12 branch
         * on all three high bytes being zero together, so spreading them out would mean no case in
         * the sweep ever collided with anything.
         */
        block.z.hi = (_distance == 0u) ? std::uint8_t{0} : static_cast<std::uint8_t>(_distance + slot);
        block.nose.z.hi = 20u; // pitch counter
        block.speed = 20u; // speed
        block.state = _state;
        block.ai = 0u;    // no AI, so `TACTICS` is not reached
        block.heap = Elite::HeapOffset::FromAddress(0x0C00u); // the heap pointer's high byte
        block.energy = 60u;   // energy
      }

      universe.bubble.junk = 0u;
    }

    /*
     * Run one frame on both sides and compare everything.
     *
     * The marker is 0x1D as it is in the screen tests, so "drew nothing" and "drew a zero" stay
     * different answers across the whole bitmap.
     */
    /*
     * Which part of `M%` a comparison runs.
     *
     * `Opening` stops at `MA3`, which is where part 3 falls into part 4; `Ships` starts there and
     * stops at `MA18`; `Tail` starts at `MA18` and runs to the end; `Whole` runs the lot. The three
     * pieces are separately entered because the loop's failures localise badly otherwise -- a wrong
     * byte in part 15 shows up as a screen difference after two hundred thousand instructions.
     */
    enum class Reach
    {
      Opening,
      Ships,
      Tail,
      Whole,
    };

    /// `_seed` runs after the mirror and before the call: the one hook for a byte `UniverseImage`
    /// does not carry, which since M2-c is `MathWorkspace`'s two (the altitude's `Q`, and `MV40`'s
    /// `K2`). Everything else in the frame goes through the image.
    void CompareFrames(Frame& _frame, const OracleImage& _oracle, const Where& _at, const LoopWhere& _loop, const std::wstring& _context,
                       Reach _reach = Reach::Opening, const std::function<void(Cpu6502&)>& _seed = {})
    {
      Cpu6502 cpu = _oracle.Fresh();

      if (_reach == Reach::Opening)
      {
        cpu.AddTrap(_loop.ma3);
      }
      if (_reach == Reach::Opening || _reach == Reach::Ships)
      {
        cpu.AddTrap(_loop.ma18);
      }

      /*
       * The three exits are PATCHED rather than trapped, and the difference matters.
       *
       * A trap returns by popping the stack, which is right for a `JSR`ed routine and wrong for a
       * `JMP` that never comes back: `JMP DEATH` inside `OOPS` is one level down, so a trap there
       * unwinds into the middle of part 10 and the frame carries on. Patching each with a jump to the
       * stop address ends the run where the game's would end, and the `INC` in front of it is what
       * makes which one it took observable.
       */
      const std::uint16_t EXITS[] = {_loop.death, _loop.doentry, _loop.escape};
      for (std::size_t index = 0; index < 3u; ++index)
      {
        const std::uint16_t probe = static_cast<std::uint16_t>(EXIT_PROBE + index);
        cpu.memory[probe] = 0u;
        const std::uint8_t leave[] = {0xEEu, static_cast<std::uint8_t>(probe & 0xFFu), static_cast<std::uint8_t>(probe >> 8), 0x4Cu, 0xF9u,
                                      0xFFu};
        cpu.Load(EXITS[index], leave, sizeof(leave));
      }

      cpu.AddTrap(_loop.doexp);
      cpu.AddTrap(_loop.planet);
      cpu.AddTrap(_loop.setl1);
      cpu.AddTrap(_loop.dovdu19);
      cpu.AddTrap(_loop.sfs1, _frame.effects.childSucceeds ? Cpu6502::TrapExit::SetCarry : Cpu6502::TrapExit::ClearCarry);
      cpu.AddTrap(_loop.startbd);
      cpu.AddTrap(_loop.stopbd);


      /*
       * `MVTRIBS` USED TO BE PATCHED OUT HERE and is not any more (slice 4d-a).
       *
       * It was replaced by `INC probe / JMP NOMVETR` while the port had a seam where the routine
       * should be -- entered by `JMP` and leaving by `JMP NOMVETR`, it cannot be trapped, because a
       * trap's `RTS` would pop the fake return address and end the run mid-frame. The port runs the
       * real routine now, and the real routine takes a random number, so leaving the patch in place
       * would put the two generators one call apart on every frame with a Trumble aboard. What
       * replaces the probe is the sprite bank in `Mirror` and `CompareState`, which says more:
       * whether it was reached AND what it did.
       *
       * A FRAME WITH TRUMBLES ABOARD MUST NOT REACH THE SHIPS. The sprite registers are `XX21` in a
       * flat image (`Where::vic`), so `MVTRIBS` overwrites the blueprint pointers for types 3 to 9
       * on the oracle's side and on nobody else's. `TheControlRatesMatchM` is the only fixture that
       * sets `TRIBCT`, and it runs the frame's head.
       */

      FillScreens(cpu, _frame.universe.canvas, _at.screen, 0x1Du);
      Mirror(_frame.universe, cpu, _at);
      MirrorFrame(_frame, cpu, _at, _loop);

      /*
       * 6502: RAND -- the generator goes in with the frame and is compared on the way out.
       *
       * `Mirror` does not carry it, and until 2026-09-06 no frame here did: a frame that seeds an
       * explosion cloud runs `DORND` four times, the first on the carry `LL9` was reached with, and
       * with the fixture's heap pointers outside the arena the seeds it writes are compared nowhere.
       * The generator's state after the frame is the one place that carry is visible, and the
       * `cs-ll9-carry-hit` mutant is what found the comparison missing.
       */
      for (std::size_t index = 0; index < 4u; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(_at.rand + index)] = _frame.universe.rng.State()[index];
      }

      /*
       * The ship line heap, which the port keeps apart from the blocks and the original does not.
       *
       * `LineHeap`'s arena runs from `K%` to `LS%` and the bottom of it IS the block region, which
       * `Mirror` has just written -- so only the bytes above the last block are the heap's, and
       * mirroring the whole arena would undo the blocks.
       */
      for (std::uint16_t address = HEAP_START; address < Elite::LineHeap::TOP; ++address)
      {
        cpu.memory[address] = _frame.universe.heap.Read(Elite::HeapOffset::FromAddress(address));
      }
      cpu.memory[_loop.slsp] = static_cast<std::uint8_t>(_frame.universe.bubble.heapBottom.Address() & 0xFFu);
      cpu.memory[static_cast<std::uint16_t>(_loop.slsp + 1u)] = static_cast<std::uint8_t>(_frame.universe.bubble.heapBottom.Address() >> 8);

      if (_seed)
      {
        _seed(cpu);
      }

      const std::uint16_t entry = (_reach == Reach::Ships) ? _loop.ma3 : (_reach == Reach::Tail) ? _loop.ma18 : _loop.mainLoop;

      const Elite::Testing::RunResult run = cpu.CallSubroutine(entry, 8'000'000);
      Assert::IsTrue(run.completed, (_context + L": M% reached an exit").c_str());

      Elite::Ports ports = _frame.universe.PortsWith(_frame.outside, _frame.effects, _frame.universe.unused);
      const Elite::LoopOutcome outcome = (_reach == Reach::Ships)   ? Elite::MoveEveryShip(_frame.universe, ports)
                                         : (_reach == Reach::Tail)  ? Elite::EndFlightFrame(_frame.universe, ports)
                                         : (_reach == Reach::Whole) ? Elite::MainFlightLoop(_frame.universe, ports)
                                                                    : Elite::BeginFlightFrame(_frame.universe, ports);

      // ---- the exit ------------------------------------------------------------------------------
      std::uint32_t reachedEnd = 0;
      std::uint32_t escaped = 0;
      std::uint32_t died = 0;
      std::uint32_t docked = 0;
      std::uint32_t starts = 0;
      std::uint32_t stops = 0;

      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        if (hit.address == _loop.ma3 || hit.address == _loop.ma18)
        {
          ++reachedEnd;
        }
        else if (hit.address == _loop.startbd)
        {
          ++starts;
        }
        else if (hit.address == _loop.stopbd)
        {
          ++stops;
        }
      }

      died = cpu.memory[EXIT_PROBE];
      docked = cpu.memory[static_cast<std::uint16_t>(EXIT_PROBE + 1u)];
      escaped = cpu.memory[static_cast<std::uint16_t>(EXIT_PROBE + 2u)];

      Assert::AreEqual<std::uint32_t>(outcome == Elite::LoopOutcome::Escaped ? 1u : 0u, escaped, (_context + L": ESCAPE taken").c_str());
      Assert::AreEqual<std::uint32_t>(outcome == Elite::LoopOutcome::Died ? 1u : 0u, died, (_context + L": DEATH taken").c_str());
      Assert::AreEqual<std::uint32_t>(outcome == Elite::LoopOutcome::Docked ? 1u : 0u, docked, (_context + L": DOENTRY taken").c_str());

      if (_reach == Reach::Opening || _reach == Reach::Ships)
      {
        Assert::AreEqual<std::uint32_t>(
          outcome == Elite::LoopOutcome::Continued ? 1u : 0u, reachedEnd,
          (_context + L": fell through to the next part -- outcome " + std::to_wstring(static_cast<int>(outcome))).c_str());
      }

      /*
       * 6502: NOISE, NOISE2 and NOISEOFF -- and the BUFFER is what compares them since M3-b-2a.
       *
       * All three were trapped on the oracle and recorded on the port, and this was a comparison of
       * two lists: which effect, in what order, with what carry going in. Both machines run the
       * routines now, so what is compared is `sound_variables` -- ten runs of three plus `PULSEW`
       * and `DNOIZ` -- through `CompareState` below, along with everything else the frame touches.
       *
       * IT SUBSUMES §6.118's GAP rather than losing it. The old comparison could only check the
       * carry going INTO four hand-picked effects, because every other call passes a flag through
       * from somewhere the port does not model; the carry coming OUT was never compared at all.
       * `NOISE`'s answer now comes from two buffers that agree byte for byte, and its consequence
       * -- `LASLI` and `OUCH` open a `DORND` on it (§6.86, §6.88) -- lands in `RAND`, which this
       * comparison already carries. Nothing is excluded by name any more.
       */


      Assert::AreEqual(starts, _frame.effects.musicStarts, (_context + L": startbd").c_str());
      Assert::AreEqual(stops, _frame.effects.musicStops, (_context + L": stopbd").c_str());

      // ---- the universe -----------------------------------------------------------------------------
      CompareScreens(cpu, _at.screen, _frame.universe.canvas, 0x1Du, _context);
      CompareState(cpu, _frame.universe, _at, _context);
      CompareFrame(cpu, _frame, _loop, _context);

      /*
       * The heap, ALL of it, and the generator with it.
       *
       * Until 2026-09-06 this stepped over the six bytes at the head of a newly killed ship's run
       * and skipped the generator on any frame that seeded a cloud, on §6.91's belief that the
       * seeding's first `DORND` ran on a carry out of `LOIN` the port could not know. It runs on
       * `EE51`'s, which is a `CMP`'s, and the port derives it now (§6.157) -- so a frame that kills
       * a ship is compared whole, seeds included.
       */
      for (std::uint16_t address = HEAP_START; address < Elite::LineHeap::TOP; ++address)
      {
        Assert::AreEqual(cpu.memory[address], _frame.universe.heap.Read(Elite::HeapOffset::FromAddress(address)),
                         (_context + L": heap byte " + std::to_wstring(address)).c_str());
      }

      const std::uint16_t bottom =
        static_cast<std::uint16_t>(cpu.memory[_loop.slsp] | (cpu.memory[static_cast<std::uint16_t>(_loop.slsp + 1u)] << 8));
      Assert::AreEqual<std::uint32_t>(bottom, _frame.universe.bubble.heapBottom.Address(), (_context + L": SLSP").c_str());

      for (std::size_t index = 0; index < 4u; ++index)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(_at.rand + index)], _frame.universe.rng.State()[index],
                         (_context + L": RAND+" + std::to_wstring(index)).c_str());
      }

      for (std::size_t slot = 0; slot < _frame.universe.bubble.slots.size(); ++slot)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(_at.frin + slot)], _frame.universe.bubble.slots[slot],
                         (_context + L": FRIN " + std::to_wstring(slot)).c_str());
      }
      for (std::size_t type = 0; type < _frame.universe.bubble.counts.size(); ++type)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(_at.many + type)], _frame.universe.bubble.counts[type],
                         (_context + L": MANY " + std::to_wstring(type)).c_str());
      }
    }
  } // namespace

  TEST_CLASS(TheFlightFrameOpening)
  {
  public:
    /*
     * 6502: M% parts 1 and 2 -- the seed stir, the Trumbles and both control rates.
     *
     * The pitch is swept against every roll because of what the roll leaves behind: its magnitude
     * ends `CMP #8 / BCS P%+3 / LSR A` and the pitch's begins `ADC #4` with no `SEC` or `CLC`
     * between them, and `cntr` sets no flags on any of its three paths -- so the four added to the
     * pitch is four or five depending on the roll (§6.85). A port that cleared the carry would be
     * right for every roll of eight or more and wrong for half the rest, which is why the sweep
     * covers both sides of eight and both parities below it.
     */
    TEST_METHOD(TheControlRatesMatchM)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      const std::uint8_t RATES[] = {1u, 2u, 96u, 120u, 127u, 128u, 129u, 136u, 160u, 200u, 254u, 255u};

      std::uint32_t compared = 0;
      std::uint32_t moved = 0;

      for (const std::uint8_t roll : RATES)
      {
        for (const std::uint8_t pitch : RATES)
        {
          for (std::uint8_t mode = 0; mode < 4u; ++mode)
          {
            Frame frame(roll * 7u + pitch * 3u + mode);
            frame.universe.control.roll = roll;
            frame.universe.control.pitch = pitch;
            frame.universe.options.dampingDisabled = ((mode & 1u) != 0u) ? 0xFFu : 0u;
            frame.universe.control.dockingComputer = ((mode & 2u) != 0u) ? 0xFFu : 0u;
            /*
             * 6502: TRIBCT -- half the sweep carries Trumbles and half does not, so part 1's
             * `LDA TRIBCT / BEQ NOMVETR` is exercised both ways. With three of them showing and
             * `MCNT` at zero, `MVTRIBS` moves sprite 0 and takes a random number for it, which is
             * why the generator's state is part of what `CompareState` checks.
             */
            frame.universe.spriteRegistersAreOurs = true;
            frame.universe.trumbles.count = ((roll & 1u) != 0u) ? 0u : 3u;
            for (std::size_t sprite = Elite::FIRST_TRUMBLE_SPRITE; sprite < Elite::SPRITE_COUNT; ++sprite)
            {
              frame.universe.video.x[sprite] = static_cast<std::uint16_t>(0x20u + 0x10u * sprite);
              frame.universe.video.y[sprite] = static_cast<std::uint8_t>(0x40u + 0x10u * sprite);
            }

            const std::wstring where =
              WidenText("M% (JSTX " + std::to_string(roll) + ", JSTY " + std::to_string(pitch) + ", mode " + std::to_string(mode) + ")");
            CompareFrames(frame, oracle, at, loop, where);

            moved += (frame.universe.trumbles.count != 0u) ? 1u : 0u;
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(12u * 12u * 4u, compared, L"the whole sweep ran");
      Assert::IsTrue(moved > 0u, L"the Trumbles moved on some passes");
    }

    /*
     * 6502: M% part 3's speed keys -- and both ends of the range they clamp against.
     *
     * `CMP #40 / BCS MA17` makes forty the ceiling and `DEC DELTA / BNE MA4 / INC DELTA` makes one
     * the floor, so a ship at rest is not a state the keys can produce. Both keys held at once is
     * a real frame: the speed rises and then falls in the same pass.
     */
    TEST_METHOD(TheSpeedKeysMatchM)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      const std::uint8_t SPEEDS[] = {1u, 2u, 20u, 38u, 39u, 40u, 41u, 255u};

      std::uint32_t compared = 0;

      for (const std::uint8_t speed : SPEEDS)
      {
        for (std::uint8_t keys = 0; keys < 4u; ++keys)
        {
          Frame frame(speed + keys * 101u);
          frame.universe.flight.delta = speed;
          frame.universe.keys[Elite::KEY_SPEED_UP] = ((keys & 1u) != 0u) ? 0xFFu : 0u;
          frame.universe.keys[Elite::KEY_SLOW_DOWN] = ((keys & 2u) != 0u) ? 0xFFu : 0u;

          const std::wstring where = WidenText("M% (DELTA " + std::to_string(speed) + ", keys " + std::to_string(keys) + ")");
          CompareFrames(frame, oracle, at, loop, where);
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(8u * 4u, compared, L"the whole sweep ran");
    }

    /*
     * 6502: M% part 3's missile keys, and the branch that skips five others.
     *
     * `.MA25 LDA KY16 / BEQ MA24 / LDA MSTG / BMI MA64 / JSR FRMIS` -- pressing "M" with no lock
     * jumps clear over the energy bomb, the docking-computer cancel, the escape pod, the warp and
     * the E.C.M. Every one of those five is held down in the sweep so that the skip is visible as
     * five things NOT happening rather than as one branch not taken.
     */
    TEST_METHOD(TheMissileKeysMatchM)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      struct Case
      {
        const char* what;
        std::uint8_t missiles, target, armed;
        bool unarm, arm, fire, spawns;
      };

      const std::vector<Case> CASES = {
        {"nothing pressed", 3, 0xFF, 0, false, false, false, true},
        {"unarm with three on the rail", 3, 0xFF, 0xFF, true, false, false, true},
        {"unarm with an empty rail", 0, 0xFF, 0xFF, true, false, false, true},
        {"unarm with one left", 1, 0xFF, 0xFF, true, false, false, true},
        {"arm with three on the rail", 3, 0xFF, 0, false, true, false, true},
        {"arm with an empty rail", 0, 0xFF, 0, false, true, false, true},
        {"arm while already locked", 3, 2, 0, false, true, false, true},
        {"arm and unarm together", 3, 0xFF, 0xFF, true, true, false, true},
        {"fire with no lock -- skips five keys", 3, 0xFF, 0xFF, false, false, true, true},
        {"fire with a lock, room to spawn", 3, 2, 0xFF, false, false, true, true},
        {"fire with a lock, bubble full", 3, 2, 0xFF, false, false, true, false},
        {"fire with a lock and one missile", 1, 1, 0xFF, false, false, true, true},
        {"fire not pressed, lock held", 3, 2, 0xFF, false, false, false, true},
      };

      std::uint32_t skipped = 0;
      std::uint32_t jammed = 0;

      for (const Case& item : CASES)
      {
        Frame frame(0x31u);
        frame.universe.commander.missiles = item.missiles;
        frame.universe.bubble.missileTarget = item.target;
        frame.universe.status.missileArmed = item.armed;
        /*
         * "The bubble is full" is now a FULL BUBBLE rather than a trap answering `BCC`.
         *
         * `FRS1` was trapped on the oracle with its carry chosen by the case and answered on the
         * port by a seam that returned the same bool. M3-b-1d took the seam away, so `NWSHP`
         * decides on both sides -- and the only way to make it refuse is to leave it no slot.
         * `Seed` fills three; this fills the rest with the type already in slot 2.
         */
        if (!item.spawns)
        {
          for (std::size_t slot = 3; slot < Elite::MAX_SHIPS; ++slot)
          {
            frame.universe.bubble.slots[slot] = frame.universe.bubble.slots[2];
            ++frame.universe.bubble.counts[frame.universe.bubble.slots[2]];
          }
        }

        frame.universe.keys[Elite::KEY_UNARM_MISSILE] = item.unarm ? 0xFFu : 0u;
        frame.universe.keys[Elite::KEY_ARM_MISSILE] = item.arm ? 0xFFu : 0u;
        frame.universe.keys[Elite::KEY_FIRE_MISSILE] = item.fire ? 0xFFu : 0u;

        // The five keys `BMI MA64` jumps over, all held, on every case.
        frame.universe.keys[Elite::KEY_ENERGY_BOMB] = 0xFFu;
        frame.universe.keys[Elite::KEY_CANCEL_DOCKING] = 0xFFu;
        frame.universe.keys[Elite::KEY_ESCAPE_POD] = 0xFFu;
        frame.universe.keys[Elite::KEY_ECM] = 0xFFu;
        frame.universe.commander.energyBomb = 1u;
        frame.universe.commander.escapePod = 0u; // or the frame would end at ESCAPE
        frame.universe.commander.ecm = 0xFFu;
        frame.universe.control.dockingComputer = 0xFFu;

        const std::wstring where = WidenText(std::string("M% (") + item.what + ")");
        CompareFrames(frame, oracle, at, loop, where);

        if (item.fire && (item.target & 0x80u) != 0u)
        {
          ++skipped;
          Assert::AreEqual<std::uint32_t>(0u, frame.effects.musicStops, (where + L": the cancel key was skipped").c_str());
          Assert::AreEqual<std::uint8_t>(1u, frame.universe.commander.energyBomb,
                                         (where + L": and so was the bomb").c_str());
        }
        if (item.fire && (item.target & 0x80u) == 0u && !item.spawns)
        {
          ++jammed;

          /*
           * AND THE JAM IS OBSERVED rather than merely counted (M3-b-1d).
           *
           * The case used to be "the trap answered `BCC`", which cannot stop being true; it is
           * "`NWSHP` found no slot" now, which can -- a fixture that quietly stopped filling the
           * bubble would spawn the missile and this loop would still tick over. `FR1` gives up
           * before `DEC NOMSL`, so the count still standing is what says the rail kept it.
           */
          Assert::AreEqual(item.missiles, frame.universe.commander.missiles, (where + L": the missile stayed on the rail").c_str());
        }
      }

      Assert::IsTrue(skipped > 0u, L"the skip was reached");
      Assert::IsTrue(jammed > 0u, L"and a jammed missile too");
    }

    /*
     * 6502: M% part 3's other keys -- the bomb, the docking computer, the pod, the warp, the E.C.M.
     *
     * The docking-computer case is the one worth the table: `LDA KY19 / AND DKCMP / BEQ MA68 /
     * EOR KLO+&29 / BEQ MA68` reads `KY5` by offset rather than by name, so holding "X" while
     * pressing "C" cancels the request. Nothing in the original source says so.
     */
    TEST_METHOD(TheSecondaryKeysMatchM)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      struct Case
      {
        const char* what;
        std::uint8_t key, fitting, bomb, ecmCountdown, midJump, dockingComputer, pitchUp;
      };

      const std::vector<Case> CASES = {
        {"the bomb, unfitted", Elite::KEY_ENERGY_BOMB, 0, 0, 0, 0, 0, 0},
        {"the bomb, one bit left", Elite::KEY_ENERGY_BOMB, 0, 1, 0, 0, 0, 0},
        {"the bomb, halfway through", Elite::KEY_ENERGY_BOMB, 0, 0x10u, 0, 0, 0, 0},
        {"the bomb, last frame", Elite::KEY_ENERGY_BOMB, 0, 0x80u, 0, 0, 0, 0},
        {"cancel docking, running", Elite::KEY_CANCEL_DOCKING, 0, 0, 0, 0, 0xFFu, 0},
        {"cancel docking, not running", Elite::KEY_CANCEL_DOCKING, 0, 0, 0, 0, 0, 0},
        {"the pod, unfitted", Elite::KEY_ESCAPE_POD, 0, 0, 0, 0, 0, 0},
        {"the pod, fitted", Elite::KEY_ESCAPE_POD, 0xFFu, 0, 0, 0, 0, 0},
        {"the pod, mid-jump", Elite::KEY_ESCAPE_POD, 0xFFu, 0, 0, 0xFFu, 0, 0},
        {"the warp", Elite::KEY_WARP, 0, 0, 0, 0, 0, 0},
        {"the warp, mid-jump", Elite::KEY_WARP, 0, 0, 0, 0xFFu, 0, 0},
        {"E.C.M., unfitted", Elite::KEY_ECM, 0, 0, 0, 0, 0, 0},
        {"E.C.M., fitted and idle", Elite::KEY_ECM, 0xFFu, 0, 0, 0, 0, 0},
        {"E.C.M., already running", Elite::KEY_ECM, 0xFFu, 0, 20u, 0, 0, 0},
        {"docking, unfitted", Elite::KEY_DOCKING_COMPUTER, 0, 0, 0, 0, 0, 0},
        {"docking, fitted", Elite::KEY_DOCKING_COMPUTER, 0xFFu, 0, 0, 0, 0, 0},
        {"docking, with X held", Elite::KEY_DOCKING_COMPUTER, 0xFFu, 0, 0, 0, 0, 0xFFu},
        {"docking, already running", Elite::KEY_DOCKING_COMPUTER, 0xFFu, 0, 0, 0, 0xFFu, 0},
        {"X alone, no docking key", Elite::KEY_PITCH_UP, 0xFFu, 0, 0, 0, 0, 0xFFu},
      };

      std::uint32_t escaped = 0;

      for (const Case& item : CASES)
      {
        Frame frame(0x77u);
        frame.universe.keys[item.key] = 0xFFu;
        frame.universe.keys[Elite::KEY_PITCH_UP] = item.pitchUp;
        frame.universe.commander.energyBomb = item.bomb;
        frame.universe.commander.escapePod = (item.key == Elite::KEY_ESCAPE_POD) ? item.fitting : 0u;
        frame.universe.commander.ecm = (item.key == Elite::KEY_ECM) ? item.fitting : 0u;
        frame.universe.commander.dockingComputer =
          (item.key == Elite::KEY_DOCKING_COMPUTER || item.key == Elite::KEY_PITCH_UP) ? item.fitting : 0u;
        frame.universe.status.ecmCountdown = item.ecmCountdown;
        frame.universe.status.midJump = item.midJump;
        frame.universe.control.dockingComputer = item.dockingComputer;

        const std::wstring where = WidenText(std::string("M% (") + item.what + ")");
        CompareFrames(frame, oracle, at, loop, where);

        escaped += (item.key == Elite::KEY_ESCAPE_POD && item.fitting != 0u && item.midJump == 0u) ? 1u : 0u;
      }

      Assert::AreEqual<std::uint32_t>(1u, escaped, L"exactly one case left through ESCAPE");
    }

    /*
     * 6502: M% part 3's tail -- `DELT4`, and the gun.
     *
     * Four laser types across four views, because the sound is chosen by an `EQUB &2C` that
     * swallows an `LDY` (§6.79) and the countdown by `PLA / BPL ma1 / LDA #0`, so a beam laser
     * gets no countdown and can be held down while a pulse laser cannot. `GNTMP` is swept across
     * the jam at 242 from both sides.
     */
    TEST_METHOD(TheGunMatchesM)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      const std::uint8_t FITTED[] = {0u, Elite::LASER_PULSE, Elite::LASER_BEAM, Elite::LASER_MILITARY, Elite::LASER_POWER_MINING};
      const std::uint8_t HEAT[] = {0u, 100u, 241u, 242u, 243u};
      const std::uint8_t COUNTS[] = {0u, 1u, 7u};

      std::uint32_t compared = 0;
      std::uint32_t fired = 0;

      for (std::uint8_t view = 0; view < 4u; ++view)
      {
        for (const std::uint8_t fitted : FITTED)
        {
          for (const std::uint8_t heat : HEAT)
          {
            for (const std::uint8_t count : COUNTS)
            {
              Frame frame(view * 13u + fitted + heat + count);
              frame.universe.spaceView = view;
              frame.universe.view = 0u;
              frame.universe.status.laserTemperature = heat;
              frame.universe.status.laserCount = count;
              frame.universe.keys[Elite::KEY_FIRE] = 0xFFu;
              for (std::size_t index = 0; index < 4u; ++index)
              {
                frame.universe.commander.lasers[index] = (index == view) ? fitted : 0u;
              }

              const std::wstring where = WidenText("M% (VIEW " + std::to_string(view) + ", LASER " + std::to_string(fitted) + ", GNTMP " +
                                                   std::to_string(heat) + ", LASCT " + std::to_string(count) + ")");
              CompareFrames(frame, oracle, at, loop, where);

              fired += (frame.universe.status.laserPower != 0u) ? 1u : 0u;
              ++compared;
            }
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(4u * 5u * 5u * 3u, compared, L"the whole sweep ran");
      Assert::IsTrue(fired > 0u, L"the gun went off on some passes");
    }

    /*
     * 6502: M% part 3's tail on a chart -- `LASLI2 LDA QQ11 / BNE LASLI-1`.
     *
     * The laser still heats, still drains the banks and still picks a convergence point when the
     * player is looking at a chart; it just does not draw. The frame is otherwise the firing case
     * above, so the only difference between the two is the bitmap.
     */
    TEST_METHOD(TheGunOnAChartMatchesM)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      for (const std::uint8_t view : {std::uint8_t{64}, std::uint8_t{128}, std::uint8_t{255}})
      {
        Frame frame(view);
        frame.universe.view = view;
        frame.universe.spaceView = 0u;
        frame.universe.status.laserTemperature = 40u;
        frame.universe.keys[Elite::KEY_FIRE] = 0xFFu;
        frame.universe.commander.lasers[0] = Elite::LASER_BEAM;

        CompareFrames(frame, oracle, at, loop, WidenText("M% (QQ11 " + std::to_string(view) + ", firing)"));
      }
    }

    /*
     * 6502: M% with the energy banks nearly out.
     *
     * `LASLI` ends `JSR DENGY`, which is the only thing in the opening that touches `ENERGY`, and
     * the drain is what makes firing cost something. One at the boundary and one at zero, because
     * `DENGY` is `LDA ENERGY / BEQ D1 / DEC ENERGY` and a bank at zero must stay there.
     */
    TEST_METHOD(TheEnergyDrainMatchesM)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      for (const std::uint8_t energy : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{200}})
      {
        Frame frame(energy + 5u);
        frame.universe.status.energy = energy;
        frame.universe.status.laserTemperature = 40u;
        frame.universe.keys[Elite::KEY_FIRE] = 0xFFu;
        frame.universe.commander.lasers[0] = Elite::LASER_PULSE;

        CompareFrames(frame, oracle, at, loop, WidenText("M% (ENERGY " + std::to_string(energy) + ", firing)"));
      }
    }
  };

  TEST_CLASS(TheFlightFrameShips)
  {
  public:
    /*
     * 6502: MA3 to `JMP MAL1` -- the per-ship loop, over bubbles that exercise each of its exits.
     *
     * The loop is a `JMP MAL1` back edge with the index advanced by hand, and `KS1` is inside it:
     * killing a ship shuffles the slots down and goes round WITHOUT advancing, so the ship that
     * took the dead one's place is processed next. Every case here has something behind a ship
     * that leaves, because a port that wrote a `for` over the slots agrees with the game on every
     * bubble where nothing dies.
     */
    TEST_METHOD(ThePerShipLoopMatchesMAL1)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      struct Case
      {
        const char* what;
        std::uint8_t distance; ///< what goes into every high byte, so "how far away"
        std::uint8_t state;    ///< 6502: INWK+31
        std::uint8_t laser;    ///< 6502: LAS
        std::uint8_t bomb;     ///< 6502: BOMB
        std::uint8_t scoops;   ///< 6502: BST
        std::uint8_t view;     ///< 6502: QQ11
        bool empty;
        bool inSights = false; ///< dead ahead and close: what `HITCH` says yes to

        /*
         * 6502: INWK+35 and TYPE -- what it takes to reach `BURN`'s kill and the two `JSR SPIN`s
         * after it, which no case here reached until M2-d went looking for the carry they run on.
         *
         * `energy` under `LAS` is what makes the subtraction borrow; `asteroids` makes the three
         * shootable ships type 7, because only an asteroid meets the `CMP #AST` that leads to the
         * splinter roll -- and that roll is a `DORND` whose carry the port had wrong.
         */
        std::uint8_t energy = 60;
        bool asteroids = false;
      };

      const std::vector<Case> CASES = {
        {"an empty bubble", 0x20, 0x00, 0, 0, 0, 0, true},
        {"three ships, all distant", 0x20, 0x00, 0, 0, 0, 0, false},
        {"distant, with the laser on", 0x20, 0x00, 15, 0, 0, 0, false},
        {"distant, on a chart", 0x20, 0x00, 15, 0, 0, 128, false},
        {"right on top of us", 0x00, 0x00, 0, 0, 0, 0, false},
        {"on top of us with scoops", 0x00, 0x00, 0, 0, 0xFF, 0, false},
        {"on top of us, exploding", 0x00, 0x20, 0, 0, 0, 0, false},
        {"on top of us, already dead", 0x00, 0x80, 0, 0, 0, 0, false},
        {"in the sights, already dead", 0x00, 0x80, 0, 0, 0, 0, false, true},
        {"the energy bomb going off", 0x20, 0x00, 0, 0xFF, 0, 0, false},
        {"the bomb with the laser on", 0x00, 0x00, 15, 0xFF, 0, 0, false},
        {"far enough to leave", 0xF0, 0x00, 0, 0, 0, 0, false},
        {"a beam laser at close range", 0x00, 0x00, 143 & 0x7F, 0, 0, 0, false},
        {"a mining laser at close range", 0x00, 0x00, 50, 0, 0, 0, false},
        {"a military laser at close range", 0x00, 0x00, 151 & 0x7F, 0, 0, 0, false},
        {"in the sights and shot to bits", 0x00, 0x00, 50, 0, 0, 0, false, true, 8},
        {"an asteroid in the sights, shot to bits", 0x00, 0x00, 50, 0, 0, 0, false, true, 8, true},
        {"an asteroid shot with the wrong laser", 0x00, 0x00, 15, 0, 0, 0, false, true, 8, true},
      };

      std::uint32_t compared = 0;
      std::uint32_t killed = 0;

      for (const Case& item : CASES)
      {
        for (const std::uint8_t missileArmed : {std::uint8_t{0}, std::uint8_t{0xFF}})
        {
          Frame frame(0x4Du);
          PopulateBubble(frame, item.distance, item.state, item.empty);

          /*
           * "On top of us" is BEHIND us by the time `HITCH` looks: `MVEIT` takes the player's speed
           * off z first, so a ship at the origin has a negative z and the sights never close on it.
           * Two units ahead survives the move with x and y still under a byte, so `HITCH` sets the
           * carry -- and a ship that arrives at `LL9` killed seeds its cloud's first `DORND` on
           * that carry (§6.157), which is what the `cs-ll9-carry-hit` mutant watches for.
           */
          if (item.inSights)
          {
            for (std::size_t slot = 2; slot < 5u; ++slot)
            {
              frame.universe.bubble.blocks[slot].z.hi = 2u;
            }
          }

          // 6502: INWK+35 and FRIN/MANY -- what a laser has to get through, and what it is shooting.
          for (std::size_t slot = 2; slot < 5u; ++slot)
          {
            frame.universe.bubble.blocks[slot].energy = item.energy;
            if (item.asteroids)
            {
              const std::uint8_t was = frame.universe.bubble.slots[slot];
              if (was < 34u && frame.universe.bubble.counts[was] != 0u)
              {
                --frame.universe.bubble.counts[was];
              }
              frame.universe.bubble.slots[slot] = Elite::Byte(Elite::ShipType::Asteroid);
              ++frame.universe.bubble.counts[Elite::Byte(Elite::ShipType::Asteroid)];
            }
          }

          frame.universe.status.laserPower = item.laser;
          frame.universe.status.missileArmed = missileArmed;
          frame.universe.commander.energyBomb = item.bomb;
          frame.universe.commander.fuelScoops = item.scoops;
          frame.universe.view = item.view;
          frame.universe.commander.missiles = 3u;

          const std::wstring where = WidenText(std::string("MAL1 (") + item.what + (missileArmed != 0u ? ", missile armed)" : ")"));
          CompareFrames(frame, oracle, at, loop, where, Reach::Ships);

          std::uint32_t left = 0;
          for (const std::uint8_t occupant : frame.universe.bubble.slots)
          {
            left += (occupant != 0u) ? 1u : 0u;
          }
          killed += (!item.empty && left < 5u) ? 1u : 0u;
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(18u * 2u, compared, L"the whole sweep ran");
      Assert::IsTrue(killed > 0u, L"some bubbles emptied");
    }
  };

  TEST_CLASS(TheFlightFrameTail)
  {
  public:
    /*
     * 6502: MA18 to `JMP STARS` -- everything on a clock, swept over the whole clock.
     *
     * `MCNT AND 7` gates the shields and the banks, `AND 31` gates the station check, and steps 10,
     * 15 and 20 of the same thirty-two are the energy warning, the docking reminder and the cabin
     * temperature. So the counter is swept end to end rather than sampled: the parts that do
     * nothing are as much of the routine as the parts that do.
     */
    TEST_METHOD(TheFrameTailMatchesMA18)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      std::uint32_t compared = 0;
      std::uint32_t bombEnded = 0;

      for (std::uint8_t counter = 0; counter < 32u; ++counter)
      {
        for (std::uint8_t shape = 0; shape < 4u; ++shape)
        {
          Frame frame(counter * 5u + shape);
          PopulateBubble(frame, 0x20u, 0x00u, false);

          frame.universe.flight.mainLoopCounter = counter;
          frame.universe.status.midJump = ((shape & 1u) != 0u) ? 0xFFu : 0u;
          frame.universe.status.energy = ((shape & 2u) != 0u) ? 200u : 40u;
          frame.universe.commander.energyBomb = (counter & 1u) != 0u ? 0xC0u : 0u;
          frame.universe.commander.energyUnit = 1u;
          frame.universe.commander.fuelScoops = 0xFFu;
          frame.universe.status.viewLaser = 0x4Cu;
          frame.universe.status.laserCount = static_cast<std::uint8_t>(counter & 15u);
          frame.universe.status.ecmOurs = ((counter & 4u) != 0u) ? 0xFFu : 0u;
          frame.universe.status.ecmCountdown = ((counter & 8u) != 0u) ? 1u : 0u;

          const std::wstring where = WidenText("MA18 (MCNT " + std::to_string(counter) + ", shape " + std::to_string(shape) + ")");
          CompareFrames(frame, oracle, at, loop, where, Reach::Tail);

          bombEnded += (frame.universe.commander.energyBomb == 0x80u) ? 1u : 0u;
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(32u * 4u, compared, L"the whole clock was swept");
      Assert::IsTrue(bombEnded > 0u, L"the bomb burned down on some passes");
    }

    /*
     * 6502: part 15's cabin temperature, at the two thresholds that do more than warm the cabin.
     *
     * 224 is where the fuel scoops start working and 240 is where the Trumbles die, and both are
     * reached by flying at the SUN -- so the sun is put close and the distance swept across both.
     */
    TEST_METHOD(TheCabinTemperatureMatchesMA33)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      std::uint32_t compared = 0;
      std::uint32_t scooped = 0;
      std::uint32_t cooked = 0;

      /*
       * The distances that matter are 44 to 82, not 0 to 15.
       *
       * The temperature is `30 - MAS3`, and `MAS3` sums the HIGH bytes of three squares -- so a sun
       * closer than about fifty on each axis gives zero, the subtraction carries, and every case
       * dies before reaching the thresholds. 224 (the scoops) needs the sum between 30 and 61, and
       * 240 (the Trumbles) between 30 and 45, which is a narrow band of distances either side of
       * sixty.
       */
      for (std::uint8_t distance = 44; distance < 84u; distance = static_cast<std::uint8_t>(distance + 2u))
      {
        for (const std::uint8_t scoops : {std::uint8_t{0}, std::uint8_t{0xFF}})
        {
          Frame frame(distance * 3u + scoops);
          PopulateBubble(frame, 0x20u, 0x00u, false);

          // The sun in slot 1 at `distance` on every axis, and no station, so part 15 measures it.
          frame.universe.bubble.slots[1] = 129u;
          frame.universe.bubble.Count(Elite::ShipType::Station) = 0u;
          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = frame.universe.bubble.blocks[1].ToBytes();
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            shipBytes[byte] = 0u;
          }
          frame.universe.bubble.blocks[1] = Elite::Ship::FromBytes(shipBytes);
          frame.universe.bubble.blocks[1].x.hi = distance;
          frame.universe.bubble.blocks[1].y.hi = distance;
          frame.universe.bubble.blocks[1].z.hi = distance;

          frame.universe.flight.mainLoopCounter = 20u;
          frame.universe.status.midJump = 0u;
          frame.universe.commander.fuelScoops = scoops;
          frame.universe.commander.fuel = 40u;
          frame.universe.fuel = 40u;
          frame.universe.commander.tribbles.lo = 0x40u;
          frame.universe.commander.tribbles.hi = 0x21u;
          frame.universe.flight.delt4Next = 0xC0u;

          const std::wstring where =
            WidenText("MA33 (sun at " + std::to_string(distance) + (scoops != 0u ? ", scoops fitted)" : ", no scoops)"));
          CompareFrames(frame, oracle, at, loop, where, Reach::Tail);

          scooped += (frame.universe.commander.fuel > 40u) ? 1u : 0u;
          cooked += (frame.universe.sight.maskedWith.empty() ? 0u : 1u);
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(20u * 2u, compared, L"the whole sweep ran");
      Assert::IsTrue(scooped > 0u, L"the tank filled on some passes");
      Assert::IsTrue(cooked > 0u, L"and the Trumbles died on some");
    }
  };

  /*
   * 6502: MA23's altitude, and the byte it takes its low half from (risk R22).
   *
   * `MA93` reaches `LDY #&FF / STY ALTIT / INY / JSR m / BNE MA23` on step 10 of the thirty-two,
   * and if the planet's three sign bytes are clear and `MAS3`'s sum of squares is between 36 and
   * 254 it goes on to `SBC #36 / STA R / JSR LL5 / LDA Q / STA ALTIT`. **`Q` IS NOT WRITTEN
   * THERE.** `LL5` takes `(R Q)` as its radicand and `MAS2`/`MAS3` do not touch `Q`, so the low
   * half of what the altitude is a square root of is whatever the frame last left in that byte.
   *
   * That is the whole of R22, and until this fixture nothing measured it: every frame sweep in
   * this file puts the planet at a high byte of 0x20, so `MAS2` answers non-zero, `ALTIT` stays
   * 255 and the square root never runs. The port's `MathWorkspace::q` could have held anything.
   *
   * The sweep below picks distances that make `SBC #36` leave a SMALL number, because that is what
   * makes `Q` visible: with the difference at zero the radicand IS `Q` and `ALTIT` is its square
   * root, so the fixture reads a byte through a function that loses only the bottom bits of it.
   */
  TEST_CLASS(TheAltitude)
  {
  public:
    TEST_METHOD(TheAltitudeMatchesMA23)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);
      const std::uint16_t qq = oracle.Label("Q");

      /*
       * `MAS3` squares each high byte with `SQUA2` (the top byte of the square) and adds, so a
       * high byte of h contributes h*h/256 -- and 36 is the planet's own radius. 96 gives 36
       * exactly, which is the difference of zero the sweep wants; the rest step up from there.
       */
      const std::uint8_t DISTANCES[] = {96, 97, 100, 110, 128, 160, 200, 255};

      std::uint32_t compared = 0;
      std::uint32_t measured = 0;
      std::set<std::uint8_t> altitudes;

      for (const std::uint8_t distance : DISTANCES)
      {
        for (const std::uint8_t seeded : {0u, 1u, 63u, 64u, 127u, 128u, 200u, 255u})
        {
          Frame frame(distance * 7u + seeded);
          PopulateBubble(frame, 0x20u, 0x00u, false);

          // The planet in slot 0, straight ahead on one axis so that `MAS2` answers zero and
          // `MAS3` measures one square rather than three.
          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> planet = frame.universe.bubble.blocks[0].ToBytes();
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            planet[byte] = 0u;
          }
          frame.universe.bubble.blocks[0] = Elite::Ship::FromBytes(planet);
          frame.universe.bubble.blocks[0].z.hi = distance;

          frame.universe.flight.mainLoopCounter = 10u; // 6502: MCNT AND 31 == 10
          frame.universe.status.midJump = 0u;
          frame.universe.status.energy = 200u; // above the warning, so no message is printed

          /*
           * The frame's `Q`, seeded on BOTH sides. `UniverseImage` does not carry `MathWorkspace`
           * -- it is scratch, and M2-c took all of it but this byte and one other -- so the sweep
           * mirrors it by hand, the way `CompareFrames` mirrors `RAND`.
           */
          frame.universe.math.q = static_cast<std::uint8_t>(seeded);

          const std::wstring where = WidenText("MA23 (planet at " + std::to_string(distance) + ", Q " + std::to_string(seeded) + ")");
          CompareFrames(frame, oracle, at, loop, where, Reach::Tail,
                        [&](Cpu6502& _cpu) { _cpu.memory[qq] = static_cast<std::uint8_t>(seeded); });

          if (frame.universe.status.altitude != 0xFFu)
          {
            ++measured;
            altitudes.insert(frame.universe.status.altitude);
          }
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(8u * 8u, compared, L"the whole sweep ran");
      Assert::IsTrue(measured > 0u, L"the square root actually ran");
      Assert::IsTrue(altitudes.size() > 4u, L"and the seeded Q moved the answer");
    }

    /*
     * And the half R22 is actually about: `Q` decided by the FRAME rather than seeded.
     *
     * The sweep above proves the port takes the same square root of the same `(R Q)` as the game.
     * This one runs the whole of `M%` with the planet close enough for the root to run, over
     * bubbles that make different routines the last to touch `Q` -- a bubble with nothing in it, a
     * ship the loop moves and does not draw (`MVS4`'s `STA Q` of BETA), one it draws as a
     * wireframe (`LL9`'s last vertex distance, or the clipper's divisor), one it draws as a dot
     * (`DVID3B`'s), one exploding (`DOEXP`'s cloud size) and a sun (`SUN`'s last row).
     *
     * `ALTIT` is in the compared image, so a port whose frame left a different byte there than the
     * game's says so here. Nothing seeds `Q` on either side: that is the point.
     */
    TEST_METHOD(TheFramesOwnQReachesTheAltitude)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      struct Case
      {
        const char* what;
        std::uint8_t distance; ///< the OTHER ships' high bytes, which decides how they are drawn
        std::uint8_t state;    ///< 6502: INWK+31
        bool empty;
        bool sun;
      };

      const std::vector<Case> CASES = {
        {"nothing else in the bubble", 0x20, 0x00, true, false},
        {"ships too far to draw", 0x70, 0x00, false, false},
        {"ships drawn as wireframes", 0x02, 0x00, false, false},
        {"ships drawn as dots", 0x18, 0x00, false, false},
        {"a ship exploding", 0x02, 0x20, false, false},
        {"a sun close enough to draw", 0x02, 0x00, false, true},
      };

      // 96 puts `MAS3` at exactly the planet's radius, which is the case the difference of zero
      // makes `Q` the whole radicand; the rest step the difference up so the root moves with it.
      const std::uint8_t DISTANCES[] = {97, 100, 110, 128, 180, 255};

      std::uint32_t compared = 0;
      std::uint32_t measured = 0;
      std::set<std::uint8_t> altitudes;

      for (const Case& item : CASES)
      {
        for (const std::uint8_t distance : DISTANCES)
        {
          Frame frame(distance * 11u + static_cast<std::uint8_t>(item.distance));
          PopulateBubble(frame, item.distance, item.state, item.empty);

          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> planet = frame.universe.bubble.blocks[0].ToBytes();
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            planet[byte] = 0u;
          }
          frame.universe.bubble.blocks[0] = Elite::Ship::FromBytes(planet);
          frame.universe.bubble.slots[0] = 128u;
          frame.universe.bubble.blocks[0].z.hi = distance;

          if (!item.sun)
          {
            // Take the sun out, so the only body the frame draws is the planet.
            frame.universe.bubble.slots[1] = 0u;
          }

          frame.universe.flight.mainLoopCounter = 10u;
          frame.universe.status.midJump = 0u;
          frame.universe.status.energy = 200u;
          frame.universe.view = 0u;

          const std::wstring where =
            WidenText(std::string("MA23 whole frame (") + item.what + ", planet at " + std::to_string(distance) + ")");
          CompareFrames(frame, oracle, at, loop, where, Reach::Whole);

          if (frame.universe.status.altitude != 0xFFu)
          {
            ++measured;
            altitudes.insert(frame.universe.status.altitude);
          }
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(6u * 6u, compared, L"the whole sweep ran");
      Assert::IsTrue(measured > 0u, L"the square root ran on frames the loop itself filled");
      Assert::IsTrue(altitudes.size() > 2u, L"and the answers were not all the same");
    }
  };

  TEST_CLASS(TheWholeFlightFrame)
  {
  public:
    /*
     * 6502: `M%` from end to end, which is what the game actually calls.
     *
     * The three halves are compared separately above so that a failure localises; this is here so
     * that the JOINS between them are compared too -- the carry part 2 leaves for part 3, the slot
     * index part 4 hands part 12, and the counter part 13 shares with part 15.
     */
    TEST_METHOD(TheWholeFrameMatchesM)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      std::uint32_t compared = 0;

      for (std::uint8_t counter = 0; counter < 8u; ++counter)
      {
        for (const std::uint8_t distance : {std::uint8_t{0}, std::uint8_t{0x20}, std::uint8_t{0xF0}})
        {
          for (std::uint8_t shape = 0; shape < 4u; ++shape)
          {
            Frame frame(counter * 7u + distance + shape);
            PopulateBubble(frame, distance, 0x00u, false);

            frame.universe.flight.mainLoopCounter = static_cast<std::uint8_t>(counter * 4u + shape);
            frame.universe.control.roll = static_cast<std::uint8_t>(100u + counter * 5u);
            frame.universe.control.pitch = static_cast<std::uint8_t>(150u - counter * 3u);
            frame.universe.keys[Elite::KEY_FIRE] = ((shape & 1u) != 0u) ? 0xFFu : 0u;
            frame.universe.keys[Elite::KEY_SPEED_UP] = ((shape & 2u) != 0u) ? 0xFFu : 0u;
            frame.universe.commander.lasers[0] = Elite::LASER_PULSE;
            frame.universe.commander.missiles = 3u;
            frame.universe.commander.energyUnit = 1u;

            const std::wstring where = WidenText("M% whole (MCNT " + std::to_string(counter * 4u + shape) + ", distance " +
                                                 std::to_string(distance) + ", shape " + std::to_string(shape) + ")");
            CompareFrames(frame, oracle, at, loop, where, Reach::Whole);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(8u * 3u * 4u, compared, L"the whole sweep ran");
    }

    /*
     * The station you have just left is still there when you look back (§6.112).
     *
     * `TT110` spawns two ships: the planet ahead and the station BEHIND, at `INWK+7` = 1 with the
     * z sign set, which is 256 units back. Nothing in the loop is supposed to take it away -- part
     * 14 only ever ADDS one -- so a launch followed by a few frames should leave it in the bubble,
     * moving away as the ship pulls out.
     *
     * This is a whole-loop test rather than a routine comparison because that is where the fault
     * was: every routine on the path agreed with the shipped game one at a time.
     */
    TEST_METHOD(TheStationSurvivesTheLaunch)
    {
      Frame frame(4242u);

      // A clean bubble, because the launch builds its own: `RES2` empties it and then the two
      // spawns fill it, and a fixture's fleet would be a third thing that is not the game's.
      frame.universe.bubble.slots.fill(0u);
      frame.universe.bubble.counts.fill(0u);
      frame.universe.bubble.junk = 0u;
      frame.universe.view = 0u;
      frame.universe.spaceView = 0u;

      Elite::Ports ports = frame.universe.PortsWith(frame.outside, frame.effects, frame.universe.unused);

      std::uint8_t docked = 0xFFu;
      Elite::SystemSeeds selected{};
      Elite::Launch(frame.universe, ports, nullptr, docked, frame.universe.commander.systemX, frame.universe.commander.systemY, selected);

      Assert::AreEqual<std::uint32_t>(1u, frame.universe.bubble.Count(Elite::ShipType::Station),
                                      L"the launch leaves the station in the bubble");

      // 6502: LOOK1, which the launch ends with -- the front view, and `QQ11` back to zero.
      frame.universe.view = 0u;

      // 6502: LOOK1 with X = 1 -- the rear view, which is where a station you have just left is.
      frame.universe.spaceView = 1u;

      for (int pass = 0; pass < 40; ++pass)
      {
        const Elite::LoopOutcome outcome = Elite::MainFlightLoop(frame.universe, ports);
        Assert::IsTrue(outcome == Elite::LoopOutcome::Continued,
                       (L"frame " + std::to_wstring(pass) + L" should not end the flight").c_str());

        Assert::AreEqual<std::uint32_t>(1u, frame.universe.bubble.Count(Elite::ShipType::Station),
                                        (L"the station is still there after " + std::to_wstring(pass + 1) + L" frames").c_str());
      }

      /*
       * AND IT IS ON THE SCREEN, which is the half that was actually broken (§6.112).
       *
       * `NWSPS` points the station's heap at `LSO` -- the SUN's -- and the port keeps that region
       * in a different object from the ship arena, so every line the station drew was written out
       * of range and dropped. Byte 31 still said "drawn", the bubble still held it, the scanner
       * still counted it, and the rear view was empty. So this asserts the LINES: a station in the
       * heap it was given, and pixels on the canvas from drawing them.
       */
      const std::uint16_t heapAt = frame.universe.bubble.blocks[1].heap.Address();
      Assert::AreEqual<std::uint32_t>(Elite::SUN_HEAP_ADDRESS, heapAt, L"the station draws through the sun's heap");
      Assert::IsTrue(frame.universe.heap.Read(Elite::HeapOffset::FromAddress(heapAt)) > 1u, L"and the heap holds the lines it last drew");

      std::size_t lit = 0;
      for (int y = 0; y < Elite::Canvas::SPACE_VIEW_HEIGHT; ++y)
      {
        for (int column = 0; column < Elite::Canvas::CELL_COLUMNS; ++column)
        {
          lit += (frame.universe.canvas.Read(static_cast<std::uint16_t>(y / 8 * Elite::Canvas::ROW_BYTES + column * 8 + (y % 8))) != 0u)
                   ? 1u
                   : 0u;
        }
      }
      Assert::IsTrue(lit > 20u, L"and the space view has the station drawn in it");
    }

    /*
     * What a flight frame COSTS, which is what decides how fast the game runs (§6.114).
     *
     * §6.17 established that the C64's main loop has no frame cap: `WSCAN` is called from `DELAY`,
     * `TT16+7` and `FREEZE` and from nowhere else, and `main_flight_loop_part_13_of_16`'s `JSR
     * WSCAN` is inside a version gate the C64 is not in. So the game runs `M%` as fast as a 6510
     * gets round it, and "how fast does Elite run" is a question about cycles rather than about
     * refresh rates. §6.17 asked for the measurement in as many words -- "Measure what an iteration
     * costs, divide by the clock" -- and until this it had never been taken: the port ran the loop
     * at the NTSC vertical refresh, which is four to six times too fast.
     *
     * THE TRAPS ARE THE ONES THAT ARE REALLY OUTSIDE, and no others. A trapped call costs nothing
     * (`CycleTests::ATrappedCallCostsNothing`), so trapping `PLANET` -- as the comparison runs do,
     * because its effect is a seam -- would leave out the planet's drawing, which is a large part
     * of a frame. Here only the sound and the VIC-II registers are trapped, and everything the
     * 6510 would have computed is computed.
     *
     * The measurement is a lower bound all the same, for the two reasons the `cycles` field
     * documents: the trapped sound calls are free here and cost the machine something, and the
     * VIC-II steals 5-10% of the cycles for its own fetches, which is not modelled.
     */
    TEST_METHOD(TheFlightFrameCostsWhatItCosts)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const Where at(oracle);
      const LoopWhere loop(oracle);

      struct Scene
      {
        std::uint32_t seed;
        std::size_t ships; ///< how many of `Seed`'s fleet to keep, past the planet and the sun
        const char* what;
      };

      /*
       * Three scenes, and the FOURTH is missing for a reason worth writing down: `Seed`'s third
       * slot is a space STATION, and a station's own thinking does not come back inside forty
       * million instructions. That was written when `TACTICS` was trapped in every comparison in
       * this file and untrapped only here; M3-b-1c took the seam away and every comparison runs
       * the AI now, but this measurement is the one that has to run it in FULL -- a comparison
       * stops at `MA18`, and this does not. So the crowded end of the range is still not measured
       * here, and the port's rate is derived from what is (§6.114).
       */
      const Scene SCENES[] = {
        {11u, 0u, "an empty bubble"},
        {23u, 1u, "one ship"},
        {37u, 2u, "two ships"},
      };

      for (const Scene& scene : SCENES)
      {
        Frame frame(scene.seed);

        // `Seed` fills the first three slots; anything past the scene's count goes, and the list's
        // terminator moves with it -- `FRIN`'s zero is what makes it a list.
        for (std::size_t slot = scene.ships; slot < 3u; ++slot)
        {
          frame.universe.bubble.slots[slot] = 0u;
        }

        Cpu6502 cpu = oracle.Fresh();

        /*
         * The three exits are patched to stop the run, as `CompareFrames` does and for its reason:
         * `JMP DEATH` never comes back, so a frame that takes one would run off into the death
         * sequence and never return. A scene that ends the flight is not a frame to time.
         */
        const std::uint16_t EXITS[] = {loop.death, loop.doentry, loop.escape};
        for (const std::uint16_t exit : EXITS)
        {
          const std::uint8_t leave[] = {0x4Cu, 0xF9u, 0xFFu};
          cpu.Load(exit, leave, sizeof(leave));
        }

        // Sound and the VIC-II only: everything that draws or thinks runs for real.
        cpu.AddTrap(loop.setl1);
        cpu.AddTrap(loop.dovdu19);
        cpu.AddTrap(loop.startbd);
        cpu.AddTrap(loop.stopbd);

        FillScreens(cpu, frame.universe.canvas, at.screen, 0x1Du);
        Mirror(frame.universe, cpu, at);
        MirrorFrame(frame, cpu, at, loop);

        for (std::uint16_t address = HEAP_START; address < Elite::LineHeap::TOP; ++address)
        {
          cpu.memory[address] = frame.universe.heap.Read(Elite::HeapOffset::FromAddress(address));
        }
        cpu.memory[loop.slsp] = static_cast<std::uint8_t>(frame.universe.bubble.heapBottom.Address() & 0xFFu);
        cpu.memory[static_cast<std::uint16_t>(loop.slsp + 1u)] = static_cast<std::uint8_t>(frame.universe.bubble.heapBottom.Address() >> 8);

        const std::uint64_t before = cpu.cycles;
        const Elite::Testing::RunResult run = cpu.CallSubroutine(loop.mainLoop, 40'000'000);
        Assert::IsTrue(run.completed, WidenText(std::string("M% returned for ") + scene.what).c_str());

        const std::uint64_t cost = cpu.cycles - before;

        /*
         * And how much was DRAWN, because that is what a cost model would be indexed by: every
         * ship's heap holds the lines it last put on screen, four bytes each after the count.
         */
        std::uint32_t lines = 0;
        for (std::size_t slot = 0; slot < scene.ships; ++slot)
        {
          const std::uint16_t block = static_cast<std::uint16_t>(at.kPercent + slot * Elite::SHIP_BLOCK_SIZE);
          const std::uint16_t heapAt = static_cast<std::uint16_t>(cpu.memory[static_cast<std::uint16_t>(block + 33u)] |
                                                                  (cpu.memory[static_cast<std::uint16_t>(block + 34u)] << 8));
          const std::uint8_t used = cpu.memory[heapAt];
          lines += (used > 1u) ? ((used - 1u) / 4u) : 0u;
        }

        Logger::WriteMessage((std::string("flight frame, ") + scene.what + ": " + std::to_string(cost) + " cycles, " +
                              std::to_string(lines) + " ship lines, " + std::to_string(1022727.0 / static_cast<double>(cost)) +
                              " frames a second")
                               .c_str());

        /*
         * The bound is what the port's rate is derived from, and it is wide for the same reason the
         * title screen's is: this is a MEASUREMENT, and narrowing it turns any upstream change into
         * a failing test with nothing wrong. What it asserts is the order -- tens of thousands of
         * cycles a frame, which is tens of frames a second and not sixty.
         */
        Assert::IsTrue(cost > 10'000u && cost < 400'000u, (L"a flight frame cost " + std::to_wstring(cost) +
                                                           L" cycles, which is outside the range "
                                                           L"Outpost::FLIGHT_FRAME_COSTS was derived from")
                                                            .c_str());
      }
    }
  };

} // namespace GameLogicTests
