#include "pch.h"

#include "Spawn.h"

#include "ShipMove.h"

#include "EliteTypes.h"
#include "ShipBlueprint.h"

namespace Elite
{

  namespace
  {
    /// 6502: INWK+32 -- a missile's AI byte, which for a locked missile is `%1ttttttt` with the
    /// target slot in bits 1 to 6. It is not `INWK+31`, the state byte the drawing reads.
    constexpr std::uint8_t SHIP_AI_OFFSET = 32;

    /// 6502: LDY #5 / LDA (XX0),Y -- how many bytes of line heap a type needs. Through the table
    /// as it stands rather than the assembled one, because the station's entry is written (`NWSPS`).
    [[nodiscard]] std::uint8_t HeapSizeFor(const Bubble& _bubble, std::uint8_t _type) noexcept
    {
      const std::uint16_t blueprint = BlueprintFor(_bubble, _type);
      return (blueprint == 0u) ? std::uint8_t{0} : ShipByte(static_cast<std::uint16_t>(blueprint + 5u));
    }
  } // namespace

  void KillShip(Bubble& _bubble, LineHeap& _heap, PlanetSunState& _state, ShipBlock& _work, CommanderBlock& _commander,
                SpawnEffects& _effects, std::uint8_t _slot, std::uint16_t& _blueprint) noexcept
  {
    // 6502: STX XX4 / LDA MSTG / CMP XX4 / BNE KS5 -- the player's missile was chasing this one,
    // so it is unlocked and the player told.
    if (_bubble.missileTarget == _slot)
    {
      _effects.AbortMissile(SpawnEffects::MISSILE_GREEN);
      _effects.ShowMessage(200);
    }

    const std::uint8_t type = _bubble.slots[_slot];

    if (type == SHIP_TYPE_STATION)
    {
      /*
       * 6502: KS4 -- the space station is the one death that changes the system rather than the
       * bubble. Nothing shuffles: the slot list is cut back, the station indicator goes out, and
       * a SUN is created in its place, because a system without a station still has to have
       * something for the player to fly towards.
       */
      ClearShipBlock(_work);
      ClearSunHeap(_state);

      // 6502: STA FRIN+1 / STA SSPR, both with the zero `FLFLLS` left in A -- and `SSPR` is
      // `MANY+SST`, so this one store is what takes the station out of the type counts (§6.58).
      _bubble.slots[1] = 0;
      _bubble.counts[SHIP_TYPE_STATION] = 0;
      _effects.ToggleStationIndicator();

      // 6502: LDA #129 / JSR NWSHP -- and `XX0` is passed rather than kept locally even though this
      // call cannot reach the store: the type is negative, so `BMI NW2` jumps past it. Passing it
      // is what stops the next caller of this path from inheriting the bug `NWSPS` exposed.
      _work[5] = 6;
      (void)AddShip(_bubble, _work, 129, _blueprint);
      return;
    }

    // 6502: CPX #CON / BNE lll -- killing the Constrictor is the end of the first mission, and it
    // is scored as 256 kills rather than one.
    if (type == SHIP_TYPE_CONSTRICTOR)
    {
      _commander.At(Field::MissionProgress) = static_cast<std::uint8_t>(_commander.At(Field::MissionProgress) | 0x02u);
      ++_commander.bytes[static_cast<std::size_t>(Field::Kills) + 1u];
    }

    // 6502: lll -- the rock hermit counts as junk despite its type, which is the same extra
    // comparison `NWSHP` has.
    if (type == SHIP_TYPE_HERMIT || (type >= JUNK_TYPE_FIRST && type < JUNK_TYPE_LIMIT))
    {
      --_bubble.junk;
    }

    // 6502: KS7 -- DEC MANY,X.
    if (type < _bubble.counts.size())
    {
      --_bubble.counts[type];
    }

    /*
     * 6502: LDY #5 / LDA (XX0),Y / LDY #33 / CLC / ADC (INF),Y / STA P / INY / LDA (INF),Y /
     * ADC #0 / STA P+1.
     *
     * `P(1 0)` starts at the TOP of the dead ship's heap block -- its own pointer plus its own
     * size -- and comes down by each surviving ship's size in turn. So it is always pointing at
     * where the next ship's heap belongs, and when the walk ends it is the new `SLSP`.
     */
    const ShipBlock& dead = _bubble.blocks[_slot];
    const AddResult topLow = AddWithCarry(HeapSizeFor(_bubble, type), dead[SHIP_HEAP_LOW_OFFSET], false);
    const AddResult topHigh = AddWithCarry(dead[SHIP_HEAP_HIGH_OFFSET], 0u, topLow.carry);
    std::uint16_t top = static_cast<std::uint16_t>(topLow.value | (topHigh.value << 8));

    // 6502: KSL1 -- every slot above the dead one comes down by one, and its heap with it.
    for (std::size_t into = _slot; into + 1u < _bubble.slots.size(); ++into)
    {
      const std::uint8_t moved = _bubble.slots[into + 1u];
      _bubble.slots[into] = moved;
      if (moved == 0u)
      {
        break; // 6502: BNE P%+5 / JMP KS2 -- the end of the list
      }

      const std::uint8_t size = HeapSizeFor(_bubble, moved);
      top = static_cast<std::uint16_t>(top - size);

      /*
       * The block moves down a slot, and bytes 33 and 34 take the NEW heap address rather than
       * being copied -- the original interleaves the two, reading the old pointer into `K` in the
       * same breath as writing the new one, because it needs the old one to copy from.
       */
      const ShipBlock source = _bubble.blocks[into + 1u];
      const std::uint16_t was = static_cast<std::uint16_t>(source[SHIP_HEAP_LOW_OFFSET] | (source[SHIP_HEAP_HIGH_OFFSET] << 8));

      ShipBlock& destination = _bubble.blocks[into];
      destination = source;
      destination[SHIP_HEAP_LOW_OFFSET] = static_cast<std::uint8_t>(top);
      destination[SHIP_HEAP_HIGH_OFFSET] = static_cast<std::uint8_t>(top >> 8);

      // 6502: KSL3 -- LDY T / DEY / LDA (K),Y / STA (P),Y / TYA / BNE KSL3. Downwards in index,
      // which is what makes an overlapping move safe when the destination is below the source.
      for (std::uint8_t byte = size; byte-- > 0u;)
      {
        _heap.Write(static_cast<std::uint16_t>(top + byte), _heap.Read(static_cast<std::uint16_t>(was + byte)));
        if (byte == 0u)
        {
          break;
        }
      }
    }

    /*
     * 6502: KS2 -- and every missile in the bubble has to be renumbered.
     *
     * A locked missile keeps its target in `INWK+32` as `%1ttttttt` with the slot shifted up one,
     * so the comparison is against the slot the target used to be in. Below the dead one, nothing
     * changes; above it, one is taken off; and a missile chasing the dead ship itself has its AI
     * byte cleared entirely, which is what stops it hunting a slot that now holds someone else.
     */
    for (std::size_t slot = 0; slot < _bubble.slots.size(); ++slot)
    {
      const std::uint8_t moved = _bubble.slots[slot];
      if (moved == 0u)
      {
        break; // 6502: BEQ KS3
      }
      if (moved != SHIP_TYPE_MISSILE)
      {
        continue;
      }

      const std::uint8_t ai = _bubble.blocks[slot][SHIP_AI_OFFSET];
      if ((ai & 0x80u) == 0u)
      {
        continue; // 6502: BPL KSL4 -- not locked on anything
      }

      const std::uint8_t target = static_cast<std::uint8_t>((ai & 0x7Fu) >> 1);
      if (target < _slot)
      {
        continue; // 6502: BCC KSL4
      }
      if (target == _slot)
      {
        _bubble.blocks[slot][SHIP_AI_OFFSET] = 0; // 6502: KS6
        continue;
      }

      // 6502: SBC #1 / ASL A / ORA #%10000000, and the SBC runs on the carry `CMP` left SET.
      _bubble.blocks[slot][SHIP_AI_OFFSET] = static_cast<std::uint8_t>(((target - 1u) << 1) | 0x80u);
    }

    // 6502: KS3 -- and the heap's bottom is wherever the walk left `P`.
    _bubble.heapBottom = top;
  }

  NewShip AddPlanetOrSun(Bubble& _bubble, ShipBlock& _work, SpawnEffects& _effects, std::uint8_t _techLevel,
                         std::uint16_t& _blueprint) noexcept
  {
    // 6502: SOS1 -- JSR msblob / LDA #127 / STA INWK+29 / STA INWK+30.
    _effects.ResetMissileIndicators();
    _work[29] = 127;
    _work[30] = 127;

    /*
     * 6502: LDA tek / AND #%00000010 / ORA #%10000000 / JMP NWSHP.
     *
     * One bit of the system's tech level becomes one bit of the ship type, so a planet is 128 or
     * 130 -- and that is the whole of how Elite decides whether a world gets meridians or a
     * crater. There is no per-system flag for it; the look of a planet is a side effect of how
     * advanced it is.
     */
    const std::uint8_t type = static_cast<std::uint8_t>((_techLevel & 0x02u) | 0x80u);
    return AddShip(_bubble, _work, type, _blueprint);
  }

  NewShip AddStation(Bubble& _bubble, ShipBlock& _work, SpawnEffects& _effects, std::uint8_t _techLevel, std::uint16_t& _blueprint) noexcept
  {
    _effects.ToggleStationIndicator(); // 6502: JSR SPBLB

    // 6502: LDX #%10000001 / STX INWK+32 -- the AI byte: hostile, and AI enabled.
    _work[SHIP_AI_OFFSET] = 0x81u;

    _work[30] = 0u;                // 6502: LDX #0 / STX INWK+30 -- the pitch counter
    _work[SHIP_FLAGS_OFFSET] = 0u; // 6502: STX NEWB, which `NWSHP` ORs into rather than sets
    _bubble.slots[1] = 0u;         // 6502: STX FRIN+1 -- and slot 1 is the SUN's
    _work[29] = 0xFFu;             // 6502: DEX / STX INWK+29 -- the roll counter, at maximum

    /*
     * 6502: LDX #10 / JSR NwS1, three times.
     *
     * `NwS1` is `LDA INWK,X / EOR #%10000000 / STA INWK,X / INX / INX / RTS`, so the three calls
     * reach 10, 12 and 14 -- the high bytes of the nose vector's three components. Flipping bit 7
     * of each negates the vector, which turns the station to face the way you have just come.
     */
    for (std::uint8_t at = 10u; at <= 14u; at = static_cast<std::uint8_t>(at + 2u))
    {
      _work[at] = static_cast<std::uint8_t>(_work[at] ^ 0x80u);
    }

    /*
     * 6502: LDA spasto / STA XX21+2*SST-2 ... LDA tek / CMP #10 / BCC notadodo / LDA XX21+2*DOD-2.
     *
     * `spasto` is the Coriolis's address, which `BEGIN` copied out of this same table at boot --
     * so on the port's immutable region it is simply the table's own entry. The store happens
     * UNCONDITIONALLY and is then overwritten, which matters: a station created in a low-tech
     * system after one created in a high-tech system goes back to being a Coriolis, and a port
     * that only wrote on the Dodo branch would leave the Dodo behind for ever.
     */
    _bubble.stationBlueprint = BlueprintAddress(SHIP_TYPE_STATION);
    if (_techLevel >= STATION_DODO_TECH_LEVEL)
    {
      _bubble.stationBlueprint = BlueprintAddress(SHIP_TYPE_DODO);
    }

    // 6502: LDA #LO(LSO) / STA INWK+33 / LDA #HI(LSO) / STA INWK+34 -- the sun's heap, which the
    // slot above has just been emptied of. `NWSHP` skips its own allocation for a station, so this
    // is the pointer the block keeps.
    _work[SHIP_HEAP_LOW_OFFSET] = static_cast<std::uint8_t>(SUN_HEAP_ADDRESS);
    _work[SHIP_HEAP_HIGH_OFFSET] = static_cast<std::uint8_t>(SUN_HEAP_ADDRESS >> 8);

    return AddShip(_bubble, _work, SHIP_TYPE_STATION, _blueprint); // 6502: LDA #SST, and no RTS -- it falls in
  }

  void BuildSystem(Canvas& _canvas, DrawWorkspace& _draw, Stardust& _dust, PlanetSunState& _state, Bubble& _bubble, ShipBlock& _work,
                   CommanderBlock& _commander, Rng& _rng, FlightState& _flight, SpawnEffects& _effects, std::uint8_t _techLevel,
                   const std::array<std::uint8_t, 6>& _seeds, std::uint8_t _view, bool _carryIn) noexcept
  {
    const std::size_t tribble = static_cast<std::size_t>(Field::Tribbles);

    // 6502: LDA TRIBBLE / BEQ nobirths -- only the LOW byte is tested, so a swarm whose count has
    // reached a multiple of 256 stops breeding until it moves off one.
    if (_commander.bytes[tribble] != 0u)
    {
      /*
       * 6502: LDA #0 / STA QQ20 / STA QQ20+6 -- the Trumbles eat the food and the narcotics, and
       * only those two.
       */
      _commander.bytes[static_cast<std::size_t>(Field::CargoHold)] = 0;
      _commander.bytes[static_cast<std::size_t>(Field::CargoHold) + 6u] = 0;

      /*
       * 6502: JSR DORND / AND #15 / ADC TRIBBLE / ORA #4 / ROL A / STA TRIBBLE /
       * ROL TRIBBLE+1 / BPL P%+5 / ROR TRIBBLE+1.
       *
       * A population model in nine instructions. The `ADC` runs on `DORND`'s exit carry, the
       * `ORA #4` stops a pair dying out, and the `ROL` doubles the swarm every jump -- so what
       * starts as two Trumbles fills the hold in about six. The `BPL` guard undoes the high
       * byte's shift when it would go negative, which is the only thing bounding it.
       */
      // The entry carry is the caller's: `LDA TRIBBLE / BEQ / LDA #0 / STA QQ20 / STA QQ20+6`
      // touches no flag between `SOLAR`'s first instruction and this call.
      const RngResult roll = _rng.Next(_carryIn);
      const AddResult grown = AddWithCarry(static_cast<std::uint8_t>(roll.value & 0x0Fu), _commander.bytes[tribble], roll.carry);
      const ShiftResult doubled = RotateLeftValue(static_cast<std::uint8_t>(grown.value | 0x04u), grown.carry);
      _commander.bytes[tribble] = doubled.value;

      const ShiftResult high = RotateLeftValue(_commander.bytes[tribble + 1u], doubled.carry);
      if ((high.value & 0x80u) == 0u)
      {
        _commander.bytes[tribble + 1u] = high.value;
      }
      else
      {
        _commander.bytes[tribble + 1u] = RotateRight(high.value, high.carry).value;
      }
    }

    /*
     * 6502: nobirths -- LSR FIST. Half of whatever the player is wanted for is forgiven at every
     * jump, which is why a fugitive can fly himself clean given enough hyperspace fuel.
     *
     * And the bit it shifts out is not discarded. `ZINF` touches no flag, so the `ADC #3` below
     * runs on it: **the planet's distance from the player depends on whether their bounty was
     * odd** (§6.58). Nobody designed that; it is what happens when a routine is written straight
     * through without a `CLC`, and it is in every copy of the game ever sold.
     */
    const std::uint8_t bounty = _commander.At(Field::LegalStatus);
    const bool odd = (bounty & 0x01u) != 0u;
    _commander.At(Field::LegalStatus) = static_cast<std::uint8_t>(bounty >> 1);

    /*
     * 6502: JSR ZINF, then the planet's position from the system's own seed bytes.
     *
     * `QQ15+1 AND 3 + 3` is a distance between three and six, and the ROR of it into `INWK+2` and
     * `INWK+5` puts the planet off to one side by half of that -- so every system's planet sits in
     * a different place, generated rather than stored, like everything else about a system.
     */
    ClearShipBlock(_work);

    const AddResult distance = AddWithCarry(static_cast<std::uint8_t>(_seeds[1] & 0x03u), 3u, odd);
    _work[8] = distance.value;
    const std::uint8_t offset = RotateRight(distance.value, distance.carry).value;
    _work[2] = offset;
    _work[5] = offset;

    (void)AddPlanetOrSun(_bubble, _work, _effects, _techLevel, _flight.blueprint);

    // 6502: the sun, from two more seed bytes, and its type is 129 rather than 128 -- the bottom
    // bit is what `PLANET` tests to send it to `SUN` instead of `PL9`.
    _work[8] = static_cast<std::uint8_t>((_seeds[3] & 0x07u) | 0x81u);
    const std::uint8_t across = static_cast<std::uint8_t>(_seeds[5] & 0x03u);
    _work[2] = across;
    _work[1] = across;
    _work[29] = 0;
    _work[30] = 0;

    const NewShip sun = AddShip(_bubble, _work, 129, _flight.blueprint);

    /*
     * 6502: and there is no `RTS`. `SOLAR` runs straight on into `NWSTARS`, so arriving in a
     * system fills the stardust field, takes every ship off the screen and resets both line heaps
     * as part of the same call (§6.58).
     *
     * The carry `NWSHP` returns is what `nWq`'s first `DORND` runs on -- SET when the sun was
     * created, which it always is, and clear only if the bubble had no room for it.
     */
    SeedStardustAndClearShips(_canvas, _draw, _dust, _rng, _state, _bubble, _work, _flight, _view, sun.created);
  }

  RngResult SeedDebris(ShipBlock& _work, Rng& _rng, bool _carryIn) noexcept
  {
    ClearShipBlock(_work); // 6502: JSR ZINF -- and it leaves the carry as it found it

    const RngResult first = _rng.Next(_carryIn); // 6502: JSR DORND

    // 6502: STA T1 / AND #%10000000 / STA INWK+2 -- the x sign, and `T1` is dead here: nothing
    // between this and the `RTS` reads it.
    _work[2] = static_cast<std::uint8_t>(first.value & 0x80u);

    // 6502: TXA / AND #%10000000 / STA INWK+5 -- and X is the PREVIOUS random byte, not this one.
    _work[5] = static_cast<std::uint8_t>(first.previous & 0x80u);

    // 6502: LDA #25 / STA INWK+1 / STA INWK+4 / STA INWK+7 -- one distance in all three axes.
    _work[1] = DEBRIS_DISTANCE;
    _work[4] = DEBRIS_DISTANCE;
    _work[7] = DEBRIS_DISTANCE;

    // 6502: TXA / CMP #245 / ROL A / ORA #%11000000 / STA INWK+32.
    const bool aggressive = first.previous >= DEBRIS_AI_THRESHOLD;
    const std::uint8_t rolled = static_cast<std::uint8_t>((first.previous << 1) | (aggressive ? 1u : 0u));
    _work[32] = static_cast<std::uint8_t>(rolled | 0xC0u);

    // 6502: and no RTS -- it falls into `DORND2`, which is a `CLC` in front of `DORND`. So the
    // second byte always rotates a clear carry in, whatever the `ROL A` above shifted out.
    return _rng.Next(false);
  }

  NewShip AddDebris(Bubble& _bubble, ShipBlock& _work, std::uint8_t _shipType, std::uint8_t _speed, bool _carryIn,
                    std::uint16_t& _blueprint) noexcept
  {
    _work[14] = DEBRIS_ORIENTATION;                                    // 6502: LDA #&60 / STA INWK+14
    _work[22] = static_cast<std::uint8_t>(DEBRIS_ORIENTATION | 0x80u); // 6502: ORA #128 / STA INWK+22

    // 6502: LDA DELTA / ROL A / STA INWK+27 -- a ROTATE, so the carry comes in at the bottom.
    _work[27] = static_cast<std::uint8_t>((_speed << 1) | (_carryIn ? 1u : 0u));

    return AddShip(_bubble, _work, _shipType, _blueprint); // 6502: TXA / JMP NWSHP
  }

  NewShip SpawnShipAhead(Bubble& _bubble, ShipBlock& _work, std::uint8_t _shipType, std::uint8_t _speed, std::uint8_t _missileTarget,
                         std::uint16_t& _blueprint) noexcept
  {
    ClearShipBlock(_work); // 6502: JSR ZINF

    // 6502: LDA #28 / STA INWK+3 / LSR A / STA INWK+6 -- and the 14 is the 28 shifted, so the two
    // distances are one constant. `LSR` also clears the carry, which the `ORA` below does not use.
    _work[3] = SPAWN_AHEAD_X;
    _work[6] = SPAWN_AHEAD_Z;

    _work[5] = 0x80u; // 6502: LDA #%10000000 / STA INWK+5 -- below us, so it appears in the view

    /*
     * 6502: LDA MSTG / ASL A / ORA #%10000000 / STA INWK+32.
     *
     * The `ASL` doubles the target slot into the AI byte's aggression field and pushes `MSTG`'s
     * BIT 7 into the carry, where `fq1`'s `ROL A` collects it four instructions later (§6.121).
     */
    const bool carry = (_missileTarget & 0x80u) != 0u;
    _work[32] = static_cast<std::uint8_t>((_missileTarget << 1u) | 0x80u);

    return AddDebris(_bubble, _work, _shipType, _speed, carry, _blueprint); // 6502: no JSR -- a fall into `fq1`
  }

  void MoveShipAlongAxis(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _amount, std::uint8_t _axis) noexcept
  {
    // 6502: ASL A / STA R / LDA #0 / ROR A / JMP MVT1 -- R is the doubled magnitude and A the sign.
    _math.r = static_cast<std::uint8_t>(_amount << 1u);
    const std::uint8_t sign = static_cast<std::uint8_t>((_amount & 0x80u) != 0u ? 0x80u : 0x00u);

    AddToShipCoordinate(_work, _math, sign, _axis, false);
  }

  NewShip SpawnChildShip(Bubble& _bubble, ShipBlock& _work, Rng& _rng, MathWorkspace& _math, std::uint8_t _parent, std::uint8_t _parentType,
                         std::uint8_t _aiFlag, std::uint8_t _shipType, std::uint16_t& _blueprint) noexcept
  {
    // 6502: STA T1 / TXA / PHA / LDA XX0 / PHA ... -- the AI byte kept and the caller's state saved.
    _math.t1 = _aiFlag;
    const std::uint16_t savedBlueprint = _blueprint;

    // 6502: LDY #NI%-1 / .FRL2 LDA INWK,Y / STA XX3,Y / LDA (INF),Y / STA INWK,Y / DEY / BPL FRL2.
    const ShipBlock saved = _work;
    _work = _bubble.blocks[_parent];

    // 6502: LDA TYPE / CMP #SST / BNE rx -- the PARENT's type, which `MVEIT` left in `TYPE`, and
    // not the type being created.
    if (_parentType == SHIP_TYPE_STATION)
    {
      _work[27] = STATION_CHILD_SPEED; // 6502: LDA #32 / STA INWK+27

      // 6502: LDX #0 / LDA INWK+10 / JSR SFS2, and twice more -- out along the station's own axes,
      // so a ship leaves through the slot rather than out of the middle of the hull.
      MoveShipAlongAxis(_work, _math, _work[10], 0u);
      MoveShipAlongAxis(_work, _math, _work[12], 3u);
      MoveShipAlongAxis(_work, _math, _work[14], 6u);
    }

    // 6502: .rx LDA T1 / STA INWK+32 / LSR INWK+29 / ASL INWK+29 -- the AI byte, then bit 0 of the
    // roll counter cleared, which is what makes the new ship's roll damp rather than lock.
    _work[32] = _math.t1;
    _work[29] = static_cast<std::uint8_t>(_work[29] & 0xFEu);

    /*
     * 6502: TXA / CMP #SPL+1 / BCS NOIL / CMP #PLT / BCC NOIL -- the cargo range, plate to
     * splinter, and only that range is given a random tumble.
     */
    if (_shipType >= SHIP_TYPE_ALLOY_PLATE && _shipType <= SHIP_TYPE_SPLINTER)
    {
      /*
       * 6502: JSR DORND / ASL A / STA INWK+30 / TXA / AND #%00001111 / STA INWK+27.
       *
       * AND THE CARRY GOING IN IS SET, every time. Reaching this line means `CMP #PLT` did not
       * borrow -- that is what `BCC NOIL` just tested -- and `PHA` leaves the flag alone, so the
       * generator's first rotate takes a one. The port had `false` here and the oracle disagreed on
       * the generator's own state, which is the only place a wrong carry into `DORND` shows
       * (§6.121).
       */
      const RngResult roll = _rng.Next(true);
      _work[30] = static_cast<std::uint8_t>(roll.value << 1u);
      _work[27] = static_cast<std::uint8_t>(roll.previous & 0x0Fu);

      // 6502: LDA #&FF / ROR A / STA INWK+29 -- and the carry it rotates in is the `ASL A` above,
      // so the pitch counter's sign is bit 7 of the random byte that set the roll.
      const bool carry = (roll.value & 0x80u) != 0u;
      _work[29] = static_cast<std::uint8_t>((0xFFu >> 1u) | (carry ? 0x80u : 0x00u));
    }

    const NewShip made = AddShip(_bubble, _work, _shipType, _blueprint); // 6502: .NOIL JSR NWSHP

    // 6502: PLA / STA INF+1 ... / .FRL3 LDA XX3,X / STA INWK,X -- everything put back.
    _work = saved;
    _blueprint = savedBlueprint;

    return made;
  }

  NewShip SpawnEscapePod(Bubble& _bubble, ShipBlock& _work, Rng& _rng, MathWorkspace& _math, std::uint8_t _parent, std::uint8_t _parentType,
                         std::uint16_t& _blueprint) noexcept
  {
    // 6502: LDX #ESC / LDA #%11111110, and then straight into `SFS1`.
    return SpawnChildShip(_bubble, _work, _rng, _math, _parent, _parentType, SPAWN_CHILD_AI, SHIP_TYPE_ESCAPE_POD, _blueprint);
  }

} // namespace Elite
