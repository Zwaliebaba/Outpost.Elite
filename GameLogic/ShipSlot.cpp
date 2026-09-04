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

} // namespace Elite
