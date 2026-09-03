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

std::uint8_t MultiplyByX(MathWorkspace& _work, std::uint8_t _x) noexcept
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

  return a;
}

std::uint8_t MultiplyUnsigned(MathWorkspace& _work) noexcept
{
  const std::uint8_t multiplier = _work.q;

  if (multiplier == 0)
  {
    // 6502: MU1 -- the zero case returns through a different tail that clears both halves.
    _work.p = 0;
    return 0;
  }

  return MultiplyByX(_work, multiplier);
}

std::uint8_t MultiplyMagnitudeByQ(MathWorkspace& _work, std::uint8_t _a) noexcept
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

std::uint8_t SquareUnsigned(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.p = _a;

  if (_a == 0)
  {
    // Falls through into the same zero tail the unsigned multiply uses.
    _work.p = 0;
    return 0;
  }

  return MultiplyByX(_work, _a);
}

std::uint8_t Square(MathWorkspace& _work, std::uint8_t _a) noexcept
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
    return AddSignedResult{ static_cast<std::uint8_t>(high.value | _work.t), low.value };
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

  if (!borrowClear)
  {
    // 6502: the branch that turns a negative difference back into sign-magnitude form.
    _work.u = high;

    const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(low ^ 0xFFu), 1u, false);
    low = negated.value;

    const std::uint16_t negatedHigh = 0u - _work.u - (negated.carry ? 0u : 1u);
    high = static_cast<std::uint8_t>(static_cast<std::uint8_t>(negatedHigh) | 0x80u);
  }

  // 6502: MU9 -- fold in the sign the first operand arrived with.
  return AddSignedResult{ static_cast<std::uint8_t>(high ^ _work.t), low };
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

std::uint8_t MultiplyWide(MathWorkspace& _work, std::uint8_t _a) noexcept
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

  return a;
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

std::uint8_t MultiplyByLog(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  _work.widget = _a;

  if (_a == 0 || _work.q == 0)
  {
    return 0;
  }

  const AddResult low = AddWithCarry(LOG_LOW_TABLE[_a], LOG_LOW_TABLE[_work.q], false);
  const bool useOddTable = (low.value & 0x80u) != 0u;

  const AddResult high = AddWithCarry(LOG_TABLE[_work.q], LOG_TABLE[_a], low.carry);
  if (!high.carry)
  {
    return 0;
  }

  return useOddTable ? ANTILOG_ODD_TABLE[high.value] : ANTILOG_TABLE[high.value];
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

std::uint8_t CombineSigned(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  if (((_a ^ _work.s) & 0x80u) == 0u)
  {
    // Signs agree, so the two parts add.
    return AddWithCarry(_work.q, _work.r, false).value;
  }

  // 6502: LL39 -- signs differ, so they subtract, and a borrow means the answer changed sign.
  const std::uint16_t difference = static_cast<std::uint16_t>(_work.r) - _work.q;
  const std::uint8_t result = static_cast<std::uint8_t>(difference);

  if (difference < 0x100u)
  {
    return result;
  }

  // 6502: LL40 -- flip the sign held in S and negate the magnitude.
  _work.s = static_cast<std::uint8_t>(_work.s ^ 0x80u);
  return AddWithCarry(static_cast<std::uint8_t>(result ^ 0xFFu), 1u, false).value;
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

std::uint8_t MultiplyKBySine(MathWorkspace& _work, std::uint8_t _a) noexcept
{
  // 6502: FMLTU2's own three instructions. It then falls through into FMLTU with K in A, so the
  // multiplicand is K and the multiplier is the sine it just put in Q.
  _work.q = SINE_TABLE[_a & 0x1Fu];
  return MultiplyByLog(_work, _work.k[0]);
}

std::uint8_t DivideAndScale(MathWorkspace& _work, std::uint8_t _a) noexcept
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
  (void)DivideByLogarithms(_work, remainder);
  return _work.r;
}

void SquareRoot(MathWorkspace& _work) noexcept
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
      x = RotateLeftValue(x, lowHalf.carry).value;
    }
  }
}

} // namespace Elite
