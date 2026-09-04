#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "FlightLoop.h"
#include "Rng.h"
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

    for (const std::uint8_t slot : { std::uint8_t{ 0 }, std::uint8_t{ 1 } })
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
          for (const bool viaM : { false, true })
          {
            cpu.a = static_cast<std::uint8_t>(seedByte);
            cpu.y = static_cast<std::uint8_t>(slot * Elite::SHIP_BLOCK_SIZE);

            const Elite::Testing::RunResult run = cpu.CallSubroutine(viaM ? m : mas2, 500);
            Assert::IsTrue(run.completed, L"MAS2 returned");

            const std::uint8_t ours =
              viaM ? Elite::LargestAxis(bubble, slot)
                   : Elite::LargestAxisFrom(bubble, slot, static_cast<std::uint8_t>(seedByte));

            const std::wstring where =
              Widen(std::string(viaM ? "m" : "MAS2") + "(slot " + std::to_string(slot) + ", A "
                    + std::to_string(seedByte) + ")");
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
                         Widen("MAS4(A " + std::to_string(seedByte) + ")").c_str());
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

    const std::uint8_t VALUES[] = { 0, 1, 2, 15, 16, 63, 64, 100, 127, 128, 180, 200, 254, 255 };

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
          const std::uint8_t BYTES[3] = { x, y, z };
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

          const std::wstring where = Widen("MAS3(" + std::to_string(x) + ", " + std::to_string(y)
                                           + ", " + std::to_string(z) + ")");
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
    Logger::WriteMessage(("MAS3: " + std::to_string(compared) + " sums, " + std::to_string(saturated)
                          + " saturated, " + std::to_string(answers.size()) + " distinct answers")
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

    const std::uint8_t VALUES[] = { 0, 1, 0x40, 0x7F, 0x80, 0x81, 0xC0, 0xFF };

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

            const std::wstring where =
              Widen("MAS1(low " + std::to_string(low) + ", high " + std::to_string(high)
                    + ", sign " + std::to_string(sign) + ", target " + std::to_string(target) + ")");

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

    for (const std::uint8_t docking : { std::uint8_t{ 0 }, std::uint8_t{ 1 }, std::uint8_t{ 0xFF } })
    {
      for (const std::uint8_t damping : { std::uint8_t{ 0 }, std::uint8_t{ 1 }, std::uint8_t{ 0xFF } })
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

          const std::wstring where =
            Widen("cntr(" + std::to_string(reading) + ", auto " + std::to_string(docking)
                  + ", DAMP " + std::to_string(damping) + ")");

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
      for (const std::uint8_t type : { std::uint8_t{ 3 }, std::uint8_t{ 5 }, std::uint8_t{ 17 } })
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

        const std::wstring where =
          Widen("SPIN2(count " + std::to_string(count) + ", type " + std::to_string(type) + ")");

        Assert::AreEqual<std::size_t>(cpu.trapHits.size(), effects.types.size(),
                                      (where + L": how many were spawned").c_str());
        for (std::size_t hit = 0; hit < cpu.trapHits.size(); ++hit)
        {
          Assert::AreEqual(cpu.trapHits[hit].a, effects.flags[hit],
                           (where + L": the AI flag of #" + std::to_wstring(hit)).c_str());
          Assert::AreEqual(cpu.trapHits[hit].x, effects.types[hit],
                           (where + L": the type of #" + std::to_wstring(hit)).c_str());
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
        for (const bool carry : { false, true })
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
          cpu.memory[static_cast<std::uint16_t>(xx0 + 1)] =
            static_cast<std::uint8_t>(blueprint >> 8);
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

          const std::wstring where =
            Widen("SPIN(type " + std::to_string(type) + ", seed " + std::to_string(seed)
                  + ", carry " + std::to_string(carry ? 1 : 0) + ")");

          Assert::AreEqual<std::size_t>(cpu.trapHits.size(), effects.types.size(),
                                        (where + L": how many were spawned").c_str());
          for (std::size_t hit = 0; hit < cpu.trapHits.size(); ++hit)
          {
            Assert::AreEqual(cpu.trapHits[hit].a, effects.flags[hit],
                             (where + L": the AI flag of #" + std::to_wstring(hit)).c_str());
            Assert::AreEqual(cpu.trapHits[hit].x, effects.types[hit],
                             (where + L": the type of #" + std::to_wstring(hit)).c_str());
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

} // namespace GameLogicTests
