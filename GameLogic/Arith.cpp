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
 *
 * Every routine here takes its operands as values and returns what it leaves (Modernize.md
 * M2-b). Where the original stored a scratch byte -- `T`, `T1`, `U`, `widget` -- the port holds a
 * local of the same name, so the listing beside each routine still reads against the code.
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

  Product MultiplyUnguarded(std::uint8_t _multiplicand, std::uint8_t _multiplier) noexcept
  {
    // 6502: DEX / STX T -- the decremented multiplier, which the set carry below puts back.
    const std::uint8_t t = static_cast<std::uint8_t>(_multiplier - 1);

    std::uint8_t a = 0;

    // 6502: LSR P -- the first multiplier bit is shifted out before the loop starts.
    bool carry = (_multiplicand & 0x01u) != 0u;
    std::uint8_t low = static_cast<std::uint8_t>(_multiplicand >> 1);

    for (int step = 0; step < 8; ++step)
    {
      ShiftAndAddStep(a, low, t, carry);
    }

    // The carry is the last `ROR P`'s, and three callers read it before doing anything that would
    // set it themselves. See the note on Product.
    return Product{a, low, carry};
  }

  Product MultiplyUnsigned(std::uint8_t _multiplicand, std::uint8_t _multiplier) noexcept
  {
    if (_multiplier == 0)
    {
      // 6502: MU1 -- `CLC / STX P / TXA / RTS`, so the zero case clears both halves AND the carry.
      // The `CLC` is the part a port drops, and it is what stops a zero multiply looking like an
      // overflow to the caller below it.
      return Product{0, 0, false};
    }

    return MultiplyUnguarded(_multiplicand, _multiplier);
  }

  Product MultiplyMagnitude(std::uint8_t _value, std::uint8_t _multiplier) noexcept
  {
    // 6502: AND #%01111111 / STA P / JMP MULTU.
    return MultiplyUnsigned(static_cast<std::uint8_t>(_value & 0x7Fu), _multiplier);
  }

  Product MultiplySigned(std::uint8_t _value, std::uint8_t _multiplier) noexcept
  {
    // The sign of the product is decided up front and re-applied at the very end.
    const std::uint8_t t = static_cast<std::uint8_t>((_value ^ _multiplier) & 0x80u);

    // P starts as |A| >> 1, and the bit shifted out is the first multiplier bit.
    const std::uint8_t magnitude = static_cast<std::uint8_t>(_value & 0x7Fu);
    bool carry = (magnitude & 0x01u) != 0u;
    std::uint8_t low = static_cast<std::uint8_t>(magnitude >> 1);

    const std::uint8_t multiplier = static_cast<std::uint8_t>(_multiplier & 0x7Fu);
    if (multiplier == 0)
    {
      // 6502: mu10 -- a zero multiplier zeroes the low byte and returns zero, sign and all. The
      // carry is the `LSR A`'s above: nothing between it and the `RTS` touches the flag.
      return Product{0, 0, carry};
    }

    const std::uint8_t t1 = static_cast<std::uint8_t>(multiplier - 1);

    std::uint8_t a = 0;

    // Seven steps here rather than eight: the eighth bit was consumed by the shift above, and
    // the final shift below completes it.
    for (int step = 0; step < 7; ++step)
    {
      ShiftAndAddStep(a, low, t1, carry);
    }

    carry = (a & 0x01u) != 0u;
    a = static_cast<std::uint8_t>(a >> 1);

    // 6502: ROR P / ORA T -- the last rotate's carry out is the routine's, `ORA` leaves it alone.
    const ShiftResult rotatedLow = RotateRight(low, carry);
    low = rotatedLow.value;

    return Product{static_cast<std::uint8_t>(a | t), low, rotatedLow.carry};
  }

  Product SquareUnsigned(std::uint8_t _value) noexcept
  {
    if (_value == 0)
    {
      // 6502: MU1 -- CLC / STX P / TXA / RTS. It falls into the same zero tail the unsigned
      // multiply uses, and that tail CLEARS the carry, which is half of why `MAS3` can read one.
      return Product{0, 0, false};
    }

    // 6502: STA P / TAX / BNE MU11 -- the value is both operands.
    return MultiplyUnguarded(_value, _value);
  }

  Product Square(std::uint8_t _value) noexcept
  {
    return SquareUnsigned(static_cast<std::uint8_t>(_value & 0x7Fu));
  }

  AddSignedResult AddSigned(SignMag16 _value, SignMag16 _addend) noexcept
  {
    // 6502: STA T1 / AND #128 / STA T -- the first operand's high byte and its sign.
    const std::uint8_t t1 = _value.hi;
    const std::uint8_t t = static_cast<std::uint8_t>(_value.hi & 0x80u);

    if (((t ^ _addend.hi) & 0x80u) == 0u)
    {
      // Signs agree, so the magnitudes simply add and the shared sign is put back on top.
      const AddResult low = AddWithCarry(_addend.lo, _value.lo, false);
      const AddResult high = AddWithCarry(_addend.hi, t1, low.carry);
      // 6502: `ORA T` does not touch the carry, so what `ADC T1` produced is what the caller gets.
      return AddSignedResult{static_cast<std::uint8_t>(high.value | t), low.value, high.carry};
    }

    // 6502: MU8 -- signs differ, so this is a subtraction of magnitudes that may come out
    // negative, in which case the result is negated and marked.
    std::uint8_t u = static_cast<std::uint8_t>(_addend.hi & 0x7Fu);

    const std::uint16_t lowDifference = static_cast<std::uint16_t>(_value.lo) - _addend.lo;
    std::uint8_t low = static_cast<std::uint8_t>(lowDifference);
    bool borrowClear = lowDifference < 0x100u;

    const std::uint16_t highDifference = static_cast<std::uint16_t>(t1 & 0x7Fu) - u - (borrowClear ? 0u : 1u);
    std::uint8_t high = static_cast<std::uint8_t>(highDifference);
    borrowClear = highDifference < 0x100u;

    // 6502: BCS MU9 -- taken means no borrow, and the carry it was taken on is the exit carry.
    bool exitCarry = borrowClear;

    if (!borrowClear)
    {
      // 6502: the branch that turns a negative difference back into sign-magnitude form.
      u = high;

      const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(low ^ 0xFFu), 1u, false);
      low = negated.value;

      const std::uint16_t negatedHigh = 0u - u - (negated.carry ? 0u : 1u);
      high = static_cast<std::uint8_t>(static_cast<std::uint8_t>(negatedHigh) | 0x80u);
      exitCarry = negatedHigh < 0x100u; // 6502: the second `SBC U`
    }

    // 6502: MU9 -- fold in the sign the first operand arrived with. `EOR T` leaves the carry.
    return AddSignedResult{static_cast<std::uint8_t>(high ^ t), low, exitCarry};
  }

  AddSignedResult MultiplyAndAdd(std::uint8_t _value, std::uint8_t _multiplier, SignMag16 _addend) noexcept
  {
    // In the original this is one call followed by a fall-through into the addition, which is
    // why the multiply's low byte is left where the addition expects to find it.
    const Product product = MultiplySigned(_value, _multiplier);
    return AddSigned(product.Pair(), _addend);
  }

  Product MultiplyScaled(std::uint8_t _multiplicand, std::uint8_t _value) noexcept
  {
    const std::uint8_t t = static_cast<std::uint8_t>(_value & 0x80u);

    const std::uint8_t multiplier = static_cast<std::uint8_t>(_value & 0x7Fu);
    if (multiplier == 0)
    {
      // 6502: BEQ MU6 -- STA P+1 / STA P / RTS, with A zero. The carry is whatever the caller
      // arrived with, and no caller reads it: `ADD` and `MULT1` both set their own.
      return Product{0, 0, false};
    }

    const std::uint8_t t1 = static_cast<std::uint8_t>(multiplier - 1);

    std::uint8_t a = 0;
    bool carry = (_multiplicand & 0x01u) != 0u;
    std::uint8_t low = static_cast<std::uint8_t>(_multiplicand >> 1);

    // Only five of the eight bits get an addition.
    for (int step = 0; step < 5; ++step)
    {
      ShiftAndAddStep(a, low, t1, carry);
    }

    // The remaining three are shifted through without one, which is what scales the result down.
    for (int step = 0; step < 3; ++step)
    {
      carry = (a & 0x01u) != 0u;
      a = static_cast<std::uint8_t>(a >> 1);
      const ShiftResult rotated = RotateRight(low, carry);
      low = rotated.value;
      carry = rotated.carry;
    }

    return Product{static_cast<std::uint8_t>(a | t), low, carry};
  }

  Product24 MultiplyWide(std::uint8_t _high, std::uint8_t _low, std::uint8_t _multiplier) noexcept
  {
    // The multiplier arrives complemented, which turns the usual add-on-a-set-bit test into an
    // add-on-a-clear-bit one and saves the routine an instruction per step.
    const std::uint8_t complemented = static_cast<std::uint8_t>(_high ^ 0xFFu);

    bool carry = (complemented & 0x01u) != 0u;
    std::uint8_t mid = static_cast<std::uint8_t>(complemented >> 1);

    std::uint8_t a = 0;

    const ShiftResult seeded = RotateRight(_low, carry);
    std::uint8_t low = seeded.value;
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
        const AddResult sum = AddWithCarry(a, _multiplier, false);
        a = sum.value;
        carry = sum.carry;

        const ShiftResult rotated = RotateRight(a, carry);
        a = rotated.value;
        carry = rotated.carry;
      }

      const ShiftResult rotatedMiddle = RotateRight(mid, carry);
      mid = rotatedMiddle.value;
      carry = rotatedMiddle.carry;

      const ShiftResult rotatedLow = RotateRight(low, carry);
      low = rotatedLow.value;
      carry = rotatedLow.carry;
    }

    // The carry is the one the final `ROR P` left, which is what MVEIT reads. See the header.
    return Product24{a, mid, low, carry};
  }

  std::uint8_t DivideBy96(std::uint8_t _value) noexcept
  {
    const std::uint8_t t = static_cast<std::uint8_t>(_value & 0x80u);

    std::uint8_t a = static_cast<std::uint8_t>(_value & 0x7Fu);

    // The counter doubles as the result: it is seeded with seven set bits, and each rotation
    // both shifts a quotient bit in at the bottom and pushes a marker out of the top. When the
    // markers run out the loop is done, so no separate counter is needed.
    std::uint8_t t1 = 0xFEu;

    bool looping = true;
    while (looping)
    {
      const ShiftResult shifted = {static_cast<std::uint8_t>(a << 1), (a & 0x80u) != 0u};
      a = shifted.value;

      bool quotientBit = a >= 96u;
      if (quotientBit)
      {
        a = static_cast<std::uint8_t>(a - 96u);
      }

      const ShiftResult counter = RotateLeftValue(t1, quotientBit);
      t1 = counter.value;
      looping = counter.carry;
    }

    return static_cast<std::uint8_t>(t1 | t);
  }

  std::uint8_t MultiplyAddDivide96(std::uint8_t _value, std::uint8_t _multiplier, SignMag16 _addend) noexcept
  {
    // 6502: STX Q, then flipping the sign bit is what turns the accumulate into a subtract.
    const AddSignedResult combined = MultiplyAndAdd(static_cast<std::uint8_t>(_value ^ 0x80u), _multiplier, _addend);

    return DivideBy96(combined.high);
  }

  std::uint8_t DivideSigned(std::uint8_t _value, std::uint8_t _divisor) noexcept
  {
    const std::uint8_t sign = static_cast<std::uint8_t>(_value & 0x80u);
    std::uint8_t a = static_cast<std::uint8_t>(_value & 0x7Fu);

    if (a >= _divisor)
    {
      // 6502: TI4 -- the magnitude is too large to divide, so the result saturates.
      return static_cast<std::uint8_t>(sign | 96u);
    }

    std::uint8_t t = 0xFEu;

    bool looping = true;
    while (looping)
    {
      a = static_cast<std::uint8_t>(a << 1);

      bool quotientBit = a >= _divisor;
      if (quotientBit)
      {
        a = static_cast<std::uint8_t>(a - _divisor);
      }

      const ShiftResult counter = RotateLeftValue(t, quotientBit);
      t = counter.value;
      looping = counter.carry;
    }

    // The quotient is then scaled by a shift-and-add rather than a second division.
    std::uint8_t value = t;
    value = static_cast<std::uint8_t>(value >> 1);
    value = static_cast<std::uint8_t>(value >> 1);
    t = value;

    // Only the THIRD `LSR`'s carry is read. The first two set it in the original too and nothing
    // between them tests it, so the port wrote all three and used one until slice 5d.
    bool carry = (value & 0x01u) != 0u;
    value = static_cast<std::uint8_t>(value >> 1);

    const AddResult scaled = AddWithCarry(value, t, carry);
    t = scaled.value;

    return static_cast<std::uint8_t>(sign | t);
  }

  WideQuotient DivideWide(std::uint8_t _high, std::uint8_t _low, std::uint8_t _divisor) noexcept
  {
    // 6502: STA P+1 / EOR Q / AND #128 / STA T.
    std::uint8_t high = _high;
    const std::uint8_t t = static_cast<std::uint8_t>((_high ^ _divisor) & 0x80u);

    std::uint8_t a = 0;

    // Shift the dividend up one and clear the divisor's sign bit, so the comparison below is
    // between magnitudes. Clearing the sign bit costs a shift up and back down, which also
    // leaves the carry clear -- and that clear carry is what the first rotate below shifts in.
    bool carry = (_low & 0x80u) != 0u;
    std::uint8_t low = static_cast<std::uint8_t>(_low << 1);
    const ShiftResult rolled = RotateLeftValue(high, carry);
    high = rolled.value;

    const std::uint8_t divisor = static_cast<std::uint8_t>(_divisor & 0x7Fu);
    carry = false;

    for (int step = 0; step < 16; ++step)
    {
      // The carry threads right through the loop: what falls out of the top of the quotient on
      // one step is shifted into the remainder on the next. Restarting it at zero each time
      // looks harmless and quietly produces a different number.
      const ShiftResult shifted = RotateLeftValue(a, carry);
      a = shifted.value;

      const bool quotientBit = a >= divisor;
      if (quotientBit)
      {
        a = static_cast<std::uint8_t>(a - divisor);
      }

      const ShiftResult lowStep = RotateLeftValue(low, quotientBit);
      low = lowStep.value;

      const ShiftResult highStep = RotateLeftValue(high, lowStep.carry);
      high = highStep.value;
      carry = highStep.carry;
    }

    return WideQuotient{high, low, t};
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

  LogProduct MultiplyByLog(std::uint8_t _value, std::uint8_t _multiplier, bool _carryIn) noexcept
  {
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
    if (_value == 0 || _multiplier == 0)
    {
      return LogProduct{0, _carryIn};
    }

    // 6502: STA widget -- the first operand, parked while X addresses the tables.
    const AddResult low = AddWithCarry(LOG_LOW_TABLE[_value], LOG_LOW_TABLE[_multiplier], false);
    const bool useOddTable = (low.value & 0x80u) != 0u;

    const AddResult high = AddWithCarry(LOG_TABLE[_multiplier], LOG_TABLE[_value], low.carry);
    if (!high.carry)
    {
      return LogProduct{0, false}; // 6502: BCC MU3again -- the branch is taken, so the carry is clear
    }

    // 6502: the two antilog exits, reached because the BCC above was NOT taken.
    return LogProduct{useOddTable ? ANTILOG_ODD_TABLE[high.value] : ANTILOG_TABLE[high.value], true};
  }

  namespace
  {

    /*
     * 6502: LL28's body, from the STA widget onwards -- the part after the "does it fit" guard.
     *
     * It is a helper because the shipped game has this code TWICE: once inside LL28, and once
     * unlabelled at the end of DVID4, which falls into it. The second copy is byte-identical except
     * that it has no guard in front, so the two share this and differ only in what precedes it.
     */
    Quotient DivideByLogUnguarded(std::uint8_t _dividend, std::uint8_t _divisor) noexcept
    {
      if (_dividend == 0)
      {
        return Quotient{0, false};
      }

      const std::uint16_t lowDifference = static_cast<std::uint16_t>(LOG_LOW_TABLE[_dividend]) - LOG_LOW_TABLE[_divisor];
      const std::uint8_t lowResult = static_cast<std::uint8_t>(lowDifference);
      const bool borrowClear = lowDifference < 0x100u;
      const bool useOddTable = (lowResult & 0x80u) != 0u;

      const std::uint16_t highDifference = static_cast<std::uint16_t>(LOG_TABLE[_dividend]) - LOG_TABLE[_divisor] - (borrowClear ? 0u : 1u);

      if (highDifference < 0x100u)
      {
        // No borrow means the quotient overflowed a byte, which is the saturating case again.
        return Quotient{255, true};
      }

      const std::uint8_t index = static_cast<std::uint8_t>(highDifference);
      return Quotient{useOddTable ? ANTILOG_ODD_TABLE[index] : ANTILOG_TABLE[index], false};
    }

  } // namespace

  Quotient DivideByLog(std::uint8_t _dividend, std::uint8_t _divisor) noexcept
  {
    if (_dividend >= _divisor)
    {
      // 6502: LL2 -- the ratio does not fit in a byte, so it pins at the maximum. The carry the
      // comparison left is set, and callers read it.
      return Quotient{255, true};
    }

    return DivideByLogUnguarded(_dividend, _divisor);
  }

  SignedSum CombineSigned(std::uint8_t _termSign, std::uint8_t _term, SignMag16 _total) noexcept
  {
    if (((_termSign ^ _total.hi) & 0x80u) == 0u)
    {
      // Signs agree, so the two parts add -- and this is the ONLY path that can return a set
      // carry, which is what makes the flag mean "overflowed".
      const AddResult sum = AddWithCarry(_term, _total.lo, false);
      return SignedSum{sum.value, _total.hi, sum.carry};
    }

    // 6502: LL39 -- signs differ, so they subtract, and a borrow means the answer changed sign.
    const std::uint16_t difference = static_cast<std::uint16_t>(_total.lo) - _term;
    const std::uint8_t result = static_cast<std::uint8_t>(difference);

    if (difference < 0x100u)
    {
      // The original's `CLC` here looks dead -- the `SBC` above it left the carry set, and nothing
      // in this branch reads it. It is not dead: it is what stops a subtraction being reported as
      // an overflow.
      return SignedSum{result, _total.hi, false};
    }

    // 6502: LL40 -- flip the sign held in S and negate the magnitude. The negation's own carry can
    // only be set for a zero magnitude, which an underflow cannot produce, so this exit is always
    // carry clear.
    const std::uint8_t flipped = static_cast<std::uint8_t>(_total.hi ^ 0x80u);
    const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(result ^ 0xFFu), 1u, false);
    return SignedSum{negated.value, flipped, negated.carry};
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

    RatioAngle AngleOfRatio(std::uint8_t _dividend, std::uint8_t _divisor) noexcept
    {
      std::uint8_t value = DivideByLog(_dividend, _divisor).value;
      bool carry = false;
      for (int shift = 0; shift < 3; ++shift)
      {
        carry = (value & 0x01u) != 0u;
        value = static_cast<std::uint8_t>(value >> 1);
      }

      return RatioAngle{ARCTAN_TABLE[value], carry};
    }

  } // namespace

  std::uint8_t Arctan(std::uint8_t _numerator, std::uint8_t _denominator) noexcept
  {
    // 6502: LDA P / EOR Q / STA T1 -- the operands' signs, which decide the quadrant at the end.
    const std::uint8_t t1 = static_cast<std::uint8_t>(_numerator ^ _denominator);

    if (_denominator == 0)
    {
      // 6502: AR2 -- a zero denominator is a right angle by convention.
      return 63;
    }

    const std::uint8_t denominator = static_cast<std::uint8_t>(_denominator << 1);
    const std::uint8_t numerator = static_cast<std::uint8_t>(_numerator << 1);

    std::uint8_t angle = 0;
    bool carry = false;

    if (numerator >= denominator)
    {
      // 6502: AR1 -- the ratio is the wrong way up, so it is inverted and the angle reflected:
      // `STA Q / ... STA P`, and ARS1 divides the old denominator by the new one.
      const RatioAngle ratio = AngleOfRatio(denominator, numerator);
      const std::uint8_t t = ratio.angle;

      const std::uint16_t reflected = 64u - t - (ratio.carry ? 0u : 1u);
      if (reflected >= 0x100u)
      {
        return 63;
      }

      angle = static_cast<std::uint8_t>(reflected);
      carry = true;
    }
    else
    {
      angle = AngleOfRatio(numerator, denominator).angle;
      carry = true;
    }

    // 6502: AR4 -- the operands' signs decided the quadrant before any of this ran.
    if ((t1 & 0x80u) != 0u)
    {
      const std::uint8_t t = angle;
      const std::uint16_t opposite = 128u - t - (carry ? 0u : 1u);
      return static_cast<std::uint8_t>(opposite);
    }

    return angle;
  }

  LogProduct MultiplyBySine(std::uint8_t _value, std::uint8_t _angle, bool _carryIn) noexcept
  {
    // 6502: FMLTU2's own three instructions. It then falls through into FMLTU with K in A, so the
    // multiplicand is K and the multiplier is the sine it just put in Q.
    return MultiplyByLog(_value, SINE_TABLE[_angle & 0x1Fu], _carryIn);
  }

  ScaledDivision DivideAndScale(std::uint8_t _dividend, std::uint8_t _divisor) noexcept
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
    const ShiftResult shifted = RotateLeftValue(_dividend, false);
    std::uint8_t whole = shifted.value;

    std::uint8_t remainder = 0;
    bool carry = shifted.carry;

    for (int step = 0; step < 8; ++step)
    {
      const ShiftResult next = RotateLeftValue(remainder, carry);
      remainder = next.value;

      // 6502: CMP Q / BCC / SBC Q -- subtract only when it fits, and the comparison's carry is
      // both the decision and the quotient bit.
      carry = remainder >= _divisor;
      if (carry)
      {
        remainder = static_cast<std::uint8_t>(remainder - _divisor);
      }

      const ShiftResult quotient = RotateLeftValue(whole, carry);
      whole = quotient.value;
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
    const Quotient fraction = DivideByLogUnguarded(remainder, _divisor);
    return ScaledDivision{whole, fraction.value, fraction.carry};
  }

  Root SquareRoot(std::uint8_t _high, std::uint8_t _low) noexcept
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
    std::uint8_t y = _high;
    std::uint8_t s = _low;
    std::uint8_t x = 0;
    std::uint8_t q = 0;
    bool exitCarry = false;

    for (int round = 0; round < 8; ++round)
    {
      // 6502: CPX Q / BCC LL7 / BNE / CPY #64 / BCC LL7 -- does (Q 0x40) fit into (X Y)?
      bool fits = false;
      if (x > q)
      {
        fits = true;
      }
      else if (x == q && y >= 0x40u)
      {
        fits = true;
      }

      if (fits)
      {
        // 6502: TYA / SBC #64 / TAY / TXA / SBC Q / TAX -- the comparison left carry set, so the
        // subtraction borrows nothing on its first half.
        const std::uint16_t low = static_cast<std::uint16_t>(y) - 0x40u;
        y = static_cast<std::uint8_t>(low);
        const std::uint16_t high = static_cast<std::uint16_t>(x) - q - (low < 0x100u ? 0u : 1u);
        x = static_cast<std::uint8_t>(high);
      }

      // 6502: LL7 -- ROL Q brings in the answer bit, which is the carry the comparison left set
      // exactly when the candidate fitted.
      q = RotateLeftValue(q, fits).value;

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
    return Root{q, exitCarry};
  }

  KBlock MultiplySigned24(SignMag24 _value, std::uint8_t _multiplier) noexcept
  {
    // 6502: STA R / AND #127 / STA K+2 -- R keeps the sign, K+2 takes the magnitude.
    const std::uint8_t r = _value.sgn;
    KBlock k;
    k.high = static_cast<std::uint8_t>(_value.sgn & 0x7Fu);

    const std::uint8_t magnitude = static_cast<std::uint8_t>(_multiplier & 0x7Fu);
    if (magnitude == 0u)
    {
      // 6502: BEQ MU5 -- and MU5 zeroes all four bytes of K, sign included.
      return KBlock::Filled(0);
    }

    // 6502: SEC / SBC #1 / STA T. See the header: the missing one comes back as the carry.
    const std::uint8_t t = static_cast<std::uint8_t>(magnitude - 1u);

    /*
     * 6502: LDA P+1 / LSR K+2 / ROR A / STA K+1 / LDA P / ROR A / STA K.
     *
     * One right shift of the whole twenty-four bit magnitude, which seeds the loop with the first
     * bit already in the carry.
     */
    bool carry = (k.high & 1u) != 0u;
    k.high = static_cast<std::uint8_t>(k.high >> 1);

    k.mid = static_cast<std::uint8_t>((_value.hi >> 1) | (carry ? 0x80u : 0u));
    carry = (_value.hi & 1u) != 0u;

    k.low = static_cast<std::uint8_t>((_value.lo >> 1) | (carry ? 0x80u : 0u));
    carry = (_value.lo & 1u) != 0u;

    // 6502: LDA #0 / LDX #24 / .MUL2
    std::uint8_t accumulator = 0;
    for (int step = 0; step < 24; ++step)
    {
      if (carry)
      {
        // 6502: ADC T -- with the carry set, so this adds |Q| rather than |Q| - 1.
        const std::uint16_t sum = static_cast<std::uint16_t>(accumulator) + t + 1u;
        accumulator = static_cast<std::uint8_t>(sum);
        carry = sum > 0xFFu;
      }

      // 6502: ROR A / ROR K+2 / ROR K+1 / ROR K -- one shift right through all four bytes.
      const bool intoAccumulator = carry;
      carry = (accumulator & 1u) != 0u;
      accumulator = static_cast<std::uint8_t>((accumulator >> 1) | (intoAccumulator ? 0x80u : 0u));

      for (std::uint8_t* const byte : {&k.high, &k.mid, &k.low})
      {
        const bool next = (*byte & 1u) != 0u;
        *byte = static_cast<std::uint8_t>((*byte >> 1) | (carry ? 0x80u : 0u));
        carry = next;
      }
    }

    // 6502: STA T / LDA R / EOR Q / AND #128 / ORA T / STA K+3 -- the sign is the two operands'.
    k.top = static_cast<std::uint8_t>(accumulator | ((r ^ _multiplier) & 0x80u));
    return k;
  }

  std::uint8_t Normalise(std::span<std::uint8_t, 3> _vector) noexcept
  {
    /*
     * 6502: LDA XX15 / JSR SQUA / STA R / LDA P / STA Q, then the same for the other two with the
     * running sum added in. The additions are `ADC` with no `CLC`, so the carry SQUA leaves is part
     * of them -- see the header.
     */
    const Product first = Square(_vector[0]);
    std::uint8_t r = first.high;
    std::uint8_t q = first.low;

    bool carry = false;
    for (int axis = 1; axis < 3; ++axis)
    {
      const Product squared = Square(_vector[axis]);
      const std::uint8_t t = squared.high;

      const std::uint16_t low = static_cast<std::uint16_t>(squared.low) + q + (carry ? 1u : 0u);
      q = static_cast<std::uint8_t>(low);
      carry = low > 0xFFu;

      const std::uint16_t high = static_cast<std::uint16_t>(t) + r + (carry ? 1u : 0u);
      r = static_cast<std::uint8_t>(high);
      carry = high > 0xFFu;
    }

    // 6502: JSR LL5 -- Q = sqrt(R Q). The exit carry is not read here.
    const std::uint8_t length = SquareRoot(r, q).value;

    // 6502: LDA XX15,n / JSR TIS2 / STA XX15,n -- each component scaled to a length of 96.
    for (int axis = 0; axis < 3; ++axis)
    {
      _vector[axis] = DivideSigned(_vector[axis], length);
    }

    return length;
  }

  KBlock DivideSigned24(SignMag24 _numerator, SignMag24 _denominator) noexcept
  {
    // P(2 1 0) is forced to at least 1, for the same reason Q is: the scaling loop below shifts
    // until a set bit arrives, and an all-zero numerator has none to give it.
    std::uint8_t p = static_cast<std::uint8_t>(_numerator.lo | 0x01u);
    std::uint8_t p1 = _numerator.hi;

    // The sign of the answer, put aside now because the division that follows is on magnitudes.
    const std::uint8_t t = static_cast<std::uint8_t>((_numerator.sgn ^ _denominator.sgn) & 0x80u);

    // The scale factor, counted UP by the numerator's shifts and DOWN by the denominator's, so
    // what is left at the end is the difference -- and a byte, so it wraps rather than going
    // negative, which is why the test below is on bit 7 and not on a comparison.
    std::uint8_t y = 0;

    std::uint8_t a = static_cast<std::uint8_t>(_numerator.sgn & 0x7Fu);

    // 6502: DVL9 -- shift the numerator up until its top byte reaches 64.
    //
    // The second condition is the `BNE DVL9` at the bottom, which the upstream source calls
    // "effectively a JMP, as Y will never be zero". It is a JMP given the `ORA #1` above, which
    // guarantees a set bit to shift up within twenty-four steps -- but it is the loop's ONLY exit
    // when there is not one, and a port that dropped it would hang where the original returns a
    // wrong answer. Cheaper to keep than to argue about.
    while (a < 64u)
    {
      const ShiftResult low = RotateLeftValue(p, false);
      p = low.value;
      const ShiftResult middle = RotateLeftValue(p1, low.carry);
      p1 = middle.value;
      const ShiftResult high = RotateLeftValue(a, middle.carry);
      a = high.value;
      ++y;
      if (y == 0u)
      {
        break;
      }
    }

    const std::uint8_t p2 = a;

    // 6502: DVL6 -- and the denominator up until its top BIT is set. The decrement is at the top
    // of the loop and the test at the bottom, so this always runs at least once.
    std::uint8_t q = _denominator.lo;
    std::uint8_t r = _denominator.hi;
    a = static_cast<std::uint8_t>(_denominator.sgn & 0x7Fu);
    do
    {
      --y;
      const ShiftResult low = RotateLeftValue(q, false);
      q = low.value;
      const ShiftResult middle = RotateLeftValue(r, low.carry);
      r = middle.value;
      const ShiftResult high = RotateLeftValue(a, middle.carry);
      a = high.value;
    } while ((a & 0x80u) == 0u);

    // 6502: DV9 -- the two top bytes are now as large as they will go, so the ratio can be had
    // from them alone.
    q = a;
    r = 254;
    a = p2;

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
        a = SubtractWithCarry(a, q, true).value;
        bit = true;
      }
      else if (a >= q)
      {
        a = SubtractWithCarry(a, q, true).value;
        bit = true;
      }

      const ShiftResult quotient = RotateLeft(r, bit);
      r = quotient.value;
      if (!quotient.carry)
      {
        break;
      }
    }

    // 6502: LL312new -- the answer is the byte in R, and all that is left is to put it back on
    // the scale the two loops above took it off.
    KBlock k;

    if ((y & 0x80u) != 0u)
    {
      // 6502: DVL8 -- Y came out negative, so the denominator was shifted further than the
      // numerator and the answer is scaled back UP, through all four bytes of K.
      a = r;
      do
      {
        const ShiftResult low = RotateLeftValue(a, false);
        a = low.value;
        const ShiftResult k1 = RotateLeft(k.mid, low.carry);
        k.mid = k1.value;
        const ShiftResult k2 = RotateLeft(k.high, k1.carry);
        k.high = k2.value;
        k.top = RotateLeft(k.top, k2.carry).value;
        ++y;
      } while (y != 0u);

      k.low = a;

      // The sign is ORed in here and STORED on the other two paths, because only this one can
      // have shifted something into K+3 that is worth keeping.
      k.top = static_cast<std::uint8_t>(k.top | t);
      return k;
    }

    if (y == 0u)
    {
      // 6502: DV13 -- the two scalings cancelled, so R is already the answer.
      k.low = r;
      k.top = t;
      return k;
    }

    // 6502: DVL10 -- Y is positive, so the answer is scaled back DOWN. The top three bytes stay
    // zero: nothing shifted right out of the lowest byte can reach them.
    a = r;
    do
    {
      a = static_cast<std::uint8_t>(a >> 1);
      --y;
    } while (y != 0u);

    k.low = a;
    k.top = t;
    return k;
  }

  Quotient16 DivideWideByLog(std::uint8_t _dividend, std::uint8_t _divisor, std::uint8_t _high) noexcept
  {
    // 6502: LL84 -- the divisor is zero, so there is no answer to give.
    if (_divisor == 0u)
    {
      return Quotient16{50, 50};
    }

    // 6502: LL63 -- halve A until LL28 will take it. The shift happens before the test, so an A
    // that is already smaller than Q is still halved once and the count is still one.
    std::uint8_t shifts = 0;
    std::uint8_t value = _dividend;
    do
    {
      value = static_cast<std::uint8_t>(value >> 1);
      ++shifts;
    } while (value >= _divisor);

    // 6502: STX S / JSR LL28 -- the count parked in S, which LL28 leaves alone.
    std::uint8_t doubled = DivideByLog(value, _divisor).value;

    // 6502: LL64 -- and double the answer back, through U. The sign test is on U after the rotate,
    // so an answer that needs seventeen bits is an overflow and takes the same exit as a zero
    // divisor does. U is whatever the caller left there, and the rotate brings its old bits back
    // up: LL9 clears it before the y divide for exactly that reason.
    std::uint8_t high = _high;
    for (std::uint8_t remaining = shifts; remaining != 0u; --remaining)
    {
      const ShiftResult shifted = RotateLeftValue(doubled, false);
      doubled = shifted.value;
      high = RotateLeft(high, shifted.carry).value;

      if ((high & 0x80u) != 0u)
      {
        return Quotient16{50, 50};
      }
    }

    return Quotient16{high, doubled};
  }

} // namespace Elite
