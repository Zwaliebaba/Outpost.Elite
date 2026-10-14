// GameLogic/DockedState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// What Docked.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never written. Its members are the 1987
/// data segment's, by their names, in their order there; the GameState holds it in GameData.
struct DockedState
{
  /// the fuel's light years as the status screen prints them, either side of the point (FormatFuelLightYears)
  std::uint8_t fuelUnitsDigit;
  std::uint8_t fuelTenthsDigit;
  /// the five digits FormatDecimal5 writes for the fuel, of which the last two are shown (change 3c names it)
  std::uint8_t fuelDigits[5];
  /// the missiles' count as a digit, inside the status screen's missiles row
  std::uint8_t missileCountDigit;
  /// text; 17-byte rows: name, unit (t, kg, g); the 18th is Refugees
  std::uint8_t productNames[312];
  /// walks cargoHold while the trade screens draw
  std::uint16_t cargoRowPointer;
  /// 0 sell screen (F2), 1 buy screen (F3); RunCargoTradeMenu ignores the screen's own F-key (0x3C+this), B on the sell screen and S on...
  std::uint8_t tradeScreenIsBuy;
  /// '0' or '4' patched into "Please accept 1000 Credits!"
  std::uint8_t refugeeRewardDigit;
  /// ship type shown on the title (1Ah in the file), F9/F10 step it through 2-29
  std::uint8_t titleShipType;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("fuelUnitsDigit", _self.fuelUnitsDigit);
    _visitor("fuelTenthsDigit", _self.fuelTenthsDigit);
    _visitor("fuelDigits", _self.fuelDigits);
    _visitor("missileCountDigit", _self.missileCountDigit);
    _visitor("productNames", _self.productNames);
    _visitor("cargoRowPointer", _self.cargoRowPointer);
    _visitor("tradeScreenIsBuy", _self.tradeScreenIsBuy);
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
