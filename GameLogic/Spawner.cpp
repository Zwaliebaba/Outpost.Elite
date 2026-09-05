#include "pch.h"

#include "Spawner.h"

#include "Arith.h"
#include "EliteTypes.h"
#include "PlanetDraw.h"
#include "Market.h"
#include "Spawn.h"

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
    NewShip Spawn(Bubble& _bubble, ShipBlock& _work, std::uint8_t _type, std::uint16_t& _blueprint) noexcept
    {
      return AddShip(_bubble, _work, _type, _blueprint);
    }

  } // namespace

  bool AtConstrictorSystem(const CommanderBlock& _commander) noexcept
  {
    // 6502: LDX GCNT / DEX / BNE THEX -- galaxy 2 and no other, and the `DEX` is why: galaxy 1 is
    // GCNT 0, so only GCNT 1 leaves zero behind.
    if (static_cast<std::uint8_t>(_commander.At(Field::GalaxyNumber) - 1u) != 0u)
    {
      return false; // 6502: .THEX CLC / RTS
    }

    // 6502: LDA QQ0 / CMP #144 / BNE THEX.
    if (_commander.At(Field::SystemX) != 144u)
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
    return _commander.At(Field::SystemY) == 33u;
  }

  NewShip SpawnThargoidPair(Bubble& _bubble, ShipBlock& _work, Rng& _rng, std::uint16_t& _blueprint, bool _carryIn) noexcept
  {
    // 6502: JSR Ze -- a block at a fixed distance in a random direction, and a second `DORND`
    // whose answer this routine throws away.
    static_cast<void>(SeedDebris(_work, _rng, _carryIn));

    // 6502: LDA #%11111111 / STA INWK+32 -- hostile, and the fastest AI the byte can express.
    _work[32] = 0xFFu;

    // 6502: LDA #THG / JSR NWSHP -- and the answer is discarded, because the next line is a JMP.
    static_cast<void>(Spawn(_bubble, _work, SHIP_TYPE_THARGOID, _blueprint));

    /*
     * 6502: LDA #TGL / JMP NWSHP -- a JMP and not a JSR, so `GTHG` returns the THARGON's answer.
     *
     * A bubble with one slot left gets the mothership and no escort and reports success, because
     * the carry that comes back is the second call's. Reproduced rather than tidied: the caller
     * that reads it is part 4's `fothg2`, which ignores it, so the only thing this changes is what
     * a comparison against the shipped routine sees.
     */
    return Spawn(_bubble, _work, SHIP_TYPE_THARGON, _blueprint);
  }

  void RunSpawning(Bubble& _bubble, ShipBlock& _work, Rng& _rng, CommanderBlock& _commander, const CurrentSystem& _current,
                   const FlightStatus& _status, std::uint8_t& _explosionCount, std::uint16_t& _blueprint, bool _carryIn) noexcept
  {
    // 6502: LDA MJ / BNE ytq -- nothing spawns in witchspace, because witchspace has no system to
    // spawn from. `MJP` puts the Thargoids there itself.
    if (_status.midJump != 0u)
    {
      return;
    }

    bool carry = _carryIn;

    /*
     * 6502: JSR DORND / CMP #35 / BCS MTT1 -- 35 chances in 256 of anything arriving at all.
     *
     * THE COMPARE OVERWRITES THE GENERATOR'S OWN CARRY, and the next `DORND` rotates in what the
     * compare left rather than what `DORND` returned. §6.125 found six of these in `TACTICS`; this
     * routine has nine, and the port had the first two wrong until the oracle disagreed about a
     * trader's AI byte in an empty bubble.
     */
    const RngResult roll = _rng.Next(carry);
    carry = roll.value >= TRADER_ROLL;

    bool toPart3 = carry;

    // 6502: LDA JUNK / CMP #3 / BCS MTT1 -- and junk counts the canisters and the hermits, so a
    // bubble already littered stops attracting traders. This compare sets the flag too.
    if (!toPart3)
    {
      carry = _bubble.junk >= JUNK_LIMIT;
      toPart3 = carry;
    }

    std::uint8_t pendingType = 0;
    bool spawnPending = false;

    if (!toPart3)
    {
      // 6502: JSR ZINF / LDA #38 / STA INWK+7 -- a clean block at one fixed distance.
      ClearShipBlock(_work);
      _work[7] = SPAWN_DISTANCE;

      /*
       * 6502: JSR DORND / STA INWK / STX INWK+3 / AND #%10000000 / STA INWK+2 / TXA /
       * AND #%10000000 / STA INWK+5 / ROL INWK+1 / ROL INWK+1.
       *
       * One random pair gives the x and y low bytes AND both signs, and then `INWK+1` -- the x high
       * byte -- is rotated twice through the carry the second `AND` left. Two rotations of a byte
       * that `ZINF` has just cleared put the carry in bit 1, so the x high byte is 0 or 2.
       */
      const RngResult place = _rng.Next(carry);
      _work[0] = place.value;
      _work[3] = place.previous;
      _work[2] = static_cast<std::uint8_t>(place.value & 0x80u);

      // 6502: TXA / AND #%10000000 / STA INWK+5 -- and `AND` does not touch the carry, so the flag
      // the two rotations below shift in is still the one `DORND` returned.
      _work[5] = static_cast<std::uint8_t>(place.previous & 0x80u);
      carry = place.carry;

      ShiftResult rotated = RotateLeftValue(_work[1], carry);
      _work[1] = rotated.value;
      carry = rotated.carry;
      rotated = RotateLeftValue(_work[1], carry);
      _work[1] = rotated.value;
      carry = rotated.carry;

      // 6502: JSR DORND / BVS MTT4 -- the OVERFLOW flag, which is the one branch in these four
      // parts that reads it. Set means part 1: a trader.
      const RngResult kind = _rng.Next(carry);
      carry = kind.carry;

      if (kind.overflow)
      {
        /*
         * 6502: .MTT4 -- part 1, the trader.
         *
         * `LSR A` halves the byte and pushes bit 0 into the carry; the same value becomes the AI
         * byte and the roll counter, and the carry is rotated into `INWK+31` before `AND #31 /
         * ORA #16` makes a speed between 16 and 31.
         */
        const RngResult trader = _rng.Next(carry);
        const ShiftResult halved = {static_cast<std::uint8_t>(trader.value >> 1u), (trader.value & 1u) != 0u};
        _work[32] = halved.value;
        _work[29] = halved.value;

        const ShiftResult flags = RotateLeftValue(_work[31], halved.carry);
        _work[31] = flags.value;
        carry = flags.carry;

        _work[27] = static_cast<std::uint8_t>((halved.value & 31u) | 16u);

        /*
         * 6502: JSR DORND / BMI nodo -- a NEGATIVE byte skips the escort flag entirely, so half
         * the traders fly with `NEWB` bit 4 set and half with whatever `ZINF` left.
         */
        const RngResult escort = _rng.Next(carry);
        carry = escort.carry;
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
          _work[32] = static_cast<std::uint8_t>(_work[32] | 0xC0u);
          _work[36] = 0x10u;
          a = _work[32];
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
        const AddResult type = AddWithCarry(static_cast<std::uint8_t>(a & 2u), SHIP_TYPE_COBRA_MK3, carry);
        carry = type.carry;

        if (type.value == SHIP_TYPE_HERMIT)
        {
          return; // 6502: BEQ TT100 -- unreachable on the C64 constants
        }

        pendingType = type.value;
        spawnPending = true;
      }
      else
      {
        // 6502: ORA #%01101111 / STA INWK+29 -- a hard roll, on the byte `BVS` did not take.
        _work[29] = static_cast<std::uint8_t>(kind.value | 0x6Fu);

        // 6502: LDA SSPR / BNE MTT1 -- inside the station's sphere nothing drifts in.
        if (_bubble.StationPresent() != 0u)
        {
          toPart3 = true;
        }
        else
        {
          /*
           * 6502: TXA / BCS MTT2 / AND #31 / ORA #16 / STA INWK+27 / BCC MTT3, and `.MTT2 ORA
           * #%01111111 / STA INWK+30`.
           *
           * The carry decides whether the byte becomes a speed or a pitch, and `BCC MTT3` after a
           * path that cannot have set the carry is an unconditional jump.
           */
          const std::uint8_t x = kind.previous;
          if (carry)
          {
            _work[30] = static_cast<std::uint8_t>(x | 0x7Fu);
          }
          else
          {
            _work[27] = static_cast<std::uint8_t>((x & 31u) | 16u);
          }

          // 6502: .MTT3 JSR DORND / CMP #252 / BCC thongs.
          const RngResult cargo = _rng.Next(carry);
          carry = cargo.carry;

          if (cargo.value >= HERMIT_ROLL)
          {
            // 6502: LDA #HER / STA INWK+32 / BNE whips -- and `HER` is 15, so the `BNE` is a JMP.
            _work[32] = SHIP_TYPE_HERMIT;
            pendingType = SHIP_TYPE_HERMIT;
          }
          else
          {
            /*
             * 6502: .thongs CMP #10 / AND #1 / ADC #OIL.
             *
             * The `CMP #10` sets the carry and its ANSWER IS NEVER TESTED -- it is there to feed
             * the `ADC` below, so a byte of 10 or more adds one. With `AND #1` giving 0 or 1 the
             * type is 5, 6 or 7: a canister, an alloy plate or an asteroid.
             */
            const bool ten = cargo.value >= 10u;
            const AddResult junkType = AddWithCarry(static_cast<std::uint8_t>(cargo.value & 1u), SHIP_TYPE_CANISTER, ten);
            carry = junkType.carry;
            pendingType = junkType.value;
          }
          spawnPending = true;
        }
      }
    }

    // 6502: .whips JSR NWSHP -- and then it FALLS INTO part 3 whatever the answer was.
    if (spawnPending)
    {
      static_cast<void>(Spawn(_bubble, _work, pendingType, _blueprint));
    }

    /*
     * 6502: .MTT1 LDA SSPR / BEQ P%+5 / .MLOOPS JMP MLOOP -- part 3, the police.
     *
     * `BEQ P%+5` steps over a three-byte `JMP`, so a station in range sends the pass BACK to the
     * top: no police spawn inside the safe zone, and parts 5 and 6 do not run either.
     */
    if (_bubble.StationPresent() != 0u)
    {
      return;
    }

    /*
     * 6502: JSR BAD / ASL A / LDX MANY+COPS / BEQ P%+5 / ORA FIST / STA T.
     *
     * What the hold is worth in trouble, doubled, and the legal status ORed in ONLY IF there is
     * already a Viper about -- `BEQ P%+5` skips the two-byte `ORA` and the two-byte `STA`... no:
     * it skips `ORA FIST` (2 bytes) and lands on `STA T` (2 bytes), because P%+5 counts from the
     * branch. So a clean bubble stores the doubled cargo alone.
     */
    const std::uint8_t penalty = ContrabandPenalty(_commander);
    const ShiftResult doubled = {static_cast<std::uint8_t>(penalty << 1u), (penalty & 0x80u) != 0u};
    carry = doubled.carry; // 6502: ASL A -- and nothing between here and `Ze` touches the flag

    std::uint8_t threshold = doubled.value;
    if (_bubble.counts[SHIP_TYPE_VIPER] != 0u)
    {
      threshold = static_cast<std::uint8_t>(doubled.value | _commander.At(Field::LegalStatus));
    }

    // 6502: JSR Ze / CMP #136 / BEQ fothg -- one byte in 256 goes to the Cougar path.
    RngResult ze = SeedDebris(_work, _rng, carry);
    carry = ze.value == COUGAR_BYTE; // 6502: CMP #136

    if (ze.value == COUGAR_BYTE)
    {
      /*
       * 6502: .fothg LDA K%+6 / AND #%00111110 / BNE fothg2 -- byte 6 of the PLANET's block, which
       * is the low byte of its z coordinate, masked to five bits. Non-zero and this is a Thargoid
       * after all; zero and it is the Cougar, which is the rarest thing in the game.
       */
      if ((_bubble.blocks[0][6] & 0x3Eu) != 0u)
      {
        static_cast<void>(SpawnThargoidPair(_bubble, _work, _rng, _blueprint, carry)); // 6502: fothg2
        return;                                                                        // 6502: .mj1 JMP MLOOP
      }

      // 6502: LDA #18 / STA INWK+27 / LDA #%01111001 / STA INWK+32 / LDA #COU / BNE focoug.
      _work[27] = 18u;
      _work[32] = 0x79u;
      static_cast<void>(Spawn(_bubble, _work, SHIP_TYPE_COUGAR, _blueprint));
      return;
    }

    /*
     * 6502: CMP T / BCS P%+7 / LDA #COPS / JSR NWSHP.
     *
     * `P%+7` counts from the branch: two bytes of `BCS`, then `LDA #COPS` (2) and `JSR NWSHP` (3)
     * make five, so the branch skips BOTH. A roll at or above the threshold means no policeman.
     */
    carry = ze.value >= threshold; // 6502: CMP T
    if (!carry)
    {
      // 6502: LDA #COPS / JSR NWSHP -- and `NWSHP` returns its own carry, which is the flag any
      // later `DORND` on this path rotates in.
      carry = Spawn(_bubble, _work, SHIP_TYPE_VIPER, _blueprint).created;
    }

    // 6502: LDA MANY+COPS / BNE MLOOPS -- and this reads the count AFTER the spawn, so one Viper
    // in the bubble ends the pass whether it arrived just now or was already there.
    if (_bubble.counts[SHIP_TYPE_VIPER] != 0u)
    {
      return;
    }

    /*
     * 6502: part 4. .DEC EV / BPL MLOOPS / INC EV -- the encounter counter, which is a rate limit:
     * it counts down and only a pass that takes it negative gets any further, and then it is put
     * back so the next pass tries again.
     */
    --_explosionCount;
    if ((_explosionCount & 0x80u) == 0u)
    {
      return;
    }
    ++_explosionCount;

    // 6502: LDA TP / AND #%00001100 / CMP #%00001000 / BNE nopl -- mission 1 at stage 2, which is
    // when the Thargoids start hunting you.
    const std::uint8_t stage = static_cast<std::uint8_t>(_commander.At(Field::MissionProgress) & 0x0Cu);
    carry = stage >= 0x08u; // 6502: CMP #%00001000, and the flag outlives the BNE

    if (stage == 0x08u)
    {
      // 6502: JSR DORND / CMP #200 / BCC nopl / .fothg2 JSR GTHG.
      const RngResult thargoid = _rng.Next(carry);
      carry = thargoid.value >= THARGOID_ROLL; // 6502: CMP #200
      if (carry)
      {
        static_cast<void>(SpawnThargoidPair(_bubble, _work, _rng, _blueprint, carry));
        return; // 6502: .mj1 JMP MLOOP
      }
    }

    /*
     * 6502: .nopl JSR DORND / LDY gov / BEQ LABEL_2 / CMP #90 / BCS MLOOPS / AND #7 / CMP gov /
     * BCC MLOOPS.
     *
     * Anarchy -- government 0 -- always spawns. Everywhere else needs a byte under 90 AND its low
     * three bits to reach the government's own number, so a corporate state is nearly safe.
     */
    const RngResult law = _rng.Next(carry);
    carry = law.carry;

    if (_current.government != 0u)
    {
      carry = law.value >= GOVERNMENT_ROLL; // 6502: CMP #90
      if (carry)
      {
        return;
      }
      // 6502: AND #7 / CMP gov / BCC MLOOPS -- and this compare is the one `Ze` below rotates in.
      carry = static_cast<std::uint8_t>(law.value & 7u) >= _current.government;
      if (!carry)
      {
        return;
      }
    }

    // 6502: .LABEL_2 JSR Ze / CMP #100 / BCS mt1 -- above 100 it is a pack of pirates.
    ze = SeedDebris(_work, _rng, carry);
    carry = ze.value >= PIRATE_ROLL; // 6502: CMP #100

    if (carry)
    {
      /*
       * 6502: .mt1 AND #3 / STA EV / STA XX13 / .mt3 ... DEC XX13 / BPL mt3.
       *
       * The low two bits become BOTH the encounter counter and the loop count, so a pass that
       * spawns four pirates also sets the longest cooldown. One to four of them, and each is
       * `DORND AND DORND AND 7` -- two rolls ANDed, so the low types are far more likely.
       */
      const std::uint8_t count = static_cast<std::uint8_t>(ze.value & 3u);
      _explosionCount = count;

      for (int remaining = static_cast<int>(count); remaining >= 0; --remaining)
      {
        const RngResult first = _rng.Next(carry);
        const RngResult second = _rng.Next(first.carry);
        carry = second.carry;

        const std::uint8_t masked = static_cast<std::uint8_t>(static_cast<std::uint8_t>(second.value & first.value) & 7u);
        const AddResult pack = AddWithCarry(masked, SHIP_TYPE_PACK_FIRST, carry);
        carry = pack.carry;

        // 6502: JSR NWSHP / DEC XX13 / BPL mt3 -- so the NEXT pass's first `DORND` rotates in the
        // carry `NWSHP` returned, not the one the `ADC` above left.
        carry = Spawn(_bubble, _work, pack.value, _blueprint).created;
      }

      // 6502: the loop's fall-through IS part 5.
      return;
    }

    /*
     * 6502: INC EV / AND #3 / ADC #CYL2 / TAY / JSR THERE / BCC NOCON -- a lone bounty hunter, and
     * `THERE` is asked whether this is the Constrictor's system.
     */
    ++_explosionCount;
    const AddResult hunter = AddWithCarry(static_cast<std::uint8_t>(ze.value & 3u), SHIP_TYPE_COBRA_PIRATE, carry);
    carry = hunter.carry;
    const std::uint8_t y = hunter.value;

    // 6502: JSR THERE -- and its answer IS the carry, so the flag survives into what follows.
    carry = AtConstrictorSystem(_commander);

    bool constrictor = false;
    if (carry)
    {
      /*
       * 6502: LDA #%11111001 / STA INWK+32 / LDA TP / AND #%00000011 / LSR A / BCC NOCON /
       * ORA MANY+CON / BEQ YESCON.
       *
       * The AI byte is set BEFORE the mission test, so a hunter in that system is hostile whether
       * or not it turns out to be the Constrictor. Then mission 1 has to be at stage 1 -- the
       * `LSR` puts bit 0 in the carry -- and the Constrictor must not already be in the bubble.
       */
      _work[32] = 0xF9u;

      const std::uint8_t stage = static_cast<std::uint8_t>(_commander.At(Field::MissionProgress) & 3u);
      const ShiftResult shifted = {static_cast<std::uint8_t>(stage >> 1u), (stage & 1u) != 0u};

      // 6502: LSR A -- and `ORA`, `BEQ`, `LDA` and `STA` all leave the flag alone, so this is what
      // the `DORND` down in `NOCON` rotates in.
      carry = shifted.carry;

      if (carry)
      {
        constrictor = static_cast<std::uint8_t>(shifted.value | _bubble.counts[SHIP_TYPE_CONSTRICTOR]) == 0u;
      }
    }

    std::uint8_t hunterType = 0;
    if (constrictor)
    {
      hunterType = SHIP_TYPE_CONSTRICTOR; // 6502: .YESCON LDA #CON
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
      _work[36] = 0x04u;
      const RngResult ai = _rng.Next(carry);
      const ShiftResult rolled = RotateLeftValue(ai.value, ai.value >= THARGOID_ROLL);
      carry = rolled.carry;
      _work[32] = static_cast<std::uint8_t>(rolled.value | 0xC0u);
      hunterType = y;
    }

    // 6502: .focoug JSR NWSHP / .mj1 JMP MLOOP.
    static_cast<void>(Spawn(_bubble, _work, hunterType, _blueprint));
    return;
  }

} // namespace Elite
