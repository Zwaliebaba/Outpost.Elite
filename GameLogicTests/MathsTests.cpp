#include "pch.h"

#include "ComparisonRig.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t ARC_TANGENT_2 = 0x24A5;
constexpr std::uint16_t RATIO_ARC_TANGENT = 0x24F7;
constexpr std::uint16_t ANGLE_WITHIN_TOLERANCE = 0x2CDB;

} // namespace

// Constructed inputs for the ported arithmetic (plan §6.3): the edges the replays may not reach.
TEST_CLASS(MathsTests)
{
public:
  // 0/0, and every ratio of 2 or more, overflow RatioArcTangent's divide: the game's own trap handler
  // saturates the quotient, and the native routine must leave what it leaves.
  TEST_METHOD(RatioArcTangentAgreesThroughTheDivideTrap)
  {
    ComparisonRig rig("RatioArcTangent");
    const std::initializer_list<Inputs> calls = {{0, 0}, {5, 1},           {0xFFFF, 0x7FFF}, {1, 2},
                                                 {0, 1}, {0x7FFF, 0xFFFF}, {0x1234, 0x1235}, {3, 3}};
    for (const Inputs& inputs : calls)
      rig.Call(RATIO_ARC_TANGENT, inputs);
    rig.AssertAllAgreed(RATIO_ARC_TANGENT, calls.size());
  }

  TEST_METHOD(ArcTangent2AgreesInEveryQuadrantAndAtTheExtremes)
  {
    ComparisonRig rig("ArcTangent2");
    const std::initializer_list<Inputs> calls = {{0, 0},        {0x8000, 0}, {0, 0x8000},      {0xFFFF, 0xFFFF}, {100, 0xFF9C},
                                                 {0xFF9C, 100}, {1, 0x7FFF}, {0x8000, 0x8000}, {0x7FFF, 1},      {0x4000, 0x4000}};
    for (const Inputs& inputs : calls)
      rig.Call(ARC_TANGENT_2, inputs);
    rig.AssertAllAgreed(ARC_TANGENT_2, calls.size());
  }

  // AX and CX are angles, BX the tolerance.
  TEST_METHOD(AngleWithinToleranceAgreesAcrossTheWrap)
  {
    ComparisonRig rig("AngleWithinTolerance");
    const std::initializer_list<Inputs> calls = {{.ax = 0x7FF, .bx = 1, .cx = 0x010},    {.ax = 0x001, .bx = 0x20, .cx = 0x7FF},
                                                 {.ax = 0x400, .bx = 0x64, .cx = 0x3FF}, {.ax = 0x3FF, .bx = 0x64, .cx = 0x400},
                                                 {.ax = 0x123, .bx = 0, .cx = 0x123},    {.ax = 0xFFFF, .bx = 2, .cx = 0},
                                                 {.ax = 0x200, .bx = 0x400, .cx = 0x600}};
    for (const Inputs& inputs : calls)
      rig.Call(ANGLE_WITHIN_TOLERANCE, inputs);
    rig.AssertAllAgreed(ANGLE_WITHIN_TOLERANCE, calls.size());
  }
};

} // namespace GameLogicTests
