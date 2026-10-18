// GameLogic/DockedState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Docked.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the
/// original data segment's, by their names, in their order there; the GameState holds it in GameData.
struct DockedState
{
  /// text; 17-byte rows: name, unit (t, kg, g); the 18th is Refugees
  std::uint8_t productNames[312];
  /// '0' or '4' patched into "Please accept 1000 Credits!"
  std::uint8_t refugeeRewardDigit;
  /// ship type shown on the title (1Ah in the file), F9/F10 step it through 2-29
  std::uint8_t titleShipType;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("productNames", _self.productNames);
    _visitor("refugeeRewardDigit", _self.refugeeRewardDigit);
    _visitor("titleShipType", _self.titleShipType);
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

static_assert(std::is_trivially_copyable_v<DockedState> && std::is_standard_layout_v<DockedState>);

} // namespace Elite
