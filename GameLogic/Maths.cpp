#include "pch.h"

#include "Maths.h"

#include "Arithmetic.h"

namespace Elite
{

namespace
{

// What DivideOverflowInterrupt leaves in AX after a word divide overflows: the positive maximum (plan §7.1).
constexpr std::uint16_t DIVIDE_OVERFLOW_WORD = 0x7FFF;
// What it leaves in AL after a byte divide overflows.
constexpr std::uint8_t DIVIDE_OVERFLOW_BYTE = 0x7F;

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
// ScaleByInverseDistance's divide, DIV BX at CS:40E0, a word divide's opcode.
constexpr DivideTrap SCALE_DIVIDE_TRAP = DivideTrap::Word;
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

[[nodiscard]] std::uint16_t Word(std::int16_t _value) noexcept
{
  return static_cast<std::uint16_t>(_value);
}

[[nodiscard]] std::int16_t Signed(std::uint16_t _value) noexcept
{
  return static_cast<std::int16_t>(_value);
}

} // namespace

// ── The routines ──

std::uint16_t DivideOverflowInterrupt(DivideTrap _trap, std::uint16_t _ax)
{
  return _trap == DivideTrap::Word ? DIVIDE_OVERFLOW_WORD : WithLow(_ax, DIVIDE_OVERFLOW_BYTE);
}

WordQuotient DivideUnsigned(std::uint32_t _dividend, std::uint16_t _divisor, DivideTrap _trap)
{
  if (_divisor == 0 || _dividend / _divisor > 0xFFFF)
  {
    return WordQuotient{DivideOverflowInterrupt(_trap, static_cast<std::uint16_t>(_dividend)), static_cast<std::uint16_t>(_dividend >> 16)};
  }
  return WordQuotient{static_cast<std::uint16_t>(_dividend / _divisor), static_cast<std::uint16_t>(_dividend % _divisor)};
}

ByteQuotient DivideByte(std::uint16_t _dividend, std::uint8_t _divisor)
{
  if (High(_dividend) >= _divisor)
  {
    return ByteQuotient{DIVIDE_OVERFLOW_BYTE, High(_dividend)};
  }
  return ByteQuotient{static_cast<std::uint8_t>(_dividend / _divisor), static_cast<std::uint8_t>(_dividend % _divisor)};
}

WordQuotient DivideWord(std::uint32_t _dividend, std::uint16_t _divisor)
{
  const auto high = static_cast<std::uint16_t>(_dividend >> 16);
  if (high >= _divisor)
  {
    return WordQuotient{DIVIDE_OVERFLOW_WORD, high};
  }
  return WordQuotient{static_cast<std::uint16_t>(_dividend / _divisor), static_cast<std::uint16_t>(_dividend % _divisor)};
}

WordQuotient DivideSignedWord(std::uint32_t _dividend, std::uint16_t _divisor)
{
  const bool dividendNegative = (_dividend & 0x80000000u) != 0;
  const bool divisorNegative = SignWord(_divisor) != 0;
  const std::uint32_t dividendMagnitude = dividendNegative ? 0u - _dividend : _dividend;
  const std::uint32_t divisorMagnitude = divisorNegative ? 0x10000u - _divisor : _divisor;
  const std::uint32_t quotient = (dividendMagnitude >> 16) >= divisorMagnitude ? 0x8000u : dividendMagnitude / divisorMagnitude;
  if ((quotient & 0x8000u) != 0)
  {
    return WordQuotient{DIVIDE_OVERFLOW_WORD, static_cast<std::uint16_t>(_dividend >> 16)};
  }
  const std::uint32_t remainder = dividendMagnitude % divisorMagnitude;
  return WordQuotient{static_cast<std::uint16_t>(dividendNegative != divisorNegative ? 0u - quotient : quotient),
                      static_cast<std::uint16_t>(dividendNegative ? 0u - remainder : remainder)};
}

std::uint16_t NextRandom(GameState& _state)
{
  const std::uint16_t a = _state.Data().commanderBlock.randomState0;
  const std::uint16_t b = _state.Data().commanderBlock.randomState1;
  const std::uint16_t c = _state.Data().commanderBlock.randomState2;
  const auto sum = static_cast<std::uint16_t>(a + b);
  _state.Data().commanderBlock.randomState1 = c;
  _state.Data().commanderBlock.randomState0 = b;
  _state.Data().commanderBlock.randomState2 = static_cast<std::uint16_t>(c + sum);
  return sum;
}

SinCos SinCosOf(const GameState& _state, std::uint16_t _angle)
{
  const auto index = static_cast<std::uint16_t>(_angle & ANGLE_MASK);
  const auto cosineIndex = static_cast<std::uint16_t>((index + COSINE_OFFSET_BYTES / 2) & (SINE_WORDS - 1));
  return SinCos{Signed(_state.Data().sineTable[index]), Signed(_state.Data().sineTable[cosineIndex])};
}

SinCos SetSinCos(GameState& _state, std::size_t _pair, std::uint16_t _angle)
{
  const SinCos pair = SinCosOf(_state, _angle);
  Counted<std::uint8_t>(&stored)[4] = _state.Data().rotationSinCos[_pair];
  SetWordIn(stored, 0, Word(pair.sine));
  SetWordIn(stored, 2, Word(pair.cosine));
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
  const Counted<std::uint8_t>(&stored)[4] = _state.Data().rotationSinCos[_pair];
  return RotateBySinCos(_point, SinCos{Signed(WordIn(stored, 0)), Signed(WordIn(stored, 2))});
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
    const std::uint16_t tangent = _state.Data().tangentTable[middle >> 1];
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

bool AngleWithinTolerance(std::uint16_t _a, std::uint16_t _b, std::uint16_t _tolerance)
{
  std::uint16_t difference = SignExtendAngle(static_cast<std::uint16_t>(SignExtendAngle(_a) - SignExtendAngle(_b)));
  if ((difference & 0x8000) != 0)
  {
    difference = Negate(difference);
  }
  return difference < _tolerance;
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

bool ObjectWithinBox(const SpaceObject& _slot, std::uint16_t _halfSize)
{
  return VectorWithinBox(Vector{Signed(_slot.x), Signed(_slot.y), Signed(_slot.z)}, _halfSize);
}

Vector RotatePitchYawRoll(GameState& _state, Vector _vector)
{
  const Pair yz = RotateByStoredSinCos(_state, 0, Pair{_vector.y, _vector.z});
  _state.Data().rotateScratch = Word(yz.first);
  const Pair xz = RotateByStoredSinCos(_state, 1, Pair{_vector.x, yz.second});
  _state.Data().rotateScratch = Word(xz.second);
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
  _state.Data().rotateScratch = Word(yz.second);
  const Pair xy = RotateByStoredSinCos(_state, 2, Pair{_vector.x, yz.first});
  _state.Data().rotateScratch = Word(xy.second);
  const Pair xz = RotateByStoredSinCos(_state, 1, Pair{xy.first, yz.second});
  _state.Data().rotateScratch = Word(xz.first);
  const Pair rotatedYz = RotateByStoredSinCos(_state, 0, Pair{xy.second, xz.second});
  return Vector{xz.first, rotatedYz.first, rotatedYz.second};
}

std::uint16_t ScaleByInverseDistance(const SpaceObject& _slot, std::uint32_t _value)
{
  const std::uint32_t sum = Square(_slot.viewX) + Square(_slot.viewY) + Square(_slot.viewZ);
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
  const std::uint8_t shift = Low(_slot.pitch); // the disc's scale shift
  auto shifted = static_cast<std::int32_t>(_value);
  shifted >>= shift < 31 ? shift : 31;
  const auto dividend = static_cast<std::uint32_t>(shifted);
  std::uint16_t scaled = 0;
  if (divisor == 0 || dividend / divisor > 0xFFFF)
  {
    scaled = DivideOverflowInterrupt(SCALE_DIVIDE_TRAP, static_cast<std::uint16_t>(dividend));
  }
  else
  {
    scaled = static_cast<std::uint16_t>(dividend / divisor);
  }
  return scaled > SCALE_LIMIT ? SCALE_LIMIT : scaled;
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

PositionScale GetPositionScaleShift(const SpaceObject& _slot)
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
  return PositionScale{shift};
}

Vector ScalePositionDown(const SpaceObject& _slot, std::uint8_t _shift)
{
  const auto lowWord = [_shift](std::uint32_t _coordinate)
  { return Signed(static_cast<std::uint16_t>(ShiftRight24(_coordinate, _shift))); };
  return Vector{lowWord(_slot.PositionX()), lowWord(_slot.PositionY()), lowWord(_slot.PositionZ())};
}

Angles ComputeAnglesToObject(GameState& _state, const SpaceObject& _slot)
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

} // namespace Elite
