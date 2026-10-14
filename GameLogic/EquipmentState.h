// GameLogic/EquipmentState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Equipment.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the
/// 1987 data segment's, by their names, in their order there; the GameState holds it in GameData.
struct EquipmentState
{
  /// 'cargo item...', overwritten by the scooped product's name
  std::uint8_t scoopedCargoText[14];
  /// TryScoopObject
  std::uint8_t freeCargoTonnes;
  /// 0 fore, 1 aft, 2 right, 3 left
  std::uint8_t selectedLaserMount;
  /// 0 pulse, 1 beam, 2 mining, 3 military
  std::uint8_t selectedLaserType;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("scoopedCargoText", _self.scoopedCargoText);
    _visitor("freeCargoTonnes", _self.freeCargoTonnes);
    _visitor("selectedLaserMount", _self.selectedLaserMount);
    _visitor("selectedLaserType", _self.selectedLaserType);
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
