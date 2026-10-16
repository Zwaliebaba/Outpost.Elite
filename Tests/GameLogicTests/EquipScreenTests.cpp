#include "pch.h"

#include "EquipScreen.h"
#include "GameRig.h"
#include "Market.h"
#include "Replay.h"

#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The list's rows the tests name, from 1.
constexpr std::uint8_t FUEL = 1;
constexpr std::uint8_t MISSILE = 2;
constexpr std::uint8_t CARGO_BAY = 3;
constexpr std::uint8_t ECM = 4;
constexpr std::uint8_t PULSE_LASER = 5;
constexpr std::uint8_t BEAM_LASER = 6;
constexpr std::uint8_t FUEL_SCOOPS = 7;
constexpr std::uint8_t MINING_LASER = 13;

// The mounts, as the game numbers them.
constexpr std::uint8_t FORE = 0;
constexpr std::uint8_t AFT = 1;
constexpr std::uint8_t RIGHT = 2;
constexpr std::uint8_t LEFT = 3;

// The laser types.
constexpr std::uint8_t PULSE = 0;
constexpr std::uint8_t BEAM = 1;

// A shipyard selling all 14 items, row r for r × 100.0 credits and fuel for 2.0 a light year, to a ship with 3.5 light years of
// fuel, three missiles and a pulse laser fore, and 5000.0 credits.
[[nodiscard]] Elite::EquipGoods EvenShipyard()
{
  Elite::EquipGoods goods;
  goods.rows = Elite::EQUIPMENT_LIST_ROWS;
  for (std::uint8_t row = 1; row <= Elite::EQUIPMENT_LIST_ROWS; ++row)
  {
    goods.priceTenths[std::size_t{row} - 1] = static_cast<std::uint16_t>(row * 1000);
  }
  goods.priceTenths[FUEL - 1] = 20;
  goods.fitting.counts[FUEL - 1] = 0x80;
  goods.fitting.counts[MISSILE - 1] = 3;
  goods.fitting.counts[PULSE_LASER - 1] = 1;
  goods.fitting.laserMountsFitted = 0x01;
  goods.mounts = Elite::LaserMounts{.fitted = 0x01, .types = 0x00};
  goods.creditsTenths = 50000;
  return goods;
}

// _screen with _action applied over _goods.
[[nodiscard]] Elite::EquipStep Step(const Elite::EquipScreen& _screen, Elite::EquipAction _action, const Elite::EquipGoods& _goods,
                                    std::uint8_t _index = 0)
{
  return Elite::StepEquip(_screen, Elite::EquipInput{.action = _action, .index = _index}, _goods);
}

// A screen on row _row, from 1, the picker closed.
[[nodiscard]] Elite::EquipScreen On(std::uint8_t _row)
{
  return Elite::EquipScreen{.selected = static_cast<std::uint8_t>(_row - 1), .picker = std::nullopt, .message = {}};
}

// The notice B or S on row _row of _goods leaves.
[[nodiscard]] Elite::EquipNotice Notice(Elite::EquipAction _action, std::uint8_t _row, const Elite::EquipGoods& _goods)
{
  return Step(On(_row), _action, _goods).screen.message.notice;
}

// The trade _step left, which must be there.
[[nodiscard]] Elite::EquipTrade Traded(const Elite::EquipStep& _step)
{
  Assert::IsTrue(_step.traded.has_value(), L"a trade");
  return _step.traded.value_or(Elite::EquipTrade{});
}

// Whether _input is _action.
[[nodiscard]] bool IsKey(const std::optional<Elite::EquipInput>& _input, Elite::EquipAction _action)
{
  return _input && _input->action == _action;
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

// The shipyard, D02 and its mount picker D03 (D82, D103 to D107): its rules over ADR-043's, as values, and the screen as the game
// runs it.
TEST_CLASS(EquipScreenTests)
{
public:
  // ↑ and ↓ stop at the list's ends, and a click selects a row within it.
  TEST_METHOD(TheRowStaysInTheList)
  {
    Elite::EquipGoods goods = EvenShipyard();
    goods.rows = 8;
    Assert::AreEqual(std::uint8_t{0}, Step(On(1), Elite::EquipAction::Up, goods).screen.selected, L"the first row");
    Assert::AreEqual(std::uint8_t{7}, Step(On(8), Elite::EquipAction::Down, goods).screen.selected, L"the last row");
    Assert::AreEqual(std::uint8_t{7}, Step(On(1), Elite::EquipAction::Select, goods, 12).screen.selected, L"a click past the end");
    Assert::AreEqual(std::uint8_t{4}, Step(On(1), Elite::EquipAction::Select, goods, 4).screen.selected, L"a click on a row");
  }

  // Fuel fills the tank at the tank's price, a missile is one more, and anything else is fitted: each pays and leaves its strip.
  TEST_METHOD(BuyingPaysAndFits)
  {
    const Elite::EquipGoods goods = EvenShipyard();
    const Elite::EquipStep fuel = Step(On(FUEL), Elite::EquipAction::Buy, goods);
    Assert::IsTrue(fuel.traded.has_value(), L"fuel bought");
    const std::uint32_t fuelCost = Elite::FuelPriceTenths(0x80, 20);
    Assert::AreEqual(Elite::FUEL_FULL, Traded(fuel).counts[FUEL - 1], L"the tank full");
    Assert::AreEqual(50000u - fuelCost, Traded(fuel).creditsTenths, L"the tank's price");
    Assert::IsTrue(fuel.screen.message.notice == Elite::EquipNotice::Bought && fuel.screen.message.fuelTenths == 70, L"7.0 light years");

    const Elite::EquipStep missile = Step(On(MISSILE), Elite::EquipAction::Buy, goods);
    Assert::AreEqual(std::uint8_t{4}, Traded(missile).counts[MISSILE - 1], L"a fourth missile");
    Assert::AreEqual(50000u - 2000u, Traded(missile).creditsTenths, L"its price");

    const Elite::EquipStep ecm = Step(On(ECM), Elite::EquipAction::Buy, goods);
    Assert::AreEqual(std::uint8_t{1}, Traded(ecm).counts[ECM - 1], L"ECM fitted");
    Assert::IsTrue(ecm.screen.message.notice == Elite::EquipNotice::Bought && ecm.screen.message.tenths == 4000, L"its strip");
  }

  // Selling takes one back at ResaleTenths of the price.
  TEST_METHOD(SellingPaysTheSellPrice)
  {
    const Elite::EquipStep missile = Step(On(MISSILE), Elite::EquipAction::Sell, EvenShipyard());
    Assert::AreEqual(std::uint8_t{2}, Traded(missile).counts[MISSILE - 1], L"a missile fewer");
    Assert::AreEqual(50000u + Elite::ResaleTenths(2000), Traded(missile).creditsTenths, L"its sell price");
    Assert::IsTrue(missile.screen.message.notice == Elite::EquipNotice::Sold, L"its strip");
    Assert::AreEqual(0u, Elite::SellingPriceTenths(EvenShipyard(), FUEL), L"fuel has no sell price");
  }

  // Each refusal of D11's Equipment column, and the two the drawing does not show, with their figures.
  TEST_METHOD(EveryRefusalHasItsStrip)
  {
    Elite::EquipGoods goods = EvenShipyard();
    goods.fitting.counts[FUEL - 1] = 0xFF;
    Assert::IsTrue(Notice(Elite::EquipAction::Buy, FUEL, goods) == Elite::EquipNotice::FuelFull, L"the tanks full");
    goods = EvenShipyard();
    goods.fitting.missionNumber = 1;
    Assert::IsTrue(Notice(Elite::EquipAction::Buy, FUEL, goods) == Elite::EquipNotice::NoFuel, L"the first mission");
    goods = EvenShipyard();
    goods.fitting.counts[MISSILE - 1] = 4;
    Assert::IsTrue(Notice(Elite::EquipAction::Buy, MISSILE, goods) == Elite::EquipNotice::FourMissiles, L"four missiles");
    goods = EvenShipyard();
    goods.fitting.counts[ECM - 1] = 1;
    Assert::IsTrue(Notice(Elite::EquipAction::Buy, ECM, goods) == Elite::EquipNotice::AlreadyFitted, L"ECM fitted");
    goods = EvenShipyard();
    Assert::IsTrue(Notice(Elite::EquipAction::Buy, MINING_LASER, goods) == Elite::EquipNotice::ScoopsNeeded, L"no scoops");
    goods.fitting.counts[CARGO_BAY - 1] = 1;
    goods.fitting.cargoUsedTonnes = 26;
    const Elite::EquipMessage cargo = Step(On(CARGO_BAY), Elite::EquipAction::Sell, goods).screen.message;
    Assert::IsTrue(cargo.notice == Elite::EquipNotice::TooMuchCargo && cargo.tonnes == 6, L"unload 6 t first");
    goods = EvenShipyard();
    goods.fitting.counts[FUEL_SCOOPS - 1] = 1;
    goods.fitting.counts[MINING_LASER - 1] = 1;
    Assert::IsTrue(Notice(Elite::EquipAction::Sell, FUEL_SCOOPS, goods) == Elite::EquipNotice::MiningLaserFitted, L"the scoops kept");
    goods = EvenShipyard();
    goods.creditsTenths = 412;
    const Elite::EquipMessage credits = Step(On(ECM), Elite::EquipAction::Buy, goods).screen.message;
    Assert::IsTrue(credits.notice == Elite::EquipNotice::NotEnoughCredits && credits.tenths == 4000 && credits.creditsTenths == 412,
                   L"4000 needed, 412 available");
    Assert::IsTrue(Notice(Elite::EquipAction::Sell, FUEL, goods) == Elite::EquipNotice::FuelNotBought, L"fuel never sold");
    Assert::IsTrue(Notice(Elite::EquipAction::Sell, ECM, goods) == Elite::EquipNotice::NoneFitted, L"no ECM to sell");
    goods = EvenShipyard();
    goods.mounts = Elite::LaserMounts{.fitted = 0x0F, .types = 0x55};
    Assert::IsTrue(Notice(Elite::EquipAction::Buy, BEAM_LASER, goods) == Elite::EquipNotice::FourLasers, L"a beam laser on every mount");
    Assert::IsTrue(Step(On(PULSE_LASER), Elite::EquipAction::Buy, goods).screen.picker.has_value(),
                   L"a pulse laser can still go where a beam laser is");
  }

  // B on a laser opens the picker and trades nothing; a free mount, by the picker's key, takes it and pays its price.
  TEST_METHOD(ALaserGoesOnTheMountPicked)
  {
    const Elite::EquipGoods goods = EvenShipyard();
    const Elite::EquipStep open = Step(On(BEAM_LASER), Elite::EquipAction::Buy, goods);
    Assert::IsFalse(open.traded.has_value(), L"nothing bought yet");
    Assert::IsTrue(open.screen.picker && open.screen.picker->row == BEAM_LASER && !open.screen.picker->selling, L"the picker, buying");
    const std::optional<Elite::EquipInput> rear = Elite::EquipKey(0x05, true);
    Assert::IsTrue(rear && rear->action == Elite::EquipAction::Mount && rear->index == AFT, L"4 is the rear, the original's aft");
    const Elite::EquipStep fitted = Elite::StepEquip(open.screen, rear.value_or(Elite::EquipInput{}), goods);
    Assert::IsTrue(fitted.traded.has_value(), L"fitted");
    Assert::AreEqual(std::uint8_t{1}, Traded(fitted).counts[BEAM_LASER - 1], L"a beam laser");
    Assert::AreEqual(std::uint8_t{0x03}, Traded(fitted).mounts.fitted, L"fore and aft");
    Assert::AreEqual(BEAM, Elite::LaserTypeOnMount(Traded(fitted).mounts, AFT), L"a beam laser aft");
    Assert::AreEqual(50000u - 6000u, Traded(fitted).creditsTenths, L"its price");
    Assert::IsFalse(fitted.screen.picker.has_value(), L"the picker closed");
    const Elite::EquipMessage& strip = fitted.screen.message;
    Assert::IsTrue(strip.notice == Elite::EquipNotice::LaserFitted && strip.mount == AFT && !strip.tradedIn, L"its strip");
  }

  // A mount carrying another laser trades it in at its sell price (D106): the credits need cover only the difference.
  TEST_METHOD(AnOccupiedMountTradesItsLaserIn)
  {
    Elite::EquipGoods goods = EvenShipyard();
    const std::uint32_t tradeIn = Elite::ResaleTenths(5000);
    goods.creditsTenths = 6000 - tradeIn;
    const Elite::EquipStep open = Step(On(BEAM_LASER), Elite::EquipAction::Buy, goods);
    const Elite::EquipStep fitted = Step(open.screen, Elite::EquipAction::Mount, goods, FORE);
    Assert::IsTrue(fitted.traded.has_value(), L"the difference is enough");
    Assert::AreEqual(0u, Traded(fitted).creditsTenths, L"all of it spent");
    Assert::AreEqual(std::uint8_t{0}, Traded(fitted).counts[PULSE_LASER - 1], L"the pulse laser traded in");
    Assert::AreEqual(std::uint8_t{1}, Traded(fitted).counts[BEAM_LASER - 1], L"the beam laser fitted");
    Assert::AreEqual(std::uint8_t{0x01}, Traded(fitted).mounts.fitted, L"fore only");
    Assert::AreEqual(BEAM, Elite::LaserTypeOnMount(Traded(fitted).mounts, FORE), L"a beam laser fore");
    const Elite::EquipMessage& strip = fitted.screen.message;
    Assert::IsTrue(strip.tradedIn && strip.tradedRow == PULSE_LASER, L"the strip says what was traded in");

    goods.creditsTenths -= 1;
    const Elite::EquipStep refused = Step(open.screen, Elite::EquipAction::Mount, goods, FORE);
    Assert::IsFalse(refused.traded.has_value(), L"a tenth short");
    Assert::IsTrue(refused.screen.message.notice == Elite::EquipNotice::NotEnoughCredits, L"not enough credits");
    Assert::AreEqual(6000u - tradeIn, refused.screen.message.tenths, L"needed: the difference");
    Assert::IsTrue(refused.screen.picker.has_value(), L"the picker stays open");
  }

  // A mount carrying the same laser cannot take it (D107): the picker says why and stays open; Cancel closes it.
  TEST_METHOD(AMountWithTheSameLaserIsRefused)
  {
    const Elite::EquipGoods goods = EvenShipyard();
    const Elite::EquipStep open = Step(On(PULSE_LASER), Elite::EquipAction::Buy, goods);
    const Elite::EquipStep taken = Step(open.screen, Elite::EquipAction::Mount, goods, FORE);
    Assert::IsFalse(taken.traded.has_value(), L"nothing bought");
    Assert::IsTrue(taken.screen.message.notice == Elite::EquipNotice::MountTaken && taken.screen.message.mount == FORE, L"its strip");
    Assert::IsTrue(taken.screen.picker.has_value(), L"the picker stays open");
    const Elite::EquipStep canceled = Step(taken.screen, Elite::EquipAction::Cancel, goods);
    Assert::IsFalse(canceled.screen.picker.has_value() || canceled.traded.has_value(), L"closed, nothing traded");
    Assert::IsFalse(Step(On(1), Elite::EquipAction::Mount, goods, AFT).traded.has_value(), L"a mount with no picker does nothing");
  }

  // S on a laser opens the picker selling (D103): only a mount carrying it sells it.
  TEST_METHOD(ALaserIsSoldOffItsMount)
  {
    const Elite::EquipGoods goods = EvenShipyard();
    const Elite::EquipStep open = Step(On(PULSE_LASER), Elite::EquipAction::Sell, goods);
    Assert::IsTrue(open.screen.picker && open.screen.picker->selling, L"the picker, selling");
    const Elite::EquipStep empty = Step(open.screen, Elite::EquipAction::Mount, goods, LEFT);
    Assert::IsTrue(empty.screen.message.notice == Elite::EquipNotice::MountWithout && empty.screen.picker, L"no pulse laser left");
    const Elite::EquipStep sold = Step(open.screen, Elite::EquipAction::Mount, goods, FORE);
    Assert::AreEqual(std::uint8_t{0}, Traded(sold).counts[PULSE_LASER - 1], L"the pulse laser sold");
    Assert::AreEqual(std::uint8_t{0}, Traded(sold).mounts.fitted, L"no laser on any mount");
    Assert::AreEqual(50000u + Elite::ResaleTenths(5000), Traded(sold).creditsTenths, L"its sell price");
    Assert::IsTrue(sold.screen.message.notice == Elite::EquipNotice::LaserSold && sold.screen.message.mount == FORE, L"its strip");
  }

  // The view: each row's prices, count and mounts, the buttons, and the picker's mounts.
  TEST_METHOD(TheViewShowsTheListAndThePicker)
  {
    Elite::EquipGoods goods = EvenShipyard();
    goods.mounts = Elite::LaserMounts{.fitted = 0x05, .types = 0x00};
    goods.fitting.counts[PULSE_LASER - 1] = 2;
    const Elite::EquipView list = Elite::EquipViewOf(On(PULSE_LASER), goods);
    Assert::AreEqual(std::uint8_t{14}, list.rows, L"every row");
    Assert::AreEqual(std::uint16_t{0}, list.items[FUEL - 1].sellTenths, L"fuel's sell price");
    Assert::AreEqual(std::uint8_t{35}, list.fuelTenths, L"3.5 light years, to a tenth");
    Assert::AreEqual(std::uint8_t{3}, list.items[MISSILE - 1].count, L"three missiles");
    Assert::AreEqual(static_cast<std::uint8_t>(Elite::MOUNT_FORE | Elite::MOUNT_RIGHT), list.items[PULSE_LASER - 1].mounts,
                     L"pulse lasers fore and right");
    Assert::IsTrue(list.buyAllowed && list.sellAllowed, L"a pulse laser can be bought and sold");
    Assert::IsFalse(list.picker.open, L"no picker");

    const Elite::EquipView picking = Elite::EquipViewOf(Step(On(PULSE_LASER), Elite::EquipAction::Sell, goods).screen, goods);
    Assert::IsTrue(picking.picker.open && picking.picker.selling, L"the picker selling");
    Assert::IsTrue(picking.picker.mounts[FORE].allowed && picking.picker.mounts[RIGHT].allowed, L"the mounts carrying it lit");
    Assert::IsFalse(picking.picker.mounts[AFT].allowed || picking.picker.mounts[LEFT].allowed, L"the others dimmed");
    Assert::IsTrue(picking.picker.mounts[RIGHT].laser && picking.picker.mounts[RIGHT].type == PULSE, L"a pulse laser right");

    goods.creditsTenths = 0;
    goods.priceTenths[BEAM_LASER - 1] = 4000;
    const Elite::EquipView broke = Elite::EquipViewOf(On(BEAM_LASER), goods);
    Assert::IsTrue(broke.buyAllowed, L"a beam laser can still be bought, trading a pulse laser in");
    goods.mounts.fitted = 0;
    Assert::IsFalse(Elite::EquipViewOf(On(BEAM_LASER), goods).buyAllowed, L"not with nothing to trade in");
  }

  // The footer's keys: ↑ ↓, B and S, and in the picker 1 to 4 (D104) and Esc.
  TEST_METHOD(TheKeysAreTheFootersKeys)
  {
    Assert::IsTrue(IsKey(Elite::EquipKey(0x48, false), Elite::EquipAction::Up), L"\u2191");
    Assert::IsTrue(IsKey(Elite::EquipKey(0x50, false), Elite::EquipAction::Down), L"\u2193");
    Assert::IsTrue(IsKey(Elite::EquipKey(0x30, false), Elite::EquipAction::Buy), L"B");
    Assert::IsTrue(IsKey(Elite::EquipKey(0x1F, false), Elite::EquipAction::Sell), L"S");
    Assert::IsFalse(Elite::EquipKey(0x02, false).has_value(), L"1 without the picker");
    Assert::IsFalse(Elite::EquipKey(0x01, false).has_value(), L"Esc without the picker closes the screen");
    Assert::IsTrue(IsKey(Elite::EquipKey(0x01, true), Elite::EquipAction::Cancel), L"Esc in the picker");
    const std::array<std::uint8_t, 4> mounts = {FORE, LEFT, RIGHT, AFT};
    for (std::uint8_t key = 0; key < 4; ++key)
    {
      const std::optional<Elite::EquipInput> input = Elite::EquipKey(static_cast<std::uint8_t>(0x02 + key), true);
      Assert::IsTrue(input && input->action == Elite::EquipAction::Mount && input->index == mounts[key],
                     L"1 front, 2 left, 3 right, 4 rear");
    }
  }

  // F3 opens the shipyard at Lave, 8 items at tech level 6; B buys by key and a click, F3 keeps it open, and F6 closes it.
  TEST_METHOD(TheShipyardRunsDocked)
  {
    GameRig rig("Shipyard");
    Play(rig, TO_THE_STATUS_SCREEN);
    Play(rig, "key F3; wait 0.2");
    const Elite::EquipView* view = rig.Host().ShownEquip();
    Assert::IsTrue(view != nullptr, L"F3 opens the shipyard");
    Assert::AreEqual(std::uint8_t{8}, view->rows, L"Lave's 8 items");
    Assert::AreEqual(std::uint8_t{6}, view->techLevel, L"tech level 6");
    Assert::IsTrue(view->systemName == "LAVE" && !view->dodo, L"Lave's Coriolis");

    const std::uint32_t credits = rig.State().Data().commander.creditsTenths;
    Play(rig, "key Down; wait 0.05\nkey b; wait 0.05");
    Assert::AreEqual(std::uint8_t{4}, rig.State().Data().commander.missileCount, L"a fourth missile by keys");
    const std::uint16_t missile = rig.Host().ShownEquip()->items[MISSILE - 1].buyTenths;
    Assert::AreEqual(credits - missile, rig.State().Data().commander.creditsTenths, L"its price");

    rig.Host().Click(Elite::EquipClick{.control = Elite::EquipControl::Sell});
    Play(rig, "wait 0.05\nkey F3; wait 0.05");
    Assert::AreEqual(std::uint8_t{3}, rig.State().Data().commander.missileCount, L"sold back by a click");
    Assert::IsTrue(rig.Host().ShownEquip() != nullptr, L"F3, its own key, keeps it open");

    rig.Host().Click(Elite::EquipClick{.control = Elite::EquipControl::Row, .index = PULSE_LASER - 1});
    Play(rig, "wait 0.05\nkey s; wait 0.05\nkey Escape; wait 0.05");
    Assert::IsTrue(rig.Host().ShownEquip() != nullptr && !rig.Host().ShownEquip()->picker.open, L"Esc closes the picker, not the screen");
    Assert::AreEqual(std::uint8_t{1}, rig.State().Data().commander.pulseLaserCount, L"nothing sold");

    Play(rig, "key F6; wait 0.3");
    Assert::IsTrue(rig.Host().ShownEquip() == nullptr, L"F6 closes it");
    rig.Host().Click(Elite::EquipClick{.control = Elite::EquipControl::Buy});
    Assert::IsFalse(rig.Host().TakeEquipClick().has_value(), L"a click with the shipyard closed is dropped");
  }
};

} // namespace GameLogicTests
