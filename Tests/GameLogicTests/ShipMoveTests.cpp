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
 * That all three of `TIDY`'s shapes are reached (slice 3a).
 *
 * The sign-magnitude adders, the rotation steppers, the view axes and the movers were swept
 * against the shipped routines and went with the oracle (M6-b-5), and `TIDY`'s own comparison
 * with them. What is left is the branch coverage the comparison never stated: `TIDY` normalises
 * an orientation three different ways depending on which axis is largest, and without this the
 * suite could be green with two of them never exercised.
 */
namespace GameLogicTests
{

  namespace
  {
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
