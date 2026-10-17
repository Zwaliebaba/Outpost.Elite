#include "pch.h"

#include "EquipScreen.h"

#include "DockedKeys.h"
#include "GameState.h"
#include "Host.h"
#include "Input.h"
#include "Market.h"
#include "StatusView.h"
#include "Text.h"
#include "Timer.h"

#include <algorithm>
#include <string>
#include <utility>

namespace Elite
{

namespace
{

// The keys D02 and D03 read, as scan codes.
constexpr std::uint8_t SCAN_ESC = 0x01;
constexpr std::uint8_t SCAN_1 = 0x02;
constexpr std::uint8_t SCAN_UP = 0x48;
constexpr std::uint8_t SCAN_DOWN = 0x50;
constexpr std::uint8_t SCAN_B = 0x30;
constexpr std::uint8_t SCAN_S = 0x1F;
constexpr std::uint8_t SCAN_F3 = 0x3D;

// A stored tech level from which the station is a Dodo.
constexpr std::uint8_t DODO_TECH_LEVEL = 9;

// The fuel's units: 255 is 7.0 light years, 36 to a tenth.
constexpr std::uint8_t FUEL_TENTHS_PER_UNIT = 10;
constexpr std::uint8_t FUEL_UNITS_PER_TENTH = 0x24;

// Row _row's count fitted in _goods.
[[nodiscard]] std::uint8_t CountOf(const EquipGoods& _goods, std::uint8_t _row) noexcept
{
  return _goods.fitting.counts[std::size_t{_row} - 1];
}

// The row of the laser of type _type.
[[nodiscard]] std::uint8_t RowOfLaserType(std::uint8_t _type) noexcept
{
  for (std::uint8_t row = 1; row <= EQUIPMENT_LIST_ROWS; ++row)
  {
    if (LaserTypeOfRow(row) == _type)
    {
      return row;
    }
  }
  return 0;
}

// _fuel's units in tenths of a light year.
[[nodiscard]] std::uint8_t FuelTenthsOf(std::uint8_t _fuel) noexcept
{
  return static_cast<std::uint8_t>(_fuel * FUEL_TENTHS_PER_UNIT / FUEL_UNITS_PER_TENTH);
}

// Whether every mount carries a laser of type _type.
[[nodiscard]] bool EveryMountCarries(const LaserMounts& _mounts, std::uint8_t _type) noexcept
{
  for (std::uint8_t mount = 0; mount < LASER_MOUNT_COUNT; ++mount)
  {
    if (!MountHoldsLaser(_mounts, mount) || LaserTypeOnMount(_mounts, mount) != _type)
    {
      return false;
    }
  }
  return true;
}

// The strip for ADR-043's _refusal of row _row, with its figures.
[[nodiscard]] EquipMessage RefusalMessage(EquipmentRefusal _refusal, const EquipGoods& _goods, std::uint8_t _row) noexcept
{
  EquipMessage message{.row = _row, .creditsTenths = _goods.creditsTenths, .fuelTenths = FuelTenthsOf(_goods.fitting.counts[0])};
  switch (_refusal)
  {
  case EquipmentRefusal::None:
    break;
  case EquipmentRefusal::NoFuelAvailable:
    message.notice = EquipNotice::NoFuel;
    break;
  case EquipmentRefusal::FuelFull:
    message.notice = EquipNotice::FuelFull;
    break;
  case EquipmentRefusal::FourMissilesOnly:
    message.notice = EquipNotice::FourMissiles;
    break;
  case EquipmentRefusal::AlreadyFitted:
    message.notice = EquipNotice::AlreadyFitted;
    break;
  case EquipmentRefusal::FourLasers:
    message.notice = EquipNotice::FourLasers;
    break;
  case EquipmentRefusal::ScoopNeeded:
    message.notice = EquipNotice::ScoopsNeeded;
    break;
  case EquipmentRefusal::NotEnoughCredits:
    message.notice = EquipNotice::NotEnoughCredits;
    break;
  case EquipmentRefusal::FuelSaleIllegal:
    message.notice = EquipNotice::FuelNotBought;
    break;
  case EquipmentRefusal::NoMissileToSell:
  case EquipmentRefusal::NoItemToSell:
    message.notice = EquipNotice::NoneFitted;
    break;
  case EquipmentRefusal::TooMuchCargo:
    message.notice = EquipNotice::TooMuchCargo;
    message.tonnes = static_cast<std::uint8_t>(_goods.fitting.cargoUsedTonnes - CARGO_BAY_TONNES);
    break;
  case EquipmentRefusal::SellMiningLaser:
    message.notice = EquipNotice::MiningLaserFitted;
    break;
  }
  return message;
}

// The trade _goods would leave as they are.
[[nodiscard]] EquipTrade TradeOf(const EquipGoods& _goods) noexcept
{
  return EquipTrade{.counts = _goods.fitting.counts, .mounts = _goods.mounts, .creditsTenths = _goods.creditsTenths};
}

// What a laser's trade-in on mount _mount fetches, and its row: nothing for a free mount.
struct TradeIn
{
  std::uint8_t row;
  std::uint32_t tenths;
};

[[nodiscard]] TradeIn TradeInOn(const EquipGoods& _goods, std::uint8_t _mount) noexcept
{
  if (!MountHoldsLaser(_goods.mounts, _mount))
  {
    return TradeIn{.row = 0, .tenths = 0};
  }
  const std::uint8_t row = RowOfLaserType(LaserTypeOnMount(_goods.mounts, _mount));
  return TradeIn{.row = row, .tenths = SellingPriceTenths(_goods, row)};
}

// Row _row, anything but a laser, bought.
[[nodiscard]] EquipStep Bought(const EquipScreen& _screen, const EquipGoods& _goods, std::uint8_t _row)
{
  const std::uint32_t cost = BuyingCostTenths(_goods, _row);
  EquipTrade trade = TradeOf(_goods);
  std::uint8_t& count = trade.counts[std::size_t{_row} - 1];
  count = CountAfterFitting(_row, count);
  trade.creditsTenths = CreditsAfterPaying(_goods.creditsTenths, cost).value_or(_goods.creditsTenths);
  EquipStep step{.screen = _screen, .traded = trade};
  step.screen.message =
    EquipMessage{.notice = EquipNotice::Bought, .row = _row, .tenths = cost, .fuelTenths = FuelTenthsOf(trade.counts[0])};
  return step;
}

// Row _row, anything but a laser, sold.
[[nodiscard]] EquipStep Sold(const EquipScreen& _screen, const EquipGoods& _goods, std::uint8_t _row)
{
  const std::uint32_t price = SellingPriceTenths(_goods, _row);
  EquipTrade trade = TradeOf(_goods);
  --trade.counts[std::size_t{_row} - 1];
  trade.creditsTenths = CreditsAfterEarning(_goods.creditsTenths, price);
  EquipStep step{.screen = _screen, .traded = trade};
  step.screen.message = EquipMessage{.notice = EquipNotice::Sold, .row = _row, .tenths = price};
  return step;
}

// Row _row's laser fitted on mount _mount, the laser there traded in.
[[nodiscard]] EquipStep LaserFitted(const EquipScreen& _screen, const EquipGoods& _goods, std::uint8_t _row, std::uint8_t _mount)
{
  const std::uint32_t price = _goods.priceTenths[std::size_t{_row} - 1];
  const TradeIn tradeIn = TradeInOn(_goods, _mount);
  EquipTrade trade = TradeOf(_goods);
  ++trade.counts[std::size_t{_row} - 1];
  if (tradeIn.row != 0)
  {
    --trade.counts[std::size_t{tradeIn.row} - 1];
  }
  trade.mounts = WithLaserFitted(_goods.mounts, _mount, LaserTypeOfRow(_row).value_or(0));
  trade.creditsTenths = CreditsAfterEarning(_goods.creditsTenths, tradeIn.tenths) - price;
  EquipStep step{.screen = _screen, .traded = trade};
  step.screen.picker.reset();
  step.screen.message = EquipMessage{.notice = EquipNotice::LaserFitted,
                                     .row = _row,
                                     .mount = _mount,
                                     .tradedIn = tradeIn.row != 0,
                                     .tradedRow = tradeIn.row,
                                     .tenths = price};
  return step;
}

// Row _row's laser sold off mount _mount.
[[nodiscard]] EquipStep LaserSold(const EquipScreen& _screen, const EquipGoods& _goods, std::uint8_t _row, std::uint8_t _mount)
{
  const std::uint32_t price = SellingPriceTenths(_goods, _row);
  EquipTrade trade = TradeOf(_goods);
  --trade.counts[std::size_t{_row} - 1];
  trade.mounts = WithLaserCleared(_goods.mounts, _mount);
  trade.creditsTenths = CreditsAfterEarning(_goods.creditsTenths, price);
  EquipStep step{.screen = _screen, .traded = trade};
  step.screen.picker.reset();
  step.screen.message = EquipMessage{.notice = EquipNotice::LaserSold, .row = _row, .mount = _mount, .tenths = price};
  return step;
}

// Whether some mount would take row _row's laser.
[[nodiscard]] bool SomeMountTakes(const EquipGoods& _goods, std::uint8_t _row)
{
  for (std::uint8_t mount = 0; mount < LASER_MOUNT_COUNT; ++mount)
  {
    if (RefuseToFitOnMount(_goods, _row, mount).notice == EquipNotice::None)
    {
      return true;
    }
  }
  return false;
}

// A trade's counts, mounts and credits, stored where the game keeps them, and the credits reformatted.
void StoreTrade(GameState& _state, const EquipTrade& _trade)
{
  CommanderState& commander = _state.Data().commander;
  for (std::size_t item = 0; item < EQUIPMENT_LIST_ROWS; ++item)
  {
    commander.*EQUIPMENT_ITEMS[item] = _trade.counts[item];
  }
  commander.laserMountsFitted = _trade.mounts.fitted;
  commander.laserMountTypes = _trade.mounts.types;
  commander.creditsTenths = _trade.creditsTenths;
  FormatCredits(_state);
}

// Hides D02 when its routine ends, however it ends.
class EquipShown
{
public:
  explicit EquipShown(Host& _host) noexcept
    : m_host(_host)
  {
  }

  EquipShown(const EquipShown&) = delete;
  EquipShown& operator=(const EquipShown&) = delete;

  ~EquipShown()
  {
    m_host.HideEquip();
  }

private:
  Host& m_host;
};

} // namespace

std::uint32_t BuyingCostTenths(const EquipGoods& _goods, std::uint8_t _row)
{
  const std::uint16_t price = _goods.priceTenths[std::size_t{_row} - 1];
  return _row == FUEL_ROW ? FuelPriceTenths(CountOf(_goods, FUEL_ROW), price) : price;
}

std::uint32_t SellingPriceTenths(const EquipGoods& _goods, std::uint8_t _row) noexcept
{
  return _row == FUEL_ROW ? 0 : ResaleTenths(_goods.priceTenths[std::size_t{_row} - 1]);
}

EquipMessage RefuseToBuyEquipment(const EquipGoods& _goods, std::uint8_t _row)
{
  if (const std::optional<std::uint8_t> type = LaserTypeOfRow(_row))
  {
    if (EveryMountCarries(_goods.mounts, *type))
    {
      return RefusalMessage(EquipmentRefusal::FourLasers, _goods, _row);
    }
    if (_row == MINING_LASER_ROW && CountOf(_goods, FUEL_SCOOPS_ROW) != 1)
    {
      return RefusalMessage(EquipmentRefusal::ScoopNeeded, _goods, _row);
    }
    return EquipMessage{};
  }
  const EquipmentRefusal refusal = RefuseToFit(_row, _goods.fitting);
  if (refusal != EquipmentRefusal::None)
  {
    return RefusalMessage(refusal, _goods, _row);
  }
  const std::uint32_t cost = BuyingCostTenths(_goods, _row);
  if (!CreditsAfterPaying(_goods.creditsTenths, cost))
  {
    EquipMessage message = RefusalMessage(EquipmentRefusal::NotEnoughCredits, _goods, _row);
    message.tenths = cost;
    return message;
  }
  return EquipMessage{};
}

EquipMessage RefuseToFitOnMount(const EquipGoods& _goods, std::uint8_t _row, std::uint8_t _mount)
{
  const std::uint8_t type = LaserTypeOfRow(_row).value_or(0);
  if (MountHoldsLaser(_goods.mounts, _mount) && LaserTypeOnMount(_goods.mounts, _mount) == type)
  {
    return EquipMessage{.notice = EquipNotice::MountTaken, .row = _row, .mount = _mount};
  }
  const std::uint32_t price = _goods.priceTenths[std::size_t{_row} - 1];
  const std::uint32_t tradeIn = TradeInOn(_goods, _mount).tenths;
  if (!CreditsAfterPaying(CreditsAfterEarning(_goods.creditsTenths, tradeIn), price))
  {
    return EquipMessage{.notice = EquipNotice::NotEnoughCredits,
                        .row = _row,
                        .mount = _mount,
                        .tenths = price - tradeIn,
                        .creditsTenths = _goods.creditsTenths};
  }
  return EquipMessage{};
}

EquipMessage RefuseToSellOffMount(const EquipGoods& _goods, std::uint8_t _row, std::uint8_t _mount)
{
  const std::uint8_t type = LaserTypeOfRow(_row).value_or(0);
  if (!MountHoldsLaser(_goods.mounts, _mount) || LaserTypeOnMount(_goods.mounts, _mount) != type)
  {
    return EquipMessage{.notice = EquipNotice::MountWithout, .row = _row, .mount = _mount};
  }
  return EquipMessage{};
}

EquipStep StepEquip(const EquipScreen& _screen, const EquipInput& _input, const EquipGoods& _goods)
{
  EquipStep step{.screen = _screen, .traded = std::nullopt};
  step.screen.message = EquipMessage{};
  const std::uint8_t last = _goods.rows == 0 ? std::uint8_t{0} : static_cast<std::uint8_t>(_goods.rows - 1);
  const auto select = [&step, last](std::uint8_t _row)
  {
    step.screen.picker.reset();
    step.screen.selected = std::min(_row, last);
  };
  const auto row = static_cast<std::uint8_t>(_screen.selected + 1);
  switch (_input.action)
  {
  case EquipAction::Up:
    select(_screen.selected == 0 ? std::uint8_t{0} : static_cast<std::uint8_t>(_screen.selected - 1));
    break;
  case EquipAction::Down:
    select(static_cast<std::uint8_t>(_screen.selected + 1));
    break;
  case EquipAction::Select:
    select(_input.index);
    break;
  case EquipAction::Buy:
  {
    step.screen.picker.reset();
    const EquipMessage refusal = RefuseToBuyEquipment(_goods, row);
    if (refusal.notice != EquipNotice::None)
    {
      step.screen.message = refusal;
    }
    else if (LaserTypeOfRow(row))
    {
      step.screen.picker = MountPicker{.row = row, .selling = false};
    }
    else
    {
      return Bought(step.screen, _goods, row);
    }
    break;
  }
  case EquipAction::Sell:
  {
    step.screen.picker.reset();
    const EquipmentRefusal refusal = RefuseToRemove(row, _goods.fitting);
    if (refusal != EquipmentRefusal::None)
    {
      step.screen.message = RefusalMessage(refusal, _goods, row);
    }
    else if (LaserTypeOfRow(row))
    {
      step.screen.picker = MountPicker{.row = row, .selling = true};
    }
    else
    {
      return Sold(step.screen, _goods, row);
    }
    break;
  }
  case EquipAction::Mount:
  {
    if (!_screen.picker || _input.index >= LASER_MOUNT_COUNT)
    {
      break;
    }
    const MountPicker picker = *_screen.picker;
    const EquipMessage refusal =
      picker.selling ? RefuseToSellOffMount(_goods, picker.row, _input.index) : RefuseToFitOnMount(_goods, picker.row, _input.index);
    if (refusal.notice != EquipNotice::None)
    {
      step.screen.message = refusal;
      break;
    }
    return picker.selling ? LaserSold(step.screen, _goods, picker.row, _input.index)
                          : LaserFitted(step.screen, _goods, picker.row, _input.index);
  }
  case EquipAction::Cancel:
    step.screen.picker.reset();
    break;
  }
  return step;
}

std::optional<EquipInput> EquipKey(std::uint8_t _scanCode, bool _picking) noexcept
{
  if (_picking)
  {
    if (_scanCode >= SCAN_1 && _scanCode < SCAN_1 + LASER_MOUNT_COUNT)
    {
      return EquipInput{.action = EquipAction::Mount, .index = MOUNT_OF_PICKER_KEY[std::size_t{_scanCode} - SCAN_1]};
    }
    if (_scanCode == SCAN_ESC)
    {
      return EquipInput{.action = EquipAction::Cancel};
    }
  }
  switch (_scanCode)
  {
  case SCAN_UP:
    return EquipInput{.action = EquipAction::Up};
  case SCAN_DOWN:
    return EquipInput{.action = EquipAction::Down};
  case SCAN_B:
    return EquipInput{.action = EquipAction::Buy};
  case SCAN_S:
    return EquipInput{.action = EquipAction::Sell};
  default:
    return std::nullopt;
  }
}

EquipInput EquipClickInput(const EquipClick& _click) noexcept
{
  switch (_click.control)
  {
  case EquipControl::Row:
    return EquipInput{.action = EquipAction::Select, .index = _click.index};
  case EquipControl::Buy:
    return EquipInput{.action = EquipAction::Buy};
  case EquipControl::Sell:
    return EquipInput{.action = EquipAction::Sell};
  case EquipControl::Mount:
    return EquipInput{.action = EquipAction::Mount, .index = _click.index};
  case EquipControl::Cancel:
    return EquipInput{.action = EquipAction::Cancel};
  }
  return EquipInput{};
}

EquipView EquipViewOf(const EquipScreen& _screen, const EquipGoods& _goods)
{
  EquipView view;
  view.creditsTenths = _goods.creditsTenths;
  view.rows = _goods.rows;
  for (std::uint8_t row = 1; row <= _goods.rows; ++row)
  {
    EquipRowView& item = view.items[std::size_t{row} - 1];
    item.buyTenths = _goods.priceTenths[std::size_t{row} - 1];
    item.sellTenths = static_cast<std::uint16_t>(SellingPriceTenths(_goods, row));
    item.count = CountOf(_goods, row);
    if (const std::optional<std::uint8_t> type = LaserTypeOfRow(row))
    {
      for (std::uint8_t mount = 0; mount < LASER_MOUNT_COUNT; ++mount)
      {
        if (MountHoldsLaser(_goods.mounts, mount) && LaserTypeOnMount(_goods.mounts, mount) == *type)
        {
          item.mounts = static_cast<std::uint8_t>(item.mounts | (1u << mount));
        }
      }
    }
  }
  view.fuelTenths = FuelTenthsOf(CountOf(_goods, FUEL_ROW));
  view.selected = _screen.selected;
  const auto row = static_cast<std::uint8_t>(_screen.selected + 1);
  const bool laser = LaserTypeOfRow(row).has_value();
  view.buyAllowed = RefuseToBuyEquipment(_goods, row).notice == EquipNotice::None && (!laser || SomeMountTakes(_goods, row));
  view.sellAllowed = RefuseToRemove(row, _goods.fitting) == EquipmentRefusal::None;
  if (_screen.picker)
  {
    view.picker.open = true;
    view.picker.selling = _screen.picker->selling;
    view.picker.row = _screen.picker->row;
    for (std::uint8_t mount = 0; mount < LASER_MOUNT_COUNT; ++mount)
    {
      MountView& shown = view.picker.mounts[mount];
      shown.laser = MountHoldsLaser(_goods.mounts, mount);
      shown.type = shown.laser ? LaserTypeOnMount(_goods.mounts, mount) : std::uint8_t{0};
      const EquipMessage refusal = _screen.picker->selling ? RefuseToSellOffMount(_goods, _screen.picker->row, mount)
                                                           : RefuseToFitOnMount(_goods, _screen.picker->row, mount);
      shown.allowed = refusal.notice == EquipNotice::None;
    }
  }
  view.message = _screen.message;
  return view;
}

EquipGoods EquipGoodsOf(const GameState& _state)
{
  const CommanderState& commander = _state.Data().commander;
  const MarketPlace place{.economy = commander.currentSystem.economy,
                          .government = commander.currentSystem.government,
                          .techLevel = commander.currentSystem.techLevel};
  EquipGoods goods;
  goods.rows = EquipmentRowsOnSale(place.techLevel);
  for (std::uint8_t row = 1; row <= EQUIPMENT_LIST_ROWS; ++row)
  {
    goods.priceTenths[std::size_t{row} - 1] = EquipmentPriceTenths(EquipmentItemAt(row), place);
    goods.fitting.counts[std::size_t{row} - 1] = commander.*EQUIPMENT_ITEMS[std::size_t{row} - 1];
  }
  goods.fitting.laserMountsFitted = commander.laserMountsFitted;
  goods.fitting.cargoUsedTonnes = commander.cargoHold.cargoUsedTonnes;
  goods.fitting.missionNumber = commander.missionNumber;
  goods.mounts = LaserMounts{.fitted = commander.laserMountsFitted, .types = commander.laserMountTypes};
  goods.creditsTenths = commander.creditsTenths;
  return goods;
}

Task<ScreenKey> ShowEquipScreen(GameState& _state, Host& _host)
{
  const EquipShown shown(_host);
  const std::string name(TextWithin(_state.Data().commander.currentSystem.name, 0));
  const bool dodo = _state.Data().commander.currentSystem.techLevel >= DODO_TECH_LEVEL;
  const auto techLevel = static_cast<std::uint8_t>(_state.Data().commander.currentSystem.techLevel + 1);
  EquipScreen screen;
  const auto publish = [&]()
  {
    EquipView view = EquipViewOf(screen, EquipGoodsOf(_state));
    view.systemName = name;
    view.dodo = dodo;
    view.techLevel = techLevel;
    _host.ShowEquip(std::move(view));
  };
  const auto act = [&](const EquipInput& _input)
  {
    const EquipStep step = StepEquip(screen, _input, EquipGoodsOf(_state));
    if (step.traded)
    {
      StoreTrade(_state, *step.traded);
    }
    screen = step.screen;
  };
  publish();
  for (;;)
  {
    co_await WaitForTimerTick(_state, _host);
    bool changed = false;
    while (const std::optional<EquipClick> click = _host.TakeEquipClick())
    {
      act(EquipClickInput(*click));
      changed = true;
    }
    const KeyPress key = GetKey(_state);
    if (key.scanCode != 0)
    {
      if (const std::optional<EquipInput> input = EquipKey(key.scanCode, screen.picker.has_value()))
      {
        act(*input);
        changed = true;
      }
      else if (key.scanCode != SCAN_F3 && ClosesScreen(key.scanCode, ScreenPlace::Docked))
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
