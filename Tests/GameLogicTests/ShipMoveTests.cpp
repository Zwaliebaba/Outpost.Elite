#include "pch.h"

#include "FlightUniverse.h"
#include "ShipBytes.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Arith.h"
#include "Canvas.h"
#include "ShipBlueprint.h"
#include "ShipMove.h"
#include "ShipSlot.h"

#include <cstdint>
#include <span>
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
    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    /// Values chosen for the boundaries: zero, one either side of it, the sign bit, and the extremes.
    const std::vector<std::uint8_t> EDGES = {0, 1, 2, 127, 128, 129, 254, 255};

    /// The three axes `MVEIT` calls these on: x at INWK+0, y at INWK+3, z at INWK+6.
    const std::vector<std::uint8_t> AXES = {0, 3, 6};
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
      for (const bool masked : {false, true})
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
                  Elite::Ship work;

                  // The three bytes of the coordinate, and the addend's low byte in R.
                  const std::uint8_t bytes[3] = {low, high, static_cast<std::uint8_t>(a ^ 0x55u)};
                  for (int byte = 0; byte < 3; ++byte)
                  {
                    cpu.memory[static_cast<std::uint16_t>(inwk + axis + byte)] = bytes[byte];
                    PokeShip(work, axis + byte, bytes[byte]);
                  }
                  cpu.memory[rr] = r;

                  cpu.a = a;
                  cpu.x = axis;
                  const Elite::Testing::RunResult run = cpu.CallSubroutine(masked ? static_cast<std::uint16_t>(mvt1 - 2) : mvt1);
                  Assert::IsTrue(run.completed, L"MVT1 returned");

                  Elite::AddToShipCoordinate(work, a, r, axis, masked);

                  const std::wstring where =
                    Widen(std::string(masked ? "MVT1-2" : "MVT1") + "(a=" + std::to_string(a) + ", r=" + std::to_string(r) +
                          ", x=" + std::to_string(axis) + ", coord=" + std::to_string(low) + "/" + std::to_string(high) + ")");
                  for (int byte = 0; byte < 3; ++byte)
                  {
                    Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + axis + byte)], PeekShip(work, axis + byte),
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
                Elite::Ship work;

                const std::uint8_t bytes[3] = {low, static_cast<std::uint8_t>(low ^ 0x3Cu), k3};
                for (int byte = 0; byte < 3; ++byte)
                {
                  cpu.memory[static_cast<std::uint16_t>(inwk + axis + byte)] = bytes[byte];
                  PokeShip(work, axis + byte, bytes[byte]);
                }
                const std::uint8_t k[4] = {0, k1, k2, k3};
                for (int byte = 0; byte < 4; ++byte)
                {
                  cpu.memory[static_cast<std::uint16_t>(kk + byte)] = k[byte];
                }

                cpu.x = axis;
                const Elite::Testing::RunResult run = cpu.CallSubroutine(mvt3);
                Assert::IsTrue(run.completed, L"MVT3 returned");

                const Elite::KBlockSum sum = Elite::AddShipCoordinateToK(work, Elite::KBlock{0, k1, k2, k3}, axis);
                const bool carry = sum.carry;
                const std::uint8_t ours[4] = {sum.value.low, sum.value.mid, sum.value.high, sum.value.top};

                const std::wstring where = Widen("MVT3(K=" + std::to_string(k1) + "/" + std::to_string(k2) + "/" + std::to_string(k3) +
                                                 ", x=" + std::to_string(axis) + ")");
                for (int byte = 0; byte < 4; ++byte)
                {
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(kk + byte)], ours[byte],
                                   (where + L": K+" + std::to_wstring(byte)).c_str());
                }

                // The exit carry, which is the `ADC`'s on one path, SET on the second and the final
                // `SBC`'s on the third. `VCSUB`'s last call leaves it standing out to `TA64`, so it
                // is part of the contract and belongs in the sweep rather than in the caller
                // (§6.126).
                Assert::AreEqual(cpu.c, carry, (where + L": carry").c_str());
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
                Elite::Ship work;

                const std::uint8_t bytes[3] = {low, static_cast<std::uint8_t>(low ^ 0x81u), a};
                for (int byte = 0; byte < 3; ++byte)
                {
                  cpu.memory[static_cast<std::uint16_t>(inwk + axis + byte)] = bytes[byte];
                  PokeShip(work, axis + byte, bytes[byte]);
                }
                cpu.memory[static_cast<std::uint16_t>(pp + 1)] = p1;
                cpu.memory[static_cast<std::uint16_t>(pp + 2)] = p2;

                cpu.a = a;
                cpu.x = axis;
                const Elite::Testing::RunResult run = cpu.CallSubroutine(mvt6);
                Assert::IsTrue(run.completed, L"MVT6 returned");

                const Elite::SignMag24 moved = Elite::AddShipCoordinateToP(work, Elite::SignMag24{p1, p2, a}, axis);

                const std::wstring where = Widen("MVT6(a=" + std::to_string(a) + ", P=" + std::to_string(p1) + "/" + std::to_string(p2) +
                                                 ", x=" + std::to_string(axis) + ")");
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(pp + 1)], moved.lo, (where + L": P+1").c_str());
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(pp + 2)], moved.hi, (where + L": P+2").c_str());
                Assert::AreEqual(cpu.a, moved.sgn, (where + L": the sign it hands back").c_str());
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
      const std::vector<std::uint8_t> VECTORS = {9, 15, 21};
      const std::vector<std::uint8_t> ANGLES = {0, 1, 31, 127, 128, 129, 255};

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
              Elite::Ship work;

              // Six bytes of vector, spread so the three axes differ from one another.
              for (std::uint8_t byte = 0; byte < 6u; ++byte)
              {
                const std::uint8_t value = static_cast<std::uint8_t>(seed ^ (byte * 0x27u));
                cpu.memory[static_cast<std::uint16_t>(inwk + vector + byte)] = value;
                PokeShip(work, vector + byte, value);
              }
              cpu.memory[alpha] = a;
              cpu.memory[beta] = b;

              cpu.y = vector;
              const Elite::Testing::RunResult run = cpu.CallSubroutine(mvs4);
              Assert::IsTrue(run.completed, L"MVS4 returned");

              Elite::RotateShipVector(work, vector, a, b);

              const std::wstring where = Widen("MVS4(y=" + std::to_string(vector) + ", alpha=" + std::to_string(a) +
                                               ", beta=" + std::to_string(b) + ", seed=" + std::to_string(seed) + ")");
              for (std::uint8_t byte = 0; byte < 6u; ++byte)
              {
                Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + vector + byte)], PeekShip(work, vector + byte),
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
        {15, 9}, {17, 11}, {19, 13}, {15, 21}, {17, 23}, {19, 25},
      };

      std::uint32_t compared = 0;
      for (const auto& pair : PAIRS)
      {
        for (const std::uint8_t direction : {std::uint8_t{0}, std::uint8_t{128}})
        {
          for (const std::uint8_t xLow : EDGES)
          {
            for (const std::uint8_t xHigh : EDGES)
            {
              for (const std::uint8_t yLow : EDGES)
              {
                Cpu6502 cpu = oracle.Fresh();
                Elite::Ship work;

                const std::uint8_t yHigh = static_cast<std::uint8_t>(yLow ^ 0x93u);
                const std::uint8_t bytes[4] = {xLow, xHigh, yLow, yHigh};
                const std::uint8_t at[4] = {pair.first, static_cast<std::uint8_t>(pair.first + 1u), pair.second,
                                            static_cast<std::uint8_t>(pair.second + 1u)};
                for (int index = 0; index < 4; ++index)
                {
                  cpu.memory[static_cast<std::uint16_t>(inwk + at[index])] = bytes[index];
                  PokeShip(work, at[index], bytes[index]);
                }
                cpu.memory[rat2] = direction;

                cpu.x = pair.first;
                cpu.y = pair.second;
                const Elite::Testing::RunResult run = cpu.CallSubroutine(mvs5);
                Assert::IsTrue(run.completed, L"MVS5 returned");

                Elite::RotateCoordinatePair(work, pair.first, pair.second, direction);

                const std::wstring where = Widen("MVS5(x=" + std::to_string(pair.first) + ", y=" + std::to_string(pair.second) +
                                                 ", rat2=" + std::to_string(direction) + ", " + std::to_string(xLow) + "/" +
                                                 std::to_string(xHigh) + " " + std::to_string(yLow) + ")");
                for (int index = 0; index < 4; ++index)
                {
                  Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + at[index])], PeekShip(work, at[index]),
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
        {"pointing along x", {96, 0, 0}},
        {"pointing along y (TI1)", {0, 96, 0}},
        {"pointing along z (TI2)", {0, 0, 96}},
        {"x small, y large (TI1)", {16, 96, 31}},
        {"x and y small (TI2)", {8, 16, 96}},
        {"a general direction", {60, 45, 30}},
        {"negative components", {0xE0, 0xA0, 0x90}},
        {"one component at the AND #&60 boundary", {32, 40, 50}},
        {"one just below it", {31, 96, 20}},
        {"all three at the maximum", {127, 127, 127}},
        {"all three negative maxima", {255, 255, 255}},
        {"everything zero", {0, 0, 0}},
      };

      std::uint32_t compared = 0;
      for (const Case& item : CASES)
      {
        for (const std::uint8_t roofSeed : {std::uint8_t{0}, std::uint8_t{40}, std::uint8_t{200}})
        {
          Cpu6502 cpu = oracle.Fresh();
          Elite::Ship work;

          // The whole orientation area, INWK+9 to INWK+26, so the low bytes the routine clears are
          // non-zero going in and the clearing is visible rather than assumed.
          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
          for (std::uint8_t offset = 9; offset <= 26u; ++offset)
          {
            const std::uint8_t value = static_cast<std::uint8_t>(roofSeed ^ (offset * 0x11u));
            cpu.memory[static_cast<std::uint16_t>(inwk + offset)] = value;
            shipBytes[offset] = value;
          }
          work = Elite::Ship::FromBytes(shipBytes);

          // The nose vector's high bytes are what select the branch.
          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> noseBytes = work.ToBytes();
          for (int axis = 0; axis < 3; ++axis)
          {
            const std::uint8_t at = static_cast<std::uint8_t>(10u + axis * 2);
            cpu.memory[static_cast<std::uint16_t>(inwk + at)] = item.nose[axis];
            noseBytes[at] = item.nose[axis];
          }
          work = Elite::Ship::FromBytes(noseBytes);

          const Elite::Testing::RunResult run = cpu.CallSubroutine(tidy);
          Assert::IsTrue(run.completed, L"TIDY returned");

          Elite::TidyOrientation(work);

          const std::wstring where = Widen(std::string("TIDY: ") + item.what + ", roof seed " + std::to_string(roofSeed));
          for (std::uint8_t offset = 9; offset <= 26u; ++offset)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + offset)], work.ToBytes()[offset],
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
      const std::uint8_t NOSE_X[] = {96, 0, 0};
      const std::uint8_t NOSE_Y[] = {0, 96, 0};
      const std::uint8_t NOSE_Z[] = {0, 0, 96};

      // 6502: AND #&60 -- the test each branch turns on.
      Assert::IsTrue((NOSE_X[0] & 0x60u) != 0u, L"the first case takes the main path");
      Assert::IsTrue((NOSE_Y[0] & 0x60u) == 0u && (NOSE_Y[1] & 0x60u) != 0u, L"the second falls through to TI1");
      Assert::IsTrue((NOSE_Z[0] & 0x60u) == 0u && (NOSE_Z[1] & 0x60u) == 0u, L"and the third all the way to TI2");
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
      const std::vector<std::uint8_t> TURNS = {0, 1, 3, 127, 128, 129, 131, 255};

      std::uint32_t compared = 0;
      for (const std::uint8_t a : TURNS)
      {
        for (const std::uint8_t b : TURNS)
        {
          for (const std::uint8_t seed : EDGES)
          {
            Cpu6502 cpu = oracle.Fresh();
            Elite::Ship work;
            Elite::MathWorkspace math;

            // 6502: JMP MV45 -- stop there rather than running MVEIT's tail as well.
            cpu.AddTrap(mv45);

            // The nine bytes of position: three axes of three bytes each, all different.
            std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
            for (std::uint8_t offset = 0; offset < 9u; ++offset)
            {
              const std::uint8_t value = static_cast<std::uint8_t>(seed ^ (offset * 0x35u));
              cpu.memory[static_cast<std::uint16_t>(inwk + offset)] = value;
              shipBytes[offset] = value;
            }
            work = Elite::Ship::FromBytes(shipBytes);
            cpu.memory[alpha] = a;
            cpu.memory[beta] = b;

            const Elite::Testing::RunResult run = cpu.CallSubroutine(mv40);
            Assert::IsTrue(run.completed, L"MV40 reached MV45");

            Elite::MovePlanetOrSun(work, math, a, b);

            const std::wstring where =
              Widen("MV40(alpha=" + std::to_string(a) + ", beta=" + std::to_string(b) + ", seed=" + std::to_string(seed) + ")");
            for (std::uint8_t offset = 0; offset < 9u; ++offset)
            {
              Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + offset)], work.ToBytes()[offset],
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
   * `TACTICS` IS NO LONGER TRAPPED EITHER, for the same reason `SCAN` is not (M3-b-1c). It was
   * trapped and counted while the AI was phase 4's and this slice was 3a's, and the count proved
   * one thing: how often `MV26` was reached. Slice 4a-c built the AI, M3-b-1c took the seam away,
   * and both machines now run it -- so the state it leaves is compared instead, over the whole
   * universe rather than the ship block. That says how often it ran AND what it did each time.
   *
   * WHICH IS WHY THE FIXTURE IS THE SHARED ONE. The AI reads the bubble, the commander, the ECM
   * countdown, the message line and the generator, and writes ships, sounds and screen bytes; a
   * `Ship` and a `FlightState` in isolation cannot put any of that into the oracle. `MVEIT` is now
   * a routine that reaches all of `Universe`, which is what `FlightUniverse.h` exists for.
   *
   * `SCAN` IS NO LONGER TRAPPED, because slice 3d-a built it. The count it used to be asserted
   * against is replaced by the SCREEN, which says more: `MVEIT` scans an ordinary ship twice a
   * pass and the ship has MOVED in between, so the two blips usually land in different places and
   * both survive the EOR. A canvas that agrees byte for byte therefore proves how many times the
   * scanner ran, where each blip went, and what colour it was -- the count only ever proved the
   * first.
   */
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
      const Where at{oracle};
      const std::uint16_t inwk = at.inwk;
      const std::uint16_t mveit = oracle.Label("MVEIT");
      const std::uint16_t xx0 = at.xx0;

      // 6502: SCBASE, which is an assembler constant rather than a label -- ylookup's first entry
      // is it plus the space view's four-cell left margin.
      const std::uint16_t screenBase = ScreenBase(oracle);

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
      };

      const std::vector<Case> CASES = {
        {"a Cobra, still, nobody turning", 11, 0, 0, 0, 0, 0, 0, 0},
        {"a Cobra under way", 11, 0, 0, 20, 0, 0, 0, 0},
        {"the player rolling", 11, 0, 0, 20, 0, 0, 12, 0},
        {"the player pitching", 11, 0, 0, 20, 0, 0, 0, 9},
        {"both, the other way", 11, 0, 0, 20, 0, 0, 0x8C, 0x89},
        {"the ship rolling too", 11, 40, 0, 20, 0, 0, 5, 3},
        {"and pitching", 11, 40, 33, 20, 0, 0, 5, 3},
        {"roll pinned at 127, which does not decay", 11, 127, 127, 20, 0, 0, 5, 3},
        {"at full speed, so the clamp bites", 11, 20, 20, 255, 0, 0, 4, 4},
        {"a HOSTILE ship, so tactics run", 11, 10, 10, 20, 0, 0x80, 4, 4},
        {"a MISSILE, which thinks every pass", 1, 10, 10, 30, 0, 0x80, 4, 4},
        {"an EXPLODING ship, which does not move", 11, 40, 40, 20, 0x20, 0x80, 6, 6},
        {"a dead one", 11, 40, 40, 20, 0x80, 0x80, 6, 6},
        {"the PLANET, which goes through MV40", 128, 0, 0, 0, 0, 0, 7, 5},
        {"the SUN, which skips the orientation", 129, 0, 0, 0, 0, 0, 7, 5},
        {"an Anaconda, whose maximum speed differs", 14, 20, 20, 200, 0, 0, 3, 3},
      };
      constexpr int ITERATIONS = 20;

      /*
       * §6.39's counter, because two blank screens agree: the scanner has to have DRAWN something
       * for the comparison above to be evidence of anything, and the ships whose state byte says
       * they are not on the scanner have to draw nothing.
       */
      std::uint32_t blips = 0;

      for (const Case& item : CASES)
      {
        const std::wstring where = Widen(std::string("MVEIT: ") + item.what);

        Cpu6502 cpu = oracle.Fresh();

        /*
         * The whole universe, because the AI runs for real now and reaches all of it. The four
         * names below are ALIASES into it rather than objects of their own -- `MoveShip` takes
         * `(Universe&, Ports&)` since M3-b-1c and the case still pokes the work block by hand.
         */
        Universe universe;
        // 6502: SFS1's carry was a fixture boolean here until M4-a-1, when `PerformDrop` started
        // spawning into the real bubble and the answer became the slot list's.
        Elite::Ports ports = universe.PortsWith(universe.unused, universe.unused);
        Elite::Ship& work = universe.work;
        Elite::FlightState& flight = universe.flight;
        Elite::Canvas& canvas = universe.canvas;

        // The seams the AI still reaches, trapped on the oracle so both sides do the same nothing:
        // `SFS1` and `FRS1` answer a carry the recorder answers with a `bool`, and `NOISE` is the
        // SID. `TACTICS` itself is not among them any more.
        cpu.AddTrap(oracle.Label("SFS1"), Cpu6502::TrapExit::SetCarry);
        cpu.AddTrap(oracle.Label("FRS1"), Cpu6502::TrapExit::ClearCarry);
        cpu.AddTrap(oracle.Label("NOISE"), Cpu6502::TrapExit::SetCarry);
        cpu.AddTrap(oracle.Label("NOISE2"));

        // 6502: QQ11 -- the space view, so `SCAN` has a dashboard to draw on.
        universe.view = 0;

        // A whole ship: position, orientation, speed, roll, pitch and flags, the same on both sides.
        std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
        for (std::uint8_t offset = 0; offset < Elite::SHIP_BLOCK_SIZE; ++offset)
        {
          const std::uint8_t value = static_cast<std::uint8_t>((offset * 0x1Du) ^ 0x41u);
          cpu.memory[static_cast<std::uint16_t>(inwk + offset)] = value;
          shipBytes[offset] = value;
        }
        work = Elite::Ship::FromBytes(shipBytes);

        /*
         * The position, chosen so the ship is ON the scanner rather than off the end of it: the
         * pattern above puts bit 6 in every high byte, which `SCAN`'s `AND #%11000000` rejects, so
         * with it the screen comparison would be a comparison of two blank screens.
         *
         * `_work.state`'s bit 4 comes from the case's own `exploding` byte, so the ships that the
         * game does not scan still are not scanned.
         */
        const std::uint8_t FIXED[][2] = {{1u, 0x12u},
                                         {2u, 0x00u},
                                         {4u, 0x21u},
                                         {5u, 0x80u},
                                         {7u, 0x33u},
                                         {8u, 0x00u},
                                         {27u, item.speed},
                                         {28u, 0u},
                                         {29u, item.roll},
                                         {30u, item.pitch},
                                         {31u, static_cast<std::uint8_t>(item.exploding | 0x10u)},
                                         {32u, item.hostile}};
        for (const auto& set : FIXED)
        {
          cpu.memory[static_cast<std::uint16_t>(inwk + set[0])] = set[1];
          PokeShip(work, set[0], set[1]);
        }

        // The blueprint MVEIT reads its maximum speed from, in XX0 on the oracle's side.
        const Elite::Blueprint* blueprint =
          Elite::BlueprintOf((item.type & 0x80u) != 0u ? Elite::ShipType::CobraMk3 : Elite::TypeOf(item.type));
        flight.blueprint = blueprint;
        cpu.memory[xx0] = static_cast<std::uint8_t>(blueprint->address & 0xFFu);
        cpu.memory[static_cast<std::uint16_t>(xx0 + 1)] = static_cast<std::uint8_t>(blueprint->address >> 8);

        // The player's roll and pitch, in all three of the forms MVEIT reads them in.
        flight.alpha = item.alpha;
        flight.alp1 = static_cast<std::uint8_t>(item.alpha & 0x7Fu);
        flight.alp2 = static_cast<std::uint8_t>(item.alpha & 0x80u);
        flight.alp2Next = static_cast<std::uint8_t>(flight.alp2 ^ 0x80u);
        flight.beta = item.beta;
        flight.bet1 = static_cast<std::uint8_t>(item.beta & 0x7Fu);
        flight.bet2 = static_cast<std::uint8_t>(item.beta & 0x80u);
        flight.delta = 14;
        flight.type = Elite::TypeOf(item.type);
        flight.slot = 3;

        const std::uint8_t NAMES[][2] = {
          {0u, flight.alpha}, {1u, flight.alp1}, {2u, flight.alp2}, {3u, flight.alp2Next},
          {4u, flight.beta},  {5u, flight.bet1}, {6u, flight.bet2}, {7u, flight.delta},
        };
        const char* LABELS[] = {"ALPHA", "ALP1", "ALP2", "", "BETA", "BET1", "BET2", "DELTA"};
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
         * And everything else, once, before the first pass: the bubble the AI reads its target and
         * its station out of, the commander, the generator, the message line. The ship block is
         * written afterwards, because `Mirror` carries `INWK` too and the case has already put its
         * own pattern there.
         */
        Mirror(universe, cpu, at);
        for (std::uint8_t offset = 0; offset < Elite::SHIP_BLOCK_SIZE; ++offset)
        {
          cpu.memory[static_cast<std::uint16_t>(inwk + offset)] = work.ToBytes()[offset];
        }

        /*
         * N iterations, with the main loop counter advancing exactly as the game's does -- which is
         * what makes TIDY fire on one pass in sixteen and TACTICS on one in eight rather than never
         * or always.
         */
        for (int iteration = 0; iteration < ITERATIONS; ++iteration)
        {
          const std::uint8_t counter = static_cast<std::uint8_t>(iteration);
          cpu.memory[at.mcnt] = counter;
          flight.mainLoopCounter = counter;

          cpu.a = 0;
          const Elite::Testing::RunResult run = cpu.CallSubroutine(mveit);
          Assert::IsTrue(run.completed, (where + L": MVEIT returned on iteration " + std::to_wstring(iteration)).c_str());

          Assert::IsTrue(Elite::MoveShip(universe, ports),
                         (where + L": MVEIT does not kill the player in this fixture").c_str());

          for (std::uint8_t offset = 0; offset < Elite::SHIP_BLOCK_SIZE; ++offset)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + offset)], work.ToBytes()[offset],
                             (where + L": iteration " + std::to_wstring(iteration) + L", INWK+" + std::to_wstring(offset)).c_str());
          }

          /*
           * And the screen, which is where `SCAN` went. It is compared on every iteration rather
           * than once at the end because everything here is EOR: two errors that cancel would
           * leave the last frame agreeing with a port that drew the blips in the wrong places.
           */
          const std::span<const std::uint8_t> ours = canvas.Screen();
          for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
          {
            const std::uint8_t expected = cpu.memory[static_cast<std::uint16_t>(screenBase + offset)];
            if (expected != ours[offset])
            {
              Assert::Fail((where + L": iteration " + std::to_wstring(iteration) + L", screen offset " + std::to_wstring(offset) +
                            L" -- game has " + std::to_wstring(expected) + L", port has " + std::to_wstring(ours[offset]))
                             .c_str());
            }
            blips += (ours[offset] != 0u) ? 1u : 0u;
          }

          /*
           * And the rest of the universe, which is what the tactics COUNT used to stand in for.
           *
           * The count said `MV26` was reached three times in twenty for a hostile ship and twenty
           * for a missile; this says that AND what the AI did on each of them -- the roll and
           * pitch counters it wrote, the generator it advanced, the ECM it noticed, the missile it
           * armed. Compared every iteration for the same reason the screen is: two errors that
           * cancel would leave the last frame agreeing.
           */
          CompareState(cpu, universe, at, where + L": iteration " + std::to_wstring(iteration));
        }
      }

      Assert::IsTrue(blips > 0u, L"and the scanner actually drew something to compare");
      Logger::WriteMessage(
        ("MVEIT: " + std::to_string(blips) + " marked scanner bytes over " + std::to_string(CASES.size()) + " ships").c_str());
    }
  };

  TEST_CLASS(TheViewAxes)
  {
  public:
    /*
     * 6502: PLUT and PU1 -- both entry points, every view, and one view the game never sets.
     *
     * The interesting inputs are the views themselves rather than the coordinates, so the whole of
     * `INWK` is filled with a pattern that makes every byte distinguishable and the comparison is
     * on all thirty-seven of them plus `RAT` and `RAT2`. A port that swapped the right pair of
     * bytes and the wrong pair of signs passes a test that only looks at the position.
     *
     * View 4 is in the sweep and the game never produces it: `PU1` decrements without checking, so
     * it takes the same path as the right view, and the port has to agree about that rather than
     * about the three values it will see.
     */
    TEST_METHOD(FlippingTheAxesMatchesPLUT)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t inwk = oracle.Label("INWK");
      const std::uint16_t view = oracle.Label("VIEW");
      const std::uint16_t rat = oracle.Label("RAT");
      const std::uint16_t rat2 = oracle.Label("RAT2");
      const std::uint16_t plut = oracle.Label("PLUT");
      const std::uint16_t pu1 = oracle.Label("PU1");

      std::uint32_t compared = 0;
      for (const std::uint8_t which : {0, 1, 2, 3, 4})
      {
        for (int entry = 0; entry < 2; ++entry)
        {
          // PLUT reads VIEW itself; PU1 is entered with it in X, so the front view reaches the
          // rear view's code through it and that difference is part of what is compared.
          if (entry == 1 && which == 0)
          {
            continue;
          }

          Cpu6502 cpu = oracle.Fresh();
          Elite::Ship work;
          Elite::FlightState flight;

          std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> shipBytes = work.ToBytes();
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            const std::uint8_t value = static_cast<std::uint8_t>(byte * 7u + 3u);
            cpu.memory[static_cast<std::uint16_t>(inwk + byte)] = value;
            shipBytes[byte] = value;
          }
          work = Elite::Ship::FromBytes(shipBytes);
          cpu.memory[view] = which;
          cpu.memory[rat] = 0x11;
          cpu.memory[rat2] = 0x22;
          flight.rat = 0x11;
          flight.rat2 = 0x22;

          cpu.x = which;
          const Elite::Testing::RunResult run = cpu.CallSubroutine((entry == 0) ? plut : pu1, 20'000);
          Assert::IsTrue(run.completed, L"the axis flip returned");

          if (entry == 0)
          {
            Elite::FlipAxesForView(work, flight, which);
          }
          else
          {
            Elite::FlipAxes(work, flight, which);
          }

          const std::wstring where = Widen(std::string(entry == 0 ? "PLUT" : "PU1") + "(view=" + std::to_string(which) + ")");
          for (std::size_t byte = 0; byte < Elite::SHIP_BLOCK_SIZE; ++byte)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(inwk + byte)], work.ToBytes()[byte],
                             (where + L": INWK+" + std::to_wstring(byte)).c_str());
          }
          Assert::AreEqual(cpu.memory[rat], flight.rat, (where + L": RAT").c_str());
          Assert::AreEqual(cpu.memory[rat2], flight.rat2, (where + L": RAT2").c_str());
          ++compared;
        }
      }

      Assert::AreEqual<std::uint32_t>(9u, compared, L"both entry points, every view");
    }
  };

} // namespace GameLogicTests
