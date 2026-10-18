// GameLogic/EquipmentState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Equipment.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the
/// original data segment's, by their names, in their order there; the GameState holds it in GameData.
struct EquipmentState
{
  /// 'cargo item...', overwritten by the scooped product's name
  std::uint8_t scoopedCargoText[14];
  /// TryScoopObject
  std::uint8_t freeCargoTonnes;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("scoopedCargoText", _self.scoopedCargoText);
    _visitor("freeCargoTonnes", _self.freeCargoTonnes);
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

static_assert(std::is_trivially_copyable_v<EquipmentState> && std::is_standard_layout_v<EquipmentState>);

} // namespace Elite
