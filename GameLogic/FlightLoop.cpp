#include "pch.h"

#include "FlightLoop.h"

#include "EliteTypes.h"
#include "ShipBlueprint.h"
#include "Lasers.h"
#include "Messages.h"
#include "Combat.h"
#include "Market.h"
#include "PlanetDraw.h"
#include "Scanner.h"
#include "ShipDraw.h"
#include "Spawn.h"
#include "Stardust.h"

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

namespace
{
/*
 * 6502: KILLSHP's four calls, wired to the routines the port already has.
 *
 * `SpawnEffects` was a seam when `KILLSHP` was written, because the dashboard did not exist yet.
 * It does now, and every one of the four is ported -- so the flight loop hands `KILLSHP` the real
 * thing rather than counting calls it could make for real (§6.73's rule, applied forwards).
 */
class LoopSpawnEffects final : public Elite::SpawnEffects
{
public:
  explicit LoopSpawnEffects(Elite::FlightLoop& _loop) noexcept : m_loop(_loop) {}

  void AbortMissile(std::uint8_t _colour) override
  {
    Elite::FlightScreen& screen = m_loop.screen;
    Elite::AbortMissileLock(screen.canvas, screen.bubble, screen.status.missileArmed,
                            screen.commander.At(Elite::Field::Missiles), _colour);
  }

  void ShowMessage(std::uint8_t _token) override
  {
    Elite::FlightScreen& screen = m_loop.screen;
    Elite::ShowMessage(screen.canvas, screen.printer, screen.text, screen.extended, screen.message,
                       _token, screen.view);
  }

  void ToggleStationIndicator() override
  {
    Elite::ToggleStationIndicator(m_loop.screen.canvas);
  }

  void ResetMissileIndicators() override
  {
    Elite::ResetMissileIndicators(m_loop.screen.canvas,
                                  m_loop.screen.commander.At(Elite::Field::Missiles));
  }

private:
  Elite::FlightLoop& m_loop;
};
} // namespace

namespace
{
/// 6502: INWK's bytes parts 4 to 12 name by number.
inline constexpr std::size_t SHIP_X_LOW = 0;
inline constexpr std::size_t SHIP_Y_LOW = 3;
inline constexpr std::size_t SHIP_Z_LOW = 6;
inline constexpr std::size_t SHIP_Z_HIGH = 7;
inline constexpr std::size_t SHIP_SCOOP_FLAGS = 5;
inline constexpr std::size_t SHIP_PITCH_COUNTER = 14;
inline constexpr std::size_t SHIP_ROLL_COUNTER = 16;
inline constexpr std::size_t SHIP_STATE = 31;
inline constexpr std::size_t SHIP_ENERGY = 35;
inline constexpr std::size_t SHIP_FLAGS = 36; ///< 6502: NEWB, which IS `INWK+36`

/// 6502: the bits of `INWK+31` this half tests and sets.
inline constexpr std::uint8_t SHIP_KILLED = 0x80;
inline constexpr std::uint8_t SHIP_EXPLODING = 0x20;
inline constexpr std::uint8_t SHIP_DRAWN_OR_EXPLODING = 0xA0;

/// 6502: the bits of `NEWB` -- bit 7 "leave the bubble", bit 6 "shooting one is a crime",
/// bit 2 "hostile", and bit 0's neighbour that `SCAN` reads.
inline constexpr std::uint8_t NEWB_REMOVE = 0x80;
inline constexpr std::uint8_t NEWB_INNOCENT = 0x40;
inline constexpr std::uint8_t NEWB_HOSTILE = 0x04;

/// 6502: (XX0),10 and (XX0),11 -- the bounty a blueprint carries, low byte first.
inline constexpr std::uint16_t BLUEPRINT_BOUNTY_LOW = 10;
inline constexpr std::uint16_t BLUEPRINT_BOUNTY_HIGH = 11;

/// 6502: (XX0),0 -- the top nibble is what the ship is worth when it is scooped.
inline constexpr std::uint16_t BLUEPRINT_SCOOP = 0;

/// 6502: LDA K%+NI%+36 / AND #%00000100 -- the station's own `NEWB`, in slot 1.
inline constexpr std::uint8_t STATION_SLOT = 1;

/// 6502: the docking check's three thresholds, none of which is named in the source.
inline constexpr std::uint8_t DOCK_MINIMUM_PITCH = 214;
inline constexpr std::uint8_t DOCK_MINIMUM_ALIGNMENT = 89;
inline constexpr std::uint8_t DOCK_MAXIMUM_ROLL = 80;

/// 6502: LDA DELTA / CMP #5 -- below this a failed dock is survivable and above it is not.
inline constexpr std::uint8_t DOCK_SURVIVABLE_SPEED = 5;

/// 6502: LDY #78 -- "CARGO SCOOPED" is not a token here, it is `MESS`'s argument for a full hold.
inline constexpr std::uint8_t MESSAGE_HOLD_FULL = 78;

/// 6502: LDA #208 -- the first cargo name's token, which the item number is added to.
inline constexpr std::uint8_t MESSAGE_FIRST_CARGO = 208;

/// 6502: LDX #15 -- the laser's own "damage" for the noise `EXNO` makes, which is not `LAS`.
inline constexpr std::uint8_t LASER_HIT_ENERGY = 15;

/// 6502: LDA #50 / CMP ENERGY -- and the message doubles when the banks are under half of it.
inline constexpr std::uint8_t ENERGY_WARNING = 50;
inline constexpr std::uint8_t MESSAGE_ENERGY_LOW = 50;

/// 6502: the three steps of `MCNT AND 31` that do something.
inline constexpr std::uint8_t STEP_ENERGY_CHECK = 10;
inline constexpr std::uint8_t STEP_DOCKING_REMINDER = 15;
inline constexpr std::uint8_t STEP_CABIN_TEMPERATURE = 20;

/// 6502: LDA #123 and LDA #160 -- "DOCKING COMPUTERS ON" and "FUEL SCOOPS ON".
inline constexpr std::uint8_t MESSAGE_DOCKING_ON = 123;
inline constexpr std::uint8_t MESSAGE_SCOOPS_ON = 160;

/// 6502: SBC #36 / STA R / JSR LL5 -- the altitude's own constant, and the cabin's.
inline constexpr std::uint8_t ALTITUDE_PLANET_RADIUS = 36;
inline constexpr std::uint8_t CABIN_BASE = 30;

/// 6502: CMP #224 / CMP #240 -- the sun cooks the cabin, and then it cooks the Trumbles.
inline constexpr std::uint8_t CABIN_SCOOPING = 224;
inline constexpr std::uint8_t CABIN_TRUMBLE_DEATH = 240;

/// 6502: CMP #70 -- a full tank, in tenths of a light year.
inline constexpr std::uint8_t FUEL_MAXIMUM = 70;

/// 6502: LDA #192 / JSR FAROF2 -- the station is respawned when the planet is inside this.
inline constexpr std::uint8_t STATION_SPAWN_RANGE = 192;

/// 6502: LDA #%101 and LDA #%100 -- the two raster modes `SETL1` is asked for around the sprite
/// write that kills a Trumble.
inline constexpr std::uint8_t RASTER_TRUMBLE_KILL = 0x05;
inline constexpr std::uint8_t RASTER_TRUMBLE_DONE = 0x04;

/// 6502: LDA VIC+&15 / AND #%00000011 -- everything but the two lowest sprites goes off.
inline constexpr std::uint8_t SPRITES_KEEP = 0x03;

/// 6502: LDA LASCT / CMP #8 -- above this the beam is still being drawn and is left alone.
inline constexpr std::uint8_t LASER_ERASE_LIMIT = 8;

/// 6502: ASL x / SEC / ROR x -- set bit 7 without touching the other seven.
[[nodiscard]] std::uint8_t MarkKilled(std::uint8_t _state) noexcept
{
  return static_cast<std::uint8_t>(_state | SHIP_KILLED);
}
} // namespace

/*
 * 6502: part 11 from `.MA47`'s `LDX #15 / JSR EXNO` to `.MA14` -- what our laser does to it.
 *
 * `LDX #15` IS DEAD. `EXNO`'s first two instructions are `LDA INWK+7 / LDX #11`, so the fifteen
 * is overwritten before it is read and the frequency `NOISE2` gets is the 208 `EXNO` loads for
 * itself. The upstream comment beside it describes the call, not the load.
 *
 * `stores` is false for the two paths that branch to `MA14+2` -- a MID-INSTRUCTION address, and
 * `STA INWK+35` is two bytes because `INWK` is in zero page. A station and a hardened ship shot
 * with the wrong laser take no damage at all; they only get angry.
 */
struct LaserHit
{
  bool stores;
  std::uint8_t energy;
};

[[nodiscard]] LaserHit ApplyLaserHit(FlightLoop& _loop, SpawnChildEffects& _spawn,
                                     std::uint16_t _blueprint, std::uint8_t _type) noexcept
{
  FlightScreen& screen = _loop.screen;

  (void)PlayHitSound(screen.work, _loop.effects); // 6502: LDX #15 / JSR EXNO

  // 6502: LDA TYPE / CMP #SST / BEQ MA14+2.
  if (_type == SHIP_TYPE_STATION)
  {
    return { false, screen.work[SHIP_ENERGY] };
  }

  std::uint8_t power = screen.status.laserPower;

  /*
   * 6502: CMP #CON / BCC BURN / LDA LAS / CMP #(Armlas AND 127) / BNE MA14+2 / LSR LAS / LSR LAS.
   *
   * A Constrictor or anything above it is immune to everything but a military laser, and takes a
   * QUARTER of even that. `Armlas AND 127` is 23, which is the power `MA68` stored after masking
   * the top bit off -- so the comparison is against the masked value and not against `Armlas`.
   */
  if (_type >= SHIP_TYPE_CONSTRICTOR)
  {
    if (power != static_cast<std::uint8_t>(LASER_POWER_MILITARY & 0x7Fu))
    {
      return { false, screen.work[SHIP_ENERGY] };
    }

    power = static_cast<std::uint8_t>(power >> 1u);
    power = static_cast<std::uint8_t>(power >> 1u);
    screen.status.laserPower = power; // 6502: LSR LAS twice, in place
  }

  // 6502: .BURN LDA INWK+35 / SEC / SBC LAS / BCS MA14 -- it survived with what is left.
  const SubResult left = SubtractWithCarry(screen.work[SHIP_ENERGY], power, true);
  if (left.carry)
  {
    return { true, left.value };
  }

  screen.work[SHIP_STATE] = static_cast<std::uint8_t>(screen.work[SHIP_STATE] | SHIP_KILLED);

  /*
   * 6502: LDA TYPE / CMP #AST / BNE nosp / LDA LAS / CMP #Mlas / BNE nosp / JSR DORND / LDX #SPL /
   * AND #3 / JSR SPIN2.
   *
   * ONLY a mining laser splits an asteroid, and only an asteroid splits. Everything else drops
   * whatever `SPIN` decides from its blueprint, which is not random at all (§6.74).
   */
  if (_type == SHIP_TYPE_ASTEROID && power == LASER_POWER_MINING)
  {
    const RngResult roll = screen.rng.Next(false);
    SpawnItems(screen.math, _spawn, SHIP_TYPE_SPLINTER,
               static_cast<std::uint8_t>(roll.value & 3u));
  }

  // 6502: .nosp LDY #PLT / JSR SPIN / LDY #OIL / JSR SPIN -- both, in that order, every time.
  SpawnDebris(screen.rng, screen.math, _spawn, _blueprint, SHIP_TYPE_ALLOY_PLATE, false);
  SpawnDebris(screen.rng, screen.math, _spawn, _blueprint, SHIP_TYPE_CANISTER, false);

  // 6502: LDX TYPE / JSR EXNO2 -- and what `.MA14` stores is what NOISE2 left in A (§6.86's
  // dependency again: the dead ship's energy byte comes out of the sound system).
  return { true, RecordKill(screen, _loop.effects, _type) };
}

LoopOutcome MoveEveryShip(FlightLoop& _loop) noexcept
{
  FlightScreen& screen = _loop.screen;
  CommanderBlock& commander = screen.commander;
  LoopSpawnEffects spawning(_loop);

  // 6502: .MA3 LDX #0 / .MAL1 STX XSAV -- and the index is advanced by hand, never by the loop.
  std::uint8_t slot = 0;

  for (;;)
  {
    screen.flight.slot = slot; // 6502: STX XSAV

    // 6502: LDA FRIN,X / BNE P%+5 / JMP MA18 -- the first empty slot ends the pass.
    const std::uint8_t type = screen.bubble.slots[slot];
    if (type == 0u)
    {
      return LoopOutcome::Continued;
    }

    screen.flight.type = type; // 6502: STA TYPE

    // 6502: JSR GINF / LDY #NI%-1 / .MAL2 LDA (INF),Y / STA INWK,Y / DEY / BPL MAL2.
    ShipBlock& block = screen.bubble.blocks[slot];
    screen.work = block;

    /*
     * 6502: LDA TYPE / BMI MA21 / ASL A / TAY / LDA XX21-2,Y / STA XX0 / LDA XX21-1,Y / STA XX0+1.
     *
     * The planet and the sun have no blueprint, and `XX0` is left holding the LAST ship's -- which
     * is why part 5's `CPY` tests below run on a Y that only a real ship has set.
     */
    const bool isBody = (type & 0x80u) != 0u;
    if (!isBody)
    {
      screen.flight.blueprint = BlueprintAddress(type);

      /*
       * 6502: part 5 -- LDA BOMB / BPL MA21 and four tests under it.
       *
       * The energy bomb kills everything in the bubble EXCEPT a station, a Thargoid and anything
       * from the Constrictor upwards, and the three exemptions are written as `CPY` against
       * `2*SST`, `2*THG` and `2*CON` -- comparisons on the DOUBLED type, because Y still holds
       * the blueprint index. A ship already exploding is skipped as well, or the bomb would
       * restart its cloud on every frame it burns.
       */
      const bool exempt = (type == SHIP_TYPE_STATION) || (type == SHIP_TYPE_THARGOID)
                          || (type >= SHIP_TYPE_CONSTRICTOR);

      if ((commander.At(Field::EnergyBomb) & 0x80u) != 0u && !exempt
          && (screen.work[SHIP_STATE] & SHIP_EXPLODING) == 0u)
      {
        screen.work[SHIP_STATE] = MarkKilled(screen.work[SHIP_STATE]);
        (void)RecordKill(screen, _loop.effects, type); // 6502: LDX TYPE / JSR EXNO2
      }
    }

    // 6502: .MA21 JSR MVEIT / LDY #NI%-1 / .MAL3 LDA INWK,Y / STA (INF),Y / DEY / BPL MAL3.
    MoveShip(screen.canvas, screen.draw, screen.work, screen.math, screen.flight, _loop.tactics,
             screen.flight.blueprint, screen.view);
    block = screen.work;


    /*
     * ---- part 7: is it touching us? ---------------------------------------------------------
     *
     * 6502: LDA INWK+31 / AND #%10100000 / JSR MAS4 / BNE MA65.
     *
     * ONE TEST DOES FOUR JOBS. `MAS4` ORs the three high bytes into whatever it is handed, so
     * seeding it with the "exploding or dead" bits means a ship that is far away on any axis, or
     * already exploding, or already killed, all fail together. What survives is close and intact.
     */
    bool docking = false;
    bool scoopable = false;
    bool collision = false;
    {
      const std::uint8_t seed =
        static_cast<std::uint8_t>(screen.work[SHIP_STATE] & SHIP_DRAWN_OR_EXPLODING);

      if (LargestShipAxis(screen.work, seed) == 0u)
      {
        // 6502: LDA INWK / ORA INWK+3 / ORA INWK+6 / BMI MA65 -- and A survives to the `AND` below.
        const std::uint8_t low = static_cast<std::uint8_t>(
          screen.work[SHIP_X_LOW] | screen.work[SHIP_Y_LOW] | screen.work[SHIP_Z_LOW]);

        if ((low & 0x80u) == 0u && !isBody)
        {
          if (type == SHIP_TYPE_STATION)
          {
            docking = true; // 6502: CPX #SST / BEQ ISDK
          }
          else if ((low & 0xC0u) == 0u && type != SHIP_TYPE_MISSILE)
          {
            /*
             * 6502: LDA BST / AND INWK+5 / BPL MA58.
             *
             * `BST` is &FF with fuel scoops fitted and byte 5 is the ship's y sign, so the `AND`
             * is "we have scoops AND it is below us" -- scooping only works on things that come
             * up from underneath. Anything else at this range is a collision.
             */
            const std::uint8_t under = static_cast<std::uint8_t>(commander.At(Field::FuelScoops)
                                                                 & screen.work[SHIP_SCOOP_FLAGS]);
            scoopable = (under & 0x80u) != 0u;
            collision = !scoopable;
          }
        }
      }
    }

    /*
     * ---- part 8: scooping it up ---------------------------------------------------------------
     *
     * 6502: CPX #OIL / BEQ oily / LDY #0 / LDA (XX0),Y / LSR A x4 / BEQ MA58 / ADC #1.
     *
     * AND THE `ADC` TAKES THE FOURTH `LSR`'s CARRY, which is bit 3 of the blueprint's first byte.
     * `BEQ MA58` sits between them and tests A without touching the flags, so what lands in the
     * hold is the top nibble plus one plus a bit of the bottom nibble (§6.89). A cargo canister
     * skips all of that and rolls for a random item instead.
     */
    bool holdFull = false;
    if (scoopable)
    {
      std::uint8_t item = 0;
      bool crashed = false;

      if (type == SHIP_TYPE_CANISTER)
      {
        item = static_cast<std::uint8_t>(screen.rng.Next(false).value & 7u); // 6502: oily
      }
      else
      {
        const std::uint8_t nibble = ShipByte(static_cast<std::uint16_t>(screen.flight.blueprint + BLUEPRINT_SCOOP));
        const std::uint8_t worth = static_cast<std::uint8_t>(nibble >> 4u);
        if (worth == 0u)
        {
          crashed = true; // 6502: BEQ MA58 -- nothing worth scooping, so it is a collision
        }
        else
        {
          item = static_cast<std::uint8_t>(worth + 1u + ((nibble & 0x08u) != 0u ? 1u : 0u));
        }
      }

      if (crashed)
      {
        scoopable = false;
        collision = true;
      }
      else
      {
        // 6502: .slvy2 JSR tnpr1 / LDY #78 / BCS MA59 -- `tnpr1` stores the item and asks for ONE.
        if (!CargoFits(commander, item, 1u))
        {
          holdFull = true;
          scoopable = false;
        }
        else
        {
          // 6502: LDY QQ29 / ADC QQ20,Y / STA QQ20,Y -- A is the 1 `tnpr` pushed and popped, and
          // the carry is clear because that is how the `BCS` was not taken.
          const std::size_t held = static_cast<std::size_t>(Field::CargoHold) + item;
          const AddResult stored = AddWithCarry(1u, commander.bytes[held], false);
          commander.bytes[held] = stored.value;

          // 6502: TYA / ADC #208 / JSR MESS -- on the carry the store above left behind.
          const AddResult token = AddWithCarry(item, MESSAGE_FIRST_CARGO, stored.carry);
          ShowMessage(screen.canvas, screen.printer, screen.text, screen.extended, screen.message,
                      token.value, screen.view);

          // 6502: ASL NEWB / SEC / ROR NEWB -- bit 7 is "take it out of the bubble", so a scooped
          // canister is removed by part 12 rather than by anything here.
          screen.work[SHIP_FLAGS] =
            static_cast<std::uint8_t>(screen.work[SHIP_FLAGS] | NEWB_REMOVE);
        }
      }
    }

    /*
     * ---- part 9: docking, or not ------------------------------------------------------------
     *
     * Four tests and every one of them has to pass. The station must not be hostile, the ship
     * must be pointing the right way (`INWK+14` is the pitch counter), the player must be lined
     * up with the slot, and the roll must be slow enough. `SPS1` is called for its side effect:
     * it leaves the normalised vector to the PLANET in `XX15`, and `XX15+2` is what the
     * alignment test reads.
     */
    if (docking)
    {
      bool arrived = false;

      const bool hostile =
        (screen.bubble.blocks[STATION_SLOT][SHIP_FLAGS] & NEWB_HOSTILE) != 0u;

      if (!hostile && screen.work[SHIP_PITCH_COUNTER] >= DOCK_MINIMUM_PITCH)
      {
        LoadPlanetAxes(screen.bubble, _loop.axes, screen.draw, screen.math); // 6502: JSR SPS1
        NormaliseAxes(_loop.axes, screen.draw, screen.math);                 // the fall-through

        if (screen.draw.x2 >= DOCK_MINIMUM_ALIGNMENT
            && static_cast<std::uint8_t>(screen.work[SHIP_ROLL_COUNTER] & 0x7Fu)
                 >= DOCK_MAXIMUM_ROLL)
        {
          arrived = true;
        }
      }

      if (arrived)
      {
        _loop.effects.StopDockingMusic(); // 6502: .GOIN JSR stopbd / JMP DOENTRY
        return LoopOutcome::Docked;
      }

      // 6502: .MA62 LDA DELTA / CMP #5 / BCC MA67 / JMP DEATH -- fast enough and you are dead.
      if (screen.flight.delta >= DOCK_SURVIVABLE_SPEED)
      {
        return LoopOutcome::Died;
      }

      collision = false;
      scoopable = false;
      holdFull = false;
    }

    /*
     * ---- part 10: what the collision costs ---------------------------------------------------
     *
     * Three entries into one call. `MA59` is a full hold, which is not damage at all -- the thing
     * bounces off and the frame carries on. `MA67` is a slow bump into the station, worth five.
     * `MA58` is everything else, and its damage is HALF THE OTHER SHIP'S OWN ENERGY: `LDA INWK+35
     * / SEC / ROR A`, which is also where `OOPS`'s carry comes from (§6.87).
     */
    if (holdFull)
    {
      (void)_loop.effects.PlaySound(SOUND_EXPLOSION);   // 6502: .MA59 JSR EXNO3
      screen.work[SHIP_STATE] = MarkKilled(screen.work[SHIP_STATE]); // 6502: .MA60
      // 6502: .MA61 BNE MA26 -- and `ROR` has just set bit 7, so it always branches.
    }
    else if (collision || docking)
    {
      std::uint8_t damage = 0;
      bool carry = false;

      if (docking)
      {
        screen.flight.delta = 1u;      // 6502: .MA67 LDA #1 / STA DELTA
        damage = DOCK_SURVIVABLE_SPEED;// 6502: LDA #5 -- and the carry is the `BCC`'s, so clear
      }
      else
      {
        screen.work[SHIP_STATE] = MarkKilled(screen.work[SHIP_STATE]); // 6502: .MA58
        const ShiftResult halved = RotateRight(screen.work[SHIP_ENERGY], true);
        damage = halved.value;
        carry = halved.carry;
      }

      // 6502: .MA63 JSR OOPS / JSR EXNO3 -- and `OOPS` makes the same noise itself on the path
      // that survives, so a hit that costs the banks is heard twice.
      if (!TakeDamage(screen, _loop.effects, block, damage, carry))
      {
        return LoopOutcome::Died;
      }
      (void)_loop.effects.PlaySound(SOUND_EXPLOSION);
    }

    /*
     * ---- part 11: our laser, and the scanner -------------------------------------------------
     *
     * 6502: .MA26 LDA NEWB / BPL P%+5 / JSR SCAN.
     *
     * Bit 7 of `NEWB` is "take this out of the bubble" AND "it is on the scanner", one bit doing
     * two jobs: a ship marked for removal has its blip drawn here so that the EOR erases it.
     */
    if ((screen.work[SHIP_FLAGS] & NEWB_REMOVE) != 0u)
    {
      DrawScannerBlip(screen.canvas, screen.draw, screen.work, type, screen.view);
    }

    bool drawIt = true;

    if (screen.view == 0u) // 6502: LDA QQ11 / BNE MA15 -- a chart means no drawing at all
    {
      FlipAxesForView(screen.work, screen.flight, screen.spaceView); // 6502: JSR PLUT

      if (IsHit(screen.work, screen.math, screen.flight.blueprint, type)) // 6502: JSR HITCH / BCC MA8
      {
        // 6502: LDA MSAR / BEQ MA47 / JSR BEEP / LDX XSAV / LDY #RED2 / JSR ABORT2 -- an armed
        // missile locks onto whatever the sights are on, and the indicator turns red.
        if (screen.status.missileArmed != 0u)
        {
          (void)_loop.effects.PlaySound(SOUND_BEEP);
          SetMissileTarget(screen.canvas, screen.bubble, screen.status.missileArmed,
                           commander.At(Field::Missiles), screen.flight.slot, MISSILE_LOCKED);
        }

        // 6502: .MA47 LDA LAS / BEQ MA8 -- no laser firing this frame, so nothing is damaged.
        if (screen.status.laserPower != 0u)
        {
          const LaserHit hit = ApplyLaserHit(_loop, _loop.effects, screen.flight.blueprint, type);
          if (hit.stores)
          {
            screen.work[SHIP_ENERGY] = hit.energy; // 6502: .MA14 STA INWK+35
          }

          // 6502: `MA14+2` -- LDA TYPE / JSR ANGRY, which both skip-the-store paths land on too.
          _loop.effects.Anger(type);
        }
      }
    }
    else
    {
      drawIt = false;
    }

    // 6502: .MA8 JSR LL9 -- and it is the same call that erases the last frame's ship.
    if (drawIt)
    {
      DrawShip(screen.canvas, screen.draw, screen.geometry, screen.math, _loop.clip,
               _loop.projection, screen.work, block, _loop.heap, screen.flight.blueprint,
               type, _loop.drawing);
    }

    /*
     * ---- part 12: what is written back, and what is removed ----------------------------------
     *
     * 6502: .MA15 LDY #35 / LDA INWK+35 / STA (INF),Y -- the energy goes back on every path, and
     * byte 31 only on the path that keeps the ship. That asymmetry is the routine: a ship being
     * removed has its block shuffled away by `KILLSHP`, so writing its state would be wasted.
     */
    block[SHIP_ENERGY] = screen.work[SHIP_ENERGY];

    bool remove = (screen.work[SHIP_FLAGS] & NEWB_REMOVE) != 0u;

    if (!remove && (screen.work[SHIP_STATE] & SHIP_KILLED) != 0u
        && (screen.work[SHIP_STATE] & SHIP_EXPLODING) != 0u)
    {
      /*
       * 6502: LDA NEWB / AND #%01000000 / ORA FIST / STA FIST.
       *
       * Bit 6 is "shooting this was a crime", and it is ORed into the legal status rather than
       * added -- so the offence is recorded once however many innocents die, and a fugitive
       * cannot become more of one this way.
       */
      commander.At(Field::LegalStatus) = static_cast<std::uint8_t>(
        commander.At(Field::LegalStatus) | (screen.work[SHIP_FLAGS] & NEWB_INNOCENT));

      // 6502: LDA DLY / ORA MJ / BNE KS1S -- no bounty while a message is up or in witchspace,
      // because the bounty IS a message and there is nowhere to put it.
      const bool quiet = (screen.message.delay | screen.status.midJump) == 0u;

      if (quiet)
      {
        // 6502: LDY #10 / LDA (XX0),Y / BEQ KS1S / TAX / INY / LDA (XX0),Y / TAY / JSR MCASH.
        const std::uint8_t low =
          ShipByte(static_cast<std::uint16_t>(screen.flight.blueprint + BLUEPRINT_BOUNTY_LOW));

        if (low != 0u)
        {
          const std::uint8_t high =
            ShipByte(static_cast<std::uint16_t>(screen.flight.blueprint + BLUEPRINT_BOUNTY_HIGH));
          ReceiveCash(commander, static_cast<std::uint16_t>(low | (high << 8)));

          /*
           * 6502: LDA #0 / JSR MESS.
           *
           * TOKEN ZERO IS THE CASH. `TT27` opens `TAX / BEQ csh`, so the bounty message is the
           * player's balance printed in flight -- and `MESS` then stores that zero in `MCH`, so
           * the next message within twenty frames erases this one by printing the balance again.
           */
          ShowMessage(screen.canvas, screen.printer, screen.text, screen.extended, screen.message,
                      0u, screen.view);
        }
      }

      remove = true; // 6502: .KS1S JMP KS1 -- every path through this block ends there
    }

    if (!remove)
    {
      // 6502: .MAC1 LDA TYPE / BMI MA27 / JSR FAROF / BCC KS1S -- and the planet and the sun are
      // never out of range, because they are what range is measured against.
      remove = !isBody && !WithinLoopRange(screen.work);
    }

    if (remove)
    {
      /*
       * 6502: .KS1 LDX XSAV / JSR KILLSHP / LDX XSAV / JMP MAL1.
       *
       * THE INDEX IS NOT ADVANCED. `KILLSHP` shuffles every slot above the dead one down, so the
       * ship that was behind it is now in the same slot -- and going round with the same X is
       * what processes it. A port that wrote a `for` over the slots would skip a ship for every
       * one killed.
       */
      KillShip(screen.bubble, _loop.heap, screen.heaps, screen.work, commander, spawning, slot);
    }
    else
    {
      block[SHIP_STATE] = screen.work[SHIP_STATE]; // 6502: .MA27 LDY #31 / STA (INF),Y
      ++slot;                                     // 6502: LDX XSAV / INX / JMP MAL1
    }
  }
}

/*
 * 6502: part 16, from `.MA23` -- the laser beam, the E.C.M. countdowns, and the stardust.
 *
 * Every path through parts 13 to 15 ends here, most of them by `JMP MA23`. It is written as a
 * separate function for exactly that reason: `MA23S` appears four times in the source and is
 * nothing but `JMP MA23`, which is what a shared tail looks like when the branch cannot reach.
 */
[[nodiscard]] LoopOutcome EndFlightFrameTail(FlightLoop& _loop) noexcept
{
  FlightScreen& screen = _loop.screen;

  /*
   * 6502: .MA23 LDA LAS2 / BEQ MA16 / LDA LASCT / CMP #8 / BCS MA16 / JSR LASLI2 / LDA #0 /
   * STA LAS2.
   *
   * `LAS2` is "there is a beam on screen" and `LASCT` is how long it has left. Below eight the
   * beam is rubbed out by drawing it again -- `LASLI2` is `LASLI` without the firing -- so the
   * shot is visible for a fixed number of frames however long the trigger is held.
   */
  if (screen.status.viewLaser != 0u && screen.status.laserCount < LASER_ERASE_LIMIT)
  {
    (void)DrawLaserLines(screen.canvas, screen.draw, _loop.burst, screen.view);
    screen.status.viewLaser = 0u;
  }

  /*
   * 6502: .MA16 LDA ECMP / BEQ MA69 / JSR DENGY / BEQ MA70 / .MA69 LDA ECMA / BEQ MA66 /
   * DEC ECMA / BNE MA66 / .MA70 JSR ECMOF.
   *
   * TWO COUNTDOWNS AND ONE OFF SWITCH. `ECMP` says the E.C.M. is ours, and while it is the banks
   * pay for it a unit a frame -- `DENGY` returning zero means the banks are empty, which turns
   * the E.C.M. off in the middle of a burst. `ECMA` is the burst's own timer and reaching zero
   * turns it off the ordinary way.
   */
  bool stop = false;

  if (screen.status.ecmOurs != 0u)
  {
    stop = DrainEnergy(screen.status); // 6502: JSR DENGY / BEQ MA70
  }

  if (!stop && screen.status.ecmCountdown != 0u)
  {
    screen.status.ecmCountdown = static_cast<std::uint8_t>(screen.status.ecmCountdown - 1u);
    stop = screen.status.ecmCountdown == 0u;
  }

  if (stop)
  {
    StopEcm(screen.canvas, screen.status, _loop.effects); // 6502: .MA70 JSR ECMOF
  }

  /*
   * 6502: .MA66 LDA QQ11 / BNE oh / JMP STARS -- and `oh` is an `RTS` thirty-three bytes further
   * on, borrowed from another routine. So a chart on screen ends the frame with the stardust
   * left exactly where it was.
   */
  if (screen.view == 0u)
  {
    MoveStardust(screen.canvas, screen.draw, screen.math, screen.flight, screen.dust, screen.rng,
                 screen.spaceView);
  }

  return LoopOutcome::Continued;
}

LoopOutcome EndFlightFrame(FlightLoop& _loop) noexcept
{
  FlightScreen& screen = _loop.screen;
  CommanderBlock& commander = screen.commander;

  /*
   * ---- part 13: the bomb, the shields and the banks -----------------------------------------
   *
   * 6502: .MA18 LDA BOMB / BPL MA77 / ASL BOMB / BMI MA77 / JSR BOMBOFF.
   *
   * The bomb is a countdown kept as a shift register: part 3 doubles it when the key is pressed
   * and this doubles it again every frame, so it burns for as many frames as it has bits left and
   * ends when the top bit falls off.
   */
  if ((commander.At(Field::EnergyBomb) & 0x80u) != 0u)
  {
    commander.At(Field::EnergyBomb) =
      static_cast<std::uint8_t>(commander.At(Field::EnergyBomb) << 1u);

    if ((commander.At(Field::EnergyBomb) & 0x80u) == 0u)
    {
      StopEnergyBomb(screen.screen);
    }
  }

  // 6502: .MA77 LDA MCNT / AND #7 / BNE MA22 -- seven frames in eight skip straight to part 15.
  const std::uint8_t counter = static_cast<std::uint8_t>(screen.flight.mainLoopCounter & 31u);

  if ((screen.flight.mainLoopCounter & 7u) == 0u)
  {
    /*
     * 6502: LDX ENERGY / BPL b -- the shields are fed FROM the banks, so they only recharge while
     * the banks are at least half full. `SHD` itself takes a unit of energy per shield (§6.83).
     */
    if ((screen.status.energy & 0x80u) != 0u)
    {
      screen.status.aftShield = RechargeShield(screen.status, screen.status.aftShield);
      screen.status.forwardShield = RechargeShield(screen.status, screen.status.forwardShield);
    }

    /*
     * 6502: .b SEC / LDA ENGY / ADC ENERGY / BCS P%+5 / STA ENERGY.
     *
     * The `SEC` is the recharge: a commander with no energy unit still gains one point every
     * eighth frame. And the overflow branch SKIPS the store rather than clamping, so banks that
     * would pass 255 are left exactly where they were.
     */
    const AddResult banks =
      AddWithCarry(commander.At(Field::EnergyUnit), screen.status.energy, true);
    if (!banks.carry)
    {
      screen.status.energy = banks.value;
    }

    /*
     * ---- part 14: bringing the space station back --------------------------------------------
     *
     * 6502: LDA MJ / BNE MA23S / LDA MCNT / AND #31 / BNE MA93.
     *
     * Once every thirty-two frames, and only when the station is NOT in the bubble, the loop
     * checks whether the planet is close enough to have one -- and `MAS1` is called three times
     * to DOUBLE the planet's coordinates into `INWK`, so the test is run at twice the distance.
     */
    if (screen.status.midJump == 0u && counter == 0u
        && screen.bubble.counts[SHIP_TYPE_STATION] == 0u
        && LargestAxis(screen.bubble, 0u) == 0u)
    {
      // 6502: LDX #28 / .MAL4 LDA K%,X / STA INWK,X / DEX / BPL MAL4 -- 29 bytes, not the block.
      for (std::size_t byte = 0; byte < 29u; ++byte)
      {
        screen.work[byte] = screen.bubble.blocks[0][byte];
      }

      // 6502: INX / LDY #9 / JSR MAS1 / BNE MA23S, and twice more at (3, 11) and (6, 13).
      // The `&&`s short-circuit and have to: each `MAS1` DOUBLES the coordinate it reads, in
      // place, so a second call after a non-zero answer would move the planet twice.
      const bool ahead = DoubleAndAddCoordinate(screen.work, screen.math, 9u, 0u) == 0u
                         && DoubleAndAddCoordinate(screen.work, screen.math, 11u, 3u) == 0u
                         && DoubleAndAddCoordinate(screen.work, screen.math, 13u, 6u) == 0u;

      if (ahead && WithinRange(screen.work, STATION_SPAWN_RANGE))
      {
        EraseSun(screen.canvas, screen.heaps, screen.math, screen.draw); // 6502: JSR WPLS
        _loop.effects.SpawnStation();                                    // 6502: JSR NWSPS
      }
    }

    return EndFlightFrameTail(_loop);
  }

  /*
   * ---- part 15: one job every sixteen frames -----------------------------------------------
   *
   * 6502: .MA22 LDA MJ / BNE MA23S / LDA MCNT / AND #31 / .MA93 CMP #10 / BNE MA29.
   *
   * `MA93` is entered from part 14 as well, with the same masked counter in A -- so the three
   * jobs below run on steps 10, 15 and 20 of a thirty-two step cycle whichever way in it came.
   */
  if (screen.status.midJump != 0u)
  {
    return EndFlightFrameTail(_loop);
  }

  if (counter == STEP_ENERGY_CHECK)
  {
    /*
     * 6502: LDA #50 / CMP ENERGY / BCC P%+6 / ASL A / JSR MESS.
     *
     * AND `P%+6` SKIPS BOTH INSTRUCTIONS, not just the shift. The branch is two bytes and `ASL A`
     * plus `JSR MESS` is four, so healthy banks send no message at all -- the fifty is a
     * threshold that happens to be half of the token, and the token itself is only ever 100.
     * Reading it as "50 or 100" gives a warning every sixteenth frame for the whole game.
     */
    if (ENERGY_WARNING >= screen.status.energy)
    {
      ShowMessage(screen.canvas, screen.printer, screen.text, screen.extended, screen.message,
                  static_cast<std::uint8_t>(ENERGY_WARNING << 1u), screen.view);
    }

    /*
     * 6502: LDY #&FF / STY ALTIT / INY / JSR m / BNE MA23 -- the altitude is 255 until proved
     * otherwise, so a planet too far away in any axis leaves the dial full.
     */
    screen.status.altitude = 0xFFu;

    if (LargestAxis(screen.bubble, 0u) == 0u)
    {
      // 6502: JSR MAS3 / BCS MA23 -- and the carry is `MAS3`'s saturation, not a comparison.
      const std::uint8_t squares = SumOfSquares(screen.bubble, screen.math, 0u);
      if (squares != 0xFFu)
      {
        // 6502: SBC #36 / BCC MA28 -- inside the planet's own radius, so this is the ground.
        const SubResult above = SubtractWithCarry(squares, ALTITUDE_PLANET_RADIUS, true);
        if (!above.carry)
        {
          return LoopOutcome::Died; // 6502: .MA28 JMP DEATH
        }

        // 6502: STA R / JSR LL5 / LDA Q / STA ALTIT.
        screen.math.r = above.value;
        (void)SquareRoot(screen.math);
        screen.status.altitude = screen.math.q;
      }
    }
  }
  else if (counter == STEP_DOCKING_REMINDER)
  {
    // 6502: .MA29 CMP #15 / BNE MA33 / LDA auto / BEQ MA23 / LDA #123 / BNE MA34.
    if (_loop.control.dockingComputer != 0u)
    {
      ShowMessage(screen.canvas, screen.printer, screen.text, screen.extended, screen.message,
                  MESSAGE_DOCKING_ON, screen.view);
    }
  }
  else if (counter == STEP_CABIN_TEMPERATURE)
  {
    /*
     * 6502: .MA33 CMP #20 / BNE MA23 / LDA #30 / STA CABTMP / LDA SSPR / BNE MA23.
     *
     * Thirty is room temperature and it is written unconditionally, so the sun's contribution
     * below is a replacement rather than an increase. Inside station range there is no sun to
     * feel, which is why the check comes second.
     */
    screen.status.cabinTemperature = CABIN_BASE;

    if (screen.bubble.counts[SHIP_TYPE_STATION] != 0u || LargestAxis(screen.bubble, 1u) != 0u)
    {
      return EndFlightFrameTail(_loop);
    }

    /*
     * 6502: JSR MAS3 / EOR #%11111111 / ADC #30 / STA CABTMP / BCS MA28.
     *
     * The temperature is thirty MINUS the distance squared, written as a negate-and-add on
     * `MAS3`'s exit carry -- and the carry OUT is death: an overflow here means the sum passed
     * 255, which is the sun.
     */
    const std::uint8_t squares = SumOfSquares(screen.bubble, screen.math, 1u);
    const AddResult heat =
      AddWithCarry(static_cast<std::uint8_t>(squares ^ 0xFFu), CABIN_BASE, false);
    screen.status.cabinTemperature = heat.value;

    if (heat.carry)
    {
      return LoopOutcome::Died; // 6502: BCS MA28
    }

    // 6502: CMP #224 / BCC MA23 -- below this the sun is just warm and nothing else happens.
    if (heat.value < CABIN_SCOOPING)
    {
      return EndFlightFrameTail(_loop);
    }

    /*
     * 6502: CMP #240 / BCC nokilltr / LDA #%101 / JSR SETL1 / LDA VIC+&15 / AND #%00000011 /
     * STA VIC+&15 / LDA #%100 / JSR SETL1 / LSR TRIBBLE+1 / ROR TRIBBLE.
     *
     * THE TRUMBLES COOK. The sprite write is bracketed by two raster-mode changes because the
     * sprites belong to the interrupt handler, and the population is HALVED as a sixteen-bit
     * shift -- so they die off exponentially rather than all at once.
     */
    if (heat.value >= CABIN_TRUMBLE_DEATH)
    {
      screen.sight.SetRasterMode(RASTER_TRUMBLE_KILL);
      screen.sight.MaskSprites(SPRITES_KEEP);
      screen.sight.SetRasterMode(RASTER_TRUMBLE_DONE);

      const std::size_t tribbles = static_cast<std::size_t>(Field::Tribbles);
      const ShiftResult high = RotateRight(commander.bytes[tribbles + 1u], false);
      commander.bytes[tribbles + 1u] = high.value;
      commander.bytes[tribbles] = RotateRight(commander.bytes[tribbles], high.carry).value;
    }

    /*
     * 6502: .nokilltr LDA BST / BEQ MA23 / LDA DELT4+1 / LSR A / ADC QQ14 / CMP #70 / BCC P%+4 /
     * LDA #70 / STA QQ14 / LDA #160 / .MA34 JSR MESS.
     *
     * Fuel scooping, and the amount is the player's own SPEED: `DELT4+1` is `DELTA` shifted up
     * six places, halved again here. Flying into the sun faster fills the tank faster.
     */
    if (commander.At(Field::FuelScoops) != 0u)
    {
      const ShiftResult scooped = RotateRight(screen.flight.delt4Next, false);
      const AddResult tank = AddWithCarry(scooped.value, commander.At(Field::Fuel), scooped.carry);

      commander.At(Field::Fuel) = (tank.value < FUEL_MAXIMUM) ? tank.value : FUEL_MAXIMUM;

      ShowMessage(screen.canvas, screen.printer, screen.text, screen.extended, screen.message,
                  MESSAGE_SCOOPS_ON, screen.view);
    }
  }

  return EndFlightFrameTail(_loop);
}

LoopOutcome MainFlightLoop(FlightLoop& _loop) noexcept
{
  const LoopOutcome opening = BeginFlightFrame(_loop);
  if (opening != LoopOutcome::Continued)
  {
    return opening;
  }

  const LoopOutcome ships = MoveEveryShip(_loop);
  if (ships != LoopOutcome::Continued)
  {
    return ships;
  }

  return EndFlightFrame(_loop);
}

} // namespace Elite
