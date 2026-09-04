#include "pch.h"

#include "FlightLoop.h"

#include "EliteTypes.h"
#include "ShipBlueprint.h"
#include "Lasers.h"
#include "Messages.h"
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

void FireMissile(FlightLoop& _loop) noexcept
{
  FlightScreen& screen = _loop.screen;

  // 6502: LDX #MSL / JSR FRS1 / BCC FR1 -- a full bubble means the missile stays on the rail.
  if (!_loop.effects.SpawnAhead(SHIP_TYPE_MISSILE))
  {
    // 6502: .FR1 LDA #201 / JMP MESS -- "MISSILE JAMMED".
    ShowMessage(screen.canvas, screen.printer, screen.text, screen.extended, screen.message,
                MESSAGE_MISSILE_JAMMED, screen.view);
    return;
  }

  // 6502: LDX MSTG / JSR GINF / LDA FRIN,X / JSR ANGRY -- the TARGET's type, not the missile's.
  const std::uint8_t target = screen.bubble.missileTarget;
  _loop.effects.Anger(screen.bubble.slots[target]);

  // 6502: LDY #BLACK2 / JSR ABORT -- the lock is gone and so is the indicator.
  AbortMissileLock(screen.canvas, screen.bubble, screen.status.missileArmed,
                   screen.commander.At(Field::Missiles), MISSILE_NONE);

  // 6502: DEC NOMSL -- one fewer on the rail.
  screen.commander.At(Field::Missiles) =
    static_cast<std::uint8_t>(screen.commander.At(Field::Missiles) - 1u);

  (void)_loop.effects.PlaySound(SOUND_MISSILE); // 6502: LDY #sfxwhosh / JMP NOISE
}

LoopOutcome BeginFlightFrame(FlightLoop& _loop) noexcept
{
  FlightScreen& screen = _loop.screen;

  /*
   * 6502: LDA K% / STA RAND -- the planet's own x low byte, into the generator, every frame.
   *
   * Only the FIRST of the four seed bytes, so the other three carry on from wherever the last
   * call left them: this stirs the sequence rather than resetting it.
   */
  std::array<std::uint8_t, 4> seed = screen.rng.State();
  seed[0] = screen.bubble.blocks[0][0];
  screen.rng.SetState(seed);

  // 6502: LDA TRIBCT / BEQ NOMVETR / JMP MVTRIBS -- and `MVTRIBS` jumps back to `NOMVETR`, so
  // this is a call written as two jumps (§6.82).
  if (screen.trumbleSprites != 0u)
  {
    _loop.effects.MoveTrumbles();
  }

  // ---- part 2: the roll ------------------------------------------------------------------------

  // 6502: LDX JSTX / JSR cntr / JSR cntr -- twice, so the roll creeps back by two per frame.
  std::uint8_t roll = _loop.control.roll;
  roll = DampTowardsCentre(roll, _loop.control.dockingComputer, _loop.options.dampingDisabled);
  roll = DampTowardsCentre(roll, _loop.control.dockingComputer, _loop.options.dampingDisabled);

  // 6502: TXA / EOR #%10000000 / TAY / AND #%10000000 / STA ALP2 / STX JSTX / EOR #%10000000 /
  // STA ALP2+1 -- the rate turned into a sign and a magnitude, and the sign kept both ways round.
  const std::uint8_t rollSigned = static_cast<std::uint8_t>(roll ^ 0x80u);
  screen.flight.alp2 = static_cast<std::uint8_t>(rollSigned & 0x80u);
  _loop.control.roll = roll;
  screen.flight.alp2Next = static_cast<std::uint8_t>(screen.flight.alp2 ^ 0x80u);

  // 6502: TYA / BPL P%+7 / EOR #%11111111 / CLC / ADC #1 -- the magnitude, negated if it is on the
  // far side of the centre.
  std::uint8_t rollMagnitude = rollSigned;
  if ((rollMagnitude & 0x80u) != 0u)
  {
    rollMagnitude = static_cast<std::uint8_t>((rollMagnitude ^ 0xFFu) + 1u);
  }

  rollMagnitude = static_cast<std::uint8_t>(rollMagnitude >> 1u); // 6502: LSR A
  rollMagnitude = static_cast<std::uint8_t>(rollMagnitude >> 1u); // 6502: LSR A

  /*
   * 6502: CMP #8 / BCS P%+3 / LSR A.
   *
   * AND THE CARRY THIS LEAVES IS READ BY THE PITCH. Either the compare's, when the magnitude is
   * eight or more, or the extra shift's bit 0 when it is not -- and the pitch's `ADC #4` below
   * has no `SEC` or `CLC` before it, with `cntr` touching no flags on any of its paths (§6.85).
   */
  bool carry = rollMagnitude >= 8u;
  if (!carry)
  {
    carry = (rollMagnitude & 1u) != 0u;
    rollMagnitude = static_cast<std::uint8_t>(rollMagnitude >> 1u);
  }

  screen.flight.alp1 = rollMagnitude; // 6502: STA ALP1
  screen.flight.alpha = static_cast<std::uint8_t>(rollMagnitude | screen.flight.alp2);

  // ---- part 2: the pitch, which is not the same shape ------------------------------------------

  // 6502: LDX JSTY / JSR cntr -- ONCE, where the roll gets two.
  std::uint8_t pitch = _loop.control.pitch;
  pitch = DampTowardsCentre(pitch, _loop.control.dockingComputer, _loop.options.dampingDisabled);

  // 6502: TXA / EOR #%10000000 / TAY / AND #%10000000 / STX JSTY / STA BET2+1 / EOR #%10000000 /
  // STA BET2 -- the two sign bytes are written the OTHER way round from the roll's.
  const std::uint8_t pitchSigned = static_cast<std::uint8_t>(pitch ^ 0x80u);
  _loop.control.pitch = pitch;
  screen.flight.bet2Next = static_cast<std::uint8_t>(pitchSigned & 0x80u);
  screen.flight.bet2 = static_cast<std::uint8_t>(screen.flight.bet2Next ^ 0x80u);

  // 6502: TYA / BPL P%+4 / EOR #%11111111 -- and no negate-by-adding-one here, because the `ADC`
  // below does it.
  std::uint8_t pitchMagnitude = pitchSigned;
  if ((pitchMagnitude & 0x80u) != 0u)
  {
    pitchMagnitude = static_cast<std::uint8_t>(pitchMagnitude ^ 0xFFu);
  }

  // 6502: ADC #4 -- on the ROLL's carry, which is the finding above.
  pitchMagnitude = static_cast<std::uint8_t>(pitchMagnitude + 4u + (carry ? 1u : 0u));

  for (int shift = 0; shift < 4; ++shift) // 6502: LSR A four times
  {
    pitchMagnitude = static_cast<std::uint8_t>(pitchMagnitude >> 1u);
  }

  // 6502: CMP #3 / BCS P%+3 / LSR A -- three rather than the roll's eight, so the pitch is
  // coarser at the low end than the roll is.
  if (pitchMagnitude < 3u)
  {
    pitchMagnitude = static_cast<std::uint8_t>(pitchMagnitude >> 1u);
  }

  screen.flight.bet1 = pitchMagnitude; // 6502: STA BET1
  screen.flight.beta = static_cast<std::uint8_t>(pitchMagnitude | screen.flight.bet2);

  // ---- part 3: the keys ------------------------------------------------------------------------

  CommanderBlock& commander = screen.commander;

  // 6502: LDA KY2 / BEQ MA17 / LDA DELTA / CMP #40 / BCS MA17 / INC DELTA -- forty is the ceiling.
  if (_loop.keys[KEY_SPEED_UP] != 0u && screen.flight.delta < 40u)
  {
    screen.flight.delta = static_cast<std::uint8_t>(screen.flight.delta + 1u);
  }

  // 6502: .MA17 LDA KY1 / BEQ MA4 / DEC DELTA / BNE MA4 / INC DELTA -- and one is the floor, so
  // the ship never stops dead.
  if (_loop.keys[KEY_SLOW_DOWN] != 0u)
  {
    screen.flight.delta = static_cast<std::uint8_t>(screen.flight.delta - 1u);
    if (screen.flight.delta == 0u)
    {
      screen.flight.delta = 1u;
    }
  }

  // 6502: .MA4 LDA KY15 / AND NOMSL / BEQ MA20 -- the AND is the "have we got one" test, so the
  // key does nothing at all with an empty rail.
  if ((_loop.keys[KEY_UNARM_MISSILE] & commander.At(Field::Missiles)) != 0u)
  {
    AbortMissileLock(screen.canvas, screen.bubble, screen.status.missileArmed,
                     commander.At(Field::Missiles), MISSILE_READY);
    (void)_loop.effects.PlaySound(SOUND_BOOP); // 6502: LDY #sfxboop / JSR NOISE
    screen.status.missileArmed = 0u;     // 6502: LDA #0 / STA MSAR, which `ABORT` has already done
  }

  /*
   * 6502: .MA20 LDA MSTG / BPL MA25 / LDA KY14 / BEQ MA25 / LDX NOMSL / BEQ MA25 / STA MSAR /
   * LDY #YELLOW2 / JSR MSBAR.
   *
   * `MSTG` is 255 for no lock, so `BPL` skips this whenever there IS one: a missile already
   * seeking cannot be re-armed. And `STA MSAR` stores the KEY's value rather than a flag of its
   * own, which is &FF because that is what the scan writes.
   */
  if ((screen.bubble.missileTarget & 0x80u) != 0u && _loop.keys[KEY_ARM_MISSILE] != 0u
      && commander.At(Field::Missiles) != 0u)
  {
    screen.status.missileArmed = _loop.keys[KEY_ARM_MISSILE];
    SetMissileIndicator(screen.canvas, commander.At(Field::Missiles), MISSILE_ARMED);
  }

  /*
   * 6502: .MA25 LDA KY16 / BEQ MA24 / LDA MSTG / BMI MA64 / JSR FRMIS.
   *
   * PRESSING "M" WITH NO LOCK SKIPS FIVE OTHER KEYS. `BMI MA64` jumps past the energy bomb, the
   * docking-computer cancel, the escape pod, the warp and the E.C.M., so a frame in which the
   * player asks to fire a missile they have not locked is a frame in which none of those five
   * does anything. Nothing else in the routine branches that far forward.
   */
  bool checkedTheRest = true;
  if (_loop.keys[KEY_FIRE_MISSILE] != 0u)
  {
    if ((screen.bubble.missileTarget & 0x80u) != 0u)
    {
      checkedTheRest = false;
    }
    else
    {
      FireMissile(_loop);
    }
  }

  if (checkedTheRest)
  {
    // 6502: .MA24 LDA KY12 / BEQ MA76 / ASL BOMB / BEQ MA76 -- `BOMB` is a countdown kept as a
    // shift, so the bomb burns for as many frames as it has bits left.
    if (_loop.keys[KEY_ENERGY_BOMB] != 0u)
    {
      commander.At(Field::EnergyBomb) =
        static_cast<std::uint8_t>(commander.At(Field::EnergyBomb) << 1u);

      if (commander.At(Field::EnergyBomb) != 0u)
      {
        // 6502: LDY #%11010000 / STY moonflower -- the upper half of the screen changes mode, and
        // that IS the effect: no drawing is involved.
        screen.screen.upperBitmapMode = BOMB_BITMAP_MODE;
        (void)_loop.effects.PlaySound(SOUND_ENERGY_BOMB);
      }
    }

    // 6502: .MA76 LDA KY20 / BEQ MA78 / LDA #0 / STA auto / JSR stopbd.
    if (_loop.keys[KEY_CANCEL_DOCKING] != 0u)
    {
      _loop.control.dockingComputer = 0u;
      _loop.effects.StopDockingMusic();
    }

    // 6502: .MA78 LDA KY13 / AND ESCP / BEQ noescp / LDA MJ / BNE noescp / JMP ESCAPE -- and it
    // does not come back, so the frame ends here.
    if ((_loop.keys[KEY_ESCAPE_POD] & commander.At(Field::EscapePod)) != 0u
        && screen.status.midJump == 0u)
    {
      return LoopOutcome::Escaped;
    }

    // 6502: .noescp LDA KY18 / BEQ P%+5 / JSR WARP.
    if (_loop.keys[KEY_WARP] != 0u)
    {
      Warp(screen);
    }

    // 6502: LDA KY17 / AND ECM / BEQ MA64 / LDA ECMA / BNE MA64 / DEC ECMP / JSR ECBLB2 -- and
    // `DEC ECMP` on a zero byte is what makes it &FF, which is "ours" (§6.71's pair).
    if ((_loop.keys[KEY_ECM] & commander.At(Field::Ecm)) != 0u && screen.status.ecmCountdown == 0u)
    {
      screen.status.ecmOurs = static_cast<std::uint8_t>(screen.status.ecmOurs - 1u);
      StartEcm(screen.canvas, screen.status, _loop.effects);
    }
  }

  /*
   * 6502: .MA64 LDA KY19 / AND DKCMP / BEQ MA68 / EOR KLO+&29 / BEQ MA68 / STA auto / JSR startbd.
   *
   * `KLO+&29` IS `KY5`, the "X" key. So holding X while pressing C cancels the docking computer
   * request -- the two bytes are both &FF when held, and the `EOR` of a pair of &FFs is zero.
   * Nothing in the source says so and the offset is written as a number rather than as the label.
   */
  const std::uint8_t requested =
    static_cast<std::uint8_t>((_loop.keys[KEY_DOCKING_COMPUTER] & commander.At(Field::DockingComputer))
                              ^ _loop.keys[KEY_PITCH_UP]);
  if ((_loop.keys[KEY_DOCKING_COMPUTER] & commander.At(Field::DockingComputer)) != 0u
      && requested != 0u)
  {
    _loop.control.dockingComputer = requested;
    _loop.effects.StartDockingMusic();
  }

  // ---- part 3's tail: the guns -----------------------------------------------------------------

  screen.status.laserPower = 0u; // 6502: .MA68 LDA #0 / STA LAS
  screen.flight.delt4 = 0u;      // 6502: STA DELT4

  // 6502: LDA DELTA / LSR A / ROR DELT4 / LSR A / ROR DELT4 / STA DELT4+1 -- the speed as a
  // sixteen-bit value the stardust subtracts, which is DELTA shifted up six places.
  {
    ShiftResult step = RotateRight(screen.flight.delta, false);
    ShiftResult low = RotateRight(screen.flight.delt4, step.carry);
    step = RotateRight(step.value, false);
    low = RotateRight(low.value, step.carry);
    screen.flight.delt4 = low.value;
    screen.flight.delt4Next = step.value;
  }

  // 6502: LDA LASCT / BNE MA3 -- a pulse laser's countdown, which is why it cannot be held down.
  if (screen.status.laserCount != 0u)
  {
    return LoopOutcome::Continued;
  }

  // 6502: LDA KY7 / BEQ MA3 / LDA GNTMP / CMP #242 / BCS MA3 -- and 242 is where the gun jams.
  if (_loop.keys[KEY_FIRE] == 0u || screen.status.laserTemperature >= 242u)
  {
    return LoopOutcome::Continued;
  }

  // 6502: LDX VIEW / LDA LASER,X / BEQ MA3 -- this view's laser, if it has one.
  const std::uint8_t fitted =
    commander.bytes[static_cast<std::size_t>(Field::Lasers) + screen.spaceView];
  if (fitted == 0u)
  {
    return LoopOutcome::Continued;
  }

  // 6502: PHA / AND #%01111111 / STA LAS / STA LAS2 -- the power without its top bit, which is
  // what the damage arithmetic uses.
  screen.status.laserPower = static_cast<std::uint8_t>(fitted & 0x7Fu);
  screen.status.viewLaser = screen.status.laserPower;

  /*
   * 6502: LDY #sfxplas / PLA / PHA / BMI bmorarm / CMP #Mlas / BNE P%+4 / LDY #sfxmlas /
   * BNE custard / .bmorarm CMP #Armlas / BEQ P%+5 / LDY #sfxblas / EQUB &2C / LDY #sfxalas.
   *
   * The `EQUB &2C` is `BIT abs` again, swallowing the `LDY #sfxalas` so that the beam laser's
   * sound survives (§6.79). Sixth time in this port.
   */
  std::uint8_t sound = SOUND_PULSE_LASER;
  if ((fitted & 0x80u) != 0u)
  {
    sound = (fitted == LASER_POWER_MILITARY) ? SOUND_MILITARY_LASER : SOUND_BEAM_LASER;
  }
  else if (fitted == LASER_POWER_MINING)
  {
    sound = SOUND_MINING_LASER;
  }

  // 6502: .custard JSR NOISE -- and `LASLI` opens `JSR DORND`, whose `ROL A` reads the carry
  // this leaves, so the sound's own outcome shifts the burst by a pixel (§6.86).
  const bool heard = _loop.effects.PlaySound(sound);

  // 6502: JSR LASLI -- the burst itself, which draws and heats the gun.
  (void)FireLaser(screen.canvas, screen.draw, screen.rng, _loop.burst, screen.status,
                  screen.view, heard);

  // 6502: PLA / BPL ma1 / LDA #0 / .ma1 AND #%11111010 / STA LASCT -- a beam laser gets no
  // countdown at all, which is what lets it be held down.
  const std::uint8_t countdown = ((fitted & 0x80u) != 0u) ? std::uint8_t{ 0u } : fitted;
  screen.status.laserCount = static_cast<std::uint8_t>(countdown & 0xFAu);

  return LoopOutcome::Continued;
}

} // namespace Elite
