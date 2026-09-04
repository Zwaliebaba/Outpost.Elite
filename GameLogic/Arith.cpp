#include "pch.h"

#include "Arith.h"

#include "LookupTables.h"

/*
 * The arithmetic kernel (ADR-002).
 *
 * Elite multiplies by shift-and-add: one bit of the multiplier is examined per step, the
 * running total lives in A, and the result is shifted down into P as it is built. The 6502
 * expresses that with ROR through the carry flag, which is why these functions track carry
 * explicitly instead of using wider integers -- the carry is not an implementation detail here,
 * it is how the low half of the product gets assembled.
 *
 * The other thing to know before reading: the multiplier is decremented before the loop and the
 * addition is done with carry set, so the two cancel and each step adds the true multiplier.
 * That saves an instruction in the original and looks like an off-by-one here if you skim it.
 */

namespace Elite
{

namespace
{

/// One shift-and-add step: conditionally accumulate, then rotate the running total down into
/// the low byte, bringing the next multiplier bit out into the carry.
/// Shared by every multiply below, which in the original is eight copies of four instructions.
inline void ShiftAndAddStep(std::uint8_t& _a, std::uint8_t& _low, std::uint8_t _addend, bool& _carry) noexcept
{
  if (_carry)
  {
    const AddResult sum = AddWithCarry(_a, _addend, true);
    _a = sum.value;
    _carry = sum.carry;
  }

  const ShiftResult rotatedHigh = RotateRight(_a, _carry);
  _a = rotatedHigh.value;
  _carry = rotatedHigh.carry;

  const ShiftResult rotatedLow = RotateRight(_low, _carry);
  _low = rotatedLow.value;
  _carry = rotatedLow.carry;
}

} // namespace

WideResult MultiplyByX(MathWorkspace& _work, std::uint8_t _x) noexcept
{
  _work.t = static_cast<std::uint8_t>(_x - 1);

  std::uint8_t a = 0;

  // The first multiplier bit is shifted out before the loop starts.
  bool carry = (_work.p & 0x01u) != 0u;
  _work.p = static_cast<std::uint8_t>(_work.p >> 1);

  for (int step = 0; step < 8; ++step)
  {
    ShiftAndAddStep(a, _work.p, _work.t, carry);
  }

  // The carry is the last `ROR P`'s, and three callers read it before doing anything that would
  // set it themselves. See the note on WideResult.
  return WideResult{ a, carry };
}

WideResult MultiplyUnsigned(MathWorkspace& _work) noexcept
{
  const std::uint8_t multiplier = _work.q;

  if (multiplier == 0)
  {
    // 6502: MU1 -- `CLC / STX P / TXA / RTS`, so the zero case clears both halves AND the carry.
    // The `CLC` is the part a port drops, and it is what stops a zero multiply looking like an
    // overflow to the caller below it.
    _work.p = 0;
    return WideResult{ 0, false };
  }

  return MultiplyByX(_work, multiplier);
}

WideResult MultiplyMagnitudeByQ(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.p = static_cast<std::uint8_t>(_a & 0x7Fu);
  return MultiplyUnsigned(_work);
}

std::uint8_t MultiplySigned(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  // The sign of the product is decided up front and re-applied at the very end.
  _work.t = static_cast<std::uint8_t>((_a ^ _work.q) & 0x80u);

  // P starts as |A| >> 1, and the bit shifted out is the first multiplier bit.
  std::uint8_t magnitude = static_cast<std::uint8_t>(_a & 0x7Fu);
  bool carry = (magnitude & 0x01u) != 0u;
  _work.p = static_cast<std::uint8_t>(magnitude >> 1);

  const std::uint8_t multiplier = static_cast<std::uint8_t>(_work.q & 0x7Fu);
  if (multiplier == 0)
  {
    // 6502: mu10 -- a zero multiplier zeroes the low byte and returns zero, sign and all.
    _work.p = 0;
    return 0;
  }

  _work.t1 = static_cast<std::uint8_t>(multiplier - 1);

  std::uint8_t a = 0;

  // Seven steps here rather than eight: the eighth bit was consumed by the shift above, and
  // the final shift below completes it.
  for (int step = 0; step < 7; ++step)
  {
    ShiftAndAddStep(a, _work.p, _work.t1, carry);
  }

  carry = (a & 0x01u) != 0u;
  a = static_cast<std::uint8_t>(a >> 1);

  const ShiftResult rotatedLow = RotateRight(_work.p, carry);
  _work.p = rotatedLow.value;

  return static_cast<std::uint8_t>(a | _work.t);
}

void MultiplySignedToSR(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  const std::uint8_t high = MultiplySigned(_work, _a);
  _work.s = high;
  _work.r = _work.p;
}

WideResult SquareUnsigned(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.p = _a;

  if (_a == 0)
  {
    // 6502: MU1 -- CLC / STX P / TXA / RTS. It falls into the same zero tail the unsigned
    // multiply uses, and that tail CLEARS the carry, which is half of why `MAS3` can read one.
    _work.p = 0;
    return { 0, false };
  }

  return MultiplyByX(_work, _a);
}

WideResult Square(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  return SquareUnsigned(_work, static_cast<std::uint8_t>(_a & 0x7Fu));
}

AddSignedResult AddSigned(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.t1 = _a;
  _work.t = static_cast<std::uint8_t>(_a & 0x80u);

  if (((_work.t ^ _work.s) & 0x80u) == 0u)
  {
    // Signs agree, so the magnitudes simply add and the shared sign is put back on top.
    const AddResult low = AddWithCarry(_work.r, _work.p, false);
    const AddResult high = AddWithCarry(_work.s, _work.t1, low.carry);
    // 6502: `ORA T` does not touch the carry, so what `ADC T1` produced is what the caller gets.
    return AddSignedResult{ static_cast<std::uint8_t>(high.value | _work.t), low.value,
                            high.carry };
  }

  // 6502: MU8 -- signs differ, so this is a subtraction of magnitudes that may come out
  // negative, in which case the result is negated and marked.
  _work.u = static_cast<std::uint8_t>(_work.s & 0x7Fu);

  const std::uint16_t lowDifference = static_cast<std::uint16_t>(_work.p) - _work.r;
  std::uint8_t low = static_cast<std::uint8_t>(lowDifference);
  bool borrowClear = lowDifference < 0x100u;

  const std::uint16_t highDifference =
    static_cast<std::uint16_t>(_work.t1 & 0x7Fu) - _work.u - (borrowClear ? 0u : 1u);
  std::uint8_t high = static_cast<std::uint8_t>(highDifference);
  borrowClear = highDifference < 0x100u;

  // 6502: BCS MU9 -- taken means no borrow, and the carry it was taken on is the exit carry.
  bool exitCarry = borrowClear;

  if (!borrowClear)
  {
    // 6502: the branch that turns a negative difference back into sign-magnitude form.
    _work.u = high;

    const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(low ^ 0xFFu), 1u, false);
    low = negated.value;

    const std::uint16_t negatedHigh = 0u - _work.u - (negated.carry ? 0u : 1u);
    high = static_cast<std::uint8_t>(static_cast<std::uint8_t>(negatedHigh) | 0x80u);
    exitCarry = negatedHigh < 0x100u; // 6502: the second `SBC U`
  }

  // 6502: MU9 -- fold in the sign the first operand arrived with. `EOR T` leaves the carry.
  return AddSignedResult{ static_cast<std::uint8_t>(high ^ _work.t), low, exitCarry };
}

AddSignedResult MultiplyAndAdd(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  // In the original this is one call followed by a fall-through into the addition, which is
  // why the multiply's low byte is left where the addition expects to find it.
  const std::uint8_t product = MultiplySigned(_work, _a);
  return AddSigned(_work, product);
}

void FillK(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.k[0] = _a;
  _work.k[1] = _a;
  _work.k[2] = _a;
  _work.k[3] = _a;
}

void SetPairP(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.p1 = _a;
  _work.p = _a;
}

std::uint8_t MultiplyScaled(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.t = static_cast<std::uint8_t>(_a & 0x80u);

  const std::uint8_t multiplier = static_cast<std::uint8_t>(_a & 0x7Fu);
  if (multiplier == 0)
  {
    SetPairP(_work, 0);
    return 0;
  }

  _work.t1 = static_cast<std::uint8_t>(multiplier - 1);

  std::uint8_t a = 0;
  bool carry = (_work.p & 0x01u) != 0u;
  _work.p = static_cast<std::uint8_t>(_work.p >> 1);

  // Only five of the eight bits get an addition.
  for (int step = 0; step < 5; ++step)
  {
    ShiftAndAddStep(a, _work.p, _work.t1, carry);
  }

  // The remaining three are shifted through without one, which is what scales the result down.
  for (int step = 0; step < 3; ++step)
  {
    carry = (a & 0x01u) != 0u;
    a = static_cast<std::uint8_t>(a >> 1);
    const ShiftResult rotated = RotateRight(_work.p, carry);
    _work.p = rotated.value;
    carry = rotated.carry;
  }

  return static_cast<std::uint8_t>(a | _work.t);
}

WideResult MultiplyWide(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  // The multiplier arrives complemented, which turns the usual add-on-a-set-bit test into an
  // add-on-a-clear-bit one and saves the routine an instruction per step.
  const std::uint8_t complemented = static_cast<std::uint8_t>(_a ^ 0xFFu);

  bool carry = (complemented & 0x01u) != 0u;
  _work.p1 = static_cast<std::uint8_t>(complemented >> 1);

  std::uint8_t a = 0;

  const ShiftResult seeded = RotateRight(_work.p, carry);
  _work.p = seeded.value;
  carry = seeded.carry;

  for (int step = 0; step < 16; ++step)
  {
    if (carry)
    {
      // The set-bit path shifts without accumulating.
      carry = (a & 0x01u) != 0u;
      a = static_cast<std::uint8_t>(a >> 1);
    }
    else
    {
      const AddResult sum = AddWithCarry(a, _work.q, false);
      a = sum.value;
      carry = sum.carry;

      const ShiftResult rotated = RotateRight(a, carry);
      a = rotated.value;
      carry = rotated.carry;
    }

    const ShiftResult rotatedMiddle = RotateRight(_work.p1, carry);
    _work.p1 = rotatedMiddle.value;
    carry = rotatedMiddle.carry;

    const ShiftResult rotatedLow = RotateRight(_work.p, carry);
    _work.p = rotatedLow.value;
    carry = rotatedLow.carry;
  }

  // The carry is the one the final `ROR P` left, which is what MVEIT reads. See the header.
  return WideResult{ a, carry };
}

std::uint8_t DivideBy96(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.t = static_cast<std::uint8_t>(_a & 0x80u);

  std::uint8_t a = static_cast<std::uint8_t>(_a & 0x7Fu);

  // The counter doubles as the result: it is seeded with seven set bits, and each rotation
  // both shifts a quotient bit in at the bottom and pushes a marker out of the top. When the
  // markers run out the loop is done, so no separate counter is needed.
  _work.t1 = 0xFEu;

  bool looping = true;
  while (looping)
  {
    const ShiftResult shifted = { static_cast<std::uint8_t>(a << 1), (a & 0x80u) != 0u };
    a = shifted.value;

    bool quotientBit = a >= 96u;
    if (quotientBit)
    {
      a = static_cast<std::uint8_t>(a - 96u);
    }

    const ShiftResult counter = RotateLeftValue(_work.t1, quotientBit);
    _work.t1 = counter.value;
    looping = counter.carry;
  }

  return static_cast<std::uint8_t>(_work.t1 | _work.t);
}

std::uint8_t MultiplyAddDivide96(MathWorkspace& _work, std::uint8_t _a, std::uint8_t _x) noexcept
{
  _work.q = _x;

  // Flipping the sign bit is what turns the accumulate into a subtract.
  const AddSignedResult combined = MultiplyAndAdd(_work, static_cast<std::uint8_t>(_a ^ 0x80u));

  return DivideBy96(_work, combined.high);
}

std::uint8_t DivideByQ(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  const std::uint8_t sign = static_cast<std::uint8_t>(_a & 0x80u);
  std::uint8_t a = static_cast<std::uint8_t>(_a & 0x7Fu);

  if (a >= _work.q)
  {
    // 6502: TI4 -- the magnitude is too large to divide, so the result saturates.
    return static_cast<std::uint8_t>(sign | 96u);
  }

  _work.t = 0xFEu;

  bool looping = true;
  while (looping)
  {
    a = static_cast<std::uint8_t>(a << 1);

    bool quotientBit = a >= _work.q;
    if (quotientBit)
    {
      a = static_cast<std::uint8_t>(a - _work.q);
    }

    const ShiftResult counter = RotateLeftValue(_work.t, quotientBit);
    _work.t = counter.value;
    looping = counter.carry;
  }

  // The quotient is then scaled by a shift-and-add rather than a second division.
  std::uint8_t value = _work.t;
  bool carry = (value & 0x01u) != 0u;
  value = static_cast<std::uint8_t>(value >> 1);
  carry = (value & 0x01u) != 0u;
  value = static_cast<std::uint8_t>(value >> 1);
  _work.t = value;

  carry = (value & 0x01u) != 0u;
  value = static_cast<std::uint8_t>(value >> 1);

  const AddResult scaled = AddWithCarry(value, _work.t, carry);
  _work.t = scaled.value;

  return static_cast<std::uint8_t>(sign | _work.t);
}

std::uint8_t DivideWide(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.p1 = _a;
  _work.t = static_cast<std::uint8_t>((_a ^ _work.q) & 0x80u);

  std::uint8_t a = 0;

  // Shift the dividend up one and clear the divisor's sign bit, so the comparison below is
  // between magnitudes. Clearing the sign bit costs a shift up and back down, which also
  // leaves the carry clear -- and that clear carry is what the first rotate below shifts in.
  bool carry = (_work.p & 0x80u) != 0u;
  _work.p = static_cast<std::uint8_t>(_work.p << 1);
  const ShiftResult rolled = RotateLeftValue(_work.p1, carry);
  _work.p1 = rolled.value;

  _work.q = static_cast<std::uint8_t>(_work.q & 0x7Fu);
  carry = false;

  for (int step = 0; step < 16; ++step)
  {
    // The carry threads right through the loop: what falls out of the top of the quotient on
    // one step is shifted into the remainder on the next. Restarting it at zero each time
    // looks harmless and quietly produces a different number.
    const ShiftResult shifted = RotateLeftValue(a, carry);
    a = shifted.value;

    const bool quotientBit = a >= _work.q;
    if (quotientBit)
    {
      a = static_cast<std::uint8_t>(a - _work.q);
    }

    const ShiftResult low = RotateLeftValue(_work.p, quotientBit);
    _work.p = low.value;

    const ShiftResult high = RotateLeftValue(_work.p1, low.carry);
    _work.p1 = high.value;
    carry = high.carry;
  }

  return static_cast<std::uint8_t>(_work.p | _work.t);
}

/*
 * The logarithm-table group.
 *
 * Adding two logarithms and looking the sum back up is faster than eight shift-and-add steps,
 * and the game leans on it wherever the operands are already known to be well behaved. The
 * fiddly part is not the arithmetic, it is that the inverse comes from one of two tables,
 * chosen by whether the sum of the low halves came out negative. Get that test wrong and the
 * results are right about half the time, which is the worst possible failure mode.
 */

WideResult MultiplyByLog(MathWorkspace& _work, std::uint8_t _a, bool _carryIn) noexcept
{
  _work.widget = _a;

  /*
   * 6502: TAX / BEQ MU3, and LDX Q / BEQ MU3again -- the two zero exits, and neither of them
   * touches the carry. `MU3` is `LDX P / RTS` with A still zero; `MU3again` is `LDA #0 / LDX P /
   * RTS`. So on both, the carry the caller arrived with is the carry it leaves with.
   *
   * That is why `_carryIn` exists and why passing the wrong one is mostly harmless: the returned
   * BYTE is zero on either path whatever the flag was, so only a caller that reads the carry can
   * tell. Two do -- `DOEXP` (`JSR FMLTU / ADC R`) and `CIRCLE2` through `FMLTU2` -- and the
   * others follow the call with a `STA`.
   */
  if (_a == 0 || _work.q == 0)
  {
    return { 0, _carryIn };
  }

  const AddResult low = AddWithCarry(LOG_LOW_TABLE[_a], LOG_LOW_TABLE[_work.q], false);
  const bool useOddTable = (low.value & 0x80u) != 0u;

  const AddResult high = AddWithCarry(LOG_TABLE[_work.q], LOG_TABLE[_a], low.carry);
  if (!high.carry)
  {
    return { 0, false }; // 6502: BCC MU3again -- the branch is taken, so the carry is clear
  }

  // 6502: the two antilog exits, reached because the BCC above was NOT taken.
  return { useOddTable ? ANTILOG_ODD_TABLE[high.value] : ANTILOG_TABLE[high.value], true };
}

/*
 * 6502: LL28's body, from the STA widget onwards -- the part after the "does it fit" guard.
 *
 * It is a helper because the shipped game has this code TWICE: once inside LL28, and once
 * unlabelled at the end of DVID4, which falls into it. The second copy is byte-identical except
 * that it has no guard in front, so the two share this and differ only in what precedes it.
 */
bool DivideByLogarithms(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.widget = _a;

  if (_a == 0)
  {
    _work.r = 0;
    return false;
  }

  const std::uint16_t lowDifference = static_cast<std::uint16_t>(LOG_LOW_TABLE[_a]) - LOG_LOW_TABLE[_work.q];
  const std::uint8_t lowResult = static_cast<std::uint8_t>(lowDifference);
  const bool borrowClear = lowDifference < 0x100u;
  const bool useOddTable = (lowResult & 0x80u) != 0u;

  const std::uint16_t highDifference =
    static_cast<std::uint16_t>(LOG_TABLE[_a]) - LOG_TABLE[_work.q] - (borrowClear ? 0u : 1u);

  if (highDifference < 0x100u)
  {
    // No borrow means the quotient overflowed a byte, which is the saturating case again.
    _work.r = 255;
    return true;
  }

  const std::uint8_t index = static_cast<std::uint8_t>(highDifference);
  _work.r = useOddTable ? ANTILOG_ODD_TABLE[index] : ANTILOG_TABLE[index];
  return false;
}

bool DivideToR(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  if (_a >= _work.q)
  {
    // 6502: LL2 -- the ratio does not fit in a byte, so it pins at the maximum. The carry the
    // comparison left is set, and callers read it.
    _work.r = 255;
    return true;
  }

  return DivideByLogarithms(_work, _a);
}

SignedSum CombineSigned(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  if (((_a ^ _work.s) & 0x80u) == 0u)
  {
    // Signs agree, so the two parts add -- and this is the ONLY path that can return a set
    // carry, which is what makes the flag mean "overflowed".
    const AddResult sum = AddWithCarry(_work.q, _work.r, false);
    return SignedSum{ sum.value, sum.carry };
  }

  // 6502: LL39 -- signs differ, so they subtract, and a borrow means the answer changed sign.
  const std::uint16_t difference = static_cast<std::uint16_t>(_work.r) - _work.q;
  const std::uint8_t result = static_cast<std::uint8_t>(difference);

  if (difference < 0x100u)
  {
    // The original's `CLC` here looks dead -- the `SBC` above it left the carry set, and nothing
    // in this branch reads it. It is not dead: it is what stops a subtraction being reported as
    // an overflow.
    return SignedSum{ result, false };
  }

  // 6502: LL40 -- flip the sign held in S and negate the magnitude. The negation's own carry can
  // only be set for a zero magnitude, which an underflow cannot produce, so this exit is always
  // carry clear.
  _work.s = static_cast<std::uint8_t>(_work.s ^ 0x80u);
  const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(result ^ 0xFFu), 1u, false);
  return SignedSum{ negated.value, negated.carry };
}

namespace
{

/// 6502: ARS1 -- the ratio's angle from the table, plus the carry the three shifts leave. That
/// carry is not incidental: the caller subtracts with it.
struct RatioAngle
{
  std::uint8_t angle = 0;
  bool carry = false;
};

RatioAngle AngleOfRatio(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  (void)DivideToR(_work, _a);

  std::uint8_t value = _work.r;
  bool carry = false;
  for (int shift = 0; shift < 3; ++shift)
  {
    carry = (value & 0x01u) != 0u;
    value = static_cast<std::uint8_t>(value >> 1);
  }

  return RatioAngle{ ARCTAN_TABLE[value], carry };
}

} // namespace

std::uint8_t Arctan(MathWorkspace& _work) noexcept
{
  _work.t1 = static_cast<std::uint8_t>(_work.p ^ _work.q);

  if (_work.q == 0)
  {
    // 6502: AR2 -- a zero denominator is a right angle by convention.
    return 63;
  }

  const std::uint8_t denominator = static_cast<std::uint8_t>(_work.q << 1);
  const std::uint8_t numerator = static_cast<std::uint8_t>(_work.p << 1);
  _work.q = denominator;

  std::uint8_t angle = 0;
  bool carry = false;

  if (numerator >= denominator)
  {
    // 6502: AR1 -- the ratio is the wrong way up, so it is inverted and the angle reflected.
    _work.q = numerator;
    _work.p = denominator;

    const RatioAngle ratio = AngleOfRatio(_work, denominator);
    _work.t = ratio.angle;

    const std::uint16_t reflected = 64u - _work.t - (ratio.carry ? 0u : 1u);
    if (reflected >= 0x100u)
    {
      return 63;
    }

    angle = static_cast<std::uint8_t>(reflected);
    carry = true;
  }
  else
  {
    angle = AngleOfRatio(_work, numerator).angle;
    carry = true;
  }

  // 6502: AR4 -- the operands' signs decided the quadrant before any of this ran.
  if ((_work.t1 & 0x80u) != 0u)
  {
    _work.t = angle;
    const std::uint16_t opposite = 128u - _work.t - (carry ? 0u : 1u);
    return static_cast<std::uint8_t>(opposite);
  }

  return angle;
}

WideResult MultiplyKBySine(MathWorkspace& _work, std::uint8_t _a, bool _carryIn) noexcept
{
  // 6502: FMLTU2's own three instructions. It then falls through into FMLTU with K in A, so the
  // multiplicand is K and the multiplier is the sine it just put in Q.
  _work.q = SINE_TABLE[_a & 0x1Fu];
  return MultiplyByLog(_work, _work.k[0], _carryIn);
}

ScaledDivision DivideAndScale(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  // 6502: DVID4. Restoring division: shift the dividend up a bit at a time, and after each
  // shift subtract the divisor if it fits, recording whether it did as the next quotient bit.
  //
  // The comparison is what carries the quotient bit. CMP leaves carry SET when the value is not
  // smaller than Q, and the SBC that follows consumes that same set carry as "no borrow" -- so
  // the subtract is correct and the ROL that follows shifts a 1 in. When the branch is taken
  // instead, carry is clear and the ROL shifts a 0. One flag doing two jobs, which is why the
  // port keeps the comparison and the shift adjacent rather than tidying them apart.
  // 6502: ASL A -- a left shift is a rotate with no carry coming in.
  const ShiftResult shifted = RotateLeftValue(_a, false);
  _work.p = shifted.value;

  std::uint8_t remainder = 0;
  bool carry = shifted.carry;

  for (int step = 0; step < 8; ++step)
  {
    const ShiftResult next = RotateLeftValue(remainder, carry);
    remainder = next.value;

    // 6502: CMP Q / BCC / SBC Q -- subtract only when it fits, and the comparison's carry is
    // both the decision and the quotient bit.
    carry = remainder >= _work.q;
    if (carry)
    {
      remainder = static_cast<std::uint8_t>(remainder - _work.q);
    }

    const ShiftResult quotient = RotateLeftValue(_work.p, carry);
    _work.p = quotient.value;
    carry = quotient.carry;
  }

  /*
   * And now the part that is easy to miss: DVID4 has no RTS.
   *
   * It runs straight on into an unlabelled copy of LL28's body, and both of its callers get
   * that. The copy is byte-identical to LL28 except that the "does it fit in a byte" guard in
   * front of it is absent -- which is safe rather than sloppy, because a remainder is always
   * smaller than the divisor it came from, so the guard could never have fired here.
   *
   * The consequence for callers: P holds the quotient of the eight steps, and R holds that
   * remainder scaled up by the same divisor. Returning only the remainder, as an eight-step
   * divide would, is not what the game does.
   */
  const bool exitCarry = DivideByLogarithms(_work, remainder);
  return { _work.r, exitCarry };
}

bool SquareRoot(MathWorkspace& _work) noexcept
{
  /*
   * 6502: LL5. The radicand is (R Q); Y and X hold the running remainder, S the bits still to be
   * shifted in, and Q accumulates the answer.
   *
   * The subtraction compares (X Y) against (Q 0x40) and takes it away when it fits. The original
   * spells the comparison as CPX / BCC / BNE / CPY, which is a three-way branch on two bytes,
   * and then relies on the carry that comparison left to make the SBC below correct. Both halves
   * are kept as flags here for that reason.
   */
  std::uint8_t y = _work.r;
  std::uint8_t s = _work.q;
  std::uint8_t x = 0;
  bool exitCarry = false;
  _work.q = 0;

  for (int round = 0; round < 8; ++round)
  {
    // 6502: CPX Q / BCC LL7 / BNE / CPY #64 / BCC LL7 -- does (Q 0x40) fit into (X Y)?
    bool fits = false;
    if (x > _work.q)
    {
      fits = true;
    }
    else if (x == _work.q && y >= 0x40u)
    {
      fits = true;
    }

    if (fits)
    {
      // 6502: TYA / SBC #64 / TAY / TXA / SBC Q / TAX -- the comparison left carry set, so the
      // subtraction borrows nothing on its first half.
      const std::uint16_t low = static_cast<std::uint16_t>(y) - 0x40u;
      y = static_cast<std::uint8_t>(low);
      const std::uint16_t high = static_cast<std::uint16_t>(x) - _work.q - (low < 0x100u ? 0u : 1u);
      x = static_cast<std::uint8_t>(high);
    }

    // 6502: LL7 -- ROL Q brings in the answer bit, which is the carry the comparison left set
    // exactly when the candidate fitted.
    _work.q = RotateLeftValue(_work.q, fits).value;

    // 6502: two rounds of ASL S / ROL A / ROL A -- two more bits of the radicand into (X Y).
    for (int pair = 0; pair < 2; ++pair)
    {
      const ShiftResult shifted = RotateLeftValue(s, false);
      s = shifted.value;
      const ShiftResult lowHalf = RotateLeftValue(y, shifted.carry);
      y = lowHalf.value;
      const ShiftResult highHalf = RotateLeftValue(x, lowHalf.carry);
      x = highHalf.value;
      exitCarry = highHalf.carry;
    }
  }

  /*
   * 6502: the last `ROL A` before `DEC T / BNE LL6 / RTS`, and `DEC` does not touch the carry.
   *
   * `SUN` reads it: `JSR LL5 / LDY Y1 / JSR DORND`, and the generator takes the carry as an
   * operand -- so the sun's ragged edge is seeded by the last bit to fall out of the square root
   * (§6.55). The tenth dropped flag.
   */
  return exitCarry;
}

void MultiplySignedToK(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  // 6502: STA R / AND #127 / STA K+2 -- R keeps the sign, K+2 takes the magnitude.
  _work.r = _a;
  _work.k[2] = static_cast<std::uint8_t>(_a & 0x7Fu);

  const std::uint8_t magnitude = static_cast<std::uint8_t>(_work.q & 0x7Fu);
  if (magnitude == 0u)
  {
    // 6502: BEQ MU5 -- and MU5 zeroes all four bytes of K, sign included.
    _work.k[0] = 0;
    _work.k[1] = 0;
    _work.k[2] = 0;
    _work.k[3] = 0;
    return;
  }

  // 6502: SEC / SBC #1 / STA T. See the header: the missing one comes back as the carry.
  _work.t = static_cast<std::uint8_t>(magnitude - 1u);

  /*
   * 6502: LDA P+1 / LSR K+2 / ROR A / STA K+1 / LDA P / ROR A / STA K.
   *
   * One right shift of the whole twenty-four bit magnitude, which seeds the loop with the first
   * bit already in the carry.
   */
  bool carry = (_work.k[2] & 1u) != 0u;
  _work.k[2] = static_cast<std::uint8_t>(_work.k[2] >> 1);

  std::uint8_t shifted = static_cast<std::uint8_t>((_work.p1 >> 1) | (carry ? 0x80u : 0u));
  carry = (_work.p1 & 1u) != 0u;
  _work.k[1] = shifted;

  shifted = static_cast<std::uint8_t>((_work.p >> 1) | (carry ? 0x80u : 0u));
  carry = (_work.p & 1u) != 0u;
  _work.k[0] = shifted;

  // 6502: LDA #0 / LDX #24 / .MUL2
  std::uint8_t accumulator = 0;
  for (int step = 0; step < 24; ++step)
  {
    if (carry)
    {
      // 6502: ADC T -- with the carry set, so this adds |Q| rather than |Q| - 1.
      const std::uint16_t sum = static_cast<std::uint16_t>(accumulator) + _work.t + 1u;
      accumulator = static_cast<std::uint8_t>(sum);
      carry = sum > 0xFFu;
    }

    // 6502: ROR A / ROR K+2 / ROR K+1 / ROR K -- one shift right through all four bytes.
    const bool intoAccumulator = carry;
    carry = (accumulator & 1u) != 0u;
    accumulator = static_cast<std::uint8_t>((accumulator >> 1) | (intoAccumulator ? 0x80u : 0u));

    for (int byte = 2; byte >= 0; --byte)
    {
      const bool next = (_work.k[byte] & 1u) != 0u;
      _work.k[byte] = static_cast<std::uint8_t>((_work.k[byte] >> 1) | (carry ? 0x80u : 0u));
      carry = next;
    }
  }

  // 6502: STA T / LDA R / EOR Q / AND #128 / ORA T / STA K+3 -- the sign is the two operands'.
  _work.t = accumulator;
  _work.k[3] = static_cast<std::uint8_t>(accumulator | ((_work.r ^ _work.q) & 0x80u));
}

void Normalise(MathWorkspace& _work, std::span<std::uint8_t, 3> _vector) noexcept
{
  /*
   * 6502: LDA XX15 / JSR SQUA / STA R / LDA P / STA Q, then the same for the other two with the
   * running sum added in. The additions are `ADC` with no `CLC`, so the carry SQUA leaves is part
   * of them -- see the header.
   */
  _work.r = Square(_work, _vector[0]).high;
  _work.q = _work.p;

  bool carry = false;
  for (int axis = 1; axis < 3; ++axis)
  {
    _work.t = Square(_work, _vector[axis]).high;

    const std::uint16_t low = static_cast<std::uint16_t>(_work.p) + _work.q + (carry ? 1u : 0u);
    _work.q = static_cast<std::uint8_t>(low);
    carry = low > 0xFFu;

    const std::uint16_t high = static_cast<std::uint16_t>(_work.t) + _work.r + (carry ? 1u : 0u);
    _work.r = static_cast<std::uint8_t>(high);
    carry = high > 0xFFu;
  }

  // 6502: JSR LL5 -- Q = sqrt(R Q). The exit carry is not read here.
  (void)SquareRoot(_work);

  // 6502: LDA XX15,n / JSR TIS2 / STA XX15,n -- each component scaled to a length of 96.
  for (int axis = 0; axis < 3; ++axis)
  {
    _vector[axis] = DivideByQ(_work, _vector[axis]);
  }
}


void DivideSignedToK(MathWorkspace& _work) noexcept
{
  // P(2 1 0) is forced to at least 1, for the same reason Q is: the scaling loop below shifts
  // until a set bit arrives, and an all-zero numerator has none to give it.
  _work.p = static_cast<std::uint8_t>(_work.p | 0x01u);

  // The sign of the answer, put aside now because the division that follows is on magnitudes.
  _work.t = static_cast<std::uint8_t>((_work.p2 ^ _work.s) & 0x80u);

  // The scale factor, counted UP by the numerator's shifts and DOWN by the denominator's, so
  // what is left at the end is the difference -- and a byte, so it wraps rather than going
  // negative, which is why the test below is on bit 7 and not on a comparison.
  std::uint8_t y = 0;

  std::uint8_t a = static_cast<std::uint8_t>(_work.p2 & 0x7Fu);

  // 6502: DVL9 -- shift the numerator up until its top byte reaches 64.
  //
  // The second condition is the `BNE DVL9` at the bottom, which the upstream source calls
  // "effectively a JMP, as Y will never be zero". It is a JMP given the `ORA #1` above, which
  // guarantees a set bit to shift up within twenty-four steps -- but it is the loop's ONLY exit
  // when there is not one, and a port that dropped it would hang where the original returns a
  // wrong answer. Cheaper to keep than to argue about.
  while (a < 64u)
  {
    const ShiftResult low = RotateLeftValue(_work.p, false);
    _work.p = low.value;
    const ShiftResult middle = RotateLeftValue(_work.p1, low.carry);
    _work.p1 = middle.value;
    const ShiftResult high = RotateLeftValue(a, middle.carry);
    a = high.value;
    ++y;
    if (y == 0u)
    {
      break;
    }
  }

  _work.p2 = a;

  // 6502: DVL6 -- and the denominator up until its top BIT is set. The decrement is at the top
  // of the loop and the test at the bottom, so this always runs at least once.
  a = static_cast<std::uint8_t>(_work.s & 0x7Fu);
  do
  {
    --y;
    const ShiftResult low = RotateLeftValue(_work.q, false);
    _work.q = low.value;
    const ShiftResult middle = RotateLeftValue(_work.r, low.carry);
    _work.r = middle.value;
    const ShiftResult high = RotateLeftValue(a, middle.carry);
    a = high.value;
  } while ((a & 0x80u) == 0u);

  // 6502: DV9 -- the two top bytes are now as large as they will go, so the ratio can be had
  // from them alone.
  _work.q = a;
  _work.r = 254;
  a = _work.p2;

  // 6502: LL31new / LL29new -- LL31's body, inlined in the original and a loop here. R is both
  // the answer and the counter: the eight bits shifted in push the seven set bits out, and the
  // zero underneath them ends the loop when it reaches the top.
  for (;;)
  {
    const ShiftResult shifted = RotateLeftValue(a, false);
    a = shifted.value;

    bool bit = false;
    if (shifted.carry)
    {
      // The numerator has a ninth bit, so the subtraction cannot borrow and the original does
      // not bother testing -- it subtracts and forces the quotient bit with a `SEC`.
      a = SubtractWithCarry(a, _work.q, true).value;
      bit = true;
    }
    else if (a >= _work.q)
    {
      a = SubtractWithCarry(a, _work.q, true).value;
      bit = true;
    }

    const ShiftResult quotient = RotateLeft(_work.r, bit);
    _work.r = quotient.value;
    if (!quotient.carry)
    {
      break;
    }
  }

  // 6502: LL312new -- the answer is the byte in R, and all that is left is to put it back on
  // the scale the two loops above took it off.
  _work.k[1] = 0;
  _work.k[2] = 0;
  _work.k[3] = 0;

  if ((y & 0x80u) != 0u)
  {
    // 6502: DVL8 -- Y came out negative, so the denominator was shifted further than the
    // numerator and the answer is scaled back UP, through all four bytes of K.
    a = _work.r;
    do
    {
      const ShiftResult low = RotateLeftValue(a, false);
      a = low.value;
      const ShiftResult k1 = RotateLeft(_work.k[1], low.carry);
      _work.k[1] = k1.value;
      const ShiftResult k2 = RotateLeft(_work.k[2], k1.carry);
      _work.k[2] = k2.value;
      _work.k[3] = RotateLeft(_work.k[3], k2.carry).value;
      ++y;
    } while (y != 0u);

    _work.k[0] = a;

    // The sign is ORed in here and STORED on the other two paths, because only this one can
    // have shifted something into K+3 that is worth keeping.
    _work.k[3] = static_cast<std::uint8_t>(_work.k[3] | _work.t);
    return;
  }

  if (y == 0u)
  {
    // 6502: DV13 -- the two scalings cancelled, so R is already the answer.
    _work.k[0] = _work.r;
    _work.k[3] = _work.t;
    return;
  }

  // 6502: DVL10 -- Y is positive, so the answer is scaled back DOWN. The top three bytes stay
  // zero: nothing shifted right out of the lowest byte can reach them.
  a = _work.r;
  do
  {
    a = static_cast<std::uint8_t>(a >> 1);
    --y;
  } while (y != 0u);

  _work.k[0] = a;
  _work.k[3] = _work.t;
}


void DivideToUR(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  // 6502: LL84 -- the divisor is zero, so there is no answer to give.
  if (_work.q == 0u)
  {
    _work.r = 50;
    _work.u = 50;
    return;
  }

  // 6502: LL63 -- halve A until LL28 will take it. The shift happens before the test, so an A
  // that is already smaller than Q is still halved once and the count is still one.
  std::uint8_t shifts = 0;
  std::uint8_t value = _a;
  do
  {
    value = static_cast<std::uint8_t>(value >> 1);
    ++shifts;
  } while (value >= _work.q);

  _work.s = shifts;
  (void)DivideToR(_work, value);

  // 6502: LL64 -- and double the answer back, through U. The sign test is on U after the rotate,
  // so an answer that needs seventeen bits is an overflow and takes the same exit as a zero
  // divisor does.
  std::uint8_t doubled = _work.r;
  for (std::uint8_t remaining = shifts; remaining != 0u; --remaining)
  {
    const ShiftResult shifted = RotateLeftValue(doubled, false);
    doubled = shifted.value;
    _work.u = RotateLeft(_work.u, shifted.carry).value;

    if ((_work.u & 0x80u) != 0u)
    {
      _work.r = 50;
      _work.u = 50;
      return;
    }
  }

  _work.r = doubled;
}

} // namespace Elite
