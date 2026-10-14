// GameLogic/StartUpState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What StartUp.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the
/// original data segment's, by their names, in their order there; the GameState holds it in GameData.
struct StartUpState
{
  /// 1 when the game was started with the cheat on (CheckCheatArgument, D80), where the original read ' cheat' from the command tail; RunFlight then ignores the...
  std::uint8_t cheatEnabled;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("cheatEnabled", _self.cheatEnabled);
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

static_assert(std::is_trivially_copyable_v<StartUpState> && std::is_standard_layout_v<StartUpState>);

} // namespace Elite
