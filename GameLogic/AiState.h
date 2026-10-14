// GameLogic/AiState.h
#pragma once

#include "Field.h"
#include "SpaceObject.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Ai.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987 data
/// segment's, by their names, in their order there; the GameState holds it in GameData.
struct AiState
{
  AiState() noexcept = default;
  explicit AiState(const AiState&) noexcept = default;

  /// active objects of class other than 7 this frame; spawning needs < 10
  Field<std::uint8_t> activeObjectCount;
  /// behaviorClassCounts's fields, DS:79A1-79A8 in 1987, at their places there.
  struct BehaviorClassCounts
  {
    BehaviorClassCounts() noexcept = default;
    explicit BehaviorClassCounts(const BehaviorClassCounts&) noexcept = default;

    Field<std::uint8_t> gap79A1[3];
    /// behaviorClassCounts[3]: class 3 ('Debris'/'Simple' drifters)
    Field<std::uint8_t> drifterCount;
    /// behaviorClassCounts[4]: class 4 ('Trader')
    Field<std::uint8_t> traderCount;
    /// behaviorClassCounts[5]: class 5 ('Wolf'); also gates mask and invasion spawns
    Field<std::uint8_t> wolfCount;
    /// behaviorClassCounts[6]: class 6 ('Hunter')
    Field<std::uint8_t> hunterCount;
    Field<std::uint8_t> gap79A8[1];

    /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
    template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
    {
      _visitor("gap79A1", _self.gap79A1);
      _visitor("drifterCount", _self.drifterCount);
      _visitor("traderCount", _self.traderCount);
      _visitor("wolfCount", _self.wolfCount);
      _visitor("hunterCount", _self.hunterCount);
      _visitor("gap79A8", _self.gap79A8);
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
  /// byte; active objects per class 0-7, recounted each frame by UpdateObjectsAndSpawn
  BehaviorClassCounts behaviorClassCounts;
  /// spawnGovernment x 4, set by UpdateObjectsAndSpawn
  Field<std::uint16_t> spawnLimitOffset;
  /// NPC ECM: 20 from the station or a fleeing trader, 30 from a Thargoid; counted down only by UpdateStationAi, which removes all missi...
  Field<std::uint8_t> npcEcmFrames;
  /// mask ship slot kept while its escorts are placed
  Field<SlotIndex> maskShipSlot;
  /// currentGovernment x 8, set by UpdateObjectsAndSpawn
  Field<std::uint16_t> spawnOddsOffset;

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
