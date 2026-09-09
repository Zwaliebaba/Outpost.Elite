#include "pch.h"

#include "Spawn.h"

#include "ShipMove.h"

#include "EliteTypes.h"
#include "ShipBlueprint.h"
#include "Messages.h"
#include "Ports.h"
#include "Universe.h"

namespace Elite
{

  namespace
  {
    /// Byte 5 of the blueprint -- how many bytes of line heap a type needs. Through the table
    /// as it stands rather than the assembled one, because the station's entry is written (`NWSPS`).
    [[nodiscard]] std::uint8_t HeapSizeFor(const Bubble& _bubble, ShipType _type) noexcept
    {
      const Blueprint* blueprint = BlueprintFor(_bubble, _type);
      return (blueprint == nullptr) ? std::uint8_t{0} : blueprint->heapBytes;
    }
  } // namespace

  void KillShip(Universe& _universe, Ports& _ports, std::uint8_t _slot) noexcept
  {
    // The slot compared against `MSTG` -- the player's missile was chasing this one, so it
    // is unlocked and the player told.
    if (_universe.bubble.missileTarget == _slot)
    {
      AbortMissileLock(_universe, _universe.commander.missiles, MISSILE_READY); // ABORT with GREEN2 -- the indicator's own green
      ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, 200,
                  _universe.view, &_universe.backdrop); // MESS with token 200
    }

    const ShipType type = TypeOf(_universe.bubble.slots[_slot]);

    if (type == ShipType::Station)
    {
      /*
       * The space station is the one death that changes the system rather than the
       * bubble. Nothing shuffles: the slot list is cut back, the station indicator goes out, and
       * a SUN is created in its place, because a system without a station still has to have
       * something for the player to fly towards.
       */
      ClearShip(_universe.work);
      ClearSunHeap(_universe.heaps);

      // Both stores take the zero `FLFLLS` left in the accumulator -- and `SSPR` is
      // `MANY+SST`, so the second is what takes the station out of the type counts (§6.58).
      _universe.bubble.slots[1] = 0;
      _universe.bubble.Count(ShipType::Station) = 0;
      ToggleStationIndicator(_universe.canvas, &_universe.backdrop);

      // NWSHP with a sun -- and `XX0` is passed rather than kept locally even though this
      // call cannot reach the store: the type is negative, so the branch jumps past it. Passing it
      // is what stops the next caller of this path from inheriting the bug `NWSPS` exposed.
      _universe.work.y.sgn = 6;
      (void)AddShip(_universe.bubble, _universe.work, ShipType::Sun, _universe.flight.blueprint);
      return;
    }

    // Killing the Constrictor is the end of the first mission, and it is scored as 256
    // kills rather than one.
    if (type == ShipType::Constrictor)
    {
      _universe.commander.missionProgress = static_cast<std::uint8_t>(_universe.commander.missionProgress | 0x02u);
      ++_universe.commander.kills.hi;
    }

    // The rock hermit counts as junk despite its type, which is the same extra
    // comparison `NWSHP` has.
    if (IsJunk(type))
    {
      --_universe.bubble.junk;
    }

    // The type's own count steps down.
    if (Byte(type) < _universe.bubble.counts.size())
    {
      --_universe.bubble.counts[Byte(type)];
    }

    /*
     * The dead ship's heap pointer plus its own size, as a sixteen-bit addition into `P`.
     *
     * `P(1 0)` starts at the TOP of the dead ship's heap block -- its own pointer plus its own
     * size -- and comes down by each surviving ship's size in turn. So it is always pointing at
     * where the next ship's heap belongs, and when the walk ends it is the new `SLSP`.
     */
    // The top of the dead ship's run, which is a sixteen-bit addition in two bytes there
    // and one here.
    HeapOffset top = _universe.bubble.blocks[_slot].heap.Byte(HeapSizeFor(_universe.bubble, type));

    // Every slot above the dead one comes down by one, and its heap with it.
    for (std::size_t into = _slot; into + 1u < _universe.bubble.slots.size(); ++into)
    {
      const ShipType moved = TypeOf(_universe.bubble.slots[into + 1u]);
      _universe.bubble.slots[into] = Byte(moved);
      if (moved == ShipType::None)
      {
        break; // The end of the list
      }

      const std::uint8_t size = HeapSizeFor(_universe.bubble, moved);
      top = top.Back(size);

      /*
       * The block moves down a slot, and bytes 33 and 34 take the NEW heap address rather than
       * being copied -- the original interleaves the two, reading the old pointer into `K` in the
       * same breath as writing the new one, because it needs the old one to copy from.
       */
      const Ship source = _universe.bubble.blocks[into + 1u];
      const HeapOffset was = source.heap;

      Ship& destination = _universe.bubble.blocks[into];
      destination = source;
      destination.heap = top;

      // The copy runs DOWNWARDS in index, which is what makes an overlapping move
      // safe when the destination is below the source.
      for (std::uint8_t byte = size; byte-- > 0u;)
      {
        _universe.heap.Write(top.Byte(byte), _universe.heap.Read(was.Byte(byte)));
        if (byte == 0u)
        {
          break;
        }
      }
    }

    /*
     * KS2 -- and every missile in the bubble has to be renumbered.
     *
     * A locked missile keeps its target in `INWK+32` as `%1ttttttt` with the slot shifted up one,
     * so the comparison is against the slot the target used to be in. Below the dead one, nothing
     * changes; above it, one is taken off; and a missile chasing the dead ship itself has its AI
     * byte cleared entirely, which is what stops it hunting a slot that now holds someone else.
     */
    for (std::size_t slot = 0; slot < _universe.bubble.slots.size(); ++slot)
    {
      const ShipType moved = TypeOf(_universe.bubble.slots[slot]);
      if (moved == ShipType::None)
      {
        break;
      }
      if (moved != ShipType::Missile)
      {
        continue;
      }

      const std::uint8_t ai = _universe.bubble.blocks[slot].ai;
      if (!Has(ai, AiBit::Active))
      {
        continue; // Not locked on anything
      }

      const std::uint8_t target = MissileTargetOf(ai);
      if (target < _slot)
      {
        continue; // KSL4, from below the dead slot
      }
      if (target == _slot)
      {
        _universe.bubble.blocks[slot].ai = 0;
        continue;
      }

      // One off the target, doubled, with the top bit back on -- and the subtraction runs
      // on the carry the comparison left SET.
      _universe.bubble.blocks[slot].ai = MissileAiFor(static_cast<std::uint8_t>(target - 1u));
    }

    // KS3 -- and the heap's bottom is wherever the walk left `P`.
    _universe.bubble.heapBottom = top;
  }

  NewShip AddPlanetOrSun(Universe& _universe, Ports& _ports) noexcept
  {
    // The missile indicators reset, then 127 into both turn counters.
    ResetMissileIndicators(_universe.canvas, _universe.commander.missiles, &_universe.backdrop);
    _universe.work.rollCounter = 127;
    _universe.work.pitchCounter = 127;

    /*
     * One bit of `tek` ORed with 128 and handed straight to `NWSHP`.
     *
     * One bit of the system's tech level becomes one bit of the ship type, so a planet is 128 or
     * 130 -- and that is the whole of how Elite decides whether a world gets meridians or a
     * crater. There is no per-system flag for it; the look of a planet is a side effect of how
     * advanced it is.
     */
    const ShipType type = TypeOf(static_cast<std::uint8_t>((_universe.current.techLevel & 0x02u) | 0x80u));
    return AddShip(_universe.bubble, _universe.work, type, _universe.flight.blueprint);
  }

  NewShip AddStation(Universe& _universe, Ports& _ports) noexcept
  {
    ToggleStationIndicator(_universe.canvas, &_universe.backdrop);

    // The AI byte: hostile, and AI enabled.
    _universe.work.ai = Mask(AiBit::Active, AiBit::HasEcm);

    _universe.work.pitchCounter = 0u;   // Zero into the pitch counter
    _universe.work.traits = 0u;           // NEWB, which `NWSHP` ORs into rather than sets
    _universe.bubble.slots[1] = 0u;     // FRIN+1 -- and slot 1 is the SUN's
    _universe.work.rollCounter = 0xFFu; // The roll counter, at maximum

    /*
     * NwS1 three times, from index 10.
     *
     * `NwS1` flips the top bit of one byte and steps the index on by two, so the three calls
     * reach 10, 12 and 14 -- the high bytes of the nose vector's three components. Flipping bit 7
     * of each negates the vector, which turns the station to face the way you have just come.
     */
    auto& nose = _universe.work.nose;
    nose.x.hi = static_cast<std::uint8_t>(nose.x.hi ^ 0x80u);
    nose.y.hi = static_cast<std::uint8_t>(nose.y.hi ^ 0x80u);
    nose.z.hi = static_cast<std::uint8_t>(nose.z.hi ^ 0x80u);

    /*
     * The Coriolis's address stored unconditionally, then the Dodo's over it above tech 10.
     *
     * `spasto` is the Coriolis's address, which `BEGIN` copied out of this same table at boot --
     * so on the port's immutable region it is simply the table's own entry. The store happens
     * UNCONDITIONALLY and is then overwritten, which matters: a station created in a low-tech
     * system after one created in a high-tech system goes back to being a Coriolis, and a port
     * that only wrote on the Dodo branch would leave the Dodo behind for ever.
     */
    _universe.bubble.stationType = ShipType::Station;
    if (_universe.current.techLevel >= STATION_DODO_TECH_LEVEL)
    {
      _universe.bubble.stationType = ShipType::Dodo;
    }

    // The sun's heap address, which the slot above has just been emptied of. `NWSHP` skips
    // its own allocation for a station, so this is the pointer the block keeps.
    _universe.work.heap = HeapOffset::FromAddress(SUN_HEAP_ADDRESS);

    return AddShip(_universe.bubble, _universe.work, ShipType::Station,
                   _universe.flight.blueprint); // The station's type, and no return -- it falls in
  }

  void BuildSystem(Universe& _universe, Ports& _ports, bool _carryIn) noexcept
  {
    // Only the LOW byte is tested, so a swarm whose count has reached a multiple of 256
    // stops breeding until it moves off one.
    if (_universe.commander.tribbles.lo != 0u)
    {
      /*
       * Two cargo bays zeroed -- the Trumbles eat the food and the narcotics, and
       * only those two.
       */
      _universe.commander.cargoHold[0] = 0;
      _universe.commander.cargoHold[6u] = 0;

      /*
       * Four random bits added to the low byte, a floor forced on, and the pair doubled.
       *
       * A population model in nine instructions. The addition runs on `DORND`'s exit carry, the
       * forced floor stops a pair dying out, and the doubling grows the swarm every jump -- so what
       * starts as two Trumbles fills the hold in about six. The guard on the high byte undoes its
       * shift when it would go negative, which is the only thing bounding it.
       */
      // The entry carry is the caller's: the population test and the two cargo stores above touch
      // no flag between `SOLAR`'s first instruction and this call.
      const RngResult roll = _universe.rng.Next(_carryIn);
      const AddResult grown = AddWithCarry(static_cast<std::uint8_t>(roll.value & 0x0Fu), _universe.commander.tribbles.lo, roll.carry);
      const ShiftResult doubled = RotateLeftValue(static_cast<std::uint8_t>(grown.value | 0x04u), grown.carry);
      _universe.commander.tribbles.lo = doubled.value;

      const ShiftResult high = RotateLeftValue(_universe.commander.tribbles.hi, doubled.carry);
      if ((high.value & 0x80u) == 0u)
      {
        _universe.commander.tribbles.hi = high.value;
      }
      else
      {
        _universe.commander.tribbles.hi = RotateRight(high.value, high.carry).value;
      }
    }

    /*
     * The bounty byte shifted right. Half of whatever the player is wanted for
     * is forgiven at every jump, which is why a fugitive can fly himself clean given enough
     * hyperspace fuel.
     *
     * And the bit it shifts out is not discarded. `ZINF` touches no flag, so the addition below
     * runs on it: **the planet's distance from the player depends on whether their bounty was
     * odd** (§6.58). Nobody designed that; it is what happens when a routine is written straight
     * through without a `CLC`, and it is in every copy of the game ever sold.
     */
    const std::uint8_t bounty = _universe.commander.legalStatus;
    const bool odd = (bounty & 0x01u) != 0u;
    _universe.commander.legalStatus = static_cast<std::uint8_t>(bounty >> 1);

    /*
     * ZINF, then the planet's position from the system's own seed bytes.
     *
     * `QQ15+1 AND 3 + 3` is a distance between three and six, and the ROR of it into `INWK+2` and
     * `INWK+5` puts the planet off to one side by half of that -- so every system's planet sits in
     * a different place, generated rather than stored, like everything else about a system.
     */
    ClearShip(_universe.work);

    const AddResult distance = AddWithCarry(static_cast<std::uint8_t>(_universe.current.seeds.bytes[1] & 0x03u), 3u, odd);
    _universe.work.z.sgn = distance.value;
    const std::uint8_t offset = RotateRight(distance.value, distance.carry).value;
    _universe.work.x.sgn = offset;
    _universe.work.y.sgn = offset;

    (void)AddPlanetOrSun(_universe, _ports);

    // The sun, from two more seed bytes, and its type is 129 rather than 128 -- the bottom
    // bit is what `PLANET` tests to send it to `SUN` instead of `PL9`.
    _universe.work.z.sgn = static_cast<std::uint8_t>((_universe.current.seeds.bytes[3] & 0x07u) | 0x81u);
    const std::uint8_t across = static_cast<std::uint8_t>(_universe.current.seeds.bytes[5] & 0x03u);
    _universe.work.x.sgn = across;
    _universe.work.x.hi = across;
    _universe.work.rollCounter = 0;
    _universe.work.pitchCounter = 0;

    const NewShip sun = AddShip(_universe.bubble, _universe.work, ShipType::Sun, _universe.flight.blueprint);

    /*
     * And there is no `RTS`. `SOLAR` runs straight on into `NWSTARS`, so arriving in a
     * system fills the stardust field, takes every ship off the screen and resets both line heaps
     * as part of the same call (§6.58).
     *
     * The carry `NWSHP` returns is what `nWq`'s first `DORND` runs on -- SET when the sun was
     * created, which it always is, and clear only if the bubble had no room for it.
     */
    SeedStardustAndClearShips(_universe.canvas, _universe.dust, _universe.rng, _universe.heaps, _universe.bubble, _universe.work,
                              _universe.flight, _universe.view, sun.created, &_universe.picture);
  }

  RngResult SeedDebris(Ship& _work, Rng& _rng, bool _carryIn) noexcept
  {
    ClearShip(_work); // ZINF -- and it leaves the carry as it found it

    const RngResult first = _rng.Next(_carryIn);

    // The x sign, and the copy parked in `T1` is dead here: nothing between this and the
    // return reads it.
    _work.x.sgn = static_cast<std::uint8_t>(first.value & 0x80u);

    // The y sign comes from X, which is the PREVIOUS random byte and not this one.
    _work.y.sgn = static_cast<std::uint8_t>(first.previous & 0x80u);

    // One distance, stored into all three axes.
    _work.x.hi = DEBRIS_DISTANCE;
    _work.y.hi = DEBRIS_DISTANCE;
    _work.z.hi = DEBRIS_DISTANCE;

    // The previous random byte compared against 245, doubled with that carry, and the top
    // two bits forced on.
    const bool aggressive = first.previous >= DEBRIS_AI_THRESHOLD;
    const std::uint8_t rolled = static_cast<std::uint8_t>((first.previous << 1) | (aggressive ? 1u : 0u));
    _work.ai = With(rolled, AiBit::Active, AiBit::Hostile);

    // And no return -- it falls into `DORND2`, which clears the carry in front of `DORND`.
    // So the second byte always rotates a clear carry in, whatever the doubling above shifted out.
    return _rng.Next(false);
  }

  NewShip AddDebris(Bubble& _bubble, Ship& _work, ShipType _shipType, std::uint8_t _speed, bool _carryIn,
                    const Blueprint*& _blueprint) noexcept
  {
    _work.nose.z.hi = DEBRIS_ORIENTATION;                                    // &60 into the nose's z
    _work.side.x.hi = static_cast<std::uint8_t>(DEBRIS_ORIENTATION | 0x80u); // The same with its top bit on, into the side's x

    // The speed ROTATED rather than shifted, so the carry comes in at the bottom.
    _work.speed = static_cast<std::uint8_t>((_speed << 1) | (_carryIn ? 1u : 0u));

    return AddShip(_bubble, _work, _shipType, _blueprint); // The type into A, then NWSHP
  }

  NewShip SpawnShipAhead(Bubble& _bubble, Ship& _work, ShipType _shipType, std::uint8_t _speed, std::uint8_t _missileTarget,
                         const Blueprint*& _blueprint) noexcept
  {
    ClearShip(_work);

    // 14 is 28 shifted right, so the two distances are one constant. The shift also clears
    // the carry, which the fold below does not use.
    _work.y.lo = SPAWN_AHEAD_X;
    _work.z.lo = SPAWN_AHEAD_Z;

    _work.y.sgn = 0x80u; // Below us, so it appears in the view

    /*
     * The missile target doubled into the AI byte, with the active bit forced on.
     *
     * The doubling puts the target slot into the AI byte's aggression field and pushes `MSTG`'s
     * BIT 7 into the carry, where `fq1`'s own rotate collects it four instructions later (§6.121).
     */
    const bool carry = (_missileTarget & 0x80u) != 0u;
    _work.ai = MissileAiFor(_missileTarget);

    return AddDebris(_bubble, _work, _shipType, _speed, carry, _blueprint); // No JSR -- a fall into `fq1`
  }

  void MoveShipAlongAxis(Ship& _work, std::uint8_t _amount, std::uint8_t _axis) noexcept
  {
    // The amount doubled into `R` and its sign rotated into the accumulator, then MVT1.
    const std::uint8_t doubled = static_cast<std::uint8_t>(_amount << 1u);
    const std::uint8_t sign = static_cast<std::uint8_t>((_amount & 0x80u) != 0u ? 0x80u : 0x00u);

    AddToShipCoordinate(_work, sign, doubled, _axis, false);
  }

  NewShip SpawnChildShip(Bubble& _bubble, Ship& _work, Rng& _rng, std::uint8_t _parent, ShipType _parentType, std::uint8_t _aiFlag,
                         ShipType _shipType, const Blueprint*& _blueprint) noexcept
  {
    // The AI byte kept in `T1` and the caller's state pushed.
    // `T1` is this routine's own since M2-c-3: `FRS1` parks the byte across the copy below and
    // reads it back twenty instructions later, and nothing else touches it in between.
    const std::uint8_t aiFlag = _aiFlag;
    const Blueprint* const savedBlueprint = _blueprint;

    // The whole block swapped with the parent's, a byte at a time.
    const Ship saved = _work;
    _work = _bubble.blocks[_parent];

    // `TYPE` holds the PARENT's type, which `MVEIT` left there, and not the type being
    // created.
    if (_parentType == ShipType::Station)
    {
      _work.speed = STATION_CHILD_SPEED; // 32 into the speed

      // SFS2 three times -- out along the station's own axes,
      // so a ship leaves through the slot rather than out of the middle of the hull.
      MoveShipAlongAxis(_work, _work.nose.x.hi, 0u);
      MoveShipAlongAxis(_work, _work.nose.y.hi, 3u);
      MoveShipAlongAxis(_work, _work.nose.z.hi, 6u);
    }

    // The AI byte back, then bit 0 of the roll counter cleared by a shift each way,
    // which is what makes the new ship's roll damp rather than lock.
    _work.ai = aiFlag;
    _work.rollCounter = static_cast<std::uint8_t>(_work.rollCounter & 0xFEu);

    /*
     * Two comparisons bracketing the cargo range, plate to
     * splinter, and only that range is given a random tumble.
     */
    if (IsWreckage(_shipType))
    {
      /*
       * A random byte doubled into the pitch counter, and four bits of the previous one
       * into the speed.
       *
       * AND THE CARRY GOING IN IS SET, every time. Reaching this line means the lower comparison
       * did not borrow -- that is what the branch just tested -- and the push leaves the flag
       * alone, so the
       * generator's first rotate takes a one. The port had `false` here and the oracle disagreed on
       * the generator's own state, which is the only place a wrong carry into `DORND` shows
       * (§6.121).
       */
      const RngResult roll = _rng.Next(true);
      _work.pitchCounter = static_cast<std::uint8_t>(roll.value << 1u);
      _work.speed = static_cast<std::uint8_t>(roll.previous & 0x0Fu);

      // &FF rotated right into the roll counter, and the carry it rotates in comes from the
      // doubling above -- so the pitch counter's sign is bit 7 of the byte that set the roll.
      const bool carry = (roll.value & 0x80u) != 0u;
      _work.rollCounter = static_cast<std::uint8_t>((0xFFu >> 1u) | (carry ? 0x80u : 0x00u));
    }

    const NewShip made = AddShip(_bubble, _work, _shipType, _blueprint); // NWSHP

    // Everything pulled back off the stack and copied back.
    _work = saved;
    _blueprint = savedBlueprint;

    return made;
  }

  NewShip SpawnEscapePod(Bubble& _bubble, Ship& _work, Rng& _rng, std::uint8_t _parent, ShipType _parentType,
                         const Blueprint*& _blueprint) noexcept
  {
    // The escape pod's type and AI byte, and then straight into `SFS1`.
    return SpawnChildShip(_bubble, _work, _rng, _parent, _parentType, SPAWN_CHILD_AI, ShipType::EscapePod, _blueprint);
  }

} // namespace Elite
