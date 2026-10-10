#include "pch.h"

#include "Maths.h"

#include "Arithmetic.h"
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

// The magnitude of a 24-bit coordinate: not, not, add 1, adc 0, so that 800000h stays 800000h.
[[nodiscard]] std::uint32_t Magnitude24(std::uint32_t _value) noexcept
{
  return (_value & SIGN_24_BITS) != 0 ? (0u - _value) & MASK_24_BITS : _value;
}

// What DivideOverflowInterrupt does first, whatever the divide: BX and DS kept in the code segment.
void SaveDivideRegisters(GameState& _state, std::uint16_t _bx, std::uint16_t _ds)
{
  _state.SetCodeWord(DIVIDE_SAVED_BX_OFFSET, _bx);
  _state.SetCodeWord(DIVIDE_SAVED_DS_OFFSET, _ds);
}

// DivideOverflowInterrupt after its saves, for a divide whose return address is _segment:_offset: the offset it resumes
// at, and AX as it leaves AX = _ax.
[[nodiscard]] DivideTrap SaturateDivide(const GameState& _state, std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _ax)
{
  std::uint16_t resume = _offset;
  if ((_state.FarWord(_segment, _offset) & DIVIDE_OPCODE_MASK) == DIVIDE_OPCODE)
  {
    resume = static_cast<std::uint16_t>(resume + REGISTER_DIVIDE_BYTES);
  }
  // Two bytes back: the opcode of a register divide, but the ModRM byte of a memory one.
  const std::uint8_t opcode = _state.FarByte(_segment, static_cast<std::uint16_t>(resume - REGISTER_DIVIDE_BYTES));
  return DivideTrap{resume, (opcode & 1) != 0 ? DIVIDE_OVERFLOW_WORD : WithLow(_ax, DIVIDE_OVERFLOW_BYTE)};
}

// The dividend of a word divide: DX:AX.
[[nodiscard]] std::uint32_t WordDividend(const Machine::Registers& _regs) noexcept
{
  return (std::uint32_t{_regs.dx} << 16) | _regs.ax;
}

// The game's memory with the data segment the registers hold, for the divide trap's register forms: the trap saves the DS
// the divide ran with, which code still on the registers holds there.
[[nodiscard]] GameState StateOnRegisters(Guest& _guest) noexcept
{
  return GameState(_guest.Host().Ram(), _guest.CodeSegment(), _guest.Regs().ds);
}

[[nodiscard]] std::uint16_t Word(std::int16_t _value) noexcept
{
  return static_cast<std::uint16_t>(_value);
}

[[nodiscard]] std::int16_t Signed(std::uint16_t _value) noexcept
{
  return static_cast<std::int16_t>(_value);
}

// A rotationSinCos entry's index from its offset, as BX holds it in the register contracts.
[[nodiscard]] std::size_t PairIndex(std::uint16_t _pairOffset) noexcept
{
  return static_cast<std::size_t>(_pairOffset - DS.rotationSinCos.offset) / decltype(DS.rotationSinCos)::ENTRY_BYTES;
}

} // namespace

void DivideUnsignedOnRegisters(Guest& _guest, std::uint16_t _divisor, std::uint16_t _returnOffset)
{
  Machine::Registers& regs = _guest.Regs();
  GameState state = StateOnRegisters(_guest);
  const WordQuotient divided = DivideUnsigned(state, WordDividend(regs), _divisor, _returnOffset, regs.bx);
  regs.ax = divided.quotient;
  regs.dx = divided.remainder;
}

void DivideByteOnRegisters(Guest& _guest, std::uint8_t _divisor)
{
  Machine::Registers& regs = _guest.Regs();
  GameState state = StateOnRegisters(_guest);
  const ByteQuotient divided = DivideByte(state, regs.ax, _divisor, regs.bx);
  regs.ax = Join(divided.remainder, divided.quotient);
}

void DivideWordOnRegisters(Guest& _guest, std::uint16_t _divisor)
{
  Machine::Registers& regs = _guest.Regs();
  GameState state = StateOnRegisters(_guest);
  const WordQuotient divided = DivideWord(state, WordDividend(regs), _divisor, regs.bx);
  regs.ax = divided.quotient;
  regs.dx = divided.remainder;
}

void DivideSignedWordOnRegisters(Guest& _guest, std::uint16_t _divisor)
{
  Machine::Registers& regs = _guest.Regs();
  GameState state = StateOnRegisters(_guest);
  const WordQuotient divided = DivideSignedWord(state, WordDividend(regs), _divisor, regs.bx);
  regs.ax = divided.quotient;
  regs.dx = divided.remainder;
}

// ── The routines ──

DivideTrap DivideOverflowInterrupt(GameState& _state, std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _ax, std::uint16_t _bx)
{
  SaveDivideRegisters(_state, _bx, _state.DataSegment());
  return SaturateDivide(_state, _segment, _offset, _ax);
}

WordQuotient DivideUnsigned(GameState& _state, std::uint32_t _dividend, std::uint16_t _divisor, std::uint16_t _returnOffset,
                            std::uint16_t _bx)
{
  if (_divisor == 0 || _dividend / _divisor > 0xFFFF)
  {
    const DivideTrap trap =
      DivideOverflowInterrupt(_state, _state.CodeSegment(), _returnOffset, static_cast<std::uint16_t>(_dividend), _bx);
    return WordQuotient{trap.ax, static_cast<std::uint16_t>(_dividend >> 16)};
  }
  return WordQuotient{static_cast<std::uint16_t>(_dividend / _divisor), static_cast<std::uint16_t>(_dividend % _divisor)};
}

ByteQuotient DivideByte(GameState& _state, std::uint16_t _dividend, std::uint8_t _divisor, std::uint16_t _bx)
{
  if (High(_dividend) >= _divisor)
  {
    SaveDivideRegisters(_state, _bx, _state.DataSegment());
    return ByteQuotient{DIVIDE_OVERFLOW_BYTE, High(_dividend)};
  }
  return ByteQuotient{static_cast<std::uint8_t>(_dividend / _divisor), static_cast<std::uint8_t>(_dividend % _divisor)};
}

WordQuotient DivideWord(GameState& _state, std::uint32_t _dividend, std::uint16_t _divisor, std::uint16_t _bx)
{
  const auto high = static_cast<std::uint16_t>(_dividend >> 16);
  if (high >= _divisor)
  {
    SaveDivideRegisters(_state, _bx, _state.DataSegment());
    return WordQuotient{DIVIDE_OVERFLOW_WORD, high};
  }
  return WordQuotient{static_cast<std::uint16_t>(_dividend / _divisor), static_cast<std::uint16_t>(_dividend % _divisor)};
}

WordQuotient DivideSignedWord(GameState& _state, std::uint32_t _dividend, std::uint16_t _divisor, std::uint16_t _bx)
{
  const bool dividendNegative = (_dividend & 0x80000000u) != 0;
  const bool divisorNegative = SignWord(_divisor) != 0;
  const std::uint32_t dividendMagnitude = dividendNegative ? 0u - _dividend : _dividend;
  const std::uint32_t divisorMagnitude = divisorNegative ? 0x10000u - _divisor : _divisor;
  const std::uint32_t quotient = (dividendMagnitude >> 16) >= divisorMagnitude ? 0x8000u : dividendMagnitude / divisorMagnitude;
  if ((quotient & 0x8000u) != 0)
  {
    SaveDivideRegisters(_state, _bx, _state.DataSegment());
    return WordQuotient{DIVIDE_OVERFLOW_WORD, static_cast<std::uint16_t>(_dividend >> 16)};
  }
  const std::uint32_t remainder = dividendMagnitude % divisorMagnitude;
  return WordQuotient{static_cast<std::uint16_t>(dividendNegative != divisorNegative ? 0u - quotient : quotient),
                      static_cast<std::uint16_t>(dividendNegative ? 0u - remainder : remainder)};
}

std::uint16_t NextRandom(GameState& _state)
{
  const std::uint16_t a = _state.Get(DS.randomState0);
  const std::uint16_t b = _state.Get(DS.randomState1);
  const std::uint16_t c = _state.Get(DS.randomState2);
  const auto sum = static_cast<std::uint16_t>(a + b);
  _state.Set(DS.randomState1, c);
  _state.Set(DS.randomState0, b);
  _state.Set(DS.randomState2, static_cast<std::uint16_t>(c + sum));
  return sum;
}

SinCos SinCosOf(const GameState& _state, std::uint16_t _angle)
{
  const auto index = static_cast<std::uint16_t>(_angle & ANGLE_MASK);
  const auto cosineIndex = static_cast<std::uint16_t>((index + COSINE_OFFSET_BYTES / 2) & (SINE_WORDS - 1));
  return SinCos{Signed(_state.Word(DS.sineTable.At(index))), Signed(_state.Word(DS.sineTable.At(cosineIndex)))};
}

SinCos SetSinCos(GameState& _state, std::size_t _pair, std::uint16_t _angle)
{
  const SinCos pair = SinCosOf(_state, _angle);
  const std::uint16_t at = DS.rotationSinCos.At(_pair);
  _state.SetWord(at, Word(pair.sine));
  _state.SetWord(static_cast<std::uint16_t>(at + 2), Word(pair.cosine));
  return pair;
}

Pair RotateBySinCos(Pair _point, SinCos _by)
{
  const auto a = static_cast<std::uint16_t>(Word(_point.first) << 1);
  const auto b = static_cast<std::uint16_t>(Word(_point.second) << 1);
  const std::uint16_t bCos = RoundedProduct(b, Word(_by.cosine));
  const std::uint16_t aCos = RoundedProduct(a, Word(_by.cosine));
  const std::uint16_t aSin = RoundedProduct(a, Word(_by.sine));
  const std::uint16_t bSin = RoundedProduct(b, Word(_by.sine));
  return Pair{Signed(static_cast<std::uint16_t>(aCos - bSin)), Signed(static_cast<std::uint16_t>(bCos + aSin))};
}

Pair RotateByStoredSinCos(const GameState& _state, std::size_t _pair, Pair _point)
{
  const std::uint16_t at = DS.rotationSinCos.At(_pair);
  return RotateBySinCos(_point, SinCos{Signed(_state.Word(at)), Signed(_state.Word(static_cast<std::uint16_t>(at + 2)))});
}

std::uint16_t ArcTangent2(GameState& _state, std::int16_t _a, std::int16_t _b)
{
  const bool negativeA = _a < 0;
  const bool negativeB = _b < 0;
  std::uint16_t angle = QuadrantArcTangent(_state, Magnitude(Word(_a)), Magnitude(Word(_b)));
  if (negativeA == negativeB)
  {
    angle = Negate(angle);
  }
  angle = static_cast<std::uint16_t>(negativeA ? angle - QUARTER_TURN : angle + QUARTER_TURN);
  return static_cast<std::uint16_t>(angle & ANGLE_MASK);
}

std::uint16_t QuadrantArcTangent(GameState& _state, std::uint16_t _a, std::uint16_t _b)
{
  if (_b < _a)
  {
    return RatioArcTangent(_state, _b, _a);
  }
  return static_cast<std::uint16_t>(Negate(RatioArcTangent(_state, _a, _b)) + QUARTER_TURN);
}

std::uint16_t RatioArcTangent(GameState& _state, std::uint16_t _numerator, std::uint16_t _denominator)
{
  // _numerator * 32768 over _denominator; a quotient that does not fit goes through the trap.
  const std::uint32_t dividend = std::uint32_t{_numerator} << 15;
  if (_denominator == 0 || dividend / _denominator > 0xFFFF)
  {
    SaveDivideRegisters(_state, _denominator, _state.DataSegment());
    return EIGHTH_TURN;
  }
  const auto ratio = static_cast<std::uint16_t>(dividend / _denominator);
  if (ratio >= DIVIDE_OVERFLOW_WORD)
  {
    return EIGHTH_TURN;
  }

  // A binary search of tangentTable's 256 words for the ratio, nine halvings, in byte offsets.
  std::uint16_t low = 0;
  std::uint16_t high = TANGENT_TOP_BYTES;
  std::uint16_t middle = 0;
  for (std::uint16_t steps = TANGENT_SEARCH_STEPS; steps != 0; --steps)
  {
    middle = static_cast<std::uint16_t>(((low + high) >> 1) & 0xFFFE);
    const std::uint16_t tangent = _state.Word(static_cast<std::uint16_t>(DS.tangentTable.offset + middle));
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
  }
  return static_cast<std::uint16_t>(middle >> 1);
}

AngleTolerance AngleWithinTolerance(std::uint16_t _a, std::uint16_t _b, std::uint16_t _tolerance)
{
  std::uint16_t difference = SignExtendAngle(static_cast<std::uint16_t>(SignExtendAngle(_a) - SignExtendAngle(_b)));
  if ((difference & 0x8000) != 0)
  {
    difference = Negate(difference);
  }
  return AngleTolerance{difference < _tolerance, static_cast<std::uint16_t>(difference - _tolerance)};
}

std::uint16_t VectorLength(Vector _vector)
{
  const std::uint32_t sum = Square(Word(_vector.x)) + Square(Word(_vector.z)) + Square(Word(_vector.y));
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
  return static_cast<std::uint16_t>(root << shift);
}

bool VectorWithinBox(Vector _vector, std::uint16_t _halfSize)
{
  // Each axis in turn, stopping at the first outside, as the original does.
  return Magnitude(Word(_vector.x)) < _halfSize && Magnitude(Word(_vector.y)) < _halfSize && Magnitude(Word(_vector.z)) < _halfSize;
}

bool ObjectWithinBox(const ObjectSlot& _slot, std::uint16_t _halfSize)
{
  return VectorWithinBox(Vector{Signed(_slot.Get(SlotWord::X)), Signed(_slot.Get(SlotWord::Y)), Signed(_slot.Get(SlotWord::Z))}, _halfSize);
}

Vector RotatePitchYawRoll(GameState& _state, Vector _vector)
{
  const Pair yz = RotateByStoredSinCos(_state, 0, Pair{_vector.y, _vector.z});
  _state.Set(DS.rotateScratch, Word(yz.first));
  const Pair xz = RotateByStoredSinCos(_state, 1, Pair{_vector.x, yz.second});
  _state.Set(DS.rotateScratch, Word(xz.second));
  const Pair xy = RotateByStoredSinCos(_state, 2, Pair{xz.first, yz.first});
  return Vector{xy.first, xy.second, xz.second};
}

Vector RotateRollYawPitch(const GameState& _state, Vector _vector)
{
  const Pair xy = RotateByStoredSinCos(_state, 2, Pair{_vector.x, _vector.y});
  const Pair xz = RotateByStoredSinCos(_state, 1, Pair{xy.first, _vector.z});
  const Pair yz = RotateByStoredSinCos(_state, 0, Pair{xy.second, xz.second});
  return Vector{xz.first, yz.first, yz.second};
}

Vector RotateBySinCos7210(GameState& _state, Vector _vector)
{
  const Pair yz = RotateByStoredSinCos(_state, 7, Pair{_vector.y, _vector.z});
  _state.Set(DS.rotateScratch, Word(yz.second));
  const Pair xy = RotateByStoredSinCos(_state, 2, Pair{_vector.x, yz.first});
  _state.Set(DS.rotateScratch, Word(xy.second));
  const Pair xz = RotateByStoredSinCos(_state, 1, Pair{xy.first, yz.second});
  _state.Set(DS.rotateScratch, Word(xz.first));
  const Pair rotatedYz = RotateByStoredSinCos(_state, 0, Pair{xy.second, xz.second});
  return Vector{xz.first, rotatedYz.first, rotatedYz.second};
}

InverseDistanceScale ScaleByInverseDistance(GameState& _state, const ObjectSlot& _slot, std::uint32_t _value)
{
  const std::uint32_t sum = Square(_slot.Get(SlotWord::ViewX)) + Square(_slot.Get(SlotWord::ViewY)) + Square(_slot.Get(SlotWord::ViewZ));
  // One more than the root of the high word, counted in the divisor's high byte: a count of 256 wraps to 0.
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
  const auto divisor = static_cast<std::uint16_t>(count << 8);
  // The value shifted arithmetically right by the disc scale, then divided through the trap.
  const std::uint8_t shift = _slot.Get(SlotByte::DiscScale);
  auto shifted = static_cast<std::int32_t>(_value);
  shifted >>= shift < 31 ? shift : 31;
  const auto dividend = static_cast<std::uint32_t>(shifted);
  std::uint16_t scaled = 0;
  if (divisor == 0 || dividend / divisor > 0xFFFF)
  {
    SaveDivideRegisters(_state, divisor, _state.DataSegment());
    scaled = SaturateDivide(_state, _state.CodeSegment(), SCALE_DIVIDE_RETURN, static_cast<std::uint16_t>(dividend)).ax;
  }
  else
  {
    scaled = static_cast<std::uint16_t>(dividend / divisor);
  }
  return InverseDistanceScale{scaled > SCALE_LIMIT ? SCALE_LIMIT : scaled, divisor};
}

std::uint32_t ShiftRight24(std::uint32_t _value, std::uint8_t _count)
{
  if (_count == 0)
  {
    return _value;
  }
  const auto high = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(_value >> 16)));
  auto value = static_cast<std::int32_t>((high << 16) | (_value & 0xFFFF));
  value >>= _count < 31 ? _count : 31;
  return static_cast<std::uint32_t>(value) & MASK_24_BITS;
}

PositionScale GetPositionScaleShift(const ObjectSlot& _slot)
{
  const std::uint32_t x = Magnitude24(_slot.PositionX());
  const std::uint32_t y = Magnitude24(_slot.PositionY());
  const std::uint32_t z = Magnitude24(_slot.PositionZ());
  std::uint32_t largest = x >= y ? x : y;
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
  return PositionScale{shift, static_cast<std::uint16_t>(largest)};
}

Vector ScalePositionDown(const ObjectSlot& _slot, std::uint8_t _shift)
{
  const auto lowWord = [_shift](std::uint32_t _coordinate)
  { return Signed(static_cast<std::uint16_t>(ShiftRight24(_coordinate, _shift))); };
  return Vector{lowWord(_slot.PositionX()), lowWord(_slot.PositionY()), lowWord(_slot.PositionZ())};
}

Angles ComputeAnglesToObject(GameState& _state, const ObjectSlot& _slot)
{
  const Vector scaled = ScalePositionDown(_slot, GetPositionScaleShift(_slot).shift);
  const std::uint16_t first = ArcTangent2(_state, scaled.y, scaled.z);
  (void)SetSinCos(_state, 6, first);
  const Pair yz = RotateByStoredSinCos(_state, 6, Pair{scaled.y, scaled.z});
  return Angles{first, ArcTangent2(_state, scaled.x, yz.second)};
}

Angles ConvertVectorToAngles(GameState& _state, Vector _vector)
{
  const auto quarter = [](std::int16_t _value) { return static_cast<std::int16_t>(_value >> 2); };
  const std::int16_t x = quarter(_vector.x);
  const std::int16_t y = quarter(_vector.y);
  const std::int16_t z = quarter(_vector.z);
  const std::uint16_t first = ArcTangent2(_state, y, z);
  (void)SetSinCos(_state, 8, first);
  const Pair yz = RotateByStoredSinCos(_state, 8, Pair{y, z});
  return Angles{Negate(first), Negate(ArcTangent2(_state, x, yz.second))};
}

// ── Their entries ──

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
// The box tests and AngleWithinTolerance: every register as the original leaves it, a caller reading
// the box tests' magnitudes (VectorWithinBoxEntry).
constexpr Machine::NativeContract RETURNS_CARRY{0, FLAG_CARRY};

[[nodiscard]] Vector VectorIn(const Machine::Registers& _regs) noexcept
{
  return Vector{Signed(_regs.ax), Signed(_regs.bx), Signed(_regs.cx)};
}

void VectorOut(Machine::Registers& _regs, Vector _vector) noexcept
{
  _regs.ax = Word(_vector.x);
  _regs.bx = Word(_vector.y);
  _regs.cx = Word(_vector.z);
}

} // namespace

void DivideOverflowInterruptEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The return address IRET takes, IP then CS; the trap saves the DS the divide ran with.
  GameState state = StateOnRegisters(_guest);
  const DivideTrap trap = DivideOverflowInterrupt(state, _guest.StackWord(2), _guest.StackWord(0), regs.ax, regs.bx);
  _guest.SetStackWord(0, trap.resume);
  regs.ax = trap.ax;
  _guest.Clobber(PRESERVES_ALL);
}

void NextRandomEntry(Guest& _guest)
{
  _guest.Regs().ax = NextRandom(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void SetSinCosEntry(Guest& _guest, std::uint16_t _pairOffset)
{
  Machine::Registers& regs = _guest.Regs();
  const SinCos pair = SetSinCos(_guest.State(), PairIndex(_pairOffset), regs.ax);
  regs.ax = Word(pair.sine);
  regs.bx = Word(pair.cosine);
  _guest.Clobber(PRESERVES_ALL);
}

void RotateBySinCosEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Pair rotated = RotateBySinCos(Pair{Signed(regs.ax), Signed(regs.bx)}, SinCos{Signed(regs.di), Signed(regs.bp)});
  regs.ax = Word(rotated.first);
  regs.bx = Word(rotated.second);
  _guest.Clobber(CLOBBERS_DX_BP);
}

void RotateByStoredSinCosEntry(Guest& _guest, std::uint16_t _pairOffset)
{
  Machine::Registers& regs = _guest.Regs();
  const Pair rotated = RotateByStoredSinCos(_guest.State(), PairIndex(_pairOffset), Pair{Signed(regs.ax), Signed(regs.bx)});
  regs.ax = Word(rotated.first);
  regs.bx = Word(rotated.second);
  _guest.Clobber(CLOBBERS_DX);
}

void ArcTangent2Entry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = ArcTangent2(_guest.State(), Signed(regs.ax), Signed(regs.bx));
  _guest.Clobber(CLOBBERS_BX_CX_DX);
}

void QuadrantArcTangentEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = QuadrantArcTangent(_guest.State(), regs.ax, regs.bx);
  _guest.Clobber(CLOBBERS_BX_CX_DX);
}

void RatioArcTangentEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = RatioArcTangent(_guest.State(), regs.ax, regs.bx);
  _guest.Clobber(CLOBBERS_BX_CX_DX);
}

void AngleWithinToleranceEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const AngleTolerance tolerance = AngleWithinTolerance(regs.ax, regs.cx, regs.bx);
  // The contract keeps CX, which the original leaves sign-extended, and DX, the excess.
  regs.cx = SignExtendAngle(regs.cx);
  regs.dx = tolerance.excess;
  _guest.SetFlag(Machine::FLAG_CARRY, tolerance.within);
  _guest.Clobber(RETURNS_CARRY);
}

void VectorLengthEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = VectorLength(VectorIn(regs));
  _guest.Clobber(CLOBBERS_BX_CX_DX);
}

void ObjectWithinBoxEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  regs.ax = slot.Get(SlotWord::X);
  regs.bx = slot.Get(SlotWord::Y);
  regs.cx = slot.Get(SlotWord::Z);
  VectorWithinBoxEntry(_guest);
}

void VectorWithinBoxEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.SetFlag(Machine::FLAG_CARRY, VectorWithinBox(VectorIn(regs), regs.dx));
  // The original leaves each magnitude it took in its register, and UpdateObjectsAndSpawn reads them
  // (51BE, 51D5): AX always, BX once x is inside, CX once y is too.
  regs.ax = Magnitude(regs.ax);
  if (regs.ax < regs.dx)
  {
    regs.bx = Magnitude(regs.bx);
    if (regs.bx < regs.dx)
    {
      regs.cx = Magnitude(regs.cx);
    }
  }
  _guest.Clobber(RETURNS_CARRY);
}

void RotatePitchYawRollEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  VectorOut(regs, RotatePitchYawRoll(_guest.State(), VectorIn(regs)));
  _guest.Clobber(CLOBBERS_DX);
}

void RotateRollYawPitchEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  VectorOut(regs, RotateRollYawPitch(_guest.State(), VectorIn(regs)));
  _guest.Clobber(CLOBBERS_DX);
}

void RotateBySinCos7210Entry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  VectorOut(regs, RotateBySinCos7210(_guest.State(), VectorIn(regs)));
  _guest.Clobber(CLOBBERS_DX);
}

void ScaleByInverseDistanceEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const InverseDistanceScale scale =
    ScaleByInverseDistance(_guest.State(), ObjectSlot(_guest.State(), regs.di), (std::uint32_t{regs.dx} << 16) | regs.ax);
  regs.ax = scale.scaled;
  regs.bx = scale.divisor;
  _guest.Clobber(CLOBBERS_CX_DX);
}

void ShiftRight24Entry(Guest& _guest)
{
  _guest.Clobber(PRESERVES_ALL);
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t count = High(regs.dx);
  if (count == 0)
  {
    return;
  }
  const std::uint32_t shifted = ShiftRight24((std::uint32_t{Low(regs.dx)} << 16) | regs.ax, count);
  regs.ax = static_cast<std::uint16_t>(shifted);
  regs.dx = static_cast<std::uint16_t>(shifted >> 16);
}

void GetPositionScaleShiftEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const PositionScale scale = GetPositionScaleShift(ObjectSlot(_guest.State(), regs.di));
  regs.bx = scale.magnitude;
  SetLow(regs.cx, scale.shift);
  _guest.Clobber(CLOBBERS_AX_DX);
}

void ScalePositionDownEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  VectorOut(regs, ScalePositionDown(ObjectSlot(_guest.State(), regs.di), High(regs.dx)));
  _guest.Clobber(CLOBBERS_DX_BP);
}

void ComputeAnglesToObjectEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Angles angles = ComputeAnglesToObject(_guest.State(), ObjectSlot(_guest.State(), regs.di));
  regs.ax = angles.first;
  regs.bx = angles.second;
  // The original keeps the first angle in BP, and leaves it there.
  regs.bp = regs.ax;
  _guest.Clobber(CLOBBERS_CX_DX);
}

void ConvertVectorToAnglesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Angles angles = ConvertVectorToAngles(_guest.State(), VectorIn(regs));
  regs.ax = angles.first;
  regs.bx = angles.second;
  // The original keeps the first angle in BP, and leaves it there.
  regs.bp = regs.ax;
  _guest.Clobber(CLOBBERS_CX_DX);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x025E, "DivideOverflowInterrupt", &DivideOverflowInterruptEntry, PRESERVES_ALL, Machine::NativeReturn::Interrupt},
  NativeEntry{0x061C, "NextRandom", &NextRandomEntry, PRESERVES_ALL},
  NativeEntry{0x23DB, "RotateBySinCos", &RotateBySinCosEntry, CLOBBERS_DX_BP},
  NativeEntry{0x2421, "SetSinCos0", [](Guest& _guest) { SetSinCosEntry(_guest, 0x41A0); }, PRESERVES_ALL},
  NativeEntry{0x2426, "SetSinCos1", [](Guest& _guest) { SetSinCosEntry(_guest, 0x41A4); }, PRESERVES_ALL},
  NativeEntry{0x242B, "SetSinCos2", [](Guest& _guest) { SetSinCosEntry(_guest, 0x41A8); }, PRESERVES_ALL},
  NativeEntry{0x2430, "SetSinCos3", [](Guest& _guest) { SetSinCosEntry(_guest, 0x41AC); }, PRESERVES_ALL},
  NativeEntry{0x2435, "SetSinCos4", [](Guest& _guest) { SetSinCosEntry(_guest, 0x41B0); }, PRESERVES_ALL},
  NativeEntry{0x243B, "SetSinCos5", [](Guest& _guest) { SetSinCosEntry(_guest, 0x41B4); }, PRESERVES_ALL},
  NativeEntry{0x2441, "SetSinCos6", [](Guest& _guest) { SetSinCosEntry(_guest, 0x41B8); }, PRESERVES_ALL},
  NativeEntry{0x2447, "SetSinCos8", [](Guest& _guest) { SetSinCosEntry(_guest, 0x41C0); }, PRESERVES_ALL},
  NativeEntry{0x244D, "SetSinCos7", [](Guest& _guest) { SetSinCosEntry(_guest, 0x41BC); }, PRESERVES_ALL},
  NativeEntry{0x2453, "RotateBySinCos0", [](Guest& _guest) { RotateByStoredSinCosEntry(_guest, 0x41A0); }, CLOBBERS_DX},
  NativeEntry{0x2465, "RotateBySinCos1", [](Guest& _guest) { RotateByStoredSinCosEntry(_guest, 0x41A4); }, CLOBBERS_DX},
  NativeEntry{0x246D, "RotateBySinCos2", [](Guest& _guest) { RotateByStoredSinCosEntry(_guest, 0x41A8); }, CLOBBERS_DX},
  NativeEntry{0x2475, "RotateBySinCos3", [](Guest& _guest) { RotateByStoredSinCosEntry(_guest, 0x41AC); }, CLOBBERS_DX},
  NativeEntry{0x247D, "RotateBySinCos4", [](Guest& _guest) { RotateByStoredSinCosEntry(_guest, 0x41B0); }, CLOBBERS_DX},
  NativeEntry{0x2485, "RotateBySinCos5", [](Guest& _guest) { RotateByStoredSinCosEntry(_guest, 0x41B4); }, CLOBBERS_DX},
  NativeEntry{0x248D, "RotateBySinCos6", [](Guest& _guest) { RotateByStoredSinCosEntry(_guest, 0x41B8); }, CLOBBERS_DX},
  NativeEntry{0x2495, "RotateBySinCos7", [](Guest& _guest) { RotateByStoredSinCosEntry(_guest, 0x41BC); }, CLOBBERS_DX},
  NativeEntry{0x249D, "RotateBySinCos8", [](Guest& _guest) { RotateByStoredSinCosEntry(_guest, 0x41C0); }, CLOBBERS_DX},
  NativeEntry{0x24A5, "ArcTangent2", &ArcTangent2Entry, CLOBBERS_BX_CX_DX},
  NativeEntry{0x24E5, "QuadrantArcTangent", &QuadrantArcTangentEntry, CLOBBERS_BX_CX_DX},
  NativeEntry{0x24F7, "RatioArcTangent", &RatioArcTangentEntry, CLOBBERS_BX_CX_DX},
  NativeEntry{0x2CDB, "AngleWithinTolerance", &AngleWithinToleranceEntry, RETURNS_CARRY},
  NativeEntry{0x2E96, "VectorLength", &VectorLengthEntry, CLOBBERS_BX_CX_DX},
  NativeEntry{0x2F65, "ObjectWithinBox", &ObjectWithinBoxEntry, RETURNS_CARRY},
  NativeEntry{0x2F6E, "VectorWithinBox", &VectorWithinBoxEntry, RETURNS_CARRY},
  NativeEntry{0x3EAC, "RotatePitchYawRoll", &RotatePitchYawRollEntry, CLOBBERS_DX},
  NativeEntry{0x3EC7, "RotateRollYawPitch", &RotateRollYawPitchEntry, CLOBBERS_DX},
  NativeEntry{0x3F02, "RotateBySinCos7210", &RotateBySinCos7210Entry, CLOBBERS_DX},
  NativeEntry{0x40A4, "ScaleByInverseDistance", &ScaleByInverseDistanceEntry, CLOBBERS_CX_DX},
  NativeEntry{0x4326, "ShiftRight24", &ShiftRight24Entry, PRESERVES_ALL},
  NativeEntry{0x4333, "GetPositionScaleShift", &GetPositionScaleShiftEntry, CLOBBERS_AX_DX},
  NativeEntry{0x439E, "ScalePositionDown", &ScalePositionDownEntry, CLOBBERS_DX_BP},
  NativeEntry{0x4ECF, "ComputeAnglesToObject", &ComputeAnglesToObjectEntry, CLOBBERS_CX_DX},
  NativeEntry{0x4F08, "ConvertVectorToAngles", &ConvertVectorToAnglesEntry, CLOBBERS_CX_DX},
};

} // namespace

std::span<const NativeEntry> MathsEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
