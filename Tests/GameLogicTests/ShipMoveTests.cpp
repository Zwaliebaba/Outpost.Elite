#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "ShipBlueprint.h"
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

/*
 * 6502: TIDY, and the TIS3 it is the only caller of (slice 3a).
 *
 * `MVEIT` runs this on one ship every sixteenth iteration to undo the drift that `MVS4` and
 * `MVS5` accumulate. The sweep is chosen to reach all THREE of its shapes: it divides by whichever
 * component of the nose vector is large enough, and a port that always took the first branch
 * would be right until a ship pointed down an axis.
 */
TEST_CLASS(TidyingAShipsOrientation)
{
public:
  TEST_METHOD(TheWholeRoutineMatchesTIDY)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t tidy = oracle.Label("TIDY");

    /*
     * Nose vectors chosen for the branch, not for plausibility: a large first component takes the
     * main path, a small first and large second takes TI1, and two small ones take TI2 -- which
     * is a ship pointing very nearly along its own z axis.
     */
    struct Case
    {
      const char* what;
      std::uint8_t nose[3];
    };

    const std::vector<Case> CASES = {
      { "pointing along x", { 96, 0, 0 } },
      { "pointing along y (TI1)", { 0, 96, 0 } },
      { "pointing along z (TI2)", { 0, 0, 96 } },
      { "x small, y large (TI1)", { 16, 96, 31 } },
      { "x and y small (TI2)", { 8, 16, 96 } },
      { "a general direction", { 60, 45, 30 } },
      { "negative components", { 0xE0, 0xA0, 0x90 } },
      { "one component at the AND #&60 boundary", { 32, 40, 50 } },
      { "one just below it", { 31, 96, 20 } },
      { "all three at the maximum", { 127, 127, 127 } },
      { "all three negative maxima", { 255, 255, 255 } },
      { "everything zero", { 0, 0, 0 } },
    };

    std::uint32_t compared = 0;
    for (const Case& item : CASES)
    {
      for (const std::uint8_t roofSeed : { std::uint8_t{ 0 }, std::uint8_t{ 40 }, std::uint8_t{ 200 } })
      {
        Cpu6502 cpu = oracle.Fresh();
        Elite::ShipBlock work;

        // The whole orientation area, INWK+9 to INWK+26, so the low bytes the routine clears are
        // non-zero going in and the clearing is visible rather than assumed.
        for (std::uint8_t offset = 9; offset <= 26u; ++offset)
        {
          const std::uint8_t value = static_cast<std::uint8_t>(roofSeed ^ (offset * 0x11u));
          cpu.memory[static_cast<std::uint16_t>(inwk + offset)] = value;
          work[offset] = value;
        }

        // The nose vector's high bytes are what select the branch.
        for (int axis = 0; axis < 3; ++axis)
        {
          const std::uint8_t at = static_cast<std::uint8_t>(10u + axis * 2);
          cpu.memory[static_cast<std::uint16_t>(inwk + at)] = item.nose[axis];
          work[at] = item.nose[axis];
        }

        const Elite::Testing::RunResult run = cpu.CallSubroutine(tidy);
        Assert::IsTrue(run.completed, L"TIDY returned");

        Elite::MathWorkspace math;
        Elite::TidyOrientation(work, math);

        const std::wstring where =
          Widen(std::string("TIDY: ") + item.what + ", roof seed " + std::to_string(roofSeed));
        for (std::uint8_t offset = 9; offset <= 26u; ++offset)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + offset)], work[offset],
                           (where + L": INWK+" + std::to_wstring(offset)).c_str());
        }
        ++compared;
      }
    }

    Assert::AreEqual<std::uint32_t>(12u * 3u, compared, L"the whole sweep ran");
  }

  /*
   * And the branch coverage, stated rather than hoped for: all three of TIDY's shapes are reached
   * by the cases above. Without this the suite could be green with two of them never exercised.
   */
  TEST_METHOD(AllThreeOfTidysShapesAreReached)
  {
    const std::uint8_t NOSE_X[] = { 96, 0, 0 };
    const std::uint8_t NOSE_Y[] = { 0, 96, 0 };
    const std::uint8_t NOSE_Z[] = { 0, 0, 96 };

    // 6502: AND #&60 -- the test each branch turns on.
    Assert::IsTrue((NOSE_X[0] & 0x60u) != 0u, L"the first case takes the main path");
    Assert::IsTrue((NOSE_Y[0] & 0x60u) == 0u && (NOSE_Y[1] & 0x60u) != 0u,
                   L"the second falls through to TI1");
    Assert::IsTrue((NOSE_Z[0] & 0x60u) == 0u && (NOSE_Z[1] & 0x60u) == 0u,
                   L"and the third all the way to TI2");
  }
};

/*
 * 6502: MV40 -- the planet and sun path through MVEIT (slice 3a).
 *
 * Run by TRAPPING MV45, because MV40 is a branch of MVEIT rather than a subroutine of it: it is
 * reached by `JMP MV40` and leaves by `JMP MV45`, so calling it and letting it run would execute
 * MVEIT's tail as well. The trap is what makes "just this branch" a thing that can be compared.
 */
TEST_CLASS(MovingThePlanetAndTheSun)
{
public:
  TEST_METHOD(TheWorldTurningMatchesMV40)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t alpha = oracle.Label("ALPHA");
    const std::uint16_t beta = oracle.Label("BETA");
    const std::uint16_t mv40 = oracle.Label("MV40");
    const std::uint16_t mv45 = oracle.Label("MV45");

    // Roll and pitch: still, gentle either way, and the extremes of the sign-magnitude range.
    const std::vector<std::uint8_t> TURNS = { 0, 1, 3, 127, 128, 129, 131, 255 };

    std::uint32_t compared = 0;
    for (const std::uint8_t a : TURNS)
    {
      for (const std::uint8_t b : TURNS)
      {
        for (const std::uint8_t seed : EDGES)
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::ShipBlock work;
          Elite::MathWorkspace math;

          // 6502: JMP MV45 -- stop there rather than running MVEIT's tail as well.
          cpu.AddTrap(mv45);

          // The nine bytes of position: three axes of three bytes each, all different.
          for (std::uint8_t offset = 0; offset < 9u; ++offset)
          {
            const std::uint8_t value = static_cast<std::uint8_t>(seed ^ (offset * 0x35u));
            cpu.memory[static_cast<std::uint16_t>(inwk + offset)] = value;
            work[offset] = value;
          }
          cpu.memory[alpha] = a;
          cpu.memory[beta] = b;

          const Elite::Testing::RunResult run = cpu.CallSubroutine(mv40);
          Assert::IsTrue(run.completed, L"MV40 reached MV45");

          Elite::MovePlanetOrSun(work, math, a, b);

          const std::wstring where = Widen("MV40(alpha=" + std::to_string(a) + ", beta="
                                           + std::to_string(b) + ", seed=" + std::to_string(seed) + ")");
          for (std::uint8_t offset = 0; offset < 9u; ++offset)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + offset)], work[offset],
                             (where + L": INWK+" + std::to_wstring(offset)).c_str());
          }
          ++compared;
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(8u * 8u * 8u, compared, L"the whole sweep ran");
  }
};

/*
 * 6502: MVEIT -- slice 3a's acceptance criterion, in the plan's own words: "run MVEIT on a slot
 * with sampled orientations/speeds/roll/pitch for N iterations; byte-identical INWK".
 *
 * ITERATED rather than called once, because that is what the criterion asks for and because it is
 * a different question. A single call compares one step of arithmetic; N calls compare the
 * FEEDBACK -- the damping in the tail, the sixteenth-iteration `TIDY`, the acceleration cleared
 * each pass -- and a port that was wrong by one in a byte nothing immediately reads would agree
 * for one iteration and diverge over twenty.
 *
 * `SCAN` and `TACTICS` are trapped, and counted on both sides: they belong to slice 3d and phase
 * 4, and a port that called them a different number of times would be wrong about the loop even
 * if every byte of INWK agreed.
 */
namespace
{
/// Counts the two seams instead of performing them, so the comparison covers WHETHER they were
/// reached as well as what the arithmetic did.
class CountingEffects final : public Elite::ShipEffects
{
public:
  void UpdateScanner(Elite::ShipBlock&) override { ++scans; }
  void RunTactics(Elite::ShipBlock&) override { ++tactics; }

  std::uint32_t scans = 0;
  std::uint32_t tactics = 0;
};
} // namespace

TEST_CLASS(MovingAShip)
{
public:
  TEST_METHOD(TwentyIterationsMatchMVEIT)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t inwk = oracle.Label("INWK");
    const std::uint16_t mveit = oracle.Label("MVEIT");
    const std::uint16_t scan = oracle.Label("SCAN");
    const std::uint16_t tactics = oracle.Label("TACTICS");
    const std::uint16_t xx0 = oracle.Label("XX0");

    struct Case
    {
      const char* what;
      std::uint8_t type;
      std::uint8_t roll;      ///< INWK+29
      std::uint8_t pitch;     ///< INWK+30
      std::uint8_t speed;     ///< INWK+27
      std::uint8_t exploding; ///< INWK+31
      std::uint8_t hostile;   ///< INWK+32
      std::uint8_t alpha;
      std::uint8_t beta;

      /*
       * How often each seam is reached in twenty iterations, and these ARE the behaviour rather
       * than a side effect. Two scans a pass is the ordinary case -- `MV30` and the tail's
       * `JMP SCAN`; one is a ship that took a short path; none is the sun.
       */
      std::uint32_t scans;
      std::uint32_t tactics;
    };

    const std::vector<Case> CASES = {
      { "a Cobra, still, nobody turning", 11, 0, 0, 0, 0, 0, 0, 0, 40, 0 },
      { "a Cobra under way", 11, 0, 0, 20, 0, 0, 0, 0, 40, 0 },
      { "the player rolling", 11, 0, 0, 20, 0, 0, 12, 0, 40, 0 },
      { "the player pitching", 11, 0, 0, 20, 0, 0, 0, 9, 40, 0 },
      { "both, the other way", 11, 0, 0, 20, 0, 0, 0x8C, 0x89, 40, 0 },
      { "the ship rolling too", 11, 40, 0, 20, 0, 0, 5, 3, 40, 0 },
      { "and pitching", 11, 40, 33, 20, 0, 0, 5, 3, 40, 0 },
      { "roll pinned at 127, which does not decay", 11, 127, 127, 20, 0, 0, 5, 3, 40, 0 },
      { "at full speed, so the clamp bites", 11, 20, 20, 255, 0, 0, 4, 4, 40, 0 },
      { "a HOSTILE ship, so tactics run", 11, 10, 10, 20, 0, 0x80, 4, 4, 40, 3 },
      { "a MISSILE, which thinks every pass", 1, 10, 10, 30, 0, 0x80, 4, 4, 40, 20 },
      { "an EXPLODING ship, which does not move", 11, 40, 40, 20, 0x20, 0x80, 6, 6, 20, 0 },
      { "a dead one", 11, 40, 40, 20, 0x80, 0x80, 6, 6, 20, 0 },
      { "the PLANET, which goes through MV40", 128, 0, 0, 0, 0, 0, 7, 5, 20, 0 },
      { "the SUN, which skips the orientation", 129, 0, 0, 0, 0, 0, 7, 5, 0, 0 },
      { "an Anaconda, whose maximum speed differs", 14, 20, 20, 200, 0, 0, 3, 3, 40, 0 },
    };

    constexpr int ITERATIONS = 20;

    for (const Case& item : CASES)
    {
      const std::wstring where = Widen(std::string("MVEIT: ") + item.what);

      Cpu6502 cpu = oracle.Fresh();
      Elite::ShipBlock work;
      Elite::MathWorkspace math;
      Elite::FlightState flight;
      CountingEffects effects;

      // 6502: the two seams, trapped so the interpreter returns instead of running slice 3d's
      // dashboard and phase 4's AI.
      cpu.AddTrap(scan);
      cpu.AddTrap(tactics);

      // A whole ship: position, orientation, speed, roll, pitch and flags, the same on both sides.
      for (std::uint8_t offset = 0; offset < Elite::SHIP_BLOCK_SIZE; ++offset)
      {
        const std::uint8_t value = static_cast<std::uint8_t>((offset * 0x1Du) ^ 0x41u);
        cpu.memory[static_cast<std::uint16_t>(inwk + offset)] = value;
        work[offset] = value;
      }

      const std::uint8_t FIXED[][2] = { { 27u, item.speed }, { 28u, 0u },   { 29u, item.roll },
                                        { 30u, item.pitch }, { 31u, item.exploding },
                                        { 32u, item.hostile } };
      for (const auto& set : FIXED)
      {
        cpu.memory[static_cast<std::uint16_t>(inwk + set[0])] = set[1];
        work[set[0]] = set[1];
      }

      // The blueprint MVEIT reads its maximum speed from, in XX0 on the oracle's side.
      const std::uint16_t blueprint =
        Elite::BlueprintAddress((item.type & 0x80u) != 0u ? std::uint8_t{ 11 } : item.type);
      cpu.memory[xx0] = static_cast<std::uint8_t>(blueprint & 0xFFu);
      cpu.memory[static_cast<std::uint16_t>(xx0 + 1)] = static_cast<std::uint8_t>(blueprint >> 8);

      // The player's roll and pitch, in all three of the forms MVEIT reads them in.
      flight.alpha = item.alpha;
      flight.alp1 = static_cast<std::uint8_t>(item.alpha & 0x7Fu);
      flight.alp2 = static_cast<std::uint8_t>(item.alpha & 0x80u);
      flight.alp2Next = static_cast<std::uint8_t>(flight.alp2 ^ 0x80u);
      flight.beta = item.beta;
      flight.bet1 = static_cast<std::uint8_t>(item.beta & 0x7Fu);
      flight.bet2 = static_cast<std::uint8_t>(item.beta & 0x80u);
      flight.delta = 14;
      flight.type = item.type;
      flight.slot = 3;

      const std::uint8_t NAMES[][2] = {
        { 0u, flight.alpha }, { 1u, flight.alp1 }, { 2u, flight.alp2 }, { 3u, flight.alp2Next },
        { 4u, flight.beta },  { 5u, flight.bet1 }, { 6u, flight.bet2 }, { 7u, flight.delta },
      };
      const char* LABELS[] = { "ALPHA", "ALP1", "ALP2", "", "BETA", "BET1", "BET2", "DELTA" };
      for (const auto& named : NAMES)
      {
        const char* label = LABELS[named[0]];
        if (label[0] != '\0')
        {
          cpu.memory[oracle.Label(label)] = named[1];
        }
      }
      cpu.memory[static_cast<std::uint16_t>(oracle.Label("ALP2") + 1)] = flight.alp2Next;
      cpu.memory[oracle.Label("TYPE")] = item.type;
      cpu.memory[oracle.Label("XSAV")] = flight.slot;

      /*
       * N iterations, with the main loop counter advancing exactly as the game's does -- which is
       * what makes TIDY fire on one pass in sixteen and TACTICS on one in eight rather than never
       * or always.
       */
      std::uint32_t scans = 0;
      std::uint32_t tacticsRuns = 0;
      for (int iteration = 0; iteration < ITERATIONS; ++iteration)
      {
        const std::uint8_t counter = static_cast<std::uint8_t>(iteration);
        cpu.memory[oracle.Label("MCNT")] = counter;
        flight.mainLoopCounter = counter;

        // Count the seams on the oracle's side by stepping until it returns, noting each trap.
        cpu.a = 0;
        const Elite::Testing::RunResult run = cpu.CallSubroutine(mveit);
        Assert::IsTrue(run.completed,
                       (where + L": MVEIT returned on iteration " + std::to_wstring(iteration)).c_str());

        Elite::MoveShip(work, math, flight, effects, blueprint);

        for (std::uint8_t offset = 0; offset < Elite::SHIP_BLOCK_SIZE; ++offset)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + offset)], work[offset],
                           (where + L": iteration " + std::to_wstring(iteration) + L", INWK+"
                            + std::to_wstring(offset))
                             .c_str());
        }
      }
      (void)scans;
      (void)tacticsRuns;

      /*
       * The seam counts, which are the behaviour and not bookkeeping. An ordinary ship is scanned
       * TWICE a pass -- once at `MV30` and once by the tail's `JMP SCAN` -- while an exploding one
       * is scanned once and then has its "on the scanner" bit cleared instead, and the SUN is
       * never scanned at all because `MV40` skips the first and its early return skips the second.
       * The tactics counts are the loop spreading: a missile thinks every pass and everything else
       * one pass in eight, which with this slot lands three times in twenty.
       */
      Assert::AreEqual(item.scans, effects.scans, (where + L": how often the scanner was reached").c_str());
      Assert::AreEqual(item.tactics, effects.tactics, (where + L": how often tactics ran").c_str());
    }
  }
};

} // namespace GameLogicTests
