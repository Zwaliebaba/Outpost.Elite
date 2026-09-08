#include "pch.h"

#include "ShipSlot.h"

namespace Elite
{

  namespace
  {
    /// What GINF computes -- the ADDRESS of slot X's block, which is what `NWSHP` compares the
    /// heap against. The blocks are an array; this is the address the original would have used, and
    /// `TryReserveHeap` is the one place that needs it.
    [[nodiscard]] constexpr std::uint16_t SlotAddress(std::uint8_t _slot) noexcept
    {
      return static_cast<std::uint16_t>(SHIP_BLOCK_BASE + _slot * SHIP_BLOCK_SIZE);
    }
  } // namespace

  Bubble::HeapReservation Bubble::TryReserveHeap(std::uint8_t _slot, std::uint8_t _bytes) noexcept
  {
    const std::uint16_t block = SlotAddress(_slot);
    const std::uint16_t bottom = heapBottom.Address();

    // The heap's bottom less the blueprint's byte count, sixteen bits, into the block's own
    // heap pointer.
    const std::uint16_t lowDifference = static_cast<std::uint16_t>((bottom & 0xFFu) + 0x100u - _bytes);
    const std::uint8_t heapLow = static_cast<std::uint8_t>(lowDifference);
    bool carry = lowDifference >= 0x100u;

    const std::uint16_t highDifference = static_cast<std::uint16_t>((bottom >> 8) + 0xFFu + (carry ? 1u : 0u));
    const std::uint8_t heapHigh = static_cast<std::uint8_t>(highDifference);
    carry = highDifference >= 0x100u;

    HeapReservation reservation{HeapOffset::FromAddress(static_cast<std::uint16_t>(heapLow | (heapHigh << 8))), false};

    // NW3+1 -- that pointer compared against the block's address, and a borrow means the heap
    // would run below the block.
    const std::uint16_t lowGap = static_cast<std::uint16_t>(heapLow + 0xFFu + (carry ? 1u : 0u) - (block & 0xFFu));
    const std::uint8_t gapLow = static_cast<std::uint8_t>(lowGap);
    carry = lowGap >= 0x100u;

    const std::uint16_t highGap = static_cast<std::uint16_t>(heapHigh + 0xFFu + (carry ? 1u : 0u) - (block >> 8));
    const std::uint8_t gapHigh = static_cast<std::uint8_t>(highGap);
    carry = highGap >= 0x100u;

    if (!carry)
    {
      return reservation;
    }

    // Within the SAME page the gap must be a whole block, and the index still holds
    // the LOW byte of the difference, which is what the compare reads.
    if (gapHigh == 0u && gapLow < SHIP_BLOCK_SIZE)
    {
      return reservation;
    }

    // The allocation is committed.
    heapBottom = reservation.start;
    reservation.fits = true;
    return reservation;
  }

  const Blueprint* BlueprintFor(const Bubble& _bubble, ShipType _shipType) noexcept
  {
    // The table is RAM and only the station's entry is ever written into. See `Bubble`.
    return BlueprintOf((_shipType == ShipType::Station) ? _bubble.stationType : _shipType);
  }

  Ship* SlotBlock(Bubble& _bubble, std::uint8_t _slot) noexcept
  {
    // The original has no bound here: `UNIV` is `NOSH` entries and `GINF` reads whatever the index
    // lands on. Nothing asks for a slot it has not just found free, so this is a guard against a
    // future routine being wrong rather than a behaviour the game has.
    return (_slot < MAX_SHIPS) ? &_bubble.blocks[_slot] : nullptr;
  }

  NewShip AddShip(Bubble& _bubble, Ship& _work, ShipType _shipType, const Blueprint*& _blueprint) noexcept
  {
    // The slot table walked for an empty entry, stopping at `NOSH`.
    std::uint8_t slot = 0;
    while (slot < MAX_SHIPS && _bubble.slots[slot] != 0u)
    {
      ++slot;
    }
    if (slot >= MAX_SHIPS)
    {
      return {}; // The carry clear, and out
    }

    // A NEGATIVE type branches past all of this: the planet and the sun have no
    // blueprint and no heap.
    if (!IsBody(_shipType))
    {
      /*
     * The blueprint's HIGH byte read from `XX21`, tested, and stored; then the low one.
       *
       * THE HIGH BYTE IS TESTED AND STORED BEFORE THE LOW ONE IS READ, so a refused type leaves
       * `XX0+1` alone as well -- the `BEQ` is taken before the `STA`. And the store happens at all,
       * which is what makes `XX0` an output of this routine rather than a local.
       */
      const Blueprint* blueprint = BlueprintFor(_bubble, _shipType);
      if (blueprint == nullptr)
      {
        return {}; // A type this build does not carry
      }
      _blueprint = blueprint;

      // The space station keeps no line heap of its own.
      if (_shipType != ShipType::Station)
      {
      // GINF, then the heap check -- the block's heap pointer takes the new value whether or
      // not the ship is admitted, and `SLSP` moves only when it is.
        const Bubble::HeapReservation reservation = _bubble.TryReserveHeap(slot, blueprint->heapBytes);
        _work.heap = reservation.start;
        if (!reservation.fits)
        {
          return {}; // Below the block, or not a whole block clear
        }
      }

      // Byte 14 of the blueprint is the energy, and byte 19 masked to three bits is
      // the missiles.
      _work.energy = blueprint->maxEnergy;
      _work.state = MissilesOf(blueprint->weapons);
    }

    // The slot takes the type, and X BECOMES the type.
    _bubble.slots[slot] = Byte(_shipType);

    if (!IsBody(_shipType))
    {
      /*
     * The hermit compared for on its own, then the junk range's two ends.
       *
       * The rock hermit is counted as junk even though its type is nowhere near the junk range,
       * which is what the extra comparison is for -- it looks like an asteroid until it opens fire.
       */
      if (IsJunk(_shipType))
      {
        ++_bubble.junk;
      }

      // The per-type count stepped up.
      if (Byte(_shipType) < _bubble.counts.size())
      {
        ++_bubble.Count(_shipType);
      }
    }

    /*
     * The type's defaults from `E%`, masked, ORed with what the caller already had.
     *
     * `E%` is indexed from ONE, exactly as `XX21` is, and for the same reason: type 0 is an empty
     * slot. The mask keeps bits 0-3, 5 and 6 of the type's defaults and lets the caller's own bits
     * 4 and 7 through untouched.
     *
     * The NEGATIVE types reach here too -- `NW2` falls into `NW8` -- so `E%-1,Y` is read with Y at
     * 128 or 129, well past the thirty-three entries. It lands elsewhere in the ship data region,
     * which is a defined byte rather than a fault, and reproducing it costs nothing.
     */
    const std::uint8_t defaults = DefaultNewbFor(_shipType);
    _work.traits = static_cast<std::uint8_t>(Without(defaults, TraitBit::Docking, TraitBit::Remove) | _work.traits);

    // The whole workspace copied into the slot's block, counting down, and out with
    // the carry SET.
    _bubble.blocks[slot] = _work;

    return {true, slot};
  }

} // namespace Elite
