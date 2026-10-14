// GameLogic/MarketState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Market.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct MarketState
{
  /// where ComputeMarketPrices leaves its walk: the economy's and the government's price factors, a row of eight words to a
  /// product, and productTradeRecords, each past the last commodity
  std::uint16_t economyFactorIndex;
  std::uint16_t governmentFactorIndex;
  std::uint16_t tradeRecordIndex;
  /// a copy of the commander's randomState0-2, three words stepped by NextMarketRandom
  std::uint16_t marketRandomState[3];
  /// currentSystem.techLevel clamped to 9
  std::uint8_t marketTechLevel;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("economyFactorIndex", _self.economyFactorIndex);
    _visitor("governmentFactorIndex", _self.governmentFactorIndex);
    _visitor("tradeRecordIndex", _self.tradeRecordIndex);
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
