#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "ShipMove.h"
#include "ShipSlot.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The three sign-magnitude adders `MVEIT` is built from (slice 3a).
 *
 * A ship's position is three twenty-four bit SIGN-MAGNITUDE numbers, not two's complement, so
 * every addition is a comparison of signs followed by an add or a subtract, and the subtract has
 * to negate its own result when it crosses zero. These are that operation in three shapes, and
 * they are swept rather than sampled because the interesting behaviour is entirely at the
 * boundaries -- crossing zero, and the sign bit of each operand.
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

/// Values chosen for the boundaries: zero, one either side of it, the sign bit, and the extremes.
const std::vector<std::uint8_t> EDGES = { 0, 1, 2, 127, 128, 129, 254, 255 };

/// The three axes `MVEIT` calls these on: x at INWK+0, y at INWK+3, z at INWK+6.
const std::vector<std::uint8_t> AXES = { 0, 3, 6 };
} // namespace

TEST_CLASS(TheSignMagnitudeAdders)
{
public:
  /*
   * 6502: MVT1 and MVT1-2, which are the same routine two bytes apart.
   *
   * Both entry points are swept, because the two bytes are an `AND #128` and `MVEIT` calls each
   * of them -- on one path A holds nothing but a sign and on the other a whole coordinate byte,
   * and a port that wired both to the masked entry would agree on the first and not the second.
   */
  TEST_METHOD(AddingToACoordinateMatchesMVT1)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t rr = oracle.Label("R");
    const std::uint16_t mvt1 = oracle.Label("MVT1");

    std::uint32_t compared = 0;
    for (const bool masked : { false, true })
    {
      for (const std::uint8_t axis : AXES)
      {
        for (const std::uint8_t a : EDGES)
        {
          for (const std::uint8_t r : EDGES)
          {
            for (const std::uint8_t low : EDGES)
            {
              for (const std::uint8_t high : EDGES)
              {
                Cpu6502 cpu = oracle.Fresh();
                Elite::ShipBlock work;
                Elite::MathWorkspace math;

                // The three bytes of the coordinate, and the addend's low byte in R.
                const std::uint8_t bytes[3] = { low, high, static_cast<std::uint8_t>(a ^ 0x55u) };
                for (int byte = 0; byte < 3; ++byte)
                {
                  cpu.memory[static_cast<std::uint16_t>(inwk + axis + byte)] = bytes[byte];
                  work[axis + byte] = bytes[byte];
                }
                cpu.memory[rr] = r;
                math.r = r;

                cpu.a = a;
                cpu.x = axis;
                const Elite::Testing::RunResult run =
                  cpu.CallSubroutine(masked ? static_cast<std::uint16_t>(mvt1 - 2) : mvt1);
                Assert::IsTrue(run.completed, L"MVT1 returned");

                Elite::AddToShipCoordinate(work, math, a, axis, masked);

                const std::wstring where = Widen(
                  std::string(masked ? "MVT1-2" : "MVT1") + "(a=" + std::to_string(a) + ", r="
                  + std::to_string(r) + ", x=" + std::to_string(axis) + ", coord="
                  + std::to_string(low) + "/" + std::to_string(high) + ")");
                for (int byte = 0; byte < 3; ++byte)
                {
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + axis + byte)],
                                   work[axis + byte],
                                   (where + L": INWK+" + std::to_wstring(axis + byte)).c_str());
                }
                ++compared;
              }
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(2u * 3u * 8u * 8u * 8u * 8u, compared, L"the whole sweep ran");
  }

  /// 6502: MVT3 -- the same operation with the operands the other way round and the answer in K.
  TEST_METHOD(AddingACoordinateIntoKMatchesMVT3)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t kk = oracle.Label("K");
    const std::uint16_t mvt3 = oracle.Label("MVT3");

    std::uint32_t compared = 0;
    for (const std::uint8_t axis : AXES)
    {
      for (const std::uint8_t k1 : EDGES)
      {
        for (const std::uint8_t k2 : EDGES)
        {
          for (const std::uint8_t k3 : EDGES)
          {
            for (const std::uint8_t low : EDGES)
            {
              Cpu6502 cpu = oracle.Fresh();
              Elite::ShipBlock work;
              Elite::MathWorkspace math;

              const std::uint8_t bytes[3] = { low, static_cast<std::uint8_t>(low ^ 0x3Cu), k3 };
              for (int byte = 0; byte < 3; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(inwk + axis + byte)] = bytes[byte];
                work[axis + byte] = bytes[byte];
              }
              const std::uint8_t k[4] = { 0, k1, k2, k3 };
              for (int byte = 0; byte < 4; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(kk + byte)] = k[byte];
                math.k[byte] = k[byte];
              }

              cpu.x = axis;
              const Elite::Testing::RunResult run = cpu.CallSubroutine(mvt3);
              Assert::IsTrue(run.completed, L"MVT3 returned");

              Elite::AddShipCoordinateToK(work, math, axis);

              const std::wstring where =
                Widen("MVT3(K=" + std::to_string(k1) + "/" + std::to_string(k2) + "/"
                      + std::to_string(k3) + ", x=" + std::to_string(axis) + ")");
              for (int byte = 0; byte < 4; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(kk + byte)], math.k[byte],
                                 (where + L": K+" + std::to_wstring(byte)).c_str());
              }
              ++compared;
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(3u * 8u * 8u * 8u * 8u, compared, L"the whole sweep ran");
  }

  /*
   * 6502: MVT6 -- sixteen bits, and the sign comes back in A rather than being stored.
   *
   * The asymmetry is the thing to catch: the branch that crosses zero and negates returns the
   * sign UNCHANGED, while the one that does not returns it FLIPPED. Reading it the other way
   * round produces a routine that is right for half its inputs.
   */
  TEST_METHOD(AddingACoordinateIntoPMatchesMVT6)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t pp = oracle.Label("P");
    const std::uint16_t mvt6 = oracle.Label("MVT6");

    std::uint32_t compared = 0;
    for (const std::uint8_t axis : AXES)
    {
      for (const std::uint8_t a : EDGES)
      {
        for (const std::uint8_t p1 : EDGES)
        {
          for (const std::uint8_t p2 : EDGES)
          {
            for (const std::uint8_t low : EDGES)
            {
              Cpu6502 cpu = oracle.Fresh();
              Elite::ShipBlock work;
              Elite::MathWorkspace math;

              const std::uint8_t bytes[3] = { low, static_cast<std::uint8_t>(low ^ 0x81u), a };
              for (int byte = 0; byte < 3; ++byte)
              {
                cpu.memory[static_cast<std::uint16_t>(inwk + axis + byte)] = bytes[byte];
                work[axis + byte] = bytes[byte];
              }
              cpu.memory[static_cast<std::uint16_t>(pp + 1)] = p1;
              cpu.memory[static_cast<std::uint16_t>(pp + 2)] = p2;
              math.p1 = p1;
              math.p2 = p2;

              cpu.a = a;
              cpu.x = axis;
              const Elite::Testing::RunResult run = cpu.CallSubroutine(mvt6);
              Assert::IsTrue(run.completed, L"MVT6 returned");

              const std::uint8_t sign = Elite::AddShipCoordinateToP(work, math, a, axis);

              const std::wstring where =
                Widen("MVT6(a=" + std::to_string(a) + ", P=" + std::to_string(p1) + "/"
                      + std::to_string(p2) + ", x=" + std::to_string(axis) + ")");
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(pp + 1)], math.p1,
                               (where + L": P+1").c_str());
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(pp + 2)], math.p2,
                               (where + L": P+2").c_str());
              Assert::AreEqual(cpu.a, sign, (where + L": the sign it hands back").c_str());
              ++compared;
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(3u * 8u * 8u * 8u * 8u, compared, L"the whole sweep ran");
  }
};

} // namespace GameLogicTests
