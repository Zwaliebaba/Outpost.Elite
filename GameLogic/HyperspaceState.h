// GameLogic/HyperspaceState.h
#pragma once

#include "CommanderState.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Hyperspace.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the
/// original's data segment's, by their names, in their order there; the GameState holds it in GameData.
struct HyperspaceState
{
  /// 'Hyperspaced into galaxy ', the digit GalacticJump writes, galaxyNumber + '1', and the NUL
  std::uint8_t arrivalGalaxyText[26];
  /// 3 bytes; working copy animated by DrawHyperspaceRings
  std::uint8_t hyperspaceRings[10][3];
  /// the selected system's record, latched when the countdown starts
  SystemRecord hyperspaceTarget;
  /// two ASCII digits at the end of hyperspaceCountdownMessage, written by ShowHyperspaceCountdown
  std::uint8_t hyperspaceCountdownDigits[2];

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("arrivalGalaxyText", _self.arrivalGalaxyText);
    _visitor("hyperspaceRings", _self.hyperspaceRings);
    _visitor("hyperspaceTarget", _self.hyperspaceTarget);
    _visitor("hyperspaceCountdownDigits", _self.hyperspaceCountdownDigits);
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

static_assert(std::is_trivially_copyable_v<HyperspaceState> && std::is_standard_layout_v<HyperspaceState>);

} // namespace Elite
