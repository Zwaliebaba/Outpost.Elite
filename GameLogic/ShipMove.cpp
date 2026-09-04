#include "pch.h"

#include "ShipMove.h"

#include <array>

namespace Elite
{

void AddToShipCoordinate(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a, std::uint8_t _x,
                         bool _maskSign) noexcept
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

  SubResult high =
    SubtractWithCarry(static_cast<std::uint8_t>(_work[_x + 2u] & 0x7Fu), _math.s, middle.carry);
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

std::uint8_t AddShipCoordinateToP(const ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a,
                                  std::uint8_t _x) noexcept
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


void RotateShipVector(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _y, std::uint8_t _alpha,
                      std::uint8_t _beta) noexcept
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
[[nodiscard]] AddSignedResult RotateHalf(const ShipBlock& _work, MathWorkspace& _math,
                                         std::uint8_t _from, std::uint8_t _other, std::uint8_t _rat2,
                                         bool _flip) noexcept
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

void RotateCoordinatePair(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _x, std::uint8_t _y,
                          std::uint8_t _rat2) noexcept
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


std::uint8_t OrientationComponent(const ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a,
                                  std::uint8_t _x, std::uint8_t _y) noexcept
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
  std::array<std::uint8_t, 3> vector = { _work[_at], _work[_at + 2u], _work[_at + 4u] };
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

} // namespace Elite
