// GameLogic/MarketState.h
#pragma once

#include "Field.h"

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Market.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct MarketState
{
  MarketState() noexcept = default;
  explicit MarketState(const MarketState&) noexcept = default;

  Field<std::uint16_t> data8C10;
  Field<std::uint16_t> data8C12;
  Field<std::uint16_t> data8C14;
  /// a copy of the commander's randomState0-2, three words stepped by NextMarketRandom
  Field<std::uint16_t> marketRandomState[3];
  /// currentSystem.techLevel clamped to 9
  Field<std::uint8_t> marketTechLevel;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("data8C10", _self.data8C10);
    _visitor("data8C12", _self.data8C12);
    _visitor("data8C14", _self.data8C14);
    _visitor("marketRandomState", _self.marketRandomState);
    _visitor("marketTechLevel", _self.marketTechLevel);
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

static_assert(std::is_trivially_copyable_v<MarketState> && std::is_standard_layout_v<MarketState>);

} // namespace Elite
