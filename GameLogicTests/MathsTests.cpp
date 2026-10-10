#include "pch.h"

#include "ComparisonRig.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t DIVIDE_OVERFLOW_INTERRUPT = 0x025E;
constexpr std::uint16_t ARC_TANGENT_2 = 0x24A5;
constexpr std::uint16_t RATIO_ARC_TANGENT = 0x24F7;
constexpr std::uint16_t ANGLE_WITHIN_TOLERANCE = 0x2CDB;
constexpr std::uint16_t VECTOR_LENGTH = 0x2E96;
constexpr std::uint16_t SCALE_BY_INVERSE_DISTANCE = 0x40A4;
constexpr std::uint16_t SET_SIN_COS_0 = 0x2421;
constexpr std::uint16_t SET_SIN_COS_1 = 0x2426;
constexpr std::uint16_t SET_SIN_COS_2 = 0x242B;
constexpr std::uint16_t ROTATE_ROLL_YAW_PITCH = 0x3EC7;

constexpr std::uint8_t DIVIDE_VECTOR = 0;
// Where Pc::CallInterrupt returns to; the divide trap reads the opcode two bytes before it.
constexpr std::uint16_t INTERRUPT_RETURN_OFFSET = 0xFFFF;
// Slot 4, empty at the title screen.
constexpr std::uint16_t SPARE_SLOT = 0x6A30;

// The original ran each of _offsets while the routine at _entry was compared with it.
void AssertExecuted(ComparisonRig& _rig, std::uint16_t _entry, std::initializer_list<std::uint16_t> _offsets)
{
  const auto& hooks = _rig.Host().Native().Hooks();
  const auto found = hooks.find(Machine::Memory::Linear(_rig.Program().loadSegment, _entry));
  Assert::IsTrue(found != hooks.end(), L"the routine is ported");
  for (const std::uint16_t offset : _offsets)
  {
    Assert::IsTrue(found->second.executed.Contains(offset), (L"reached CS:" + std::to_wstring(offset)).c_str());
  }
}

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

  // The handler entered as int 0 enters it, its return address CallInterrupt's FFFFh: the byte at
  // CS:FFFD stands for the divide's opcode, F6h saturating AL and F7h all of AX.
  TEST_METHOD(DivideOverflowInterruptAgreesForByteAndWordDivides)
  {
    ComparisonRig rig("DivideOverflowInterrupt");
    Machine::Registers& regs = rig.Host().Processor().Regs();
    const Machine::Registers saved = regs;
    const std::initializer_list<std::uint8_t> opcodes = {0xF6, 0xF7};
    for (const std::uint8_t opcode : opcodes)
    {
      rig.Host().Ram().Write8(regs.cs, static_cast<std::uint16_t>(INTERRUPT_RETURN_OFFSET - 2), opcode);
      regs.ax = 0x1234;
      regs.bx = 0x5678;
      regs.ds = Elite::DataSegment(rig.Program());
      rig.Host().CallInterrupt(DIVIDE_VECTOR);
      regs = saved;
    }
    rig.AssertAllAgreed(DIVIDE_OVERFLOW_INTERRUPT, opcodes.size());
    AssertExecuted(rig, DIVIDE_OVERFLOW_INTERRUPT, {0x028D, 0x0291});
  }

  // A sum of squares below 10000h takes the root of its low word; the extremes overflow nothing.
  TEST_METHOD(VectorLengthAgreesAtEveryScale)
  {
    ComparisonRig rig("VectorLength");
    const std::initializer_list<Inputs> calls = {{.ax = 1, .bx = 2, .cx = 3},
                                                 {},
                                                 {.ax = 0x40},
                                                 {.ax = 0x7FFF, .bx = 0x8000, .cx = 0x7FFF},
                                                 {.ax = 0xFFFF, .bx = 0x00FF, .cx = 0x0F00}};
    for (const Inputs& inputs : calls)
      rig.Call(VECTOR_LENGTH, inputs);
    rig.AssertAllAgreed(VECTOR_LENGTH, calls.size());
    AssertExecuted(rig, VECTOR_LENGTH, {0x2EBE, 0x2EC0, 0x2EC2});
  }

  // A body 10 units off on each axis: K / 256 is 256 or more and saturates, and K = 7FFFh overflows the
  // divide into the trap; a scale shift of 4 brings K = 1 below the limit.
  TEST_METHOD(ScaleByInverseDistanceAgreesWhenItSaturates)
  {
    ComparisonRig rig("ScaleByInverseDistance");
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    Machine::Memory& ram = rig.Host().Ram();
    const std::initializer_list<std::uint16_t> axes = {0x10, 0x12, 0x14};
    for (const std::uint16_t axis : axes)
      ram.Write16(data, static_cast<std::uint16_t>(SPARE_SLOT + axis), 10);
    const std::initializer_list<std::uint8_t> shifts = {0, 4};
    const std::initializer_list<std::uint16_t> scales = {0x64, 0x7FFF, 0x0001};
    for (const std::uint8_t shift : shifts)
    {
      ram.Write8(data, static_cast<std::uint16_t>(SPARE_SLOT + 0x0A), shift);
      for (const std::uint16_t scale : scales)
        rig.Call(SCALE_BY_INVERSE_DISTANCE, {.dx = scale, .di = SPARE_SLOT});
    }
    rig.AssertAllAgreed(SCALE_BY_INVERSE_DISTANCE, shifts.size() * scales.size());
    AssertExecuted(rig, SCALE_BY_INVERSE_DISTANCE, {0x40E7, 0x40E8, 0x0291});
  }

  // LaunchPlayerMissile's rotation, from three orientations of rotation pairs 0-2, of vectors along each axis
  // and at the extremes.
  TEST_METHOD(RotateRollYawPitchAgreesFromSeveralOrientations)
  {
    ComparisonRig rig("RotateRollYawPitch");
    const std::initializer_list<Inputs> vectors = {{.ax = 0x0100, .dx = 0x4444},
                                                   {.bx = 0xFF00, .dx = 0x4444},
                                                   {.cx = 0x4000, .dx = 0x4444},
                                                   {.ax = 0x7FFF, .bx = 0x8000, .cx = 0x7FFF, .dx = 0x4444, .di = 0x6666, .bp = 0x7777}};
    const std::initializer_list<std::uint16_t> angles = {0x0000, 0x0123, 0x0700};
    for (const std::uint16_t angle : angles)
    {
      rig.Call(SET_SIN_COS_0, {.ax = angle});
      rig.Call(SET_SIN_COS_1, {.ax = static_cast<std::uint16_t>(angle * 3)});
      rig.Call(SET_SIN_COS_2, {.ax = static_cast<std::uint16_t>(angle + 0x0345)});
      for (const Inputs& inputs : vectors)
        rig.Call(ROTATE_ROLL_YAW_PITCH, inputs);
    }
    rig.AssertAllAgreed(ROTATE_ROLL_YAW_PITCH, angles.size() * vectors.size());
  }
};

} // namespace GameLogicTests
