#include "pch.h"

#include "Tactics.h"

#include "Combat.h"
#include "Messages.h"
#include "ShipBlueprint.h"
#include "ShipMove.h"
#include "Spawn.h"

#include <array>
#include <span>

namespace Elite
{

  namespace
  {

    /*
     * One axis of `DCS1`'s subtraction, entered with A = the nose vector's high byte
     * and X = the base index into `K3`.
     *
     * It is not a subroutine of its own in the source: `DCS1` falls into it from the third `LDX`,
     * which is why the routine's last call is a fall-through rather than a `JSR`.
     */
    void OffsetAxis(K3Block& _axes, std::uint8_t _nose, std::uint8_t _at) noexcept
    {
      // The doubling, and the carry out of it is the vector's SIGN.
      const std::uint8_t doubled = static_cast<std::uint8_t>(_nose << 1u);
      const bool negative = (_nose & 0x80u) != 0u;

      /*
       * The carry rotated into an empty byte, flipped, and matched against the axis sign.
       *
       * The rotate puts the carry into bit 7 of a zero, so this is the vector's sign on its own;
       * flipping it is what makes the addition a SUBTRACTION of the vector. Comparing that against
       * `K3`'s sign says whether the magnitudes add or fight.
       */
      const std::uint8_t sign = static_cast<std::uint8_t>((negative ? 0x80u : 0x00u) ^ 0x80u);
      const bool opposed = ((sign ^ _axes[_at + 2u]) & 0x80u) != 0u;

      if (!opposed)
      {
        /*
         * The doubled vector added into the axis, carrying up into the high byte.
         *
         * The carry going in is clear and nothing says so explicitly: the rotate four instructions
         * up shifted bit 0 of a literal zero out into it. The sign byte is untouched on this path.
         */
        const AddResult low = AddWithCarry(doubled, _axes[_at], false);
        _axes[_at] = low.value;

        if (low.carry)
        {
          ++_axes[_at + 1u]; // An increment, so it cannot carry any further
        }
        return;
      }

      // The doubled vector taken off the axis, borrowing into the high byte.
      SubResult low = SubtractWithCarry(_axes[_at], doubled, true);
      _axes[_at] = low.value;

      SubResult high = SubtractWithCarry(_axes[_at + 1u], 0u, low.carry);
      _axes[_at + 1u] = high.value;

      if (high.carry)
      {
        return; // The magnitude was big enough, so the sign stands
      }

      /*
       * Both bytes complemented and one added, then the sign byte flipped.
       *
       * The subtraction went the wrong way round, so the answer is negated and the sign flipped --
       * the same fix `MVT3` makes, and the add gets its clean carry from the borrow the branch
       * above just tested.
       */
      AddResult negatedLow = AddWithCarry(static_cast<std::uint8_t>(_axes[_at] ^ 0xFFu), 1u, false);
      _axes[_at] = negatedLow.value;

      AddResult negatedHigh = AddWithCarry(static_cast<std::uint8_t>(_axes[_at + 1u] ^ 0xFFu), 0u, negatedLow.carry);
      _axes[_at + 1u] = negatedHigh.value;

      _axes[_at + 2u] = static_cast<std::uint8_t>(_axes[_at + 2u] ^ 0x80u);
    }

  } // namespace

  namespace
  {

    /*
     * The tail of `TAS2`, and a second entry point into it.
     *
     * `DOCKIT` calls it directly to skip the shifting loop: the vector is already small enough, so
     * all that is wanted is the three seven-bit magnitudes and the fall into `NORM`. The port's
     * `NormaliseAxes` is the whole of `TAS2`, so this is the half of it below the loop.
     */
    /// Returns the vector and the length `NORM` leaves in `Q`, which `DOCKIT` reads as the distance
    /// to the station.
    NormalisedVector BuildUnitVector(const K3Block& _axes) noexcept
    {
      std::array<std::uint8_t, 3> vector = {static_cast<std::uint8_t>((_axes[1] >> 1u) | _axes[2]),
                                            static_cast<std::uint8_t>((_axes[4] >> 1u) | _axes[5]),
                                            static_cast<std::uint8_t>((_axes[7] >> 1u) | _axes[8])};

      // And no return -- `TA2` falls into `NORM`, exactly as `TAS2` does above it.
      const std::uint8_t length = Normalise(std::span<std::uint8_t, 3>(vector));
      return NormalisedVector{UnitVector{vector[0], vector[1], vector[2]}, length};
    }

    /*
     * TA15 to TA10 -- the steering, and the only part of `TACTICS` that `DOCKIT` shares.
     *
     * Two dot products and two thresholds. The ROOF vector says whether to pitch and which way; the
     * SIDE vector says whether to roll; `RAT2` is how far off the nose the ship tolerates before it
     * bothers turning and `RAT` is how hard it turns when it does. `CNT` -- the nose dot product
     * the caller measured -- then decides the throttle.
     */
    /// `_towards` is `XX15`: the unit vector the caller measured, which the two dot products read.
    void SteerTowards(Universe& _universe, Ports& _ports, UnitVector _towards, std::uint8_t _offNose) noexcept
    {
      Ship& work = _universe.work;

      // The roof dot product, whose flipped sign becomes the pitch counter.
      const AddSignedResult roof = DotProductWithShip(work, _towards, ORIENTATION_ROOF);
      work.pitchCounter = static_cast<std::uint8_t>((roof.high ^ 0x80u) & 0x80u);

      /*
       * The saved dot product doubled and compared against the tolerance, then the turn rate
       * folded into the pitch counter.
       *
       * The byte brought back is the dot product as it was four instructions earlier, BEFORE the
       * two masks flattened it to a sign. So the magnitude decides whether to pitch at all and the
       * sign decides which way, out of one measurement read twice.
       */
      if (static_cast<std::uint8_t>(roof.high << 1u) >= _universe.flight.signMask2)
      {
        work.pitchCounter = static_cast<std::uint8_t>(_universe.flight.signMask | work.pitchCounter);
      }

      // A ship already rolling hard is left to finish the roll rather than given a
      // new one.
      if (static_cast<std::uint8_t>(work.rollCounter << 1u) < 32u)
      {
        // The roll's direction is the side dot product exclusive-ored with the PITCH just
        // chosen, which is what makes a ship bank into its turn rather than roll and pitch
        // independently.
        const AddSignedResult side = DotProductWithShip(work, _towards, ORIENTATION_SIDE);
        work.rollCounter = static_cast<std::uint8_t>((((side.high ^ work.pitchCounter) & 0x80u) ^ 0x80u));

        if (static_cast<std::uint8_t>(side.high << 1u) >= _universe.flight.signMask2)
        {
          work.rollCounter = static_cast<std::uint8_t>(_universe.flight.signMask | work.rollCounter);
        }
      }

      /*
       * TA6 and PH10E -- behind or inside the cone, and the ship throttles back to three.
       *
       * `CNT` is the NOSE dot product, so bit 7 means the target is behind. Behind, or inside the
       * cone `CNT2` names, and the ship stops here.
       */
      // TA152's only job is to park the byte its caller measured, so it is this routine's
      // parameter since M2-c-3.
      const std::uint8_t offNose = _offNose;
      if ((offNose & 0x80u) == 0u && offNose >= _universe.flight.steerCone)
      {
        work.acceleration = 3u;
        return;
      }

      // The magnitude against eighteen, and `TA10` is a bare return.
      if (static_cast<std::uint8_t>(offNose & 0x7Fu) < 18u)
      {
        return;
      }

      /*
       * Minus one into the acceleration, doubled first if this is a missile.
       *
       * THIS IS A DECELERATION and the byte is signed: `MVEIT` adds the acceleration to the speed
       * and clamps, so &FF is minus one and &FE is minus two. Doubling &FF gives &FE rather than
       * doubling anything, so a missile sheds speed twice as fast as a ship -- which is how it
       * turns tightly enough to come back round. The branch that skips the doubling steps over a
       * single byte.
       *
       * And reaching here at all means the target is behind or wide (`TA6`'s two branches), so the
       * ship that is pointing AT you is the one that speeds up, three lines above.
       */
      work.acceleration = (_universe.flight.type == ShipType::Missile) ? static_cast<std::uint8_t>(0xFFu << 1u) : std::uint8_t{0xFFu};
    }

    /// One nose dot product, which can throw the turn rate away, then `TA152`.
    void AimAlongNose(Universe& _universe, Ports& _ports, UnitVector _towards) noexcept
    {
      // The nose dot product, and past &98 the tolerance is cleared entirely.
      const AddSignedResult nose = DotProductWithShip(_universe.work, _towards, ORIENTATION_NOSE);
      if (nose.high >= 0x98u)
      {
        _universe.flight.signMask2 = 0u;
      }

      SteerTowards(_universe, _ports, _towards, nose.high); // Ttt, into TA152
    }

    /*
     * TN4 and TA19 as PART 1 reaches them -- and part 1 is only ever a missile.
     *
     * `TN4` copies the ship's own position into `K3`, `TA19` normalises it and takes the nose dot
     * product, and then the code FALLS INTO PART 4, whose first act is a missile test that jumps
     * to `TA20`. So a missile does not steer along that vector: `TA20` turns it round with `TAS6`
     * and flips the sign of `CNT` first, because a missile closes on its target rather than facing
     * it.
     *
     * The port jumped from `TA19` straight to the steering at both of part 1's rejoins, which is
     * two instructions of part 4 skipped, and it only became visible once the test fixture stopped
     * handing the geometry a zero-length vector (§6.126).
     */
    void SteerMissileTowardsTarget(Universe& _universe, Ports& _ports) noexcept
    {
      const UnitVector towards = NormaliseAxes(_universe.axes).vector; // TA19, into TAS2
      const AddSignedResult nose = DotProductWithShip(_universe.work, towards, ORIENTATION_NOSE);

      // TA20 and TAS6, reached through part 4's missile test
      SteerTowards(_universe, _ports, NegateVector(towards), static_cast<std::uint8_t>(nose.high ^ 0x80u));
    }

    /// Give up on the station and steer at the PLANET instead.
    void AimAtPlanet(Universe& _universe, Ports& _ports) noexcept
    {
      // `SPS1` is the compass's own "where is the planet", and it leaves the unit vector in
      // `XX15` exactly where the steering wants it.
      AimAlongNose(_universe, _ports, LoadPlanetAxes(_universe.bubble, _universe.axes));
    }

    /// Stop dead and turn on the spot, which is what an autopilot does when it is
    /// pointing the wrong way.
    void HaltAndTurn(Ship& _work) noexcept
    {
      _work.acceleration = 0u; // No acceleration
      _work.speed = 1u;        // And the slowest speed there is
    }

    /*
     * A shift up, then a shift back down through a set carry.
     *
     * THE TWO SHIFTS CANCEL. The first moves every bit up and drops bit 7; the second moves every
     * bit back down and puts a one into bit 7. What comes out is the byte it went in as, with bit 7
     * set -- a bitwise OR written in three instructions, which is how a 6502 sets the top bit of a
     * memory location without loading it into the accumulator.
     *
     * The port had it as a shift AND a set, which is a different answer for every byte with
     * anything in bits 0 to 6. It agreed for a whole slice because the two places that reach it
     * were only ever tested with the byte at zero (§6.126) -- and `FlightLoop.cpp`'s `MarkKilled`
     * had the idiom RIGHT since slice 3d-d-iii-b, under a comment saying "set bit 7 without
     * touching the other seven". The reading was in the codebase and was re-derived wrongly.
     */
    void MarkAsKilled(Ship& _work) noexcept
    {
      _work.state = With(_work.state, ShipStateBit::Killed);
    }

  } // namespace

  bool SubtractShipAxis(const Ship& _other, const Ship& _work, K3Block& _axes, std::uint8_t _at) noexcept
  {
    // The other object's sign, negated.
    const SignMag24& axis = _other.PositionAt(_at);
    KBlock difference;
    difference.top = static_cast<std::uint8_t>(axis.sgn ^ 0x80u);

    // And its two magnitude bytes below that.
    difference.high = axis.hi;
    difference.mid = axis.lo;

    // MVT3 adds this ship's coordinate, so K is now this ship minus the other.
    const KBlockSum sum = AddShipCoordinateToK(_work, difference, _at);

    // The sign byte `MVT3` left behind is what gets stored.
    _axes[_at + 2u] = sum.value.top;
    _axes[_at + 1u] = sum.value.high;
    _axes[_at] = sum.value.mid;

    // The loads and stores that follow leave the flag alone, so what `MVT3` exited with is
    // what the caller gets.
    return sum.carry;
  }

  bool SubtractShipAxes(const Ship& _other, const Ship& _work, K3Block& _axes) noexcept
  {
    // TAS1 on each of the three axes, and the last is a fall-through, so the carry the third
    // leaves is the routine's.
    (void)SubtractShipAxis(_other, _work, _axes, 0u);
    (void)SubtractShipAxis(_other, _work, _axes, 3u);
    return SubtractShipAxis(_other, _work, _axes, 6u);
  }

  bool SubtractStationAxes(const Bubble& _bubble, const Ship& _work, K3Block& _axes) noexcept
  {
    // The station's block address parked, and then straight into `VCSUB`.
    return SubtractShipAxes(_bubble.blocks[1], _work, _axes);
  }

  AddSignedResult DotProductWithShip(const Ship& _block, UnitVector _vector, std::uint8_t _at) noexcept
  {
    // MULT12 gives the first product. The index is 10, 16 or 22 -- the HIGH byte of the
    // vector's x -- so the vector is the one starting a byte earlier.
    const Vector16& vector = _block.VectorAt(static_cast<std::uint8_t>(_at - 1u));
    const Product first = MultiplySigned(_vector.x, vector.x.hi);

    // MAD folds in the second axis and keeps the running sum.
    const AddSignedResult second = MultiplyAndAdd(_vector.y, vector.y.hi, first.Pair());

    // The third axis, and no call -- it falls into `MAD`.
    return MultiplyAndAdd(_vector.z, vector.z.hi, second.Pair());
  }

  UnitVector NegateVector(UnitVector _vector) noexcept
  {
    // The sign bit flipped on each of the three components.
    _vector.x = static_cast<std::uint8_t>(_vector.x ^ 0x80u);
    _vector.y = static_cast<std::uint8_t>(_vector.y ^ 0x80u);
    _vector.z = static_cast<std::uint8_t>(_vector.z ^ 0x80u);
    return _vector;
  }

  void OffsetDockingPosition(const Bubble& _bubble, K3Block& _axes) noexcept
  {
    const Ship& station = _bubble.blocks[1];

    // The routine calls its own body, so it runs twice and each subtraction is the nose
    // vector times four.
    for (int pass = 0; pass < 2; ++pass)
    {
      OffsetAxis(_axes, station.nose.x.hi, 0u); // TAS7 on x
      OffsetAxis(_axes, station.nose.y.hi, 3u); // TAS7 on y
      OffsetAxis(_axes, station.nose.z.hi, 6u); // And z, as a fall-through
    }
  }

  bool Anger(Bubble& _bubble, const FlightState& _flight, std::uint8_t _slot, ShipType _type) noexcept
  {
    Ship& station = _bubble.blocks[1];

    // The station is always slot 1, so this is a fixed address in the original and a
    // fixed index here. None of it touches the carry, so what the station comparison left is what
    // every path out of here still holds.
    const auto angerStation = [&station]() noexcept { station.traits = With(station.traits, TraitBit::Hostile); };

    // The station comparison, whose flag is the routine's exit on two of the three paths.
    const bool comparedToStation = _type >= ShipType::Station;

    if (_type == ShipType::Station)
    {
      angerStation(); // AN2 returns straight away -- nothing else happens
      return comparedToStation;
    }

    Ship& ship = _bubble.blocks[_slot];

    // The innocent bit tested, and `AN2` is CALLED rather than jumped to -- so an ally of the
    // station angers the station AND carries on being angered itself.
    if (Has(ship.traits, TraitBit::Innocent))
    {
      angerStation();
    }

    // The AI byte tested, and the branch lands on a bare return borrowed from `HITCH`. A ship
    // with no AI byte is left entirely alone: no acceleration, no dive, no hostile flag.
    if (ship.ai == 0u)
    {
      return comparedToStation;
    }

    ship.ai = With(ship.ai, AiBit::Active); // The AI byte's top bit set

    // Two into the acceleration and four into the pitch counter -- and doubling a two clears
    // the carry, which the compare below then overwrites on every path.
    ship.acceleration = ANGRY_ACCELERATION;
    ship.pitchCounter = static_cast<std::uint8_t>(ANGRY_ACCELERATION << 1u);

    // The LOOP's type byte compared against the Cobra, not the one in the accumulator.
    const bool comparedToCobra = _flight.type >= ShipType::CobraMk3;
    if (comparedToCobra)
    {
      ship.traits = With(ship.traits, TraitBit::Hostile);
    }
    return comparedToCobra; // AN3 returns on the flag that comparison left
  }

  /*
   * What one part of `TACTICS` hands the next -- and there are FOUR answers where the port
   * had a `bool` (M4-c).
   *
   * `RunTactics` returned "did the player survive", which conflated three different things: a ship
   * finished with for this frame (`TA22`'s return), the player killed by `OOPS` reaching `DEATH`,
   * and `TN2` handing the ship to `DOCKIT`, a different routine altogether. The third was
   * hidden as a tail call whose boolean was passed straight through.
   */
  enum class Tactic : std::uint8_t
  {
    Steer,   ///< fall through to the next part, and eventually to `TA4`'s steering
    Done,    ///< This ship is finished with for the frame
    Fatal,   ///< OOPS reaching DEATH -- the PLAYER died, and the frame ends
    Docking, ///< TN2 hands off to DOCKIT -- the docking computer flies this ship instead
  };

  /*
   * What the parts of `TACTICS` share, in one place (M4-c, the shape M4-b gave `LL9`).
   *
   * File-local for P5's reason: `aggregate-refs` counts the argument-list structs in the HEADERS,
   * which are signatures threaded through the library; this is seven parts in one translation unit
   * that would otherwise pass four arguments each and two results by reference.
   *
   * `towards` and `offNose` are `XX15` and `CNT`, written by part 3 and read by parts 4 to 7 --
   * the two values the parts genuinely hand each other. Everything else a part needs it reads from
   * the universe.
   */
  struct TacticFrame
  {
    Universe& universe;
    Ports& ports;
    Ship& work;
    K3Block& axes;
    ShipType type;
    std::uint8_t slot;

    UnitVector towards{};     ///< The unit vector to the target, which `TAS2` leaves
    std::uint8_t offNose = 0; ///< How far off the nose the target is
  };

  /*
   * ---- part 1: the missile ----------------------------------------------------------------------
   *
   * The missile test sends it to `TA18`, which is in PART 1 and not the beginning.
   *
   * EVERY BRANCH ANSWERS and none of them steers, which is why the port has no line for part 4's
   * missile test: the two branches that fall through in the original call
   * `SteerMissileTowardsTarget`, which is `TA19` and that branch inlined.
   */
  [[nodiscard]] Tactic DecideMissile(TacticFrame& _frame) noexcept
  {
    /*
     * The missile test sends it to `TA18`, which is in PART 1 and not the beginning. A
     * missile has its own logic and rejoins the common tail only through `TA19` or `TA34`.
     */
    // THE TEST IS THE CALLER'S AND THE GUARD IS GONE (slice 5d) -- see `DecideStation` for why.
    // An E.C.M. going off destroys the missile without anybody having to hit it, and
    // `TA352` is how a missile dies.
    bool destroyed = _frame.universe.status.ecmCountdown != 0u;

    if (!destroyed)
    {
      // Bit 6 of the AI byte says the missile is aimed at US, and the shift up is how the
      // sign test gets to read it.
      if (Has(_frame.work.ai, AiBit::AimedAtPlayer))
      {
        /*
         * How far away the missile is, and one that is not yet touching us goes back
         * to the common steering at `TN4`.
         */
        if (LargestShipAxis(_frame.work, 0u) != 0u)
        {
          // TN4 and TAL1 -- the missile's own position becomes the vector to steer along,
          // because the thing it is chasing is at the origin: us.
          const std::array<std::uint8_t, SHIP_BLOCK_SIZE> position = _frame.work.ToBytes();
          for (std::size_t byte = 0; byte < 9u; ++byte)
          {
            _frame.axes[byte] = position[byte];
          }

          // TA19, and then part 4 -- which sends a missile to `TA20`.
          SteerMissileTowardsTarget(_frame.universe, _frame.ports);
          return Tactic::Done;
        }

        /*
         * TA873, the explosion, and 250 damage into `OOPS` -- it has arrived. The missile is
         * marked dead, the explosion is heard, and 250 is nearly always fatal.
         *
         * AND THE CARRY THE EXPLOSION LEAVES IS `OOPS`'s, which is §6.87 a second time: loading the
         * damage touches no flag, so `OOPS` subtracts on whatever `NOISE` returned. The port
         * passed false here while `PlaySound` was a seam whose answer was discarded (M3-b-2a);
         * a missile that arrives while the explosion is refused a voice costs one more point
         * of shield than one that gets one.
         */
        MarkAsKilled(_frame.work);
        const bool heard = PlaySoundEffect(_frame.universe.sound, SoundEffect::Explosion, false).carry;
        return TakeDamage(_frame.universe, _frame.ports, _frame.universe.bubble.blocks[_frame.slot], MISSILE_DAMAGE, heard) ? Tactic::Done
                                                                                                                            : Tactic::Fatal;
      }

      // The missile's TARGET slot, halved out of the AI byte it has been carrying since
      // `FRS1` doubled the lock into it, then `VCSUB` against that ship.
      const std::uint8_t target = MissileTargetOf(_frame.work.ai);
      const bool vectorCarry = SubtractShipAxes(_frame.universe.bubble.blocks[target], _frame.work, _frame.axes);

      /*
       * The three high bytes with their signs masked, ORed with the three middle bytes, so
       * this asks "is the target still further away than 256 units on any axis".
       */
      // NOT `far`: that is a macro in <windows.h>, like `near`, and `check_gamelogic.py` exists
      // to say so (AGENTS.md §5).
      const std::uint8_t distant =
        static_cast<std::uint8_t>((static_cast<std::uint8_t>(_frame.axes[2] | _frame.axes[5] | _frame.axes[8]) & 0x7Fu) | _frame.axes[1] |
                                  _frame.axes[4] | _frame.axes[7]);
      if (distant != 0u)
      {
        // One time in sixteen the missile checks whether its target still has an
        // E.C.M., and the rest of the time it just steers.
        // And the carry going into the roll is the one `VCSUB`'s last `MVT3` exited with:
        // everything between them is masking, which touches no flag (§6.126).
        const RngResult roll = _frame.universe.rng.Next(vectorCarry);
        if (roll.value < 16u)
        {
          /*
           * M32 and TA19S -- the target's AI byte shifted, and the branch chooses between
           * steering and setting the E.C.M. off.
           *
           * The branch steps over a three-byte jump, so a SET bit 0 -- "this ship has an E.C.M." --
           * skips the jump to the steering and lands on the one to `ECBLB2`. The port had the test
           * the other way round, so a missile set off the E.C.M. of every target that did not have
           * one and steered at the ones that did (§6.126).
           */
          if (Has(_frame.universe.bubble.blocks[target].ai, AiBit::HasEcm))
          {
            // The carry is SET here and nothing on the way to `ECBLB2` touches it: the branch is
            // only taken when the shift moved a 1 out, which is the bit it tested. `ECBLB2` hands
            // the flag straight to `NOISE`, whose only use for it is the value it returns when the
            // sound is switched off -- so it is unobservable, and the port passed `false` until
            // M2-d read the branch (§8).
            StartEcm(_frame.universe.canvas, _frame.universe.status, _frame.universe.sound, true, &_frame.universe.backdrop);
            return Tactic::Done;
          }
        }

        // Steer at the target, whose vector `VCSUB` has just left in `K3`, and then
        // part 4 again: this is a missile, so `TA20`.
        SteerMissileTowardsTarget(_frame.universe, _frame.ports);
        return Tactic::Done;
      }

      // A missile whose AI byte names slot 1 dies rather than exploding, because slot 1 is
      // the station.
      destroyed = (_frame.work.ai == MissileAiFor(1u));

      if (!destroyed)
      {
        /*
         * The target's state byte tested, then marked killed if it is not already a wreck.
         *
         * THE TEST READS AN INSTRUCTION AS DATA. What it tests against is the OPERAND of the load
         * at `M32` -- a constant 32 sitting in the middle of an instruction. Bit 5 of the state
         * byte is "already exploding", and 32 is bit 5, so this is "do not blow up a wreck"
         * written by pointing at a byte of code (§6.125).
         */
        Ship& victim = _frame.universe.bubble.blocks[target];
        if (!Has(victim.state, ShipStateBit::Exploding))
        {
          victim.state = With(victim.state, ShipStateBit::Killed);
        }
      }
    }

    if (destroyed)
    {
      // A missile dying right beside us still hurts, and 80 is survivable.
      if (static_cast<std::uint8_t>(_frame.work.x.lo | _frame.work.y.lo | _frame.work.z.lo) == 0u)
      {
        if (!TakeDamage(_frame.universe, _frame.ports, _frame.universe.bubble.blocks[_frame.slot], COLLISION_DAMAGE, false))
        {
          return Tactic::Fatal;
        }
      }

      // TA872 and TA353 -- the explosion is scored as though a plate had been destroyed.
      RecordKill(_frame.universe, _frame.ports, ShipType::AlloyPlate);
      MarkAsKilled(_frame.work); // Falls straight through from `TA353`
      return Tactic::Done;
    }

    // A missile dying right beside us costs 80, which is survivable.
    if (static_cast<std::uint8_t>(_frame.work.x.lo | _frame.work.y.lo | _frame.work.z.lo) == 0u)
    {
      if (!TakeDamage(_frame.universe, _frame.ports, _frame.universe.bubble.blocks[_frame.slot], COLLISION_DAMAGE, false))
      {
        return Tactic::Fatal;
      }
    }

    // TA87 into TA353 -- the TARGET's slot, halved out of the AI byte, becomes the type
    // handed to `EXNO2`, which is what makes a big ship a loud explosion.
    RecordKill(_frame.universe, _frame.ports, TypeOf(MissileTargetOf(_frame.work.ai)));
    MarkAsKilled(_frame.work);
    return Tactic::Done;
  }

  /*
   * ---- part 2: the station, which does not fly but LAUNCHES --------------------------------------
   *
   * The station test, and `TA13` is where everything else goes. Which ship it launches
   * depends on whether the player has made it angry, and every path here is finished with the
   * ship for this frame.
   */
  [[nodiscard]] Tactic DecideStation(TacticFrame& _frame) noexcept
  {
    /*
     * A station does not fly, it LAUNCHES, and which ship it launches depends on whether the
     * player has made it angry.
     */
    /*
     * THE TEST IS THE CALLER'S AND THE GUARD IS GONE (slice 5d). `RunTactics` dispatches on the
     * type, so this routine was re-testing it and falling off its own end when the answer was no
     * -- which is undefined behaviour for a function that returns a `Tactic`, unreachable only
     * because of a caller the compiler cannot see. The station test is one test in the original
     * and it is one test here, at the dispatch.
     */
    ShipType launch = ShipType::None;

    // The hostile bit `ANGRY` sets.
    if (!Has(_frame.work.traits, TraitBit::Hostile))
    {
      // One Transporter at a time, and the count read is the one for the type ABOVE the
      // Shuttle, because the two are launched as a pair.
      if (_frame.universe.bubble.Count(ShipType::Transporter) != 0u)
      {
        return Tactic::Done;
      }

      /*
       * Three times in 256, and a coin flip picks the Shuttle or the Transporter.
       *
       * AND THE CARRY GOING IN IS THE STATION TEST'S, from eleven instructions earlier: nothing
       * between that comparison and this call touches the flag, and a station compares equal, so
       * it is always SET (§6.125). The addition below reads it a second time, which is why the
       * constant it adds is one short.
       */
      const RngResult roll = _frame.universe.rng.Next(Byte(_frame.type) >= Byte(ShipType::Station));
      if (roll.value < 253u)
      {
        return Tactic::Done;
      }
      launch = TypeOf(static_cast<std::uint8_t>((roll.value & 1u) + (Byte(ShipType::Shuttle) - 1u) + 1u));
    }
    else
    {
      // Sixteen times in 256, and only while the police are under strength. The carry
      // going in is the station test's again, by the same argument.
      const RngResult roll = _frame.universe.rng.Next(Byte(_frame.type) >= Byte(ShipType::Station));
      if (roll.value < 240u || _frame.universe.bubble.Count(ShipType::Viper) >= MAXIMUM_POLICE)
      {
        return Tactic::Done;
      }
      launch = ShipType::Viper;
    }

    // Hostile, aggressive, and out of the slot.
    (void)SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, STATION_LAUNCH_AI, launch,
                         _frame.universe.flight.blueprint);
    return Tactic::Done;
  }

  /*
   * ---- part 3: what kind of ship this is, and whether it wants anything to do with us ------------
   *
   * `TA13` to `TA19` -- the hermit, the energy regrowth, the Thargon whose Thargoid is dead,
   * and then `TA14`'s walk down the trait byte: trader, bounty hunter, hostile, docking,
   * station-shy. It
   * ends by setting `K3` from the ship's own position and leaving `XX15` and `CNT` in the frame,
   * which is what parts 4 to 7 steer on.
   */
  [[nodiscard]] Tactic DecideDisposition(TacticFrame& _frame) noexcept
  {
    // A rock hermit is an asteroid until it is shot at, and then it is a pirate.
    if (_frame.type == ShipType::RockHermit)
    {
      // A roll of 200 or more, and the carry going in is the hermit comparison's, which a
      // hermit satisfies with equality, so it is set.
      const RngResult roll = _frame.universe.rng.Next(Byte(_frame.type) >= Byte(ShipType::RockHermit));
      if (roll.value < 200u)
      {
        return Tactic::Done;
      }

      /*
       * The AI byte cleared, the traits set, a pirate chosen and launched, and the AI byte
       * cleared again.
       *
       * The AI byte is cleared BEFORE the spawn and again after it, because `SFS1` copies `INWK`
       * into the new ship: clearing it first is what stops the pirate inheriting the hermit's AI,
       * and clearing it after is what stops the HERMIT flying off.
       */
      _frame.work.ai = 0u;
      _frame.work.traits = HERMIT_PIRATE_NEWB;

      // Two bits of the roll plus the base type -- and the carry added in is the roll
      // comparison's, which is SET on this path.
      const ShipType pirate = TypeOf(static_cast<std::uint8_t>((roll.value & 3u) + Byte(ShipType::Sidewinder) + 1u));
      (void)SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, STATION_LAUNCH_AI, pirate,
                           _frame.universe.flight.blueprint);

      _frame.work.ai = 0u;
      return Tactic::Done;
    }

    // Energy regrows one unit a turn up to the blueprint's maximum, which is why a
    // damaged ship you leave alone is a whole ship when you come back.
    if (_frame.work.energy < _frame.universe.flight.blueprint->maxEnergy)
    {
      ++_frame.work.energy;
    }

    // A Thargon whose Thargoid is dead loses its AI and half its speed, and drifts.
    if (_frame.type == ShipType::Thargon && _frame.universe.bubble.Count(ShipType::Thargoid) == 0u)
    {
      _frame.work.ai = Without(_frame.work.ai, AiBit::HasEcm);                // Shifted down and back
      _frame.work.speed = static_cast<std::uint8_t>(_frame.work.speed >> 1u); // And the speed halved
      return Tactic::Done;
    }

    /*
     * A roll, then the trait byte shifted to test its lowest bit.
     *
     * The roll's accumulator is thrown away and its index register is not: the comparison reads
     * the PREVIOUS random byte. Bit 0 of the trait byte is "trader", and a trader with a roll of
     * 50 or more simply carries on --
     * which is why traders mostly ignore you and occasionally do not.
     */
    const RngResult roll = _frame.universe.rng.Next(Byte(_frame.type) >= Byte(ShipType::Thargon));
    std::uint8_t flags = _frame.work.traits;

    if ((flags & 1u) != 0u && roll.previous >= TRADER_FLEE_ROLL)
    {
      return Tactic::Done;
    }
    flags = static_cast<std::uint8_t>(flags >> 1u);

    // Bit 1 is "bounty hunter", and it only turns on you once your legal status is
    // over 40. The two extra shifts afterwards put the walking copy back in step.
    if ((flags & 1u) != 0u && _frame.universe.commander.legalStatus >= BOUNTY_HUNTER_FIST)
    {
      _frame.work.traits = With(_frame.work.traits, TraitBit::Hostile);
      flags = static_cast<std::uint8_t>(_frame.work.traits >> 2u);
    }
    else
    {
      flags = static_cast<std::uint8_t>(flags >> 1u);
    }

    // Bit 2 is "hostile", and a ship that is NOT hostile is either docking or minding
    // its own business.
    if ((flags & 1u) == 0u)
    {
      flags = static_cast<std::uint8_t>(flags >> 1u);

      // Bit 4 is "docking", and the alternative is `GOPL`.
      if ((static_cast<std::uint8_t>(flags >> 1u) & 1u) != 0u)
      {
        return Tactic::Docking;
      }

      AimAtPlanet(_frame.universe, _frame.ports); // GOPL, through SPS1 into TA151
      return Tactic::Done;
    }
    flags = static_cast<std::uint8_t>(flags >> 1u);

    // Bit 3 is "runs away when the station is near", so a pirate near a station keeps
    // its AI enabled and its target and drops everything else.
    if ((flags & 1u) != 0u && _frame.universe.bubble.StationPresent() != 0u)
    {
      _frame.work.ai = static_cast<std::uint8_t>(_frame.work.ai & Mask(AiBit::Active, AiBit::HasEcm));
    }

    // TN4 and TAL1 -- the ship's own position is the vector to us, because we are the origin.
    const std::array<std::uint8_t, SHIP_BLOCK_SIZE> position = _frame.work.ToBytes();
    for (std::size_t byte = 0; byte < 9u; ++byte)
    {
      _frame.axes[byte] = position[byte];
    }

    // The vector normalised, the nose dot product taken, and then part 4.
    _frame.towards = NormaliseAxes(_frame.axes).vector;
    const AddSignedResult nose = DotProductWithShip(_frame.work, _frame.towards, ORIENTATION_NOSE);
    // How far off the nose the target is. `TACTICS`'s own since M2-c-3: parts 4 to 8
    // read it and `TA152` takes it as an argument.
    _frame.offNose = nose.high;

    return Tactic::Steer;
  }

  /*
   * ---- parts 4, 5 and 6: is it scared, does it fire, does its laser hit us -----------------------
   *
   * THE THREE ARE ONE FUNCTION and the reason is two booleans. `fightsOn` is `TA7`'s first
   * the flee branch jumping clean over part 5, and `fellFromFleeTest` is the carry that branch's
   * comparison leaves for the roll in it -- both are live from part 4 into part 5, so a split
   * between them would need
   * them as parameters to say what a local already says (§6.85's rule, applied a second time).
   */
  [[nodiscard]] Tactic DecideCombat(TacticFrame& _frame) noexcept
  {
    /*
     * ---- part 4: is it an Anaconda, is it scared, has it lost its nerve ------------------------
     *
     * The missile test that sends part 4 to `TA20` -- THE PORT HAS NO LINE FOR IT.
     *
     * In the original a missile reaches part 4 by falling out of `TN4` and `TA19`, and this test
     * is what sends it to `TA20`. The port does not arrive here that way: every branch of part 1's
     * missile block returns, and the two that would have fallen through call
     * `SteerMissileTowardsTarget`, which is `TA19` AND this branch inlined -- see its comment.
     *
     * So a copy of `TA20` stood here as well, and it was unreachable. `tools/mutate.py` is what
     * said so: `ta20-eor` flipped the sign inversion in this copy and nothing in a 1,800-case
     * sweep noticed, because no missile has ever reached this line (plan §6.152). Removed rather
     * than left, because dead code no mutation can reach is exactly what a surviving mutant is
     * for finding.
     */

    // An Anaconda spawns its escort, on a roll of 200 or more.
    bool anacondaFellThrough = false;
    if (_frame.type == ShipType::Anaconda)
    {
      // The type comparison is what sets the carry going into the roll, and for an Anaconda
      // it compares equal, so it is SET.
      const RngResult first = _frame.universe.rng.Next(Byte(_frame.type) >= Byte(ShipType::Anaconda));
      if (first.value >= 200u)
      {
        // A second roll picks which escort, and the carry going in is the first roll's
        // comparison -- reaching here means it did not borrow.
        const RngResult second = _frame.universe.rng.Next(true);
        const ShipType escort = (second.value >= 100u) ? ShipType::Worm : ShipType::Sidewinder;
        (void)SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, STATION_LAUNCH_AI, escort,
                             _frame.universe.flight.blueprint);
        return Tactic::Done;
      }

      // The roll was under 200, so the Anaconda carries on as an ordinary ship and arrives
      // at `TN7` with the carry CLEAR rather than with the type comparison's.
      anacondaFellThrough = true;
    }

    // Six times in 256 a ship rolls for no reason at all, which is most of what makes
    // a dogfight look alive.
    {
      // TN7 is reached either from the type comparison, whose carry is "type at least an
      // Anaconda", or from the Anaconda's own roll test, whose carry is clear. The second is only
      // taken when the first compared EQUAL, so the one expression covers neither path wrongly: an
      // Anaconda that falls through arrives with the carry clear.
      const RngResult chance = _frame.universe.rng.Next(anacondaFellThrough ? false : (Byte(_frame.type) >= Byte(ShipType::Anaconda)));
      if (chance.value >= 250u)
      {
        // A second roll ORed with 104, and the carry is the first comparison's, set by
        // definition here.
        const RngResult amount = _frame.universe.rng.Next(true);
        _frame.work.rollCounter = static_cast<std::uint8_t>(amount.value | 104u);
      }
    }

    /*
     * Energy above half the blueprint's maximum and the ship fights on. Below an
     * EIGHTH, two more shifts down, it may run, and a roll against 230 is how often.
     *
     * AND THE TWO BRANCHES DO NOT GO TO THE SAME PLACE. The one here is the CAPITAL `TA3`, which is
     * part SIX -- so a ship with more than half its energy jumps over part five and never launches
     * a missile at all. The one below is the lower-case `ta3`, which is part five. The
     * port ran both of them into part five, so a healthy ship could fire; `ta-half` is the
     * mutation that survived long enough to say so (§6.153).
     */
    const std::uint8_t maximumEnergy = _frame.universe.flight.blueprint->maxEnergy;
    bool fellFromFleeTest = false;
    const bool fightsOn = static_cast<std::uint8_t>(maximumEnergy >> 1u) < _frame.work.energy;

    if (!fightsOn)
    {
      if (static_cast<std::uint8_t>(maximumEnergy >> 3u) >= _frame.work.energy)
      {
        // A roll against 230, and the carry going in is the energy comparison's above --
        // reaching here means it did not borrow.
        const RngResult flee = _frame.universe.rng.Next(true);
        if (flee.value >= 230u)
        {
          // Bit 7 of the default trait byte for this type is "carries an escape pod", so only
          // a ship that HAS one bails out.
          const std::uint8_t defaults = DefaultNewbFor(_frame.type);
          if ((defaults & 0x80u) != 0u)
          {
            /*
             * The trait byte masked to its top nibble, written to both copies, the AI byte
             * cleared, and then `SESCP`.
             *
             * The abandoned hull keeps only the top nibble of its flags and is written back to the
             * SLOT as well as to `INWK` -- the one place in `TACTICS` that writes both copies --
             * and then the pod is launched with the standard hostile AI byte.
             */
            _frame.work.traits = Without(_frame.work.traits, TraitBit::Trader, TraitBit::BountyHunter, TraitBit::Hostile, TraitBit::Pirate);
            _frame.universe.bubble.blocks[_frame.slot].traits = _frame.work.traits;
            _frame.work.ai = 0u;

            (void)SpawnEscapePod(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type,
                                 _frame.universe.flight.blueprint);
            return Tactic::Done;
          }

          // The ship has no escape pod, so it falls out of the test with the roll's own
          // comparison still standing in the carry.
          fellFromFleeTest = true;
        }
      }
    }

    /*
     * ---- part 5: does it fire ------------------------------------------------------------------
     *
     * The bottom three bits of the state byte are how many missiles the ship has, and
     * the chance of it firing one is that count out of thirty-two.
     *
     * `fightsOn` is `TA7`'s first branch jumping clean over this part -- see the comment there.
     */
    const std::uint8_t missiles = MissilesOf(_frame.work.state);
    if (!fightsOn && missiles != 0u)
    {
      // The count is parked for one instruction and compared back, which is `missiles` here:
      // `TACTICS`'s own byte since M2-c-3.

      // `ta3` has two entrances. Arriving from either energy comparison means the carry is
      // CLEAR; falling out of the escape-pod test means it is the flee roll's, which is SET. The
      // masking and storing between them leave the flag alone either way.
      const RngResult chance = _frame.universe.rng.Next(fellFromFleeTest);

      // An E.C.M. running stops the launch, and the missile is not spent.
      if (static_cast<std::uint8_t>(chance.value & 31u) < missiles && _frame.universe.status.ecmCountdown == 0u)
      {
        --_frame.work.state; // One missile off the count

        // A Thargoid launches a Thargon and passes ITS OWN AI byte on, which is why Thargons
        // arrive hostile.
        if (_frame.type == ShipType::Thargoid)
        {
          (void)SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, _frame.work.ai,
                               ShipType::Thargon, _frame.universe.flight.blueprint);
          return Tactic::Done;
        }

        // TA16 -- and it answers, because a full bubble means no missile.
        if (SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, SPAWN_CHILD_AI,
                           ShipType::Missile, _frame.universe.flight.blueprint)
              .created)
        {
          ShowMessage(_frame.universe.canvas, _frame.ports.printer, _frame.universe.text, _frame.universe.sentences,
                      _frame.universe.message, MESSAGE_INCOMING_MISSILE, _frame.universe.view, &_frame.universe.backdrop);
          (void)PlaySoundEffect(_frame.universe.sound, SoundEffect::Missile, false);
        }
        return Tactic::Done;
      }
    }

    /*
     * ---- part 6: does its laser hit us ---------------------------------------------------------
     *
     * Too far away on any axis and nothing can be fired.
     */
    if ((LargestShipAxis(_frame.work, 0u) & 0xE0u) == 0u)
    {
      const std::uint8_t offNose = _frame.offNose;

      // 160 has bit 7 set, so this test is also "in front".
      if (offNose >= 160u)
      {
        // The blueprint's laser power, whose bottom three bits are the missile count rather
        // than power.
        const std::uint8_t laser = static_cast<std::uint8_t>(_frame.universe.flight.blueprint->weapons & 0xF8u);
        if (laser != 0u)
        {
          // Bit 6 is "firing", which is what draws the line from its nose in part 11 of the
          // flight loop.
          _frame.work.state = With(_frame.work.state, ShipStateBit::Firing);

          // Firing is one cone and HITTING is a tighter one.
          if (offNose >= 163u)
          {
            /*
             * Half the laser power into `OOPS` -- and the byte is read AGAIN unmasked, so the
             * missile count in its bottom three bits is part of the damage.
             *
             * AND THE HALVING IS ALSO THE CARRY. `OOPS` subtracts the damage from the shields, and
             * nothing between the shift and that subtraction touches the flag, so bit 0 of the
             * blueprint's byte 19 decides whether the player loses one more unit of shield than the
             * arithmetic says (§6.125). Found by a sweep that put the ship BESIDE us rather than in
             * front, which is the only geometry in it that reaches this line.
             */
            const std::uint8_t power = _frame.universe.flight.blueprint->weapons;
            const std::uint8_t damage = static_cast<std::uint8_t>(power >> 1u);
            if (!TakeDamage(_frame.universe, _frame.ports, _frame.universe.bubble.blocks[_frame.slot], damage, (power & 1u) != 0u))
            {
              return Tactic::Fatal;
            }

            --_frame.work.acceleration; // It slows down as it fires

            // `TA9-1` is the return one byte before `TA9`, so an E.C.M. running silences the
            // hit and RETURNS rather than skipping the sound (§6.125).
            if (_frame.universe.status.ecmCountdown != 0u)
            {
              return Tactic::Done;
            }

            (void)PlaySoundEffect(_frame.universe.sound, SoundEffect::HitByLaser, false);
            (void)PlaySoundEffect(_frame.universe.sound, SoundEffect::HitByLaser2, false);
            return Tactic::Done;
          }
        }
      }
    }

    return Tactic::Steer;
  }

  /// ---- part 7: steer ----------------------------------------------------------------------------
  [[nodiscard]] Tactic SteerTowardsTarget(TacticFrame& _frame) noexcept
  {
    /*
     * ---- part 7: steer --------------------------------------------------------------------------
     *
     * A ship that is very close steers WITHOUT the reversal below, which is what stops
     * it turning away the moment it arrives.
     */
    bool reverse = true;
    if (_frame.work.z.hi < 3u && (static_cast<std::uint8_t>(_frame.work.x.hi | _frame.work.y.hi) & 0xFEu) == 0u)
    {
      reverse = false;
    }
    else
    {
      // A random byte with bit 7 forced on, against the AI byte: the more aggressive
      // the ship, the more often it presses in.
      // TA5 is reached from the distance comparison, whose carry is set, or by falling past
      // the test below it, where the masking left the flag as that comparison set it.
      const RngResult press = _frame.universe.rng.Next(_frame.work.z.hi >= 3u);
      if (With(press.value, AiBit::Active) >= _frame.work.ai)
      {
        reverse = false;
      }
    }

    if (reverse)
    {
      // TA20 into TA152 -- turn the vector round and flip the sign of how far off it is,
      // which is how a ship backs away.
      SteerTowards(_frame.universe, _frame.ports, NegateVector(_frame.towards), static_cast<std::uint8_t>(_frame.offNose ^ 0x80u));
      return Tactic::Done;
    }

    SteerTowards(_frame.universe, _frame.ports, _frame.towards, _frame.offNose); // TA15, with `CNT` already set
    return Tactic::Done;
  }

  /*
   * One pass of a ship's AI, as the parts it always had (M4-c).
   *
   * Each part ANSWERS and this performs: `TN2`'s hand-off to `DOCKIT` is a call here, where the
   * delegation
   * is visible, rather than a tail call whose boolean was passed through.
   */
  bool RunTactics(Universe& _universe, Ports& _ports, std::uint8_t _slot) noexcept
  {
    TacticFrame frame{_universe, _ports, _universe.work, _universe.axes, _universe.flight.type, _slot};

    // TACTICS sets its three steering constants, and `DOCKIT` overwrites all three -- which
    // is the whole difference between flying and being flown.
    _universe.flight.signMask = TACTICS_RAT;
    _universe.flight.signMask2 = TACTICS_RAT2;
    _universe.flight.steerCone = TACTICS_CNT2;

    Tactic tactic = Tactic::Steer;

    if (frame.type == ShipType::Missile)
    {
      tactic = DecideMissile(frame);
    }
    else if (frame.type == ShipType::Station) // The station test, and TA13 is everything else
    {
      tactic = DecideStation(frame);
    }
    else
    {
      tactic = DecideDisposition(frame); // Parts 3
      if (tactic == Tactic::Steer)
      {
        tactic = DecideCombat(frame); // Parts 4, 5 and 6
      }
      if (tactic == Tactic::Steer)
      {
        tactic = SteerTowardsTarget(frame); // Part 7
      }
    }

    switch (tactic)
    {
    case Tactic::Docking:
      // TN2 hands off to `DOCKIT` -- a tail call in the original and a call here, so the one
      // path where `TACTICS` hands the ship to another routine is visible at the top level. It
      // cannot kill the player, so there is nothing to hand back (M4-c-2).
      RunDockingComputer(_universe, _ports, _slot);
      break;
    case Tactic::Fatal:
      return false;
    case Tactic::Done:
    case Tactic::Steer:
      break;
    }

    return true;
  }

  void RunDockingComputer(Universe& _universe, Ports& _ports, std::uint8_t _slot) noexcept
  {
    Ship& work = _universe.work;
    K3Block& axes = _universe.axes;

    // The docking constants, and the turn rate is the tolerance halved rather than a second
    // constant.
    _universe.flight.signMask2 = DOCKING_RAT2;
    _universe.flight.signMask = static_cast<std::uint8_t>(DOCKING_RAT2 >> 1u);
    _universe.flight.steerCone = DOCKING_CNT2;

    // No station in the bubble, so steer at the planet and stop pretending to dock.
    if (_universe.bubble.StationPresent() == 0u)
    {
      AimAtPlanet(_universe, _ports);
      return;
    }

    (void)SubtractStationAxes(_universe.bubble, work, axes);

    // Any axis whose HIGH byte has magnitude at all means the station is far away, and the
    // sign is masked off because a station behind you is still close.
    if ((static_cast<std::uint8_t>(axes[2] | axes[5] | axes[8]) & 0x7Fu) != 0u)
    {
      AimAtPlanet(_universe, _ports);
      return;
    }

    /*
     * `TA2` runs and its `Q` is kept -- and `Q` is what `NORM` left, the vector's LENGTH.
     *
     * `TA2` is the tail of `TAS2` (the shifting loop is skipped because the test above has just
     * proved the coordinates are small), and it falls into `NORM`, which divides by the length it
     * computed in `Q`. So `K` ends up holding how far away the station is, measured on the way to
     * working out which way it is.
     */
    const std::uint8_t distance = BuildUnitVector(axes).length;

    /*
     * `TAS2` -- and this is a SECOND normalisation, of the same `K3`, immediately after the
     * first. `TA2` skipped the shifting loop; `TAS2` runs it, so the vector `XX15` ends up holding
     * is the shifted one and not the one the length was taken from. The port did the first call and
     * not the second, and every docking approach came out on the wrong branch (§6.125).
     */
    const UnitVector towards = NormaliseAxes(axes).vector;

    // The STATION's nose against the vector to it, so this asks "am I in front of the slot",
    // and anything else goes to `PH1`.
    const AddSignedResult alongSlot = DotProductWithShip(_universe.bubble.blocks[1], towards, ORIENTATION_NOSE);

    bool fineApproach = false;
    bool wideApproach = false;

    if ((alongSlot.high & 0x80u) != 0u || alongSlot.high < 35u)
    {
      wideApproach = true;
    }
    else
    {
      // OUR nose against the same vector, so this asks "am I pointing at it", and &A2 is a
      // wide enough cone to fly straight in.
      const AddSignedResult ourNose = DotProductWithShip(work, towards, ORIENTATION_NOSE);
      if (ourNose.high >= 0xA2u)
      {
        fineApproach = true;
      }
      // Close enough and it is the fine approach for a NEGATIVE type, which is the player's
      // own computer (`auton` parks 224 in `TYPE`); a ship keeps turning towards the slot instead.
      else if (distance >= 157u && IsBody(_universe.flight.type))
      {
        fineApproach = true;
      }
    }

    if (wideApproach)
    {
      /*
       * The vector taken again, offset twice, normalised, turned round, and steered.
       *
       * The vector is taken again from scratch and `DCS1` runs TWICE -- and `DCS1` itself runs its
       * body twice (§6.121), so the docking point ends up eight nose vectors in front of the slot
       * rather than four. Then `TAS6` turns the vector round, because `TA151` steers along `XX15`
       * and what has been computed is the direction FROM the ship TO the point.
       */
      (void)SubtractStationAxes(_universe.bubble, work, axes);
      OffsetDockingPosition(_universe.bubble, axes);
      OffsetDockingPosition(_universe.bubble, axes);
      AimAlongNose(_universe, _ports, NegateVector(NormaliseAxes(axes).vector));
      return;
    }

    if (!fineApproach)
    {
      // Turn the vector round and steer, and then it FALLS INTO `PH22` rather than
      // returning.
      AimAlongNose(_universe, _ports, NegateVector(towards));
      HaltAndTurn(work);
      return;
    }

    /*
     * The fine approach, and the first thing it does is throw the turn rate away.
     *
     * Clearing the tolerance and the pitch counter means no pitch and no cone: from here the ship is
     * lined up and the corrections are made by hand below rather than by the shared steering.
     */
    _universe.flight.signMask2 = 0u;
    work.pitchCounter = 0u;

    // A NEGATIVE type is the player's own docking computer, which `auton` marks by
    // parking &E0 in `TYPE`. A ship being flown in by the AI skips all of this.
    if (IsBody(_universe.flight.type))
    {
      /*
       * The three signs folded together and rotated onto a constant two.
       *
       * Three signs are exclusive-ored -- the type's, the x component's and the y's -- and a shift
       * up pushes the result into the carry so the rotate can put it back on top. Rotating a two
       * right gives a ONE with the carry above it, so the roll is always magnitude one and all this
       * arithmetic decides is its direction.
       */
      const std::uint8_t folded = static_cast<std::uint8_t>(Byte(_universe.flight.type) ^ towards.x ^ towards.y);
      work.rollCounter = static_cast<std::uint8_t>((2u >> 1u) | ((folded & 0x80u) != 0u ? 0x80u : 0x00u));

      // Too far off sideways, so stop and turn.
      if (static_cast<std::uint8_t>(towards.x << 1u) >= 12u)
      {
        HaltAndTurn(work);
        return;
      }

      // The same shape again for the pitch.
      work.pitchCounter = static_cast<std::uint8_t>((2u >> 1u) | ((towards.y & 0x80u) != 0u ? 0x80u : 0x00u));

      if (static_cast<std::uint8_t>(towards.y << 1u) >= 12u)
      {
        HaltAndTurn(work);
        return;
      }
    }

    // The index is still the zero `PH3` left, so the roll the block above may have
    // set is thrown away again for a ship that is lined up.
    work.rollCounter = 0u;

    // The ship's own SIDE vector into `XX15`, which is asking "is the station's roof lined up
    // with my side", the last thing that has to match to fit through a slot.
    const UnitVector side{work.side.x.hi, work.side.y.hi, work.side.z.hi};

    // The dot product doubled and compared, and past 66 it goes to `TN11`.
    const AddSignedResult roll = DotProductWithShip(_universe.bubble.blocks[1], side, ORIENTATION_ROOF);
    if (static_cast<std::uint8_t>(roll.high << 1u) >= 66u)
    {
      // Roll as hard as the byte allows and speed up, which is how a ship spins
      // itself into line with the slot.
      ++work.acceleration;
      work.rollCounter = 0x7Fu;
    }
    else
    {
      HaltAndTurn(work); // PH22, and this one is CALLED -- it comes back
    }

    /*
     * One byte of `K3` guards the whole thing, and past it bit 7 of `NEWB` is set.
     *
     * THE BYTE NOBODY GAVE IT (§6.125). `K3` is `SKIP 0` and names the first byte of `XX2`, which
     * is `SKIP 14` and is `LL9`'s face-visibility array, so `K3+10` is the visibility of the
     * ELEVENTH FACE of the last ship drawn. The upstream comment says "I have no idea what K3+10
     * contains"; the port reads `XX2` because that is the memory, and §6.112 is what happens when
     * it does not.
     *
     * What it guards is the ship DOCKING: bit 7 of `NEWB` is "take this out of the bubble".
     */
    if (_universe.geometry.faceVisible[10] != 0u)
    {
      return;
    }

    // The same three-instruction "set bit 7" as `TA873`, and the same mistake: the shifts
    // cancel (§6.126).
    work.traits = With(work.traits, TraitBit::Remove);
  }

} // namespace Elite
