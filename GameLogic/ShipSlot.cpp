#include "pch.h"

#include "ShipSlot.h"

namespace Elite
{

ShipBlock* SlotBlock(Bubble& _bubble, std::uint8_t _slot) noexcept
{
  // The original has no bound here: `UNIV` is `NOSH` entries and `GINF` reads whatever the index
  // lands on. Nothing asks for a slot it has not just found free, so this is a guard against a
  // future routine being wrong rather than a behaviour the game has.
  return (_slot < MAX_SHIPS) ? &_bubble.blocks[_slot] : nullptr;
}


NewShip AddShip(Bubble& _bubble, ShipBlock& _work, std::uint8_t _shipType) noexcept
{
  // 6502: STA T / LDX #0 / .NWL1 LDA FRIN,X / BEQ NW1 / INX / CPX #NOSH / BCC NWL1.
  std::uint8_t slot = 0;
  while (slot < MAX_SHIPS && _bubble.slots[slot] != 0u)
  {
    ++slot;
  }
  if (slot >= MAX_SHIPS)
  {
    return {}; // 6502: NW3 -- CLC / RTS
  }

  // 6502: JSR GINF -- INF, which the heap check below compares against.
  const std::uint16_t block = SlotAddress(slot);

  // 6502: LDA T / BMI NW2 -- the planet and the sun have no blueprint and no heap.
  if ((_shipType & 0x80u) == 0u)
  {
    const std::uint16_t blueprint = BlueprintAddress(_shipType);
    if (blueprint == 0u)
    {
      return {}; // 6502: BEQ NW3 -- a type this build does not carry
    }

    // 6502: CPY #2*SST / BEQ NW6 -- the space station keeps no line heap of its own.
    if (_shipType != SHIP_TYPE_STATION)
    {
      const std::uint8_t heapSize = ShipByte(static_cast<std::uint16_t>(blueprint + 5u));

      // 6502: LDA SLSP / SEC / SBC T1 / STA INWK+33 / LDA SLSP+1 / SBC #0 / STA INWK+34.
      const std::uint16_t lowDifference =
        static_cast<std::uint16_t>((_bubble.heapBottom & 0xFFu) + 0x100u - heapSize);
      const std::uint8_t heapLow = static_cast<std::uint8_t>(lowDifference);
      bool carry = lowDifference >= 0x100u;

      const std::uint16_t highDifference =
        static_cast<std::uint16_t>((_bubble.heapBottom >> 8) + 0xFFu + (carry ? 1u : 0u));
      const std::uint8_t heapHigh = static_cast<std::uint8_t>(highDifference);
      carry = highDifference >= 0x100u;

      _work[SHIP_HEAP_LOW_OFFSET] = heapLow;
      _work[SHIP_HEAP_HIGH_OFFSET] = heapHigh;

      /*
       * 6502: LDA INWK+33 / SBC INF / TAY / LDA INWK+34 / SBC INF+1 / BCC NW3+1.
       *
       * NO `SEC` -- the source's is commented out -- so this runs on the carry the `SBC #0` above
       * left. See the header: reproduced rather than assumed.
       */
      const std::uint16_t lowGap =
        static_cast<std::uint16_t>(heapLow + 0xFFu + (carry ? 1u : 0u) - (block & 0xFFu));
      const std::uint8_t gapLow = static_cast<std::uint8_t>(lowGap);
      carry = lowGap >= 0x100u;

      const std::uint16_t highGap =
        static_cast<std::uint16_t>(heapHigh + 0xFFu + (carry ? 1u : 0u) - (block >> 8));
      const std::uint8_t gapHigh = static_cast<std::uint8_t>(highGap);
      carry = highGap >= 0x100u;

      if (!carry)
      {
        return {}; // 6502: BCC NW3+1 -- the heap would run below the block
      }

      // 6502: BNE NW4 / CPY #NI% / BCC NW3+1 -- within the same page, the gap must be a whole
      // block. Y still holds the LOW byte of the difference, which is what CPY compares.
      if (gapHigh == 0u && gapLow < SHIP_BLOCK_SIZE)
      {
        return {};
      }

      // 6502: NW4 -- the allocation is committed.
      _bubble.heapBottom = static_cast<std::uint16_t>(heapLow | (heapHigh << 8));
    }

    // 6502: NW6 -- LDY #14 / LDA (XX0),Y / STA INWK+35, then byte 19 masked to three bits.
    _work[SHIP_ENERGY_OFFSET] = ShipByte(static_cast<std::uint16_t>(blueprint + 14u));
    _work[SHIP_MISSILES_OFFSET] =
      static_cast<std::uint8_t>(ShipByte(static_cast<std::uint16_t>(blueprint + 19u)) & 7u);
  }

  // 6502: NW2 -- STA FRIN,X / TAX / BMI NW8. The slot takes the type, and X BECOMES the type.
  _bubble.slots[slot] = _shipType;

  if ((_shipType & 0x80u) == 0u)
  {
    /*
     * 6502: CPX #HER / BEQ gangbang / CPX #JL / BCC NW7 / CPX #JH / BCS NW7 / INC JUNK.
     *
     * The rock hermit is counted as junk even though its type is nowhere near the junk range,
     * which is what the extra comparison is for -- it looks like an asteroid until it opens fire.
     */
    if (_shipType == SHIP_TYPE_HERMIT
        || (_shipType >= JUNK_TYPE_FIRST && _shipType < JUNK_TYPE_LIMIT))
    {
      ++_bubble.junk;
    }

    // 6502: NW7 -- INC MANY,X.
    if (_shipType < _bubble.counts.size())
    {
      ++_bubble.counts[_shipType];
    }
  }

  /*
   * 6502: NW8 -- LDY T / LDA E%-1,Y / AND #&6F / ORA NEWB / STA NEWB.
   *
   * `E%` is indexed from ONE, exactly as `XX21` is, and for the same reason: type 0 is an empty
   * slot. The mask keeps bits 0-3, 5 and 6 of the type's defaults and lets the caller's own bits
   * 4 and 7 through untouched.
   *
   * The NEGATIVE types reach here too -- `NW2` falls into `NW8` -- so `E%-1,Y` is read with Y at
   * 128 or 129, well past the thirty-three entries. It lands elsewhere in the ship data region,
   * which is a defined byte rather than a fault, and reproducing it costs nothing.
   */
  const std::uint8_t defaults =
    ShipByte(static_cast<std::uint16_t>(SHIP_DEFAULT_FLAGS + _shipType - 1u));
  _work[SHIP_FLAGS_OFFSET] = static_cast<std::uint8_t>((defaults & 0x6Fu) | _work[SHIP_FLAGS_OFFSET]);

  // 6502: LDY #NI%-1 / .NWL3 LDA INWK,Y / STA (INF),Y / DEY / BPL NWL3 / SEC / RTS.
  _bubble.blocks[slot] = _work;

  return { true, slot };
}

} // namespace Elite
