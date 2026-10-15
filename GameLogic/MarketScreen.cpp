#include "pch.h"

#include "MarketScreen.h"

#include "Equipment.h"
#include "GameState.h"
#include "Host.h"
#include "Input.h"
#include "Text.h"
#include "Timer.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

namespace Elite
{

namespace
{

// The keys D01 reads, as scan codes.
constexpr std::uint8_t SCAN_UP = 0x48;
constexpr std::uint8_t SCAN_DOWN = 0x50;
constexpr std::uint8_t SCAN_MINUS = 0x0C;
constexpr std::uint8_t SCAN_EQUALS = 0x0D; // + without Shift, as on every UK and US layout
constexpr std::uint8_t SCAN_KEYPAD_MINUS = 0x4A;
constexpr std::uint8_t SCAN_KEYPAD_PLUS = 0x4E;
constexpr std::uint8_t SCAN_B = 0x30;
constexpr std::uint8_t SCAN_S = 0x1F;
constexpr std::uint8_t SCAN_F2 = 0x3C;
constexpr std::uint8_t SCAN_F3 = 0x3D;
constexpr std::uint8_t SCAN_F8 = 0x42;

// A stored tech level from which the station is a Dodo.
constexpr std::uint8_t DODO_TECH_LEVEL = 9;

// The room the hold has for _lot's commodity, in its unit, and the room there is.
struct Room
{
  std::uint8_t free;
  std::uint8_t capacity;
};

[[nodiscard]] Room RoomFor(const CargoLot& _lot, bool _largeBay) noexcept
{
  if (WeighedInTonnes(_lot.commodity))
  {
    const std::uint8_t bay = CargoBayTonnes(_largeBay);
    return Room{static_cast<std::uint8_t>(_lot.usedTonnes < bay ? bay - _lot.usedTonnes : 0), bay};
  }
  return Room{static_cast<std::uint8_t>(_lot.held < PRECIOUS_HELD_MOST ? PRECIOUS_HELD_MOST - _lot.held : 0), PRECIOUS_HELD_MOST};
}

// Whether _amount of _lot's commodity can be bought, and can be sold.
[[nodiscard]] bool CanBuy(const MarketGoods& _goods, const CargoLot& _lot, std::uint8_t _amount) noexcept
{
  return RefuseToBuy(_lot, _goods.largeBay) == CargoRefusal::None &&
         CargoBought(_lot, _goods.largeBay, _amount, _goods.prices[_lot.commodity].buyTenths).refusal == CargoRefusal::None;
}

[[nodiscard]] bool CanSell(const MarketGoods& _goods, const CargoLot& _lot, std::uint8_t _amount) noexcept
{
  return RefuseToSell(_lot) == CargoRefusal::None &&
         CargoSold(_lot, _amount, _goods.prices[_lot.commodity].sellTenths).refusal == CargoRefusal::None;
}

// _amount of _lot's commodity bought, or the refusal's strip.
[[nodiscard]] MarketStep Bought(const MarketScreen& _screen, const MarketGoods& _goods, const CargoLot& _lot) noexcept
{
  MarketStep step{.screen = _screen, .traded = std::nullopt};
  const std::uint16_t price = _goods.prices[_lot.commodity].buyTenths;
  CargoRefusal refusal = RefuseToBuy(_lot, _goods.largeBay);
  if (refusal == CargoRefusal::None)
  {
    const CargoTrade trade = CargoBought(_lot, _goods.largeBay, _screen.quantity, price);
    refusal = trade.refusal;
    if (refusal == CargoRefusal::None)
    {
      step.traded = trade.lot;
      step.screen.message = MarketMessage{.notice = MarketNotice::Bought,
                                          .commodity = _lot.commodity,
                                          .amount = _screen.quantity,
                                          .tenths = _lot.creditsTenths - trade.lot.creditsTenths};
      return step;
    }
  }
  step.screen.message = RefusalMessage(refusal, _lot, _goods.largeBay, _screen.quantity, price);
  return step;
}

[[nodiscard]] MarketStep Sold(const MarketScreen& _screen, const MarketGoods& _goods, const CargoLot& _lot) noexcept
{
  MarketStep step{.screen = _screen, .traded = std::nullopt};
  const std::uint16_t price = _goods.prices[_lot.commodity].sellTenths;
  CargoRefusal refusal = RefuseToSell(_lot);
  if (refusal == CargoRefusal::None)
  {
    const CargoTrade trade = CargoSold(_lot, _screen.quantity, price);
    refusal = trade.refusal;
    if (refusal == CargoRefusal::None)
    {
      step.traded = trade.lot;
      step.screen.message = MarketMessage{.notice = MarketNotice::Sold,
                                          .commodity = _lot.commodity,
                                          .amount = _screen.quantity,
                                          .tenths = std::uint32_t{_screen.quantity} * price};
      return step;
    }
  }
  step.screen.message = RefusalMessage(refusal, _lot, _goods.largeBay, _screen.quantity, price);
  return step;
}

// A trade's lot, stored where the game keeps it, and the credits reformatted.
void StoreLot(GameState& _state, const CargoLot& _lot)
{
  CommanderState::CargoHold& hold = _state.Data().commander.cargoHold;
  CommanderState::CargoHold::Lot& lot = hold.lots[_lot.commodity];
  lot.held = _lot.held;
  lot.onSale = _lot.onSale;
  hold.cargoUsedTonnes = _lot.usedTonnes;
  _state.Data().commander.legalStatus = _lot.legalStatus;
  _state.Data().commander.creditsTenths = _lot.creditsTenths;
  FormatCredits(_state);
}

// The current system's name, up to its NUL.
[[nodiscard]] std::string SystemName(const GameState& _state)
{
  const SystemRecord& system = _state.Data().commander.currentSystem;
  const std::size_t length = std::find(std::begin(system.name), std::end(system.name), std::uint8_t{0}) - std::begin(system.name);
  return std::string(std::begin(system.name), std::begin(system.name) + static_cast<std::ptrdiff_t>(length));
}

// Hides D01 when its routine ends, however it ends.
class MarketShown
{
public:
  explicit MarketShown(Host& _host) noexcept
    : m_host(_host)
  {
  }

  MarketShown(const MarketShown&) = delete;
  MarketShown& operator=(const MarketShown&) = delete;

  ~MarketShown()
  {
    m_host.HideMarket();
  }

private:
  Host& m_host;
};

} // namespace

CargoLot LotIn(const MarketGoods& _goods, std::uint8_t _commodity) noexcept
{
  return CargoLot{.commodity = _commodity,
                  .held = _goods.held[_commodity],
                  .onSale = _goods.onSale[_commodity],
                  .usedTonnes = _goods.usedTonnes,
                  .legalStatus = _goods.legalStatus,
                  .creditsTenths = _goods.creditsTenths};
}

std::uint8_t MostToBuy(const MarketGoods& _goods, std::uint8_t _commodity) noexcept
{
  const CargoLot lot = LotIn(_goods, _commodity);
  if (RefuseToBuy(lot, _goods.largeBay) != CargoRefusal::None)
  {
    return 0;
  }
  const std::uint16_t price = _goods.prices[_commodity].buyTenths;
  std::uint32_t most = std::min<std::uint32_t>(lot.onSale, RoomFor(lot, _goods.largeBay).free);
  if (price != 0)
  {
    most = std::min(most, lot.creditsTenths / price);
  }
  for (auto amount = static_cast<std::uint8_t>(most); amount != 0; --amount)
  {
    if (CanBuy(_goods, lot, amount))
    {
      return amount;
    }
  }
  return 0;
}

std::uint8_t MostToSell(const MarketGoods& _goods, std::uint8_t _commodity) noexcept
{
  return _goods.held[_commodity];
}

MarketMessage RefusalMessage(CargoRefusal _refusal, const CargoLot& _lot, bool _largeBay, std::uint8_t _amount,
                             std::uint16_t _priceTenths) noexcept
{
  MarketMessage message{.commodity = _lot.commodity,
                        .amount = _amount,
                        .tenths = std::uint32_t{_amount} * _priceTenths,
                        .creditsTenths = _lot.creditsTenths,
                        .stock = _lot.onSale,
                        .held = _lot.held};
  const Room room = RoomFor(_lot, _largeBay);
  message.freeUnits = room.free;
  message.capacityUnits = room.capacity;
  switch (_refusal)
  {
  case CargoRefusal::None:
    message.notice = MarketNotice::None;
    break;
  case CargoRefusal::NothingOnSale:
    message.notice = MarketNotice::NothingInStock;
    break;
  case CargoRefusal::NotForSale:
    message.notice = MarketNotice::NotForSale;
    break;
  case CargoRefusal::FullToCapacity:
  case CargoRefusal::BayFull:
  case CargoRefusal::CannotCarry:
    message.notice = MarketNotice::HoldFull;
    break;
  case CargoRefusal::NothingToSell:
    message.notice = MarketNotice::NothingHeld;
    break;
  case CargoRefusal::NotEnoughOnSale:
    message.notice = MarketNotice::NotEnoughInStock;
    break;
  case CargoRefusal::NotEnoughHeld:
    message.notice = MarketNotice::NotEnoughHeld;
    break;
  case CargoRefusal::NotEnoughCredits:
    message.notice = MarketNotice::NotEnoughCredits;
    break;
  }
  return message;
}

MarketStep StepMarket(const MarketScreen& _screen, const MarketInput& _input, const MarketGoods& _goods) noexcept
{
  MarketStep step{.screen = _screen, .traded = std::nullopt};
  step.screen.message = MarketMessage{};
  const auto select = [&step](std::uint8_t _row)
  {
    if (_row != step.screen.selected)
    {
      step.screen.selected = _row;
      step.screen.quantity = 1;
    }
  };
  const CargoLot lot = LotIn(_goods, _screen.selected);
  switch (_input.action)
  {
  case MarketAction::Up:
    select(_screen.selected == 0 ? std::uint8_t{0} : static_cast<std::uint8_t>(_screen.selected - 1));
    break;
  case MarketAction::Down:
    select(std::min(static_cast<std::uint8_t>(_screen.selected + 1), static_cast<std::uint8_t>(COMMODITY_KINDS - 1)));
    break;
  case MarketAction::Select:
    select(std::min(_input.row, static_cast<std::uint8_t>(COMMODITY_KINDS - 1)));
    break;
  case MarketAction::Less:
    step.screen.quantity = std::max(std::uint8_t{1}, static_cast<std::uint8_t>(_screen.quantity - 1));
    break;
  case MarketAction::More:
    step.screen.quantity = _screen.quantity == MOST_QUANTITY ? MOST_QUANTITY : static_cast<std::uint8_t>(_screen.quantity + 1);
    break;
  case MarketAction::Most:
  {
    const std::uint8_t buy = MostToBuy(_goods, _screen.selected);
    const std::uint8_t sell = MostToSell(_goods, _screen.selected);
    step.screen.quantity = buy != 0 ? buy : (sell != 0 ? sell : std::uint8_t{1});
    break;
  }
  case MarketAction::Buy:
    return Bought(_screen, _goods, lot);
  case MarketAction::Sell:
    return Sold(_screen, _goods, lot);
  }
  return step;
}

std::optional<MarketInput> MarketKey(std::uint8_t _scanCode) noexcept
{
  switch (_scanCode)
  {
  case SCAN_UP:
    return MarketInput{.action = MarketAction::Up};
  case SCAN_DOWN:
    return MarketInput{.action = MarketAction::Down};
  case SCAN_MINUS:
  case SCAN_KEYPAD_MINUS:
    return MarketInput{.action = MarketAction::Less};
  case SCAN_EQUALS:
  case SCAN_KEYPAD_PLUS:
    return MarketInput{.action = MarketAction::More};
  case SCAN_B:
    return MarketInput{.action = MarketAction::Buy};
  case SCAN_S:
    return MarketInput{.action = MarketAction::Sell};
  default:
    return std::nullopt;
  }
}

MarketInput MarketClickInput(const MarketClick& _click) noexcept
{
  switch (_click.control)
  {
  case MarketControl::Row:
    return MarketInput{.action = MarketAction::Select, .row = _click.row};
  case MarketControl::Less:
    return MarketInput{.action = MarketAction::Less};
  case MarketControl::More:
    return MarketInput{.action = MarketAction::More};
  case MarketControl::Most:
    return MarketInput{.action = MarketAction::Most};
  case MarketControl::Buy:
    return MarketInput{.action = MarketAction::Buy};
  case MarketControl::Sell:
    return MarketInput{.action = MarketAction::Sell};
  }
  return MarketInput{};
}

MarketView MarketViewOf(const MarketScreen& _screen, const MarketGoods& _goods)
{
  MarketView view;
  view.creditsTenths = _goods.creditsTenths;
  for (std::size_t commodity = 0; commodity < COMMODITY_KINDS; ++commodity)
  {
    view.buyTenths[commodity] = _goods.prices[commodity].buyTenths;
    view.sellTenths[commodity] = _goods.prices[commodity].sellTenths;
  }
  view.stock = _goods.onSale;
  view.held = _goods.held;
  view.selected = _screen.selected;
  view.quantity = _screen.quantity;
  const CargoLot lot = LotIn(_goods, _screen.selected);
  view.buyAllowed = CanBuy(_goods, lot, _screen.quantity);
  view.sellAllowed = CanSell(_goods, lot, _screen.quantity);
  view.usedTonnes = _goods.usedTonnes;
  view.bayTonnes = CargoBayTonnes(_goods.largeBay);
  view.message = _screen.message;
  return view;
}

MarketGoods MarketGoodsOf(const GameState& _state)
{
  const CommanderState& commander = _state.Data().commander;
  MarketGoods goods;
  for (std::size_t commodity = 0; commodity < COMMODITY_KINDS; ++commodity)
  {
    const MenusState::ScreenPrice& price = _state.Data().menus.screenPrices[commodity];
    goods.prices[commodity] = CommodityPrices{.buyTenths = price.price, .sellTenths = price.resale};
    goods.onSale[commodity] = commander.cargoHold.lots[commodity].onSale;
    goods.held[commodity] = commander.cargoHold.lots[commodity].held;
  }
  goods.usedTonnes = commander.cargoHold.cargoUsedTonnes;
  goods.legalStatus = commander.legalStatus;
  goods.creditsTenths = commander.creditsTenths;
  goods.largeBay = commander.largeCargoBayFitted == 1;
  return goods;
}

Task<ScreenKey> ShowMarketScreen(GameState& _state, Host& _host)
{
  // What Buy Cargo did on entry: the prices, the market's random state, and the stock rolled the first time after an arrival.
  ComputeMarketPrices(_state);
  StartMarketRandom(_state);
  if (_state.Data().commander.marketQuantitiesSet == 0)
  {
    for (CommanderState::CargoHold::Lot& lot : _state.Data().commander.cargoHold.lots)
    {
      lot.onSale = RollQuantityOnSale(_state).value_or(std::uint8_t{0});
    }
    _state.Data().commander.marketQuantitiesSet = 1;
  }

  const MarketShown shown(_host);
  const std::string name = SystemName(_state);
  const bool dodo = _state.Data().commander.currentSystem.techLevel >= DODO_TECH_LEVEL;
  MarketScreen screen;
  const auto publish = [&]()
  {
    MarketView view = MarketViewOf(screen, MarketGoodsOf(_state));
    view.systemName = name;
    view.dodo = dodo;
    _host.ShowMarket(std::move(view));
  };
  const auto act = [&](const MarketInput& _input)
  {
    const MarketStep step = StepMarket(screen, _input, MarketGoodsOf(_state));
    if (step.traded)
    {
      StoreLot(_state, *step.traded);
    }
    screen = step.screen;
  };
  publish();
  for (;;)
  {
    co_await WaitForTimerTick(_state, _host);
    bool changed = false;
    while (const std::optional<MarketClick> click = _host.TakeClick())
    {
      act(MarketClickInput(*click));
      changed = true;
    }
    const KeyPress key = GetKey(_state);
    if (key.scanCode != 0)
    {
      if (const std::optional<MarketInput> input = MarketKey(key.scanCode))
      {
        act(*input);
        changed = true;
      }
      else if (IsScreenKey(key.scanCode) && key.scanCode != SCAN_F2 && key.scanCode != SCAN_F3 && key.scanCode != SCAN_F8)
      {
        co_return ScreenKey{key.scanCode};
      }
    }
    if (changed)
    {
      publish();
    }
  }
}

} // namespace Elite
