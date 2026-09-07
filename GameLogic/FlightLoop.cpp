#include "pch.h"

#include "FlightLoop.h"

#include "EliteTypes.h"
#include "ShipBlueprint.h"
#include "Lasers.h"
#include "Messages.h"
#include "Music.h"
#include "Combat.h"
#include "Market.h"
#include "PlanetDraw.h"
#include "Scanner.h"
#include "ShipDraw.h"
#include "Spawn.h"
#include "Tactics.h"
#include "Stardust.h"

#include <algorithm>

namespace Elite
{

  std::uint8_t DoubleAndAddCoordinate(Ship& _work, std::uint8_t _from, std::uint8_t _to) noexcept
  {
    const auto& from = _work.ComponentAt(_from); // 6502: INWK,Y / INWK+1,Y
    auto& to = _work.PositionAt(_to);            // 6502: INWK,X to INWK+2,X
    // 6502: LDA INWK,Y / ASL A / STA K+1 / LDA INWK+1,Y / ROL A / STA K+2.
    KBlock k;
    const ShiftResult low = RotateLeftValue(from.lo, false);
    k.mid = low.value;

    const ShiftResult high = RotateLeftValue(from.hi, low.carry);
    k.high = high.value;

    // 6502: LDA #0 / ROR A / STA K+3 -- the bit that fell off the top becomes the sign byte, so the
    // doubling cannot overflow: it widens instead.
    k.top = RotateRight(0u, high.carry).value;

    // The exit carry is live only on `VCSUB`'s path out to `TA64` (§6.126). `MVT1` reads `K+3`
    // and stores, so the flag dies here.
    k = AddShipCoordinateToK(_work, k, _to).value; // 6502: JSR MVT3

    // 6502: STA INWK+2,X -- and A is `K+3`, because every path through `MVT3` ends `STA K+3`.
    to = k.Coordinate(); // 6502: LDY K+1 / STY INWK,X / LDY K+2 / STY INWK+1,X

    return static_cast<std::uint8_t>(k.top & 0x7Fu); // 6502: AND #%01111111
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
    const Ship& block = _bubble.blocks[_slot];
    const std::uint8_t together = static_cast<std::uint8_t>(_a | block.x.sgn | block.y.sgn | block.z.sgn);

    return static_cast<std::uint8_t>(together & 0x7Fu);
  }

  std::uint8_t SumOfSquares(const Bubble& _bubble, std::uint8_t _slot) noexcept
  {
    const Ship& block = _bubble.blocks[_slot];

    // 6502: LDA K%+1,Y / JSR SQUA2 / STA R.
    std::uint8_t r = SquareUnsigned(block.x.hi).high;

    // 6502: LDA K%+4,Y / JSR SQUA2 / ADC R / BCS MA30 -- the `ADC` reads `SQUA2`'s exit carry, and
    // that carry is never set (§6.70), so this is the plain addition it looks like.
    const Product second = SquareUnsigned(block.y.hi);
    const AddResult sum = AddWithCarry(second.high, r, second.carry);
    if (sum.carry)
    {
      return 0xFFu; // 6502: MA30 -- LDA #&FF
    }

    r = sum.value; // 6502: STA R

    // 6502: LDA K%+7,Y / JSR SQUA2 / ADC R / BCC P%+4 -- and the branch skips the saturation.
    const Product third = SquareUnsigned(block.z.hi);
    const AddResult total = AddWithCarry(third.high, r, third.carry);

    return total.carry ? 0xFFu : total.value;
  }

  std::uint8_t LargestShipAxis(const Ship& _work, std::uint8_t _a) noexcept
  {
    // 6502: ORA INWK+1 / ORA INWK+4 / ORA INWK+7 -- no mask, unlike `MAS2`.
    return static_cast<std::uint8_t>(_a | _work.x.hi | _work.y.hi | _work.z.hi);
  }

  std::uint8_t DampTowardsCentre(std::uint8_t _value, std::uint8_t _dockingComputer, std::uint8_t _dampingDisabled) noexcept
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

  Drop PlanItems(ShipType _type, std::uint8_t _count, bool _carryIn) noexcept
  {
    // 6502: .SPIN2 STA CNT, which sets no flags, and `LDA #0` is the AI byte every child gets.
    // `CNT` is `PerformDrop`'s loop counter now; what this answers is the number that goes in it.
    //
    // 6502: .spl BEQ oh -- on the caller's Z flag, which every caller has just set from the count,
    // and `oh` is a bare `RTS`. A count of zero is that branch, and `carryIfNone` is what it hands
    // back (M2-d).
    return {_type, _count, 0u, _carryIn};
  }

  bool PerformDrop(Universe& _universe, const Drop& _drop) noexcept
  {
    // 6502: .spl BEQ oh -- the test that runs once on entry and never again, because the loop's
    // back edge is `BNE spl+2`.
    if (_drop.count == 0u)
    {
      return _drop.carryIfNone;
    }

    std::uint8_t cnt = _drop.count;
    for (;;)
    {
      /*
       * 6502: LDA #0 / JSR SFS1 with `INF` at the ship being processed, which is `XSAV`'s slot.
       *
       * `SFS1` ends `JSR NWSHP` followed by nothing but pulls and stores, so what it leaves in the
       * carry is `NWSHP`'s: `NW3`'s `CLC` for a full bubble and `NWL3`'s `SEC` for a ship that was
       * made (M2-d). Until M4-a-1 this was a seam that answered a fixed boolean, so a full bubble
       * could not be observed here at all.
       */
      const bool carry = SpawnChildShip(_universe.bubble, _universe.work, _universe.rng, _universe.flight.slot, _universe.flight.type,
                                        _drop.aiFlag, _drop.type, _universe.flight.blueprint)
                           .created;

      cnt = static_cast<std::uint8_t>(cnt - 1u); // 6502: DEC CNT
      if (cnt == 0u)                             // 6502: BNE spl+2
      {
        return carry;
      }
    }
  }

  Drop PlanDebris(Rng& _rng, const Blueprint& _blueprint, ShipType _type, bool _carryIn) noexcept
  {
    // 6502: JSR DORND / BPL oh -- and nothing else in the routine looks at the roll's low bits
    // except as a count, so half of all calls do nothing.
    const RngResult roll = _rng.Next(_carryIn);
    if ((roll.value & 0x80u) == 0u)
    {
      // 6502: BPL oh -- a bare `RTS`, so the carry the caller gets is `DORND`'s own (M2-d). It is
      // the same answer as a count of zero and it is spelled the same way, which is not a
      // coincidence: `oh` is the one label both paths reach.
      return {_type, 0u, 0u, roll.carry};
    }

    /*
     * 6502: TYA / TAX / LDY #0 / AND (XX0),Y / AND #15.
     *
     * `TYA / TAX` reads as "copy Y into X", and it is -- but it goes THROUGH A, and the `AND` two
     * instructions later reads that A rather than the random number `DORND` left there. So the
     * count is the ship TYPE masked by the blueprint's first byte; the roll decides only whether
     * anything is dropped at all. The oracle caught the port doing it the obvious way (§6.74).
     */
    const std::uint8_t capped = static_cast<std::uint8_t>(Byte(_type) & _blueprint.cargo & 0x0Fu);

    // 6502: and it falls into SPIN2 -- with `DORND`'s carry, which `AND` did not touch.
    return PlanItems(_type, capped, roll.carry);
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

  bool WithinRange(const Ship& _work, std::uint8_t _limit) noexcept
  {
    // 6502: CMP INWK+1 / BCC FA1 / CMP INWK+4 / BCC FA1 / CMP INWK+7 / .FA1 RTS -- and the carry
    // out of the LAST compare reached is the answer, which is why the two early exits both leave a
    // clear one.
    if (_limit < _work.x.hi || _limit < _work.y.hi)
    {
      return false;
    }

    return _limit >= _work.z.hi;
  }

  bool IsHit(const Ship& _work, const Blueprint& _blueprint, ShipType _type) noexcept
  {
    // 6502: CLC / LDA INWK+8 / BNE HI1 -- the z sign byte, and anything but zero means the ship is
    // not close enough in front of us to have been hit.
    if (_work.z.sgn != 0u)
    {
      return false;
    }

    // 6502: LDA TYPE / BMI HI1 -- the planet and the sun are not shootable.
    if (IsBody(_type))
    {
      return false;
    }

    // 6502: LDA INWK+31 / AND #%00100000 / ORA INWK+1 / ORA INWK+4 / BNE HI1 -- already exploding,
    // or too far off to either side. Three tests ORed into one branch.
    if (Has(_work.state, ShipStateBit::Exploding) || (_work.x.hi | _work.y.hi) != 0u)
    {
      return false;
    }

    // 6502: LDA INWK / JSR SQUA2 / STA S / LDA P / STA R. `(S R)` is this routine's own since
    // M2-c-3: nothing between the two squares and the compare below reads either byte.
    const Product across = SquareUnsigned(_work.x.lo);
    SignMag16 area{across.low, across.high}; // 6502: (S R)

    // 6502: LDA INWK+3 / JSR SQUA2 / TAX / LDA P / ADC R / STA R / TXA / ADC S / BCS TN10.
    const Product down = SquareUnsigned(_work.y.lo);
    const AddResult low = AddWithCarry(down.low, area.lo, across.carry);
    area.lo = low.value;
    const AddResult high = AddWithCarry(down.high, area.hi, low.carry);
    if (high.carry)
    {
      return false; // 6502: .TN10 CLC / RTS -- too big to compare, which is its own "no"
    }

    area.hi = high.value; // 6502: STA S

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
    const std::uint8_t target = static_cast<std::uint8_t>(_blueprint.targetArea >> 8); // 6502: (XX0),2 -- the high byte
    if (target != area.hi)
    {
      return target >= area.hi;
    }

    return static_cast<std::uint8_t>(_blueprint.targetArea & 0xFFu) >= area.lo; // 6502: (XX0),1 -- the low byte
  }

  void FireMissile(Universe& _universe, Ports& _ports) noexcept
  {
    // 6502: LDX #MSL / JSR FRS1 / BCC FR1 -- a full bubble means the missile stays on the rail.
    if (!SpawnShipAhead(_universe.bubble, _universe.work, ShipType::Missile, _universe.flight.delta, _universe.bubble.missileTarget,
                        _universe.flight.blueprint)
           .created)
    {
      // 6502: .FR1 LDA #201 / JMP MESS -- "MISSILE JAMMED".
      ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, MESSAGE_MISSILE_JAMMED,
                  _universe.view);
      return;
    }

    // 6502: LDX MSTG / JSR GINF / LDA FRIN,X / JSR ANGRY -- the TARGET's slot and type, not the
    // missile's.
    const std::uint8_t target = _universe.bubble.missileTarget;
    (void)Anger(_universe.bubble, _universe.flight, target, TypeOf(_universe.bubble.slots[target]));

    // 6502: LDY #BLACK2 / JSR ABORT -- the lock is gone and so is the indicator.
    AbortMissileLock(_universe, _universe.commander.missiles, MISSILE_NONE);

    // 6502: DEC NOMSL -- one fewer on the rail.
    _universe.commander.missiles = static_cast<std::uint8_t>(_universe.commander.missiles - 1u);

    (void)PlaySoundEffect(_universe.sound, SoundEffect::Missile, false); // 6502: LDY #sfxwhosh / JMP NOISE
  }

  /*
   * ---- part 1: the generator and the Trumbles --------------------------------------------------
   *
   * The frame's first two instructions and its ninth seam (M4-a-3). `BeginFlightFrame` was three
   * hundred and twenty-four lines with parts 1, 2, 3 and 3's tail written out inside it under
   * comment rules; the comment rules are function boundaries now, which is what the M4-a row asks
   * for by "split at their annotated parts".
   */
  void StirTheFrame(Universe& _universe) noexcept
  {
    /*
     * 6502: LDA K% / STA RAND -- the planet's own x low byte, into the generator, every frame.
     *
     * Only the FIRST of the four seed bytes, so the other three carry on from wherever the last
     * call left them: this stirs the sequence rather than resetting it.
     */
    std::array<std::uint8_t, 4> seed = _universe.rng.State();
    seed[0] = _universe.bubble.blocks[0].x.lo;
    _universe.rng.SetState(seed);

    /*
     * 6502: LDA TRIBCT / BEQ NOMVETR / JMP MVTRIBS -- and `MVTRIBS` jumps back to `NOMVETR`, so
     * this is a call written as two jumps (§6.82).
     *
     * `MoveTrumbles` was a seam here until slice 4d-a, for the same reason five others were: the
     * routine on the other side did not exist. It does now, and the seam is deleted rather than
     * answered, which is §6.73 for the ninth time.
     */
    if (_universe.trumbles.count != 0u)
    {
      MoveTrumbleSprites(_universe.trumbles, _universe.video, _universe.rng, _universe.flight.mainLoopCounter, _universe.memoryMap);
    }
  }

  /*
   * ---- part 2: the roll and the pitch, which are not the same shape ----------------------------
   *
   * The two halves are ONE function because the roll's exit carry is the pitch's `ADC #4` input
   * (§6.85) -- a flag that is live across what looks like a boundary, so a split here would need a
   * carry parameter to say what a local already says.
   */
  void TurnTheShip(Universe& _universe) noexcept
  {
    // ---- part 2: the roll ------------------------------------------------------------------------

    // 6502: LDX JSTX / JSR cntr / JSR cntr -- twice, so the roll creeps back by two per frame.
    std::uint8_t roll = _universe.control.roll;
    roll = DampTowardsCentre(roll, _universe.control.dockingComputer, _universe.options.dampingDisabled);
    roll = DampTowardsCentre(roll, _universe.control.dockingComputer, _universe.options.dampingDisabled);

    // 6502: TXA / EOR #%10000000 / TAY / AND #%10000000 / STA ALP2 / STX JSTX / EOR #%10000000 /
    // STA ALP2+1 -- the rate turned into a sign and a magnitude, and the sign kept both ways round.
    const std::uint8_t rollSigned = static_cast<std::uint8_t>(roll ^ 0x80u);
    _universe.flight.alp2 = static_cast<std::uint8_t>(rollSigned & 0x80u);
    _universe.control.roll = roll;
    _universe.flight.alp2Next = static_cast<std::uint8_t>(_universe.flight.alp2 ^ 0x80u);

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

    _universe.flight.alp1 = rollMagnitude; // 6502: STA ALP1
    _universe.flight.alpha = static_cast<std::uint8_t>(rollMagnitude | _universe.flight.alp2);

    // ---- part 2: the pitch, which is not the same shape ------------------------------------------

    // 6502: LDX JSTY / JSR cntr -- ONCE, where the roll gets two.
    std::uint8_t pitch = _universe.control.pitch;
    pitch = DampTowardsCentre(pitch, _universe.control.dockingComputer, _universe.options.dampingDisabled);

    // 6502: TXA / EOR #%10000000 / TAY / AND #%10000000 / STX JSTY / STA BET2+1 / EOR #%10000000 /
    // STA BET2 -- the two sign bytes are written the OTHER way round from the roll's.
    const std::uint8_t pitchSigned = static_cast<std::uint8_t>(pitch ^ 0x80u);
    _universe.control.pitch = pitch;
    _universe.flight.bet2Next = static_cast<std::uint8_t>(pitchSigned & 0x80u);
    _universe.flight.bet2 = static_cast<std::uint8_t>(_universe.flight.bet2Next ^ 0x80u);

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

    _universe.flight.bet1 = pitchMagnitude; // 6502: STA BET1
    _universe.flight.beta = static_cast<std::uint8_t>(pitchMagnitude | _universe.flight.bet2);
  }

  /*
   * ---- part 3: the keys ------------------------------------------------------------------------
   *
   * Answers `Escaped` for `JMP ESCAPE`, which does not come back, and `Continued` otherwise.
   */
  [[nodiscard]] LoopOutcome RunFlightKeys(Universe& _universe, Ports& _ports) noexcept
  {
    // ---- part 3: the keys ------------------------------------------------------------------------

    Commander& commander = _universe.commander;

    // 6502: LDA KY2 / BEQ MA17 / LDA DELTA / CMP #40 / BCS MA17 / INC DELTA -- forty is the ceiling.
    if (_universe.keys[KEY_SPEED_UP] != 0u && _universe.flight.delta < 40u)
    {
      _universe.flight.delta = static_cast<std::uint8_t>(_universe.flight.delta + 1u);
    }

    // 6502: .MA17 LDA KY1 / BEQ MA4 / DEC DELTA / BNE MA4 / INC DELTA -- and one is the floor, so
    // the ship never stops dead.
    if (_universe.keys[KEY_SLOW_DOWN] != 0u)
    {
      _universe.flight.delta = static_cast<std::uint8_t>(_universe.flight.delta - 1u);
      if (_universe.flight.delta == 0u)
      {
        _universe.flight.delta = 1u;
      }
    }

    // 6502: .MA4 LDA KY15 / AND NOMSL / BEQ MA20 -- the AND is the "have we got one" test, so the
    // key does nothing at all with an empty rail.
    if ((_universe.keys[KEY_UNARM_MISSILE] & commander.missiles) != 0u)
    {
      AbortMissileLock(_universe, commander.missiles, MISSILE_READY);
      (void)PlaySoundEffect(_universe.sound, SoundEffect::Boop, false); // 6502: LDY #sfxboop / JSR NOISE
      _universe.status.missileArmed = 0u;                               // 6502: LDA #0 / STA MSAR, which `ABORT` has already done
    }

    /*
     * 6502: .MA20 LDA MSTG / BPL MA25 / LDA KY14 / BEQ MA25 / LDX NOMSL / BEQ MA25 / STA MSAR /
     * LDY #YELLOW2 / JSR MSBAR.
     *
     * `MSTG` is 255 for no lock, so `BPL` skips this whenever there IS one: a missile already
     * seeking cannot be re-armed. And `STA MSAR` stores the KEY's value rather than a flag of its
     * own, which is &FF because that is what the scan writes.
     */
    if ((_universe.bubble.missileTarget & 0x80u) != 0u && _universe.keys[KEY_ARM_MISSILE] != 0u && commander.missiles != 0u)
    {
      _universe.status.missileArmed = _universe.keys[KEY_ARM_MISSILE];
      SetMissileIndicator(_universe.canvas, commander.missiles, MISSILE_ARMED);
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
    if (_universe.keys[KEY_FIRE_MISSILE] != 0u)
    {
      if ((_universe.bubble.missileTarget & 0x80u) != 0u)
      {
        checkedTheRest = false;
      }
      else
      {
        FireMissile(_universe, _ports);
      }
    }

    if (checkedTheRest)
    {
      // 6502: .MA24 LDA KY12 / BEQ MA76 / ASL BOMB / BEQ MA76 -- `BOMB` is a countdown kept as a
      // shift, so the bomb burns for as many frames as it has bits left.
      if (_universe.keys[KEY_ENERGY_BOMB] != 0u)
      {
        commander.energyBomb = static_cast<std::uint8_t>(commander.energyBomb << 1u);

        if (commander.energyBomb != 0u)
        {
          // 6502: LDY #%11010000 / STY moonflower -- the upper half of the screen changes mode, and
          // that IS the effect: no drawing is involved.
          _universe.screen.upperBitmapMode = BOMB_BITMAP_MODE;
          (void)PlaySoundEffect(_universe.sound, SoundEffect::EnergyBomb, false);
        }
      }

      // 6502: .MA76 LDA KY20 / BEQ MA78 / LDA #0 / STA auto / JSR stopbd.
      if (_universe.keys[KEY_CANCEL_DOCKING] != 0u)
      {
        _universe.control.dockingComputer = 0u;
        StopDockingMusic(_universe.music, _universe.status.titleReset, _universe.sound, _universe.memoryMap, _ports.sid);
      }

      // 6502: .MA78 LDA KY13 / AND ESCP / BEQ noescp / LDA MJ / BNE noescp / JMP ESCAPE -- and it
      // does not come back, so the frame ends here.
      if ((_universe.keys[KEY_ESCAPE_POD] & commander.escapePod) != 0u && _universe.status.midJump == 0u)
      {
        return LoopOutcome::Escaped;
      }

      // 6502: .noescp LDA KY18 / BEQ P%+5 / JSR WARP.
      if (_universe.keys[KEY_WARP] != 0u)
      {
        Warp(_universe, _ports);
      }

      // 6502: LDA KY17 / AND ECM / BEQ MA64 / LDA ECMA / BNE MA64 / DEC ECMP / JSR ECBLB2 -- and
      // `DEC ECMP` on a zero byte is what makes it &FF, which is "ours" (§6.71's pair).
      if ((_universe.keys[KEY_ECM] & commander.ecm) != 0u && _universe.status.ecmCountdown == 0u)
      {
        _universe.status.ecmOurs = static_cast<std::uint8_t>(_universe.status.ecmOurs - 1u);
        /*
         * 6502: DEC ECMP / JSR ECBLB2, and the carry handed on is NOT KNOWN HERE (§6.118).
         *
         * `DEC`, `LDA`, `AND` and `BEQ` above this all leave the carry alone, so the flag that
         * reaches `NOISE` was set somewhere further back in the frame -- possibly inside `WARP`,
         * three instructions earlier, which this port calls through a seam. The port models no
         * carry across the flight loop, so false is what it can honestly supply, and the sound
         * comparison excludes this effect by name rather than pretending to agree.
         */
        StartEcm(_universe.canvas, _universe.status, _universe.sound, false);
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
      static_cast<std::uint8_t>((_universe.keys[KEY_DOCKING_COMPUTER] & commander.dockingComputer) ^ _universe.keys[KEY_PITCH_UP]);
    if ((_universe.keys[KEY_DOCKING_COMPUTER] & commander.dockingComputer) != 0u && requested != 0u)
    {
      _universe.control.dockingComputer = requested;
      StartDockingMusic(_universe.music, _universe.memoryMap, _ports.sid);
    }

    return LoopOutcome::Continued;
  }

  /*
   * ---- part 3's tail: the guns -----------------------------------------------------------------
   *
   * 6502: .MA68 -- every path through part 3 reaches this label, including the one `BMI MA64` cut
   * five keys short of.
   */
  [[nodiscard]] LoopOutcome FireTheGuns(Universe& _universe, Ports& _ports) noexcept
  {
    Commander& commander = _universe.commander;

    // ---- part 3's tail: the guns -----------------------------------------------------------------

    _universe.status.laserPower = 0u; // 6502: .MA68 LDA #0 / STA LAS
    _universe.flight.delt4 = 0u;      // 6502: STA DELT4

    // 6502: LDA DELTA / LSR A / ROR DELT4 / LSR A / ROR DELT4 / STA DELT4+1 -- the speed as a
    // sixteen-bit value the stardust subtracts, which is DELTA shifted up six places.
    {
      ShiftResult step = RotateRight(_universe.flight.delta, false);
      ShiftResult low = RotateRight(_universe.flight.delt4, step.carry);
      step = RotateRight(step.value, false);
      low = RotateRight(low.value, step.carry);
      _universe.flight.delt4 = low.value;
      _universe.flight.delt4Next = step.value;
    }

    // 6502: LDA LASCT / BNE MA3 -- a pulse laser's countdown, which is why it cannot be held down.
    if (_universe.status.laserCount != 0u)
    {
      return LoopOutcome::Continued;
    }

    // 6502: LDA KY7 / BEQ MA3 / LDA GNTMP / CMP #242 / BCS MA3 -- and 242 is where the gun jams.
    if (_universe.keys[KEY_FIRE] == 0u || _universe.status.laserTemperature >= 242u)
    {
      return LoopOutcome::Continued;
    }

    // 6502: LDX VIEW / LDA LASER,X / BEQ MA3 -- this view's laser, if it has one.
    const std::uint8_t fitted = commander.lasers[_universe.spaceView];
    if (fitted == 0u)
    {
      return LoopOutcome::Continued;
    }

    // 6502: PHA / AND #%01111111 / STA LAS / STA LAS2 -- the power without its top bit, which is
    // what the damage arithmetic uses.
    _universe.status.laserPower = static_cast<std::uint8_t>(fitted & 0x7Fu);
    _universe.status.viewLaser = _universe.status.laserPower;

    /*
     * 6502: LDY #sfxplas / PLA / PHA / BMI bmorarm / CMP #Mlas / BNE P%+4 / LDY #sfxmlas /
     * BNE custard / .bmorarm CMP #Armlas / BEQ P%+5 / LDY #sfxblas / EQUB &2C / LDY #sfxalas.
     *
     * The `EQUB &2C` is `BIT abs` again, swallowing the `LDY #sfxalas` so that the beam laser's
     * sound survives (§6.79). Sixth time in this port.
     */
    SoundEffect sound = SoundEffect::PulseLaser;
    if ((fitted & 0x80u) != 0u)
    {
      sound = (fitted == LASER_POWER_MILITARY) ? SoundEffect::MilitaryLaser : SoundEffect::BeamLaser;
    }
    else if (fitted == LASER_POWER_MINING)
    {
      sound = SoundEffect::MiningLaser;
    }

    /*
     * 6502: .custard JSR NOISE -- and `LASLI` opens `JSR DORND`, whose `ROL A` reads the carry
     * this leaves, so the sound's own outcome shifts the burst by a pixel (§6.86).
     *
     * THE CARRY GOING IN IS A COMPARISON'S, and both paths to `.custard` come off a `CMP`: the
     * beam half arrives through `CMP #Armlas` and the rest through `CMP #Mlas`, so it is the
     * laser power measured against whichever constant that branch tested. Nothing between the
     * compare and the call touches the flag -- `LDY` does not, and neither does the `EQUB &2C`
     * that swallows one of the loads. A silent build hands this straight back (§6.99).
     */
    const bool carryIn = ((fitted & 0x80u) != 0u) ? (fitted >= LASER_POWER_MILITARY) : (fitted >= LASER_POWER_MINING);
    const bool heard = PlaySoundEffect(_universe.sound, sound, carryIn).carry;

    // 6502: JSR LASLI -- the burst itself, which draws and heats the gun.
    (void)FireLaser(_universe.canvas, _universe.rng, _universe.burst, _universe.status, _universe.view, heard);

    // 6502: PLA / BPL ma1 / LDA #0 / .ma1 AND #%11111010 / STA LASCT -- a beam laser gets no
    // countdown at all, which is what lets it be held down.
    const std::uint8_t countdown = ((fitted & 0x80u) != 0u) ? std::uint8_t{0u} : fitted;
    _universe.status.laserCount = static_cast<std::uint8_t>(countdown & 0xFAu);

    return LoopOutcome::Continued;
  }

  LoopOutcome BeginFlightFrame(Universe& _universe, Ports& _ports) noexcept
  {
    StirTheFrame(_universe); // 6502: part 1
    TurnTheShip(_universe);  // 6502: part 2

    // 6502: part 3 -- and `JMP ESCAPE` is the one exit it has, which is why this is not a `void`.
    const LoopOutcome keys = RunFlightKeys(_universe, _ports);
    if (keys != LoopOutcome::Continued)
    {
      return keys;
    }

    return FireTheGuns(_universe, _ports); // 6502: .MA68 -- part 3's tail
  }

  namespace
  {

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

    /// 6502: LDA #192 / JSR FAROF2 -- the station is respawned when the planet is inside this.
    inline constexpr std::uint8_t STATION_SPAWN_RANGE = 192;

    /// 6502: LDA VIC+&15 / AND #%00000011 -- everything but the two lowest sprites goes off.
    inline constexpr std::uint8_t SPRITES_KEEP = 0x03;

    /// 6502: LDA LASCT / CMP #8 -- above this the beam is still being drawn and is left alone.
    inline constexpr std::uint8_t LASER_ERASE_LIMIT = 8;

    /// 6502: ASL x / SEC / ROR x -- set bit 7 without touching the other seven.
    [[nodiscard]] std::uint8_t MarkKilled(std::uint8_t _state) noexcept
    {
      return With(_state, ShipStateBit::Killed);
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

  [[nodiscard]] LaserHit ApplyLaserHit(Universe& _universe, Ports& _ports, const Blueprint& _blueprint, ShipType _type) noexcept
  {
    (void)PlayHitSound(_universe.work, _universe.sound); // 6502: LDX #15 / JSR EXNO

    // 6502: LDA TYPE / CMP #SST / BEQ MA14+2.
    if (_type == ShipType::Station)
    {
      return {false, _universe.work.energy};
    }

    std::uint8_t power = _universe.status.laserPower;

    /*
     * 6502: CMP #CON / BCC BURN / LDA LAS / CMP #(Armlas AND 127) / BNE MA14+2 / LSR LAS / LSR LAS.
     *
     * A Constrictor or anything above it is immune to everything but a military laser, and takes a
     * QUARTER of even that. `Armlas AND 127` is 23, which is the power `MA68` stored after masking
     * the top bit off -- so the comparison is against the masked value and not against `Armlas`.
     */
    if (_type >= ShipType::Constrictor)
    {
      if (power != static_cast<std::uint8_t>(LASER_POWER_MILITARY & 0x7Fu))
      {
        return {false, _universe.work.energy};
      }

      power = static_cast<std::uint8_t>(power >> 1u);
      power = static_cast<std::uint8_t>(power >> 1u);
      _universe.status.laserPower = power; // 6502: LSR LAS twice, in place
    }

    // 6502: .BURN LDA INWK+35 / SEC / SBC LAS / BCS MA14 -- it survived with what is left.
    const SubResult left = SubtractWithCarry(_universe.work.energy, power, true);
    if (left.carry)
    {
      return {true, left.value};
    }

    _universe.work.state = With(_universe.work.state, ShipStateBit::Killed);

    /*
     * 6502: LDA TYPE / CMP #AST / BNE nosp / LDA LAS / CMP #Mlas / BNE nosp / JSR DORND / LDX #SPL /
     * AND #3 / JSR SPIN2.
     *
     * ONLY a mining laser splits an asteroid, and only an asteroid splits. Everything else drops
     * whatever `SPIN` decides from its blueprint, which is not random at all (§6.74).
     *
     * THE THREE ROLLS BELOW RUN ON A CARRY THE TWO COMPARES SET, and the port passed `false` to
     * all three until M2-d (§8). The flag arriving here is CLEAR -- `ASL INWK+31 / SEC / ROR
     * INWK+31` above shifts the zero the `ASL` put in bit 0 straight back out -- and then each
     * `CMP` overwrites it, so what `SPIN` and the splinter roll rotate into `DORND` is "was the
     * type at least an asteroid" or "was the laser at least a mining one", and the second `SPIN`
     * runs on what the first one left. It changes the generator, so it changes what is dropped.
     */
    bool carry;
    if (_type != ShipType::Asteroid)
    {
      carry = Byte(_type) >= Byte(ShipType::Asteroid); // 6502: CMP #AST, then `BNE nosp`
    }
    else if (power != LASER_POWER_MINING)
    {
      carry = power >= LASER_POWER_MINING; // 6502: CMP #Mlas, then `BNE nosp`
    }
    else
    {
      // Both compares were EQUAL, so both set the carry: the roll below sees it set.
      const RngResult roll = _universe.rng.Next(true);
      carry = PerformDrop(_universe, PlanItems(ShipType::Splinter, static_cast<std::uint8_t>(roll.value & 3u), roll.carry));
    }

    // 6502: .nosp LDY #PLT / JSR SPIN / LDY #OIL / JSR SPIN -- both, in that order, every time,
    // and the second on the carry the first left.
    carry = PerformDrop(_universe, PlanDebris(_universe.rng, _blueprint, ShipType::AlloyPlate, carry));
    static_cast<void>(PerformDrop(_universe, PlanDebris(_universe.rng, _blueprint, ShipType::Canister, carry)));

    // 6502: LDX TYPE / JSR EXNO2 -- and what `.MA14` stores is what NOISE2 left in A (§6.86's
    // dependency again: the dead ship's energy byte comes out of the sound system).
    return {true, RecordKill(_universe, _ports, _type)};
  }

  /*
   * ---- part 7: is it touching us? -------------------------------------------------------------
   *
   * 6502: LDA INWK+31 / AND #%10100000 / JSR MAS4 / BNE MA65.
   *
   * ONE TEST DOES FOUR JOBS. `MAS4` ORs the three high bytes into whatever it is handed, so seeding
   * it with the "exploding or dead" bits means a ship that is far away on any axis, or already
   * exploding, or already killed, all fail together. What survives is close and intact.
   *
   * AND THE ANSWER IS ONE OF FOUR AND NOT THREE BOOLEANS (M4-a-2). It was `docking`, `scoopable`
   * and `collision` declared above the block and set inside it, and the three are mutually
   * exclusive by construction: a station takes the first branch and nothing else can. Part 9 ended
   * by clearing the other two, which were already clear on every path that reached it -- three dead
   * stores that only a type could say were dead.
   */
  enum class Contact : std::uint8_t
  {
    Clear,     ///< 6502: MA65 -- far away on some axis, or exploding, or already killed
    Docking,   ///< 6502: ISDK -- the station, close enough to try
    Scoopable, ///< 6502: the `BST AND INWK+5` path -- under us, with fuel scoops fitted
    Collision, ///< 6502: MA58 -- close, intact, and not one of the above
  };

  [[nodiscard]] Contact TestContact(const Ship& _work, const Commander& _commander, ShipType _type, bool _isBody) noexcept
  {
    const std::uint8_t seed = static_cast<std::uint8_t>(_work.state & Mask(ShipStateBit::Killed, ShipStateBit::Exploding));

    if (LargestShipAxis(_work, seed) != 0u)
    {
      return Contact::Clear;
    }

    // 6502: LDA INWK / ORA INWK+3 / ORA INWK+6 / BMI MA65 -- and A survives to the `AND` below.
    const std::uint8_t low = static_cast<std::uint8_t>(_work.x.lo | _work.y.lo | _work.z.lo);
    if ((low & 0x80u) != 0u || _isBody)
    {
      return Contact::Clear;
    }

    if (_type == ShipType::Station)
    {
      return Contact::Docking; // 6502: CPX #SST / BEQ ISDK
    }

    if ((low & 0xC0u) != 0u || _type == ShipType::Missile)
    {
      return Contact::Clear;
    }

    /*
     * 6502: LDA BST / AND INWK+5 / BPL MA58.
     *
     * `BST` is &FF with fuel scoops fitted and byte 5 is the ship's y sign, so the `AND` is "we have
     * scoops AND it is below us" -- scooping only works on things that come up from underneath.
     * Anything else at this range is a collision.
     */
    const std::uint8_t under = static_cast<std::uint8_t>(_commander.fuelScoops & _work.y.sgn);
    return ((under & 0x80u) != 0u) ? Contact::Scoopable : Contact::Collision;
  }

  /*
   * ---- part 8: scooping it up -------------------------------------------------------------------
   *
   * 6502: CPX #OIL / BEQ oily / LDY #0 / LDA (XX0),Y / LSR A x4 / BEQ MA58 / ADC #1.
   *
   * AND THE `ADC` TAKES THE FOURTH `LSR`'s CARRY, which is bit 3 of the blueprint's first byte.
   * `BEQ MA58` sits between them and tests A without touching the flags, so what lands in the hold
   * is the top nibble plus one plus a bit of the bottom nibble (§6.89). A cargo canister skips all
   * of that and rolls for a random item instead.
   */
  enum class ScoopResult : std::uint8_t
  {
    Stowed,   ///< it is in the hold and `NEWB` bit 7 is set, so part 12 takes the ship away
    HoldFull, ///< 6502: BCS MA59 -- `tnpr1` said no, which is a bounce rather than damage
    Crashed,  ///< 6502: BEQ MA58 -- nothing worth scooping, so it is a collision after all
  };

  [[nodiscard]] ScoopResult ScoopCargo(Universe& _universe, Ports& _ports, ShipType _type) noexcept
  {
    Commander& commander = _universe.commander;
    std::uint8_t item = 0;

    if (_type == ShipType::Canister)
    {
      // 6502: .oily JSR DORND / AND #7 -- and the carry it rolls in is SET, because the only way
      // here is `CPX #OIL / BEQ oily`, and a compare that finds its operand equal sets it. The
      // port passed a clear one until 2026-09-06, hidden behind a comparison that skipped the
      // generator on every frame that also seeded a cloud (§6.157).
      item = static_cast<std::uint8_t>(_universe.rng.Next(true).value & 7u);
    }
    else
    {
      const std::uint8_t nibble = _universe.flight.blueprint->cargo;
      const std::uint8_t worth = static_cast<std::uint8_t>(nibble >> 4u);
      if (worth == 0u)
      {
        return ScoopResult::Crashed; // 6502: BEQ MA58 -- nothing worth scooping
      }

      item = static_cast<std::uint8_t>(worth + 1u + ((nibble & 0x08u) != 0u ? 1u : 0u));
    }

    // 6502: .slvy2 JSR tnpr1 / LDY #78 / BCS MA59 -- `tnpr1` stores the item and asks for ONE.
    if (!CargoFits(commander, item, 1u))
    {
      return ScoopResult::HoldFull;
    }

    // 6502: LDY QQ29 / ADC QQ20,Y / STA QQ20,Y -- A is the 1 `tnpr` pushed and popped, and the
    // carry is clear because that is how the `BCS` was not taken.
    std::uint8_t& held = commander.cargoHold[item];
    const AddResult stored = AddWithCarry(1u, held, false);
    held = stored.value;

    // 6502: TYA / ADC #208 / JSR MESS -- on the carry the store above left behind.
    const AddResult token = AddWithCarry(item, MESSAGE_FIRST_CARGO, stored.carry);
    ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, token.value, _universe.view);

    // 6502: ASL NEWB / SEC / ROR NEWB -- bit 7 is "take it out of the bubble", so a scooped
    // canister is removed by part 12 rather than by anything here.
    _universe.work.newb = With(_universe.work.newb, NewbBit::Remove);
    return ScoopResult::Stowed;
  }

  /*
   * ---- part 9: docking, or not ------------------------------------------------------------------
   *
   * Four tests and every one of them has to pass. The station must not be hostile, the ship must be
   * pointing the right way (`INWK+14` is the nose vector's z high byte -- the port's own constant
   * called it the pitch counter until Modernize.md M1-a named the byte), the player must be lined up
   * with the slot, and the roll must be slow enough. `SPS1` is called for its side effect: it leaves
   * the normalised vector to the PLANET in `XX15`, and `XX15+2` is what the alignment test reads.
   */
  enum class DockingTest : std::uint8_t
  {
    Arrived, ///< 6502: GOIN -- all four passed
    TooFast, ///< 6502: .MA62 CMP #5 / JMP DEATH
    Bumped,  ///< 6502: MA67 -- a miss slow enough to survive, which part 10 charges for
  };

  [[nodiscard]] DockingTest TestDocking(Universe& _universe) noexcept
  {
    const bool hostile = Has(_universe.bubble.blocks[STATION_SLOT].newb, NewbBit::Hostile);

    if (!hostile && _universe.work.nose.z.hi >= DOCK_MINIMUM_PITCH)
    {
      (void)LoadPlanetAxes(_universe.bubble, _universe.axes);          // 6502: JSR SPS1
      const UnitVector towards = NormaliseAxes(_universe.axes).vector; // the fall-through

      if (towards.z >= DOCK_MINIMUM_ALIGNMENT && static_cast<std::uint8_t>(_universe.work.roof.x.hi & 0x7Fu) >= DOCK_MAXIMUM_ROLL)
      {
        return DockingTest::Arrived;
      }
    }

    // 6502: .MA62 LDA DELTA / CMP #5 / BCC MA67 -- fast enough and you are dead.
    return (_universe.flight.delta >= DOCK_SURVIVABLE_SPEED) ? DockingTest::TooFast : DockingTest::Bumped;
  }

  /*
   * ---- part 10: what the collision costs ---------------------------------------------------------
   *
   * Three entries into one call, and this enumerates them. `MA59` is a full hold, which is not
   * damage at all -- the thing bounces off and the frame carries on. `MA67` is a slow bump into the
   * station, worth five. `MA58` is everything else, and its damage is HALF THE OTHER SHIP'S OWN
   * ENERGY: `LDA INWK+35 / SEC / ROR A`, which is also where `OOPS`'s carry comes from (§6.87).
   */
  enum class Impact : std::uint8_t
  {
    None,    ///< nothing touched us, or it was scooped
    Bounced, ///< 6502: MA59 -- a full hold
    Bumped,  ///< 6502: MA67 -- the station, slowly
    Crashed, ///< 6502: MA58 -- everything else
  };

  /// Answers whether the player survived it. False is `JMP DEATH` out of `OOPS`.
  [[nodiscard]] bool SurviveImpact(Universe& _universe, Ports& _ports, Ship& _block, Impact _impact) noexcept
  {
    if (_impact == Impact::None)
    {
      return true;
    }

    if (_impact == Impact::Bounced)
    {
      // 6502: .MA59 JSR EXNO3 -- and the carry is SET, because `BCS MA59` in part 8 is the only way
      // here: a full hold is exactly the carry the capacity test leaves.
      (void)PlaySoundEffect(_universe.sound, SoundEffect::Explosion, true);
      _universe.work.state = MarkKilled(_universe.work.state); // 6502: .MA60
      // 6502: .MA61 BNE MA26 -- and `ROR` has just set bit 7, so it always branches.
      return true;
    }

    std::uint8_t damage = 0;
    bool carry = false;

    if (_impact == Impact::Bumped)
    {
      _universe.flight.delta = 1u;    // 6502: .MA67 LDA #1 / STA DELTA
      damage = DOCK_SURVIVABLE_SPEED; // 6502: LDA #5 -- and the carry is the `BCC`'s, so clear
    }
    else
    {
      _universe.work.state = MarkKilled(_universe.work.state); // 6502: .MA58
      const ShiftResult halved = RotateRight(_universe.work.energy, true);
      damage = halved.value;
      carry = halved.carry;
    }

    // 6502: .MA63 JSR OOPS / JSR EXNO3 -- and `OOPS` makes the same noise itself on the path that
    // survives, so a hit that costs the banks is heard twice.
    if (!TakeDamage(_universe, _ports, _block, damage, carry))
    {
      return false;
    }

    /*
     * 6502: .MA63 JSR OOPS / JSR EXNO3 -- and the carry into `EXNO3` is the one `OOPS` left.
     *
     * `OOPS` ends `ADC ENERGY / STA ENERGY / BEQ / BCS`, and reaching here at all means the `BCS`
     * was taken, so it is SET. Nothing between the two calls touches the flag.
     */
    (void)PlaySoundEffect(_universe.sound, SoundEffect::Explosion, true);
    return true;
  }

  /*
   * ---- part 11: our laser, and the scanner --------------------------------------------------------
   *
   * 6502: .MA26 LDA NEWB / BPL P%+5 / JSR SCAN.
   *
   * Bit 7 of `NEWB` is "take this out of the bubble" AND "it is on the scanner", one bit doing two
   * jobs: a ship marked for removal has its blip drawn here so that the EOR erases it.
   */
  struct Aim
  {
    /// 6502: LDA QQ11 / BNE MA15 -- a chart on screen skips the aiming AND the `JSR LL9` below it.
    bool draws;

    /*
     * The carry `JSR LL9` is reached with, which `LL9` reads on exactly one path: a ship that
     * arrives killed, not yet exploding and not on the screen seeds its cloud's first `DORND` on it
     * (§6.157). Every arrival at `MA8` is traced in the body, and each one is a flag some routine
     * left rather than one this part sets.
     */
    bool carry;
  };

  [[nodiscard]] Aim AimAtShip(Universe& _universe, Ports& _ports, ShipType _type) noexcept
  {
    if (Has(_universe.work.newb, NewbBit::Remove))
    {
      DrawScannerBlip(_universe.canvas, _universe.work, _type, _universe.view);
    }

    if (_universe.view != 0u) // 6502: LDA QQ11 / BNE MA15 -- a chart means no drawing at all
    {
      return {false, false};
    }

    FlipAxesForView(_universe.work, _universe.flight, _universe.spaceView); // 6502: JSR PLUT

    if (!IsHit(_universe.work, *_universe.flight.blueprint, _type)) // 6502: JSR HITCH / BCC MA8
    {
      return {true, false}; // 6502: BCC MA8 -- not in the sights, and `HITCH` cleared it saying so
    }

    bool carry = true; // 6502: HITCH's SEC, and nothing on the way to `MA47` touches it

    // 6502: LDA MSAR / BEQ MA47 / JSR BEEP / LDX XSAV / LDY #RED2 / JSR ABORT2 -- an armed missile
    // locks onto whatever the sights are on, and the indicator turns red. `BEEP` is `NOISE`, whose
    // exit carry is the answer here (§6.86); `ABORT2` and `MSBAR` are stores and register moves and
    // leave it alone.
    if (_universe.status.missileArmed != 0u)
    {
      // The flag going IN is the one two lines above -- `HITCH`'s `SEC`, which `LDA MSAR` and `BEQ`
      // do not touch. `NOISE` hands it straight back when the sound is switched off, so passing
      // `false` here (which the port did until M2-d) changed the carry `LL9` seeds an explosion
      // cloud on, on a silent build (§8).
      carry = PlaySoundEffect(_universe.sound, SoundEffect::Beep, carry).carry;
      SetMissileTarget(_universe, _universe.commander.missiles, _universe.flight.slot, MISSILE_LOCKED);
    }

    // 6502: .MA47 LDA LAS / BEQ MA8 -- no laser firing this frame, so nothing is damaged.
    if (_universe.status.laserPower != 0u)
    {
      const LaserHit hit = ApplyLaserHit(_universe, _ports, *_universe.flight.blueprint, _type);
      if (hit.stores)
      {
        _universe.work.energy = hit.energy; // 6502: .MA14 STA INWK+35
      }

      // 6502: `MA14+2` -- LDA TYPE / JSR ANGRY, which both skip-the-store paths land on too. INF is
      // this ship's block here, the one the loop is on, so the slot is XSAV's. Every laser path ends
      // in this call, so its exit carry is the one `LL9` gets.
      carry = Anger(_universe.bubble, _universe.flight, _universe.flight.slot, _type);
    }

    return {true, carry};
  }

  /*
   * ---- part 12: what is written back, and what is removed ----------------------------------------
   *
   * 6502: .MA15 LDY #35 / LDA INWK+35 / STA (INF),Y -- the energy goes back on every path, and byte
   * 31 only on the path that keeps the ship. That asymmetry is the routine: a ship being removed has
   * its block shuffled away by `KILLSHP`, so writing its state would be wasted.
   */
  enum class KillOutcome : std::uint8_t
  {
    Kept,    ///< 6502: .MA27 -- the state byte goes back and the index advances
    Removed, ///< 6502: .KS1 -- `KILLSHP`, and the index does NOT advance
  };

  [[nodiscard]] KillOutcome RetireShip(Universe& _universe, Ports& _ports, Ship& _block, ShipType _type, bool _isBody) noexcept
  {
    Commander& commander = _universe.commander;

    _block.energy = _universe.work.energy;

    if (Has(_universe.work.newb, NewbBit::Remove))
    {
      return KillOutcome::Removed;
    }

    if (Has(_universe.work.state, ShipStateBit::Killed) && Has(_universe.work.state, ShipStateBit::Exploding))
    {
      /*
       * 6502: LDA NEWB / AND #%01000000 / ORA FIST / STA FIST.
       *
       * Bit 6 is "shooting this was a crime", and it is ORed into the legal status rather than added
       * -- so the offence is recorded once however many innocents die, and a fugitive cannot become
       * more of one this way.
       */
      commander.legalStatus = static_cast<std::uint8_t>(commander.legalStatus | (_universe.work.newb & Mask(NewbBit::Cop)));

      // 6502: LDA DLY / ORA MJ / BNE KS1S -- no bounty while a message is up or in witchspace,
      // because the bounty IS a message and there is nowhere to put it.
      const bool quiet = (_universe.message.delay | _universe.status.midJump) == 0u;

      if (quiet)
      {
        // 6502: LDY #10 / LDA (XX0),Y / BEQ KS1S / TAX / INY / LDA (XX0),Y / TAY / JSR MCASH.
        const std::uint8_t low = static_cast<std::uint8_t>(_universe.flight.blueprint->bounty & 0xFFu);

        if (low != 0u)
        {
          ReceiveCash(commander, _universe.flight.blueprint->bounty);

          /*
           * 6502: LDA #0 / JSR MESS.
           *
           * TOKEN ZERO IS THE CASH. `TT27` opens `TAX / BEQ csh`, so the bounty message is the
           * player's balance printed in flight -- and `MESS` then stores that zero in `MCH`, so the
           * next message within twenty frames erases this one by printing the balance again.
           */
          ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, 0u, _universe.view);
        }
      }

      return KillOutcome::Removed; // 6502: .KS1S JMP KS1 -- every path through this block ends there
    }

    // 6502: .MAC1 LDA TYPE / BMI MA27 / JSR FAROF / BCC KS1S -- and the planet and the sun are never
    // out of range, because they are what range is measured against.
    return (!_isBody && !WithinLoopRange(_universe.work)) ? KillOutcome::Removed : KillOutcome::Kept;
  }

  LoopOutcome MoveEveryShip(Universe& _universe, Ports& _ports) noexcept
  {
    Commander& commander = _universe.commander;

    // 6502: .MA3 LDX #0 / .MAL1 STX XSAV -- and the index is advanced by hand, never by the loop.
    std::uint8_t slot = 0;

    for (;;)
    {
      _universe.flight.slot = slot; // 6502: STX XSAV

      // 6502: LDA FRIN,X / BNE P%+5 / JMP MA18 -- the first empty slot ends the pass.
      const ShipType type = TypeOf(_universe.bubble.slots[slot]);
      if (type == ShipType::None)
      {
        return LoopOutcome::Continued;
      }

      _universe.flight.type = type; // 6502: STA TYPE

      // 6502: JSR GINF / LDY #NI%-1 / .MAL2 LDA (INF),Y / STA INWK,Y / DEY / BPL MAL2.
      Ship& block = _universe.bubble.blocks[slot];
      _universe.work = block;

      /*
       * 6502: LDA TYPE / BMI MA21 / ASL A / TAY / LDA XX21-2,Y / STA XX0 / LDA XX21-1,Y / STA XX0+1.
       *
       * The planet and the sun have no blueprint, and `XX0` is left holding the LAST ship's -- which
       * is why part 5's `CPY` tests below run on a Y that only a real ship has set.
       */
      const bool isBody = IsBody(type);
      if (!isBody)
      {
        _universe.flight.blueprint = BlueprintFor(_universe.bubble, type);

        /*
         * 6502: part 5 -- LDA BOMB / BPL MA21 and four tests under it.
         *
         * The energy bomb kills everything in the bubble EXCEPT a station, a Thargoid and anything
         * from the Constrictor upwards, and the three exemptions are written as `CPY` against
         * `2*SST`, `2*THG` and `2*CON` -- comparisons on the DOUBLED type, because Y still holds
         * the blueprint index. A ship already exploding is skipped as well, or the bomb would
         * restart its cloud on every frame it burns.
         */
        const bool exempt = (type == ShipType::Station) || (type == ShipType::Thargoid) || (Byte(type) >= Byte(ShipType::Constrictor));

        if ((commander.energyBomb & 0x80u) != 0u && !exempt && !Has(_universe.work.state, ShipStateBit::Exploding))
        {
          _universe.work.state = MarkKilled(_universe.work.state);
          (void)RecordKill(_universe, _ports, type); // 6502: LDX TYPE / JSR EXNO2
        }
      }

      /*
       * 6502: .MA21 JSR MVEIT / LDY #NI%-1 / .MAL3 LDA INWK,Y / STA (INF),Y / DEY / BPL MAL3.
       *
       * The block is written back BEFORE the death is acted on, because the original writes it
       * back only on the path that survives -- `JMP DEATH` from inside `TACTICS` never reaches
       * `MAL3` -- and `DEATH` calls `RES2`, which clears the bubble anyway (§6.122).
       */
      if (!MoveShip(_universe, _ports))
      {
        return LoopOutcome::Died;
      }
      block = _universe.work;

      /*
       * ---- parts 7 to 12, each one a stage that ANSWERS and a caller that acts (M4-a-2) ---------
       *
       * This was three hundred and fifty-seven lines of the parts written out in place, sharing five
       * booleans -- `docking`, `scoopable`, `collision`, `holdFull`, `drawIt` -- declared above the
       * block they were set in and read two parts later. Every one of them is now a value some stage
       * returns, and the exclusivity the booleans only implied is what the types say.
       */

      const Contact contact = TestContact(_universe.work, commander, type, isBody);

      Impact impact = (contact == Contact::Collision) ? Impact::Crashed : Impact::None;

      if (contact == Contact::Scoopable)
      {
        switch (ScoopCargo(_universe, _ports, type))
        {
        case ScoopResult::Stowed:
          break;
        case ScoopResult::HoldFull:
          impact = Impact::Bounced;
          break;
        case ScoopResult::Crashed:
          impact = Impact::Crashed;
          break;
        }
      }

      if (contact == Contact::Docking)
      {
        switch (TestDocking(_universe))
        {
        case DockingTest::Arrived:
          // 6502: .GOIN JSR stopbd / JMP DOENTRY
          StopDockingMusic(_universe.music, _universe.status.titleReset, _universe.sound, _universe.memoryMap, _ports.sid);
          return LoopOutcome::Docked;
        case DockingTest::TooFast:
          return LoopOutcome::Died; // 6502: JMP DEATH
        case DockingTest::Bumped:
          impact = Impact::Bumped;
          break;
        }
      }

      if (!SurviveImpact(_universe, _ports, block, impact))
      {
        return LoopOutcome::Died;
      }

      const Aim aim = AimAtShip(_universe, _ports, type);

      // 6502: .MA8 JSR LL9 -- and it is the same call that erases the last frame's ship.
      if (aim.draws)
      {
        DrawShip(_universe.canvas, _universe.geometry, _universe.math, _universe.clip, _universe.projection, _universe.work, block,
                 _universe.heap, *_universe.flight.blueprint, type, _ports.drawing, _universe.rng, aim.carry);
      }

      if (RetireShip(_universe, _ports, block, type, isBody) == KillOutcome::Removed)
      {
        /*
         * 6502: .KS1 LDX XSAV / JSR KILLSHP / LDX XSAV / JMP MAL1.
         *
         * THE INDEX IS NOT ADVANCED. `KILLSHP` shuffles every slot above the dead one down, so the
         * ship that was behind it is now in the same slot -- and going round with the same X is
         * what processes it. A port that wrote a `for` over the slots would skip a ship for every
         * one killed.
         */
        KillShip(_universe, _ports, slot);
      }
      else
      {
        block.state = _universe.work.state; // 6502: .MA27 LDY #31 / STA (INF),Y
        ++slot;                             // 6502: LDX XSAV / INX / JMP MAL1
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
  [[nodiscard]] LoopOutcome EndFlightFrameTail(Universe& _universe, Ports& _ports) noexcept
  {
    /*
     * 6502: .MA23 LDA LAS2 / BEQ MA16 / LDA LASCT / CMP #8 / BCS MA16 / JSR LASLI2 / LDA #0 /
     * STA LAS2.
     *
     * `LAS2` is "there is a beam on screen" and `LASCT` is how long it has left. Below eight the
     * beam is rubbed out by drawing it again -- `LASLI2` is `LASLI` without the firing -- so the
     * shot is visible for a fixed number of frames however long the trigger is held.
     */
    if (_universe.status.viewLaser != 0u && _universe.status.laserCount < LASER_ERASE_LIMIT)
    {
      (void)DrawLaserLines(_universe.canvas, _universe.burst, _universe.view);
      _universe.status.viewLaser = 0u;
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

    if (_universe.status.ecmOurs != 0u)
    {
      stop = DrainEnergy(_universe.status); // 6502: JSR DENGY / BEQ MA70
    }

    if (!stop && _universe.status.ecmCountdown != 0u)
    {
      _universe.status.ecmCountdown = static_cast<std::uint8_t>(_universe.status.ecmCountdown - 1u);
      stop = _universe.status.ecmCountdown == 0u;
    }

    if (stop)
    {
      StopEcm(_universe.canvas, _universe.status, _universe.sound); // 6502: .MA70 JSR ECMOF
    }

    /*
     * 6502: .MA66 LDA QQ11 / BNE oh / JMP STARS -- and `oh` is an `RTS` thirty-three bytes further
     * on, borrowed from another routine. So a chart on screen ends the frame with the stardust
     * left exactly where it was.
     */
    if (_universe.view == 0u)
    {
      MoveStardust(_universe.canvas, _universe.flight, _universe.dust, _universe.rng, _universe.spaceView);
    }

    return LoopOutcome::Continued;
  }

  /*
   * ---- part 13's head: the energy bomb burns down --------------------------------------------
   *
   * 6502: .MA18 LDA BOMB / BPL MA77 / ASL BOMB / BMI MA77 / JSR BOMBOFF.
   *
   * The bomb is a countdown kept as a shift register: part 3 doubles it when the key is pressed and
   * this doubles it again every frame, so it burns for as many frames as it has bits left and ends
   * when the top bit falls off. It runs on EVERY frame, which is why it is above the `AND #7`.
   */
  void BurnEnergyBomb(Universe& _universe) noexcept
  {
    Commander& commander = _universe.commander;

    if ((commander.energyBomb & 0x80u) != 0u)
    {
      commander.energyBomb = static_cast<std::uint8_t>(commander.energyBomb << 1u);

      if ((commander.energyBomb & 0x80u) == 0u)
      {
        StopEnergyBomb(_universe.screen);
      }
    }
  }

  /*
   * ---- part 13's tail: the shields and the banks ---------------------------------------------
   *
   * Every eighth frame, which is what `AND #7` selects.
   */
  void RechargeBanks(Universe& _universe) noexcept
  {
    Commander& commander = _universe.commander;

    /*
     * 6502: LDX ENERGY / BPL b -- the shields are fed FROM the banks, so they only recharge while
     * the banks are at least half full. `SHD` itself takes a unit of energy per shield (§6.83).
     */
    if ((_universe.status.energy & 0x80u) != 0u)
    {
      _universe.status.aftShield = RechargeShield(_universe.status, _universe.status.aftShield);
      _universe.status.forwardShield = RechargeShield(_universe.status, _universe.status.forwardShield);
    }

    /*
     * 6502: .b SEC / LDA ENGY / ADC ENERGY / BCS P%+5 / STA ENERGY.
     *
     * The `SEC` is the recharge: a commander with no energy unit still gains one point every
     * eighth frame. And the overflow branch SKIPS the store rather than clamping, so banks that
     * would pass 255 are left exactly where they were.
     */
    const AddResult banks = AddWithCarry(commander.energyUnit, _universe.status.energy, true);
    if (!banks.carry)
    {
      _universe.status.energy = banks.value;
    }
  }

  /*
   * ---- part 14: bringing the space station back ------------------------------------------------
   *
   * Once every thirty-two frames, and only when the station is NOT in the bubble, the loop checks
   * whether the planet is close enough to have one -- and `MAS1` is called three times to DOUBLE
   * the planet's coordinates into `INWK`, so the test is run at twice the distance.
   */
  void MaybeSpawnStation(Universe& _universe, Ports& _ports) noexcept
  {
    // 6502: LDA SSPR / BNE MA23S, then TAY / JSR MAS2 / BNE MA23S.
    if (_universe.bubble.Count(ShipType::Station) != 0u || LargestAxis(_universe.bubble, 0u) != 0u)
    {
      return;
    }

    // 6502: LDX #28 / .MAL4 LDA K%,X / STA INWK,X / DEX / BPL MAL4 -- 29 bytes, not the block:
    // the position, the orientation, the speed and the acceleration, and nothing after.
    std::array<std::uint8_t, SHIP_BLOCK_SIZE> bytes = _universe.work.ToBytes();
    const std::array<std::uint8_t, SHIP_BLOCK_SIZE> planet = _universe.bubble.blocks[0].ToBytes();
    std::copy_n(planet.begin(), 29u, bytes.begin());
    _universe.work = Ship::FromBytes(bytes);

    // 6502: INX / LDY #9 / JSR MAS1 / BNE MA23S, and twice more at (3, 11) and (6, 13).
    // The `&&`s short-circuit and have to: each `MAS1` DOUBLES the coordinate it reads, in
    // place, so a second call after a non-zero answer would move the planet twice.
    const bool ahead = DoubleAndAddCoordinate(_universe.work, 9u, 0u) == 0u && DoubleAndAddCoordinate(_universe.work, 11u, 3u) == 0u &&
                       DoubleAndAddCoordinate(_universe.work, 13u, 6u) == 0u;

    if (ahead && WithinRange(_universe.work, STATION_SPAWN_RANGE))
    {
      EraseSun(_universe.canvas, _universe.heaps); // 6502: JSR WPLS

      // 6502: JSR NWSPS -- and the erase above is half of one thought with it: `NWSPS` empties
      // the sun's SLOT and takes its line heap, so this rubs the sun off the screen first.
      (void)AddStation(_universe, _ports);
    }
  }

  /*
   * ---- part 15: the thirty-two step cycle, as the table it is ----------------------------------
   *
   * 6502: .MA22 LDA MJ / BNE MA23S / LDA MCNT / AND #31 / .MA93 CMP #10 / BNE MA29.
   *
   * THE CYCLE IS THIRTY-TWO STEPS AND NOT SIXTEEN. The M4-a row and this block's own heading both
   * said sixteen and neither is right: `MCNT` is masked with 31, so the three jobs below fire once
   * each per thirty-two frames, and part 13's shields-and-banks fire once per eight. Nothing in the
   * original mentions sixteen at all; the number was carried in the plan from M0 and is corrected
   * here rather than left to be repeated (M4-a-3).
   *
   *   step 10  the energy warning, then the altitude -- and `MA28` out of it is death
   *   step 15  the docking-computer reminder
   *   step 20  the cabin temperature, the Trumbles cooking, and fuel scooping -- death here too
   *   others   nothing, which is the `BNE MA29 / BNE MA33 / BNE MA23` chain falling through
   *
   * `MCNT` DECREMENTS rather than increments (`MLOOP`'s `DEC MCNT`), so the cycle runs backwards
   * through those residues; every one of the thirty-two is still visited once per block.
   */
  [[nodiscard]] LoopOutcome RunCycleStep(Universe& _universe, Ports& _ports, std::uint8_t _counter) noexcept
  {
    Commander& commander = _universe.commander;

    if (_counter == STEP_ENERGY_CHECK)
    {
      /*
       * 6502: LDA #50 / CMP ENERGY / BCC P%+6 / ASL A / JSR MESS.
       *
       * AND `P%+6` SKIPS BOTH INSTRUCTIONS, not just the shift. The branch is two bytes and `ASL A`
       * plus `JSR MESS` is four, so healthy banks send no message at all -- the fifty is a
       * threshold that happens to be half of the token, and the token itself is only ever 100.
       * Reading it as "50 or 100" gives a warning every sixteenth frame for the whole game.
       */
      if (ENERGY_WARNING >= _universe.status.energy)
      {
        ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message,
                    static_cast<std::uint8_t>(ENERGY_WARNING << 1u), _universe.view);
      }

      /*
       * 6502: LDY #&FF / STY ALTIT / INY / JSR m / BNE MA23 -- the altitude is 255 until proved
       * otherwise, so a planet too far away in any axis leaves the dial full.
       */
      _universe.status.altitude = 0xFFu;

      if (LargestAxis(_universe.bubble, 0u) == 0u)
      {
        // 6502: JSR MAS3 / BCS MA23 -- and the carry is `MAS3`'s saturation, not a comparison.
        const std::uint8_t squares = SumOfSquares(_universe.bubble, 0u);
        if (squares != 0xFFu)
        {
          /*
           * 6502: SBC #36 / BCC MA28 -- inside the planet's own radius, so this is the ground.
           *
           * AND THE CARRY IS CLEAR, so the subtraction takes THIRTY-SEVEN. `MAS3` returns with the
           * flag its last `ADC` left -- set only when the sum saturated, which is the case the
           * `BCS MA23` two instructions above has already sent away -- so every arrival here has a
           * borrow to pay. The port subtracted 36 until the R22 fixture put a planet close enough
           * to reach this line, and the oracle died where the port did not (§8).
           */
          const SubResult above = SubtractWithCarry(squares, ALTITUDE_PLANET_RADIUS, false);
          if (!above.carry)
          {
            return LoopOutcome::Died; // 6502: .MA28 JMP DEATH
          }

          /*
           * 6502: STA R / JSR LL5 / LDA Q / STA ALTIT.
           *
           * THE RADICAND'S LOW BYTE IS WHATEVER `Q` LAST HELD. Nothing between the last ship's
           * processing and this square root writes `Q` -- `MAS3` and `m` do not -- so the altitude's
           * low bits come from the last routine of the frame that used the scratch byte: `MVS4`'s
           * `STA Q` of BETA for a ship the loop moved and did not draw, `LL9`'s vertex distance or
           * the clipper's for one it drew, `DVID3B`'s scaled divisor for a dot, `SUN`'s last row's
           * root. Since M2-b the kernel keeps its scratch to itself, so those routines write
           * `MathWorkspace::q` for this read alone -- the "frame's Q".
           *
           * R22 said there was a tenth writer the port never modelled, `LOIN`'s `STA Q`. There is
           * not: this build's `LOIN` works in `P2`, `Q2`, `R2` and `S2` at 188-191 and never
           * touches `Q` at 154, and the risk was written from the BBC commentary -- the same source
           * that gave M2-c-1 its `T`/`T2` defect. What the byte holds is now compared against the
           * game rather than argued about, by `TheFramesOwnQReachesTheAltitude` over six bubble
           * shapes and by `TheAltitudeMatchesMA23` with the byte seeded on both sides (§8).
           */
          _universe.status.altitude = SquareRoot(above.value, _universe.math.q).value;
        }
      }
    }
    else if (_counter == STEP_DOCKING_REMINDER)
    {
      // 6502: .MA29 CMP #15 / BNE MA33 / LDA auto / BEQ MA23 / LDA #123 / BNE MA34.
      if (_universe.control.dockingComputer != 0u)
      {
        ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, MESSAGE_DOCKING_ON,
                    _universe.view);
      }
    }
    else if (_counter == STEP_CABIN_TEMPERATURE)
    {
      /*
       * 6502: .MA33 CMP #20 / BNE MA23 / LDA #30 / STA CABTMP / LDA SSPR / BNE MA23.
       *
       * Thirty is room temperature and it is written unconditionally, so the sun's contribution
       * below is a replacement rather than an increase. Inside station range there is no sun to
       * feel, which is why the check comes second.
       */
      _universe.status.cabinTemperature = CABIN_BASE;

      if (_universe.bubble.Count(ShipType::Station) != 0u || LargestAxis(_universe.bubble, 1u) != 0u)
      {
        return EndFlightFrameTail(_universe, _ports);
      }

      /*
       * 6502: JSR MAS3 / EOR #%11111111 / ADC #30 / STA CABTMP / BCS MA28.
       *
       * The temperature is thirty MINUS the distance squared, written as a negate-and-add on
       * `MAS3`'s exit carry -- and the carry OUT is death: an overflow here means the sum passed
       * 255, which is the sun.
       */
      const std::uint8_t squares = SumOfSquares(_universe.bubble, 1u);
      const AddResult heat = AddWithCarry(static_cast<std::uint8_t>(squares ^ 0xFFu), CABIN_BASE, false);
      _universe.status.cabinTemperature = heat.value;

      if (heat.carry)
      {
        return LoopOutcome::Died; // 6502: BCS MA28
      }

      // 6502: CMP #224 / BCC MA23 -- below this the sun is just warm and nothing else happens.
      if (heat.value < CABIN_SCOOPING)
      {
        return EndFlightFrameTail(_universe, _ports);
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
        SetMemoryMap(_universe.memoryMap, MEMORY_MAP_IO);
        ApplyMaskSprites(_universe.video, SPRITES_KEEP);
        SetMemoryMap(_universe.memoryMap, MEMORY_MAP_RAM);

        const ShiftResult high = RotateRight(commander.tribbles.hi, false);
        commander.tribbles.hi = high.value;
        commander.tribbles.lo = RotateRight(commander.tribbles.lo, high.carry).value;
      }

      /*
       * 6502: .nokilltr LDA BST / BEQ MA23 / LDA DELT4+1 / LSR A / ADC QQ14 / CMP #70 / BCC P%+4 /
       * LDA #70 / STA QQ14 / LDA #160 / .MA34 JSR MESS.
       *
       * Fuel scooping, and the amount is the player's own SPEED: `DELT4+1` is `DELTA` shifted up
       * six places, halved again here. Flying into the sun faster fills the tank faster.
       */
      if (commander.fuelScoops != 0u)
      {
        const ShiftResult scooped = RotateRight(_universe.flight.delt4Next, false);
        commander.fuel = commander.fuel.Scooped(scooped.value, scooped.carry);

        ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, MESSAGE_SCOOPS_ON,
                    _universe.view);
      }
    }

    return EndFlightFrameTail(_universe, _ports);
  }

  LoopOutcome EndFlightFrame(Universe& _universe, Ports& _ports) noexcept
  {
    BurnEnergyBomb(_universe); // 6502: part 13's head, on every frame

    // 6502: .MA77 LDA MCNT / AND #7 / BNE MA22 -- seven frames in eight skip straight to part 15.
    const std::uint8_t counter = static_cast<std::uint8_t>(_universe.flight.mainLoopCounter & 31u);

    if ((_universe.flight.mainLoopCounter & 7u) == 0u)
    {
      RechargeBanks(_universe); // 6502: part 13's tail

      // 6502: part 14 opens LDA MJ / BNE MA23S -- no space stations in witchspace.
      if (_universe.status.midJump != 0u)
      {
        return EndFlightFrameTail(_universe, _ports);
      }

      /*
       * 6502: LDA MCNT / AND #31 / BNE MA93 -- and the fall-through matters (M4-a-3).
       *
       * A zero runs part 14 and every path through it ends at `MA23S`; anything else drops into
       * `MA93`, which is part 15's first compare. The port used to return the tail on BOTH, which
       * is observationally identical -- a step that is 0 mod 8 is never 10, 15 or 20 mod 32, so the
       * three jobs could not have fired anyway -- but it was an argument nothing had written down,
       * and the comment above part 15 asserted the opposite. The shape is the original's again.
       */
      if (counter == 0u)
      {
        MaybeSpawnStation(_universe, _ports);
        return EndFlightFrameTail(_universe, _ports);
      }
    }
    else
    {
      // 6502: .MA22 LDA MJ / BNE MA23S -- part 15's own witchspace test, the twin of part 14's.
      if (_universe.status.midJump != 0u)
      {
        return EndFlightFrameTail(_universe, _ports);
      }
    }

    return RunCycleStep(_universe, _ports, counter); // 6502: .MA93
  }

  LoopOutcome MainFlightLoop(Universe& _universe, Ports& _ports) noexcept
  {
    const LoopOutcome opening = BeginFlightFrame(_universe, _ports);
    if (opening != LoopOutcome::Continued)
    {
      return opening;
    }

    const LoopOutcome ships = MoveEveryShip(_universe, _ports);
    if (ships != LoopOutcome::Continued)
    {
      return ships;
    }

    return EndFlightFrame(_universe, _ports);
  }

  CrosshairStep ScanFlightControls(Universe& _universe, Ports& _ports, ControlEffects& _effects, std::uint8_t _view) noexcept
  {
    // 6502: JSR DOKEY, which BOTH paths do before they differ.
    ReadFlightControls(_universe, _ports, _effects);

    // 6502: LDA QQ11 / BNE TT17afterall -- the space view returns with X and Y untouched, so the
    // caller gets no movement rather than a movement of zero, and the two are the same thing here.
    if (_view == 0u)
    {
      return {};
    }

    return ReadCrosshairKeys(_universe.keys);
  }

} // namespace Elite
