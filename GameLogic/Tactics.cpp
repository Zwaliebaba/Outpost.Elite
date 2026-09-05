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
    void BuildUnitVector(const K3Block& _axes, DrawWorkspace& _draw, MathWorkspace& _math) noexcept
    {
      _draw.x1 = static_cast<std::uint8_t>((_axes[1] >> 1u) | _axes[2]);
      _draw.y1 = static_cast<std::uint8_t>((_axes[4] >> 1u) | _axes[5]);
      _draw.x2 = static_cast<std::uint8_t>((_axes[7] >> 1u) | _axes[8]);

      // 6502: and no RTS -- `TA2` falls into `NORM`, exactly as `TAS2` does above it.
      std::array<std::uint8_t, 3> vector = {_draw.x1, _draw.y1, _draw.x2};
      Normalise(_math, std::span<std::uint8_t, 3>(vector));
      _draw.x1 = vector[0];
      _draw.y1 = vector[1];
      _draw.x2 = vector[2];
    }

    /*
     * 6502: TA15 to TA10 -- the steering, and the only part of `TACTICS` that `DOCKIT` shares.
     *
     * Two dot products and two thresholds. The ROOF vector says whether to pitch and which way; the
     * SIDE vector says whether to roll; `RAT2` is how far off the nose the ship tolerates before it
     * bothers turning and `RAT` is how hard it turns when it does. `CNT` -- the nose dot product
     * the caller measured -- then decides the throttle.
     */
    void SteerTowards(FlightLoop& _loop, std::uint8_t _cnt) noexcept
    {
      FlightScreen& screen = _loop.screen;
      ShipBlock& work = screen.work;
      MathWorkspace& math = screen.math;

      math.cnt = _cnt; // 6502: .TA152 STA CNT

      // 6502: .TA15 LDY #16 / JSR TAS3 / TAX / EOR #%10000000 / AND #%10000000 / STA INWK+30.
      const AddSignedResult roof = DotProductWithShip(work, screen.draw, math, ORIENTATION_ROOF);
      work[30] = static_cast<std::uint8_t>((roof.high ^ 0x80u) & 0x80u);

      /*
       * 6502: TXA / ASL A / CMP RAT2 / BCC TA11 / LDA RAT / ORA INWK+30 / STA INWK+30.
       *
       * `TXA` brings back the byte `TAX` saved four instructions ago -- the dot product's A, before
       * the two masks flattened it to a sign. So the magnitude decides whether to pitch at all and
       * the sign decides which way, out of one measurement read twice.
       */
      if (static_cast<std::uint8_t>(roof.high << 1u) >= screen.flight.rat2)
      {
        work[30] = static_cast<std::uint8_t>(screen.flight.rat | work[30]);
      }

      // 6502: .TA11 LDA INWK+29 / ASL A / CMP #32 / BCS TA6 -- a ship already rolling hard is left
      // to finish the roll rather than given a new one.
      if (static_cast<std::uint8_t>(work[29] << 1u) < 32u)
      {
        // 6502: LDY #22 / JSR TAS3 / TAX / EOR INWK+30 / AND #%10000000 / EOR #%10000000 --
        // the roll's direction is the side dot product XORed with the PITCH just chosen, which is
        // what makes a ship bank into its turn rather than roll and pitch independently.
        const AddSignedResult side = DotProductWithShip(work, screen.draw, math, ORIENTATION_SIDE);
        work[29] = static_cast<std::uint8_t>((((side.high ^ work[30]) & 0x80u) ^ 0x80u));

        if (static_cast<std::uint8_t>(side.high << 1u) >= screen.flight.rat2)
        {
          work[29] = static_cast<std::uint8_t>(screen.flight.rat | work[29]);
        }
      }

      /*
       * 6502: .TA6 LDA CNT / BMI TA9 / CMP CNT2 / BCC TA9 / .PH10E LDA #3 / STA INWK+28 / RTS.
       *
       * `CNT` is the NOSE dot product, so bit 7 means the target is behind. Behind, or inside the
       * cone `CNT2` names, and the ship throttles back to 3 and stops here.
       */
      const std::uint8_t cnt = math.cnt;
      if ((cnt & 0x80u) == 0u && cnt >= math.cnt2)
      {
        work[28] = 3u;
        return;
      }

      // 6502: .TA9 AND #%01111111 / CMP #18 / BCC TA10 -- and `TA10` is a bare `RTS`.
      if (static_cast<std::uint8_t>(cnt & 0x7Fu) < 18u)
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
      work[28] = (screen.flight.type == SHIP_TYPE_MISSILE) ? static_cast<std::uint8_t>(0xFFu << 1u) : std::uint8_t{0xFFu};
    }

    /// 6502: .TA151 -- one nose dot product, which can throw the turn rate away, then `TA152`.
    void AimAlongNose(FlightLoop& _loop) noexcept
    {
      FlightScreen& screen = _loop.screen;

      // 6502: LDY #10 / JSR TAS3 / CMP #&98 / BCC ttt / LDX #0 / STX RAT2.
      const AddSignedResult nose = DotProductWithShip(screen.work, screen.draw, screen.math, ORIENTATION_NOSE);
      if (nose.high >= 0x98u)
      {
        screen.flight.rat2 = 0u;
      }

      SteerTowards(_loop, nose.high); // 6502: .ttt JMP TA152
    }

    /// 6502: .GOPL -- give up on the station and steer at the PLANET instead.
    void AimAtPlanet(FlightLoop& _loop) noexcept
    {
      FlightScreen& screen = _loop.screen;

      // 6502: JSR SPS1 / JMP TA151 -- `SPS1` is the compass's own "where is the planet", and it
      // leaves the unit vector in `XX15` exactly where the steering wants it.
      LoadPlanetAxes(screen.bubble, _loop.axes, screen.draw, screen.math);
      AimAlongNose(_loop);
    }

    /// 6502: .PH22 -- stop dead and turn on the spot, which is what an autopilot does when it is
    /// pointing the wrong way.
    void HaltAndTurn(ShipBlock& _work) noexcept
    {
      _work[28] = 0u; // 6502: LDX #0 / STX INWK+28
      _work[27] = 1u; // 6502: INX / STX INWK+27
    }

    /// 6502: .TA873 -- ASL INWK+31 / SEC / ROR INWK+31, which sets bit 7 (killed) and keeps the
    /// rest. Written as a rotate rather than an `ORA` because the `ASL` clears bit 0 on the way.
    void MarkAsKilled(ShipBlock& _work) noexcept
    {
      _work[31] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(_work[31] << 1u) | 0x80u);
    }

  } // namespace

  void SubtractShipAxis(const ShipBlock& _other, const ShipBlock& _work, K3Block& _axes, MathWorkspace& _math, std::uint8_t _at) noexcept
  {
    // 6502: LDA (V),Y / EOR #%10000000 / STA K+3 -- the other object's sign, negated.
    _math.k[3] = static_cast<std::uint8_t>(_other[_at + 2u] ^ 0x80u);

    // 6502: DEY / LDA (V),Y / STA K+2 / DEY / LDA (V),Y / STA K+1.
    _math.k[2] = _other[_at + 1u];
    _math.k[1] = _other[_at];

    // 6502: STY U / LDX U / JSR MVT3 -- K = K + INWK+X, so K is now this ship minus the other.
    _math.u = _at;
    AddShipCoordinateToK(_work, _math, _at);

    // 6502: STA K3+2,X, and the A it stores is the sign byte `MVT3` left in the register.
    _axes[_at + 2u] = _math.k[3];
    _axes[_at + 1u] = _math.k[2];
    _axes[_at] = _math.k[1];
  }

  void SubtractShipAxes(const ShipBlock& _other, const ShipBlock& _work, K3Block& _axes, MathWorkspace& _math) noexcept
  {
    // 6502: LDY #2 / JSR TAS1 / LDY #5 / JSR TAS1 / LDY #8, and the last one is a fall-through.
    SubtractShipAxis(_other, _work, _axes, _math, 0u);
    SubtractShipAxis(_other, _work, _axes, _math, 3u);
    SubtractShipAxis(_other, _work, _axes, _math, 6u);
  }

  void SubtractStationAxes(const Bubble& _bubble, const ShipBlock& _work, K3Block& _axes, MathWorkspace& _math) noexcept
  {
    // 6502: LDA #LO(K%+NI%) / STA V / LDA #HI(K%+NI%), and then straight into `VCSUB`.
    SubtractShipAxes(_bubble.blocks[1], _work, _axes, _math);
  }

  AddSignedResult DotProductWithShip(const ShipBlock& _block, const DrawWorkspace& _draw, MathWorkspace& _math, std::uint8_t _at) noexcept
  {
    // 6502: LDX INWK,Y / STX Q / LDA XX15 / JSR MULT12 -- (S R) = vect_x * XX15.
    _math.q = _block[_at];
    MultiplySignedToSR(_math, _draw.x1);

    // 6502: LDX INWK+2,Y / STX Q / LDA XX15+1 / JSR MAD / STA S / STX R.
    _math.q = _block[_at + 2u];
    const AddSignedResult second = MultiplyAndAdd(_math, _draw.y1);
    _math.s = second.high;
    _math.r = second.low;

    // 6502: LDX INWK+4,Y / STX Q / LDA XX15+2, and no `JSR` -- it falls into `MAD`.
    _math.q = _block[_at + 4u];
    return MultiplyAndAdd(_math, _draw.x2);
  }

  void NegateVector(DrawWorkspace& _draw) noexcept
  {
    // 6502: three EOR #%10000000s over XX15, XX15+1 and XX15+2.
    _draw.x1 = static_cast<std::uint8_t>(_draw.x1 ^ 0x80u);
    _draw.y1 = static_cast<std::uint8_t>(_draw.y1 ^ 0x80u);
    _draw.x2 = static_cast<std::uint8_t>(_draw.x2 ^ 0x80u);
  }

  void OffsetDockingPosition(const Bubble& _bubble, K3Block& _axes) noexcept
  {
    const ShipBlock& station = _bubble.blocks[1];

    // 6502: JSR P%+3 -- the body twice, so each subtraction is the nose vector times four.
    for (int pass = 0; pass < 2; ++pass)
    {
      OffsetAxis(_axes, station[NOSE_VECTOR_X], 0u); // 6502: LDA K%+NI%+10 / LDX #0 / JSR TAS7
      OffsetAxis(_axes, station[NOSE_VECTOR_Y], 3u); // 6502: LDA K%+NI%+12 / LDX #3 / JSR TAS7
      OffsetAxis(_axes, station[NOSE_VECTOR_Z], 6u); // 6502: LDA K%+NI%+14 / LDX #6, a fall-through
    }
  }

  void Anger(Bubble& _bubble, const FlightState& _flight, std::uint8_t _slot, std::uint8_t _type) noexcept
  {
    ShipBlock& station = _bubble.blocks[1];

    // 6502: .AN2 LDA K%+NI%+36 / ORA #%00000100 / STA K%+NI%+36 -- the station is always slot 1,
    // so this is a fixed address in the original and a fixed index here.
    const auto angerStation = [&station]() noexcept { station[36] = static_cast<std::uint8_t>(station[36] | NEWB_HOSTILE); };

    if (_type == SHIP_TYPE_STATION)
    {
      angerStation(); // 6502: CMP #SST / BEQ AN2, and AN2 returns -- nothing else happens
      return;
    }

    ShipBlock& ship = _bubble.blocks[_slot];

    // 6502: LDY #36 / LDA (INF),Y / AND #%00100000 / BEQ P%+5 / JSR AN2 -- and it is a `JSR`, so
    // an ally of the station angers the station AND carries on being angered itself.
    if ((ship[36] & NEWB_STATION_ALLY) != 0u)
    {
      angerStation();
    }

    // 6502: LDY #32 / LDA (INF),Y / BEQ HI1 -- and `HI1` is a bare `RTS` inside `HITCH`. A ship
    // with no AI byte is left entirely alone: no acceleration, no dive, no hostile flag.
    if (ship[32] == 0u)
    {
      return;
    }

    ship[32] = static_cast<std::uint8_t>(ship[32] | 0x80u); // 6502: ORA #%10000000 / STA (INF),Y

    // 6502: LDY #28 / LDA #2 / STA (INF),Y / ASL A / LDY #30 / STA (INF),Y.
    ship[28] = ANGRY_ACCELERATION;
    ship[30] = static_cast<std::uint8_t>(ANGRY_ACCELERATION << 1u);

    // 6502: LDA TYPE / CMP #CYL / BCC AN3 -- the LOOP's type byte, not the one in A.
    if (_flight.type >= SHIP_TYPE_COBRA_MK3)
    {
      ship[36] = static_cast<std::uint8_t>(ship[36] | NEWB_HOSTILE);
    }
  }

  bool RunTactics(FlightLoop& _loop, std::uint8_t _slot) noexcept
  {
    FlightScreen& screen = _loop.screen;
    ShipBlock& work = screen.work;
    MathWorkspace& math = screen.math;
    K3Block& axes = _loop.axes;
    const std::uint8_t type = screen.flight.type;

    // 6502: .TACTICS LDA #3 / STA RAT / LDA #4 / STA RAT2 / LDA #22 / STA CNT2 -- and `DOCKIT`
    // overwrites all three, which is the whole difference between flying and being flown.
    screen.flight.rat = TACTICS_RAT;
    screen.flight.rat2 = TACTICS_RAT2;
    math.cnt2 = TACTICS_CNT2;

    /*
     * 6502: CPX #MSL / BEQ TA18 -- and `TA18` is in PART 1, which is not the beginning. A missile
     * has its own logic and rejoins the common tail only through `TA19` or `TA34`.
     */
    if (type == SHIP_TYPE_MISSILE)
    {
      // 6502: .TA18 LDA ECMA / BNE TA352 -- an ECM going off destroys the missile without anybody
      // having to hit it, and `TA352` is how a missile dies.
      bool destroyed = screen.status.ecmCountdown != 0u;

      if (!destroyed)
      {
        // 6502: LDA INWK+32 / ASL A / BMI TA34 -- bit 6 of the AI byte says the missile is aimed at
        // US, and the `ASL` reads it by moving it into bit 7.
        if ((work[32] & 0x40u) != 0u)
        {
          /*
           * 6502: .TA34 LDA #0 / JSR MAS4 / BEQ P%+5 / JMP TN4 -- how far away the missile is, and
           * a missile that is not yet touching us goes back to the common steering at `TN4`.
           */
          if (LargestShipAxis(work, 0u) != 0u)
          {
            // 6502: .TN4 LDX #8 / .TAL1 LDA INWK,X / STA K3,X -- the missile's own position becomes
            // the vector to steer along, because the thing it is chasing is at the origin: us.
            for (std::size_t byte = 0; byte < 9u; ++byte)
            {
              axes[byte] = work[byte];
            }

            // 6502: .TA19 JSR TAS2 / LDY #10 / JSR TAS3 / STA CNT, and then the whole of part 7.
            NormaliseAxes(axes, screen.draw, math);
            const AddSignedResult nose = DotProductWithShip(work, screen.draw, math, ORIENTATION_NOSE);
            SteerTowards(_loop, nose.high);
            return true;
          }

          // 6502: JSR TA873 / JSR EXNO3 / LDA #250 / JMP OOPS -- it has arrived. The missile is
          // marked dead, the explosion is heard, and 250 is nearly always fatal.
          MarkAsKilled(work);
          (void)_loop.effects.PlaySound(SOUND_EXPLOSION, false);
          return TakeDamage(screen, _loop.effects, screen.bubble.blocks[_slot], MISSILE_DAMAGE, false);
        }

        // 6502: LSR A / TAX / LDA UNIV,X / STA V / LDA UNIV+1,X / JSR VCSUB -- the missile's TARGET
        // slot, out of the AI byte it has been carrying since `FRS1` doubled `MSTG` into it.
        const std::uint8_t target = static_cast<std::uint8_t>((work[32] & 0x7Fu) >> 1u);
        SubtractShipAxes(screen.bubble.blocks[target], work, axes, math);

        /*
         * 6502: LDA K3+2 / ORA K3+5 / ORA K3+8 / AND #%01111111 / ORA K3+1 / ORA K3+4 / ORA K3+7 /
         * BNE TA64 -- the three high bytes with their signs masked OR the three middle bytes, so
         * this is "is the target still further away than 256 units on any axis".
         */
        // NOT `far`: that is a macro in <windows.h>, like `near`, and `check_gamelogic.py` exists
        // to say so (AGENTS.md §5).
        const std::uint8_t distant =
          static_cast<std::uint8_t>((static_cast<std::uint8_t>(axes[2] | axes[5] | axes[8]) & 0x7Fu) | axes[1] | axes[4] | axes[7]);
        if (distant != 0u)
        {
          // 6502: .TA64 JSR DORND / CMP #16 / BCS TA19S -- one time in sixteen the missile checks
          // whether its target still has an ECM, and the rest of the time it just steers.
          const RngResult roll = screen.rng.Next(false);
          if (roll.value < 16u)
          {
            // 6502: .M32 LDY #32 / LDA (V),Y / LSR A / BCS P%+5 / JMP ECBLB2 -- bit 0 of the
            // target's AI byte is "it has an ECM", and setting it off is the target's doing.
            if ((screen.bubble.blocks[target][32] & 1u) == 0u)
            {
              StartEcm(screen.canvas, screen.status, _loop.effects, false);
              return true;
            }
          }

          // 6502: JMP TA19 -- steer at the target, whose vector `VCSUB` has just left in `K3`.
          NormaliseAxes(axes, screen.draw, math);
          const AddSignedResult nose = DotProductWithShip(work, screen.draw, math, ORIENTATION_NOSE);
          SteerTowards(_loop, nose.high);
          return true;
        }

        // 6502: LDA INWK+32 / CMP #%10000010 / BEQ TA352 -- a missile that has reached the ship in
        // slot 1 dies rather than exploding, because slot 1 is the station.
        destroyed = (work[32] == 0x82u);

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
          ShipBlock& victim = screen.bubble.blocks[target];
          if ((victim[31] & 0x20u) == 0u)
          {
            victim[31] = static_cast<std::uint8_t>(victim[31] | 0x80u);
          }
        }
      }

      if (destroyed)
      {
        // 6502: .TA352 LDA INWK / ORA INWK+3 / ORA INWK+6 / BNE TA872 / LDA #80 / JSR OOPS -- a
        // missile dying right beside us still hurts, and 80 is a survivable amount.
        if (static_cast<std::uint8_t>(work[0] | work[3] | work[6]) == 0u)
        {
          if (!TakeDamage(screen, _loop.effects, screen.bubble.blocks[_slot], COLLISION_DAMAGE, false))
          {
            return false;
          }
        }

        // 6502: .TA872 LDX #PLT / BNE TA353 -- and `TA353` is `JSR EXNO2` with X as the type, so
        // the explosion is scored as though a plate had been destroyed.
        RecordKill(screen, _loop.effects, SHIP_TYPE_ALLOY_PLATE);
        MarkAsKilled(work); // 6502: .TA873 -- falls straight through from `TA353`
        return true;
      }

      // 6502: .TA35 LDA INWK / ORA INWK+3 / ORA INWK+6 / BNE TA87 / LDA #80 / JSR OOPS.
      if (static_cast<std::uint8_t>(work[0] | work[3] | work[6]) == 0u)
      {
        if (!TakeDamage(screen, _loop.effects, screen.bubble.blocks[_slot], COLLISION_DAMAGE, false))
        {
          return false;
        }
      }

      // 6502: .TA87 LDA INWK+32 / AND #%01111111 / LSR A / TAX / .TA353 JSR EXNO2 -- the TARGET's
      // slot becomes the type handed to `EXNO2`, which is what makes a big ship a loud explosion.
      RecordKill(screen, _loop.effects, static_cast<std::uint8_t>((work[32] & 0x7Fu) >> 1u));
      MarkAsKilled(work);
      return true;
    }

    /*
     * 6502: CPX #SST / BNE TA13 -- a station does not fly, it LAUNCHES, and which ship it launches
     * depends on whether the player has made it angry.
     */
    if (type == SHIP_TYPE_STATION)
    {
      std::uint8_t launch = 0;

      // 6502: LDA NEWB / AND #%00000100 / BNE TN5 -- the hostile bit `ANGRY` sets.
      if ((work[36] & NEWB_HOSTILE) == 0u)
      {
        // 6502: LDA MANY+SHU+1 / BNE TA1 -- one Transporter at a time, and `MANY+SHU+1` is the
        // count of the type ABOVE the Shuttle because the two are launched as a pair.
        if (screen.bubble.counts[SHIP_TYPE_SHUTTLE + 1u] != 0u)
        {
          return true;
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
        const RngResult roll = screen.rng.Next(type >= SHIP_TYPE_STATION);
        if (roll.value < 253u)
        {
          return true;
        }
        launch = static_cast<std::uint8_t>((roll.value & 1u) + (SHIP_TYPE_SHUTTLE - 1u) + 1u);
      }
      else
      {
        // 6502: .TN5 JSR DORND / CMP #240 / BCC TA1 / LDA MANY+COPS / CMP #4 / BCS TA22 -- and the
        // carry is `CPX #SST`'s again, by the same argument.
        const RngResult roll = screen.rng.Next(type >= SHIP_TYPE_STATION);
        if (roll.value < 240u || screen.bubble.counts[SHIP_TYPE_VIPER] >= MAXIMUM_POLICE)
        {
          return true;
        }
        launch = SHIP_TYPE_VIPER;
      }

      // 6502: .TN6 LDA #%11110001 / JMP SFS1 -- hostile, aggressive, and out of the slot.
      (void)SpawnChildShip(screen.bubble, work, screen.rng, math, _slot, type, STATION_LAUNCH_AI, launch, screen.flight.blueprint);
      return true;
    }

    // 6502: .TA13 CPX #HER / BNE TA17 -- a rock hermit is an asteroid until it is shot at, and
    // then it is a pirate.
    if (type == SHIP_TYPE_HERMIT)
    {
      // 6502: JSR DORND / CMP #200 / BCC TA22 -- and the carry is `CPX #HER`'s, which a hermit
      // satisfies with equality, so it is set.
      const RngResult roll = screen.rng.Next(type >= SHIP_TYPE_HERMIT);
      if (roll.value < 200u)
      {
        return true;
      }

      /*
       * 6502: LDX #0 / STX INWK+32 / LDX #%00100100 / STX NEWB / AND #3 / ADC #SH3 / TAX /
       * JSR TN6 / LDA #0 / STA INWK+32 / RTS.
       *
       * The AI byte is cleared BEFORE the spawn and again after it, because `SFS1` copies `INWK`
       * into the new ship: clearing it first is what stops the pirate inheriting the hermit's AI,
       * and clearing it after is what stops the HERMIT flying off.
       */
      work[32] = 0u;
      work[36] = HERMIT_PIRATE_NEWB;

      // 6502: AND #3 / ADC #SH3 -- and the carry is the `CMP #200`'s, which is SET on this path.
      const std::uint8_t pirate = static_cast<std::uint8_t>((roll.value & 3u) + SHIP_TYPE_SIDEWINDER + 1u);
      (void)SpawnChildShip(screen.bubble, work, screen.rng, math, _slot, type, STATION_LAUNCH_AI, pirate, screen.flight.blueprint);

      work[32] = 0u;
      return true;
    }

    // 6502: .TA17 LDY #14 / LDA INWK+35 / CMP (XX0),Y / BCS TA21 / INC INWK+35 -- energy regrows
    // one unit a turn up to the blueprint's maximum, which is why a damaged ship you leave alone
    // is a whole ship when you come back.
    if (work[35] < ShipByte(static_cast<std::uint16_t>(screen.flight.blueprint + 14u)))
    {
      ++work[35];
    }

    // 6502: .TA21 CPX #TGL / BNE TA14 / LDA MANY+THG / BNE TA14 -- a Thargon whose Thargoid is
    // dead loses its AI and half its speed, and drifts.
    if (type == SHIP_TYPE_THARGON && screen.bubble.counts[SHIP_TYPE_THARGOID] == 0u)
    {
      work[32] = static_cast<std::uint8_t>(work[32] & 0xFEu); // 6502: LSR INWK+32 / ASL INWK+32
      work[27] = static_cast<std::uint8_t>(work[27] >> 1u);   // 6502: LSR INWK+27
      return true;                                            // 6502: .TA22 RTS
    }

    /*
     * 6502: .TA14 JSR DORND / LDA NEWB / LSR A / BCC TN1 / CPX #50 / BCS TA22.
     *
     * The `DORND`'s A is thrown away and its X is not: `CPX #50` reads the PREVIOUS random byte.
     * Bit 0 of `NEWB` is "trader", and a trader with a roll of 50 or more simply carries on --
     * which is why traders mostly ignore you and occasionally do not.
     */
    const RngResult roll = screen.rng.Next(type >= SHIP_TYPE_THARGON);
    std::uint8_t flags = work[36];

    if ((flags & 1u) != 0u && roll.previous >= TRADER_FLEE_ROLL)
    {
      return true;
    }
    flags = static_cast<std::uint8_t>(flags >> 1u);

    // 6502: .TN1 LSR A / BCC TN2 / LDX FIST / CPX #40 / BCC TN2 / LDA NEWB / ORA #%00000100 /
    // STA NEWB / LSR A / LSR A -- bit 1 is "bounty hunter", and it only turns on you once your
    // legal status is over 40. The two `LSR`s put the shifted copy back in step.
    if ((flags & 1u) != 0u && screen.commander.At(Field::LegalStatus) >= BOUNTY_HUNTER_FIST)
    {
      work[36] = static_cast<std::uint8_t>(work[36] | NEWB_HOSTILE);
      flags = static_cast<std::uint8_t>(work[36] >> 2u);
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
        return RunDockingComputer(_loop, _slot);
      }

      AimAtPlanet(_loop); // 6502: .GOPL JSR SPS1 / JMP TA151
      return true;
    }
    flags = static_cast<std::uint8_t>(flags >> 1u);

    // 6502: .TN3 LSR A / BCC TN4 / LDA SSPR / BEQ TN4 / LDA INWK+32 / AND #%10000001 / STA INWK+32
    // -- bit 3 is "runs away when the station is near", so a pirate near a station keeps its AI
    // enabled and its target and drops everything else.
    if ((flags & 1u) != 0u && screen.bubble.StationPresent() != 0u)
    {
      work[32] = static_cast<std::uint8_t>(work[32] & 0x81u);
    }

    // 6502: .TN4 LDX #8 / .TAL1 LDA INWK,X / STA K3,X / DEX / BPL TAL1 -- the ship's own position
    // is the vector to us, because we are the origin.
    for (std::size_t byte = 0; byte < 9u; ++byte)
    {
      axes[byte] = work[byte];
    }

    // 6502: .TA19 JSR TAS2 / LDY #10 / JSR TAS3 / STA CNT, and then part 4.
    NormaliseAxes(axes, screen.draw, math);
    const AddSignedResult nose = DotProductWithShip(work, screen.draw, math, ORIENTATION_NOSE);
    math.cnt = nose.high;

    /*
     * ---- part 4: is it an Anaconda, is it scared, has it lost its nerve ------------------------
     *
     * 6502: LDA TYPE / CMP #MSL / BNE P%+5 / JMP TA20 -- a missile that got here (through `TN4`)
     * skips everything below and goes straight to the steering with its vector REVERSED.
     */
    if (type == SHIP_TYPE_MISSILE)
    {
      NegateVector(screen.draw); // 6502: .TA20 JSR TAS6
      SteerTowards(_loop, static_cast<std::uint8_t>(math.cnt ^ 0x80u));
      return true;
    }

    // 6502: CMP #ANA / BNE TN7 / JSR DORND / CMP #200 / BCC TN7 -- an Anaconda spawns its escort.
    bool anacondaFellThrough = false;
    if (type == SHIP_TYPE_ANACONDA)
    {
      // 6502: CMP #ANA / BNE TN7 / JSR DORND -- and the compare is what sets the carry, which for
      // an Anaconda is equality and therefore SET.
      const RngResult first = screen.rng.Next(type >= SHIP_TYPE_ANACONDA);
      if (first.value >= 200u)
      {
        // 6502: JSR DORND / LDX #WRM / CMP #100 / BCS P%+4 / LDX #SH3 / JMP TN6 -- the carry is
        // `CMP #200`'s, and reaching here means it did not borrow.
        const RngResult second = screen.rng.Next(true);
        const std::uint8_t escort = (second.value >= 100u) ? SHIP_TYPE_WORM : SHIP_TYPE_SIDEWINDER;
        (void)SpawnChildShip(screen.bubble, work, screen.rng, math, _slot, type, STATION_LAUNCH_AI, escort, screen.flight.blueprint);
        return true;
      }

      // 6502: BCC TN7 -- the roll was under 200, so the Anaconda carries on as an ordinary ship
      // and arrives at `TN7` with the carry CLEAR rather than with the type compare's.
      anacondaFellThrough = true;
    }

    // 6502: .TN7 JSR DORND / CMP #250 / BCC TA7 / JSR DORND / ORA #104 / STA INWK+29 -- six times
    // in 256 a ship rolls for no reason at all, which is most of what makes a dogfight look alive.
    {
      // 6502: .TN7 JSR DORND -- reached either from `CMP #ANA / BNE TN7`, whose carry is
      // `TYPE >= ANA`, or from the Anaconda's own `CMP #200 / BCC TN7`, whose carry is clear. The
      // second is only taken when the first compare was EQUAL, so `TYPE >= ANA` covers neither
      // path wrongly: an Anaconda that falls through arrives with the carry clear.
      const RngResult chance = screen.rng.Next(anacondaFellThrough ? false : (type >= SHIP_TYPE_ANACONDA));
      if (chance.value >= 250u)
      {
        // 6502: JSR DORND / ORA #104 -- the carry is `CMP #250`'s, set by definition here.
        const RngResult amount = screen.rng.Next(true);
        work[29] = static_cast<std::uint8_t>(amount.value | 104u);
      }
    }

    /*
     * 6502: .TA7 LDY #14 / LDA (XX0),Y / LSR A / CMP INWK+35 / BCC TA3 -- energy above half the
     * blueprint's maximum and the ship fights on. Below a QUARTER (`LSR` twice) it may run, and
     * `DORND / CMP #230` is how often.
     */
    const std::uint8_t maximumEnergy = ShipByte(static_cast<std::uint16_t>(screen.flight.blueprint + 14u));
    bool fellFromFleeTest = false;

    if (static_cast<std::uint8_t>(maximumEnergy >> 1u) >= work[35])
    {
      if (static_cast<std::uint8_t>(maximumEnergy >> 3u) >= work[35])
      {
        // 6502: JSR DORND / CMP #230 / BCC ta3 -- the carry is the `CMP INWK+35` above, and
        // reaching here means it did not borrow.
        const RngResult flee = screen.rng.Next(true);
        if (flee.value >= 230u)
        {
          // 6502: LDX TYPE / LDA E%-1,X / BPL ta3 -- bit 7 of the default `NEWB` for this type is
          // "carries an escape pod", so only a ship that HAS one bails out.
          const std::uint8_t defaults = ShipByte(static_cast<std::uint16_t>(SHIP_DEFAULT_FLAGS + type - 1u));
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
            work[36] = static_cast<std::uint8_t>(work[36] & 0xF0u);
            screen.bubble.blocks[_slot][36] = work[36];
            work[32] = 0u;

            (void)SpawnEscapePod(screen.bubble, work, screen.rng, math, _slot, type, screen.flight.blueprint);
            return true;
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
     */
    const std::uint8_t missiles = static_cast<std::uint8_t>(work[31] & 7u);
    if (missiles != 0u)
    {
      math.t = missiles;

      // 6502: .ta3 ... STA T / JSR DORND -- and `ta3` has two entrances. `BCC ta3` from either
      // energy compare arrives with the carry CLEAR; falling out of the escape-pod test arrives
      // with `CMP #230`'s, which is SET. `AND` and `STA` leave the flag alone either way.
      const RngResult chance = screen.rng.Next(fellFromFleeTest);

      // 6502: LDA ECMA / BNE TA3 -- an ECM running stops the launch, and the missile is not spent.
      if (static_cast<std::uint8_t>(chance.value & 31u) < missiles && screen.status.ecmCountdown == 0u)
      {
        --work[31]; // 6502: DEC INWK+31

        // 6502: LDA TYPE / CMP #THG / BNE TA16 / LDX #TGL / LDA INWK+32 / JMP SFS1 -- a Thargoid
        // launches a Thargon and passes ITS OWN AI byte on, which is why Thargons arrive hostile.
        if (type == SHIP_TYPE_THARGOID)
        {
          (void)SpawnChildShip(screen.bubble, work, screen.rng, math, _slot, type, work[32], SHIP_TYPE_THARGON, screen.flight.blueprint);
          return true;
        }

        // 6502: .TA16 JMP SFRMIS -- and it answers, because a full bubble means no missile.
        if (SpawnChildShip(screen.bubble, work, screen.rng, math, _slot, type, SPAWN_CHILD_AI, SHIP_TYPE_MISSILE, screen.flight.blueprint)
              .created)
        {
          ShowMessage(screen.canvas, screen.printer, screen.text, screen.extended, screen.message, MESSAGE_INCOMING_MISSILE, screen.view);
          (void)_loop.effects.PlaySound(SOUND_MISSILE, false);
        }
        return true;
      }
    }

    /*
     * ---- part 6: does its laser hit us ---------------------------------------------------------
     *
     * 6502: .TA3 LDA #0 / JSR MAS4 / AND #%11100000 / BNE TA4 -- too far away on any axis and
     * nothing can be fired.
     */
    if ((LargestShipAxis(work, 0u) & 0xE0u) == 0u)
    {
      const std::uint8_t cnt = math.cnt;

      // 6502: LDX CNT / CPX #160 / BCC TA4 -- and 160 has bit 7 set, so this is also "in front".
      if (cnt >= 160u)
      {
        // 6502: LDY #19 / LDA (XX0),Y / AND #%11111000 / BEQ TA4 -- the blueprint's laser power,
        // and the bottom three bits are the missile count rather than power.
        const std::uint8_t laser = static_cast<std::uint8_t>(ShipByte(static_cast<std::uint16_t>(screen.flight.blueprint + 19u)) & 0xF8u);
        if (laser != 0u)
        {
          // 6502: LDA INWK+31 / ORA #%01000000 / STA INWK+31 -- bit 6 is "firing", which is what
          // draws the line from its nose in part 11 of the flight loop.
          work[31] = static_cast<std::uint8_t>(work[31] | 0x40u);

          // 6502: CPX #163 / BCC TA4 -- firing is one cone and HITTING is a tighter one.
          if (cnt >= 163u)
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
            const std::uint8_t power = ShipByte(static_cast<std::uint16_t>(screen.flight.blueprint + 19u));
            const std::uint8_t damage = static_cast<std::uint8_t>(power >> 1u);
            if (!TakeDamage(screen, _loop.effects, screen.bubble.blocks[_slot], damage, (power & 1u) != 0u))
            {
              return false;
            }

            --work[28]; // 6502: DEC INWK+28 -- it slows down as it fires

            // 6502: LDA ECMA / BNE TA9-1 -- and `TA9-1` is the `RTS` one byte before `TA9`, so an
            // ECM running silences the hit and returns rather than skipping the sound (§6.125).
            if (screen.status.ecmCountdown != 0u)
            {
              return true;
            }

            (void)_loop.effects.PlaySound(SOUND_HIT_BY_LASER, false);
            (void)_loop.effects.PlaySound(SOUND_HIT_BY_LASER_2, false);
            return true;
          }
        }
      }
    }

    /*
     * ---- part 7: steer --------------------------------------------------------------------------
     *
     * 6502: .TA4 LDA INWK+7 / CMP #3 / BCS TA5 / LDA INWK+1 / ORA INWK+4 / AND #%11111110 /
     * BEQ TA15 -- a ship that is very close steers WITHOUT the reversal below, which is what stops
     * it turning away the moment it arrives.
     */
    bool reverse = true;
    if (work[7] < 3u && (static_cast<std::uint8_t>(work[1] | work[4]) & 0xFEu) == 0u)
    {
      reverse = false;
    }
    else
    {
      // 6502: .TA5 JSR DORND / ORA #%10000000 / CMP INWK+32 / BCS TA15 -- a random byte with bit 7
      // forced on, against the AI byte: the more aggressive the ship, the more often it presses in.
      // 6502: .TA5 JSR DORND -- reached from `CMP #3 / BCS TA5`, whose carry is set, or by falling
      // past the `BEQ TA15` below it, where the `AND` left the flag as the compare set it.
      const RngResult press = screen.rng.Next(work[7] >= 3u);
      if (static_cast<std::uint8_t>(press.value | 0x80u) >= work[32])
      {
        reverse = false;
      }
    }

    if (reverse)
    {
      // 6502: .TA20 JSR TAS6 / LDA CNT / EOR #%10000000 / .TA152 STA CNT -- turn the vector round
      // and flip the sign of how far off it is, which is how a ship backs away.
      NegateVector(screen.draw);
      SteerTowards(_loop, static_cast<std::uint8_t>(math.cnt ^ 0x80u));
      return true;
    }

    SteerTowards(_loop, math.cnt); // 6502: .TA15, entered with `CNT` already set
    return true;
  }

  bool RunDockingComputer(FlightLoop& _loop, std::uint8_t _slot) noexcept
  {
    FlightScreen& screen = _loop.screen;
    ShipBlock& work = screen.work;
    MathWorkspace& math = screen.math;
    K3Block& axes = _loop.axes;

    // 6502: LDA #6 / STA RAT2 / LSR A / STA RAT / LDA #29 / STA CNT2 -- and `RAT` is the six
    // shifted, not a second constant.
    screen.flight.rat2 = DOCKING_RAT2;
    screen.flight.rat = static_cast<std::uint8_t>(DOCKING_RAT2 >> 1u);
    math.cnt2 = DOCKING_CNT2;

    // 6502: LDA SSPR / BNE P%+5 / .GOPLS JMP GOPL -- no station in the bubble, so steer at the
    // planet and stop pretending to dock.
    if (screen.bubble.StationPresent() == 0u)
    {
      AimAtPlanet(_loop);
      return true;
    }

    SubtractStationAxes(screen.bubble, work, axes, math); // 6502: JSR VCSU1

    // 6502: LDA K3+2 / ORA K3+5 / ORA K3+8 / AND #%01111111 / BNE GOPLS -- any axis whose HIGH
    // byte has magnitude at all means the station is far away, and the sign is masked off because
    // a station behind you is still close.
    if ((static_cast<std::uint8_t>(axes[2] | axes[5] | axes[8]) & 0x7Fu) != 0u)
    {
      AimAtPlanet(_loop);
      return true;
    }

    /*
     * 6502: JSR TA2 / LDA Q / STA K -- and `Q` is what `NORM` left, the vector's LENGTH.
     *
     * `TA2` is the tail of `TAS2` (the shifting loop is skipped because the test above has just
     * proved the coordinates are small), and it falls into `NORM`, which divides by the length it
     * computed in `Q`. So `K` ends up holding how far away the station is, measured on the way to
     * working out which way it is.
     */
    BuildUnitVector(axes, screen.draw, math);
    const std::uint8_t distance = math.q;

    /*
     * 6502: JSR TAS2 -- and this is a SECOND normalisation, of the same `K3`, immediately after the
     * first. `TA2` skipped the shifting loop; `TAS2` runs it, so the vector `XX15` ends up holding
     * is the shifted one and not the one the length was taken from. The port did the first call and
     * not the second, and every docking approach came out on the wrong branch (§6.125).
     */
    NormaliseAxes(axes, screen.draw, math);

    // 6502: LDY #10 / JSR TAS4 / BMI PH1 / CMP #35 / BCC PH1 -- the STATION's nose against the
    // vector to it, so this asks "am I in front of the slot", and anything else goes to `PH1`.
    const AddSignedResult alongSlot = DotProductWithShip(screen.bubble.blocks[1], screen.draw, math, ORIENTATION_NOSE);

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
      const AddSignedResult ourNose = DotProductWithShip(work, screen.draw, math, ORIENTATION_NOSE);
      if (ourNose.high >= 0xA2u)
      {
        fineApproach = true;
      }
      // 6502: LDA K / CMP #157 / BCC PH2 / LDA TYPE / BMI PH3 -- close enough and it is the fine
      // approach for a NEGATIVE type, which is the player's own computer (`auton` stores 224 in
      // `TYPE`); a ship keeps turning towards the slot instead.
      else if (distance >= 157u && (screen.flight.type & 0x80u) != 0u)
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
      SubtractStationAxes(screen.bubble, work, axes, math);
      OffsetDockingPosition(screen.bubble, axes);
      OffsetDockingPosition(screen.bubble, axes);
      NormaliseAxes(axes, screen.draw, math);
      NegateVector(screen.draw);
      AimAlongNose(_loop);
      return true;
    }

    if (!fineApproach)
    {
      // 6502: .PH2 JSR TAS6 / JSR TA151, and then it FALLS INTO `PH22` rather than returning.
      NegateVector(screen.draw);
      AimAlongNose(_loop);
      HaltAndTurn(work);
      return true;
    }

    /*
     * 6502: .PH3 -- the fine approach, and the first thing it does is throw the turn rate away.
     *
     * `LDX #0 / STX RAT2 / STX INWK+30` means no pitch and no tolerance: from here the ship is
     * lined up and the corrections are made by hand below rather than by the shared steering.
     */
    screen.flight.rat2 = 0u;
    work[30] = 0u;

    // 6502: LDA TYPE / BPL PH32 -- and a NEGATIVE type is the player's own docking computer, which
    // `auton` marks by storing &E0 in `TYPE`. A ship being flown in by the AI skips all of this.
    if ((screen.flight.type & 0x80u) != 0u)
    {
      /*
       * 6502: EOR XX15 / EOR XX15+1 / ASL A / LDA #2 / ROR A / STA INWK+29.
       *
       * Three signs folded together -- the type's, the x component's and the y's -- and the `ASL`
       * pushes the result into the carry so the `ROR` can put it back on top. `ROR` of a 2 is a
       * ONE with the carry above it, so the roll is always magnitude one and all this arithmetic
       * decides is its direction.
       */
      const std::uint8_t folded = static_cast<std::uint8_t>(screen.flight.type ^ screen.draw.x1 ^ screen.draw.y1);
      work[29] = static_cast<std::uint8_t>((2u >> 1u) | ((folded & 0x80u) != 0u ? 0x80u : 0x00u));

      // 6502: LDA XX15 / ASL A / CMP #12 / BCS PH22 -- too far off sideways, so stop and turn.
      if (static_cast<std::uint8_t>(screen.draw.x1 << 1u) >= 12u)
      {
        HaltAndTurn(work);
        return true;
      }

      // 6502: LDA XX15+1 / ASL A / LDA #2 / ROR A / STA INWK+30 -- the same shape for the pitch.
      work[30] = static_cast<std::uint8_t>((2u >> 1u) | ((screen.draw.y1 & 0x80u) != 0u ? 0x80u : 0x00u));

      if (static_cast<std::uint8_t>(screen.draw.y1 << 1u) >= 12u)
      {
        HaltAndTurn(work);
        return true;
      }
    }

    // 6502: .PH32 STX INWK+29 -- and X is still the zero from `PH3`, so the roll the block above
    // may have set is thrown away again for a ship that is lined up.
    work[29] = 0u;

    // 6502: LDA INWK+22 / STA XX15 ... -- the ship's own SIDE vector into `XX15`, which is asking
    // "is the station's roof lined up with my side", the last thing that has to match to fit
    // through a slot.
    screen.draw.x1 = work[22];
    screen.draw.y1 = work[24];
    screen.draw.x2 = work[26];

    // 6502: LDY #16 / JSR TAS4 / ASL A / CMP #66 / BCS TN11.
    const AddSignedResult roll = DotProductWithShip(screen.bubble.blocks[1], screen.draw, math, ORIENTATION_ROOF);
    if (static_cast<std::uint8_t>(roll.high << 1u) >= 66u)
    {
      // 6502: .TN11 INC INWK+28 / LDA #%01111111 / STA INWK+29 / BNE TN13 -- roll as hard as the
      // byte allows and speed up, which is how a ship spins itself into line with the slot.
      ++work[28];
      work[29] = 0x7Fu;
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
    if (screen.geometry.xx2[10] != 0u)
    {
      return true;
    }

    work[36] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(work[36] << 1u) | 0x80u);
    return true;
  }

} // namespace Elite
