#include "pch.h"

#include "Cpu6502.h"
#include "FlightWorld.h"
#include "OracleImage.h"

#include "Arith.h"
#include "FlightLoop.h"
#include "Rng.h"
#include "Dashboard.h"
#include "ShipBlueprint.h"
#include "ShipSlot.h"

#include <array>
#include <cstdint>
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
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            state = state * 1103515245u + 12345u;
            const std::uint8_t value = static_cast<std::uint8_t>(state >> 17);
            bubble.blocks[slot][byte] = value;
            cpu.memory[static_cast<std::uint16_t>(kPercent + slot * Elite::SHIP_BLOCK_SIZE + byte)] = value;
          }

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
        Elite::ShipBlock work;
        std::uint32_t state = 0x71A3C25Fu ^ (seed * 0x85EBCA6Bu);
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          state = state * 1103515245u + 12345u;
          const std::uint8_t value = static_cast<std::uint8_t>(state >> 17);
          work[byte] = value;
          cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = value;
        }

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
            for (int axis = 0; axis < 3; ++axis)
            {
              const std::size_t at = static_cast<std::size_t>(axis) * 3u + 1u;
              bubble.blocks[0][at] = BYTES[axis];
              cpu.memory[static_cast<std::uint16_t>(kPercent + at)] = BYTES[axis];
            }

            cpu.y = 0;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(mas3, 5'000);
            Assert::IsTrue(run.completed, L"MAS3 returned");

            Elite::MathWorkspace math;
            const std::uint8_t ours = Elite::SumOfSquares(bubble, math, 0);

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
              Elite::ShipBlock work;
              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                work[byte] = 0;
              }

              // The source coordinate at INWK+9, and the destination at INWK+0.
              work[9] = low;
              work[10] = high;
              work[0] = target;
              work[1] = static_cast<std::uint8_t>(target ^ 0x5Au);
              work[2] = sign;

              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = work[byte];
              }

              cpu.y = 9;
              cpu.x = 0;
              const Elite::Testing::RunResult run = cpu.CallSubroutine(mas1, 5'000);
              Assert::IsTrue(run.completed, L"MAS1 returned");

              Elite::MathWorkspace math;
              const std::uint8_t ours = Elite::DoubleAndAddCoordinate(work, math, 9, 0);

              const std::wstring where = WidenText("MAS1(low " + std::to_string(low) + ", high " + std::to_string(high) + ", sign " +
                                                   std::to_string(sign) + ", target " + std::to_string(target) + ")");

              Assert::AreEqual(cpu.a, ours, (where + L": the returned magnitude").c_str());
              for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + byte)], work[byte],
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

      struct Recorder final : Elite::SpawnChildEffects
      {
        std::vector<std::uint8_t> flags;
        std::vector<std::uint8_t> types;
        bool SpawnChild(std::uint8_t _aiFlag, std::uint8_t _type) override
        {
          flags.push_back(_aiFlag);
          types.push_back(_type);
          return false; // `SFS1`'s carry, which a trap leaves clear and `SPIN` ignores
        }
      };

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(sfs1);

      // ---- SPIN2 on its own --------------------------------------------------------------------
      std::uint32_t placed = 0;
      for (std::uint32_t count = 0; count < 256; ++count)
      {
        for (const std::uint8_t type : {std::uint8_t{3}, std::uint8_t{5}, std::uint8_t{17}})
        {
          cpu.ClearTrapHits();
          cpu.memory[cnt] = 0xEEu;
          cpu.a = static_cast<std::uint8_t>(count);
          cpu.x = type;
          cpu.z = (count == 0u); // what the caller's own `AND` has just left behind

          const Elite::Testing::RunResult run = cpu.CallSubroutine(spin2, 20'000);
          Assert::IsTrue(run.completed, L"SPIN2 returned");

          Recorder effects;
          Elite::MathWorkspace math;
          math.cnt = 0xEEu;
          Elite::SpawnItems(math, effects, type, static_cast<std::uint8_t>(count));

          const std::wstring where = WidenText("SPIN2(count " + std::to_string(count) + ", type " + std::to_string(type) + ")");

          Assert::AreEqual<std::size_t>(cpu.trapHits.size(), effects.types.size(), (where + L": how many were spawned").c_str());
          for (std::size_t hit = 0; hit < cpu.trapHits.size(); ++hit)
          {
            Assert::AreEqual(cpu.trapHits[hit].a, effects.flags[hit], (where + L": the AI flag of #" + std::to_wstring(hit)).c_str());
            Assert::AreEqual(cpu.trapHits[hit].x, effects.types[hit], (where + L": the type of #" + std::to_wstring(hit)).c_str());
          }
          Assert::AreEqual(cpu.memory[cnt], math.cnt, (where + L": CNT").c_str());

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
        const std::uint16_t blueprint = Elite::BlueprintAddress(type);
        if (blueprint == 0u)
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
            cpu.memory[xx0] = static_cast<std::uint8_t>(blueprint & 0xFFu);
            cpu.memory[static_cast<std::uint16_t>(xx0 + 1)] = static_cast<std::uint8_t>(blueprint >> 8);
            cpu.y = type;
            cpu.c = carry;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(spin, 20'000);
            Assert::IsTrue(run.completed, L"SPIN returned");

            Recorder effects;
            Elite::MathWorkspace math;
            math.cnt = 0xEEu;
            Elite::Rng rng;
            rng.SetState(bytes);
            Elite::SpawnDebris(rng, math, effects, blueprint, type, carry);

            const std::wstring where = WidenText("SPIN(type " + std::to_string(type) + ", seed " + std::to_string(seed) + ", carry " +
                                                 std::to_string(carry ? 1 : 0) + ")");

            Assert::AreEqual<std::size_t>(cpu.trapHits.size(), effects.types.size(), (where + L": how many were spawned").c_str());
            for (std::size_t hit = 0; hit < cpu.trapHits.size(); ++hit)
            {
              Assert::AreEqual(cpu.trapHits[hit].a, effects.flags[hit], (where + L": the AI flag of #" + std::to_wstring(hit)).c_str());
              Assert::AreEqual(cpu.trapHits[hit].x, effects.types[hit], (where + L": the type of #" + std::to_wstring(hit)).c_str());
            }
            Assert::AreEqual(cpu.memory[cnt], math.cnt, (where + L": CNT").c_str());
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
            Elite::ShipBlock work{};
            work[1] = x;
            work[4] = y;
            work[7] = z;
            for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
            {
              cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = work[byte];
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
        const std::uint16_t blueprint = Elite::BlueprintAddress(shipType);
        if (blueprint == 0u)
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
                Elite::ShipBlock work{};
                work[0] = across;
                work[3] = down;
                work[8] = behind;
                work[31] = exploding;

                for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
                {
                  cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = work[byte];
                }
                cpu.memory[xx0] = static_cast<std::uint8_t>(blueprint & 0xFFu);
                cpu.memory[static_cast<std::uint16_t>(xx0 + 1)] = static_cast<std::uint8_t>(blueprint >> 8);
                cpu.memory[type] = shipType;

                Assert::IsTrue(cpu.CallSubroutine(hitch, 5'000).completed, L"HITCH returned");

                Elite::MathWorkspace math;
                const bool ours = Elite::IsHit(work, math, blueprint, shipType);

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
    /// Flat memory the game never touches, where the patched `MVTRIBS` counts its own arrivals.
    constexpr std::uint16_t TRUMBLE_PROBE = 0xFFF0;

    /// Three more of the same, for `DEATH`, `DOENTRY` and `ESCAPE` in that order.
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

      std::uint16_t mvtribs, nomvetr, ma3, ma18, escape, frs1, angry, startbd, stopbd, noise;
      std::uint16_t mainLoop, death, doentry, tactics, doexp, planet, nwsps, sfs1, noise2;
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

        mvtribs = _oracle.Label("MVTRIBS");
        nomvetr = _oracle.Label("NOMVETR");
        ma3 = _oracle.Label("MA3");
        escape = _oracle.Label("ESCAPE");
        frs1 = _oracle.Label("FRS1");
        angry = _oracle.Label("ANGRY");
        startbd = _oracle.Label("startbd");
        stopbd = _oracle.Label("stopbd");
        noise = _oracle.Label("NOISE");
        mainLoop = _oracle.Label("M%");
        ma18 = _oracle.Label("MA18");
        death = _oracle.Label("DEATH");
        doentry = _oracle.Label("DOENTRY");
        tactics = _oracle.Label("TACTICS");
        doexp = _oracle.Label("DOEXP");
        planet = _oracle.Label("PLANET");
        nwsps = _oracle.Label("NWSPS");
        sfs1 = _oracle.Label("SFS1");
        noise2 = _oracle.Label("NOISE2");
        setl1 = _oracle.Label("SETL1");
        dovdu19 = _oracle.Label("DOVDU19");
        slsp = _oracle.Label("SLSP");
      }
    };

    /*
     * The flight loop's own seams, recording into the world's sound list.
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

      std::vector<std::uint8_t>& sounds;
      std::vector<Pitched> pitched;
      std::vector<std::uint8_t> stopped;
      std::vector<std::uint8_t> spawned;
      std::vector<std::uint8_t> angered;
      std::uint32_t trumbleMoves = 0;
      std::uint32_t musicStarts = 0;
      std::uint32_t musicStops = 0;

      /// What `FRS1` answers -- carry set for "there was room", clear for a full bubble.
      bool spawnSucceeds = true;

      explicit RecordingLoop(std::vector<std::uint8_t>& _sounds) noexcept
        : sounds(_sounds)
      {
      }

      bool PlaySound(std::uint8_t _effect) override
      {
        sounds.push_back(_effect);
        return true;
      }
      /// `NOISE2` is trapped at its own address, so its hits never reach `NOISE` and belong in
      /// their own list -- putting them in `sounds` as well would double-count every explosion.
      bool PlaySoundPitched(std::uint8_t _effect, std::uint8_t _sustain, std::uint8_t _frequency) override
      {
        pitched.push_back({_effect, _sustain, _frequency});
        return true;
      }
      void StopSound(std::uint8_t _effect) override
      {
        stopped.push_back(_effect);
      }
      void MoveTrumbles() override
      {
        ++trumbleMoves;
      }
      void StartDockingMusic() override
      {
        ++musicStarts;
      }
      void StopDockingMusic() override
      {
        ++musicStops;
      }
      bool SpawnAhead(std::uint8_t _type) override
      {
        spawned.push_back(_type);
        return spawnSucceeds;
      }
      void Anger(std::uint8_t _type) override
      {
        angered.push_back(_type);
      }
      void SpawnStation() override
      {
        ++stationSpawns;
      }

      bool SpawnChild(std::uint8_t _aiFlag, std::uint8_t _type) override
      {
        children.push_back({_aiFlag, _type});
        return childSucceeds;
      }

      struct Child
      {
        std::uint8_t aiFlag, type;
      };

      std::vector<Child> children;
      std::uint32_t stationSpawns = 0;
      bool childSucceeds = true;
    };

    /// What `MVEIT` and `LL9` reach that this slice does not build.
    struct RecordingWorld final : Elite::ShipEffects, Elite::ShipDrawEffects
    {
      std::vector<std::uint8_t> tactics;
      std::uint32_t planets = 0;
      std::uint32_t explosions = 0;
      std::uint32_t clouds = 0;

      void RunTactics(Elite::ShipBlock& _work) override
      {
        tactics.push_back(_work[32]);
      }
      void DrawPlanetOrSun() override
      {
        ++planets;
      }
      void DrawExplosion() override
      {
        ++explosions;
      }
      void SeedExplosionCloud(Elite::LineHeap&, std::uint16_t _address, std::uint16_t) override
      {
        seeded.push_back(_address);
        ++clouds;
      }

      std::vector<std::uint16_t> seeded;
    };

    /// Everything one frame needs that the shared `World` does not carry.
    struct Frame
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
      RecordingWorld outside;
      RecordingLoop effects{world.effects.sounds};

      explicit Frame(std::uint32_t _seed)
      {
        Seed(world, _seed);

        /*
         * A message already up, and a REAL one.
         *
         * `Seed` leaves `MCH` at zero, and a message that jams sends `MESS` down `me1` to erase
         * whatever is showing -- so a zero would be printed, and `TT27` opens `TAX / BEQ csh`, which
         * prints the player's cash. The game cannot reach that: `MCH` is written only by `MESS`
         * itself, and the erase only runs when `DLY` is non-zero, which only `MESS` makes it.
         */
        world.message.token = 101u;
        world.message.column = 9u;
        world.message.append = 1u;
        world.message.delay = 12u;

        /*
         * 6502: XX0 -- a REAL blueprint, because zero is not a state the game reaches.
         *
         * Part 4 leaves `XX0` alone for the planet and the sun, so a body inherits whatever the last
         * ship put there (§6.90) -- and by the time the flight loop runs, something always has. With
         * zero the game reads `(XX0),15` out of its own zero page and the port reads a guarded zero,
         * which is a disagreement about an address neither would ever form.
         */
        world.flight.blueprint = Elite::BlueprintAddress(11u);
        world.screen.upperBitmapMode = 0xC0u;
        world.status.laserCount = 0u;
        world.status.laserPower = 0u;
        world.status.missileArmed = 0u;
        world.status.ecmOurs = 0u;
        world.bubble.missileTarget = 0xFFu;
        world.flight.delt4 = 0u;
        world.flight.delt4Next = 0u;
        control.roll = 128u;
        control.pitch = 128u;
      }
    };

    /// The port's world into the oracle, for everything past what `Mirror` covers.
    void MirrorFrame(const Frame& _frame, Cpu6502& _cpu, const Where& _at, const LoopWhere& _loop)
    {
      const World& world = _frame.world;

      for (std::size_t slot = 0; slot < _frame.keys.size(); ++slot)
      {
        _cpu.memory[static_cast<std::uint16_t>(_loop.klo + slot)] = _frame.keys[slot];
      }

      _cpu.memory[_loop.jstx] = _frame.control.roll;
      _cpu.memory[_loop.jsty] = _frame.control.pitch;
      _cpu.memory[_loop.autoByte] = _frame.control.dockingComputer;
      _cpu.memory[_loop.damp] = _frame.options.dampingDisabled;
      _cpu.memory[_loop.djd] = _frame.options.recentreDisabled;
      _cpu.memory[_loop.jstk] = _frame.options.joystick;

      _cpu.memory[_loop.alpha] = world.flight.alpha;
      _cpu.memory[_loop.alp2Next] = world.flight.alp2Next;
      _cpu.memory[_loop.bet2] = world.flight.bet2;
      _cpu.memory[_loop.bet2Next] = world.flight.bet2Next;
      _cpu.memory[_loop.delt4] = world.flight.delt4;
      _cpu.memory[static_cast<std::uint16_t>(_loop.delt4 + 1u)] = world.flight.delt4Next;

      _cpu.memory[_loop.las] = world.status.laserPower;
      _cpu.memory[_loop.lasct] = world.status.laserCount;
      _cpu.memory[_loop.msar] = world.status.missileArmed;
      _cpu.memory[_loop.mstg] = world.bubble.missileTarget;
      _cpu.memory[_loop.ecmp] = world.status.ecmOurs;
      _cpu.memory[_loop.moonflower] = world.screen.upperBitmapMode;

      _cpu.memory[_loop.lasx] = _frame.burst.x;
      _cpu.memory[_loop.lasy] = _frame.burst.y;
    }

    /// Every byte the frame's opening can write, beyond what `CompareState` already covers.
    void CompareFrame(const Cpu6502& _cpu, const Frame& _frame, const LoopWhere& _loop, const std::wstring& _context)
    {
      const World& world = _frame.world;

      auto same = [&](std::uint16_t _address, std::uint8_t _ours, const wchar_t* _name)
      { Assert::AreEqual(_cpu.memory[_address], _ours, (_context + L": " + _name).c_str()); };

      same(_loop.jstx, _frame.control.roll, L"JSTX");
      same(_loop.jsty, _frame.control.pitch, L"JSTY");
      same(_loop.autoByte, _frame.control.dockingComputer, L"auto");

      same(_loop.alpha, world.flight.alpha, L"ALPHA");
      same(_loop.alp2Next, world.flight.alp2Next, L"ALP2+1");
      same(_loop.bet2, world.flight.bet2, L"BET2");
      same(_loop.bet2Next, world.flight.bet2Next, L"BET2+1");
      same(_loop.delt4, world.flight.delt4, L"DELT4");
      same(static_cast<std::uint16_t>(_loop.delt4 + 1u), world.flight.delt4Next, L"DELT4+1");

      same(_loop.las, world.status.laserPower, L"LAS");
      same(_loop.lasct, world.status.laserCount, L"LASCT");
      same(_loop.lasx, _frame.burst.x, L"LASX");
      same(_loop.lasy, _frame.burst.y, L"LASY");
      same(_loop.msar, world.status.missileArmed, L"MSAR");
      same(_loop.mstg, world.bubble.missileTarget, L"MSTG");
      same(_loop.ecmp, world.status.ecmOurs, L"ECMP");
      same(_loop.moonflower, world.screen.upperBitmapMode, L"moonflower");

      same(_loop.mch, world.message.token, L"MCH");
      same(_loop.messxc, world.message.column, L"messXC");
      same(_loop.gntmp, world.status.laserTemperature, L"GNTMP");
      same(_loop.energy, world.status.energy, L"ENERGY");

      for (std::size_t index = 0; index < Elite::COMMANDER_BLOCK_SIZE; ++index)
      {
        Assert::AreEqual(_cpu.memory[static_cast<std::uint16_t>(_loop.tp + index)], world.commander.bytes[index],
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
      World& world = _frame.world;

      for (std::size_t slot = 0; slot < world.bubble.slots.size(); ++slot)
      {
        world.bubble.slots[slot] = 0u;
      }
      for (std::size_t type = 0; type < world.bubble.counts.size(); ++type)
      {
        world.bubble.counts[type] = 0u;
      }

      if (_empty)
      {
        world.bubble.junk = 0u;
        return;
      }

      const std::uint8_t TYPES[] = {128u, 129u, 3u, 5u, 11u};

      for (std::size_t slot = 0; slot < 5u; ++slot)
      {
        world.bubble.slots[slot] = TYPES[slot];
        if (TYPES[slot] < 34u)
        {
          ++world.bubble.counts[TYPES[slot]];
        }

        Elite::ShipBlock& block = world.bubble.blocks[slot];
        for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
        {
          block[byte] = 0u;
        }

        block[1] = _distance; // x high
        block[4] = _distance; // y high

        /*
         * The z high byte separates the ships EXCEPT at zero, where it must not: parts 7 to 12 branch
         * on all three high bytes being zero together, so spreading them out would mean no case in
         * the sweep ever collided with anything.
         */
        block[7] = (_distance == 0u) ? std::uint8_t{0} : static_cast<std::uint8_t>(_distance + slot);
        block[14] = 20u; // pitch counter
        block[27] = 20u; // speed
        block[31] = _state;
        block[32] = 0u;    // no AI, so `TACTICS` is not reached
        block[34] = 0x0Cu; // the heap pointer's high byte
        block[35] = 60u;   // energy
      }

      world.bubble.junk = 0u;
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

    void CompareFrames(Frame& _frame, const OracleImage& _oracle, const Where& _at, const LoopWhere& _loop, const std::wstring& _context,
                       Reach _reach = Reach::Opening)
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

      cpu.AddTrap(_loop.tactics);
      cpu.AddTrap(_loop.doexp);
      cpu.AddTrap(_loop.planet);
      cpu.AddTrap(_loop.nwsps);
      cpu.AddTrap(_loop.setl1);
      cpu.AddTrap(_loop.dovdu19);
      cpu.AddTrap(_loop.noise2);
      cpu.AddTrap(_loop.sfs1, _frame.effects.childSucceeds ? Cpu6502::TrapExit::SetCarry : Cpu6502::TrapExit::ClearCarry);
      cpu.AddTrap(_loop.startbd);
      cpu.AddTrap(_loop.stopbd);
      cpu.AddTrap(_loop.angry);
      cpu.AddTrap(_loop.frs1, _frame.effects.spawnSucceeds ? Cpu6502::TrapExit::SetCarry : Cpu6502::TrapExit::ClearCarry);

      // `NOISE` ends `SEC / RTS` on the path that gives the effect a voice, and `LASLI`'s opening
      // `DORND` rolls that carry into its own answer (§6.86).
      cpu.AddTrap(_loop.noise, Cpu6502::TrapExit::SetCarry);

      /*
       * `MVTRIBS` is entered by `JMP` and leaves by `JMP NOMVETR`, so it is patched rather than
       * trapped: a trap's RTS would pop the fake return address and end the run mid-frame. The `INC`
       * in front of the jump back is what makes "it was reached" observable at all -- without it the
       * oracle's side of the comparison would only be the branch the port itself takes.
       */
      cpu.memory[TRUMBLE_PROBE] = 0u;
      const std::uint8_t back[] = {0xEEu, static_cast<std::uint8_t>(TRUMBLE_PROBE & 0xFFu), static_cast<std::uint8_t>(TRUMBLE_PROBE >> 8),
                                   0x4Cu, static_cast<std::uint8_t>(_loop.nomvetr & 0xFFu), static_cast<std::uint8_t>(_loop.nomvetr >> 8)};
      cpu.Load(_loop.mvtribs, back, sizeof(back));

      FillScreens(cpu, _frame.world.canvas, _at.screen, 0x1Du);
      Mirror(_frame.world, cpu, _at);
      MirrorFrame(_frame, cpu, _at, _loop);

      /*
       * The ship line heap, which the port keeps apart from the blocks and the original does not.
       *
       * `LineHeap`'s arena runs from `K%` to `LS%` and the bottom of it IS the block region, which
       * `Mirror` has just written -- so only the bytes above the last block are the heap's, and
       * mirroring the whole arena would undo the blocks.
       */
      for (std::uint16_t address = HEAP_START; address < Elite::LineHeap::TOP; ++address)
      {
        cpu.memory[address] = _frame.heap.Read(address);
      }
      cpu.memory[_loop.slsp] = static_cast<std::uint8_t>(_frame.world.bubble.heapBottom & 0xFFu);
      cpu.memory[static_cast<std::uint16_t>(_loop.slsp + 1u)] = static_cast<std::uint8_t>(_frame.world.bubble.heapBottom >> 8);

      const std::uint16_t entry = (_reach == Reach::Ships) ? _loop.ma3 : (_reach == Reach::Tail) ? _loop.ma18 : _loop.mainLoop;

      const Elite::Testing::RunResult run = cpu.CallSubroutine(entry, 8'000'000);
      Assert::IsTrue(run.completed, (_context + L": M% reached an exit").c_str());

      Elite::FlightScreen screen = _frame.world.Screen();
      Elite::FlightLoop loop{screen,      _frame.keys,       _frame.control, _frame.options, _frame.burst,   _frame.heap,
                             _frame.clip, _frame.projection, _frame.axes,    _frame.outside, _frame.outside, _frame.effects};
      const Elite::LoopOutcome outcome = (_reach == Reach::Ships)   ? Elite::MoveEveryShip(loop)
                                         : (_reach == Reach::Tail)  ? Elite::EndFlightFrame(loop)
                                         : (_reach == Reach::Whole) ? Elite::MainFlightLoop(loop)
                                                                    : Elite::BeginFlightFrame(loop);

      // ---- the exit ------------------------------------------------------------------------------
      std::uint32_t reachedEnd = 0;
      std::uint32_t escaped = 0;
      std::uint32_t died = 0;
      std::uint32_t docked = 0;
      std::uint32_t stationSpawns = 0;
      std::vector<Elite::Testing::Cpu6502::TrapHit> pitched;
      std::vector<std::uint8_t> sounds;
      std::vector<std::uint8_t> spawned;
      std::vector<std::uint8_t> angered;
      std::uint32_t starts = 0;
      std::uint32_t stops = 0;

      for (const Cpu6502::TrapHit& hit : cpu.trapHits)
      {
        if (hit.address == _loop.ma3 || hit.address == _loop.ma18)
        {
          ++reachedEnd;
        }
        else if (hit.address == _loop.nwsps)
        {
          ++stationSpawns;
        }
        else if (hit.address == _loop.noise2)
        {
          pitched.push_back(hit);
        }
        else if (hit.address == _loop.noise)
        {
          sounds.push_back(hit.y);
        }
        else if (hit.address == _loop.frs1)
        {
          spawned.push_back(hit.x);
        }
        else if (hit.address == _loop.angry)
        {
          angered.push_back(hit.a);
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

      Assert::AreEqual(stationSpawns, _frame.effects.stationSpawns, (_context + L": NWSPS calls").c_str());
      Assert::AreEqual(pitched.size(), _frame.effects.pitched.size(), (_context + L": NOISE2 calls").c_str());
      for (std::size_t index = 0; index < pitched.size(); ++index)
      {
        const std::wstring where = _context + L": NOISE2 " + std::to_wstring(index);
        Assert::AreEqual(pitched[index].y, _frame.effects.pitched[index].effect, (where + L" effect").c_str());
        Assert::AreEqual(pitched[index].a, _frame.effects.pitched[index].sustain, (where + L" sustain").c_str());
        Assert::AreEqual(pitched[index].x, _frame.effects.pitched[index].frequency, (where + L" frequency").c_str());
      }

      // ---- the seams -----------------------------------------------------------------------------
      {
        std::wstring wanted;
        for (const std::uint8_t effect : sounds)
        {
          wanted += std::to_wstring(effect) + L" ";
        }
        std::wstring got;
        for (const std::uint8_t effect : _frame.world.effects.sounds)
        {
          got += std::to_wstring(effect) + L" ";
        }
        Assert::AreEqual(sounds.size(), _frame.world.effects.sounds.size(),
                         (_context + L": sounds asked for -- game [" + wanted + L"] port [" + got + L"]").c_str());
      }
      for (std::size_t index = 0; index < sounds.size(); ++index)
      {
        Assert::AreEqual(sounds[index], _frame.world.effects.sounds[index], (_context + L": sound " + std::to_wstring(index)).c_str());
      }

      Assert::AreEqual(spawned.size(), _frame.effects.spawned.size(), (_context + L": FRS1 calls").c_str());
      for (std::size_t index = 0; index < spawned.size(); ++index)
      {
        Assert::AreEqual(spawned[index], _frame.effects.spawned[index], (_context + L": FRS1 type").c_str());
      }

      Assert::AreEqual(angered.size(), _frame.effects.angered.size(), (_context + L": ANGRY calls").c_str());
      for (std::size_t index = 0; index < angered.size(); ++index)
      {
        Assert::AreEqual(angered[index], _frame.effects.angered[index], (_context + L": ANGRY type").c_str());
      }

      Assert::AreEqual(starts, _frame.effects.musicStarts, (_context + L": startbd").c_str());
      Assert::AreEqual(stops, _frame.effects.musicStops, (_context + L": stopbd").c_str());
      Assert::AreEqual<std::uint32_t>(cpu.memory[TRUMBLE_PROBE], _frame.effects.trumbleMoves, (_context + L": MVTRIBS").c_str());

      // ---- the world -----------------------------------------------------------------------------
      CompareScreens(cpu, _at.screen, _frame.world.canvas, 0x1Du, _context);
      CompareState(cpu, _frame.world, _at, _context, _frame.outside.clouds == 0u);
      CompareFrame(cpu, _frame, _loop, _context);

      /*
       * The heap, minus what an unseeded explosion cloud owns.
       *
       * `LL9`'s `EE55` block writes six bytes at the head of a newly killed ship's run and four of
       * them come from `DORND` -- on a carry that arrives out of `LOIN`, through `EE51`, and the port
       * has no exit carry for `LOIN` to give it. So the cloud stays behind `SeedExplosionCloud`, and
       * the six bytes it owns plus the generator are the only things this comparison leaves out
       * (§6.91). Everything else on a frame that kills a ship is still compared.
       */
      auto seededHere = [&](std::uint16_t _address)
      {
        for (const std::uint16_t cloud : _frame.outside.seeded)
        {
          if (_address >= static_cast<std::uint16_t>(cloud + 1u) && _address <= static_cast<std::uint16_t>(cloud + 6u))
          {
            return true;
          }
        }
        return false;
      };

      for (std::uint16_t address = HEAP_START; address < Elite::LineHeap::TOP; ++address)
      {
        if (seededHere(address))
        {
          continue;
        }
        Assert::AreEqual(cpu.memory[address], _frame.heap.Read(address), (_context + L": heap byte " + std::to_wstring(address)).c_str());
      }

      const std::uint16_t bottom =
        static_cast<std::uint16_t>(cpu.memory[_loop.slsp] | (cpu.memory[static_cast<std::uint16_t>(_loop.slsp + 1u)] << 8));
      Assert::AreEqual<std::uint32_t>(bottom, _frame.world.bubble.heapBottom, (_context + L": SLSP").c_str());

      for (std::size_t slot = 0; slot < _frame.world.bubble.slots.size(); ++slot)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(_at.frin + slot)], _frame.world.bubble.slots[slot],
                         (_context + L": FRIN " + std::to_wstring(slot)).c_str());
      }
      for (std::size_t type = 0; type < _frame.world.bubble.counts.size(); ++type)
      {
        Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(_at.many + type)], _frame.world.bubble.counts[type],
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
            frame.control.roll = roll;
            frame.control.pitch = pitch;
            frame.options.dampingDisabled = ((mode & 1u) != 0u) ? 0xFFu : 0u;
            frame.control.dockingComputer = ((mode & 2u) != 0u) ? 0xFFu : 0u;
            frame.world.trumbles = ((roll & 1u) != 0u) ? 0u : 3u;

            const std::wstring where =
              WidenText("M% (JSTX " + std::to_string(roll) + ", JSTY " + std::to_string(pitch) + ", mode " + std::to_string(mode) + ")");
            CompareFrames(frame, oracle, at, loop, where);

            moved += frame.effects.trumbleMoves;
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
          frame.world.flight.delta = speed;
          frame.keys[Elite::KEY_SPEED_UP] = ((keys & 1u) != 0u) ? 0xFFu : 0u;
          frame.keys[Elite::KEY_SLOW_DOWN] = ((keys & 2u) != 0u) ? 0xFFu : 0u;

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
        frame.world.commander.At(Elite::Field::Missiles) = item.missiles;
        frame.world.bubble.missileTarget = item.target;
        frame.world.status.missileArmed = item.armed;
        frame.effects.spawnSucceeds = item.spawns;

        frame.keys[Elite::KEY_UNARM_MISSILE] = item.unarm ? 0xFFu : 0u;
        frame.keys[Elite::KEY_ARM_MISSILE] = item.arm ? 0xFFu : 0u;
        frame.keys[Elite::KEY_FIRE_MISSILE] = item.fire ? 0xFFu : 0u;

        // The five keys `BMI MA64` jumps over, all held, on every case.
        frame.keys[Elite::KEY_ENERGY_BOMB] = 0xFFu;
        frame.keys[Elite::KEY_CANCEL_DOCKING] = 0xFFu;
        frame.keys[Elite::KEY_ESCAPE_POD] = 0xFFu;
        frame.keys[Elite::KEY_ECM] = 0xFFu;
        frame.world.commander.At(Elite::Field::EnergyBomb) = 1u;
        frame.world.commander.At(Elite::Field::EscapePod) = 0u; // or the frame would end at ESCAPE
        frame.world.commander.At(Elite::Field::Ecm) = 0xFFu;
        frame.control.dockingComputer = 0xFFu;

        const std::wstring where = WidenText(std::string("M% (") + item.what + ")");
        CompareFrames(frame, oracle, at, loop, where);

        if (item.fire && (item.target & 0x80u) != 0u)
        {
          ++skipped;
          Assert::AreEqual<std::uint32_t>(0u, frame.effects.musicStops, (where + L": the cancel key was skipped").c_str());
          Assert::AreEqual<std::uint8_t>(1u, frame.world.commander.At(Elite::Field::EnergyBomb),
                                         (where + L": and so was the bomb").c_str());
        }
        if (item.fire && (item.target & 0x80u) == 0u && !item.spawns)
        {
          ++jammed;
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
        frame.keys[item.key] = 0xFFu;
        frame.keys[Elite::KEY_PITCH_UP] = item.pitchUp;
        frame.world.commander.At(Elite::Field::EnergyBomb) = item.bomb;
        frame.world.commander.At(Elite::Field::EscapePod) = (item.key == Elite::KEY_ESCAPE_POD) ? item.fitting : 0u;
        frame.world.commander.At(Elite::Field::Ecm) = (item.key == Elite::KEY_ECM) ? item.fitting : 0u;
        frame.world.commander.At(Elite::Field::DockingComputer) =
          (item.key == Elite::KEY_DOCKING_COMPUTER || item.key == Elite::KEY_PITCH_UP) ? item.fitting : 0u;
        frame.world.status.ecmCountdown = item.ecmCountdown;
        frame.world.status.midJump = item.midJump;
        frame.control.dockingComputer = item.dockingComputer;

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
              frame.world.spaceView = view;
              frame.world.view = 0u;
              frame.world.status.laserTemperature = heat;
              frame.world.status.laserCount = count;
              frame.keys[Elite::KEY_FIRE] = 0xFFu;
              for (std::size_t index = 0; index < 4u; ++index)
              {
                frame.world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers) + index] = (index == view) ? fitted : 0u;
              }

              const std::wstring where = WidenText("M% (VIEW " + std::to_string(view) + ", LASER " + std::to_string(fitted) + ", GNTMP " +
                                                   std::to_string(heat) + ", LASCT " + std::to_string(count) + ")");
              CompareFrames(frame, oracle, at, loop, where);

              fired += (frame.world.status.laserPower != 0u) ? 1u : 0u;
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
        frame.world.view = view;
        frame.world.spaceView = 0u;
        frame.world.status.laserTemperature = 40u;
        frame.keys[Elite::KEY_FIRE] = 0xFFu;
        frame.world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers)] = Elite::LASER_BEAM;

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
        frame.world.status.energy = energy;
        frame.world.status.laserTemperature = 40u;
        frame.keys[Elite::KEY_FIRE] = 0xFFu;
        frame.world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers)] = Elite::LASER_PULSE;

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
        {"the energy bomb going off", 0x20, 0x00, 0, 0xFF, 0, 0, false},
        {"the bomb with the laser on", 0x00, 0x00, 15, 0xFF, 0, 0, false},
        {"far enough to leave", 0xF0, 0x00, 0, 0, 0, 0, false},
        {"a beam laser at close range", 0x00, 0x00, 143 & 0x7F, 0, 0, 0, false},
        {"a mining laser at close range", 0x00, 0x00, 50, 0, 0, 0, false},
        {"a military laser at close range", 0x00, 0x00, 151 & 0x7F, 0, 0, 0, false},
      };

      std::uint32_t compared = 0;
      std::uint32_t killed = 0;

      for (const Case& item : CASES)
      {
        for (const std::uint8_t missileArmed : {std::uint8_t{0}, std::uint8_t{0xFF}})
        {
          Frame frame(0x4Du);
          PopulateBubble(frame, item.distance, item.state, item.empty);
          frame.world.status.laserPower = item.laser;
          frame.world.status.missileArmed = missileArmed;
          frame.world.commander.At(Elite::Field::EnergyBomb) = item.bomb;
          frame.world.commander.At(Elite::Field::FuelScoops) = item.scoops;
          frame.world.view = item.view;
          frame.world.commander.At(Elite::Field::Missiles) = 3u;

          const std::wstring where = WidenText(std::string("MAL1 (") + item.what + (missileArmed != 0u ? ", missile armed)" : ")"));
          CompareFrames(frame, oracle, at, loop, where, Reach::Ships);

          std::uint32_t left = 0;
          for (const std::uint8_t occupant : frame.world.bubble.slots)
          {
            left += (occupant != 0u) ? 1u : 0u;
          }
          killed += (!item.empty && left < 5u) ? 1u : 0u;
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(14u * 2u, compared, L"the whole sweep ran");
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

          frame.world.flight.mainLoopCounter = counter;
          frame.world.status.midJump = ((shape & 1u) != 0u) ? 0xFFu : 0u;
          frame.world.status.energy = ((shape & 2u) != 0u) ? 200u : 40u;
          frame.world.commander.At(Elite::Field::EnergyBomb) = (counter & 1u) != 0u ? 0xC0u : 0u;
          frame.world.commander.At(Elite::Field::EnergyUnit) = 1u;
          frame.world.commander.At(Elite::Field::FuelScoops) = 0xFFu;
          frame.world.status.viewLaser = 0x4Cu;
          frame.world.status.laserCount = static_cast<std::uint8_t>(counter & 15u);
          frame.world.status.ecmOurs = ((counter & 4u) != 0u) ? 0xFFu : 0u;
          frame.world.status.ecmCountdown = ((counter & 8u) != 0u) ? 1u : 0u;

          const std::wstring where = WidenText("MA18 (MCNT " + std::to_string(counter) + ", shape " + std::to_string(shape) + ")");
          CompareFrames(frame, oracle, at, loop, where, Reach::Tail);

          bombEnded += (frame.world.commander.At(Elite::Field::EnergyBomb) == 0x80u) ? 1u : 0u;
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
          frame.world.bubble.slots[1] = 129u;
          frame.world.bubble.counts[Elite::SHIP_TYPE_STATION] = 0u;
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            frame.world.bubble.blocks[1][byte] = 0u;
          }
          frame.world.bubble.blocks[1][1] = distance;
          frame.world.bubble.blocks[1][4] = distance;
          frame.world.bubble.blocks[1][7] = distance;

          frame.world.flight.mainLoopCounter = 20u;
          frame.world.status.midJump = 0u;
          frame.world.commander.At(Elite::Field::FuelScoops) = scoops;
          frame.world.commander.At(Elite::Field::Fuel) = 40u;
          frame.world.fuel = 40u;
          frame.world.commander.At(Elite::Field::Tribbles) = 0x40u;
          frame.world.commander.bytes[static_cast<std::size_t>(Elite::Field::Tribbles) + 1u] = 0x21u;
          frame.world.flight.delt4Next = 0xC0u;

          const std::wstring where =
            WidenText("MA33 (sun at " + std::to_string(distance) + (scoops != 0u ? ", scoops fitted)" : ", no scoops)"));
          CompareFrames(frame, oracle, at, loop, where, Reach::Tail);

          scooped += (frame.world.commander.At(Elite::Field::Fuel) > 40u) ? 1u : 0u;
          cooked += (frame.world.sight.maskedWith.empty() ? 0u : 1u);
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(20u * 2u, compared, L"the whole sweep ran");
      Assert::IsTrue(scooped > 0u, L"the tank filled on some passes");
      Assert::IsTrue(cooked > 0u, L"and the Trumbles died on some");
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

            frame.world.flight.mainLoopCounter = static_cast<std::uint8_t>(counter * 4u + shape);
            frame.control.roll = static_cast<std::uint8_t>(100u + counter * 5u);
            frame.control.pitch = static_cast<std::uint8_t>(150u - counter * 3u);
            frame.keys[Elite::KEY_FIRE] = ((shape & 1u) != 0u) ? 0xFFu : 0u;
            frame.keys[Elite::KEY_SPEED_UP] = ((shape & 2u) != 0u) ? 0xFFu : 0u;
            frame.world.commander.bytes[static_cast<std::size_t>(Elite::Field::Lasers)] = Elite::LASER_PULSE;
            frame.world.commander.At(Elite::Field::Missiles) = 3u;
            frame.world.commander.At(Elite::Field::EnergyUnit) = 1u;

            const std::wstring where = WidenText("M% whole (MCNT " + std::to_string(counter * 4u + shape) + ", distance " +
                                                 std::to_string(distance) + ", shape " + std::to_string(shape) + ")");
            CompareFrames(frame, oracle, at, loop, where, Reach::Whole);
            ++compared;
          }
        }
      }

      Assert::AreEqual<std::uint32_t>(8u * 3u * 4u, compared, L"the whole sweep ran");
    }
  };

} // namespace GameLogicTests
