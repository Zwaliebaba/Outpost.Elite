// GameLogic/AiState.h
#pragma once

#include "SpaceObject.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Ai.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members
/// are the original data segment's, by their names, in their order there; the GameState holds it in GameData.
struct AiState
{
  /// active objects of class other than 7 this frame; spawning needs < 10
  std::uint8_t activeObjectCount;
  /// active objects per behaviour class 0-7, recounted each frame by UpdateObjectsAndSpawn: class 3 the drifters ('Debris' and
  /// 'Simple'), 4 the traders, 5 the wolves, which also gate the mask and invasion spawns, and 6 the hunters
  std::uint8_t behaviorClassCounts[8];
  /// spawnGovernment x 4, set by UpdateObjectsAndSpawn
  std::uint16_t spawnLimitOffset;
  /// NPC ECM: 20 from the station or a fleeing trader, 30 from a Thargoid; counted down only by UpdateStationAi, which removes all missi...
  std::uint8_t npcEcmFrames;
  /// mask ship slot kept while its escorts are placed
  SlotIndex maskShipSlot;
  /// currentSystem.government x 8, set by UpdateObjectsAndSpawn
  std::uint16_t spawnOddsOffset;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("activeObjectCount", _self.activeObjectCount);
    _visitor("behaviorClassCounts", _self.behaviorClassCounts);
    _visitor("spawnLimitOffset", _self.spawnLimitOffset);
    _visitor("npcEcmFrames", _self.npcEcmFrames);
    _visitor("maskShipSlot", _self.maskShipSlot);
    _visitor("spawnOddsOffset", _self.spawnOddsOffset);
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

static_assert(std::is_trivially_copyable_v<AiState> && std::is_standard_layout_v<AiState>);

} // namespace Elite
