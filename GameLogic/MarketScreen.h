// GameLogic/MarketScreen.h
#pragma once

#include "Docked.h"
#include "Market.h"
#include "MarketView.h"
#include "Task.h"

#include <array>
#include <cstdint>
#include <optional>

namespace Elite
{

class GameState;
class Host;

/// The market and the hold as D01 reads them: values, from the game's state (MarketGoodsOf) or from a test.
struct MarketGoods
{
  std::array<CommodityPrices, COMMODITY_KINDS> prices{};
  std::array<std::uint8_t, COMMODITY_KINDS> onSale{};
  std::array<std::uint8_t, COMMODITY_KINDS> held{};
  std::uint8_t usedTonnes = 0;
  std::uint8_t legalStatus = 0;
  std::uint32_t creditsTenths = 0;
  bool largeBay = false; ///< the cargo bay extension is fitted
};

/// D01's own state, which the 1987 screens have no counterpart for: the row, the quantity, and the strip the last action left.
struct MarketScreen
{
  std::uint8_t selected = 0;
  std::uint8_t quantity = 1;
  MarketMessage message;
};

/// What a key or a click asks D01 to do.
enum class MarketAction : std::uint8_t
{
  Up,     ///< the row above
  Down,   ///< the row below
  Select, ///< the row a click names
  Less,   ///< one fewer, down to 1
  More,   ///< one more, up to MOST_QUANTITY
  Most,   ///< the largest trade allowed (D93): what can be bought, or failing that what is held
  Buy,
  Sell,
};

struct MarketInput
{
  MarketAction action = MarketAction::Up;
  std::uint8_t row = 0; ///< for Select
};

/// What an action did: the screen after it, and the lot a trade left, for the game to store.
struct MarketStep
{
  MarketScreen screen;
  std::optional<CargoLot> traded;
};

/// Commodity _commodity's lot in _goods, as the trade rules (ADR-043) read it.
[[nodiscard]] CargoLot LotIn(const MarketGoods& _goods, std::uint8_t _commodity) noexcept;

/// The most of commodity _commodity that can be bought (D93): the least of the market's stock, the room the hold has for it and
/// what the credits pay for, and then the largest amount up to it the rules allow; 0 when none can.
[[nodiscard]] std::uint8_t MostToBuy(const MarketGoods& _goods, std::uint8_t _commodity) noexcept;

/// The most of commodity _commodity that can be sold: what the hold has of it.
[[nodiscard]] std::uint8_t MostToSell(const MarketGoods& _goods, std::uint8_t _commodity) noexcept;

/// The strip for _refusal of a trade of _amount of _lot's commodity at _priceTenths each, with its figures.
[[nodiscard]] MarketMessage RefusalMessage(CargoRefusal _refusal, const CargoLot& _lot, bool _largeBay, std::uint8_t _amount,
                                           std::uint16_t _priceTenths) noexcept;

/// What _input does to _screen over _goods. A trade that happens leaves its lot in MarketStep::traded and its strip on the screen;
/// one that is refused leaves the refusal's strip; anything else clears the strip, and a new row starts at a quantity of 1.
[[nodiscard]] MarketStep StepMarket(const MarketScreen& _screen, const MarketInput& _input, const MarketGoods& _goods) noexcept;

/// The action of the key with scan code _scanCode on D01: ↑ and ↓, + and − on the main keys and the keypad, B and S. Nothing for
/// any other key.
[[nodiscard]] std::optional<MarketInput> MarketKey(std::uint8_t _scanCode) noexcept;

/// The action of a click on _click's control.
[[nodiscard]] MarketInput MarketClickInput(const MarketClick& _click) noexcept;

/// What D01 shows of _screen over _goods, unnumbered: the shell's MarketView.
[[nodiscard]] MarketView MarketViewOf(const MarketScreen& _screen, const MarketGoods& _goods);

/// The market and the hold in _state, at the prices ComputeMarketPrices left in screenPrices.
[[nodiscard]] MarketGoods MarketGoodsOf(const GameState& _state);

/// D01, the market (D82, D92), which F2, F3 and F8 open while docked in place of 1987's Sell Cargo, Buy Cargo and Market Prices.
/// The prices are computed and, the first time after an arrival, the stock rolled, as Buy Cargo did; then each tick a key, and
/// the clicks the shell sent, act on the screen (StepMarket), a trade is stored, and the view is published (Host::ShowMarket).
/// Returns the screen key that closes it: Esc, or an F-key other than F2, F3 and F8.
[[nodiscard]] Task<ScreenKey> ShowMarketScreen(GameState& _state, Host& _host);

} // namespace Elite
