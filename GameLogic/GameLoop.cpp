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
     * One spawn, with the type in the accumulator and the block already built.
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
     * The delay counted down, with `me2` at zero and a floor under it.
     *
     * The floor is what stops it wrapping: a `DLY` of zero decrements to 255, which is negative,
     * so the sign test falls through and an increment puts it back. Only a `DLY` of exactly 1
     * reaches `me2`.
     */
    const std::uint8_t delayed = static_cast<std::uint8_t>(_universe.message.delay - 1u);
    _universe.message.delay = delayed;

    if (delayed == 0u)
    {
      // A text screen needs its message rows cleared, a space view does not.
      if (_universe.view != 0u)
      {
        // A text screen's message is in the bottom rows. A seam until M3-b-3b.
        ClearMessageRows(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message,
                       &_universe.picture, _universe.screenLayout);
      }
      else
      {
        /*
         * The remembered token sent again, then the delay cleared.
         *
         * Sending the SAME token again is what erases it: the printer EORs, so the second print of
         * a message rubs out the first. And `MESS` sets `DLY` to twenty, which is why clearing it
         * afterwards is not redundant -- it undoes what the call just did.
         */
        ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, _universe.message.token,
                    _universe.view, &_universe.picture);
        _universe.message.delay = 0u;
      }
    }
    else if ((delayed & 0x80u) != 0u)
    {
      _universe.message.delay = static_cast<std::uint8_t>(delayed + 1u); // The floor
    }

    // The main counter down one, and reaching zero is the ONE PASS IN 256 that gets to
    // everything slice 4c-a built.
    --_universe.flight.mainLoopCounter;
    return _universe.flight.mainLoopCounter == 0u ? LoopHead::Spawn : LoopHead::SkipSpawning;
  }

  void CoolTheGuns(FlightStatus& _status) noexcept
  {
    // The laser cools by one every pass, docked or flying, because this sits above part 5's
    // view gate.
    if (_status.laserTemperature != 0u)
    {
      --_status.laserTemperature;
    }

    /*
     * The laser countdown, decremented TWICE with a zero test between.
     *
     * That middle test is why it never passes zero: the first decrement can land on it and the
     * branch then skips the second. So an odd countdown stops at zero and an even one steps through
     * it, and a port that subtracted two would go negative on the odd values.
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

    // The two countdowns above the view gate, which a docked pass reaches as well -- see
    // `CoolTheGuns`, which the executable's docked loop calls for exactly that reason.
    CoolTheGuns(_universe.status);

    // `DIALS` on every pass of the space view, which is what makes the speed, roll
    // and pitch indicators move at all.
    if (_universe.view == 0u)
    {
      DrawDials(_universe.canvas, _universe.draw, _universe.flight, _universe.status, _commander.fuel, _universe.compass, _universe.bubble,
                &_universe.picture);

      /*
       * AND `DIALS` COMES BACK WITH THE CARRY CLEAR, which is what the breeding roll below rotates
       * in on this path. Measured with §6.118's instrument -- stopped at `plus13` with the flag set
       * on entry and clear on entry, and it is clear both times -- rather than derived, because
       * `DIALS` is four parts and ends by jumping into the compass. Recorded as a measurement and
       * not a proof.
       */
      carry = false;
    }

    /*
     * The view masked with the option byte, shifted, and the delay skipped on the result.
     *
     * A frame of delay on the DOCKED screens only, and only with the author-names option OFF: the
     * shift puts bit 0 of that mask into the carry, and a set carry skips the delay. The option is
     * one bit doing two unrelated jobs (§6.121's shape), and this is the second: it also gates five
     * of the spawner's tests.
     */
    if (_universe.view != 0u) // A space view goes straight to plus13
    {
      /*
       * The shift is BOTH the test and the carry the roll below rotates in.
       *
       * Bit 0 of the view masked with the option: set and the delay is skipped and the flag arrives
       * set, clear and the pass waits two frames and the flag arrives clear. One instruction doing
       * the branch and the argument, which is why the option byte has to be passed rather than a
       * bool -- the mask is a byte operation and only bit 0 survives it.
       */
      carry = (static_cast<std::uint8_t>(_universe.view & _authorNames) & 1u) != 0u;
      if (!carry)
      {
        requestedFrames = LOOP_DELAY_FRAMES; // Two frames of DELAY
      }
    }

    /*
     * A roll, its carry added into the low byte, and the high byte clamped.
     *
     * They breed only when there is already more than a byte of them, and the increment is a CARRY
     * rather than an addition: the comparison sets it for 36 values in 256, adding zero folds that
     * one bit into the low byte, and the high byte only moves when the low byte wraps. So the
     * population grows by one about one pass in seven, and the clamp holds the high byte at 127 by
     * undoing the increment that would have set bit 7.
     */
    std::uint8_t tribbleLow = _commander.tribbles.lo;
    std::uint8_t tribbleHigh = _commander.tribbles.hi;

    if (tribbleHigh != 0u)
    {
      const RngResult roll = _universe.rng.Next(carry);
      carry = roll.value >= TRUMBLE_BREED_ROLL; // The breeding threshold

      const AddResult grown = AddWithCarry(tribbleLow, 0u, carry);
      tribbleLow = grown.value;
      carry = grown.carry;

      if (carry) // Only a wrap of the low byte reaches the high one
      {
        ++tribbleHigh;
        if ((tribbleHigh & 0x80u) != 0u) // The clamp at 127
        {
          --tribbleHigh;
        }
      }

      _commander.tribbles.lo = tribbleLow;
      _commander.tribbles.hi = tribbleHigh;
    }

    /*
     * The population doubled unless the cabin is hot, then rolled against.
     *
     * How often they squeak scales with how many there are, and the branch that skips the doubling
     * steps over two bytes, so a cabin at 224 or above halves the rate. That is the same threshold
     * the burning uses below, and the routine reads it twice rather than remembering it.
     */
    if (tribbleHigh == 0u)
    {
      return requestedFrames;
    }

    std::uint8_t threshold = tribbleHigh;

    /*
     * BOTH the temperature test and the doubling set the carry the roll below rotates in.
     *
     * A hot cabin takes the branch and arrives with the comparison's flag SET; a cool one runs the
     * doubling and arrives with bit 7 of the Trumble count instead. Two paths, two different
     * sources, and the port had the breeding block's flag standing on both.
     */
    carry = _universe.status.cabinTemperature >= TRUMBLE_BURN_TEMPERATURE; // The burn threshold
    if (!carry)
    {
      const ShiftResult doubled = RotateLeftValue(threshold, false); // The count doubled
      threshold = doubled.value;
      carry = doubled.carry;
    }

    const RngResult squeak = _universe.rng.Next(carry);
    carry = squeak.value >= threshold; // The roll against it
    if (carry)
    {
      return requestedFrames; // No squeak this pass
    }

    /*
     * A roll for the frequency, then the temperature decides the frequency and the sustain.
     *
     * Two different noises from one path. A normal squeak is a frequency with bit 6 forced and a
     * sustain of &80; a cabin at 224 or above takes the frequency down to four bits and the sustain
     * to &F1, which is the sound of them dying. The frequency goes through the index register only
     * because the accumulator is needed for the sustain in between.
     */
    // And the comparison above left the carry CLEAR, because a set one would have taken the
    // branch and returned. So this roll always rotates in a zero.
    const RngResult voice = _universe.rng.Next(carry);
    std::uint8_t frequency = static_cast<std::uint8_t>(voice.value | 0x40u);
    std::uint8_t sustain = 0x80u;

    /*
     * The temperature compared again -- and THAT COMPARISON IS THE CARRY (M3-b-2a).
     *
     * The branch decides the sustain and the frequency, and the flag it leaves reaches `NOISE2`:
     * nothing between the comparison and the call touches it. So a burning cabin squeaks
     * with the flag SET and an ordinary one with it clear -- which is the row the plan recorded as
     * "dropped at the seam" when `PlaySoundPitched` had nowhere to put it.
     */
    const bool burning = _universe.status.cabinTemperature >= TRUMBLE_BURN_TEMPERATURE;
    if (burning)
    {
      frequency = static_cast<std::uint8_t>(frequency & 0x0Fu);
      sustain = 0xF1u;
    }

    // NOISE2 with the Trumble effect, and then `NOSQUEEK` reads the keyboard.
    static_cast<void>(PlaySoundEffectPitched(_universe.sound, SoundEffect::Trumbles, sustain, frequency, burning));
    return requestedFrames;
  }

  bool AtConstrictorSystem(const Commander& _commander) noexcept
  {
    // Galaxy 2 and no other, and the decrement is why: galaxy 1 is `GCNT` 0, so only `GCNT` 1
    // leaves zero behind.
    if (static_cast<std::uint8_t>(_commander.galaxyNumber - 1u) != 0u)
    {
      return false; // Clear carry and return
    }

    // The system's x coordinate, which is 144 for this one.
    if (_commander.systemX != 144u)
    {
      return false;
    }

    /*
     * The system's y coordinate against 33, and a match branches to `THEX+1`.
     *
     * `THEX+1` is the RETURN, one byte past the carry-clearing instruction -- so a match comes back
     * with the flag that comparison left, and an equal comparison sets it. Every other path clears
     * the carry on the way out. The routine's answer IS the carry and is never in a register.
     */
    return _commander.systemY == 33u;
  }

  NewShip SpawnThargoidPair(Bubble& _bubble, Ship& _work, Rng& _rng, const Blueprint*& _blueprint, bool _carryIn) noexcept
  {
    // A block at a fixed distance in a random direction, and a second roll whose answer
    // this routine throws away.
    static_cast<void>(SeedDebris(_work, _rng, _carryIn));

    // Hostile, and the fastest AI the byte can express.
    _work.ai = 0xFFu;

    // The mothership, and its answer is discarded because the next line is a jump.
    static_cast<void>(Spawn(_bubble, _work, ShipType::Thargoid, _blueprint));

    /*
     * The escort, reached by a JUMP rather than a call, so `GTHG` returns the THARGON's
     * answer.
     *
     * A bubble with one slot left gets the mothership and no escort and reports success, because
     * the carry that comes back is the second call's. Reproduced rather than tidied: the caller
     * that reads it is part 4's `fothg2`, which ignores it, so the only thing this changes is what
     * a comparison against the shipped routine sees.
     */
    return Spawn(_bubble, _work, ShipType::Thargon, _blueprint);
  }

  /*
   * What one of `MLOOP`'s spawning parts hands the next (M4-c-3).
   *
   * Every jump back to `MLOOP` in parts 1 to 4 means the same thing -- this pass of the spawner is
   * over -- and the port spelled all fourteen of them `return;` inside one 430-line function. Two
   * values, and the fall-through is the other one.
   */
  enum class SpawnPass : std::uint8_t
  {
    Ended,     ///< Nothing more happens this pass
    Continued, ///< fall through to the next part
    Restarted, ///< Part 1's tail falling into `TT100`, which is `SpawnOutcome::Restarted`
  };

  /*
   * What `MLOOP`'s spawning parts share, in one place (M4-c-3, the shape M4-b gave `LL9`).
   *
   * `carry` IS THE REASON THIS EXISTS. §6.125's finding runs through the whole routine: every
   * comparison overwrites the generator's own flag and the next roll rotates in what it left, so a
   * single boolean is live across all four parts and thirty-odd statements. Passing it
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
    std::uint8_t& encounters; ///< The encounter counter part 4 rate-limits on
    const Blueprint*& blueprint;

    bool carry = false; ///< The flag every roll rotates in (§6.125)
  };

  /*
   * ---- parts 1 and 2: the trader, and everything that is not one --------------------------------
   *
   * A roll decides whether anything arrives, and a second one's OVERFLOW flag decides which
   * part gets it -- the one branch in these four parts that reads that flag. The two parts are one
   * function because the roll choosing between them is the same roll, and `whips` is the tail both
   * reach.
   */
  [[nodiscard]] SpawnPass SpawnTraderOrLoner(SpawnFrame& _frame) noexcept
  {
    /*
     * 35 chances in 256 of anything arriving at all.
     *
     * THE COMPARISON OVERWRITES THE GENERATOR'S OWN CARRY, and the next roll rotates in what the
     * comparison left rather than what the generator returned. §6.125 found six of these in
     * `TACTICS`; this
     * routine has nine, and the port had the first two wrong until the oracle disagreed about a
     * trader's AI byte in an empty bubble.
     */
    const RngResult roll = _frame.rng.Next(_frame.carry);
    _frame.carry = roll.value >= TRADER_ROLL;

    bool toPart3 = _frame.carry;

    // Junk counts the canisters and the hermits, so a bubble already littered stops
    // attracting traders. This comparison sets the flag too.
    if (!toPart3)
    {
      _frame.carry = _frame.bubble.junk >= JUNK_LIMIT;
      toPart3 = _frame.carry;
    }

    ShipType pendingType = ShipType::None;
    bool spawnPending = false;
    bool traderPath = false; ///< The overflow branch was taken, so the tail is part 1's

    if (!toPart3)
    {
      // A clean block at one fixed distance.
      ClearShip(_frame.work);
      _frame.work.z.hi = SPAWN_DISTANCE;

      /*
       * One roll supplies both low bytes and both signs, then the x high byte is rotated
       * twice.
       *
       * The rotations shift in the carry the masking left, and `ZINF` has just cleared the byte, so
       * two of them put that flag in bit 1 -- which makes the x high byte 0 or 2 and nothing else.
       */
      const RngResult place = _frame.rng.Next(_frame.carry);
      _frame.work.x.lo = place.value;
      _frame.work.y.lo = place.previous;
      _frame.work.x.sgn = static_cast<std::uint8_t>(place.value & 0x80u);

      // Masking does not touch the carry, so the flag the two rotations below shift in is
      // still the one the generator returned.
      _frame.work.y.sgn = static_cast<std::uint8_t>(place.previous & 0x80u);
      _frame.carry = place.carry;

      ShiftResult rotated = RotateLeftValue(_frame.work.x.hi, _frame.carry);
      _frame.work.x.hi = rotated.value;
      _frame.carry = rotated.carry;
      rotated = RotateLeftValue(_frame.work.x.hi, _frame.carry);
      _frame.work.x.hi = rotated.value;
      _frame.carry = rotated.carry;

      // The OVERFLOW flag, which is the one branch in these four parts that reads it. Set
      // means part 1: a trader.
      const RngResult kind = _frame.rng.Next(_frame.carry);
      _frame.carry = kind.carry;

      if (kind.overflow)
      {
        /*
         * Part 1, the trader.
         *
         * The roll is halved, pushing bit 0 into the carry; the same value becomes the AI byte and
         * the roll counter, and that carry is rotated into the state byte before the low five bits
         * make a speed between 16 and 31.
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
         * A NEGATIVE roll skips the escort flag entirely, so half the traders fly
         * with the docking trait set and half with whatever `ZINF` left.
         */
        const RngResult escort = _frame.rng.Next(_frame.carry);
        _frame.carry = escort.carry;
        std::uint8_t a = escort.value;

        if ((a & 0x80u) == 0u)
        {
          /*
           * The AI byte gets two more bits set, and the traits become docking.
           *
           * THE ACCUMULATOR IS NOT THE ROLL ANY MORE. Reading the AI byte replaced it and setting
           * those bits changed it again, so the mask below -- which chooses the ship type -- runs
           * on the AI BYTE on this path and on the roll on the other. Two different quantities
           * reaching the same instruction, which is the shape §6.73 keeps finding, and the port
           * had it as the roll on both paths until the oracle disagreed on the type in an empty
           * bubble.
           */
          _frame.work.ai = With(_frame.work.ai, AiBit::Active, AiBit::Hostile);
          _frame.work.traits = Mask(TraitBit::Docking);
          a = _frame.work.ai;
        }

        /*
         * One bit of the value added to the Cobra's type, then compared against the hermit's.
         *
         * The Cobra is 11 and the mask leaves 0 or 2, so the type is 11 to 14 and the hermit's 15
         * CANNOT be equal on this build. The branch back to the top of the loop is dead code here,
         * and it is kept rather than dropped because what makes it dead is two constants this
         * version happens to choose (§6.121's rule about idioms that look like something else).
         */
        const AddResult type = AddWithCarry(static_cast<std::uint8_t>(a & 2u), Byte(ShipType::CobraMk3), _frame.carry);
        _frame.carry = type.carry;

        if (TypeOf(type.value) == ShipType::RockHermit)
        {
          // Unreachable on the C64 constants, and it is the SAME destination part
          // 1's fall-through reaches, so it says so rather than pretending the pass ended.
          return SpawnPass::Restarted;
        }

        pendingType = TypeOf(type.value);
        spawnPending = true;
        traderPath = true;
      }
      else
      {
        // A hard roll, built from the byte the overflow branch did not take.
        _frame.work.rollCounter = static_cast<std::uint8_t>(kind.value | 0x6Fu);

        /*
         * Inside the station's sphere nothing drifts in.
         *
         * THE BRANCH IS THIS `if` AND NOT A FLAG. `toPart3` decided, four hundred lines above,
         * whether this block runs at all; setting it again here would be read by nothing, because
         * falling out of the block IS reaching part 3. It was set anyway until slice 5d, which is
         * the fourth vestigial assignment this port has carried from a transcription of a branch.
         */
        if (_frame.bubble.StationPresent() == 0u)
        {
          /*
           * MTT2 and MTT3 -- the carry decides whether the byte becomes a pitch or a speed.
           *
           * The branch onwards sits after a path that cannot have set the carry, so it is an
           * unconditional jump written as a conditional one.
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

          // A roll against 252 decides a hermit from ordinary junk.
          const RngResult cargo = _frame.rng.Next(_frame.carry);
          _frame.carry = cargo.carry;

          if (cargo.value >= HERMIT_ROLL)
          {
            // The hermit's own type goes in the AI byte, and since that type is 15 the branch
            // to `whips` is unconditional.
            _frame.work.ai = Byte(ShipType::RockHermit);
            pendingType = ShipType::RockHermit;
          }
          else
          {
            /*
             * The roll compared against 10, masked to one bit, and added to the
             * canister's type.
             *
             * THE COMPARISON'S ANSWER IS NEVER TESTED. It is there only to set the carry the
             * addition then takes, so a roll of 10 or more adds one; with the mask giving 0 or 1
             * the type is 5, 6 or 7 -- a canister, an alloy plate or an asteroid.
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

    /*
     * whips -- and where it goes next depends on WHICH of the two spawn calls it was.
     *
     * There are two, and they fall into different places. Part 2's is at `whips`, three bytes above
     * `MTT1`, so the loner's pass carries on into part 3. Part 1's is the last instruction of
     * `MTT4` and the next byte is `TT100`, so the TRADER's pass goes back to the top of the loop --
     * another flight frame, another turn of both countdowns, and then `ytq` sends it to `MLOOP`
     * with the main counter at 255. The port had one `Spawn` for both and continued into part 3
     * from either,
     * which gave a trader's pass a police roll the game never makes (M6-a-1; the coverage review
     * named `MTT4` a gap and the first fixture to roll one found this).
     */
    if (spawnPending)
    {
      static_cast<void>(Spawn(_frame.bubble, _frame.work, pendingType, _frame.blueprint));
    }

    return traderPath ? SpawnPass::Restarted : SpawnPass::Continued;
  }

  /*
   * ---- part 3: the police -----------------------------------------------------------------------
   *
   * The station test, whose branch steps over a three-byte jump, so a station in
   * range sends the pass back to the top: no police inside the safe zone, and parts 4, 5 and 6 do
   * not run either.
   */
  [[nodiscard]] SpawnPass SpawnPolice(SpawnFrame& _frame) noexcept
  {
    /*
     * MTT1 and MLOOPS -- part 3, the police.
     *
     * The branch steps over a three-byte jump, so a station in range sends the pass BACK to the
     * top: no police spawn inside the safe zone, and parts 5 and 6 do not run either.
     */
    if (_frame.bubble.StationPresent() != 0u)
    {
      return SpawnPass::Ended;
    }

    /*
     * The contraband penalty doubled, with the legal status folded in conditionally.
     *
     * What the hold is worth in trouble, doubled, and the legal status ORed in ONLY IF there is
     * already a Viper about: the branch steps over the fold and lands on the store, so a clean
     * bubble keeps the doubled cargo alone.
     */
    const std::uint8_t penalty = ContrabandPenalty(_frame.commander);
    const ShiftResult doubled = {static_cast<std::uint8_t>(penalty << 1u), (penalty & 0x80u) != 0u};
    _frame.carry = doubled.carry; // Nothing between the doubling and `Ze` touches the flag

    std::uint8_t threshold = doubled.value;
    if (_frame.bubble.Count(ShipType::Viper) != 0u)
    {
      threshold = static_cast<std::uint8_t>(doubled.value | _frame.commander.legalStatus);
    }

    // Ze, then one byte in 256 goes to the `fothg` path.
    RngResult debris = SeedDebris(_frame.work, _frame.rng, _frame.carry);
    _frame.carry = debris.value == COUGAR_BYTE; // And the comparison sets the flag

    if (debris.value == COUGAR_BYTE)
    {
      /*
       * Byte 6 of the PLANET's block, the low byte of its z coordinate, masked to
       * five bits. Non-zero and this is a Thargoid after all; zero and it is the Cougar, which is
       * the rarest thing in the game.
       */
      if ((_frame.bubble.blocks[0].z.lo & 0x3Eu) != 0u)
      {
        static_cast<void>(SpawnThargoidPair(_frame.bubble, _frame.work, _frame.rng, _frame.blueprint, _frame.carry)); // fothg2
        return SpawnPass::Ended; // mj1
      }

      // A fixed speed and AI byte, and then the Cougar itself.
      _frame.work.speed = 18u;
      _frame.work.ai = 0x79u;
      static_cast<void>(Spawn(_frame.bubble, _frame.work, ShipType::Cougar, _frame.blueprint));
      return SpawnPass::Ended;
    }

    /*
     * The roll against the threshold, and the branch skips the spawn entirely.
     *
     * It steps over five bytes, which is the type load and the call together, so a roll at or above
     * the threshold means no policeman at all.
     */
    _frame.carry = debris.value >= threshold; // The threshold comparison
    if (!_frame.carry)
    {
      // The Viper itself -- and the spawn returns its own carry, which is the flag any later
      // roll on this path rotates in.
      _frame.carry = Spawn(_frame.bubble, _frame.work, ShipType::Viper, _frame.blueprint).created;
    }

    // The count is read AFTER the spawn, so one Viper in the bubble ends the pass whether it
    // arrived just now or was already there.
    if (_frame.bubble.Count(ShipType::Viper) != 0u)
    {
      return SpawnPass::Ended;
    }

    return SpawnPass::Continued;
  }

  /*
   * ---- part 4: the encounter counter, and what it lets through ---------------------------------
   *
   * The encounter counter is a rate limit -- it counts down, only a pass that takes it
   * negative gets any further, and then it is put back so the next pass tries again.
   */
  [[nodiscard]] SpawnPass SpawnEncounter(SpawnFrame& _frame) noexcept
  {
    /*
     * Part 4 -- the encounter counter, which is a rate limit: it counts down, only a pass
     * that takes it negative gets any further, and then it is put back so the next pass tries
     * again.
     */
    --_frame.encounters;
    if ((_frame.encounters & 0x80u) == 0u)
    {
      return SpawnPass::Ended;
    }
    ++_frame.encounters;

    // Mission 1 at stage 2, which is when the Thargoids start hunting you.
    const std::uint8_t stage = static_cast<std::uint8_t>(_frame.commander.missionProgress & 0x0Cu);
    _frame.carry = stage >= 0x08u; // And the flag outlives the branch that reads it

    if (stage == 0x08u)
    {
      // A roll against 200, and `fothg2` is the Thargoid pair.
      const RngResult thargoid = _frame.rng.Next(_frame.carry);
      _frame.carry = thargoid.value >= THARGOID_ROLL; // The roll's threshold
      if (_frame.carry)
      {
        static_cast<void>(SpawnThargoidPair(_frame.bubble, _frame.work, _frame.rng, _frame.blueprint, _frame.carry));
        return SpawnPass::Ended; // mj1
      }
    }

    /*
     * A roll, gated on the government except in anarchy.
     *
     * Anarchy -- government 0 -- always spawns. Everywhere else needs a byte under 90 AND its low
     * three bits to reach the government's own number, so a corporate state is nearly safe.
     */
    const RngResult law = _frame.rng.Next(_frame.carry);
    _frame.carry = law.carry;

    if (_frame.current.government != 0u)
    {
      _frame.carry = law.value >= GOVERNMENT_ROLL; // The roll's own threshold
      if (_frame.carry)
      {
        return SpawnPass::Ended;
      }
      // The low three bits against the government, and this comparison is the one `Ze` below
      // rotates in.
      _frame.carry = static_cast<std::uint8_t>(law.value & 7u) >= _frame.current.government;
      if (!_frame.carry)
      {
        return SpawnPass::Ended;
      }
    }

    // `Ze` again, and a roll above 100 is a pack of pirates.
    //
    // `Ze` IS CALLED TWICE, once in part 3 and once here, and the port had ONE variable for both
    // because it was one function. Part 3's roll is dead by the time this runs -- every read of it
    // is behind the `fothg` branch, which returns -- so this is a fresh local and not the frame's
    // (M4-c-3).
    const RngResult debris = SeedDebris(_frame.work, _frame.rng, _frame.carry);
    _frame.carry = debris.value >= PIRATE_ROLL; // The carry the comparison against 100 leaves

    if (_frame.carry)
    {
      /*
       * Mt1 and mt3 -- two bits of the roll become both the encounter counter and the loop's
       * own count, which is then walked down.
       *
       * The low two bits become BOTH the encounter counter and the loop count, so a pass that
       * spawns four pirates also sets the longest cooldown. One to four of them, and each is
       * `DORND AND DORND AND 7` -- two rolls ANDed, so the low types are far more likely.
       */
      const std::uint8_t count = static_cast<std::uint8_t>(debris.value & 3u);
      _frame.encounters = count;

      for (int remaining = static_cast<int>(count); remaining >= 0; --remaining)
      {
        const RngResult first = _frame.rng.Next(_frame.carry);
        const RngResult second = _frame.rng.Next(first.carry);
        _frame.carry = second.carry;

        const std::uint8_t masked = static_cast<std::uint8_t>(static_cast<std::uint8_t>(second.value & first.value) & 7u);
        const AddResult pack = AddWithCarry(masked, Byte(ShipType::Sidewinder), _frame.carry);
        _frame.carry = pack.carry;

        // The spawn, then the count down and round again -- so the NEXT pass's first roll
        // rotates in the carry `NWSHP` returned, not the one the addition above left.
        _frame.carry = Spawn(_frame.bubble, _frame.work, TypeOf(pack.value), _frame.blueprint).created;
      }

      // The loop's fall-through IS part 5.
      return SpawnPass::Ended;
    }

    /*
     * A lone bounty hunter, and `THERE` is asked whether this is the Constrictor's system.
     */
    ++_frame.encounters;
    const AddResult hunter = AddWithCarry(static_cast<std::uint8_t>(debris.value & 3u), Byte(ShipType::CobraMk3Pirate), _frame.carry);
    _frame.carry = hunter.carry;
    const std::uint8_t y = hunter.value;

    // THERE -- and its answer IS the carry, so the flag survives into what follows.
    _frame.carry = AtConstrictorSystem(_frame.commander);

    bool constrictor = false;
    if (_frame.carry)
    {
      /*
       * The AI byte, then the mission stage shifted, then the bubble's own count.
       *
       * The AI byte is set BEFORE the mission test, so a hunter in that system is hostile whether
       * or not it turns out to be the Constrictor. Then mission 1 has to be at stage 1 -- the shift
       * puts bit 0 in the carry -- and the Constrictor must not already be in the bubble.
       */
      _frame.work.ai = 0xF9u;

      const std::uint8_t stage = static_cast<std::uint8_t>(_frame.commander.missionProgress & 3u);
      const ShiftResult shifted = {static_cast<std::uint8_t>(stage >> 1u), (stage & 1u) != 0u};

      // The shift's own bit, and everything between here and `NOCON` leaves the flag alone,
      // so this is what that roll rotates in.
      _frame.carry = shifted.carry;

      if (_frame.carry)
      {
        constrictor = static_cast<std::uint8_t>(shifted.value | _frame.bubble.Count(ShipType::Constrictor)) == 0u;
      }
    }

    ShipType hunterType = ShipType::None;
    if (constrictor)
    {
      hunterType = ShipType::Constrictor;
    }
    else
    {
      /*
       * Hostile traits, a rolled AI byte, and the type falling through.
       *
       * The routine ends on the assembler trick that hides one instruction inside another's
       * operand, so the type computed at `LABEL_2` reaches `focoug` and the Constrictor's own load
       * is stepped over. The comparison before it is again there only for its CARRY, which the
       * rotate shifts into bit 0 of the AI byte.
       */
      _frame.work.traits = Mask(TraitBit::Hostile);
      const RngResult ai = _frame.rng.Next(_frame.carry);
      const ShiftResult rolled = RotateLeftValue(ai.value, ai.value >= THARGOID_ROLL);
      _frame.carry = rolled.carry;
      _frame.work.ai = With(rolled.value, AiBit::Active, AiBit::Hostile);
      hunterType = TypeOf(y);
    }

    // focoug spawns it, and `mj1` ends the pass.
    static_cast<void>(Spawn(_frame.bubble, _frame.work, hunterType, _frame.blueprint));
    return SpawnPass::Ended;
  }

  SpawnOutcome RunSpawning(Universe& _universe, bool _carryIn) noexcept
  {
    // Nothing spawns in witchspace, because witchspace has no system to spawn from.
    // `MJP` puts the Thargoids there itself.
    if (_universe.status.midJump != 0u)
    {
      return SpawnOutcome::Ended;
    }

    SpawnFrame frame{_universe.bubble,           _universe.work,    _universe.rng,
                     _universe.commander,        _universe.current, _universe.explosions,
                     _universe.flight.blueprint, _carryIn};

    const SpawnPass first = SpawnTraderOrLoner(frame); // Parts 1 and 2
    if (first == SpawnPass::Restarted)
    {
      return SpawnOutcome::Restarted; // The fall-through past part 1's call to `NWSHP`
    }
    if (first == SpawnPass::Ended)
    {
      return SpawnOutcome::Ended;
    }

    if (SpawnPolice(frame) == SpawnPass::Ended) // Part 3
    {
      return SpawnOutcome::Ended;
    }

    static_cast<void>(SpawnEncounter(frame)); // Part 4, and the loop's fall-through IS part 5
    return SpawnOutcome::Ended;
  }

} // namespace Elite
