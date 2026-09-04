#include "pch.h"

#include "FlightLoop.h"

#include "EliteTypes.h"
#include "ShipBlueprint.h"
#include "ShipDraw.h"

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

void SpawnItems(MathWorkspace& _math, SpawnChildEffects& _effects, std::uint8_t _type,
                std::uint8_t _count) noexcept
{
  _math.cnt = _count; // 6502: .SPIN2 STA CNT, which sets no flags

  // 6502: .spl BEQ oh -- on the caller's Z flag, which every caller has just set from the count.
  if (_count == 0u)
  {
    return;
  }

  for (;;)
  {
    (void)_effects.SpawnChild(0u, _type); // 6502: LDA #0 / JSR SFS1

    _math.cnt = static_cast<std::uint8_t>(_math.cnt - 1u); // 6502: DEC CNT
    if (_math.cnt == 0u)                                   // 6502: BNE spl+2
    {
      return;
    }
  }
}

void SpawnDebris(Rng& _rng, MathWorkspace& _math, SpawnChildEffects& _effects,
                 std::uint16_t _blueprint, std::uint8_t _type, bool _carryIn) noexcept
{
  // 6502: JSR DORND / BPL oh -- and nothing else in the routine looks at the roll's low bits
  // except as a count, so half of all calls do nothing.
  const RngResult roll = _rng.Next(_carryIn);
  if ((roll.value & 0x80u) == 0u)
  {
    return;
  }

  /*
   * 6502: TYA / TAX / LDY #0 / AND (XX0),Y / AND #15.
   *
   * `TYA / TAX` reads as "copy Y into X", and it is -- but it goes THROUGH A, and the `AND` two
   * instructions later reads that A rather than the random number `DORND` left there. So the
   * count is the ship TYPE masked by the blueprint's first byte; the roll decides only whether
   * anything is dropped at all. The oracle caught the port doing it the obvious way (§6.74).
   */
  const std::uint8_t capped = static_cast<std::uint8_t>(_type & ShipByte(_blueprint) & 0x0Fu);

  SpawnItems(_math, _effects, _type, capped); // 6502: and it falls into SPIN2
}

bool DrainEnergy(FlightStatus& _status) noexcept
{
  // 6502: DEC ENERGY / PHP -- the flag the caller gets is this one, before the `INC` below.
  _status.energy = static_cast<std::uint8_t>(_status.energy - 1u);
  const bool emptied = _status.energy == 0u;

  // 6502: BNE P%+5 / INC ENERGY / PLP -- one is the floor, and the caller still hears about it.
  if (emptied)
  {
    _status.energy = static_cast<std::uint8_t>(_status.energy + 1u);
  }

  return emptied;
}

std::uint8_t RechargeShield(FlightStatus& _status, std::uint8_t _shield) noexcept
{
  // 6502: .SHD INX / BEQ SHD-2 -- a full shield is put back and costs nothing.
  const std::uint8_t raised = static_cast<std::uint8_t>(_shield + 1u);
  if (raised == 0u)
  {
    return static_cast<std::uint8_t>(raised - 1u); // 6502: SHD-2 is `DEX / RTS`
  }

  // 6502: and no RTS -- it falls into DENGY, so the unit comes out of the banks (§6.83).
  (void)DrainEnergy(_status);
  return raised;
}

bool WithinRange(const ShipBlock& _work, std::uint8_t _limit) noexcept
{
  // 6502: CMP INWK+1 / BCC FA1 / CMP INWK+4 / BCC FA1 / CMP INWK+7 / .FA1 RTS -- and the carry
  // out of the LAST compare reached is the answer, which is why the two early exits both leave a
  // clear one.
  if (_limit < _work[1] || _limit < _work[4])
  {
    return false;
  }

  return _limit >= _work[7];
}

bool IsHit(const ShipBlock& _work, MathWorkspace& _math, std::uint16_t _blueprint,
           std::uint8_t _type) noexcept
{
  // 6502: CLC / LDA INWK+8 / BNE HI1 -- the z sign byte, and anything but zero means the ship is
  // not close enough in front of us to have been hit.
  if (_work[8] != 0u)
  {
    return false;
  }

  // 6502: LDA TYPE / BMI HI1 -- the planet and the sun are not shootable.
  if ((_type & 0x80u) != 0u)
  {
    return false;
  }

  // 6502: LDA INWK+31 / AND #%00100000 / ORA INWK+1 / ORA INWK+4 / BNE HI1 -- already exploding,
  // or too far off to either side. Three tests ORed into one branch.
  if (((_work[31] & 0x20u) | _work[1] | _work[4]) != 0u)
  {
    return false;
  }

  // 6502: LDA INWK / JSR SQUA2 / STA S / LDA P / STA R.
  const WideResult across = SquareUnsigned(_math, _work[0]);
  _math.s = across.high;
  _math.r = _math.p;

  // 6502: LDA INWK+3 / JSR SQUA2 / TAX / LDA P / ADC R / STA R / TXA / ADC S / BCS TN10.
  const WideResult down = SquareUnsigned(_math, _work[3]);
  const AddResult low = AddWithCarry(_math.p, _math.r, across.carry);
  _math.r = low.value;
  const AddResult high = AddWithCarry(down.high, _math.s, low.carry);
  if (high.carry)
  {
    return false; // 6502: .TN10 CLC / RTS -- too big to compare, which is its own "no"
  }

  _math.s = high.value; // 6502: STA S

  /*
   * 6502: LDY #2 / LDA (XX0),Y / CMP S / BNE HI1 / DEY / LDA (XX0),Y / CMP R.
   *
   * A SIXTEEN-BIT COMPARE, HIGH BYTE FIRST, and `BNE HI1` is its early ANSWER rather than an
   * early no. `HI1` is a bare `RTS`, so the branch returns the carry `CMP S` just set -- which
   * says whether the blueprint's high byte is the larger. Only equal high bytes need the low
   * ones compared.
   *
   * The label is shared with four genuine rejections above, which is exactly why the port read
   * it as a fifth and failed on the first case it was given (§6.84).
   */
  const std::uint8_t target = ShipByte(static_cast<std::uint16_t>(_blueprint + 2u));
  if (target != _math.s)
  {
    return target >= _math.s;
  }

  return ShipByte(static_cast<std::uint16_t>(_blueprint + 1u)) >= _math.r;
}

} // namespace Elite
