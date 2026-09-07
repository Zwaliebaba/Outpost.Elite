#include "pch.h"

#include "GameLoop.h"

#include "Arith.h"
#include "EliteTypes.h"
#include "PlanetDraw.h"
#include "Market.h"
#include "Charts.h"
#include "Dashboard.h"
#include "Messages.h"
#include "Spawn.h"
#include "SoundEffects.h"
#include "TextPrint.h"

namespace Elite
{

  namespace
  {

    /*
     * 6502: LDA #COPS / JSR NWSHP -- one spawn, with the type in A and the block already built.
     *
     * Every call in these four parts has the same shape, and gathering it here keeps the parts
     * readable as the branch structure they are rather than as bookkeeping.
     */
    NewShip Spawn(Bubble& _bubble, Ship& _work, ShipType _type, const Blueprint*& _blueprint) noexcept
    {
      return AddShip(_bubble, _work, _type, _blueprint);
    }

  } // namespace

  LoopHead RunLoopHead(Universe& _universe, Ports& _ports) noexcept
  {
    /*
     * 6502: DEC DLY / BEQ me2 / BPL me3 / INC DLY.
     *
     * The `INC` is what stops it wrapping: `DLY` at zero decrements to 255, which is negative, so
     * the `BPL` falls through and the increment puts it back. Only a `DLY` of exactly 1 reaches
     * `me2`.
     */
    const std::uint8_t delayed = static_cast<std::uint8_t>(_universe.message.delay - 1u);
    _universe.message.delay = delayed;

    if (delayed == 0u)
    {
      // 6502: .me2 LDA QQ11 / BNE clynsneed.
      if (_universe.view != 0u)
      {
        // 6502: JSR CLYNS -- a text screen's message is in the bottom rows. A seam until M3-b-3b.
        ClearMessageRows(_universe.canvas, _ports.printer, _universe.text, _ports.characters.state, _universe.message);
      }
      else
      {
        /*
         * 6502: LDA MCH / JSR MESS / LDA #0 / STA DLY.
         *
         * Sending the SAME token again is what erases it: the printer EORs, so the second print of
         * a message rubs out the first. And `MESS` sets `DLY` to twenty, which is why the `LDA #0 /
         * STA DLY` after it is not redundant -- it undoes what the call just did.
         */
        ShowMessage(_universe.canvas, _ports.printer, _universe.text, _ports.characters.state, _universe.message, _universe.message.token,
                    _universe.view);
        _universe.message.delay = 0u;
      }
    }
    else if ((delayed & 0x80u) != 0u)
    {
      _universe.message.delay = static_cast<std::uint8_t>(delayed + 1u); // 6502: INC DLY
    }

    // 6502: .me3 DEC MCNT / BEQ P%+5 / .ytq JMP MLOOP -- and this is the ONE PASS IN 256 that
    // reaches everything slice 4c-a built.
    --_universe.flight.mainLoopCounter;
    return _universe.flight.mainLoopCounter == 0u ? LoopHead::Spawn : LoopHead::SkipSpawning;
  }

  void CoolTheGuns(FlightStatus& _status) noexcept
  {
    // 6502: LDX GNTMP / BEQ EE20 / DEC GNTMP -- the laser cools by one every pass, docked or
    // flying, because this is above part 5's `QQ11` gate.
    if (_status.laserTemperature != 0u)
    {
      --_status.laserTemperature;
    }

    /*
     * 6502: .EE20 LDX LASCT / BEQ NOLASCT / DEX / BEQ P%+3 / DEX / STX LASCT.
     *
     * TWO at a time, and the `BEQ P%+3` is why it never passes zero: one `DEX` lands on it and the
     * branch skips the second. So an odd countdown stops at zero and an even one steps through it,
     * and a port that subtracted two would go negative on the odd values.
     */
    if (_status.laserCount != 0u)
    {
      std::uint8_t count = static_cast<std::uint8_t>(_status.laserCount - 1u);
      if (count != 0u)
      {
        --count;
      }
      _status.laserCount = count;
    }
  }

  std::uint8_t RunLoopTail(Universe& _universe, Ports& _ports, Commander& _commander, std::uint8_t _authorNames, bool _carryIn) noexcept
  {
    std::uint8_t requestedFrames = 0;
    bool carry = _carryIn;

    // 6502: the two countdowns above the `QQ11` gate, which a docked pass reaches as well -- see
    // `CoolTheGuns`, which the executable's docked loop calls for exactly that reason.
    CoolTheGuns(_universe.status);

    // 6502: .NOLASCT LDA QQ11 / BNE P%+5 / JSR DIALS -- every pass on the space view, which is what
    // makes the speed, roll and pitch indicators move at all.
    if (_universe.view == 0u)
    {
      DrawDials(_universe.canvas, _universe.draw, _universe.flight, _universe.status, _commander.fuel, _universe.compass, _universe.bubble);

      /*
       * AND `DIALS` COMES BACK WITH THE CARRY CLEAR, which is what the breeding roll below rotates
       * in on this path. Measured with §6.118's instrument -- stopped at `plus13` with the flag set
       * on entry and clear on entry, and it is clear both times -- rather than derived, because
       * `DIALS` is four parts and ends `JMP COMPAS`. Recorded as a measurement and not a proof.
       */
      carry = false;
    }

    /*
     * 6502: LDA QQ11 / BEQ plus13 / AND PATG / LSR A / BCS plus13 / LDY #2 / JSR DELAY.
     *
     * A frame of delay on the DOCKED screens only, and only with the author-names option OFF --
     * `LSR A` puts bit 0 of `QQ11 AND PATG` into the carry, and `BCS` skips the delay when it is
     * set. The option is one bit doing two unrelated jobs (§6.121's shape), and this is the second:
     * it also gates five of the spawner's tests.
     */
    if (_universe.view != 0u) // 6502: LDA QQ11 / BEQ plus13
    {
      /*
       * 6502: AND PATG / LSR A / BCS plus13 -- and the `LSR` is BOTH the test and the carry the
       * roll below rotates in. Bit 0 of the view ANDed with the option: set and the delay is
       * skipped and the flag arrives set, clear and the pass waits two frames and the flag arrives
       * clear. One instruction doing the branch and the argument, which is why the option byte has
       * to be passed rather than a bool -- `AND PATG` is a byte operation and only bit 0 survives.
       */
      carry = (static_cast<std::uint8_t>(_universe.view & _authorNames) & 1u) != 0u;
      if (!carry)
      {
        requestedFrames = LOOP_DELAY_FRAMES; // 6502: LDY #2 / JSR DELAY
      }
    }

    /*
     * 6502: .plus13 LDA TRIBBLE+1 / BEQ nobabies / JSR DORND / CMP #220 / LDA TRIBBLE / ADC #0 /
     * STA TRIBBLE / BCC nobabies / INC TRIBBLE+1 / BPL nobabies / DEC TRIBBLE+1.
     *
     * They breed only when there is already more than a byte of them, and the increment is a CARRY
     * rather than an addition: `CMP #220` sets it for 36 values in 256, `ADC #0` adds that one bit
     * to the low byte, and the high byte only moves when the low byte wraps. So the population
     * grows by one about one pass in seven, and `BPL nobabies / DEC TRIBBLE+1` clamps the high byte
     * at 127 by undoing the increment that would have set bit 7.
     */
    std::uint8_t tribbleLow = _commander.tribbles.lo;
    std::uint8_t tribbleHigh = _commander.tribbles.hi;

    if (tribbleHigh != 0u)
    {
      const RngResult roll = _universe.rng.Next(carry);
      carry = roll.value >= TRUMBLE_BREED_ROLL; // 6502: CMP #220

      const AddResult grown = AddWithCarry(tribbleLow, 0u, carry);
      tribbleLow = grown.value;
      carry = grown.carry;

      if (carry) // 6502: BCC nobabies
      {
        ++tribbleHigh;
        if ((tribbleHigh & 0x80u) != 0u) // 6502: BPL nobabies
        {
          --tribbleHigh;
        }
      }

      _commander.tribbles.lo = tribbleLow;
      _commander.tribbles.hi = tribbleHigh;
    }

    /*
     * 6502: .nobabies LDA TRIBBLE+1 / BEQ NOSQUEEK / STA T / LDA CABTMP / CMP #224 / BCS P%+4 /
     * ASL T / JSR DORND / CMP T / BCS NOSQUEEK.
     *
     * How often they squeak scales with how many there are -- `T` is the high byte, DOUBLED unless
     * the cabin is hot -- and `BCS P%+4` steps over the two-byte `ASL T`, so a cabin at 224 or
     * above halves the rate. That is the same threshold the burning uses below, and the routine
     * reads it twice rather than remembering it.
     */
    if (tribbleHigh == 0u)
    {
      return requestedFrames;
    }

    std::uint8_t threshold = tribbleHigh;

    /*
     * 6502: LDA CABTMP / CMP #224 / BCS P%+4 / ASL T -- and BOTH of those set the carry the roll
     * below rotates in. A hot cabin takes the branch and arrives with the compare's flag SET; a
     * cool one runs the `ASL` and arrives with bit 7 of the Trumble count instead. Two paths, two
     * different sources, and the port had the breeding block's flag standing on both.
     */
    carry = _universe.status.cabinTemperature >= TRUMBLE_BURN_TEMPERATURE; // 6502: CMP #224
    if (!carry)
    {
      const ShiftResult doubled = RotateLeftValue(threshold, false); // 6502: ASL T
      threshold = doubled.value;
      carry = doubled.carry;
    }

    const RngResult squeak = _universe.rng.Next(carry);
    carry = squeak.value >= threshold; // 6502: CMP T
    if (carry)
    {
      return requestedFrames; // 6502: BCS NOSQUEEK
    }

    /*
     * 6502: JSR DORND / ORA #64 / TAX / LDA #&80 / LDY CABTMP / CPY #224 / BCC burnthebastards /
     * TXA / AND #15 / TAX / LDA #&F1.
     *
     * Two different noises from one path. A normal squeak is a frequency with bit 6 forced and a
     * sustain of &80; a cabin at 224 or above takes the frequency down to four bits and the sustain
     * to &F1, which is the sound of them dying. The `TAX` / `TXA` round trip is there because A is
     * needed for the sustain in between.
     */
    // 6502: JSR DORND -- and `CMP T` above left the carry CLEAR, because a set one would have
    // taken the `BCS` and returned. So this roll always rotates in a zero.
    const RngResult voice = _universe.rng.Next(carry);
    std::uint8_t frequency = static_cast<std::uint8_t>(voice.value | 0x40u);
    std::uint8_t sustain = 0x80u;

    /*
     * 6502: LDY CABTMP / CPY #&E0 / BCC burnthebastards -- and THAT COMPARE IS THE CARRY (M3-b-2a).
     *
     * The branch decides the sustain and the frequency, and the flag it leaves reaches `NOISE2`:
     * `AND`, `TAX` and `LDA` touch no carry between them and the call. So a burning cabin squeaks
     * with the flag SET and an ordinary one with it clear -- which is the row the plan recorded as
     * "dropped at the seam" when `PlaySoundPitched` had nowhere to put it.
     */
    const bool burning = _universe.status.cabinTemperature >= TRUMBLE_BURN_TEMPERATURE;
    if (burning)
    {
      frequency = static_cast<std::uint8_t>(frequency & 0x0Fu);
      sustain = 0xF1u;
    }

    // 6502: LDY #sfxtrib / JSR NOISE2, and then `.NOSQUEEK JSR TT17`.
    static_cast<void>(PlaySoundEffectPitched(_universe.sound, SoundEffect::Trumbles, sustain, frequency, burning));
    return requestedFrames;
  }

  bool AtConstrictorSystem(const Commander& _commander) noexcept
  {
    // 6502: LDX GCNT / DEX / BNE THEX -- galaxy 2 and no other, and the `DEX` is why: galaxy 1 is
    // GCNT 0, so only GCNT 1 leaves zero behind.
    if (static_cast<std::uint8_t>(_commander.galaxyNumber - 1u) != 0u)
    {
      return false; // 6502: .THEX CLC / RTS
    }

    // 6502: LDA QQ0 / CMP #144 / BNE THEX.
    if (_commander.systemX != 144u)
    {
      return false;
    }

    /*
     * 6502: LDA QQ1 / CMP #33 / BEQ THEX+1.
     *
     * `THEX+1` is the `RTS`, one byte past the `CLC` -- so the match returns with the carry that
     * `CMP #33` left, and an equal compare sets it. Every other path runs the `CLC` and returns
     * clear. The routine's answer IS the carry and it is never in A.
     */
    return _commander.systemY == 33u;
  }

  NewShip SpawnThargoidPair(Bubble& _bubble, Ship& _work, Rng& _rng, const Blueprint*& _blueprint, bool _carryIn) noexcept
  {
    // 6502: JSR Ze -- a block at a fixed distance in a random direction, and a second `DORND`
    // whose answer this routine throws away.
    static_cast<void>(SeedDebris(_work, _rng, _carryIn));

    // 6502: LDA #%11111111 / STA INWK+32 -- hostile, and the fastest AI the byte can express.
    _work.ai = 0xFFu;

    // 6502: LDA #THG / JSR NWSHP -- and the answer is discarded, because the next line is a JMP.
    static_cast<void>(Spawn(_bubble, _work, ShipType::Thargoid, _blueprint));

    /*
     * 6502: LDA #TGL / JMP NWSHP -- a JMP and not a JSR, so `GTHG` returns the THARGON's answer.
     *
     * A bubble with one slot left gets the mothership and no escort and reports success, because
     * the carry that comes back is the second call's. Reproduced rather than tidied: the caller
     * that reads it is part 4's `fothg2`, which ignores it, so the only thing this changes is what
     * a comparison against the shipped routine sees.
     */
    return Spawn(_bubble, _work, ShipType::Thargon, _blueprint);
  }

  /*
   * 6502: what one of `MLOOP`'s spawning parts hands the next (M4-c-3).
   *
   * Every `JMP MLOOPS` and `JMP MLOOP` in parts 1 to 4 means the same thing -- this pass of the
   * spawner is over -- and the port spelled all fourteen of them `return;` inside one 430-line
   * function. Two values, and the fall-through is the other one.
   */
  enum class SpawnPass : std::uint8_t
  {
    Ended,     ///< 6502: .MLOOPS JMP MLOOP -- nothing more happens this pass
    Continued, ///< fall through to the next part
  };

  /*
   * What `MLOOP`'s spawning parts share, in one place (M4-c-3, the shape M4-b gave `LL9`).
   *
   * `carry` IS THE REASON THIS EXISTS. §6.125's finding runs through the whole routine: every
   * `CMP` overwrites the generator's own flag and the next `DORND` rotates in what the compare
   * left, so a single boolean is live across all four parts and thirty-odd statements. Passing it
   * between four functions would be four more `bool _carryIn` parameters -- P11's pattern, which
   * M2-d spent a slice removing from the headers -- so it travels in the frame instead, where it
   * is one named field rather than four signatures.
   *
   * File-local for P5's reason, as `ShipRender` and `TacticFrame` are.
   */
  struct SpawnFrame
  {
    Bubble& bubble;
    Ship& work;
    Rng& rng;
    Commander& commander;
    const CurrentSystem& current;
    std::uint8_t& encounters; ///< 6502: EV -- the encounter counter part 4 rate-limits on
    const Blueprint*& blueprint;

    bool carry = false; ///< 6502: the flag every `DORND` rotates in (§6.125)
  };

  /*
   * ---- parts 1 and 2: the trader, and everything that is not one --------------------------------
   *
   * 6502: JSR DORND / CMP #35 / BCS MTT1, then `BVS MTT4` -- the OVERFLOW flag, which is the one
   * branch in these four parts that reads it. The two parts are one function because the roll that
   * chooses between them is the same roll, and `.whips`'s `JSR NWSHP` is the tail both reach.
   */
  [[nodiscard]] SpawnPass SpawnTraderOrLoner(SpawnFrame& _frame) noexcept
  {
    /*
     * 6502: JSR DORND / CMP #35 / BCS MTT1 -- 35 chances in 256 of anything arriving at all.
     *
     * THE COMPARE OVERWRITES THE GENERATOR'S OWN CARRY, and the next `DORND` rotates in what the
     * compare left rather than what `DORND` returned. §6.125 found six of these in `TACTICS`; this
     * routine has nine, and the port had the first two wrong until the oracle disagreed about a
     * trader's AI byte in an empty bubble.
     */
    const RngResult roll = _frame.rng.Next(_frame.carry);
    _frame.carry = roll.value >= TRADER_ROLL;

    bool toPart3 = _frame.carry;

    // 6502: LDA JUNK / CMP #3 / BCS MTT1 -- and junk counts the canisters and the hermits, so a
    // bubble already littered stops attracting traders. This compare sets the flag too.
    if (!toPart3)
    {
      _frame.carry = _frame.bubble.junk >= JUNK_LIMIT;
      toPart3 = _frame.carry;
    }

    ShipType pendingType = ShipType::None;
    bool spawnPending = false;

    if (!toPart3)
    {
      // 6502: JSR ZINF / LDA #38 / STA INWK+7 -- a clean block at one fixed distance.
      ClearShip(_frame.work);
      _frame.work.z.hi = SPAWN_DISTANCE;

      /*
       * 6502: JSR DORND / STA INWK / STX INWK+3 / AND #%10000000 / STA INWK+2 / TXA /
       * AND #%10000000 / STA INWK+5 / ROL INWK+1 / ROL INWK+1.
       *
       * One random pair gives the x and y low bytes AND both signs, and then `INWK+1` -- the x high
       * byte -- is rotated twice through the _frame.carry the second `AND` left. Two rotations of a byte
       * that `ZINF` has just cleared put the _frame.carry in bit 1, so the x high byte is 0 or 2.
       */
      const RngResult place = _frame.rng.Next(_frame.carry);
      _frame.work.x.lo = place.value;
      _frame.work.y.lo = place.previous;
      _frame.work.x.sgn = static_cast<std::uint8_t>(place.value & 0x80u);

      // 6502: TXA / AND #%10000000 / STA INWK+5 -- and `AND` does not touch the _frame.carry, so the flag
      // the two rotations below shift in is still the one `DORND` returned.
      _frame.work.y.sgn = static_cast<std::uint8_t>(place.previous & 0x80u);
      _frame.carry = place.carry;

      ShiftResult rotated = RotateLeftValue(_frame.work.x.hi, _frame.carry);
      _frame.work.x.hi = rotated.value;
      _frame.carry = rotated.carry;
      rotated = RotateLeftValue(_frame.work.x.hi, _frame.carry);
      _frame.work.x.hi = rotated.value;
      _frame.carry = rotated.carry;

      // 6502: JSR DORND / BVS MTT4 -- the OVERFLOW flag, which is the one branch in these four
      // parts that reads it. Set means part 1: a trader.
      const RngResult kind = _frame.rng.Next(_frame.carry);
      _frame.carry = kind.carry;

      if (kind.overflow)
      {
        /*
         * 6502: .MTT4 -- part 1, the trader.
         *
         * `LSR A` halves the byte and pushes bit 0 into the _frame.carry; the same value becomes the AI
         * byte and the roll counter, and the _frame.carry is rotated into `INWK+31` before `AND #31 /
         * ORA #16` makes a speed between 16 and 31.
         */
        const RngResult trader = _frame.rng.Next(_frame.carry);
        const ShiftResult halved = {static_cast<std::uint8_t>(trader.value >> 1u), (trader.value & 1u) != 0u};
        _frame.work.ai = halved.value;
        _frame.work.rollCounter = halved.value;

        const ShiftResult flags = RotateLeftValue(_frame.work.state, halved.carry);
        _frame.work.state = flags.value;
        _frame.carry = flags.carry;

        _frame.work.speed = static_cast<std::uint8_t>((halved.value & 31u) | 16u);

        /*
         * 6502: JSR DORND / BMI nodo -- a NEGATIVE byte skips the escort flag entirely, so half
         * the traders fly with `NEWB` bit 4 set and half with whatever `ZINF` left.
         */
        const RngResult escort = _frame.rng.Next(_frame.carry);
        _frame.carry = escort.carry;
        std::uint8_t a = escort.value;

        if ((a & 0x80u) == 0u)
        {
          /*
           * 6502: LDA INWK+32 / ORA #%11000000 / STA INWK+32 / LDX #%00010000 / STX NEWB.
           *
           * A IS NOT THE ROLL ANY MORE. `LDA INWK+32` replaced it and the `ORA` changed it again,
           * so the `AND #2` below -- which chooses the ship type -- runs on the AI BYTE on this
           * path and on the `DORND` byte on the other. Two different quantities reaching the same
           * instruction, which is the shape §6.73 keeps finding, and the port had it as the roll
           * on both paths until the oracle disagreed on the type in an empty bubble.
           */
          _frame.work.ai = With(_frame.work.ai, AiBit::Active, AiBit::Hostile);
          _frame.work.newb = Mask(NewbBit::Docking);
          a = _frame.work.ai;
        }

        /*
         * 6502: AND #2 / ADC #CYL / CMP #HER / BEQ TT100 / JSR NWSHP.
         *
         * `CYL` is 11 and the `AND` leaves 0 or 2, so the type is 11 to 14 and `CMP #HER` -- 15 --
         * CANNOT be equal on this build. The branch back to the top of the loop is dead code here,
         * and it is transcribed rather than dropped because what makes it dead is two constants
         * this version happens to choose (§6.121's rule about idioms that look like something
         * else).
         */
        const AddResult type = AddWithCarry(static_cast<std::uint8_t>(a & 2u), Byte(ShipType::CobraMk3), _frame.carry);
        _frame.carry = type.carry;

        if (TypeOf(type.value) == ShipType::RockHermit)
        {
          return SpawnPass::Ended; // 6502: BEQ TT100 -- unreachable on the C64 constants
        }

        pendingType = TypeOf(type.value);
        spawnPending = true;
      }
      else
      {
        // 6502: ORA #%01101111 / STA INWK+29 -- a hard roll, on the byte `BVS` did not take.
        _frame.work.rollCounter = static_cast<std::uint8_t>(kind.value | 0x6Fu);

        /*
         * 6502: LDA SSPR / BNE MTT1 -- inside the station's sphere nothing drifts in.
         *
         * THE BRANCH IS THIS `if` AND NOT A FLAG. `toPart3` decided, four hundred lines above,
         * whether this block runs at all; setting it again here would be read by nothing, because
         * falling out of the block IS reaching part 3. It was set anyway until slice 5d, which is
         * the fourth vestigial assignment this port has carried from a transcription of a branch.
         */
        if (_frame.bubble.StationPresent() == 0u)
        {
          /*
           * 6502: TXA / BCS MTT2 / AND #31 / ORA #16 / STA INWK+27 / BCC MTT3, and `.MTT2 ORA
           * #%01111111 / STA INWK+30`.
           *
           * The _frame.carry decides whether the byte becomes a speed or a pitch, and `BCC MTT3` after a
           * path that cannot have set the _frame.carry is an unconditional jump.
           */
          const std::uint8_t x = kind.previous;
          if (_frame.carry)
          {
            _frame.work.pitchCounter = static_cast<std::uint8_t>(x | 0x7Fu);
          }
          else
          {
            _frame.work.speed = static_cast<std::uint8_t>((x & 31u) | 16u);
          }

          // 6502: .MTT3 JSR DORND / CMP #252 / BCC thongs.
          const RngResult cargo = _frame.rng.Next(_frame.carry);
          _frame.carry = cargo.carry;

          if (cargo.value >= HERMIT_ROLL)
          {
            // 6502: LDA #HER / STA INWK+32 / BNE whips -- and `HER` is 15, so the `BNE` is a JMP.
            _frame.work.ai = Byte(ShipType::RockHermit);
            pendingType = ShipType::RockHermit;
          }
          else
          {
            /*
             * 6502: .thongs CMP #10 / AND #1 / ADC #OIL.
             *
             * The `CMP #10` sets the _frame.carry and its ANSWER IS NEVER TESTED -- it is there to feed
             * the `ADC` below, so a byte of 10 or more adds one. With `AND #1` giving 0 or 1 the
             * type is 5, 6 or 7: a canister, an alloy plate or an asteroid.
             */
            const bool ten = cargo.value >= 10u;
            const AddResult junkType = AddWithCarry(static_cast<std::uint8_t>(cargo.value & 1u), Byte(ShipType::Canister), ten);
            _frame.carry = junkType.carry;
            pendingType = TypeOf(junkType.value);
          }
          spawnPending = true;
        }
      }
    }

    // 6502: .whips JSR NWSHP -- and then it FALLS INTO part 3 whatever the answer was.
    if (spawnPending)
    {
      static_cast<void>(Spawn(_frame.bubble, _frame.work, pendingType, _frame.blueprint));
    }

    return SpawnPass::Continued;
  }

  /*
   * ---- part 3: the police -----------------------------------------------------------------------
   *
   * 6502: .MTT1 LDA SSPR / BEQ P%+5 / .MLOOPS JMP MLOOP -- and `BEQ P%+5` steps over a three-byte
   * `JMP`, so a station in range sends the pass back to the top: no police inside the safe zone,
   * and parts 4, 5 and 6 do not run either.
   */
  [[nodiscard]] SpawnPass SpawnPolice(SpawnFrame& _frame) noexcept
  {
    /*
     * 6502: .MTT1 LDA SSPR / BEQ P%+5 / .MLOOPS JMP MLOOP -- part 3, the police.
     *
     * `BEQ P%+5` steps over a three-byte `JMP`, so a station in range sends the pass BACK to the
     * top: no police spawn inside the safe zone, and parts 5 and 6 do not run either.
     */
    if (_frame.bubble.StationPresent() != 0u)
    {
      return SpawnPass::Ended;
    }

    /*
     * 6502: JSR BAD / ASL A / LDX MANY+COPS / BEQ P%+5 / ORA FIST / STA T.
     *
     * What the hold is worth in trouble, doubled, and the legal status ORed in ONLY IF there is
     * already a Viper about -- `BEQ P%+5` skips the two-byte `ORA` and the two-byte `STA`... no:
     * it skips `ORA FIST` (2 bytes) and lands on `STA T` (2 bytes), because P%+5 counts from the
     * branch. So a clean bubble stores the doubled cargo alone.
     */
    const std::uint8_t penalty = ContrabandPenalty(_frame.commander);
    const ShiftResult doubled = {static_cast<std::uint8_t>(penalty << 1u), (penalty & 0x80u) != 0u};
    _frame.carry = doubled.carry; // 6502: ASL A -- and nothing between here and `Ze` touches the flag

    std::uint8_t threshold = doubled.value;
    if (_frame.bubble.Count(ShipType::Viper) != 0u)
    {
      threshold = static_cast<std::uint8_t>(doubled.value | _frame.commander.legalStatus);
    }

    // 6502: JSR Ze / CMP #136 / BEQ fothg -- one byte in 256 goes to the Cougar path.
    RngResult ze = SeedDebris(_frame.work, _frame.rng, _frame.carry);
    _frame.carry = ze.value == COUGAR_BYTE; // 6502: CMP #136

    if (ze.value == COUGAR_BYTE)
    {
      /*
       * 6502: .fothg LDA K%+6 / AND #%00111110 / BNE fothg2 -- byte 6 of the PLANET's block, which
       * is the low byte of its z coordinate, masked to five bits. Non-zero and this is a Thargoid
       * after all; zero and it is the Cougar, which is the rarest thing in the game.
       */
      if ((_frame.bubble.blocks[0].z.lo & 0x3Eu) != 0u)
      {
        static_cast<void>(SpawnThargoidPair(_frame.bubble, _frame.work, _frame.rng, _frame.blueprint, _frame.carry)); // 6502: fothg2
        return SpawnPass::Ended; // 6502: .mj1 JMP MLOOP
      }

      // 6502: LDA #18 / STA INWK+27 / LDA #%01111001 / STA INWK+32 / LDA #COU / BNE focoug.
      _frame.work.speed = 18u;
      _frame.work.ai = 0x79u;
      static_cast<void>(Spawn(_frame.bubble, _frame.work, ShipType::Cougar, _frame.blueprint));
      return SpawnPass::Ended;
    }

    /*
     * 6502: CMP T / BCS P%+7 / LDA #COPS / JSR NWSHP.
     *
     * `P%+7` counts from the branch: two bytes of `BCS`, then `LDA #COPS` (2) and `JSR NWSHP` (3)
     * make five, so the branch skips BOTH. A roll at or above the threshold means no policeman.
     */
    _frame.carry = ze.value >= threshold; // 6502: CMP T
    if (!_frame.carry)
    {
      // 6502: LDA #COPS / JSR NWSHP -- and `NWSHP` returns its own _frame.carry, which is the flag any
      // later `DORND` on this path rotates in.
      _frame.carry = Spawn(_frame.bubble, _frame.work, ShipType::Viper, _frame.blueprint).created;
    }

    // 6502: LDA MANY+COPS / BNE MLOOPS -- and this reads the count AFTER the spawn, so one Viper
    // in the bubble ends the pass whether it arrived just now or was already there.
    if (_frame.bubble.Count(ShipType::Viper) != 0u)
    {
      return SpawnPass::Ended;
    }

    return SpawnPass::Continued;
  }

  /*
   * ---- part 4: the encounter counter, and what it lets through ---------------------------------
   *
   * 6502: .DEC EV / BPL MLOOPS / INC EV -- a rate limit: it counts down and only a pass that takes
   * it negative gets any further, and then it is put back so the next pass tries again.
   */
  [[nodiscard]] SpawnPass SpawnEncounter(SpawnFrame& _frame) noexcept
  {
    /*
     * 6502: part 4. .DEC EV / BPL MLOOPS / INC EV -- the encounter counter, which is a rate limit:
     * it counts down and only a pass that takes it negative gets any further, and then it is put
     * back so the next pass tries again.
     */
    --_frame.encounters;
    if ((_frame.encounters & 0x80u) == 0u)
    {
      return SpawnPass::Ended;
    }
    ++_frame.encounters;

    // 6502: LDA TP / AND #%00001100 / CMP #%00001000 / BNE nopl -- mission 1 at stage 2, which is
    // when the Thargoids start hunting you.
    const std::uint8_t stage = static_cast<std::uint8_t>(_frame.commander.missionProgress & 0x0Cu);
    _frame.carry = stage >= 0x08u; // 6502: CMP #%00001000, and the flag outlives the BNE

    if (stage == 0x08u)
    {
      // 6502: JSR DORND / CMP #200 / BCC nopl / .fothg2 JSR GTHG.
      const RngResult thargoid = _frame.rng.Next(_frame.carry);
      _frame.carry = thargoid.value >= THARGOID_ROLL; // 6502: CMP #200
      if (_frame.carry)
      {
        static_cast<void>(SpawnThargoidPair(_frame.bubble, _frame.work, _frame.rng, _frame.blueprint, _frame.carry));
        return SpawnPass::Ended; // 6502: .mj1 JMP MLOOP
      }
    }

    /*
     * 6502: .nopl JSR DORND / LDY gov / BEQ LABEL_2 / CMP #90 / BCS MLOOPS / AND #7 / CMP gov /
     * BCC MLOOPS.
     *
     * Anarchy -- government 0 -- always spawns. Everywhere else needs a byte under 90 AND its low
     * three bits to reach the government's own number, so a corporate state is nearly safe.
     */
    const RngResult law = _frame.rng.Next(_frame.carry);
    _frame.carry = law.carry;

    if (_frame.current.government != 0u)
    {
      _frame.carry = law.value >= GOVERNMENT_ROLL; // 6502: CMP #90
      if (_frame.carry)
      {
        return SpawnPass::Ended;
      }
      // 6502: AND #7 / CMP gov / BCC MLOOPS -- and this compare is the one `Ze` below rotates in.
      _frame.carry = static_cast<std::uint8_t>(law.value & 7u) >= _frame.current.government;
      if (!_frame.carry)
      {
        return SpawnPass::Ended;
      }
    }

    // 6502: .LABEL_2 JSR Ze / CMP #100 / BCS mt1 -- above 100 it is a pack of pirates.
    //
    // `Ze` IS CALLED TWICE, once in part 3 and once here, and the port had ONE variable for both
    // because it was one function. Part 3's roll is dead by the time this runs -- every read of it
    // is behind the `fothg` branch, which returns -- so this is a fresh local and not the frame's
    // (M4-c-3).
    const RngResult ze = SeedDebris(_frame.work, _frame.rng, _frame.carry);
    _frame.carry = ze.value >= PIRATE_ROLL; // 6502: CMP #100

    if (_frame.carry)
    {
      /*
       * 6502: .mt1 AND #3 / STA EV / STA XX13 / .mt3 ... DEC XX13 / BPL mt3.
       *
       * The low two bits become BOTH the encounter counter and the loop count, so a pass that
       * spawns four pirates also sets the longest cooldown. One to four of them, and each is
       * `DORND AND DORND AND 7` -- two rolls ANDed, so the low types are far more likely.
       */
      const std::uint8_t count = static_cast<std::uint8_t>(ze.value & 3u);
      _frame.encounters = count;

      for (int remaining = static_cast<int>(count); remaining >= 0; --remaining)
      {
        const RngResult first = _frame.rng.Next(_frame.carry);
        const RngResult second = _frame.rng.Next(first.carry);
        _frame.carry = second.carry;

        const std::uint8_t masked = static_cast<std::uint8_t>(static_cast<std::uint8_t>(second.value & first.value) & 7u);
        const AddResult pack = AddWithCarry(masked, Byte(ShipType::Sidewinder), _frame.carry);
        _frame.carry = pack.carry;

        // 6502: JSR NWSHP / DEC XX13 / BPL mt3 -- so the NEXT pass's first `DORND` rotates in the
        // _frame.carry `NWSHP` returned, not the one the `ADC` above left.
        _frame.carry = Spawn(_frame.bubble, _frame.work, TypeOf(pack.value), _frame.blueprint).created;
      }

      // 6502: the loop's fall-through IS part 5.
      return SpawnPass::Ended;
    }

    /*
     * 6502: INC EV / AND #3 / ADC #CYL2 / TAY / JSR THERE / BCC NOCON -- a lone bounty hunter, and
     * `THERE` is asked whether this is the Constrictor's system.
     */
    ++_frame.encounters;
    const AddResult hunter = AddWithCarry(static_cast<std::uint8_t>(ze.value & 3u), Byte(ShipType::CobraMk3Pirate), _frame.carry);
    _frame.carry = hunter.carry;
    const std::uint8_t y = hunter.value;

    // 6502: JSR THERE -- and its answer IS the _frame.carry, so the flag survives into what follows.
    _frame.carry = AtConstrictorSystem(_frame.commander);

    bool constrictor = false;
    if (_frame.carry)
    {
      /*
       * 6502: LDA #%11111001 / STA INWK+32 / LDA TP / AND #%00000011 / LSR A / BCC NOCON /
       * ORA MANY+CON / BEQ YESCON.
       *
       * The AI byte is set BEFORE the mission test, so a hunter in that system is hostile whether
       * or not it turns out to be the Constrictor. Then mission 1 has to be at stage 1 -- the
       * `LSR` puts bit 0 in the _frame.carry -- and the Constrictor must not already be in the bubble.
       */
      _frame.work.ai = 0xF9u;

      const std::uint8_t stage = static_cast<std::uint8_t>(_frame.commander.missionProgress & 3u);
      const ShiftResult shifted = {static_cast<std::uint8_t>(stage >> 1u), (stage & 1u) != 0u};

      // 6502: LSR A -- and `ORA`, `BEQ`, `LDA` and `STA` all leave the flag alone, so this is what
      // the `DORND` down in `NOCON` rotates in.
      _frame.carry = shifted.carry;

      if (_frame.carry)
      {
        constrictor = static_cast<std::uint8_t>(shifted.value | _frame.bubble.Count(ShipType::Constrictor)) == 0u;
      }
    }

    ShipType hunterType = ShipType::None;
    if (constrictor)
    {
      hunterType = ShipType::Constrictor; // 6502: .YESCON LDA #CON
    }
    else
    {
      /*
       * 6502: .NOCON LDA #%00000100 / STA NEWB / JSR DORND / CMP #200 / ROL A / ORA #%11000000 /
       * STA INWK+32 / TYA / EQUB &2C.
       *
       * `EQUB &2C` is `BIT abs`, which swallows the two bytes of `LDA #CON` that follow -- so the
       * `TYA` reaches `focoug` with the type `LABEL_2` computed and the Constrictor's `LDA` is
       * stepped over. The `CMP #200` is again there only for its CARRY, which `ROL A` shifts into
       * bit 0 of the AI byte.
       */
      _frame.work.newb = Mask(NewbBit::Hostile);
      const RngResult ai = _frame.rng.Next(_frame.carry);
      const ShiftResult rolled = RotateLeftValue(ai.value, ai.value >= THARGOID_ROLL);
      _frame.carry = rolled.carry;
      _frame.work.ai = With(rolled.value, AiBit::Active, AiBit::Hostile);
      hunterType = TypeOf(y);
    }

    // 6502: .focoug JSR NWSHP / .mj1 JMP MLOOP.
    static_cast<void>(Spawn(_frame.bubble, _frame.work, hunterType, _frame.blueprint));
    return SpawnPass::Ended;
  }

  void RunSpawning(Universe& _universe, bool _carryIn) noexcept
  {
    // 6502: LDA MJ / BNE ytq -- nothing spawns in witchspace, because witchspace has no system to
    // spawn from. `MJP` puts the Thargoids there itself.
    if (_universe.status.midJump != 0u)
    {
      return;
    }

    SpawnFrame frame{_universe.bubble,           _universe.work,    _universe.rng,
                     _universe.commander,        _universe.current, _universe.explosions,
                     _universe.flight.blueprint, _carryIn};

    if (SpawnTraderOrLoner(frame) == SpawnPass::Ended) // 6502: parts 1 and 2
    {
      return;
    }

    if (SpawnPolice(frame) == SpawnPass::Ended) // 6502: part 3
    {
      return;
    }

    static_cast<void>(SpawnEncounter(frame)); // 6502: part 4, and the loop's fall-through IS part 5
  }

} // namespace Elite
