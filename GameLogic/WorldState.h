// GameLogic/WorldState.h
#pragma once

#include "SpaceObject.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// The objects in space: the 36 ship slots (SpaceObject), and how many of them are in use. Its members are the original data segment's, by
/// their names, in their order there; the GameState holds it in GameData.
struct WorldState
{
  /// 36 (0x24) from SetUpLocalSpace: object slots then debris slots
  std::uint8_t shipSlotCount;
  /// 36 64-byte records: slot 0 sun, 1 planet (planetSlot), 2 station (stationSlot), 3-19 ships (firstShipSlot), 20-35 debris (debrisSlots)
  SpaceObject shipSlots[36];

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("shipSlotCount", _self.shipSlotCount);
    _visitor("shipSlots", _self.shipSlots);
  }

  template <typename Visitor> void Visit(Visitor& _visitor)
  {
    VisitMembers(*this, _visitor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor) const
  {
    VisitMembers(*this, _visitor);
  }
};

static_assert(std::is_trivially_copyable_v<WorldState> && std::is_standard_layout_v<WorldState>);

} // namespace Elite
