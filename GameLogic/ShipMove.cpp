#include "pch.h"

#include "ShipMove.h"

#include "Scanner.h"
#include "ShipBlueprint.h"
#include "Tactics.h"
#include "Universe.h"

#include <utility>

#include <array>

namespace Elite
{

  void AddToShipCoordinate(Ship& _work, std::uint8_t _high, std::uint8_t _low, std::uint8_t _axis, bool _maskSign) noexcept
  {
    // 6502: AND #128 -- the two-bytes-earlier entry point, and the only difference between them.
    std::uint8_t accumulator = _maskSign ? static_cast<std::uint8_t>(_high & 0x80u) : _high;
    auto& axis = _work.PositionAt(_axis); // 6502: INWK,X -- the axis the routine was entered with

    /*
     * 6502: ASL A / STA S / LDA #0 / ROR A / STA T / LSR S.
     *
     * S ends as the magnitude of the high byte and T as its sign, and the `LSR` leaves the carry
     * CLEAR -- bit 0 of something just shifted left is always zero -- which is what the addition
     * below runs on.
     */
    std::uint8_t magnitude = static_cast<std::uint8_t>(accumulator << 1);
    const std::uint8_t sign = static_cast<std::uint8_t>((accumulator & 0x80u) != 0u ? 0x80u : 0x00u);
    magnitude = static_cast<std::uint8_t>(magnitude >> 1);
    bool carry = false;

    // 6502: EOR INWK+2,X / BMI MV10 -- A still holds T, so this compares the two signs.
    if (((sign ^ axis.sgn) & 0x80u) == 0u)
    {
      // Same sign: add the magnitudes and keep the sign.
      const AddResult low = AddWithCarry(_low, axis.lo, carry);
      axis.lo = low.value;

      const AddResult middle = AddWithCarry(magnitude, axis.hi, low.carry);
      axis.hi = middle.value;

      const AddResult high = AddWithCarry(axis.sgn, 0, middle.carry);
      axis.sgn = static_cast<std::uint8_t>(high.value | sign);
      return;
    }

    // 6502: MV10 -- opposite signs, so subtract, and the answer may come out the other way round.
    SubResult low = SubtractWithCarry(axis.lo, _low, true);
    axis.lo = low.value;

    SubResult middle = SubtractWithCarry(axis.hi, magnitude, low.carry);
    axis.hi = middle.value;

    SubResult high = SubtractWithCarry(static_cast<std::uint8_t>(axis.sgn & 0x7Fu), 0, middle.carry);
    axis.sgn = static_cast<std::uint8_t>((high.value | 0x80u) ^ sign);

    if (high.carry)
    {
      return; // 6502: BCS MV11
    }

    /*
     * 6502: LDA #1 / SBC INWK,X ... -- the subtraction went past zero, so negate what came out.
     * The carry is clear here (the branch above was not taken), which is what makes `1 - n - 1`
     * the two's complement of n.
     */
    low = SubtractWithCarry(1, axis.lo, false);
    axis.lo = low.value;

    middle = SubtractWithCarry(0, axis.hi, low.carry);
    axis.hi = middle.value;

    high = SubtractWithCarry(0, axis.sgn, middle.carry);
    axis.sgn = static_cast<std::uint8_t>((high.value & 0x7Fu) | sign);
  }

  KBlockSum AddShipCoordinateToK(const Ship& _work, KBlock _k, std::uint8_t _axis) noexcept
  {
    // 6502: LDA K+3 / STA S / AND #128 / STA T / EOR INWK+2,X / BMI MV13.
    std::uint8_t topByte = _k.top;
    const std::uint8_t sign = static_cast<std::uint8_t>(_k.top & 0x80u);
    const auto& axis = _work.PositionAt(_axis); // 6502: INWK,X

    if (((sign ^ axis.sgn) & 0x80u) == 0u)
    {
      // 6502: LDA K+1 / CLC / ADC INWK,X ... -- an explicit CLC here, unlike MVT1's LSR.
      const AddResult low = AddWithCarry(_k.mid, axis.lo, false);
      _k.mid = low.value;

      const AddResult middle = AddWithCarry(_k.high, axis.hi, low.carry);
      _k.high = middle.value;

      const AddResult high = AddWithCarry(_k.top, axis.sgn, middle.carry);
      _k.top = static_cast<std::uint8_t>((high.value & 0x7Fu) | sign);
      return KBlockSum{_k, high.carry}; // 6502: the `ADC`'s, which `AND` and `ORA` leave alone
    }

    // 6502: MV13 -- LDA S / AND #127 / STA S, then subtract the other way round.
    topByte = static_cast<std::uint8_t>(topByte & 0x7Fu);

    SubResult low = SubtractWithCarry(axis.lo, _k.mid, true);
    _k.mid = low.value;

    SubResult middle = SubtractWithCarry(axis.hi, _k.high, low.carry);
    _k.high = middle.value;

    SubResult high = SubtractWithCarry(static_cast<std::uint8_t>(axis.sgn & 0x7Fu), topByte, middle.carry);
    _k.top = static_cast<std::uint8_t>((high.value | 0x80u) ^ sign);

    if (high.carry)
    {
      return KBlockSum{_k, true}; // 6502: BCS MV14 -- taken means the carry is set by definition
    }

    low = SubtractWithCarry(1, _k.mid, false);
    _k.mid = low.value;

    middle = SubtractWithCarry(0, _k.high, low.carry);
    _k.high = middle.value;

    high = SubtractWithCarry(0, _k.top, middle.carry);
    _k.top = static_cast<std::uint8_t>((high.value & 0x7Fu) | sign);
    return KBlockSum{_k, high.carry};
  }

  SignMag24 AddShipCoordinateToP(const Ship& _work, SignMag24 _value, std::uint8_t _axis) noexcept
  {
    const auto& axis = _work.PositionAt(_axis); // 6502: INWK,X
    // 6502: TAY / EOR INWK+2,X / BMI MV50 -- Y keeps the incoming A, which is what comes back.
    if (((_value.sgn ^ axis.sgn) & 0x80u) == 0u)
    {
      const AddResult low = AddWithCarry(_value.lo, axis.lo, false); // 6502: CLC / ADC
      _value.lo = low.value;

      const AddResult high = AddWithCarry(_value.hi, axis.hi, low.carry);
      _value.hi = high.value;

      return _value; // 6502: TYA / RTS
    }

    // 6502: MV50 -- subtract, and if it goes past zero negate and FLIP THE SIGN that comes back.
    SubResult low = SubtractWithCarry(axis.lo, _value.lo, true);
    _value.lo = low.value;

    SubResult high = SubtractWithCarry(axis.hi, _value.hi, low.carry);
    _value.hi = high.value;

    if (high.carry)
    {
      _value.sgn = static_cast<std::uint8_t>(_value.sgn ^ 0x80u); // 6502: TYA / EOR #128 / RTS
      return _value;
    }

    // 6502: MV51 -- and here the sign is NOT flipped, which is the asymmetry worth noticing.
    low = SubtractWithCarry(1, _value.lo, false);
    _value.lo = low.value;

    high = SubtractWithCarry(0, _value.hi, low.carry);
    _value.hi = high.value;

    return _value;
  }

  void RotateShipVector(Ship& _work, std::uint8_t _y, std::uint8_t _rollRate, std::uint8_t _pitchRate) noexcept
  {
    auto& vector = _work.VectorAt(_y); // 6502: INWK,Y -- the vector the routine was entered with
    // 6502: LDA ALPHA / STA Q ... -- Y = Y - alpha * X, and the subtraction is an EOR #128. The
    // `STX P` after each `MAD` is dead: `MULT1` writes P before it reads it.
    AddSignedResult result = MultiplyAndAdd(static_cast<std::uint8_t>(vector.x.hi ^ 0x80u), _rollRate, vector.y);
    vector.y.hi = result.high;
    vector.y.lo = result.low;

    // 6502: X = X + alpha * Y
    result = MultiplyAndAdd(vector.y.hi, _rollRate, vector.x);
    vector.x.hi = result.high;
    vector.x.lo = result.low;

    // 6502: LDA BETA / STA Q -- Y = Y - beta * Z
    result = MultiplyAndAdd(static_cast<std::uint8_t>(vector.z.hi ^ 0x80u), _pitchRate, vector.y);
    vector.y.hi = result.high;
    vector.y.lo = result.low;

    // 6502: Z = Z + beta * Y
    result = MultiplyAndAdd(vector.y.hi, _pitchRate, vector.z);
    vector.z.hi = result.high;
    vector.z.lo = result.low;
  }

  namespace
  {
    /*
     * One half of MVS5: shrink the value at `_from` and add a sixteenth of the one at `_other`.
     *
     * The two halves of the routine are this with the indices swapped, so it is written once. The
     * only difference between them is an extra sign flip, which is `_flip`.
     */
    [[nodiscard]] AddSignedResult RotateHalf(const Ship& _work, std::uint8_t _from, std::uint8_t _other, std::uint8_t _signMask2,
                                             bool _flip) noexcept
    {
      const auto& from = _work.ComponentAt(_from);   // 6502: INWK,X / INWK+1,X
      const auto& other = _work.ComponentAt(_other); // 6502: INWK,Y / INWK+1,Y
      // 6502: LDA INWK+1,X / AND #127 / LSR A / STA T -- half the magnitude of the high byte...
      const std::uint8_t halfHigh = static_cast<std::uint8_t>((from.hi & 0x7Fu) >> 1);

      // ...taken off the value, which is what keeps the rotation from growing without bound.
      // 6502: STA R ... STA S -- the shrunk value is the (S R) the `ADD` below takes.
      const SubResult low = SubtractWithCarry(from.lo, halfHigh, true);
      const SignMag16 shrunk{low.value, SubtractWithCarry(from.hi, 0, low.carry).value};

      // 6502: LDA INWK,Y / STA P / LDA INWK+1,Y / AND #128 / STA T -- the other value and its sign.
      std::uint8_t p = other.lo;
      std::uint8_t t = static_cast<std::uint8_t>(other.hi & 0x80u);

      // 6502: LSR A / ROR P, four times -- (A P) divided by sixteen, which is the rotation's angle.
      std::uint8_t high = static_cast<std::uint8_t>(other.hi & 0x7Fu);
      for (int shift = 0; shift < 4; ++shift)
      {
        const bool carry = (high & 1u) != 0u;
        high = static_cast<std::uint8_t>(high >> 1);
        p = static_cast<std::uint8_t>((p >> 1) | (carry ? 0x80u : 0u));
      }

      // 6502: ORA T / [EOR #128] / EOR RAT2 -- the sign back on, the half's own flip, the direction.
      std::uint8_t withSign = static_cast<std::uint8_t>(high | t);
      if (_flip)
      {
        withSign = static_cast<std::uint8_t>(withSign ^ 0x80u);
      }
      withSign = static_cast<std::uint8_t>(withSign ^ _signMask2);

      return AddSigned(SignMag16{p, withSign}, shrunk); // 6502: JSR ADD
    }
  } // namespace

  void RotateCoordinatePair(Ship& _work, std::uint8_t _x, std::uint8_t _y, std::uint8_t _signMask2) noexcept
  {
    auto& x = _work.ComponentAt(_x); // 6502: the two components MVS5 rotates into each other
    auto& y = _work.ComponentAt(_y);
    // 6502: JSR ADD / STA K+1 / STX K -- the first half is held in K while the second runs.
    const AddSignedResult first = RotateHalf(_work, _x, _y, _signMask2, false);

    // 6502: the same with X and Y swapped, and the EOR #128 that makes it a rotation.
    const AddSignedResult second = RotateHalf(_work, _y, _x, _signMask2, true);
    y.hi = second.high;
    y.lo = second.low;

    // 6502: LDX Q / LDA K / STA INWK,X / LDA K+1 / STA INWK+1,X -- and only now is X written, so
    // the second half read the value the first half had not yet replaced.
    x.lo = first.low;
    x.hi = first.high;
  }

  std::uint8_t OrientationComponent(const Ship& _work, std::uint8_t _a, std::uint8_t _x, std::uint8_t _y) noexcept
  {
    // 6502: STA P+2 -- the third index is parked in P+2 and read back as Y further down.
    const std::uint8_t divisorAxis = _a;

    // 6502: LDA INWK+10,X / STA Q / LDA INWK+16,X / JSR MULT12 -- (S R) = the first product.
    const Product first = MultiplySigned(_work.ComponentAt(static_cast<std::uint8_t>(SHIP_ROOF_OFFSET + _x)).hi,
                                         _work.ComponentAt(static_cast<std::uint8_t>(SHIP_NOSE_OFFSET + _x)).hi);

    // 6502: LDX INWK+10,Y / STX Q / LDA INWK+16,Y / JSR MAD -- (A X) = the second, plus the first.
    const AddSignedResult sum = MultiplyAndAdd(_work.ComponentAt(static_cast<std::uint8_t>(SHIP_ROOF_OFFSET + _y)).hi,
                                               _work.ComponentAt(static_cast<std::uint8_t>(SHIP_NOSE_OFFSET + _y)).hi, first.Pair());

    // 6502: STX P / LDY P+2 / LDX INWK+10,Y / STX Q / EOR #128, then the fall-through into DVIDT.
    const std::uint8_t divisor = _work.ComponentAt(static_cast<std::uint8_t>(SHIP_NOSE_OFFSET + divisorAxis)).hi;

    return DivideWide(static_cast<std::uint8_t>(sum.high ^ 0x80u), sum.low, divisor).Signed();
  }

  namespace
  {
    /// 6502: LDA INWK+n / STA XX15 ... / JSR NORM / ... -- normalise one of the three vectors in
    /// place. `NORM` works on XX15, so the six bytes go out and the three high ones come back.
    void NormaliseVector(Ship& _work, std::uint8_t _at) noexcept
    {
      auto& components = _work.VectorAt(_at); // 6502: the vector at INWK+n, of which NORM reads the three high bytes
      std::array<std::uint8_t, 3> vector = {components.x.hi, components.y.hi, components.z.hi};
      (void)Normalise(vector);
      components.x.hi = vector[0];
      components.y.hi = vector[1];
      components.z.hi = vector[2];
    }

    /// 6502: AND #&60 -- is this component big enough to divide by? Bits 5 and 6 of the magnitude,
    /// so anything below 32 fails and the routine picks a different axis.
    [[nodiscard]] bool BigEnoughToDivideBy(std::uint8_t _component) noexcept
    {
      return (_component & 0x60u) != 0u;
    }
  } // namespace

  void TidyOrientation(Ship& _work) noexcept
  {
    // 6502: the nose vector, at INWK+10 / +12 / +14.
    NormaliseVector(_work, SHIP_NOSE_OFFSET);

    /*
     * 6502: LDY #4 / LDA XX15 / AND #&60 / BEQ TI1 ... -- recompute ONE component of the roof
     * vector from the other two, dividing by whichever component of the nose vector is largest.
     *
     * The three branches write to INWK+16, +18 or +20 and pass different index triples, and the
     * indices are what select the axes. Y is 4 on the first path because it was loaded before the
     * test; `TI2` reaches its own by `TYA`, which is why the value survives that far.
     */
    if (BigEnoughToDivideBy(_work.nose.x.hi))
    {
      _work.roof.x.hi = OrientationComponent(_work, 0, 2, 4);
    }
    else if (BigEnoughToDivideBy(_work.nose.y.hi))
    {
      // 6502: TI1 -- TAX makes X the 0 the accumulator held, and A is 2.
      _work.roof.y.hi = OrientationComponent(_work, 2, 0, 4);
    }
    else
    {
      // 6502: TI2 -- TYA puts the 4 into A, and Y becomes 2.
      _work.roof.z.hi = OrientationComponent(_work, 4, 0, 2);
    }

    // 6502: TI3 -- the roof vector, now that it has been rebuilt.
    NormaliseVector(_work, SHIP_ROOF_OFFSET);

    /*
     * 6502: the side vector as the CROSS PRODUCT of the other two, one component at a time. Each is
     * `MULT12` then `TIS1` then `EOR #128`, and Q is set ONCE before the first of the three -- so
     * the second and third `MULT12` run on the Q the `TIS1` before them left, which is its X. The
     * port used to reproduce that by calling the routines in the same order on the same workspace;
     * the multiplier each one actually gets is written out here instead.
     */
    Product across = MultiplySigned(_work.roof.z.hi, _work.nose.y.hi); // 6502: LDA INWK+12 / STA Q / LDA INWK+20 / JSR MULT12
    _work.side.x.hi = static_cast<std::uint8_t>(MultiplyAddDivide96(_work.roof.y.hi, _work.nose.z.hi, across.Pair()) ^ 0x80u);

    across = MultiplySigned(_work.roof.x.hi, _work.nose.z.hi); // Q is still TIS1's X, the nose's z
    _work.side.y.hi = static_cast<std::uint8_t>(MultiplyAddDivide96(_work.roof.z.hi, _work.nose.x.hi, across.Pair()) ^ 0x80u);

    across = MultiplySigned(_work.roof.y.hi, _work.nose.x.hi); // and now the nose's x
    _work.side.z.hi = static_cast<std::uint8_t>(MultiplyAddDivide96(_work.roof.x.hi, _work.nose.y.hi, across.Pair()) ^ 0x80u);

    /*
     * 6502: LDA #0 / LDX #14 / .TIL1 STA INWK+9,X / DEX / DEX / BPL TIL1.
     *
     * The LOW bytes of the vectors, zeroed -- the fractional part is what the rounding was
     * accumulating in, and throwing it away is the point of the whole routine.
     *
     * IT STOPS AT INWK+23, not at INWK+25. X counts down in twos from 14 and the loop ends when it
     * goes negative, so the eight bytes at +9, +11, ... +23 are cleared and the ninth at +25 -- the
     * side vector's z_lo -- is left alone. Whether that is an oversight or not, it is what the
     * game does, and the exhaustive sweep is what proves it.
     */
    for (std::uint8_t* const low : {&_work.side.y.lo, &_work.side.x.lo, &_work.roof.z.lo, &_work.roof.y.lo, &_work.roof.x.lo,
                                    &_work.nose.z.lo, &_work.nose.y.lo, &_work.nose.x.lo})
    {
      *low = 0; // and `side.z.lo` is not in the list
    }
  }

  void MovePlanetOrSun(Ship& _work, MathWorkspace& _math, std::uint8_t _rollRate, std::uint8_t _pitchRate) noexcept
  {
    // 6502: LDA ALPHA / EOR #128 / STA Q / LDA INWK / STA P / LDA INWK+1 / STA P+1 / LDA INWK+2 /
    // JSR MULT3 -- K = -alpha * x.
    KBlock moved = MultiplySigned24(_work.x, static_cast<std::uint8_t>(_rollRate ^ 0x80u));
    // Discarded: `MV40` runs a second `MULT3` over this result, so nothing reads the flag (§6.126).
    moved = AddShipCoordinateToK(_work, moved, 3u).value; // 6502: LDX #3 / JSR MVT3 -- K = y - alpha * x

    /*
     * 6502: LDA K+1 / STA K2+1 / STA P ... -- the result parked in K2 while MULT3 refills K.
     *
     * BYTES 1 TO 3 ONLY. `MV40` never writes `K2`, so its bottom byte is whatever the last routine
     * to use the block left there -- the planet or sun drawer's, a frame ago -- and the
     * `LDA K / CLC / ADC K2` below reads it for its carry. That is state which outlives the call on
     * purpose (Modernize.md §4.3, the `K2` row), and the one reason this routine still sees the
     * workspace: the byte is read from it here and nowhere else.
     */
    KBlock parked = moved;
    parked.low = _math.k2Low;

    // 6502: LDA BETA / STA Q / LDA K+3 / JSR MULT3 -- K = beta * K2, the coordinate from K+1 up.
    moved = MultiplySigned24(parked.Coordinate(), _pitchRate);
    moved = AddShipCoordinateToK(_work, moved, 6u).value; // 6502: LDX #6 / JSR MVT3 -- K = z + beta * K2

    // 6502: the new z, and P set up for the multiply that follows.
    _work.z = moved.Coordinate();

    // 6502: LDA K+3 / EOR #128 / JSR MULT3 -- K = -beta * z', with Q still holding beta.
    moved = MultiplySigned24(SignMag24{moved.mid, moved.high, static_cast<std::uint8_t>(moved.top ^ 0x80u)}, _pitchRate);

    // 6502: LDA K+3 / AND #128 / STA T / EOR K2+3 / BMI MV1 -- which way the two blocks point.
    const std::uint8_t sign = static_cast<std::uint8_t>(moved.top & 0x80u);
    std::uint8_t high = 0;

    if (((sign ^ parked.top) & 0x80u) == 0u)
    {
      /*
       * 6502: LDA K / CLC / ADC K2 -- and the result is DISCARDED. Only the carry it produces is
       * wanted, because the answer is stored from K+1 upwards.
       */
      bool carry = AddWithCarry(moved.low, parked.low, false).carry;

      AddResult sum = AddWithCarry(moved.mid, parked.mid, carry);
      _work.y.lo = sum.value;
      carry = sum.carry;

      sum = AddWithCarry(moved.high, parked.high, carry);
      _work.y.hi = sum.value;
      carry = sum.carry;

      high = AddWithCarry(moved.top, parked.top, carry).value;
    }
    else
    {
      // 6502: MV1 -- LDA K / SEC / SBC K2, discarded for its borrow in the same way.
      bool carry = SubtractWithCarry(moved.low, parked.low, true).carry;

      SubResult difference = SubtractWithCarry(moved.mid, parked.mid, carry);
      _work.y.lo = difference.value;
      carry = difference.carry;

      difference = SubtractWithCarry(moved.high, parked.high, carry);
      _work.y.hi = difference.value;
      carry = difference.carry;

      // 6502: LDA K2+3 / AND #127 / STA P / LDA K+3 / AND #127 / SBC P / STA P -- magnitudes only.
      difference = SubtractWithCarry(static_cast<std::uint8_t>(moved.top & 0x7Fu), static_cast<std::uint8_t>(parked.top & 0x7Fu), carry);
      std::uint8_t magnitudeDifference = difference.value;
      high = difference.value;

      if (!difference.carry)
      {
        // 6502: the subtraction went past zero, so negate all three bytes.
        SubResult negated = SubtractWithCarry(1, _work.y.lo, false);
        _work.y.lo = negated.value;

        negated = SubtractWithCarry(0, _work.y.hi, negated.carry);
        _work.y.hi = negated.value;

        negated = SubtractWithCarry(0, magnitudeDifference, negated.carry);
        high = static_cast<std::uint8_t>(negated.value | 0x80u);
      }
    }

    // 6502: MV2 -- EOR T / STA INWK+5, the sign the two blocks agreed on.
    _work.y.sgn = static_cast<std::uint8_t>(high ^ sign);

    // 6502: LDA ALPHA / STA Q ... / JSR MULT3 / LDX #0 / JSR MVT3 -- x = x + alpha * y'.
    moved = MultiplySigned24(_work.y, _rollRate);
    moved = AddShipCoordinateToK(_work, moved, 0u).value; // the flag dies at `MV45` (§6.126)

    _work.x = moved.Coordinate();

    // 6502: LDA ALPHA / STA Q was the last store to `Q` above, and for the SUN -- which `MV45` sends
    // straight back -- it is the frame's Q, the byte the altitude check reads (`EndFlightFrame`).
    _math.lastDivisor = _rollRate;

    // 6502: JMP MV45 -- back into MVEIT's tail, which the caller runs.
  }

  namespace
  {
    /*
     * 6502: MV45 onwards -- the tail both paths through MVEIT join at.
     *
     * `MV40` reaches it with `JMP MV45` and the ordinary path falls into it, so it is written once
     * here rather than duplicated. Everything in it is about the ship's OWN motion: its speed along
     * its own z axis, and its own roll and pitch.
     */
    void MoveShipTail(Canvas& _canvas, Ship& _work, MathWorkspace& _math, FlightState& _flight, std::uint8_t _view,
                      Picture* _picture) noexcept
    {
      // 6502: LDA DELTA / STA R / LDA #128 / LDX #6 / JSR MVT1 -- z -= the player's speed. The 128 is
      // a sign and nothing else, which is why this is the unmasked entry point.
      AddToShipCoordinate(_work, 128, _flight.speed, 6u, false);

      // 6502: LDA TYPE / AND #&81 / CMP #&81 / BNE P%+3 / RTS -- the SUN, and only the sun, stops
      // here. It has no orientation to rotate.
      if ((Byte(_flight.type) & 0x81u) == 0x81u)
      {
        return;
      }

      // 6502: LDY #9 / JSR MVS4, three times -- the nose, roof and side vectors by the player's turn.
      RotateShipVector(_work, 9u, _flight.rollRate, _flight.pitchRate);
      RotateShipVector(_work, 15u, _flight.rollRate, _flight.pitchRate);
      RotateShipVector(_work, 21u, _flight.rollRate, _flight.pitchRate);

      // 6502: `MVS4`'s last `STA Q` is BETA, and nothing below writes `Q` -- so for a ship the loop
      // moves and does not go on to draw, this is the frame's Q, which the altitude check reads
      // (`EndFlightFrame`). The kernel keeps its scratch since M2-b; this one byte is kept for that
      // reader.
      _math.lastDivisor = _flight.pitchRate;

      /*
       * 6502: the ship's own roll, at INWK+30, then its pitch at INWK+29.
       *
       * `CMP #127 / SBC #0` is a DAMPING and reads as one only once you see the carry: the compare
       * sets it when the magnitude is 127, so the subtraction takes nothing off; below that it takes
       * one off every iteration. So a ship at full roll holds it and any other roll decays to zero,
       * which is how a ship straightens up after a turn without anything deciding that it should.
       */
      // 6502: the component offsets MVS5 is handed -- roofv against nosev's x, y and z for the
      // pitch, and against sidev's for the roll.
      const std::array<std::array<std::uint8_t, 3>, 2> vectors = {{{SHIP_NOSE_OFFSET, SHIP_NOSE_OFFSET + 2u, SHIP_NOSE_OFFSET + 4u},
                                                                   {SHIP_SIDE_OFFSET, SHIP_SIDE_OFFSET + 2u, SHIP_SIDE_OFFSET + 4u}}};

      for (int which = 0; which < 2; ++which)
      {
        std::uint8_t& counter = (which == 0) ? _work.pitchCounter : _work.rollCounter; // 6502: INWK+30, then INWK+29
        _flight.signMask2 = static_cast<std::uint8_t>(counter & 0x80u);

        const std::uint8_t magnitude = static_cast<std::uint8_t>(counter & 0x7Fu);
        if (magnitude == 0u)
        {
          continue; // 6502: BEQ MV8 / BEQ MV5 -- no turn, so nothing to apply and nothing to damp
        }

        const SubResult damped = SubtractWithCarry(magnitude, 0, magnitude >= 127u);
        counter = static_cast<std::uint8_t>(damped.value | _flight.signMask2);

        // 6502: LDX #15 / LDY #9 / JSR MVS5, three times over -- the orientation vectors turned
        // against the ship's own roll or pitch.
        RotateCoordinatePair(_work, SHIP_ROOF_OFFSET, vectors[which][0], _flight.signMask2);
        RotateCoordinatePair(_work, SHIP_ROOF_OFFSET + 2u, vectors[which][1], _flight.signMask2);
        RotateCoordinatePair(_work, SHIP_ROOF_OFFSET + 4u, vectors[which][2], _flight.signMask2);
      }

      /*
       * 6502: MV5 -- LDA INWK+31 / AND #&A0 / BNE MVD1 / ORA #16 / STA INWK+31 / JMP SCAN.
       *
       * Bit 4 is "this ship is drawn on the scanner". A live ship sets it and gets scanned AGAIN --
       * the second call of the iteration -- while an exploding one clears it instead and is not.
       */
      if (HasAny(_work.state, ShipStateBit::Killed, ShipStateBit::Exploding))
      {
        _work.state = Without(_work.state, ShipStateBit::OnScanner); // 6502: MVD1
        return;
      }

      _work.state = With(_work.state, ShipStateBit::OnScanner);

      // 6502: JMP SCAN -- a tail call, so it is the last thing done.
      DrawScannerBlip(_canvas, _work, _flight.type, _view, _picture);
    }
  } // namespace

  bool MoveShip(Universe& _universe, Ports& _ports) noexcept
  {
    Ship& work = _universe.work;
    FlightState& flight = _universe.flight;
    const Blueprint& blueprint = *_universe.flight.blueprint;

    // 6502: LDA INWK+31 / AND #&A0 / BNE MV30 -- exploding or already dead, so straight to the
    // scanner. Nothing below moves it, which is why a wreck hangs where it died.
    if (!HasAny(work.state, ShipStateBit::Killed, ShipStateBit::Exploding))
    {
      // 6502: LDA MCNT / EOR XSAV / AND #15 / BNE MV3 / JSR TIDY -- one ship every sixteenth pass.
      if ((static_cast<std::uint8_t>(flight.mainLoopCounter ^ flight.slot) & 15u) == 0u)
      {
        TidyOrientation(work);
      }

      // 6502: MV3 -- LDX TYPE / BPL P%+5 / JMP MV40. The planet and the sun move differently and
      // rejoin at MV45.
      if (IsBody(flight.type))
      {
        MovePlanetOrSun(work, _universe.math, flight.rollRate, flight.pitchRate);
        MoveShipTail(_universe.canvas, work, _universe.math, flight, _universe.view, &_universe.picture);
        return true;
      }

      /*
       * 6502: LDA INWK+32 / BPL MV30 / CPX #MSL / BEQ MV26 / LDA MCNT / EOR XSAV / AND #7 / BNE MV30.
       *
       * A missile thinks on EVERY iteration and everything else on one in eight, which is the whole
       * reason a missile is frightening and a Krait is not.
       */
      if (Has(work.ai, AiBit::Active) &&
          (flight.type == ShipType::Missile || (static_cast<std::uint8_t>(flight.mainLoopCounter ^ flight.slot) & 7u) == 0u))
      {
        // 6502: JSR TACTICS at MV26 -- and it can end in `JMP DEATH`, which does not come back.
        if (!RunTactics(_universe, _ports, flight.slot))
        {
          return false;
        }
      }
    }

DrawScannerBlip(_universe.canvas, work, flight.type, _universe.view, &_universe.picture); // 6502: MV30 -- JSR SCAN

    /*
     * 6502: LDA INWK+27 / ASL A / ASL A / STA Q, then three axes of FMLTU and MVT1-2.
     *
     * The ship's speed, times four, scaling its own nose vector into its position -- so a ship moves
     * along the direction it is pointing, and the multiply is the unsigned high-byte one because
     * only the magnitude matters here. The sign comes from the coordinate byte handed to `MVT1-2`.
     */
    const std::uint8_t speed = static_cast<std::uint8_t>(work.speed << 2);

    // 6502: INWK+10, +12 and +14 -- the nose vector's high bytes -- against x, y and z in turn.
    const auto step = [&work, speed](const std::uint8_t& _high, std::uint8_t _axis) noexcept
    {
      const std::uint8_t along = MultiplyByLog(static_cast<std::uint8_t>(_high & 0x7Fu), speed, false).value; // 6502: STA R
      AddToShipCoordinate(work, _high, along, _axis, true);
    };
    step(work.nose.x.hi, SHIP_X_OFFSET);
    step(work.nose.y.hi, SHIP_Y_OFFSET);
    step(work.nose.z.hi, SHIP_Z_OFFSET);

    /*
     * 6502: LDA INWK+27 / CLC / ADC INWK+28 / BPL P%+4 / LDA #0 / LDY #15 / CMP (XX0),Y / BCC P%+4 /
     * LDA (XX0),Y / STA INWK+27 / LDA #0 / STA INWK+28.
     *
     * Speed plus acceleration, clamped at both ends: a negative result becomes zero and anything
     * above the blueprint's maximum speed becomes that maximum. THEN THE ACCELERATION IS CLEARED --
     * it is a one-shot each iteration, not a persistent force, which is why a ship that stops being
     * pushed stops accelerating immediately rather than coasting up to speed.
     */
    AddResult speedNext = AddWithCarry(work.speed, work.acceleration, false);
    std::uint8_t wanted = ((speedNext.value & 0x80u) != 0u) ? std::uint8_t{0} : speedNext.value;

    const std::uint8_t maximum = blueprint.maxSpeed;
    if (wanted >= maximum)
    {
      wanted = maximum;
    }
    work.speed = wanted;
    work.acceleration = 0;

    /*
     * 6502: the rotation of the ship's POSITION by the player's roll and pitch -- y -= a*x,
     * z += b*K2, y = K2 - b*z, x += a*y -- through `MLTU2` and `MVT6`.
     *
     * `MLTU2-2` is `STX Q` and then `MLTU2`, so the multiplier the ported routine is handed is the
     * same two instructions. `MLTU2` answers with (A P+1 P), and `MVT6` takes the top two bytes of
     * that under a sign and hands back a coordinate; `K2` held the intermediate y while `P` was
     * reused for the next multiply, and both are locals here.
     */
    // 6502: LDX ALP1 / JSR MLTU2-2 -- (A P+1 P) = (~x_lo, x_hi) * alp1, then MVT6 on y.
    Product24 wide = MultiplyWide(work.x.hi, static_cast<std::uint8_t>(work.x.lo ^ 0xFFu), flight.rollMagnitude);
    const SignMag24 rolledY =
      AddShipCoordinateToP(work, SignMag24{wide.mid, wide.high, static_cast<std::uint8_t>(flight.rollSignFlipped ^ work.x.sgn)}, 3u);

    // 6502: LDX BET1 / JSR MLTU2-2 -- and the same on z, with K2's low byte complemented into P.
    wide = MultiplyWide(rolledY.hi, static_cast<std::uint8_t>(rolledY.lo ^ 0xFFu), flight.pitchMagnitude);
    work.z = AddShipCoordinateToP(work, SignMag24{wide.mid, wide.high, static_cast<std::uint8_t>(rolledY.sgn ^ flight.pitchSign)}, 6u);

    // 6502: JSR MLTU2 -- Q is still BET1, and ITS CARRY is what the arithmetic below runs on.
    wide = MultiplyWide(work.z.hi, static_cast<std::uint8_t>(work.z.lo ^ 0xFFu), flight.pitchMagnitude);
    work.y.sgn = rolledY.sgn;

    /*
     * 6502: EOR BET2 / EOR INWK+8 / BPL MV43.
     *
     * `BPL` branches when bit 7 is CLEAR, and what it branches to is the SUBTRACTION -- so the
     * signs agreeing means subtract and disagreeing means add, which is the opposite way round from
     * every other sign test in this file. Reading it the natural way put the ship's y coordinate one
     * out on the first iteration, which is how it was found.
     */
    if (((rolledY.sgn ^ flight.pitchSign ^ work.z.sgn) & 0x80u) != 0u)
    {
      /*
       * 6502: `LDA P+1 / ADC K2+1` with NO `CLC`. It runs on the carry `MLTU2` left, because
       * nothing between them touches it -- `STA`, `LDA` and `EOR` do not.
       */
      AddResult sum = AddWithCarry(wide.mid, rolledY.lo, wide.carry);
      work.y.lo = sum.value;
      sum = AddWithCarry(wide.high, rolledY.hi, sum.carry);
      work.y.hi = sum.value;
    }
    else
    {
      // 6502: MV43 -- `LDA K2+1 / SBC P+1`, and no `SEC` either, for the same reason.
      SubResult difference = SubtractWithCarry(rolledY.lo, wide.mid, wide.carry);
      work.y.lo = difference.value;
      difference = SubtractWithCarry(rolledY.hi, wide.high, difference.carry);
      work.y.hi = difference.value;

      if (!difference.carry)
      {
        SubResult negated = SubtractWithCarry(1, work.y.lo, false);
        work.y.lo = negated.value;
        negated = SubtractWithCarry(0, work.y.hi, negated.carry);
        work.y.hi = negated.value;
        work.y.sgn = static_cast<std::uint8_t>(work.y.sgn ^ 0x80u);
      }
    }

    // 6502: MV44 -- LDX ALP1 / ... / JSR MVT6 -- x = x + alpha * y.
    wide = MultiplyWide(work.y.hi, static_cast<std::uint8_t>(work.y.lo ^ 0xFFu), flight.rollMagnitude);
    work.x = AddShipCoordinateToP(work, SignMag24{wide.mid, wide.high, static_cast<std::uint8_t>(flight.rollSign ^ work.y.sgn)}, 0u);

    MoveShipTail(_universe.canvas, work, _universe.math, flight, _universe.view, &_universe.picture); // 6502: falls into MV45
    return true;
  }

  namespace
  {

    /// 6502: PUS1 -- swap one orientation vector's x and z, flipping a sign on each side. Called for
    /// the nose, roof and side vectors, and the third time by falling into it rather than calling.
    void SwapVectorAxes(Ship& _work, const FlightState& _flight, std::uint8_t _at) noexcept
    {
      Vector16& vector = _work.VectorAt(_at);
      const std::uint8_t low = vector.x.lo;
      vector.x.lo = vector.z.lo;
      vector.z.lo = low;

      const std::uint8_t high = static_cast<std::uint8_t>(vector.x.hi ^ _flight.signMask);
      vector.x.hi = static_cast<std::uint8_t>(vector.z.hi ^ _flight.signMask2);
      vector.z.hi = high;
    }

  } // namespace

  void FlipAxesForView(Ship& _work, FlightState& _flight, std::uint8_t _view) noexcept
  {
    // 6502: BEQ PU2-1 -- the front view needs nothing, and PU2-1 is the RTS at the end of the rear
    // view's block.
    if (_view == 0u)
    {
      return;
    }

    FlipAxes(_work, _flight, _view);
  }

  void FlipAxes(Ship& _work, FlightState& _flight, std::uint8_t _view) noexcept
  {
    // 6502: PU1 -- the DEX comes first, so everything below reads the view MINUS ONE.
    const std::uint8_t which = static_cast<std::uint8_t>(_view - 1u);

    if (which == 0u)
    {
      // The rear view: the ship is behind you, so x and z both point the other way. Eight sign
      // bytes -- the position's x and z, and the x and z of all three orientation vectors.
      for (std::uint8_t* const sign : {&_work.x.sgn, &_work.z.sgn, &_work.nose.x.hi, &_work.nose.z.hi, &_work.roof.x.hi, &_work.roof.z.hi,
                                       &_work.side.x.hi, &_work.side.z.hi})
      {
        *sign = static_cast<std::uint8_t>(*sign ^ 0x80u);
      }
      return;
    }

    // 6502: PU2 -- `LDA #0 / CPX #2 / ROR A` puts the comparison's carry into bit 7 of a zero, so
    // RAT2 is the sign mask for the right view and RAT for the left. One instruction less than an
    // `if`, and the reason the two masks are always each other's complement.
    _flight.signMask2 = (which >= 2u) ? std::uint8_t{0x80} : std::uint8_t{0x00};
    _flight.signMask = static_cast<std::uint8_t>(_flight.signMask2 ^ 0x80u);

    // The position, whose low and high bytes swap plainly and whose signs swap with a flip.
    std::swap(_work.x.lo, _work.z.lo);
    std::swap(_work.x.hi, _work.z.hi);

    const std::uint8_t sign = static_cast<std::uint8_t>(_work.x.sgn ^ _flight.signMask);
    _work.x.sgn = static_cast<std::uint8_t>(_work.z.sgn ^ _flight.signMask2);
    _work.z.sgn = sign;

    SwapVectorAxes(_work, _flight, 9);
    SwapVectorAxes(_work, _flight, 15);
    SwapVectorAxes(_work, _flight, 21);
  }

} // namespace Elite
