// GameLogic/CombatState.h
#pragma once

#include "SpaceObject.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Combat.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct CombatState
{
  /// toggled each shot of a type 0 or 2 laser: left or right pair
  std::uint8_t laserBeamSide;
  /// XORed with 2 each shot of a type 3 laser
  std::uint8_t laserBeamColorToggle;
  /// cycles through 1-3
  std::uint8_t laserBeamColor;
  /// slot of the ship whose laser hit the player
  SlotIndex playerHitBy;
  /// set to 2 or 1 by the enemy fire code; ApplyEnemyLaserHit applies it and clears it
  std::uint8_t playerHitPending;
  /// shooter's +3C; bit 7 picks the aft shield
  std::uint8_t playerHitByDepth;
  /// SpawnPlayerWreckage: the player's nose direction times 40 (ComputeDeathDebrisVector), x
  std::uint16_t wreckDriftX;
  /// as wreckDriftX, y
  std::uint16_t wreckDriftY;
  /// as wreckDriftX, z
  std::uint16_t wreckDriftZ;
  /// 1 while ExplodeObject explodes a station: 10 extra debris steps
  std::uint8_t explodingStation;
  /// set when laser kind 2 (Mining) hits type 5; fragments get slower, live 60 frames longer, and may carry m...
  std::uint8_t miningLaserOnAsteroid;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("laserBeamSide", _self.laserBeamSide);
    _visitor("laserBeamColorToggle", _self.laserBeamColorToggle);
    _visitor("laserBeamColor", _self.laserBeamColor);
    _visitor("playerHitBy", _self.playerHitBy);
    _visitor("playerHitPending", _self.playerHitPending);
    _visitor("playerHitByDepth", _self.playerHitByDepth);
    _visitor("wreckDriftX", _self.wreckDriftX);
    _visitor("wreckDriftY", _self.wreckDriftY);
    _visitor("wreckDriftZ", _self.wreckDriftZ);
    _visitor("explodingStation", _self.explodingStation);
    _visitor("miningLaserOnAsteroid", _self.miningLaserOnAsteroid);
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

static_assert(std::is_trivially_copyable_v<CombatState> && std::is_standard_layout_v<CombatState>);

} // namespace Elite
