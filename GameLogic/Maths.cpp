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
// What it leaves in AL after a byte divide overflows.
constexpr std::uint8_t DIVIDE_OVERFLOW_BYTE = 0x7F;
// The bits of the word at a return address that say DIV or IDIV: F6/F7 with reg 6 or 7.
constexpr std::uint16_t DIVIDE_OPCODE_MASK = 0x30FE;
constexpr std::uint16_t DIVIDE_OPCODE = 0x30F6;
constexpr std::uint16_t REGISTER_DIVIDE_BYTES = 2;

constexpr std::uint16_t ANGLE_MASK = 0x7FF;       // 2048 to a turn
constexpr std::uint16_t QUARTER_TURN = 0x200;     // in 2048ths
constexpr std::uint16_t EIGHTH_TURN = 0x100;      // in 2048ths
constexpr std::uint16_t ANGLE_SIGN = 0x400;       // the sign bit of an 11-bit angle
constexpr std::uint16_t ANGLE_EXTENSION = 0xF800; // the bits that sign-extend one
constexpr std::uint16_t SINE_WORDS = 0x800;
constexpr std::uint16_t COSINE_OFFSET_BYTES = 0x400; // a quarter turn of sineTable
constexpr std::uint16_t TANGENT_SEARCH_STEPS = 9;
constexpr std::uint16_t TANGENT_TOP_BYTES = 0x1FE;

// A slot's 24-bit position: the low words at +4, +6, +8, the high bytes at +1, +2, +3.
constexpr std::uint16_t POSITION_X_OFFSET = 4;
constexpr std::uint16_t POSITION_Y_OFFSET = 6;
constexpr std::uint16_t POSITION_Z_OFFSET = 8;
constexpr std::uint16_t POSITION_X_HIGH_OFFSET = 1;
constexpr std::uint16_t POSITION_Y_HIGH_OFFSET = 2;
constexpr std::uint16_t POSITION_Z_HIGH_OFFSET = 3;
constexpr std::uint16_t SCALE_SHIFT_OFFSET = 0x0A; // a slot's scale shift, a byte
constexpr std::uint16_t VIEW_X_OFFSET = 0x10;      // a slot's view position, words
constexpr std::uint16_t VIEW_Y_OFFSET = 0x12;
constexpr std::uint16_t VIEW_Z_OFFSET = 0x14;
constexpr std::uint32_t MASK_24_BITS = 0xFFFFFF;
constexpr std::uint32_t SIGN_24_BITS = 0x800000;
constexpr std::uint16_t SCALED_POSITION_LIMIT = 0x24B8; // GetPositionScaleShift's ceiling
// ScaleByInverseDistance's divide, and the instruction after it.
constexpr std::uint16_t SCALE_DIVIDE_RETURN = 0x40E2;
constexpr std::uint16_t SCALE_LIMIT = 0xFF;

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

// and r,r / jns / neg r: 8000h stays 8000h.
[[nodiscard]] std::uint16_t Magnitude(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0 ? Negate(_value) : _value;
}

// imul of a word by itself: DX:AX.
[[nodiscard]] std::uint32_t Square(std::uint16_t _value) noexcept
{
  const auto value = static_cast<std::int32_t>(static_cast<std::int16_t>(_value));
  return static_cast<std::uint32_t>(value * value);
}

[[nodiscard]] std::uint8_t Low(std::uint16_t _register) noexcept
{
  return static_cast<std::uint8_t>(_register);
}

[[nodiscard]] std::uint8_t High(std::uint16_t _register) noexcept
{
  return static_cast<std::uint8_t>(_register >> 8);
}

[[nodiscard]] std::uint16_t WithLow(std::uint16_t _register, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_register & 0xFF00) | _low);
}

[[nodiscard]] std::uint16_t WithHigh(std::uint16_t _register, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>((_register & 0x00FF) | (_high << 8));
}

[[nodiscard]] std::uint16_t Offset(std::uint16_t _base, std::uint16_t _bytes) noexcept
{
  return static_cast<std::uint16_t>(_base + _bytes);
}

// The magnitude of a 24-bit coordinate with its high byte at DS:_high and low word at DS:_low: not,
// not, add 1, adc 0, so that 800000h stays 800000h.
[[nodiscard]] std::uint32_t Magnitude24(const Guest& _guest, std::uint16_t _high, std::uint16_t _low) noexcept
{
  const std::uint32_t value = (std::uint32_t{_guest.Byte(_high)} << 16) | _guest.Word(_low);
  return (value & SIGN_24_BITS) != 0 ? (0u - value) & MASK_24_BITS : value;
}

// DivideOverflowInterrupt from its saves to its IRET, for a divide whose return address is
// _segment:_offset. Returns the offset it resumes at.
std::uint16_t TrapDivideOverflow(Guest& _guest, std::uint16_t _segment, std::uint16_t _offset)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.SetCodeWord(DIVIDE_SAVED_BX_OFFSET, regs.bx);
  _guest.SetCodeWord(DIVIDE_SAVED_DS_OFFSET, regs.ds);
  std::uint16_t resume = _offset;
  if ((_guest.FarWord(_segment, _offset) & DIVIDE_OPCODE_MASK) == DIVIDE_OPCODE)
  {
    resume = static_cast<std::uint16_t>(resume + REGISTER_DIVIDE_BYTES);
  }
  // Two bytes back: the opcode of a register divide, but the ModRM byte of a memory one.
  const std::uint8_t opcode = _guest.FarByte(_segment, static_cast<std::uint16_t>(resume - REGISTER_DIVIDE_BYTES));
  regs.ax = (opcode & 1) != 0 ? DIVIDE_OVERFLOW_WORD : WithLow(regs.ax, DIVIDE_OVERFLOW_BYTE);
  return resume;
}

} // namespace

void DivideOverflowInterrupt(Guest& _guest)
{
  // The return address IRET takes: IP, then CS.
  _guest.SetStackWord(0, TrapDivideOverflow(_guest, _guest.StackWord(2), _guest.StackWord(0)));
}

void DivideUnsigned(Guest& _guest, std::uint16_t _divisor, std::uint16_t _returnOffset)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint32_t dividend = (std::uint32_t{regs.dx} << 16) | regs.ax;
  if (_divisor == 0 || dividend / _divisor > 0xFFFF)
  {
    TrapDivideOverflow(_guest, _guest.CodeSegment(), _returnOffset);
    return;
  }
  regs.ax = static_cast<std::uint16_t>(dividend / _divisor);
  regs.dx = static_cast<std::uint16_t>(dividend % _divisor);
}

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
    _guest.SetCodeWord(DIVIDE_SAVED_BX_OFFSET, regs.bx);
    _guest.SetCodeWord(DIVIDE_SAVED_DS_OFFSET, regs.ds);
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

void VectorLength(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint32_t partial = Square(regs.ax) + Square(regs.cx); // CX:BX
  const std::uint32_t sum = partial + Square(regs.bx);
  // The 16 bits from the highest non-zero byte down, and how far that shifts the root back.
  auto top = static_cast<std::uint16_t>(sum >> 16);
  std::uint8_t shift = 8;
  if (High(top) == 0)
  {
    top = static_cast<std::uint16_t>(sum >> 8);
    shift = 4;
    if (High(top) == 0)
    {
      top = static_cast<std::uint16_t>(sum);
      shift = 0;
    }
  }
  std::uint16_t odd = 0xFFFF;
  std::uint8_t root = 0xFF;
  bool borrow = false;
  do
  {
    odd = static_cast<std::uint16_t>(odd + 2);
    ++root;
    borrow = top < odd;
    top = static_cast<std::uint16_t>(top - odd);
  } while (!borrow);
  regs.ax = static_cast<std::uint16_t>(root << shift);
  regs.bx = odd;
  regs.cx = static_cast<std::uint16_t>(((partial >> 16) & 0xFF00) | shift);
  regs.dx = top;
}

void ObjectWithinBox(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Word(Offset(regs.di, POSITION_X_OFFSET));
  regs.bx = _guest.Word(Offset(regs.di, POSITION_Y_OFFSET));
  regs.cx = _guest.Word(Offset(regs.di, POSITION_Z_OFFSET));
  VectorWithinBox(_guest);
}

void VectorWithinBox(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = Magnitude(regs.ax);
  bool within = regs.ax < regs.dx;
  if (within)
  {
    regs.bx = Magnitude(regs.bx);
    within = regs.bx < regs.dx;
  }
  if (within)
  {
    regs.cx = Magnitude(regs.cx);
    within = regs.cx < regs.dx;
  }
  _guest.SetFlag(Machine::FLAG_CARRY, within);
}

void RotatePitchYawRoll(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t x = regs.ax;
  regs.ax = regs.bx;
  regs.bx = regs.cx;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(0));
  _guest.Set(DS.rotateScratch, regs.ax);
  regs.ax = x;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(1));
  const std::uint16_t z = regs.bx;
  regs.bx = _guest.Get(DS.rotateScratch);
  _guest.Set(DS.rotateScratch, z);
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(2));
  regs.cx = _guest.Get(DS.rotateScratch);
}

void RotateBySinCos7210(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t x = regs.ax;
  regs.ax = regs.bx;
  regs.bx = regs.cx;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(7));
  _guest.Set(DS.rotateScratch, regs.bx);
  regs.bx = regs.ax;
  regs.ax = x;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(2));
  const std::uint16_t y = regs.bx;
  regs.bx = _guest.Get(DS.rotateScratch);
  _guest.Set(DS.rotateScratch, y);
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(1));
  const std::uint16_t rotatedX = regs.ax;
  regs.ax = _guest.Get(DS.rotateScratch);
  _guest.Set(DS.rotateScratch, rotatedX);
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(0));
  regs.cx = regs.bx;
  regs.bx = regs.ax;
  regs.ax = _guest.Get(DS.rotateScratch);
}

void ScaleByInverseDistance(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint32_t sum = Square(_guest.Word(Offset(regs.di, VIEW_X_OFFSET))) + Square(_guest.Word(Offset(regs.di, VIEW_Y_OFFSET))) +
                            Square(_guest.Word(Offset(regs.di, VIEW_Z_OFFSET)));
  // One more than the root of the high word, counted in BH: a count of 256 wraps to 0.
  auto remaining = static_cast<std::uint16_t>(sum >> 16);
  std::uint16_t odd = 0xFFFF;
  std::uint8_t count = 0;
  bool borrow = false;
  do
  {
    odd = static_cast<std::uint16_t>(odd + 2);
    ++count;
    borrow = remaining < odd;
    remaining = static_cast<std::uint16_t>(remaining - odd);
  } while (!borrow);
  regs.bx = static_cast<std::uint16_t>(count << 8);
  // DX:AX, as the caller gave it, shifted arithmetically right by the scale shift.
  const std::uint8_t shift = _guest.Byte(Offset(regs.di, SCALE_SHIFT_OFFSET));
  auto value = static_cast<std::int32_t>((std::uint32_t{regs.dx} << 16) | regs.ax);
  value >>= shift < 31 ? shift : 31;
  regs.ax = static_cast<std::uint16_t>(value);
  regs.dx = static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) >> 16);
  regs.cx = 0;
  DivideUnsigned(_guest, regs.bx, SCALE_DIVIDE_RETURN);
  if (regs.ax > SCALE_LIMIT)
  {
    regs.ax = SCALE_LIMIT;
  }
}

void ShiftRight24(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t count = High(regs.dx);
  if (count == 0)
  {
    return;
  }
  const auto high = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(Low(regs.dx))));
  auto value = static_cast<std::int32_t>((high << 16) | regs.ax);
  value >>= count < 31 ? count : 31;
  regs.ax = static_cast<std::uint16_t>(value);
  regs.dx = Low(static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) >> 16));
}

void GetPositionScaleShift(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  const std::uint32_t x = Magnitude24(_guest, Offset(slot, POSITION_X_HIGH_OFFSET), Offset(slot, POSITION_X_OFFSET));
  const std::uint32_t y = Magnitude24(_guest, Offset(slot, POSITION_Y_HIGH_OFFSET), Offset(slot, POSITION_Y_OFFSET));
  const std::uint32_t z = Magnitude24(_guest, Offset(slot, POSITION_Z_HIGH_OFFSET), Offset(slot, POSITION_Z_OFFSET));
  // DH:BX keeps the largest; DL:AX is left holding the last 24-bit difference.
  std::uint32_t largest = x >= y ? x : y;
  const std::uint32_t difference = (z - largest) & MASK_24_BITS;
  largest = z >= largest ? z : largest;
  std::uint8_t shift = 0;
  while (largest > 0xFFFF)
  {
    ++shift;
    largest >>= 1;
  }
  while (largest >= SCALED_POSITION_LIMIT)
  {
    ++shift;
    largest >>= 1;
  }
  regs.ax = static_cast<std::uint16_t>(difference);
  regs.bx = static_cast<std::uint16_t>(largest);
  regs.cx = WithLow(regs.cx, shift);
  regs.dx = static_cast<std::uint16_t>(difference >> 16);
}

void ScalePositionDown(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  regs.bp = regs.dx;
  regs.dx = WithLow(regs.bp, _guest.Byte(Offset(slot, POSITION_Z_HIGH_OFFSET)));
  regs.ax = _guest.Word(Offset(slot, POSITION_Z_OFFSET));
  ShiftRight24(_guest);
  regs.cx = regs.ax;
  regs.dx = WithLow(regs.bp, _guest.Byte(Offset(slot, POSITION_Y_HIGH_OFFSET)));
  regs.ax = _guest.Word(Offset(slot, POSITION_Y_OFFSET));
  ShiftRight24(_guest);
  regs.bx = regs.ax;
  regs.dx = WithLow(regs.bp, _guest.Byte(Offset(slot, POSITION_X_HIGH_OFFSET)));
  regs.ax = _guest.Word(Offset(slot, POSITION_X_OFFSET));
  ShiftRight24(_guest);
}

void ComputeAnglesToObject(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  GetPositionScaleShift(_guest);
  regs.dx = WithHigh(regs.dx, Low(regs.cx));
  ScalePositionDown(_guest);
  const std::uint16_t x = regs.ax;
  const std::uint16_t y = regs.bx;
  const std::uint16_t z = regs.cx;
  regs.ax = y;
  regs.bx = z;
  ArcTangent2(_guest);
  regs.bp = regs.ax;
  SetSinCos(_guest, DS.rotationSinCos.At(6));
  regs.bx = z;
  regs.ax = y;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(6));
  regs.ax = x;
  ArcTangent2(_guest);
  regs.bx = regs.ax;
  regs.ax = regs.bp;
}

void ConvertVectorToAngles(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const auto quarter = [](std::uint16_t _value) { return static_cast<std::uint16_t>(static_cast<std::int16_t>(_value) >> 2); };
  const std::uint16_t x = quarter(regs.ax);
  const std::uint16_t y = quarter(regs.bx);
  const std::uint16_t z = quarter(regs.cx);
  regs.cx = z;
  regs.ax = y;
  regs.bx = z;
  ArcTangent2(_guest);
  regs.bp = Negate(regs.ax);
  SetSinCos(_guest, DS.rotationSinCos.At(8));
  regs.bx = z;
  regs.ax = y;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(8));
  regs.ax = x;
  ArcTangent2(_guest);
  regs.bx = Negate(regs.ax);
  regs.ax = regs.bp;
}

namespace
{

using Machine::FLAG_CARRY;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DX;

constexpr Machine::NativeContract CLOBBERS_DX{REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_DX{REGISTER_AX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_CX_DX{REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_DX_BP{REGISTER_DX | REGISTER_BP, 0};
constexpr Machine::NativeContract CLOBBERS_BX_CX_DX{REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_CX_DX_BP{REGISTER_CX | REGISTER_DX | REGISTER_BP, 0};
constexpr Machine::NativeContract BOX_TEST{REGISTER_AX | REGISTER_BX | REGISTER_CX, FLAG_CARRY};

constexpr std::array ENTRIES = {
  NativeEntry{0x025E, "DivideOverflowInterrupt", &DivideOverflowInterrupt, PRESERVES_ALL, Machine::NativeReturn::Interrupt},
  NativeEntry{0x061C, "NextRandom", &NextRandom, PRESERVES_ALL},
  NativeEntry{0x23DB, "RotateBySinCos", &RotateBySinCos, CLOBBERS_DX_BP},
  NativeEntry{0x2421, "SetSinCos0", [](Guest& _guest) { SetSinCos(_guest, 0x41A0); }, PRESERVES_ALL},
  NativeEntry{0x2426, "SetSinCos1", [](Guest& _guest) { SetSinCos(_guest, 0x41A4); }, PRESERVES_ALL},
  NativeEntry{0x242B, "SetSinCos2", [](Guest& _guest) { SetSinCos(_guest, 0x41A8); }, PRESERVES_ALL},
  NativeEntry{0x2430, "SetSinCos3", [](Guest& _guest) { SetSinCos(_guest, 0x41AC); }, PRESERVES_ALL},
  NativeEntry{0x2435, "SetSinCos4", [](Guest& _guest) { SetSinCos(_guest, 0x41B0); }, PRESERVES_ALL},
  NativeEntry{0x243B, "SetSinCos5", [](Guest& _guest) { SetSinCos(_guest, 0x41B4); }, PRESERVES_ALL},
  NativeEntry{0x2441, "SetSinCos6", [](Guest& _guest) { SetSinCos(_guest, 0x41B8); }, PRESERVES_ALL},
  NativeEntry{0x2447, "SetSinCos8", [](Guest& _guest) { SetSinCos(_guest, 0x41C0); }, PRESERVES_ALL},
  NativeEntry{0x244D, "SetSinCos7", [](Guest& _guest) { SetSinCos(_guest, 0x41BC); }, PRESERVES_ALL},
  NativeEntry{0x2453, "RotateBySinCos0", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41A0); }, CLOBBERS_DX},
  NativeEntry{0x2465, "RotateBySinCos1", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41A4); }, CLOBBERS_DX},
  NativeEntry{0x246D, "RotateBySinCos2", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41A8); }, CLOBBERS_DX},
  NativeEntry{0x2475, "RotateBySinCos3", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41AC); }, CLOBBERS_DX},
  NativeEntry{0x247D, "RotateBySinCos4", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41B0); }, CLOBBERS_DX},
  NativeEntry{0x2485, "RotateBySinCos5", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41B4); }, CLOBBERS_DX},
  NativeEntry{0x248D, "RotateBySinCos6", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41B8); }, CLOBBERS_DX},
  NativeEntry{0x2495, "RotateBySinCos7", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41BC); }, CLOBBERS_DX},
  NativeEntry{0x249D, "RotateBySinCos8", [](Guest& _guest) { RotateByStoredSinCos(_guest, 0x41C0); }, CLOBBERS_DX},
  NativeEntry{0x24A5, "ArcTangent2", &ArcTangent2, CLOBBERS_BX_CX_DX},
  NativeEntry{0x24E5, "QuadrantArcTangent", &QuadrantArcTangent, CLOBBERS_BX_CX_DX},
  NativeEntry{0x24F7, "RatioArcTangent", &RatioArcTangent, CLOBBERS_BX_CX_DX},
  NativeEntry{0x2CDB, "AngleWithinTolerance", &AngleWithinTolerance, Machine::NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x2E96, "VectorLength", &VectorLength, CLOBBERS_BX_CX_DX},
  NativeEntry{0x2F65, "ObjectWithinBox", &ObjectWithinBox, BOX_TEST},
  NativeEntry{0x2F6E, "VectorWithinBox", &VectorWithinBox, BOX_TEST},
  NativeEntry{0x3EAC, "RotatePitchYawRoll", &RotatePitchYawRoll, PRESERVES_ALL},
  NativeEntry{0x3F02, "RotateBySinCos7210", &RotateBySinCos7210, PRESERVES_ALL},
  NativeEntry{0x40A4, "ScaleByInverseDistance", &ScaleByInverseDistance, CLOBBERS_CX_DX},
  NativeEntry{0x4326, "ShiftRight24", &ShiftRight24, PRESERVES_ALL},
  NativeEntry{0x4333, "GetPositionScaleShift", &GetPositionScaleShift, CLOBBERS_AX_DX},
  NativeEntry{0x439E, "ScalePositionDown", &ScalePositionDown, CLOBBERS_DX_BP},
  NativeEntry{0x4ECF, "ComputeAnglesToObject", &ComputeAnglesToObject, CLOBBERS_CX_DX_BP},
  NativeEntry{0x4F08, "ConvertVectorToAngles", &ConvertVectorToAngles, CLOBBERS_CX_DX_BP},
};

} // namespace

std::span<const NativeEntry> MathsEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
