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
     * 6502: TAS7 -- one axis of `DCS1`'s subtraction, entered with A = the nose vector's high byte
     * and X = the base index into `K3`.
     *
     * It is not a subroutine of its own in the source: `DCS1` falls into it from the third `LDX`,
     * which is why the routine's last call is a fall-through rather than a `JSR`.
     */
    void OffsetAxis(K3Block& _axes, std::uint8_t _nose, std::uint8_t _at) noexcept
    {
      // 6502: ASL A / STA R -- the doubling, and the carry out is the vector's SIGN.
      const std::uint8_t doubled = static_cast<std::uint8_t>(_nose << 1u);
      const bool negative = (_nose & 0x80u) != 0u;

      /*
       * 6502: LDA #0 / ROR A / EOR #%10000000 / EOR K3+2,X / BMI TS71.
       *
       * The `ROR` puts the carry into bit 7 of a zero, so this is the vector's sign on its own; the
       * `EOR` flips it, because what is being added is the NEGATED vector. Comparing that against
       * `K3`'s sign says whether the magnitudes add or fight.
       */
      const std::uint8_t sign = static_cast<std::uint8_t>((negative ? 0x80u : 0x00u) ^ 0x80u);
      const bool opposed = ((sign ^ _axes[_at + 2u]) & 0x80u) != 0u;

      if (!opposed)
      {
        /*
         * 6502: LDA R / ADC K3,X / STA K3,X / BCC TS72 / INC K3+1,X.
         *
         * The carry into the `ADC` is zero and no `CLC` says so: the `ROR A` four instructions up
         * shifted bit 0 of a literal zero out into it. The sign byte is not touched on this path.
         */
        const AddResult low = AddWithCarry(doubled, _axes[_at], false);
        _axes[_at] = low.value;

        if (low.carry)
        {
          ++_axes[_at + 1u]; // 6502: INC K3+1,X -- an INC, so it cannot carry any further
        }
        return;
      }

      // 6502: .TS71 LDA K3,X / SEC / SBC R / STA K3,X / LDA K3+1,X / SBC #0 / STA K3+1,X.
      SubResult low = SubtractWithCarry(_axes[_at], doubled, true);
      _axes[_at] = low.value;

      SubResult high = SubtractWithCarry(_axes[_at + 1u], 0u, low.carry);
      _axes[_at + 1u] = high.value;

      if (high.carry)
      {
        return; // 6502: BCS TS72 -- the magnitude was big enough, so the sign stands
      }

      /*
       * 6502: EOR #%11111111 / ADC #1 twice, then EOR #%10000000 on the sign byte.
       *
       * The subtraction went the wrong way round, so the answer is negated and the sign flipped --
       * the same fix `MVT3` makes, and the `ADC #1` gets its clean carry from the borrow the `BCS`
       * just tested.
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
     * 6502: TA2 -- the tail of `TAS2`, and a second entry point into it.
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

      // 6502: and no RTS -- `TA2` falls into `NORM`, exactly as `TAS2` does above it.
      const std::uint8_t length = Normalise(std::span<std::uint8_t, 3>(vector));
      return NormalisedVector{UnitVector{vector[0], vector[1], vector[2]}, length};
    }

    /*
     * 6502: TA15 to TA10 -- the steering, and the only part of `TACTICS` that `DOCKIT` shares.
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

      // 6502: .TA15 LDY #16 / JSR TAS3 / TAX / EOR #%10000000 / AND #%10000000 / STA INWK+30.
      const AddSignedResult roof = DotProductWithShip(work, _towards, ORIENTATION_ROOF);
      work.pitchCounter = static_cast<std::uint8_t>((roof.high ^ 0x80u) & 0x80u);

      /*
       * 6502: TXA / ASL A / CMP RAT2 / BCC TA11 / LDA RAT / ORA INWK+30 / STA INWK+30.
       *
       * `TXA` brings back the byte `TAX` saved four instructions ago -- the dot product's A, before
       * the two masks flattened it to a sign. So the magnitude decides whether to pitch at all and
       * the sign decides which way, out of one measurement read twice.
       */
      if (static_cast<std::uint8_t>(roof.high << 1u) >= _universe.flight.signMask2)
      {
        work.pitchCounter = static_cast<std::uint8_t>(_universe.flight.signMask | work.pitchCounter);
      }

      // 6502: .TA11 LDA INWK+29 / ASL A / CMP #32 / BCS TA6 -- a ship already rolling hard is left
      // to finish the roll rather than given a new one.
      if (static_cast<std::uint8_t>(work.rollCounter << 1u) < 32u)
      {
        // 6502: LDY #22 / JSR TAS3 / TAX / EOR INWK+30 / AND #%10000000 / EOR #%10000000 --
        // the roll's direction is the side dot product XORed with the PITCH just chosen, which is
        // what makes a ship bank into its turn rather than roll and pitch independently.
        const AddSignedResult side = DotProductWithShip(work, _towards, ORIENTATION_SIDE);
        work.rollCounter = static_cast<std::uint8_t>((((side.high ^ work.pitchCounter) & 0x80u) ^ 0x80u));

        if (static_cast<std::uint8_t>(side.high << 1u) >= _universe.flight.signMask2)
        {
          work.rollCounter = static_cast<std::uint8_t>(_universe.flight.signMask | work.rollCounter);
        }
      }

      /*
       * 6502: .TA6 LDA CNT / BMI TA9 / CMP CNT2 / BCC TA9 / .PH10E LDA #3 / STA INWK+28 / RTS.
       *
       * `CNT` is the NOSE dot product, so bit 7 means the target is behind. Behind, or inside the
       * cone `CNT2` names, and the ship throttles back to 3 and stops here.
       */
      // 6502: .TA152 STA CNT / ... / .TA6 LDA CNT -- `TA152`'s only job is to park the byte its
      // caller measured, so it is this routine's parameter since M2-c-3.
      const std::uint8_t offNose = _offNose;
      if ((offNose & 0x80u) == 0u && offNose >= _universe.flight.steerCone)
      {
        work.acceleration = 3u;
        return;
      }

      // 6502: .TA9 AND #%01111111 / CMP #18 / BCC TA10 -- and `TA10` is a bare `RTS`.
      if (static_cast<std::uint8_t>(offNose & 0x7Fu) < 18u)
      {
        return;
      }

      /*
       * 6502: LDA #&FF / LDX TYPE / CPX #MSL / BNE P%+3 / ASL A / STA INWK+28.
       *
       * THIS IS A DECELERATION and the byte is signed: `MVEIT` adds `INWK+28` to the speed and
       * clamps, so &FF is minus one and &FE is minus two. `ASL A` on &FF gives &FE rather than
       * doubling anything, so a missile sheds speed twice as fast as a ship -- which is how it
       * turns tightly enough to come back round. The branch skips a ONE-byte instruction, which is
       * what `P%+3` means after a two-byte `BNE`.
       *
       * And reaching here at all means the target is behind or wide (`TA6`'s two branches), so the
       * ship that is pointing AT you is the one that speeds up, three lines above.
       */
      work.acceleration = (_universe.flight.type == ShipType::Missile) ? static_cast<std::uint8_t>(0xFFu << 1u) : std::uint8_t{0xFFu};
    }

    /// 6502: .TA151 -- one nose dot product, which can throw the turn rate away, then `TA152`.
    void AimAlongNose(Universe& _universe, Ports& _ports, UnitVector _towards) noexcept
    {
      // 6502: LDY #10 / JSR TAS3 / CMP #&98 / BCC ttt / LDX #0 / STX RAT2.
      const AddSignedResult nose = DotProductWithShip(_universe.work, _towards, ORIENTATION_NOSE);
      if (nose.high >= 0x98u)
      {
        _universe.flight.signMask2 = 0u;
      }

      SteerTowards(_universe, _ports, _towards, nose.high); // 6502: .ttt JMP TA152
    }

    /*
     * 6502: .TN4 / .TA19 as PART 1 reaches them -- and part 1 is only ever a missile.
     *
     * `TN4` copies the ship's own position into `K3`, `TA19` normalises it and takes the nose dot
     * product, and then the code FALLS INTO PART 4, whose first act is `LDA TYPE / CMP #MSL /
     * BNE P%+5 / JMP TA20`. So a missile does not steer along that vector: `TA20` turns it round
     * with `TAS6` and flips the sign of `CNT` first, because a missile closes on its target rather
     * than facing it.
     *
     * The port jumped from `TA19` straight to the steering at both of part 1's rejoins, which is
     * two instructions of part 4 skipped, and it only became visible once the test fixture stopped
     * handing the geometry a zero-length vector (§6.126).
     */
    void SteerMissileTowardsTarget(Universe& _universe, Ports& _ports) noexcept
    {
      const UnitVector towards = NormaliseAxes(_universe.axes).vector; // 6502: .TA19 JSR TAS2
      const AddSignedResult nose = DotProductWithShip(_universe.work, towards, ORIENTATION_NOSE);

      // 6502: .TA20 JSR TAS6, reached through part 4's `CMP #MSL`
      SteerTowards(_universe, _ports, NegateVector(towards), static_cast<std::uint8_t>(nose.high ^ 0x80u));
    }

    /// 6502: .GOPL -- give up on the station and steer at the PLANET instead.
    void AimAtPlanet(Universe& _universe, Ports& _ports) noexcept
    {
      // 6502: JSR SPS1 / JMP TA151 -- `SPS1` is the compass's own "where is the planet", and it
      // leaves the unit vector in `XX15` exactly where the steering wants it.
      AimAlongNose(_universe, _ports, LoadPlanetAxes(_universe.bubble, _universe.axes));
    }

    /// 6502: .PH22 -- stop dead and turn on the spot, which is what an autopilot does when it is
    /// pointing the wrong way.
    void HaltAndTurn(Ship& _work) noexcept
    {
      _work.acceleration = 0u; // 6502: LDX #0 / STX INWK+28
      _work.speed = 1u;        // 6502: INX / STX INWK+27
    }

    /*
     * 6502: .TA873 -- ASL INWK+31 / SEC / ROR INWK+31.
     *
     * THE TWO SHIFTS CANCEL. `ASL` moves every bit up and drops bit 7; `SEC / ROR` moves every bit
     * back down and puts a one into bit 7. What comes out is the byte it went in as, with bit 7
     * set -- an `ORA #128` written in three instructions, which is how a 6502 sets the top bit of a
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
    // 6502: LDA (V),Y / EOR #%10000000 / STA K+3 -- the other object's sign, negated.
    const SignMag24& axis = _other.PositionAt(_at);
    KBlock difference;
    difference.top = static_cast<std::uint8_t>(axis.sgn ^ 0x80u);

    // 6502: DEY / LDA (V),Y / STA K+2 / DEY / LDA (V),Y / STA K+1.
    difference.high = axis.hi;
    difference.mid = axis.lo;

    // 6502: STY U / LDX U / JSR MVT3 -- K = K + INWK+X, so K is now this ship minus the other.
    const KBlockSum sum = AddShipCoordinateToK(_work, difference, _at);

    // 6502: STA K3+2,X, and the A it stores is the sign byte `MVT3` left in the register.
    _axes[_at + 2u] = sum.value.top;
    _axes[_at + 1u] = sum.value.high;
    _axes[_at] = sum.value.mid;

    // 6502: `LDY`, `LDA` and `STA` leave the flag alone, so what `MVT3` exited with is what the
    // caller gets.
    return sum.carry;
  }

  bool SubtractShipAxes(const Ship& _other, const Ship& _work, K3Block& _axes) noexcept
  {
    // 6502: LDY #2 / JSR TAS1 / LDY #5 / JSR TAS1 / LDY #8, and the last one is a fall-through, so
    // the carry the third leaves is the routine's.
    (void)SubtractShipAxis(_other, _work, _axes, 0u);
    (void)SubtractShipAxis(_other, _work, _axes, 3u);
    return SubtractShipAxis(_other, _work, _axes, 6u);
  }

  bool SubtractStationAxes(const Bubble& _bubble, const Ship& _work, K3Block& _axes) noexcept
  {
    // 6502: LDA #LO(K%+NI%) / STA V / LDA #HI(K%+NI%), and then straight into `VCSUB`.
    return SubtractShipAxes(_bubble.blocks[1], _work, _axes);
  }

  AddSignedResult DotProductWithShip(const Ship& _block, UnitVector _vector, std::uint8_t _at) noexcept
  {
    // 6502: LDX INWK,Y / STX Q / LDA XX15 / JSR MULT12 -- (S R) = vect_x * XX15. Y is 10, 16 or
    // 22: the HIGH byte of the vector's x, so the vector is the one starting a byte earlier.
    const Vector16& vector = _block.VectorAt(static_cast<std::uint8_t>(_at - 1u));
    const Product first = MultiplySigned(_vector.x, vector.x.hi);

    // 6502: LDX INWK+2,Y / STX Q / LDA XX15+1 / JSR MAD / STA S / STX R.
    const AddSignedResult second = MultiplyAndAdd(_vector.y, vector.y.hi, first.Pair());

    // 6502: LDX INWK+4,Y / STX Q / LDA XX15+2, and no `JSR` -- it falls into `MAD`.
    return MultiplyAndAdd(_vector.z, vector.z.hi, second.Pair());
  }

  UnitVector NegateVector(UnitVector _vector) noexcept
  {
    // 6502: three EOR #%10000000s over XX15, XX15+1 and XX15+2.
    _vector.x = static_cast<std::uint8_t>(_vector.x ^ 0x80u);
    _vector.y = static_cast<std::uint8_t>(_vector.y ^ 0x80u);
    _vector.z = static_cast<std::uint8_t>(_vector.z ^ 0x80u);
    return _vector;
  }

  void OffsetDockingPosition(const Bubble& _bubble, K3Block& _axes) noexcept
  {
    const Ship& station = _bubble.blocks[1];

    // 6502: JSR P%+3 -- the body twice, so each subtraction is the nose vector times four.
    for (int pass = 0; pass < 2; ++pass)
    {
      OffsetAxis(_axes, station.nose.x.hi, 0u); // 6502: LDA K%+NI%+10 / LDX #0 / JSR TAS7
      OffsetAxis(_axes, station.nose.y.hi, 3u); // 6502: LDA K%+NI%+12 / LDX #3 / JSR TAS7
      OffsetAxis(_axes, station.nose.z.hi, 6u); // 6502: LDA K%+NI%+14 / LDX #6, a fall-through
    }
  }

  bool Anger(Bubble& _bubble, const FlightState& _flight, std::uint8_t _slot, ShipType _type) noexcept
  {
    Ship& station = _bubble.blocks[1];

    // 6502: .AN2 LDA K%+NI%+36 / ORA #%00000100 / STA K%+NI%+36 -- the station is always slot 1,
    // so this is a fixed address in the original and a fixed index here. None of it touches the
    // carry, so what `CMP #SST` left is what every path out of here still holds.
    const auto angerStation = [&station]() noexcept { station.traits = With(station.traits, TraitBit::Hostile); };

    // 6502: CMP #SST -- and the flag it sets is the routine's exit on two of the three paths.
    const bool comparedToStation = _type >= ShipType::Station;

    if (_type == ShipType::Station)
    {
      angerStation(); // 6502: CMP #SST / BEQ AN2, and AN2 returns -- nothing else happens
      return comparedToStation;
    }

    Ship& ship = _bubble.blocks[_slot];

    // 6502: LDY #36 / LDA (INF),Y / AND #%00100000 / BEQ P%+5 / JSR AN2 -- and it is a `JSR`, so
    // an ally of the station angers the station AND carries on being angered itself.
    if (Has(ship.traits, TraitBit::Innocent))
    {
      angerStation();
    }

    // 6502: LDY #32 / LDA (INF),Y / BEQ HI1 -- and `HI1` is a bare `RTS` inside `HITCH`. A ship
    // with no AI byte is left entirely alone: no acceleration, no dive, no hostile flag.
    if (ship.ai == 0u)
    {
      return comparedToStation;
    }

    ship.ai = With(ship.ai, AiBit::Active); // 6502: ORA #%10000000 / STA (INF),Y

    // 6502: LDY #28 / LDA #2 / STA (INF),Y / ASL A / LDY #30 / STA (INF),Y -- and the `ASL` of a 2
    // clears the carry, which the compare below then overwrites on every path.
    ship.acceleration = ANGRY_ACCELERATION;
    ship.pitchCounter = static_cast<std::uint8_t>(ANGRY_ACCELERATION << 1u);

    // 6502: LDA TYPE / CMP #CYL / BCC AN3 -- the LOOP's type byte, not the one in A.
    const bool comparedToCobra = _flight.type >= ShipType::CobraMk3;
    if (comparedToCobra)
    {
      ship.traits = With(ship.traits, TraitBit::Hostile);
    }
    return comparedToCobra; // 6502: .AN3 RTS, on the flag `CMP #CYL` left
  }

  /*
   * 6502: what one part of `TACTICS` hands the next -- and there are FOUR answers where the port
   * had a `bool` (M4-c).
   *
   * `RunTactics` returned "did the player survive", which conflated three different things: a ship
   * finished with for this frame (`TA22`'s `RTS`), the player killed by `OOPS` reaching `DEATH`,
   * and `TN2`'s `JMP DOCKIT` handing the ship to a different routine altogether. The third was
   * hidden as a tail call whose boolean was passed straight through.
   */
  enum class Tactic : std::uint8_t
  {
    Steer,   ///< fall through to the next part, and eventually to `TA4`'s steering
    Done,    ///< 6502: .TA22 RTS -- this ship is finished with for the frame
    Fatal,   ///< 6502: OOPS reaching DEATH -- the PLAYER died, and the frame ends
    Docking, ///< 6502: .TN2's JMP DOCKIT -- the docking computer flies this ship instead
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

    UnitVector towards{};     ///< 6502: XX15 -- the unit vector to the target, which `TAS2` leaves
    std::uint8_t offNose = 0; ///< 6502: CNT -- how far off the nose the target is
  };

  /*
   * ---- part 1: the missile ----------------------------------------------------------------------
   *
   * 6502: CPX #MSL / BEQ TA18 -- and `TA18` is in PART 1, which is not the beginning.
   *
   * EVERY BRANCH ANSWERS and none of them steers, which is why the port has no line for part 4's
   * `CMP #MSL / JMP TA20`: the two branches that fall through in the original call
   * `SteerMissileTowardsTarget`, which is `TA19` and that branch inlined.
   */
  [[nodiscard]] Tactic DecideMissile(TacticFrame& _frame) noexcept
  {
    /*
     * 6502: CPX #MSL / BEQ TA18 -- and `TA18` is in PART 1, which is not the beginning. A missile
     * has its own logic and rejoins the common tail only through `TA19` or `TA34`.
     */
    // THE TEST IS THE CALLER'S AND THE GUARD IS GONE (slice 5d) -- see `DecideStation` for why.
    // 6502: .TA18 LDA ECMA / BNE TA352 -- an ECM going off destroys the missile without anybody
    // having to hit it, and `TA352` is how a missile dies.
    bool destroyed = _frame.universe.status.ecmCountdown != 0u;

    if (!destroyed)
    {
      // 6502: LDA INWK+32 / ASL A / BMI TA34 -- bit 6 of the AI byte says the missile is aimed at
      // US, and the `ASL` reads it by moving it into bit 7.
      if (Has(_frame.work.ai, AiBit::AimedAtPlayer))
      {
        /*
         * 6502: .TA34 LDA #0 / JSR MAS4 / BEQ P%+5 / JMP TN4 -- how far away the missile is, and
         * a missile that is not yet touching us goes back to the common steering at `TN4`.
         */
        if (LargestShipAxis(_frame.work, 0u) != 0u)
        {
          // 6502: .TN4 LDX #8 / .TAL1 LDA INWK,X / STA K3,X -- the missile's own position becomes
          // the vector to steer along, because the thing it is chasing is at the origin: us.
          const std::array<std::uint8_t, SHIP_BLOCK_SIZE> position = _frame.work.ToBytes();
          for (std::size_t byte = 0; byte < 9u; ++byte)
          {
            _frame.axes[byte] = position[byte];
          }

          // 6502: .TA19, and then part 4 -- which sends a missile to `TA20`.
          SteerMissileTowardsTarget(_frame.universe, _frame.ports);
          return Tactic::Done;
        }

        /*
         * 6502: JSR TA873 / JSR EXNO3 / LDA #250 / JMP OOPS -- it has arrived. The missile is
         * marked dead, the explosion is heard, and 250 is nearly always fatal.
         *
         * AND THE CARRY `EXNO3` LEAVES IS `OOPS`'s, which is §6.87 a second time: `LDA #250`
         * touches no flag, so `OOPS`'s `SBC` subtracts on whatever `NOISE` returned. The port
         * passed false here while `PlaySound` was a seam whose answer was discarded (M3-b-2a);
         * a missile that arrives while the explosion is refused a voice costs one more point
         * of shield than one that gets one.
         */
        MarkAsKilled(_frame.work);
        const bool heard = PlaySoundEffect(_frame.universe.sound, SoundEffect::Explosion, false).carry;
        return TakeDamage(_frame.universe, _frame.ports, _frame.universe.bubble.blocks[_frame.slot], MISSILE_DAMAGE, heard) ? Tactic::Done
                                                                                                                            : Tactic::Fatal;
      }

      // 6502: LSR A / TAX / LDA UNIV,X / STA V / LDA UNIV+1,X / JSR VCSUB -- the missile's TARGET
      // slot, out of the AI byte it has been carrying since `FRS1` doubled `MSTG` into it.
      const std::uint8_t target = MissileTargetOf(_frame.work.ai);
      const bool vectorCarry = SubtractShipAxes(_frame.universe.bubble.blocks[target], _frame.work, _frame.axes);

      /*
       * 6502: LDA K3+2 / ORA K3+5 / ORA K3+8 / AND #%01111111 / ORA K3+1 / ORA K3+4 / ORA K3+7 /
       * BNE TA64 -- the three high bytes with their signs masked OR the three middle bytes, so
       * this is "is the target still further away than 256 units on any axis".
       */
      // NOT `far`: that is a macro in <windows.h>, like `near`, and `check_gamelogic.py` exists
      // to say so (AGENTS.md §5).
      const std::uint8_t distant =
        static_cast<std::uint8_t>((static_cast<std::uint8_t>(_frame.axes[2] | _frame.axes[5] | _frame.axes[8]) & 0x7Fu) | _frame.axes[1] |
                                  _frame.axes[4] | _frame.axes[7]);
      if (distant != 0u)
      {
        // 6502: .TA64 JSR DORND / CMP #16 / BCS TA19S -- one time in sixteen the missile checks
        // whether its target still has an ECM, and the rest of the time it just steers.
        // 6502: .TA64 JSR DORND -- and the carry is the one `VCSUB`'s last `MVT3` exited with,
        // seven instructions of `ORA` and `AND` earlier, none of which touches the flag (§6.126).
        const RngResult roll = _frame.universe.rng.Next(vectorCarry);
        if (roll.value < 16u)
        {
          /*
           * 6502: .M32 LDY #32 / LDA (V),Y / LSR A / BCS P%+5 / .TA19S JMP TA19 / JMP ECBLB2.
           *
           * `BCS P%+5` skips a THREE-byte instruction, so a SET bit 0 -- "this ship has an ECM"
           * -- skips the jump to the steering and lands on the jump to `ECBLB2`. The port had the
           * test the other way round, so a missile set off the ECM of every target that did not
           * have one and steered at the ones that did (§6.126).
           */
          if (Has(_frame.universe.bubble.blocks[target].ai, AiBit::HasEcm))
          {
            // The carry is SET here, and `JMP ECBLB2` touches nothing on the way: `BCS` is only
            // taken when `LSR A` shifted a 1 out, which is the bit this branch tested. `ECBLB2`
            // hands it straight to `NOISE`, whose only use for it is the value it returns when
            // the sound is switched off -- so it is unobservable, and the port passed `false`
            // until M2-d read the branch (§8).
            StartEcm(_frame.universe.canvas, _frame.universe.status, _frame.universe.sound, true, &_frame.universe.picture);
            return Tactic::Done;
          }
        }

        // 6502: JMP TA19 -- steer at the target, whose vector `VCSUB` has just left in `K3`, and
        // then part 4 again: this is a missile, so `TA20`.
        SteerMissileTowardsTarget(_frame.universe, _frame.ports);
        return Tactic::Done;
      }

      // 6502: LDA INWK+32 / CMP #%10000010 / BEQ TA352 -- a missile that has reached the ship in
      // slot 1 dies rather than exploding, because slot 1 is the station.
      destroyed = (_frame.work.ai == MissileAiFor(1u));

      if (!destroyed)
      {
        /*
         * 6502: LDY #31 / LDA (V),Y / BIT M32+1 / BNE TA35 / ORA #%10000000 / STA (V),Y.
         *
         * `BIT M32+1` READS AN INSTRUCTION AS DATA. `M32` is `LDY #32`, so `M32+1` is the &20
         * operand -- a constant 32, tested against the target's state byte. Bit 5 of that byte is
         * "already exploding", and &20 is bit 5, so this is "do not blow up a wreck" written as a
         * `BIT` against the middle of an instruction (§6.125).
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
      // 6502: .TA352 LDA INWK / ORA INWK+3 / ORA INWK+6 / BNE TA872 / LDA #80 / JSR OOPS -- a
      // missile dying right beside us still hurts, and 80 is a survivable amount.
      if (static_cast<std::uint8_t>(_frame.work.x.lo | _frame.work.y.lo | _frame.work.z.lo) == 0u)
      {
        if (!TakeDamage(_frame.universe, _frame.ports, _frame.universe.bubble.blocks[_frame.slot], COLLISION_DAMAGE, false))
        {
          return Tactic::Fatal;
        }
      }

      // 6502: .TA872 LDX #PLT / BNE TA353 -- and `TA353` is `JSR EXNO2` with X as the _frame.type, so
      // the explosion is scored as though a plate had been destroyed.
      RecordKill(_frame.universe, _frame.ports, ShipType::AlloyPlate);
      MarkAsKilled(_frame.work); // 6502: .TA873 -- falls straight through from `TA353`
      return Tactic::Done;
    }

    // 6502: .TA35 LDA INWK / ORA INWK+3 / ORA INWK+6 / BNE TA87 / LDA #80 / JSR OOPS.
    if (static_cast<std::uint8_t>(_frame.work.x.lo | _frame.work.y.lo | _frame.work.z.lo) == 0u)
    {
      if (!TakeDamage(_frame.universe, _frame.ports, _frame.universe.bubble.blocks[_frame.slot], COLLISION_DAMAGE, false))
      {
        return Tactic::Fatal;
      }
    }

    // 6502: .TA87 LDA INWK+32 / AND #%01111111 / LSR A / TAX / .TA353 JSR EXNO2 -- the TARGET's
    // slot becomes the _frame.type handed to `EXNO2`, which is what makes a big ship a loud explosion.
    RecordKill(_frame.universe, _frame.ports, TypeOf(MissileTargetOf(_frame.work.ai)));
    MarkAsKilled(_frame.work);
    return Tactic::Done;
  }

  /*
   * ---- part 2: the station, which does not fly but LAUNCHES --------------------------------------
   *
   * 6502: CPX #SST / BNE TA13. Which ship it launches depends on whether the player has made it
   * angry, and every path here is finished with the ship for this frame.
   */
  [[nodiscard]] Tactic DecideStation(TacticFrame& _frame) noexcept
  {
    /*
     * 6502: CPX #SST / BNE TA13 -- a station does not fly, it LAUNCHES, and which ship it launches
     * depends on whether the player has made it angry.
     */
    /*
     * THE TEST IS THE CALLER'S AND THE GUARD IS GONE (slice 5d). `RunTactics` dispatches on the
     * type, so this routine was re-testing it and falling off its own end when the answer was no
     * -- which is undefined behaviour for a function that returns a `Tactic`, unreachable only
     * because of a caller the compiler cannot see. `CPX #SST / BNE TA13` is one test in the
     * original and it is one test here, at the dispatch.
     */
    ShipType launch = ShipType::None;

    // 6502: LDA NEWB / AND #%00000100 / BNE TN5 -- the hostile bit `ANGRY` sets.
    if (!Has(_frame.work.traits, TraitBit::Hostile))
    {
      // 6502: LDA MANY+SHU+1 / BNE TA1 -- one Transporter at a time, and `MANY+SHU+1` is the
      // count of the _frame.type ABOVE the Shuttle because the two are launched as a pair.
      if (_frame.universe.bubble.Count(ShipType::Transporter) != 0u)
      {
        return Tactic::Done;
      }

      /*
       * 6502: JSR DORND / CMP #253 / BCC TA1 / AND #1 / ADC #SHU-1 / TAX -- three times in 256,
       * and the coin flip picks the Shuttle or the Transporter.
       *
       * AND THE CARRY GOING IN IS `CPX #SST`'s, from eleven instructions earlier: nothing between
       * that compare and this call touches the flag, and a station is equal to `SST`, so it is
       * always SET (§6.125). The `ADC` below reads it a second time, which is why the constant is
       * `SHU-1` and not `SHU`.
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
      // 6502: .TN5 JSR DORND / CMP #240 / BCC TA1 / LDA MANY+COPS / CMP #4 / BCS TA22 -- and the
      // carry is `CPX #SST`'s again, by the same argument.
      const RngResult roll = _frame.universe.rng.Next(Byte(_frame.type) >= Byte(ShipType::Station));
      if (roll.value < 240u || _frame.universe.bubble.Count(ShipType::Viper) >= MAXIMUM_POLICE)
      {
        return Tactic::Done;
      }
      launch = ShipType::Viper;
    }

    // 6502: .TN6 LDA #%11110001 / JMP SFS1 -- hostile, aggressive, and out of the slot.
    (void)SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, STATION_LAUNCH_AI, launch,
                         _frame.universe.flight.blueprint);
    return Tactic::Done;
  }

  /*
   * ---- part 3: what kind of ship this is, and whether it wants anything to do with us ------------
   *
   * 6502: `TA13` to `TA19` -- the hermit, the energy regrowth, the Thargon whose Thargoid is dead,
   * and then `TA14`'s walk down `NEWB`: trader, bounty hunter, hostile, docking, station-shy. It
   * ends by setting `K3` from the ship's own position and leaving `XX15` and `CNT` in the frame,
   * which is what parts 4 to 7 steer on.
   */
  [[nodiscard]] Tactic DecideDisposition(TacticFrame& _frame) noexcept
  {
    // 6502: .TA13 CPX #HER / BNE TA17 -- a rock hermit is an asteroid until it is shot at, and
    // then it is a pirate.
    if (_frame.type == ShipType::RockHermit)
    {
      // 6502: JSR DORND / CMP #200 / BCC TA22 -- and the carry is `CPX #HER`'s, which a hermit
      // satisfies with equality, so it is set.
      const RngResult roll = _frame.universe.rng.Next(Byte(_frame.type) >= Byte(ShipType::RockHermit));
      if (roll.value < 200u)
      {
        return Tactic::Done;
      }

      /*
       * 6502: LDX #0 / STX INWK+32 / LDX #%00100100 / STX NEWB / AND #3 / ADC #SH3 / TAX /
       * JSR TN6 / LDA #0 / STA INWK+32 / RTS.
       *
       * The AI byte is cleared BEFORE the spawn and again after it, because `SFS1` copies `INWK`
       * into the new ship: clearing it first is what stops the pirate inheriting the hermit's AI,
       * and clearing it after is what stops the HERMIT flying off.
       */
      _frame.work.ai = 0u;
      _frame.work.traits = HERMIT_PIRATE_NEWB;

      // 6502: AND #3 / ADC #SH3 -- and the carry is the `CMP #200`'s, which is SET on this path.
      const ShipType pirate = TypeOf(static_cast<std::uint8_t>((roll.value & 3u) + Byte(ShipType::Sidewinder) + 1u));
      (void)SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, STATION_LAUNCH_AI, pirate,
                           _frame.universe.flight.blueprint);

      _frame.work.ai = 0u;
      return Tactic::Done;
    }

    // 6502: .TA17 LDY #14 / LDA INWK+35 / CMP (XX0),Y / BCS TA21 / INC INWK+35 -- energy regrows
    // one unit a turn up to the blueprint's maximum, which is why a damaged ship you leave alone
    // is a whole ship when you come back.
    if (_frame.work.energy < _frame.universe.flight.blueprint->maxEnergy)
    {
      ++_frame.work.energy;
    }

    // 6502: .TA21 CPX #TGL / BNE TA14 / LDA MANY+THG / BNE TA14 -- a Thargon whose Thargoid is
    // dead loses its AI and half its speed, and drifts.
    if (_frame.type == ShipType::Thargon && _frame.universe.bubble.Count(ShipType::Thargoid) == 0u)
    {
      _frame.work.ai = Without(_frame.work.ai, AiBit::HasEcm);                // 6502: LSR INWK+32 / ASL INWK+32
      _frame.work.speed = static_cast<std::uint8_t>(_frame.work.speed >> 1u); // 6502: LSR INWK+27
      return Tactic::Done;                                                    // 6502: .TA22 RTS
    }

    /*
     * 6502: .TA14 JSR DORND / LDA NEWB / LSR A / BCC TN1 / CPX #50 / BCS TA22.
     *
     * The `DORND`'s A is thrown away and its X is not: `CPX #50` reads the PREVIOUS random byte.
     * Bit 0 of `NEWB` is "trader", and a trader with a roll of 50 or more simply carries on --
     * which is why traders mostly ignore you and occasionally do not.
     */
    const RngResult roll = _frame.universe.rng.Next(Byte(_frame.type) >= Byte(ShipType::Thargon));
    std::uint8_t flags = _frame.work.traits;

    if ((flags & 1u) != 0u && roll.previous >= TRADER_FLEE_ROLL)
    {
      return Tactic::Done;
    }
    flags = static_cast<std::uint8_t>(flags >> 1u);

    // 6502: .TN1 LSR A / BCC TN2 / LDX FIST / CPX #40 / BCC TN2 / LDA NEWB / ORA #%00000100 /
    // STA NEWB / LSR A / LSR A -- bit 1 is "bounty hunter", and it only turns on you once your
    // legal status is over 40. The two `LSR`s put the shifted copy back in step.
    if ((flags & 1u) != 0u && _frame.universe.commander.legalStatus >= BOUNTY_HUNTER_FIST)
    {
      _frame.work.traits = With(_frame.work.traits, TraitBit::Hostile);
      flags = static_cast<std::uint8_t>(_frame.work.traits >> 2u);
    }
    else
    {
      flags = static_cast<std::uint8_t>(flags >> 1u);
    }

    // 6502: .TN2 LSR A / BCS TN3 -- bit 2 is "hostile", and a ship that is NOT hostile is either
    // docking or minding its own business.
    if ((flags & 1u) == 0u)
    {
      flags = static_cast<std::uint8_t>(flags >> 1u);

      // 6502: LSR A / LSR A / BCC GOPL / JMP DOCKIT -- bit 4 is "docking".
      if ((static_cast<std::uint8_t>(flags >> 1u) & 1u) != 0u)
      {
        return Tactic::Docking;
      }

      AimAtPlanet(_frame.universe, _frame.ports); // 6502: .GOPL JSR SPS1 / JMP TA151
      return Tactic::Done;
    }
    flags = static_cast<std::uint8_t>(flags >> 1u);

    // 6502: .TN3 LSR A / BCC TN4 / LDA SSPR / BEQ TN4 / LDA INWK+32 / AND #%10000001 / STA INWK+32
    // -- bit 3 is "runs away when the station is near", so a pirate near a station keeps its AI
    // enabled and its target and drops everything else.
    if ((flags & 1u) != 0u && _frame.universe.bubble.StationPresent() != 0u)
    {
      _frame.work.ai = static_cast<std::uint8_t>(_frame.work.ai & Mask(AiBit::Active, AiBit::HasEcm));
    }

    // 6502: .TN4 LDX #8 / .TAL1 LDA INWK,X / STA K3,X / DEX / BPL TAL1 -- the ship's own position
    // is the vector to us, because we are the origin.
    const std::array<std::uint8_t, SHIP_BLOCK_SIZE> position = _frame.work.ToBytes();
    for (std::size_t byte = 0; byte < 9u; ++byte)
    {
      _frame.axes[byte] = position[byte];
    }

    // 6502: .TA19 JSR TAS2 / LDY #10 / JSR TAS3 / STA CNT, and then part 4.
    _frame.towards = NormaliseAxes(_frame.axes).vector;
    const AddSignedResult nose = DotProductWithShip(_frame.work, _frame.towards, ORIENTATION_NOSE);
    // 6502: STA CNT -- how far off the nose the target is. `TACTICS`'s own since M2-c-3: parts 4
    // to 8 read it and `TA152` takes it as an argument.
    _frame.offNose = nose.high;

    return Tactic::Steer;
  }

  /*
   * ---- parts 4, 5 and 6: is it scared, does it fire, does its laser hit us -----------------------
   *
   * THE THREE ARE ONE FUNCTION and the reason is two booleans. `fightsOn` is `TA7`'s first
   * `BCC TA3` jumping clean over part 5, and `fellFromFleeTest` is the carry `CMP #230` leaves for
   * the `DORND` in it -- both are live from part 4 into part 5, so a split between them would need
   * them as parameters to say what a local already says (§6.85's rule, applied a second time).
   */
  [[nodiscard]] Tactic DecideCombat(TacticFrame& _frame) noexcept
  {
    /*
     * ---- part 4: is it an Anaconda, is it scared, has it lost its nerve ------------------------
     *
     * 6502: LDA TYPE / CMP #MSL / BNE P%+5 / JMP TA20, AND THE PORT HAS NO LINE FOR IT.
     *
     * In the original a missile reaches part 4 by falling out of `TN4` and `TA19`, and this test
     * is what sends it to `TA20`. The port does not arrive here that way: every branch of part 1's
     * missile block returns, and the two that would have fallen through call
     * `SteerMissileTowardsTarget`, which is `TA19` AND this branch inlined -- see its comment.
     *
     * So a copy of `TA20` stood here as well, and it was unreachable. `tools/mutate.py` is what
     * said so: `ta20-eor` flipped the `EOR #%10000000` in this copy and nothing in a 1,800-case
     * sweep noticed, because no missile has ever reached this line (plan §6.152). Removed rather
     * than left, because dead code no mutation can reach is exactly what a surviving mutant is
     * for finding.
     */

    // 6502: CMP #ANA / BNE TN7 / JSR DORND / CMP #200 / BCC TN7 -- an Anaconda spawns its escort.
    bool anacondaFellThrough = false;
    if (_frame.type == ShipType::Anaconda)
    {
      // 6502: CMP #ANA / BNE TN7 / JSR DORND -- and the compare is what sets the carry, which for
      // an Anaconda is equality and therefore SET.
      const RngResult first = _frame.universe.rng.Next(Byte(_frame.type) >= Byte(ShipType::Anaconda));
      if (first.value >= 200u)
      {
        // 6502: JSR DORND / LDX #WRM / CMP #100 / BCS P%+4 / LDX #SH3 / JMP TN6 -- the carry is
        // `CMP #200`'s, and reaching here means it did not borrow.
        const RngResult second = _frame.universe.rng.Next(true);
        const ShipType escort = (second.value >= 100u) ? ShipType::Worm : ShipType::Sidewinder;
        (void)SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, STATION_LAUNCH_AI, escort,
                             _frame.universe.flight.blueprint);
        return Tactic::Done;
      }

      // 6502: BCC TN7 -- the roll was under 200, so the Anaconda carries on as an ordinary ship
      // and arrives at `TN7` with the carry CLEAR rather than with the _frame.type compare's.
      anacondaFellThrough = true;
    }

    // 6502: .TN7 JSR DORND / CMP #250 / BCC TA7 / JSR DORND / ORA #104 / STA INWK+29 -- six times
    // in 256 a ship rolls for no reason at all, which is most of what makes a dogfight look alive.
    {
      // 6502: .TN7 JSR DORND -- reached either from `CMP #ANA / BNE TN7`, whose carry is
      // `TYPE >= ANA`, or from the Anaconda's own `CMP #200 / BCC TN7`, whose carry is clear. The
      // second is only taken when the first compare was EQUAL, so `TYPE >= ANA` covers neither
      // path wrongly: an Anaconda that falls through arrives with the carry clear.
      const RngResult chance = _frame.universe.rng.Next(anacondaFellThrough ? false : (Byte(_frame.type) >= Byte(ShipType::Anaconda)));
      if (chance.value >= 250u)
      {
        // 6502: JSR DORND / ORA #104 -- the carry is `CMP #250`'s, set by definition here.
        const RngResult amount = _frame.universe.rng.Next(true);
        _frame.work.rollCounter = static_cast<std::uint8_t>(amount.value | 104u);
      }
    }

    /*
     * 6502: .TA7 LDY #14 / LDA (XX0),Y / LSR A / CMP INWK+35 / BCC TA3 -- energy above half the
     * blueprint's maximum and the ship fights on. Below an EIGHTH (`LSR` twice more) it may run,
     * and `DORND / CMP #230` is how often.
     *
     * AND THE TWO BRANCHES DO NOT GO TO THE SAME PLACE. `BCC TA3` here is the CAPITAL label, which
     * is part SIX -- so a ship with more than half its energy jumps over part five and never
     * launches a missile at all. `BCC ta3` below is the lower-case one, which is part five. The
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
        // 6502: JSR DORND / CMP #230 / BCC ta3 -- the carry is the `CMP INWK+35` above, and
        // reaching here means it did not borrow.
        const RngResult flee = _frame.universe.rng.Next(true);
        if (flee.value >= 230u)
        {
          // 6502: LDX TYPE / LDA E%-1,X / BPL ta3 -- bit 7 of the default `NEWB` for this _frame.type is
          // "carries an escape pod", so only a ship that HAS one bails out.
          const std::uint8_t defaults = DefaultNewbFor(_frame.type);
          if ((defaults & 0x80u) != 0u)
          {
            /*
             * 6502: LDA NEWB / AND #%11110000 / STA NEWB / LDY #36 / STA (INF),Y / LDA #0 /
             * STA INWK+32 / JMP SESCP.
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

          // 6502: BPL ta3 -- the ship has no escape pod, so it falls out of the test with the
          // `CMP #230` still standing in the carry.
          fellFromFleeTest = true;
        }
      }
    }

    /*
     * ---- part 5: does it fire ------------------------------------------------------------------
     *
     * 6502: .ta3 LDA INWK+31 / AND #%00000111 / BEQ TA3 / STA T / JSR DORND / AND #31 / CMP T /
     * BCS TA3 -- the bottom three bits of the state byte are how many missiles the ship has, and
     * the chance of it firing one is that count out of thirty-two.
     *
     * `fightsOn` is `TA7`'s first `BCC TA3` jumping clean over this part -- see the comment there.
     */
    const std::uint8_t missiles = MissilesOf(_frame.work.state);
    if (!fightsOn && missiles != 0u)
    {
      // 6502: STA T -- the count is parked for one instruction and read back as `CMP T`, which
      // is `missiles` here: `TACTICS`'s own byte since M2-c-3.

      // 6502: .ta3 ... STA T / JSR DORND -- and `ta3` has two entrances. `BCC ta3` from either
      // energy compare arrives with the carry CLEAR; falling out of the escape-pod test arrives
      // with `CMP #230`'s, which is SET. `AND` and `STA` leave the flag alone either way.
      const RngResult chance = _frame.universe.rng.Next(fellFromFleeTest);

      // 6502: LDA ECMA / BNE TA3 -- an ECM running stops the launch, and the missile is not spent.
      if (static_cast<std::uint8_t>(chance.value & 31u) < missiles && _frame.universe.status.ecmCountdown == 0u)
      {
        --_frame.work.state; // 6502: DEC INWK+31

        // 6502: LDA TYPE / CMP #THG / BNE TA16 / LDX #TGL / LDA INWK+32 / JMP SFS1 -- a Thargoid
        // launches a Thargon and passes ITS OWN AI byte on, which is why Thargons arrive hostile.
        if (_frame.type == ShipType::Thargoid)
        {
          (void)SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, _frame.work.ai,
                               ShipType::Thargon, _frame.universe.flight.blueprint);
          return Tactic::Done;
        }

        // 6502: .TA16 JMP SFRMIS -- and it answers, because a full bubble means no missile.
        if (SpawnChildShip(_frame.universe.bubble, _frame.work, _frame.universe.rng, _frame.slot, _frame.type, SPAWN_CHILD_AI,
                           ShipType::Missile, _frame.universe.flight.blueprint)
              .created)
        {
          ShowMessage(_frame.universe.canvas, _frame.ports.printer, _frame.universe.text, _frame.universe.sentences,
                      _frame.universe.message, MESSAGE_INCOMING_MISSILE, _frame.universe.view, &_frame.universe.picture);
          (void)PlaySoundEffect(_frame.universe.sound, SoundEffect::Missile, false);
        }
        return Tactic::Done;
      }
    }

    /*
     * ---- part 6: does its laser hit us ---------------------------------------------------------
     *
     * 6502: .TA3 LDA #0 / JSR MAS4 / AND #%11100000 / BNE TA4 -- too far away on any axis and
     * nothing can be fired.
     */
    if ((LargestShipAxis(_frame.work, 0u) & 0xE0u) == 0u)
    {
      const std::uint8_t offNose = _frame.offNose;

      // 6502: LDX CNT / CPX #160 / BCC TA4 -- and 160 has bit 7 set, so this is also "in front".
      if (offNose >= 160u)
      {
        // 6502: LDY #19 / LDA (XX0),Y / AND #%11111000 / BEQ TA4 -- the blueprint's laser power,
        // and the bottom three bits are the missile count rather than power.
        const std::uint8_t laser = static_cast<std::uint8_t>(_frame.universe.flight.blueprint->weapons & 0xF8u);
        if (laser != 0u)
        {
          // 6502: LDA INWK+31 / ORA #%01000000 / STA INWK+31 -- bit 6 is "firing", which is what
          // draws the line from its nose in part 11 of the flight loop.
          _frame.work.state = With(_frame.work.state, ShipStateBit::Firing);

          // 6502: CPX #163 / BCC TA4 -- firing is one cone and HITTING is a tighter one.
          if (offNose >= 163u)
          {
            /*
             * 6502: LDA (XX0),Y / LSR A / JSR OOPS -- half the laser power, and the byte is read
             * AGAIN unmasked, so the missile count in its bottom three bits is part of the damage.
             *
             * AND THE `LSR` IS ALSO THE CARRY. `OOPS` opens `STA T ... LDA FSH / SBC T`, and the
             * only thing between the shift and that subtraction is the `JSR`, so bit 0 of the
             * blueprint's byte 19 decides whether the player loses one more unit of shield than
             * the arithmetic says (§6.125). Found by a sweep that put the ship BESIDE us rather
             * than in front, which is the only geometry in it that reaches this line.
             */
            const std::uint8_t power = _frame.universe.flight.blueprint->weapons;
            const std::uint8_t damage = static_cast<std::uint8_t>(power >> 1u);
            if (!TakeDamage(_frame.universe, _frame.ports, _frame.universe.bubble.blocks[_frame.slot], damage, (power & 1u) != 0u))
            {
              return Tactic::Fatal;
            }

            --_frame.work.acceleration; // 6502: DEC INWK+28 -- it slows down as it fires

            // 6502: LDA ECMA / BNE TA9-1 -- and `TA9-1` is the `RTS` one byte before `TA9`, so an
            // ECM running silences the hit and returns rather than skipping the sound (§6.125).
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
     * 6502: .TA4 LDA INWK+7 / CMP #3 / BCS TA5 / LDA INWK+1 / ORA INWK+4 / AND #%11111110 /
     * BEQ TA15 -- a ship that is very close steers WITHOUT the reversal below, which is what stops
     * it turning away the moment it arrives.
     */
    bool reverse = true;
    if (_frame.work.z.hi < 3u && (static_cast<std::uint8_t>(_frame.work.x.hi | _frame.work.y.hi) & 0xFEu) == 0u)
    {
      reverse = false;
    }
    else
    {
      // 6502: .TA5 JSR DORND / ORA #%10000000 / CMP INWK+32 / BCS TA15 -- a random byte with bit 7
      // forced on, against the AI byte: the more aggressive the ship, the more often it presses in.
      // 6502: .TA5 JSR DORND -- reached from `CMP #3 / BCS TA5`, whose carry is set, or by falling
      // past the `BEQ TA15` below it, where the `AND` left the flag as the compare set it.
      const RngResult press = _frame.universe.rng.Next(_frame.work.z.hi >= 3u);
      if (With(press.value, AiBit::Active) >= _frame.work.ai)
      {
        reverse = false;
      }
    }

    if (reverse)
    {
      // 6502: .TA20 JSR TAS6 / LDA CNT / EOR #%10000000 / .TA152 STA CNT -- turn the vector round
      // and flip the sign of how far off it is, which is how a ship backs away.
      SteerTowards(_frame.universe, _frame.ports, NegateVector(_frame.towards), static_cast<std::uint8_t>(_frame.offNose ^ 0x80u));
      return Tactic::Done;
    }

    SteerTowards(_frame.universe, _frame.ports, _frame.towards, _frame.offNose); // 6502: .TA15, entered with `CNT` already set
    return Tactic::Done;
  }

  /*
   * 6502: TACTICS -- one pass of a ship's AI, as the parts it always had (M4-c).
   *
   * Each part ANSWERS and this performs: `TN2`'s `JMP DOCKIT` is a call here, where the delegation
   * is visible, rather than a tail call whose boolean was passed through.
   */
  bool RunTactics(Universe& _universe, Ports& _ports, std::uint8_t _slot) noexcept
  {
    TacticFrame frame{_universe, _ports, _universe.work, _universe.axes, _universe.flight.type, _slot};

    // 6502: .TACTICS LDA #3 / STA RAT / LDA #4 / STA RAT2 / LDA #22 / STA CNT2 -- and `DOCKIT`
    // overwrites all three, which is the whole difference between flying and being flown.
    _universe.flight.signMask = TACTICS_RAT;
    _universe.flight.signMask2 = TACTICS_RAT2;
    _universe.flight.steerCone = TACTICS_CNT2;

    Tactic tactic = Tactic::Steer;

    if (frame.type == ShipType::Missile) // 6502: CPX #MSL / BEQ TA18
    {
      tactic = DecideMissile(frame);
    }
    else if (frame.type == ShipType::Station) // 6502: CPX #SST / BNE TA13
    {
      tactic = DecideStation(frame);
    }
    else
    {
      tactic = DecideDisposition(frame); // 6502: parts 3
      if (tactic == Tactic::Steer)
      {
        tactic = DecideCombat(frame); // 6502: parts 4, 5 and 6
      }
      if (tactic == Tactic::Steer)
      {
        tactic = SteerTowardsTarget(frame); // 6502: part 7
      }
    }

    switch (tactic)
    {
    case Tactic::Docking:
      // 6502: .TN2 JMP DOCKIT -- a tail call in the original and a call here, so that the one
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

    // 6502: LDA #6 / STA RAT2 / LSR A / STA RAT / LDA #29 / STA CNT2 -- and `RAT` is the six
    // shifted, not a second constant.
    _universe.flight.signMask2 = DOCKING_RAT2;
    _universe.flight.signMask = static_cast<std::uint8_t>(DOCKING_RAT2 >> 1u);
    _universe.flight.steerCone = DOCKING_CNT2;

    // 6502: LDA SSPR / BNE P%+5 / .GOPLS JMP GOPL -- no station in the bubble, so steer at the
    // planet and stop pretending to dock.
    if (_universe.bubble.StationPresent() == 0u)
    {
      AimAtPlanet(_universe, _ports);
      return;
    }

    (void)SubtractStationAxes(_universe.bubble, work, axes); // 6502: JSR VCSU1

    // 6502: LDA K3+2 / ORA K3+5 / ORA K3+8 / AND #%01111111 / BNE GOPLS -- any axis whose HIGH
    // byte has magnitude at all means the station is far away, and the sign is masked off because
    // a station behind you is still close.
    if ((static_cast<std::uint8_t>(axes[2] | axes[5] | axes[8]) & 0x7Fu) != 0u)
    {
      AimAtPlanet(_universe, _ports);
      return;
    }

    /*
     * 6502: JSR TA2 / LDA Q / STA K -- and `Q` is what `NORM` left, the vector's LENGTH.
     *
     * `TA2` is the tail of `TAS2` (the shifting loop is skipped because the test above has just
     * proved the coordinates are small), and it falls into `NORM`, which divides by the length it
     * computed in `Q`. So `K` ends up holding how far away the station is, measured on the way to
     * working out which way it is.
     */
    const std::uint8_t distance = BuildUnitVector(axes).length;

    /*
     * 6502: JSR TAS2 -- and this is a SECOND normalisation, of the same `K3`, immediately after the
     * first. `TA2` skipped the shifting loop; `TAS2` runs it, so the vector `XX15` ends up holding
     * is the shifted one and not the one the length was taken from. The port did the first call and
     * not the second, and every docking approach came out on the wrong branch (§6.125).
     */
    const UnitVector towards = NormaliseAxes(axes).vector;

    // 6502: LDY #10 / JSR TAS4 / BMI PH1 / CMP #35 / BCC PH1 -- the STATION's nose against the
    // vector to it, so this asks "am I in front of the slot", and anything else goes to `PH1`.
    const AddSignedResult alongSlot = DotProductWithShip(_universe.bubble.blocks[1], towards, ORIENTATION_NOSE);

    bool fineApproach = false;
    bool wideApproach = false;

    if ((alongSlot.high & 0x80u) != 0u || alongSlot.high < 35u)
    {
      wideApproach = true;
    }
    else
    {
      // 6502: LDY #10 / JSR TAS3 / CMP #&A2 / BCS PH3 -- OUR nose against the same vector, so this
      // asks "am I pointing at it", and &A2 is a wide enough cone to fly straight in.
      const AddSignedResult ourNose = DotProductWithShip(work, towards, ORIENTATION_NOSE);
      if (ourNose.high >= 0xA2u)
      {
        fineApproach = true;
      }
      // 6502: LDA K / CMP #157 / BCC PH2 / LDA TYPE / BMI PH3 -- close enough and it is the fine
      // approach for a NEGATIVE type, which is the player's own computer (`auton` stores 224 in
      // `TYPE`); a ship keeps turning towards the slot instead.
      else if (distance >= 157u && IsBody(_universe.flight.type))
      {
        fineApproach = true;
      }
    }

    if (wideApproach)
    {
      /*
       * 6502: .PH1 JSR VCSU1 / JSR DCS1 / JSR DCS1 / JSR TAS2 / JSR TAS6 / JMP TA151.
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
      // 6502: .PH2 JSR TAS6 / JSR TA151, and then it FALLS INTO `PH22` rather than returning.
      AimAlongNose(_universe, _ports, NegateVector(towards));
      HaltAndTurn(work);
      return;
    }

    /*
     * 6502: .PH3 -- the fine approach, and the first thing it does is throw the turn rate away.
     *
     * `LDX #0 / STX RAT2 / STX INWK+30` means no pitch and no tolerance: from here the ship is
     * lined up and the corrections are made by hand below rather than by the shared steering.
     */
    _universe.flight.signMask2 = 0u;
    work.pitchCounter = 0u;

    // 6502: LDA TYPE / BPL PH32 -- and a NEGATIVE type is the player's own docking computer, which
    // `auton` marks by storing &E0 in `TYPE`. A ship being flown in by the AI skips all of this.
    if (IsBody(_universe.flight.type))
    {
      /*
       * 6502: EOR XX15 / EOR XX15+1 / ASL A / LDA #2 / ROR A / STA INWK+29.
       *
       * Three signs folded together -- the type's, the x component's and the y's -- and the `ASL`
       * pushes the result into the carry so the `ROR` can put it back on top. `ROR` of a 2 is a
       * ONE with the carry above it, so the roll is always magnitude one and all this arithmetic
       * decides is its direction.
       */
      const std::uint8_t folded = static_cast<std::uint8_t>(Byte(_universe.flight.type) ^ towards.x ^ towards.y);
      work.rollCounter = static_cast<std::uint8_t>((2u >> 1u) | ((folded & 0x80u) != 0u ? 0x80u : 0x00u));

      // 6502: LDA XX15 / ASL A / CMP #12 / BCS PH22 -- too far off sideways, so stop and turn.
      if (static_cast<std::uint8_t>(towards.x << 1u) >= 12u)
      {
        HaltAndTurn(work);
        return;
      }

      // 6502: LDA XX15+1 / ASL A / LDA #2 / ROR A / STA INWK+30 -- the same shape for the pitch.
      work.pitchCounter = static_cast<std::uint8_t>((2u >> 1u) | ((towards.y & 0x80u) != 0u ? 0x80u : 0x00u));

      if (static_cast<std::uint8_t>(towards.y << 1u) >= 12u)
      {
        HaltAndTurn(work);
        return;
      }
    }

    // 6502: .PH32 STX INWK+29 -- and X is still the zero from `PH3`, so the roll the block above
    // may have set is thrown away again for a ship that is lined up.
    work.rollCounter = 0u;

    // 6502: LDA INWK+22 / STA XX15 ... -- the ship's own SIDE vector into `XX15`, which is asking
    // "is the station's roof lined up with my side", the last thing that has to match to fit
    // through a slot.
    const UnitVector side{work.side.x.hi, work.side.y.hi, work.side.z.hi};

    // 6502: LDY #16 / JSR TAS4 / ASL A / CMP #66 / BCS TN11.
    const AddSignedResult roll = DotProductWithShip(_universe.bubble.blocks[1], side, ORIENTATION_ROOF);
    if (static_cast<std::uint8_t>(roll.high << 1u) >= 66u)
    {
      // 6502: .TN11 INC INWK+28 / LDA #%01111111 / STA INWK+29 / BNE TN13 -- roll as hard as the
      // byte allows and speed up, which is how a ship spins itself into line with the slot.
      ++work.acceleration;
      work.rollCounter = 0x7Fu;
    }
    else
    {
      HaltAndTurn(work); // 6502: JSR PH22, and this one is a `JSR` -- it comes back
    }

    /*
     * 6502: .TN13 LDA K3+10 / BNE TNRTS / ASL NEWB / SEC / ROR NEWB.
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

    // 6502: ASL NEWB / SEC / ROR NEWB -- the same three-instruction "set bit 7" as `TA873`, and
    // the same mistake: the shifts cancel (§6.126).
    work.traits = With(work.traits, TraitBit::Remove);
  }

} // namespace Elite
