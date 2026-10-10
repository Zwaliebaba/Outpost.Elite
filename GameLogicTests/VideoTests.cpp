#include "pch.h"

#include "ComparisonRig.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t DRAW_CLIPPED_LINE = 0x1603;
constexpr std::uint16_t DRAW_DISC = 0x1826;
constexpr std::uint16_t FILL_SPAN = 0x1A07;
constexpr std::uint16_t FILL_TRIANGLE = 0x1BFB;
constexpr std::uint16_t DRAW_CHART_FRAME = 0x7C25;

// DivideOverflowInterrupt saves BX here, in the code segment, when a divide traps.
constexpr std::uint16_t DIVIDE_SAVED_BX = 0x02A1;

constexpr std::uint16_t DRAW_COLOR = 0x2CE4;
constexpr std::uint16_t DISC_FILL_BYTE = 0x2CE5;
constexpr std::uint16_t SUN_FRINGE_MASK = 0x2EF3;
constexpr std::uint16_t SCREEN_LAYOUT = 0xA7D0;

[[nodiscard]] constexpr std::uint16_t Word(int _value) noexcept
{
  return static_cast<std::uint16_t>(_value);
}

void SetDataByte(ComparisonRig& _rig, std::uint16_t _offset, std::uint8_t _value)
{
  _rig.Host().Ram().Write8(Elite::DataSegment(_rig.Program()), _offset, _value);
}

// The original ran every one of _offsets while a call of the routine at _entry was being compared:
// the instructions these inputs are here for.
void AssertExecuted(ComparisonRig& _rig, std::uint16_t _entry, std::initializer_list<std::uint16_t> _offsets)
{
  const auto& hooks = _rig.Host().Native().Hooks();
  const auto found = hooks.find(Machine::Memory::Linear(_rig.Program().loadSegment, _entry));
  Assert::IsTrue(found != hooks.end(), L"the routine is ported");
  for (const std::uint16_t offset : _offsets)
  {
    constexpr std::wstring_view DIGITS = L"0123456789ABCDEF";
    std::wstring message = L"CS:";
    for (int shift = 12; shift >= 0; shift -= 4)
      message += DIGITS[static_cast<std::size_t>((offset >> shift) & 0xF)];
    message += L" was not executed";
    Assert::IsTrue(found->second.executed.Contains(offset), message.c_str());
  }
}

} // namespace

// Constructed inputs for the ported drawing routines (plan §6.3): the paths the replays do not reach.
TEST_CLASS(VideoTests)
{
public:
  // (DX, BX) to (CX, AX): x from -30000 to 30000 wraps CX-DX past 32767, so the cut's IDIV overflows and
  // the game's own trap handler saturates the quotient.
  TEST_METHOD(DrawClippedLineAgreesThroughTheDivideTrap)
  {
    ComparisonRig rig("DrawClippedLine");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t code = rig.Program().loadSegment;
    ram.Write16(code, DIVIDE_SAVED_BX, 0x1234);
    rig.Call(DRAW_CLIPPED_LINE, {.ax = 5000, .bx = Word(-5000), .cx = Word(-30000), .dx = 30000});
    rig.AssertAllAgreed(DRAW_CLIPPED_LINE, 1);
    Assert::IsTrue(ram.Read16(code, DIVIDE_SAVED_BX) != 0x1234, L"the divide trapped");
  }

  // BX is the radius, DX the centre's x and CX its row. Radius 5 halves to 2, which skips the divide;
  // below 5 the disc is a sprite, clipped a byte at a time at the sides and a row at a time at the top
  // and bottom, or not drawn at all when it lies wholly off the buffer.
  TEST_METHOD(DrawDiscAgreesForSmallAndClippedDiscs)
  {
    ComparisonRig rig("DrawDisc");
    SetDataByte(rig, DRAW_COLOR, 2);
    SetDataByte(rig, SUN_FRINGE_MASK, 0);
    const std::initializer_list<Inputs> calls = {
      {.bx = 5, .cx = 60, .dx = 100},
      {.bx = 0, .cx = 60, .dx = 100},
      {.bx = 3, .cx = 60, .dx = Word(-1)},
      {.bx = 3, .cx = 60, .dx = Word(-10)},
      {.bx = 3, .cx = 60, .dx = Word(-3)},
      {.bx = 3, .cx = 60, .dx = 0xFE},
      {.bx = 3, .cx = 0, .dx = 100},
      {.bx = 3, .cx = Word(-10), .dx = 100},
      {.bx = 3, .cx = Word(-3), .dx = 100},
      {.bx = 3, .cx = 0x7F, .dx = 100},
      {.bx = 4, .cx = Word(-1), .dx = Word(-1)},
      {.bx = 2, .cx = 0x7E, .dx = 0xFF},
    };
    for (const Inputs& inputs : calls)
      rig.Call(DRAW_DISC, inputs);
    rig.AssertAllAgreed(DRAW_DISC, calls.size());
    AssertExecuted(rig, DRAW_DISC, {0x1872, 0x1875, 0x194F, 0x196A, 0x196D, 0x196F, 0x1971, 0x1974, 0x197E, 0x1985, 0x1988, 0x198A,
                                    0x198C, 0x198E, 0x1990, 0x1993, 0x1994, 0x1996, 0x1998, 0x19A5, 0x19A7, 0x19A9, 0x19E9, 0x19EB,
                                    0x19ED, 0x19EF, 0x19F1, 0x19F5, 0x19F7, 0x19F9, 0x19FB, 0x19FD, 0x19FF, 0x1A03, 0x1A05});
  }

  // DL and DH, the span's ends, in one byte of the row CL/2: both edge masks at once.
  TEST_METHOD(FillSpanAgreesWithinOneByte)
  {
    ComparisonRig rig("FillSpan");
    SetDataByte(rig, DISC_FILL_BYTE, 0xAA);
    const std::initializer_list<Inputs> calls = {{.cx = 120, .dx = 0x0504}, {.cx = 2, .dx = 0x0000}, {.cx = 254, .dx = 0xFFFC}};
    for (const Inputs& inputs : calls)
      rig.Call(FILL_SPAN, inputs);
    rig.AssertAllAgreed(FILL_SPAN, calls.size());
    AssertExecuted(rig, FILL_SPAN, {0x1A6D, 0x1A6F, 0x1A71, 0x1A73, 0x1A75, 0x1A77, 0x1A7B, 0x1A7D, 0x1A81, 0x1A83, 0x1A85, 0x1A89,
                                    0x1A8B, 0x1A8F, 0x1A91, 0x1A93, 0x1A95, 0x1A97, 0x1A99, 0x1A9B, 0x1A9C, 0x1A9D, 0x1A9E, 0x1A9F});
  }

  // A, B and C are (AX, DX), (BX, BP) and (CX, DI). Two x beyond 255 pass the first rejection; one row
  // with the ends off both sides; flat triangles whose edges leave the buffer at the right, on the
  // first edge or the second; and one whose rows inside the buffer all lie left of it.
  TEST_METHOD(FillTriangleAgreesOnClippedEdgeCases)
  {
    ComparisonRig rig("FillTriangle");
    const std::initializer_list<Inputs> calls = {
      {.ax = 300, .bx = 300, .cx = 10, .dx = 10, .di = 30, .bp = 20},
      {.ax = Word(-10), .bx = 100, .cx = 300, .dx = 50, .di = 50, .bp = 50},
      {.ax = 300, .bx = 100, .cx = 400, .dx = 10, .di = 60, .bp = 60},
      {.ax = 200, .bx = 400, .cx = 100, .dx = 10, .di = 60, .bp = 60},
      {.ax = 100, .bx = Word(-100), .cx = Word(-50), .dx = Word(-50), .di = 10, .bp = 10},
    };
    for (const Inputs& inputs : calls)
      rig.Call(FILL_TRIANGLE, inputs);
    rig.AssertAllAgreed(FILL_TRIANGLE, calls.size());
    AssertExecuted(rig, FILL_TRIANGLE, {0x1E88, 0x1E8A, 0x1EF4, 0x1F86, 0x1F88, 0x1FA2, 0x1FB5, 0x1FF7, 0x208B, 0x208F, 0x2091,
                                        0x2093, 0x2094, 0x2096, 0x2098, 0x2099, 0x209B, 0x209D, 0x209F, 0x20A1, 0x20A3, 0x20A6,
                                        0x20A8, 0x20AA, 0x20AC, 0x20AE, 0x20B0, 0x20B2, 0x20B6, 0x20B8, 0x20BA, 0x20BB, 0x20BE});
  }

  // From the cockpit (screenLayout 0) the chart frame clears the screen rather than setting the mode.
  TEST_METHOD(DrawChartFrameAgreesFromTheCockpit)
  {
    ComparisonRig rig("DrawChartFrame");
    SetDataByte(rig, SCREEN_LAYOUT, 0);
    rig.Call(DRAW_CHART_FRAME, {});
    rig.AssertAllAgreed(DRAW_CHART_FRAME, 1);
    AssertExecuted(rig, DRAW_CHART_FRAME, {0x7C38});
  }
};

} // namespace GameLogicTests
