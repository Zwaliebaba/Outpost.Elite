#include "pch.h"

#include "ShipMove.h"

#include "Scanner.h"
#include "ShipBlueprint.h"

#include <utility>

#include <array>

namespace Elite
{

  void AddToShipCoordinate(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a, std::uint8_t _x, bool _maskSign) noexcept
  {
    // 6502: AND #128 -- the two-bytes-earlier entry point, and the only difference between them.
    std::uint8_t accumulator = _maskSign ? static_cast<std::uint8_t>(_a & 0x80u) : _a;

    /*
     * 6502: ASL A / STA S / LDA #0 / ROR A / STA T / LSR S.
     *
     * S ends as the magnitude of the high byte and T as its sign, and the `LSR` leaves the carry
     * CLEAR -- bit 0 of something just shifted left is always zero -- which is what the addition
     * below runs on.
     */
    _math.s = static_cast<std::uint8_t>(accumulator << 1);
    _math.t = static_cast<std::uint8_t>((accumulator & 0x80u) != 0u ? 0x80u : 0x00u);
    _math.s = static_cast<std::uint8_t>(_math.s >> 1);
    bool carry = false;

    // 6502: EOR INWK+2,X / BMI MV10 -- A still holds T, so this compares the two signs.
    if (((_math.t ^ _work[_x + 2u]) & 0x80u) == 0u)
    {
      // Same sign: add the magnitudes and keep the sign.
      const AddResult low = AddWithCarry(_math.r, _work[_x], carry);
      _work[_x] = low.value;

      const AddResult middle = AddWithCarry(_math.s, _work[_x + 1u], low.carry);
      _work[_x + 1u] = middle.value;

      const AddResult high = AddWithCarry(_work[_x + 2u], 0, middle.carry);
      _work[_x + 2u] = static_cast<std::uint8_t>(high.value | _math.t);
      return;
    }

    // 6502: MV10 -- opposite signs, so subtract, and the answer may come out the other way round.
    SubResult low = SubtractWithCarry(_work[_x], _math.r, true);
    _work[_x] = low.value;

    SubResult middle = SubtractWithCarry(_work[_x + 1u], _math.s, low.carry);
    _work[_x + 1u] = middle.value;

    SubResult high = SubtractWithCarry(static_cast<std::uint8_t>(_work[_x + 2u] & 0x7Fu), 0, middle.carry);
    _work[_x + 2u] = static_cast<std::uint8_t>((high.value | 0x80u) ^ _math.t);

    if (high.carry)
    {
      return; // 6502: BCS MV11
    }

    /*
     * 6502: LDA #1 / SBC INWK,X ... -- the subtraction went past zero, so negate what came out.
     * The carry is clear here (the branch above was not taken), which is what makes `1 - n - 1`
     * the two's complement of n.
     */
    low = SubtractWithCarry(1, _work[_x], false);
    _work[_x] = low.value;

    middle = SubtractWithCarry(0, _work[_x + 1u], low.carry);
    _work[_x + 1u] = middle.value;

    high = SubtractWithCarry(0, _work[_x + 2u], middle.carry);
    _work[_x + 2u] = static_cast<std::uint8_t>((high.value & 0x7Fu) | _math.t);
  }

  void AddShipCoordinateToK(const ShipBlock& _work, MathWorkspace& _math, std::uint8_t _x) noexcept
  {
    // 6502: LDA K+3 / STA S / AND #128 / STA T / EOR INWK+2,X / BMI MV13.
    _math.s = _math.k[3];
    _math.t = static_cast<std::uint8_t>(_math.k[3] & 0x80u);

    if (((_math.t ^ _work[_x + 2u]) & 0x80u) == 0u)
    {
      // 6502: LDA K+1 / CLC / ADC INWK,X ... -- an explicit CLC here, unlike MVT1's LSR.
      const AddResult low = AddWithCarry(_math.k[1], _work[_x], false);
      _math.k[1] = low.value;

      const AddResult middle = AddWithCarry(_math.k[2], _work[_x + 1u], low.carry);
      _math.k[2] = middle.value;

      const AddResult high = AddWithCarry(_math.k[3], _work[_x + 2u], middle.carry);
      _math.k[3] = static_cast<std::uint8_t>((high.value & 0x7Fu) | _math.t);
      return;
    }

    // 6502: MV13 -- LDA S / AND #127 / STA S, then subtract the other way round.
    _math.s = static_cast<std::uint8_t>(_math.s & 0x7Fu);

    SubResult low = SubtractWithCarry(_work[_x], _math.k[1], true);
    _math.k[1] = low.value;

    SubResult middle = SubtractWithCarry(_work[_x + 1u], _math.k[2], low.carry);
    _math.k[2] = middle.value;

    SubResult high = SubtractWithCarry(static_cast<std::uint8_t>(_work[_x + 2u] & 0x7Fu), _math.s, middle.carry);
    _math.k[3] = static_cast<std::uint8_t>((high.value | 0x80u) ^ _math.t);

    if (high.carry)
    {
      return; // 6502: BCS MV14
    }

    low = SubtractWithCarry(1, _math.k[1], false);
    _math.k[1] = low.value;

    middle = SubtractWithCarry(0, _math.k[2], low.carry);
    _math.k[2] = middle.value;

    high = SubtractWithCarry(0, _math.k[3], middle.carry);
    _math.k[3] = static_cast<std::uint8_t>((high.value & 0x7Fu) | _math.t);
  }

  std::uint8_t AddShipCoordinateToP(const ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a, std::uint8_t _x) noexcept
  {
    // 6502: TAY / EOR INWK+2,X / BMI MV50 -- Y keeps the incoming A, which is what comes back.
    if (((_a ^ _work[_x + 2u]) & 0x80u) == 0u)
    {
      const AddResult low = AddWithCarry(_math.p1, _work[_x], false); // 6502: CLC / ADC
      _math.p1 = low.value;

      const AddResult high = AddWithCarry(_math.p2, _work[_x + 1u], low.carry);
      _math.p2 = high.value;

      return _a; // 6502: TYA / RTS
    }

    // 6502: MV50 -- subtract, and if it goes past zero negate and FLIP THE SIGN that comes back.
    SubResult low = SubtractWithCarry(_work[_x], _math.p1, true);
    _math.p1 = low.value;

    SubResult high = SubtractWithCarry(_work[_x + 1u], _math.p2, low.carry);
    _math.p2 = high.value;

    if (high.carry)
    {
      return static_cast<std::uint8_t>(_a ^ 0x80u); // 6502: TYA / EOR #128 / RTS
    }

    // 6502: MV51 -- and here the sign is NOT flipped, which is the asymmetry worth noticing.
    low = SubtractWithCarry(1, _math.p1, false);
    _math.p1 = low.value;

    high = SubtractWithCarry(0, _math.p2, low.carry);
    _math.p2 = high.value;

    return _a;
  }

  void RotateShipVector(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _y, std::uint8_t _alpha, std::uint8_t _beta) noexcept
  {
    // 6502: LDA ALPHA / STA Q ... -- Y = Y - alpha * X, and the subtraction is an EOR #128.
    _math.q = _alpha;
    _math.r = _work[_y + 2u];
    _math.s = _work[_y + 3u];
    _math.p = _work[_y];
    AddSignedResult result = MultiplyAndAdd(_math, static_cast<std::uint8_t>(_work[_y + 1u] ^ 0x80u));
    _work[_y + 3u] = result.high;
    _work[_y + 2u] = result.low;
    _math.p = result.low; // 6502: STX P

    // 6502: X = X + alpha * Y
    _math.r = _work[_y];
    _math.s = _work[_y + 1u];
    result = MultiplyAndAdd(_math, _work[_y + 3u]);
    _work[_y + 1u] = result.high;
    _work[_y] = result.low;
    _math.p = result.low;

    // 6502: LDA BETA / STA Q -- Y = Y - beta * Z
    _math.q = _beta;
    _math.r = _work[_y + 2u];
    _math.s = _work[_y + 3u];
    _math.p = _work[_y + 4u];
    result = MultiplyAndAdd(_math, static_cast<std::uint8_t>(_work[_y + 5u] ^ 0x80u));
    _work[_y + 3u] = result.high;
    _work[_y + 2u] = result.low;
    _math.p = result.low;

    // 6502: Z = Z + beta * Y
    _math.r = _work[_y + 4u];
    _math.s = _work[_y + 5u];
    result = MultiplyAndAdd(_math, _work[_y + 3u]);
    _work[_y + 5u] = result.high;
    _work[_y + 4u] = result.low;
  }

  namespace
  {
    /*
     * One half of MVS5: shrink the value at `_from` and add a sixteenth of the one at `_other`.
     *
     * The two halves of the routine are this with the indices swapped, so it is written once. The
     * only difference between them is an extra sign flip, which is `_flip`.
     */
    [[nodiscard]] AddSignedResult RotateHalf(const ShipBlock& _work, MathWorkspace& _math, std::uint8_t _from, std::uint8_t _other,
                                             std::uint8_t _rat2, bool _flip) noexcept
    {
      // 6502: LDA INWK+1,X / AND #127 / LSR A / STA T -- half the magnitude of the high byte...
      _math.t = static_cast<std::uint8_t>((_work[_from + 1u] & 0x7Fu) >> 1);

      // ...taken off the value, which is what keeps the rotation from growing without bound.
      const SubResult low = SubtractWithCarry(_work[_from], _math.t, true);
      _math.r = low.value;
      _math.s = SubtractWithCarry(_work[_from + 1u], 0, low.carry).value;

      // 6502: LDA INWK,Y / STA P / LDA INWK+1,Y / AND #128 / STA T -- the other value and its sign.
      _math.p = _work[_other];
      _math.t = static_cast<std::uint8_t>(_work[_other + 1u] & 0x80u);

      // 6502: LSR A / ROR P, four times -- (A P) divided by sixteen, which is the rotation's angle.
      std::uint8_t high = static_cast<std::uint8_t>(_work[_other + 1u] & 0x7Fu);
      for (int shift = 0; shift < 4; ++shift)
      {
        const bool carry = (high & 1u) != 0u;
        high = static_cast<std::uint8_t>(high >> 1);
        _math.p = static_cast<std::uint8_t>((_math.p >> 1) | (carry ? 0x80u : 0u));
      }

      // 6502: ORA T / [EOR #128] / EOR RAT2 -- the sign back on, the half's own flip, the direction.
      std::uint8_t signed_ = static_cast<std::uint8_t>(high | _math.t);
      if (_flip)
      {
        signed_ = static_cast<std::uint8_t>(signed_ ^ 0x80u);
      }
      signed_ = static_cast<std::uint8_t>(signed_ ^ _rat2);

      return AddSigned(_math, signed_); // 6502: JSR ADD
    }
  } // namespace

  void RotateCoordinatePair(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _x, std::uint8_t _y, std::uint8_t _rat2) noexcept
  {
    // 6502: JSR ADD / STA K+1 / STX K -- the first half is held in K while the second runs.
    const AddSignedResult first = RotateHalf(_work, _math, _x, _y, _rat2, false);
    _math.k[1] = first.high;
    _math.k[0] = first.low;

    // 6502: the same with X and Y swapped, and the EOR #128 that makes it a rotation.
    const AddSignedResult second = RotateHalf(_work, _math, _y, _x, _rat2, true);
    _work[_y + 1u] = second.high;
    _work[_y] = second.low;

    // 6502: LDX Q / LDA K / STA INWK,X / LDA K+1 / STA INWK+1,X -- and only now is X written, so
    // the second half read the value the first half had not yet replaced.
    _work[_x] = _math.k[0];
    _work[_x + 1u] = _math.k[1];
  }

  std::uint8_t OrientationComponent(const ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a, std::uint8_t _x,
                                    std::uint8_t _y) noexcept
  {
    // 6502: STA P+2 -- the third index is parked in P+2 and read back as Y further down.
    _math.p2 = _a;

    // 6502: LDA INWK+10,X / STA Q / LDA INWK+16,X / JSR MULT12 -- (S R) = the first product.
    _math.q = _work[10u + _x];
    MultiplySignedToSR(_math, _work[16u + _x]);

    // 6502: LDX INWK+10,Y / STX Q / LDA INWK+16,Y / JSR MAD -- (A X) = the second, plus the first.
    _math.q = _work[10u + _y];
    const AddSignedResult sum = MultiplyAndAdd(_math, _work[16u + _y]);

    // 6502: STX P / LDY P+2 / LDX INWK+10,Y / STX Q / EOR #128, then the fall-through into DVIDT.
    _math.p = sum.low;
    _math.q = _work[10u + _math.p2];

    return DivideWide(_math, static_cast<std::uint8_t>(sum.high ^ 0x80u));
  }

  namespace
  {
    /// 6502: LDA INWK+n / STA XX15 ... / JSR NORM / ... -- normalise one of the three vectors in
    /// place. `NORM` works on XX15, so the six bytes go out and the three high ones come back.
    void NormaliseVector(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _at) noexcept
    {
      std::array<std::uint8_t, 3> vector = {_work[_at], _work[_at + 2u], _work[_at + 4u]};
      Normalise(_math, vector);
      _work[_at] = vector[0];
      _work[_at + 2u] = vector[1];
      _work[_at + 4u] = vector[2];
    }

    /// 6502: AND #&60 -- is this component big enough to divide by? Bits 5 and 6 of the magnitude,
    /// so anything below 32 fails and the routine picks a different axis.
    [[nodiscard]] bool BigEnoughToDivideBy(std::uint8_t _component) noexcept
    {
      return (_component & 0x60u) != 0u;
    }
  } // namespace

  void TidyOrientation(ShipBlock& _work, MathWorkspace& _math) noexcept
  {
    // 6502: the nose vector, at INWK+10 / +12 / +14.
    NormaliseVector(_work, _math, 10u);

    /*
     * 6502: LDY #4 / LDA XX15 / AND #&60 / BEQ TI1 ... -- recompute ONE component of the roof
     * vector from the other two, dividing by whichever component of the nose vector is largest.
     *
     * The three branches write to INWK+16, +18 or +20 and pass different index triples, and the
     * indices are what select the axes. Y is 4 on the first path because it was loaded before the
     * test; `TI2` reaches its own by `TYA`, which is why the value survives that far.
     */
    if (BigEnoughToDivideBy(_work[10u]))
    {
      _work[16u] = OrientationComponent(_work, _math, 0, 2, 4);
    }
    else if (BigEnoughToDivideBy(_work[12u]))
    {
      // 6502: TI1 -- TAX makes X the 0 the accumulator held, and A is 2.
      _work[18u] = OrientationComponent(_work, _math, 2, 0, 4);
    }
    else
    {
      // 6502: TI2 -- TYA puts the 4 into A, and Y becomes 2.
      _work[20u] = OrientationComponent(_work, _math, 4, 0, 2);
    }

    // 6502: TI3 -- the roof vector, now that it has been rebuilt.
    NormaliseVector(_work, _math, 16u);

    /*
     * 6502: the side vector as the CROSS PRODUCT of the other two, one component at a time. Each is
     * `MULT12` then `TIS1` then `EOR #128`, and Q is set ONCE before the first of the three -- so
     * the second and third run on whatever Q the routines before them left, which is reproduced by
     * calling them in the same order on the same workspace rather than by reasoning about it.
     */
    _math.q = _work[12u];
    MultiplySignedToSR(_math, _work[20u]);
    _work[22u] = static_cast<std::uint8_t>(MultiplyAddDivide96(_math, _work[18u], _work[14u]) ^ 0x80u);

    MultiplySignedToSR(_math, _work[16u]);
    _work[24u] = static_cast<std::uint8_t>(MultiplyAddDivide96(_math, _work[20u], _work[10u]) ^ 0x80u);

    MultiplySignedToSR(_math, _work[18u]);
    _work[26u] = static_cast<std::uint8_t>(MultiplyAddDivide96(_math, _work[16u], _work[12u]) ^ 0x80u);

    /*
     * 6502: LDA #0 / LDX #14 / .TIL1 STA INWK+9,X / DEX / DEX / BPL TIL1.
     *
     * The LOW bytes of the vectors, zeroed -- the fractional part is what the rounding was
     * accumulating in, and throwing it away is the point of the whole routine.
     *
     * IT STOPS AT INWK+23, not at INWK+25. X counts down in twos from 14 and the loop ends when it
     * goes negative, which is eight stores covering the nose and roof vectors and only the first
     * two thirds of the side one. INWK+25 keeps whatever it had. That looks like an off-by-one and
     * is reproduced rather than tidied, because the oracle says it is what the game does.
     */
    for (int offset = 14; offset >= 0; offset -= 2)
    {
      _work[9u + static_cast<std::uint8_t>(offset)] = 0;
    }
  }

  void MovePlanetOrSun(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _alpha, std::uint8_t _beta) noexcept
  {
    // 6502: LDA ALPHA / EOR #128 / STA Q -- the player's roll, applied the other way round.
    _math.q = static_cast<std::uint8_t>(_alpha ^ 0x80u);
    _math.p = _work[0];
    _math.p1 = _work[1];
    MultiplySignedToK(_math, _work[2]);     // 6502: JSR MULT3 -- K = -alpha * x
    AddShipCoordinateToK(_work, _math, 3u); // 6502: LDX #3 / JSR MVT3 -- K = y - alpha * x

    // 6502: LDA K+1 / STA K2+1 / STA P ... -- the result parked in K2 while MULT3 refills K.
    _math.k2[1] = _math.k[1];
    _math.p = _math.k[1];
    _math.k2[2] = _math.k[2];
    _math.p1 = _math.k[2];
    _math.q = _beta;
    _math.k2[3] = _math.k[3];

    MultiplySignedToK(_math, _math.k[3]);   // 6502: JSR MULT3 -- K = beta * K2
    AddShipCoordinateToK(_work, _math, 6u); // 6502: LDX #6 / JSR MVT3 -- K = z + beta * K2

    // 6502: the new z, and P set up for the multiply that follows.
    _math.p = _math.k[1];
    _work[6] = _math.k[1];
    _math.p1 = _math.k[2];
    _work[7] = _math.k[2];
    _work[8] = _math.k[3];

    // 6502: EOR #128 / JSR MULT3 -- K = -beta * z', with Q still holding beta.
    MultiplySignedToK(_math, static_cast<std::uint8_t>(_math.k[3] ^ 0x80u));

    // 6502: LDA K+3 / AND #128 / STA T / EOR K2+3 / BMI MV1 -- which way the two blocks point.
    _math.t = static_cast<std::uint8_t>(_math.k[3] & 0x80u);
    std::uint8_t high = 0;

    if (((_math.t ^ _math.k2[3]) & 0x80u) == 0u)
    {
      /*
       * 6502: LDA K / CLC / ADC K2 -- and the result is DISCARDED. Only the carry it produces is
       * wanted, because the answer is stored from K+1 upwards.
       */
      bool carry = AddWithCarry(_math.k[0], _math.k2[0], false).carry;

      AddResult sum = AddWithCarry(_math.k[1], _math.k2[1], carry);
      _work[3] = sum.value;
      carry = sum.carry;

      sum = AddWithCarry(_math.k[2], _math.k2[2], carry);
      _work[4] = sum.value;
      carry = sum.carry;

      high = AddWithCarry(_math.k[3], _math.k2[3], carry).value;
    }
    else
    {
      // 6502: MV1 -- LDA K / SEC / SBC K2, discarded for its borrow in the same way.
      bool carry = SubtractWithCarry(_math.k[0], _math.k2[0], true).carry;

      SubResult difference = SubtractWithCarry(_math.k[1], _math.k2[1], carry);
      _work[3] = difference.value;
      carry = difference.carry;

      difference = SubtractWithCarry(_math.k[2], _math.k2[2], carry);
      _work[4] = difference.value;
      carry = difference.carry;

      // 6502: LDA K2+3 / AND #127 / STA P / LDA K+3 / AND #127 / SBC P / STA P -- magnitudes only.
      _math.p = static_cast<std::uint8_t>(_math.k2[3] & 0x7Fu);
      difference = SubtractWithCarry(static_cast<std::uint8_t>(_math.k[3] & 0x7Fu), _math.p, carry);
      _math.p = difference.value;
      high = difference.value;

      if (!difference.carry)
      {
        // 6502: the subtraction went past zero, so negate all three bytes.
        SubResult negated = SubtractWithCarry(1, _work[3], false);
        _work[3] = negated.value;

        negated = SubtractWithCarry(0, _work[4], negated.carry);
        _work[4] = negated.value;

        negated = SubtractWithCarry(0, _math.p, negated.carry);
        high = static_cast<std::uint8_t>(negated.value | 0x80u);
      }
    }

    // 6502: MV2 -- EOR T / STA INWK+5, the sign the two blocks agreed on.
    _work[5] = static_cast<std::uint8_t>(high ^ _math.t);

    // 6502: LDA ALPHA / STA Q ... / JSR MULT3 / LDX #0 / JSR MVT3 -- x = x + alpha * y'.
    _math.q = _alpha;
    _math.p = _work[3];
    _math.p1 = _work[4];
    MultiplySignedToK(_math, _work[5]);
    AddShipCoordinateToK(_work, _math, 0u);

    _work[0] = _math.k[1];
    _work[1] = _math.k[2];
    _work[2] = _math.k[3];

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
    void MoveShipTail(Canvas& _canvas, DrawWorkspace& _draw, ShipBlock& _work, MathWorkspace& _math, FlightState& _flight,
                      std::uint8_t _view) noexcept
    {
      // 6502: LDA DELTA / STA R / LDA #128 / LDX #6 / JSR MVT1 -- z -= the player's speed. The 128 is
      // a sign and nothing else, which is why this is the unmasked entry point.
      _math.r = _flight.delta;
      AddToShipCoordinate(_work, _math, 128, 6u, false);

      // 6502: LDA TYPE / AND #&81 / CMP #&81 / BNE P%+3 / RTS -- the SUN, and only the sun, stops
      // here. It has no orientation to rotate.
      if ((_flight.type & 0x81u) == 0x81u)
      {
        return;
      }

      // 6502: LDY #9 / JSR MVS4, three times -- the nose, roof and side vectors by the player's turn.
      RotateShipVector(_work, _math, 9u, _flight.alpha, _flight.beta);
      RotateShipVector(_work, _math, 15u, _flight.alpha, _flight.beta);
      RotateShipVector(_work, _math, 21u, _flight.alpha, _flight.beta);

      /*
       * 6502: the ship's own roll, at INWK+30, then its pitch at INWK+29.
       *
       * `CMP #127 / SBC #0` is a DAMPING and reads as one only once you see the carry: the compare
       * sets it when the magnitude is 127, so the subtraction takes nothing off; below that it takes
       * one off every iteration. So a ship at full roll holds it and any other roll decays to zero,
       * which is how a ship straightens up after a turn without anything deciding that it should.
       */
      const std::uint8_t ROLL_AND_PITCH[2] = {30u, 29u};
      const std::uint8_t VECTORS[2][3] = {{9u, 11u, 13u}, {21u, 23u, 25u}};

      for (int which = 0; which < 2; ++which)
      {
        const std::uint8_t at = ROLL_AND_PITCH[which];
        _flight.rat2 = static_cast<std::uint8_t>(_work[at] & 0x80u);

        const std::uint8_t magnitude = static_cast<std::uint8_t>(_work[at] & 0x7Fu);
        if (magnitude == 0u)
        {
          continue; // 6502: BEQ MV8 / BEQ MV5 -- no turn, so nothing to apply and nothing to damp
        }

        const SubResult damped = SubtractWithCarry(magnitude, 0, magnitude >= 127u);
        _work[at] = static_cast<std::uint8_t>(damped.value | _flight.rat2);

        // 6502: LDX #15 / LDY #9 / JSR MVS5, three times over -- the orientation vectors turned
        // against the ship's own roll or pitch.
        RotateCoordinatePair(_work, _math, 15u, VECTORS[which][0], _flight.rat2);
        RotateCoordinatePair(_work, _math, 17u, VECTORS[which][1], _flight.rat2);
        RotateCoordinatePair(_work, _math, 19u, VECTORS[which][2], _flight.rat2);
      }

      /*
       * 6502: MV5 -- LDA INWK+31 / AND #&A0 / BNE MVD1 / ORA #16 / STA INWK+31 / JMP SCAN.
       *
       * Bit 4 is "this ship is drawn on the scanner". A live ship sets it and gets scanned AGAIN --
       * the second call of the iteration -- while an exploding one clears it instead and is not.
       */
      if ((_work[31] & 0xA0u) != 0u)
      {
        _work[31] = static_cast<std::uint8_t>(_work[31] & 0xEFu); // 6502: MVD1
        return;
      }

      _work[31] = static_cast<std::uint8_t>(_work[31] | 0x10u);

      // 6502: JMP SCAN -- a tail call, so it is the last thing done.
      DrawScannerBlip(_canvas, _draw, _work, _flight.type, _view);
    }
  } // namespace

  bool MoveShip(Canvas& _canvas, DrawWorkspace& _draw, ShipBlock& _work, MathWorkspace& _math, FlightState& _flight, ShipEffects& _effects,
                std::uint16_t _blueprint, std::uint8_t _view) noexcept
  {
    // 6502: LDA INWK+31 / AND #&A0 / BNE MV30 -- exploding or already dead, so straight to the
    // scanner. Nothing below moves it, which is why a wreck hangs where it died.
    if ((_work[31] & 0xA0u) == 0u)
    {
      // 6502: LDA MCNT / EOR XSAV / AND #15 / BNE MV3 / JSR TIDY -- one ship every sixteenth pass.
      if ((static_cast<std::uint8_t>(_flight.mainLoopCounter ^ _flight.slot) & 15u) == 0u)
      {
        TidyOrientation(_work, _math);
      }

      // 6502: MV3 -- LDX TYPE / BPL P%+5 / JMP MV40. The planet and the sun move differently and
      // rejoin at MV45.
      if ((_flight.type & 0x80u) != 0u)
      {
        MovePlanetOrSun(_work, _math, _flight.alpha, _flight.beta);
        MoveShipTail(_canvas, _draw, _work, _math, _flight, _view);
        return true;
      }

      /*
       * 6502: LDA INWK+32 / BPL MV30 / CPX #MSL / BEQ MV26 / LDA MCNT / EOR XSAV / AND #7 / BNE MV30.
       *
       * A missile thinks on EVERY iteration and everything else on one in eight, which is the whole
       * reason a missile is frightening and a Krait is not.
       */
      if ((_work[32] & 0x80u) != 0u &&
          (_flight.type == SHIP_TYPE_MISSILE || (static_cast<std::uint8_t>(_flight.mainLoopCounter ^ _flight.slot) & 7u) == 0u))
      {
        // 6502: JSR TACTICS at MV26 -- and it can end in `JMP DEATH`, which does not come back.
        if (!_effects.RunTactics(_work))
        {
          return false;
        }
      }
    }

    DrawScannerBlip(_canvas, _draw, _work, _flight.type, _view); // 6502: MV30 -- JSR SCAN

    /*
     * 6502: LDA INWK+27 / ASL A / ASL A / STA Q, then three axes of FMLTU and MVT1-2.
     *
     * The ship's speed, times four, scaling its own nose vector into its position -- so a ship moves
     * along the direction it is pointing, and the multiply is the unsigned high-byte one because
     * only the magnitude matters here. The sign comes from the coordinate byte handed to `MVT1-2`.
     */
    _math.q = static_cast<std::uint8_t>(_work[27] << 2);

    const std::uint8_t AXES[3][2] = {{10u, 0u}, {12u, 3u}, {14u, 6u}};
    for (const auto& axis : AXES)
    {
      _math.r = MultiplyByLog(_math, static_cast<std::uint8_t>(_work[axis[0]] & 0x7Fu), false).high;
      AddToShipCoordinate(_work, _math, _work[axis[0]], axis[1], true);
    }

    /*
     * 6502: LDA INWK+27 / CLC / ADC INWK+28 / BPL P%+4 / LDA #0 / LDY #15 / CMP (XX0),Y / BCC P%+4 /
     * LDA (XX0),Y / STA INWK+27 / LDA #0 / STA INWK+28.
     *
     * Speed plus acceleration, clamped at both ends: a negative result becomes zero and anything
     * above the blueprint's maximum speed becomes that maximum. THEN THE ACCELERATION IS CLEARED --
     * it is a one-shot each iteration, not a persistent force, which is why a ship that stops being
     * pushed stops accelerating immediately rather than coasting up to speed.
     */
    AddResult speed = AddWithCarry(_work[27], _work[28], false);
    std::uint8_t wanted = ((speed.value & 0x80u) != 0u) ? std::uint8_t{0} : speed.value;

    const std::uint8_t maximum = ShipByte(static_cast<std::uint16_t>(_blueprint + 15u));
    if (wanted >= maximum)
    {
      wanted = maximum;
    }
    _work[27] = wanted;
    _work[28] = 0;

    /*
     * 6502: the rotation of the ship's POSITION by the player's roll and pitch -- y -= a*x,
     * z += b*K2, y = K2 - b*z, x += a*y -- through `MLTU2` and `MVT6`.
     *
     * `MLTU2-2` is `STX Q` and then `MLTU2`, so setting Q and calling the ported routine is the
     * same two instructions. `K2` holds the intermediate y while `P` is reused for the next
     * multiply, which is the second place in this slice that needs both blocks at once.
     */
    _math.q = _flight.alp1; // 6502: LDX ALP1 / JSR MLTU2-2
    _math.p = static_cast<std::uint8_t>(_work[0] ^ 0xFFu);
    _math.p2 = MultiplyWide(_math, _work[1]).high;
    _math.k2[3] = AddShipCoordinateToP(_work, _math, static_cast<std::uint8_t>(_flight.alp2Next ^ _work[2]), 3u);

    _math.k2[1] = _math.p1;
    _math.p = static_cast<std::uint8_t>(_math.p1 ^ 0xFFu);
    _math.k2[2] = _math.p2;

    _math.q = _flight.bet1; // 6502: LDX BET1 / JSR MLTU2-2
    _math.p2 = MultiplyWide(_math, _math.p2).high;
    _work[8] = AddShipCoordinateToP(_work, _math, static_cast<std::uint8_t>(_math.k2[3] ^ _flight.bet2), 6u);
    _work[6] = _math.p1;
    _math.p = static_cast<std::uint8_t>(_math.p1 ^ 0xFFu);
    _work[7] = _math.p2;

    // 6502: JSR MLTU2 -- Q is still BET1, and ITS CARRY is what the arithmetic below runs on.
    const WideResult wide = MultiplyWide(_math, _math.p2);
    _math.p2 = wide.high;
    _work[5] = _math.k2[3];

    /*
     * 6502: EOR BET2 / EOR INWK+8 / BPL MV43.
     *
     * `BPL` branches when bit 7 is CLEAR, and what it branches to is the SUBTRACTION -- so the
     * signs agreeing means subtract and disagreeing means add, which is the opposite way round from
     * every other sign test in this file. Reading it the natural way put the ship's y coordinate one
     * out on the first iteration, which is how it was found.
     */
    if (((_math.k2[3] ^ _flight.bet2 ^ _work[8]) & 0x80u) != 0u)
    {
      /*
       * 6502: `LDA P+1 / ADC K2+1` with NO `CLC`. It runs on the carry `MLTU2` left, because
       * nothing between them touches it -- `STA`, `LDA` and `EOR` do not.
       */
      AddResult sum = AddWithCarry(_math.p1, _math.k2[1], wide.carry);
      _work[3] = sum.value;
      sum = AddWithCarry(_math.p2, _math.k2[2], sum.carry);
      _work[4] = sum.value;
    }
    else
    {
      // 6502: MV43 -- `LDA K2+1 / SBC P+1`, and no `SEC` either, for the same reason.
      SubResult difference = SubtractWithCarry(_math.k2[1], _math.p1, wide.carry);
      _work[3] = difference.value;
      difference = SubtractWithCarry(_math.k2[2], _math.p2, difference.carry);
      _work[4] = difference.value;

      if (!difference.carry)
      {
        SubResult negated = SubtractWithCarry(1, _work[3], false);
        _work[3] = negated.value;
        negated = SubtractWithCarry(0, _work[4], negated.carry);
        _work[4] = negated.value;
        _work[5] = static_cast<std::uint8_t>(_work[5] ^ 0x80u);
      }
    }

    // 6502: MV44 -- LDX ALP1 / ... / JSR MVT6 -- x = x + alpha * y.
    _math.q = _flight.alp1;
    _math.p = static_cast<std::uint8_t>(_work[3] ^ 0xFFu);
    _math.p2 = MultiplyWide(_math, _work[4]).high;
    _work[2] = AddShipCoordinateToP(_work, _math, static_cast<std::uint8_t>(_flight.alp2 ^ _work[5]), 0u);
    _work[1] = _math.p2;
    _work[0] = _math.p1;

    MoveShipTail(_canvas, _draw, _work, _math, _flight, _view); // 6502: falls into MV45
    return true;
  }

  namespace
  {

    /// 6502: PUS1 -- swap one orientation vector's x and z, flipping a sign on each side. Called for
    /// the nose, roof and side vectors, and the third time by falling into it rather than calling.
    void SwapVectorAxes(ShipBlock& _work, const FlightState& _flight, std::size_t _at) noexcept
    {
      const std::uint8_t low = _work[_at];
      _work[_at] = _work[_at + 4u];
      _work[_at + 4u] = low;

      const std::uint8_t high = static_cast<std::uint8_t>(_work[_at + 1u] ^ _flight.rat);
      _work[_at + 1u] = static_cast<std::uint8_t>(_work[_at + 5u] ^ _flight.rat2);
      _work[_at + 5u] = high;
    }

  } // namespace

  void FlipAxesForView(ShipBlock& _work, FlightState& _flight, std::uint8_t _view) noexcept
  {
    // 6502: BEQ PU2-1 -- the front view needs nothing, and PU2-1 is the RTS at the end of the rear
    // view's block.
    if (_view == 0u)
    {
      return;
    }

    FlipAxes(_work, _flight, _view);
  }

  void FlipAxes(ShipBlock& _work, FlightState& _flight, std::uint8_t _view) noexcept
  {
    // 6502: PU1 -- the DEX comes first, so everything below reads the view MINUS ONE.
    const std::uint8_t which = static_cast<std::uint8_t>(_view - 1u);

    if (which == 0u)
    {
      // The rear view: the ship is behind you, so x and z both point the other way. Eight sign
      // bytes -- the position's x and z, and the x and z of all three orientation vectors.
      for (const std::size_t at : {2u, 8u, 10u, 14u, 16u, 20u, 22u, 26u})
      {
        _work[at] = static_cast<std::uint8_t>(_work[at] ^ 0x80u);
      }
      return;
    }

    // 6502: PU2 -- `LDA #0 / CPX #2 / ROR A` puts the comparison's carry into bit 7 of a zero, so
    // RAT2 is the sign mask for the right view and RAT for the left. One instruction less than an
    // `if`, and the reason the two masks are always each other's complement.
    _flight.rat2 = (which >= 2u) ? std::uint8_t{0x80} : std::uint8_t{0x00};
    _flight.rat = static_cast<std::uint8_t>(_flight.rat2 ^ 0x80u);

    // The position, whose low and high bytes swap plainly and whose signs swap with a flip.
    std::swap(_work[0], _work[6]);
    std::swap(_work[1], _work[7]);

    const std::uint8_t sign = static_cast<std::uint8_t>(_work[2] ^ _flight.rat);
    _work[2] = static_cast<std::uint8_t>(_work[8] ^ _flight.rat2);
    _work[8] = sign;

    SwapVectorAxes(_work, _flight, 9);
    SwapVectorAxes(_work, _flight, 15);
    SwapVectorAxes(_work, _flight, 21);
  }

} // namespace Elite
