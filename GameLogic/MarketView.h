// GameLogic/MarketView.h
#pragma once

#include "CommanderState.h"

#include <array>
#include <cstdint>
#include <string>

namespace Elite
{

/// What the message strip of D01 says after a trade or a refused one (D11's Market column): the kind, and the figures its words
/// take. The shell words it.
enum class MarketNotice : std::uint8_t
{
  None,
  Bought,           ///< amount of commodity bought for tenths
  Sold,             ///< amount of commodity sold for tenths
  NothingInStock,   ///< the market has none of commodity
  NotForSale,       ///< alien items, which the market never sells
  HoldFull,         ///< freeUnits of capacityUnits left for commodity: tonnes, or a precious commodity's kilograms or grams
  NothingHeld,      ///< none of commodity in the hold
  NotEnoughInStock, ///< amount asked for, stock in the market
  NotEnoughHeld,    ///< amount asked for, held in the hold
  NotEnoughCredits, ///< tenths needed, creditsTenths available
};

/// A message strip and its figures.
struct MarketMessage
{
  MarketNotice notice = MarketNotice::None;
  std::uint8_t commodity = 0;      ///< from 0, in the market's order
  std::uint8_t amount = 0;         ///< what was traded, or asked for
  std::uint32_t tenths = 0;        ///< what a trade paid or earned, or what it would have cost
  std::uint32_t creditsTenths = 0; ///< the credits there were
  std::uint8_t stock = 0;          ///< the market's
  std::uint8_t held = 0;           ///< the hold's
  std::uint8_t freeUnits = 0;      ///< the room left, in the commodity's unit
  std::uint8_t capacityUnits = 0;  ///< the room there is, in the commodity's unit
};

/// A control of D01 a click lands on (D83). The shell hit-tests in design units and tells the game which control, and for a row
/// which one; the game acts on it as on the key that does the same.
enum class MarketControl : std::uint8_t
{
  Row,  ///< a commodity's row: selects it
  Less, ///< − : one fewer
  More, ///< + : one more
  Most, ///< MAX: the largest trade allowed
  Buy,  ///< [B] BUY
  Sell, ///< [S] SELL
};

/// A click on a control, and for Row the commodity's row, from 0.
struct MarketClick
{
  MarketControl control = MarketControl::Row;
  std::uint8_t row = 0;
};

/// What D01 shows, for the shell (R9): plain values, nothing of the engine's and nothing of the game's state. The game publishes
/// one each time the screen changes (Host::ShowMarket).
struct MarketView
{
  std::uint64_t sequence = 0;                              ///< from 1, one more for each view published
  std::string systemName;                                  ///< the current system's name, as the game stores it
  bool dodo = false;                                       ///< the station is a Dodo, not a Coriolis
  std::uint32_t creditsTenths = 0;                         ///< the commander's credits
  std::array<std::uint16_t, COMMODITY_KINDS> buyTenths{};  ///< what the market sells each for, in tenths of a credit
  std::array<std::uint16_t, COMMODITY_KINDS> sellTenths{}; ///< what it pays for each
  std::array<std::uint8_t, COMMODITY_KINDS> stock{};       ///< the market's, in each commodity's unit
  std::array<std::uint8_t, COMMODITY_KINDS> held{};        ///< the hold's, in each commodity's unit
  std::uint8_t selected = 0;                               ///< the selected row, from 0
  std::uint8_t quantity = 1;                               ///< the quantity a trade asks for, 1 to 255
  bool buyAllowed = false;                                 ///< buying the quantity of the selected commodity would happen
  bool sellAllowed = false;                                ///< selling it would
  std::uint8_t usedTonnes = 0;                             ///< the hold's tonnes in use
  std::uint8_t bayTonnes = 0;                              ///< the hold's tonnes, 20, or 35 with the extension
  MarketMessage message;                                   ///< the strip, or MarketNotice::None for none
};

} // namespace Elite
