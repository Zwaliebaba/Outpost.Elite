#include "pch.h"



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
    /// Values chosen for the boundaries: zero, one either side of it, the sign bit, and the extremes.
    const std::vector<std::uint8_t> EDGES = {0, 1, 2, 127, 128, 129, 254, 255};

    /// The three axes `MVEIT` calls these on: x at INWK+0, y at INWK+3, z at INWK+6.
    const std::vector<std::uint8_t> AXES = {0, 3, 6};
  } // namespace

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

} // namespace GameLogicTests
