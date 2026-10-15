#include "pch.h"

#include "GameRig.h"
#include "Market.h"
#include "MarketScreen.h"
#include "Replay.h"

#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint8_t FOOD = 0;
constexpr std::uint8_t RADIOACTIVES = 2;
constexpr std::uint8_t FIREARMS = 10;
constexpr std::uint8_t GOLD = 13;
constexpr std::uint8_t ALIEN_ITEMS = 16;

// A market where everything costs 10.0 credits and sells for 9.0, 20 of each on sale, nothing held, and 1000.0 credits.
[[nodiscard]] Elite::MarketGoods EvenMarket()
{
  Elite::MarketGoods goods;
  for (std::size_t commodity = 0; commodity < Elite::COMMODITY_KINDS; ++commodity)
  {
    goods.prices[commodity] = Elite::CommodityPrices{.buyTenths = 100, .sellTenths = 90};
    goods.onSale[commodity] = 20;
  }
  goods.creditsTenths = 10000;
  return goods;
}

// _screen with _action applied over _goods.
[[nodiscard]] Elite::MarketStep Step(const Elite::MarketScreen& _screen, Elite::MarketAction _action, const Elite::MarketGoods& _goods,
                                     std::uint8_t _row = 0)
{
  return Elite::StepMarket(_screen, Elite::MarketInput{.action = _action, .row = _row}, _goods);
}

// A screen on row _row with quantity _quantity.
[[nodiscard]] Elite::MarketScreen On(std::uint8_t _row, std::uint8_t _quantity)
{
  return Elite::MarketScreen{.selected = _row, .quantity = _quantity, .message = {}};
}

// _steps, in Replay.h's syntax, played on _rig; each must run to its end.
void Play(GameRig& _rig, std::string_view _steps)
{
  std::vector<Elite::Step> steps;
  std::string error;
  Assert::IsTrue(Elite::ParseSteps(_steps, steps, error), std::wstring(error.begin(), error.end()).c_str());
  Elite::ReplayPlayer player(_rig.Host(), _rig.State());
  for (const Elite::Step& step : steps)
  {
    Elite::Checkpoint checkpoint;
    Assert::IsTrue(player.Play(step, checkpoint) == Elite::ExpectedStop(step), L"the step runs");
  }
}

// From power-on to the status screen at Lave, as a new game docks.
constexpr std::string_view TO_THE_STATUS_SCREEN = "wait 3\nkey space; wait 4";

} // namespace

// The market, D01 (D82, D92): its rules over ADR-043's, as values (known answers), and the screen as the game runs it.
TEST_CLASS(MarketTests)
{
public:
  // MAX's most to buy is the least of the stock, the room and the cash (D93), and 0 for what cannot be bought at all; a precious
  // commodity's room is what it lacks of 250, outside the tonnes.
  TEST_METHOD(TheMostToBuyIsTheLeastOfStockRoomAndCash)
  {
    Elite::MarketGoods goods = EvenMarket();
    Assert::AreEqual(std::uint8_t{20}, Elite::MostToBuy(goods, FOOD), L"the hold's 20 tonnes and the stock bind together");
    goods.onSale[FOOD] = 9;
    Assert::AreEqual(std::uint8_t{9}, Elite::MostToBuy(goods, FOOD), L"the stock binds");
    goods.usedTonnes = 15;
    Assert::AreEqual(std::uint8_t{5}, Elite::MostToBuy(goods, FOOD), L"the room binds");
    goods.creditsTenths = 412;
    Assert::AreEqual(std::uint8_t{4}, Elite::MostToBuy(goods, FOOD), L"the cash binds");
    goods.largeBay = true;
    goods.usedTonnes = 20;
    goods.creditsTenths = 100000;
    Assert::AreEqual(std::uint8_t{9}, Elite::MostToBuy(goods, FOOD), L"with the extension, 15 tonnes of room");
    goods.usedTonnes = 35;
    Assert::AreEqual(std::uint8_t{0}, Elite::MostToBuy(goods, FOOD), L"a full hold");
    goods.onSale[GOLD] = 100;
    goods.held[GOLD] = 200;
    Assert::AreEqual(std::uint8_t{50}, Elite::MostToBuy(goods, GOLD), L"gold, outside the full hold, up to 250");
    Assert::AreEqual(std::uint8_t{0}, Elite::MostToBuy(goods, ALIEN_ITEMS), L"alien items are never sold");
    goods.held[RADIOACTIVES] = 7;
    Assert::AreEqual(std::uint8_t{7}, Elite::MostToSell(goods, RADIOACTIVES), L"the most to sell is what is held");
  }

  // + and − keep the quantity from 1 to 255; a new row starts at 1; ↑ and ↓ stop at the ends; MAX takes the most to buy, or what
  // is held when nothing can be bought, or 1.
  TEST_METHOD(TheQuantityAndTheRowStayInRange)
  {
    const Elite::MarketGoods goods = EvenMarket();
    Assert::AreEqual(std::uint8_t{1}, Step(On(FOOD, 1), Elite::MarketAction::Less, goods).screen.quantity, L"not below 1");
    Assert::AreEqual(std::uint8_t{3}, Step(On(FOOD, 2), Elite::MarketAction::More, goods).screen.quantity);
    Assert::AreEqual(std::uint8_t{255}, Step(On(FOOD, 255), Elite::MarketAction::More, goods).screen.quantity, L"not past 255");
    Assert::AreEqual(std::uint8_t{0}, Step(On(FOOD, 5), Elite::MarketAction::Up, goods).screen.selected, L"the top row stays");
    Assert::AreEqual(std::uint8_t{5}, Step(On(FOOD, 5), Elite::MarketAction::Up, goods).screen.quantity, L"and so does its quantity");
    const Elite::MarketStep down = Step(On(FOOD, 5), Elite::MarketAction::Down, goods);
    Assert::AreEqual(std::uint8_t{1}, down.screen.selected);
    Assert::AreEqual(std::uint8_t{1}, down.screen.quantity, L"a new row starts at 1");
    Assert::AreEqual(std::uint8_t{16}, Step(On(ALIEN_ITEMS, 1), Elite::MarketAction::Down, goods).screen.selected, L"the last row stays");
    Assert::AreEqual(std::uint8_t{7}, Step(On(FOOD, 1), Elite::MarketAction::Select, goods, 7).screen.selected, L"a click's row");
    Assert::AreEqual(std::uint8_t{20}, Step(On(FOOD, 1), Elite::MarketAction::Most, goods).screen.quantity, L"MAX buys");
    Elite::MarketGoods held = goods;
    held.held[ALIEN_ITEMS] = 6;
    Assert::AreEqual(std::uint8_t{6}, Step(On(ALIEN_ITEMS, 1), Elite::MarketAction::Most, held).screen.quantity, L"MAX sells");
    Assert::AreEqual(std::uint8_t{1}, Step(On(ALIEN_ITEMS, 4), Elite::MarketAction::Most, goods).screen.quantity, L"MAX with neither");
  }

  // A purchase and a sale go through ADR-043's rules, and leave the lot to store and the strip with its figures.
  TEST_METHOD(ATradeLeavesItsLotAndItsStrip)
  {
    Elite::MarketGoods goods = EvenMarket();
    const Elite::MarketStep bought = Step(On(FOOD, 3), Elite::MarketAction::Buy, goods);
    Assert::IsTrue(bought.traded.has_value(), L"three tonnes of food bought");
    const Elite::CargoLot boughtLot = bought.traded.value_or(Elite::CargoLot{});
    Assert::AreEqual(std::uint8_t{3}, boughtLot.held);
    Assert::AreEqual(std::uint8_t{17}, boughtLot.onSale);
    Assert::AreEqual(std::uint8_t{3}, boughtLot.usedTonnes);
    Assert::AreEqual(std::uint32_t{9700}, boughtLot.creditsTenths);
    Assert::IsTrue(bought.screen.message.notice == Elite::MarketNotice::Bought);
    Assert::AreEqual(std::uint32_t{300}, bought.screen.message.tenths, L"30.0 credits paid");
    Assert::AreEqual(std::uint8_t{3}, bought.screen.message.amount);

    goods.held[FIREARMS] = 2;
    goods.usedTonnes = 2;
    const Elite::MarketStep sold = Step(On(FIREARMS, 2), Elite::MarketAction::Sell, goods);
    Assert::IsTrue(sold.traded.has_value(), L"the firearms sold");
    const Elite::CargoLot soldLot = sold.traded.value_or(Elite::CargoLot{});
    Assert::AreEqual(std::uint8_t{0}, soldLot.held);
    Assert::AreEqual(std::uint8_t{0}, soldLot.usedTonnes);
    Assert::AreEqual(std::uint32_t{10180}, soldLot.creditsTenths);
    Assert::AreNotEqual(std::uint8_t{0}, soldLot.legalStatus, L"firearms are contraband");
    Assert::IsTrue(sold.screen.message.notice == Elite::MarketNotice::Sold);
    Assert::AreEqual(std::uint32_t{180}, sold.screen.message.tenths, L"18.0 credits earned");

    const Elite::MarketStep cleared = Step(bought.screen, Elite::MarketAction::More, goods);
    Assert::IsTrue(cleared.screen.message.notice == Elite::MarketNotice::None, L"the next action clears the strip");
    Assert::IsFalse(cleared.traded.has_value());
  }

  // Each refusal of D11's Market column, with the figures its words take; a refusal trades nothing.
  TEST_METHOD(EveryRefusalHasItsStrip)
  {
    Elite::MarketGoods goods = EvenMarket();
    const auto refused = [](const Elite::MarketStep& _step, Elite::MarketNotice _notice, const wchar_t* _what)
    {
      Assert::IsFalse(_step.traded.has_value(), _what);
      Assert::IsTrue(_step.screen.message.notice == _notice, _what);
      return _step.screen.message;
    };

    goods.creditsTenths = 412;
    Elite::MarketMessage message =
      refused(Step(On(FOOD, 5), Elite::MarketAction::Buy, goods), Elite::MarketNotice::NotEnoughCredits, L"not enough credits");
    Assert::AreEqual(std::uint32_t{500}, message.tenths, L"50.0 credits needed");
    Assert::AreEqual(std::uint32_t{412}, message.creditsTenths, L"41.2 available");
    goods.creditsTenths = 10000;

    goods.usedTonnes = 18;
    message = refused(Step(On(FOOD, 5), Elite::MarketAction::Buy, goods), Elite::MarketNotice::HoldFull, L"the hold cannot carry it");
    Assert::AreEqual(std::uint8_t{2}, message.freeUnits, L"2 tonnes free");
    Assert::AreEqual(std::uint8_t{20}, message.capacityUnits, L"of 20");
    goods.usedTonnes = 20;
    message = refused(Step(On(FOOD, 1), Elite::MarketAction::Buy, goods), Elite::MarketNotice::HoldFull, L"the hold full");
    Assert::AreEqual(std::uint8_t{0}, message.freeUnits);
    goods.usedTonnes = 0;

    goods.held[GOLD] = 250;
    message = refused(Step(On(GOLD, 1), Elite::MarketAction::Buy, goods), Elite::MarketNotice::HoldFull, L"gold at its 250");
    Assert::AreEqual(std::uint8_t{250}, message.capacityUnits);
    goods.held[GOLD] = 0;

    goods.onSale[FOOD] = 9;
    message = refused(Step(On(FOOD, 12), Elite::MarketAction::Buy, goods), Elite::MarketNotice::NotEnoughInStock, L"more than the stock");
    Assert::AreEqual(std::uint8_t{9}, message.stock);
    Assert::AreEqual(std::uint8_t{12}, message.amount);
    goods.onSale[FOOD] = 0;
    (void)refused(Step(On(FOOD, 1), Elite::MarketAction::Buy, goods), Elite::MarketNotice::NothingInStock, L"none in stock");

    (void)refused(Step(On(ALIEN_ITEMS, 1), Elite::MarketAction::Buy, goods), Elite::MarketNotice::NotForSale, L"alien items");
    (void)refused(Step(On(RADIOACTIVES, 1), Elite::MarketAction::Sell, goods), Elite::MarketNotice::NothingHeld, L"none held");
    goods.held[RADIOACTIVES] = 2;
    message = refused(Step(On(RADIOACTIVES, 3), Elite::MarketAction::Sell, goods), Elite::MarketNotice::NotEnoughHeld, L"more than held");
    Assert::AreEqual(std::uint8_t{2}, message.held);
  }

  // The view says whether the quantity's trade would happen, which the shell shows as a lit or a dimmed button.
  TEST_METHOD(TheViewSaysWhichTradesWouldHappen)
  {
    Elite::MarketGoods goods = EvenMarket();
    goods.held[FOOD] = 2;
    Elite::MarketView view = Elite::MarketViewOf(On(FOOD, 2), goods);
    Assert::IsTrue(view.buyAllowed);
    Assert::IsTrue(view.sellAllowed);
    view = Elite::MarketViewOf(On(FOOD, 3), goods);
    Assert::IsTrue(view.buyAllowed);
    Assert::IsFalse(view.sellAllowed, L"more than is held");
    view = Elite::MarketViewOf(On(ALIEN_ITEMS, 1), goods);
    Assert::IsFalse(view.buyAllowed, L"alien items");
    Assert::AreEqual(std::uint8_t{20}, view.bayTonnes);
    Assert::AreEqual(std::uint16_t{100}, view.buyTenths[FOOD]);
    Assert::AreEqual(std::uint16_t{90}, view.sellTenths[FOOD]);
  }

  // The keys D01 reads: ↑ ↓, + and − on the main keys and the keypad, B and S; nothing else.
  TEST_METHOD(TheKeysAreTheFootersKeys)
  {
    const auto action = [](std::uint8_t _scanCode) { return Elite::MarketKey(_scanCode).value().action; };
    Assert::IsTrue(action(0x48) == Elite::MarketAction::Up);
    Assert::IsTrue(action(0x50) == Elite::MarketAction::Down);
    Assert::IsTrue(action(0x0C) == Elite::MarketAction::Less);
    Assert::IsTrue(action(0x4A) == Elite::MarketAction::Less);
    Assert::IsTrue(action(0x0D) == Elite::MarketAction::More);
    Assert::IsTrue(action(0x4E) == Elite::MarketAction::More);
    Assert::IsTrue(action(0x30) == Elite::MarketAction::Buy);
    Assert::IsTrue(action(0x1F) == Elite::MarketAction::Sell);
    Assert::IsFalse(Elite::MarketKey(0x32).has_value(), L"M is not MAX: the footer names no key for it");
    Assert::IsFalse(Elite::MarketKey(0x3C).has_value(), L"an F-key is the docked host's");
  }

  // F2 opens the market while docked: the prices are Lave's, the stock is rolled once, and keys and clicks trade; it
  // stays open on its own key and the keys that do nothing docked (D96), and another screen's key closes it.
  TEST_METHOD(TheMarketRunsDocked)
  {
    GameRig rig("Market");
    Play(rig, TO_THE_STATUS_SCREEN);
    Assert::IsTrue(rig.Host().ShownMarket() == nullptr, L"the status screen first");
    Play(rig, "key F2; wait 0.2");
    const Elite::MarketView* view = rig.Host().ShownMarket();
    Assert::IsTrue(view != nullptr, L"F2 opens the market");
    Assert::AreEqual(std::uint8_t{1}, rig.State().Data().commander.marketQuantitiesSet, L"the stock rolled");
    const Elite::SystemRecord& lave = rig.State().Data().commander.currentSystem;
    const Elite::CommodityPrices food =
      Elite::PricesOf(FOOD, Elite::MarketPlace{.economy = lave.economy, .government = lave.government, .techLevel = lave.techLevel});
    Assert::AreEqual(food.buyTenths, view->buyTenths[FOOD], L"Lave's food price");
    Assert::IsTrue(view->systemName == "LAVE", L"Lave's name");
    Assert::IsFalse(view->dodo, L"Lave's station is a Coriolis");
    const std::uint8_t stock = view->stock[FOOD];
    Assert::IsTrue(stock > 2, L"food on sale at Lave");

    Play(rig, "key equal; wait 0.05\nkey b; wait 0.05");
    view = rig.Host().ShownMarket();
    Assert::AreEqual(std::uint8_t{2}, rig.State().Data().commander.cargoHold.lots[FOOD].held, L"two tonnes bought by keys");
    Assert::AreEqual(static_cast<std::uint8_t>(stock - 2), rig.State().Data().commander.cargoHold.lots[FOOD].onSale);
    Assert::AreEqual(1000u - 2u * food.buyTenths, rig.State().Data().commander.creditsTenths);
    Assert::IsTrue(view->message.notice == Elite::MarketNotice::Bought, L"its strip");

    rig.Host().Click(Elite::MarketClick{.control = Elite::MarketControl::Sell});
    Play(rig, "wait 0.05\nkey F2; wait 0.05\nkey F8; wait 0.05\nkey F9; wait 0.05");
    Assert::AreEqual(std::uint8_t{0}, rig.State().Data().commander.cargoHold.lots[FOOD].held, L"sold back by a click");
    Assert::IsTrue(rig.Host().ShownMarket() != nullptr, L"F2, its own key, and F8 and F9, which do nothing docked, keep it open");

    Play(rig, "key F6; wait 0.3");
    Assert::IsTrue(rig.Host().ShownMarket() == nullptr, L"F6 closes it");
    Assert::IsTrue(rig.Host().ShownDockedScreen() == Elite::DockedScreen::Status, L"for the status screen");
    rig.Host().Click(Elite::MarketClick{.control = Elite::MarketControl::Buy});
    Assert::IsFalse(rig.Host().TakeClick().has_value(), L"a click with the market closed is dropped");
    Play(rig, "key F2; wait 0.2");
    Assert::AreEqual(stock, rig.Host().ShownMarket()->stock[FOOD], L"the stock is not rolled again: what was sold back is on sale");
  }
};

} // namespace GameLogicTests
