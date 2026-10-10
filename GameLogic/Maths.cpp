#include "pch.h"

#include "Maths.h"

#include "DataOverlay.h"

#include <utility>

namespace Elite
{

namespace
{

// DivideOverflowInterrupt's scratch words in the code segment, which it writes before anything else.
constexpr std::uint16_t DIVIDE_SAVED_BX_OFFSET = 0x02A1;
constexpr std::uint16_t DIVIDE_SAVED_DS_OFFSET = 0x02A3;
// What it leaves in AX after a word divide overflows: the positive maximum (plan §7.1).
constexpr std::uint16_t DIVIDE_OVERFLOW_WORD = 0x7FFF;

constexpr std::uint16_t ANGLE_MASK = 0x7FF;       // 2048 to a turn
constexpr std::uint16_t QUARTER_TURN = 0x200;     // in 2048ths
constexpr std::uint16_t EIGHTH_TURN = 0x100;      // in 2048ths
constexpr std::uint16_t ANGLE_SIGN = 0x400;       // the sign bit of an 11-bit angle
constexpr std::uint16_t ANGLE_EXTENSION = 0xF800; // the bits that sign-extend one
constexpr std::uint16_t SINE_WORDS = 0x800;
constexpr std::uint16_t COSINE_OFFSET_BYTES = 0x400; // a quarter turn of sineTable
constexpr std::uint16_t TANGENT_SEARCH_STEPS = 9;
constexpr std::uint16_t TANGENT_TOP_BYTES = 0x1FE;

// imul, then shl ax,1 / rcl dx,1 / shl ax,1 / adc dx,0: the high word of 2ab, rounded on the bit below.
[[nodiscard]] std::uint16_t RoundedProduct(std::uint16_t _a, std::uint16_t _b) noexcept
{
  const auto product = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(_a)) * static_cast<std::int16_t>(_b));
  const std::uint32_t doubled = product << 1;
  return static_cast<std::uint16_t>((doubled >> 16) + ((doubled >> 15) & 1));
}

[[nodiscard]] std::uint16_t Negate(std::uint16_t _value) noexcept
{
  return static_cast<std::uint16_t>(0u - _value);
}

[[nodiscard]] std::uint16_t SignExtendAngle(std::uint16_t _angle) noexcept
{
  const auto angle = static_cast<std::uint16_t>(_angle & ANGLE_MASK);
  return (angle & ANGLE_SIGN) != 0 ? static_cast<std::uint16_t>(angle | ANGLE_EXTENSION) : angle;
}

} // namespace

void NextRandom(Guest& _guest)
{
  const std::uint16_t a = _guest.Get(DS.randomState0);
  const std::uint16_t b = _guest.Get(DS.randomState1);
  const std::uint16_t c = _guest.Get(DS.randomState2);
  const auto sum = static_cast<std::uint16_t>(a + b);
  _guest.Set(DS.randomState1, c);
  _guest.Set(DS.randomState0, b);
  _guest.Set(DS.randomState2, static_cast<std::uint16_t>(c + sum));
  _guest.Regs().ax = sum;
}

void SetSinCos(Guest& _guest, std::uint16_t _pair)
{
  Machine::Registers& regs = _guest.Regs();
  const auto index = static_cast<std::uint16_t>((regs.ax & ANGLE_MASK) * 2);
  const std::uint16_t sine = _guest.Word(DS.sineTable.At(index / 2));
  const auto cosineIndex = static_cast<std::uint16_t>((index + COSINE_OFFSET_BYTES) & (SINE_WORDS * 2 - 1));
  const std::uint16_t cosine = _guest.Word(DS.sineTable.At(cosineIndex / 2));
  _guest.SetWord(_pair, sine);
  _guest.SetWord(static_cast<std::uint16_t>(_pair + 2), cosine);
  regs.ax = sine;
  regs.bx = cosine;
}

void RotateBySinCos(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const auto a = static_cast<std::uint16_t>(regs.ax << 1);
  const auto b = static_cast<std::uint16_t>(regs.bx << 1);
  const std::uint16_t bCos = RoundedProduct(b, regs.bp);
  const std::uint16_t aCos = RoundedProduct(a, regs.bp);
  const std::uint16_t aSin = RoundedProduct(a, regs.di);
  const std::uint16_t bSin = RoundedProduct(b, regs.di);
  const auto rotatedA = static_cast<std::uint16_t>(aCos - bSin);
  regs.bx = static_cast<std::uint16_t>(bCos + aSin);
  regs.dx = bSin;
  regs.bp = rotatedA;
  regs.ax = rotatedA;
}

void RotateByStoredSinCos(Guest& _guest, std::uint16_t _pair)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t di = regs.di;
  const std::uint16_t bp = regs.bp;
  regs.di = _guest.Word(_pair);
  regs.bp = _guest.Word(static_cast<std::uint16_t>(_pair + 2));
  RotateBySinCos(_guest);
  regs.di = di;
  regs.bp = bp;
}

void ArcTangent2(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const bool negativeA = (regs.ax & 0x8000) != 0;
  const bool negativeB = (regs.bx & 0x8000) != 0;
  if (negativeA)
  {
    regs.ax = Negate(regs.ax);
  }
  if (negativeB)
  {
    regs.bx = Negate(regs.bx);
  }
  QuadrantArcTangent(_guest);
  std::uint16_t angle = regs.ax;
  if (negativeA == negativeB)
  {
    angle = Negate(angle);
  }
  angle = static_cast<std::uint16_t>(negativeA ? angle - QUARTER_TURN : angle + QUARTER_TURN);
  regs.ax = static_cast<std::uint16_t>(angle & ANGLE_MASK);
}

void QuadrantArcTangent(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (regs.bx < regs.ax)
  {
    std::swap(regs.ax, regs.bx);
    RatioArcTangent(_guest);
    return;
  }
  RatioArcTangent(_guest);
  regs.ax = static_cast<std::uint16_t>(Negate(regs.ax) + QUARTER_TURN);
}

void RatioArcTangent(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // DX:AX = AX * 32768, divided by BX.
  const std::uint32_t dividend = std::uint32_t{regs.ax} << 15;
  std::uint16_t ratio = DIVIDE_OVERFLOW_WORD;
  if (regs.bx == 0 || dividend / regs.bx > 0xFFFF)
  {
    // The divide overflows: DivideOverflowInterrupt saves BX and DS, saturates AX and leaves DX.
    Machine::Memory& ram = _guest.Host().Ram();
    ram.Write16(regs.cs, DIVIDE_SAVED_BX_OFFSET, regs.bx);
    ram.Write16(regs.cs, DIVIDE_SAVED_DS_OFFSET, regs.ds);
    regs.dx = static_cast<std::uint16_t>(dividend >> 16);
  }
  else
  {
    ratio = static_cast<std::uint16_t>(dividend / regs.bx);
    regs.dx = static_cast<std::uint16_t>(dividend % regs.bx);
  }
  regs.ax = ratio;
  if (ratio >= DIVIDE_OVERFLOW_WORD)
  {
    regs.ax = EIGHTH_TURN;
    return;
  }

  // A binary search of tangentTable's 256 words for the ratio, nine halvings, in byte offsets.
  std::uint16_t low = 0;
  std::uint16_t high = TANGENT_TOP_BYTES;
  std::uint16_t middle = 0;
  std::uint16_t steps = TANGENT_SEARCH_STEPS;
  for (;;)
  {
    middle = static_cast<std::uint16_t>(((low + high) >> 1) & 0xFFFE);
    const std::uint16_t tangent = _guest.Word(static_cast<std::uint16_t>(DS.tangentTable.offset + middle));
    if (tangent == ratio)
    {
      break;
    }
    if (tangent < ratio)
    {
      low = middle;
    }
    else
    {
      high = middle;
    }
    if (--steps == 0)
    {
      break;
    }
  }
  regs.ax = static_cast<std::uint16_t>(middle >> 1);
  regs.bx = middle;
  regs.cx = steps;
  regs.dx = high;
}

void AngleWithinTolerance(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t first = SignExtendAngle(regs.ax);
  regs.cx = SignExtendAngle(regs.cx);
  std::uint16_t difference = SignExtendAngle(static_cast<std::uint16_t>(first - regs.cx));
  if ((difference & 0x8000) != 0)
  {
    difference = Negate(difference);
  }
  _guest.SetFlag(Machine::FLAG_CARRY, difference < regs.bx);
  regs.dx = static_cast<std::uint16_t>(difference - regs.bx);
}

} // namespace Elite
