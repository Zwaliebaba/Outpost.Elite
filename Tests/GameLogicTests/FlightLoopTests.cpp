#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "FlightLoop.h"
#include "ShipSlot.h"

#include <cstdint>
#include <set>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The flight loop's distance helpers (slice 3d-d-i).
 *
 * All four are small enough to sweep properly: `MAS2` and `MAS4` are exhaustive in the byte they
 * OR into, `MAS3` over every high byte a block can hold, and `MAS1` over the coordinates that
 * make its sixteen-bit doubling overflow -- which is the case its third byte exists for.
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
};

} // namespace GameLogicTests
