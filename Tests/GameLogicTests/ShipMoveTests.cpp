#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "ShipMove.h"
#include "ShipSlot.h"

#include <cstdint>
#include <string>
#include <utility>
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

/*
 * The two rotation steppers (slice 3a).
 *
 * `MVS4` turns one of a ship's three orientation vectors by the player's roll and pitch; `MVS5`
 * turns a pair of coordinates by a fixed sixteenth, which is how a ship's own roll and pitch are
 * applied. `MVEIT` calls the first three times and the second six.
 */
TEST_CLASS(TheRotationSteppers)
{
public:
  /// 6502: MVS4 -- four multiply-accumulates, and the subtractions are sign flips.
  TEST_METHOD(RotatingAVectorMatchesMVS4)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t alpha = oracle.Label("ALPHA");
    const std::uint16_t beta = oracle.Label("BETA");
    const std::uint16_t mvs4 = oracle.Label("MVS4");

    // The three vectors MVEIT rotates: the ship's nose, roof and side.
    const std::vector<std::uint8_t> VECTORS = { 9, 15, 21 };
    const std::vector<std::uint8_t> ANGLES = { 0, 1, 31, 127, 128, 129, 255 };

    std::uint32_t compared = 0;
    for (const std::uint8_t vector : VECTORS)
    {
      for (const std::uint8_t a : ANGLES)
      {
        for (const std::uint8_t b : ANGLES)
        {
          for (const std::uint8_t seed : EDGES)
          {
            Cpu6502 cpu = oracle.Fresh();
            Elite::ShipBlock work;
            Elite::MathWorkspace math;

            // Six bytes of vector, spread so the three axes differ from one another.
            for (std::uint8_t byte = 0; byte < 6u; ++byte)
            {
              const std::uint8_t value = static_cast<std::uint8_t>(seed ^ (byte * 0x27u));
              cpu.memory[static_cast<std::uint16_t>(inwk + vector + byte)] = value;
              work[vector + byte] = value;
            }
            cpu.memory[alpha] = a;
            cpu.memory[beta] = b;

            cpu.y = vector;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(mvs4);
            Assert::IsTrue(run.completed, L"MVS4 returned");

            Elite::RotateShipVector(work, math, vector, a, b);

            const std::wstring where =
              Widen("MVS4(y=" + std::to_string(vector) + ", alpha=" + std::to_string(a)
                    + ", beta=" + std::to_string(b) + ", seed=" + std::to_string(seed) + ")");
            for (std::uint8_t byte = 0; byte < 6u; ++byte)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + vector + byte)],
                               work[vector + byte],
                               (where + L": INWK+" + std::to_wstring(vector + byte)).c_str());
            }
            ++compared;
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(3u * 7u * 7u * 8u, compared, L"the whole sweep ran");
  }

  /*
   * 6502: MVS5 -- and the ordering is the assertion worth having.
   *
   * The routine computes both halves before writing either back to X, holding the first in K, so
   * the second half reads the value the first has not yet replaced. A port that wrote as it went
   * would feed the first result into the second and produce a rotation that is subtly wrong in a
   * way only a long run would show.
   */
  TEST_METHOD(RotatingAPairMatchesMVS5)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t rat2 = oracle.Label("RAT2");
    const std::uint16_t mvs5 = oracle.Label("MVS5");

    // The six pairs MVEIT rotates, three for roll and three for pitch.
    const std::vector<std::pair<std::uint8_t, std::uint8_t>> PAIRS = {
      { 15, 9 }, { 17, 11 }, { 19, 13 }, { 15, 21 }, { 17, 23 }, { 19, 25 },
    };

    std::uint32_t compared = 0;
    for (const auto& pair : PAIRS)
    {
      for (const std::uint8_t direction : { std::uint8_t{ 0 }, std::uint8_t{ 128 } })
      {
        for (const std::uint8_t xLow : EDGES)
        {
          for (const std::uint8_t xHigh : EDGES)
          {
            for (const std::uint8_t yLow : EDGES)
            {
              Cpu6502 cpu = oracle.Fresh();
              Elite::ShipBlock work;
              Elite::MathWorkspace math;

              const std::uint8_t yHigh = static_cast<std::uint8_t>(yLow ^ 0x93u);
              const std::uint8_t bytes[4] = { xLow, xHigh, yLow, yHigh };
              const std::uint8_t at[4] = { pair.first, static_cast<std::uint8_t>(pair.first + 1u),
                                           pair.second, static_cast<std::uint8_t>(pair.second + 1u) };
              for (int index = 0; index < 4; ++index)
              {
                cpu.memory[static_cast<std::uint16_t>(inwk + at[index])] = bytes[index];
                work[at[index]] = bytes[index];
              }
              cpu.memory[rat2] = direction;

              cpu.x = pair.first;
              cpu.y = pair.second;
              const Elite::Testing::RunResult run = cpu.CallSubroutine(mvs5);
              Assert::IsTrue(run.completed, L"MVS5 returned");

              Elite::RotateCoordinatePair(work, math, pair.first, pair.second, direction);

              const std::wstring where = Widen(
                "MVS5(x=" + std::to_string(pair.first) + ", y=" + std::to_string(pair.second)
                + ", rat2=" + std::to_string(direction) + ", " + std::to_string(xLow) + "/"
                + std::to_string(xHigh) + " " + std::to_string(yLow) + ")");
              for (int index = 0; index < 4; ++index)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + at[index])],
                                 work[at[index]],
                                 (where + L": INWK+" + std::to_wstring(at[index])).c_str());
              }
              ++compared;
            }
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(6u * 2u * 8u * 8u * 8u, compared, L"the whole sweep ran");
  }
};

} // namespace GameLogicTests
