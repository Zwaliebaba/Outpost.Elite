#include "pch.h"

#include "ShipMove.h"

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

} // namespace Elite
