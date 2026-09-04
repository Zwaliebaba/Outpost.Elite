#include "pch.h"

#include "FlightLoop.h"

#include "EliteTypes.h"

namespace Elite
{

std::uint8_t DoubleAndAddCoordinate(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _from,
                                    std::uint8_t _to) noexcept
{
  // 6502: LDA INWK,Y / ASL A / STA K+1 / LDA INWK+1,Y / ROL A / STA K+2.
  const ShiftResult low = RotateLeftValue(_work[_from], false);
  _math.k[1] = low.value;

  const ShiftResult high = RotateLeftValue(_work[static_cast<std::size_t>(_from) + 1u], low.carry);
  _math.k[2] = high.value;

  // 6502: LDA #0 / ROR A / STA K+3 -- the bit that fell off the top becomes the sign byte, so the
  // doubling cannot overflow: it widens instead.
  _math.k[3] = RotateRight(0u, high.carry).value;

  AddShipCoordinateToK(_work, _math, _to); // 6502: JSR MVT3

  // 6502: STA INWK+2,X -- and A is `K+3`, because every path through `MVT3` ends `STA K+3`.
  _work[static_cast<std::size_t>(_to) + 2u] = _math.k[3];
  _work[_to] = _math.k[1];                                        // 6502: LDY K+1 / STY INWK,X
  _work[static_cast<std::size_t>(_to) + 1u] = _math.k[2];         // 6502: LDY K+2 / STY INWK+1,X

  return static_cast<std::uint8_t>(_math.k[3] & 0x7Fu); // 6502: AND #%01111111
}

std::uint8_t LargestAxisFrom(const Bubble& _bubble, std::uint8_t _slot, std::uint8_t _a) noexcept
{
  /*
   * 6502: ORA K%+2,Y / ORA K%+5,Y / ORA K%+8,Y / AND #%01111111.
   *
   * `Y` is a byte offset into `K%` and every caller passes a multiple of the block size, so the
   * port takes the slot instead -- the same substitution `GINF` got, and for the same reason
   * (the 6502 cannot multiply by 37 and this port can).
   */
  const ShipBlock& block = _bubble.blocks[_slot];
  const std::uint8_t together =
    static_cast<std::uint8_t>(_a | block[2] | block[5] | block[8]);

  return static_cast<std::uint8_t>(together & 0x7Fu);
}

std::uint8_t SumOfSquares(const Bubble& _bubble, MathWorkspace& _math, std::uint8_t _slot) noexcept
{
  const ShipBlock& block = _bubble.blocks[_slot];

  // 6502: LDA K%+1,Y / JSR SQUA2 / STA R.
  _math.r = SquareUnsigned(_math, block[1]).high;

  // 6502: LDA K%+4,Y / JSR SQUA2 / ADC R / BCS MA30 -- the `ADC` reads `SQUA2`'s exit carry, and
  // that carry is never set (§6.70), so this is the plain addition it looks like.
  const WideResult second = SquareUnsigned(_math, block[4]);
  const AddResult sum = AddWithCarry(second.high, _math.r, second.carry);
  if (sum.carry)
  {
    return 0xFFu; // 6502: MA30 -- LDA #&FF
  }

  _math.r = sum.value; // 6502: STA R

  // 6502: LDA K%+7,Y / JSR SQUA2 / ADC R / BCC P%+4 -- and the branch skips the saturation.
  const WideResult third = SquareUnsigned(_math, block[7]);
  const AddResult total = AddWithCarry(third.high, _math.r, third.carry);

  return total.carry ? 0xFFu : total.value;
}

std::uint8_t LargestShipAxis(const ShipBlock& _work, std::uint8_t _a) noexcept
{
  // 6502: ORA INWK+1 / ORA INWK+4 / ORA INWK+7 -- no mask, unlike `MAS2`.
  return static_cast<std::uint8_t>(_a | _work[1] | _work[4] | _work[7]);
}

std::uint8_t DampTowardsCentre(std::uint8_t _value, std::uint8_t _dockingComputer,
                               std::uint8_t _dampingDisabled) noexcept
{
  // 6502: LDA auto / BNE cnt2 / LDA DAMP / BNE RE1 -- two tests, and only the second returns.
  if (_dockingComputer == 0u && _dampingDisabled != 0u)
  {
    return _value;
  }

  // 6502: TXA / BPL BUMP -- below the centre, so bump up towards it. `BUMP`'s own `BNE RE1` is
  // always taken from here, because X < 128 makes X + 1 <= 128.
  if ((_value & 0x80u) == 0u)
  {
    return static_cast<std::uint8_t>(_value + 1u);
  }

  // 6502: DEX / BMI RE1 -- at or above the centre, so reduce towards it, unless that has just
  // crossed the middle.
  const std::uint8_t reduced = static_cast<std::uint8_t>(_value - 1u);
  if ((reduced & 0x80u) != 0u)
  {
    return reduced;
  }

  // 6502: fall into `.BUMP INX` -- which only happens from X = 128, so this puts back the 128 the
  // `DEX` took away and the value sits still.
  return static_cast<std::uint8_t>(reduced + 1u);
}

} // namespace Elite
