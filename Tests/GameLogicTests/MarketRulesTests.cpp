#include "pch.h"

#include "Market.h"

#include <optional>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// Commodities, by their place in the market's order.
constexpr std::uint8_t FOOD = 0;
constexpr std::uint8_t SLAVES = 3;
constexpr std::uint8_t NARCOTICS = 6;
constexpr std::uint8_t FIREARMS = 10;
constexpr std::uint8_t GOLD = 13;
constexpr std::uint8_t PLATINUM = 14;
constexpr std::uint8_t GEM_STONES = 15;
constexpr std::uint8_t ALIEN_ITEMS = 16;

// A lot of _commodity with the hold and the market as given, a clean commander and 1,000.0 credits.
[[nodiscard]] Elite::CargoLot Lot(std::uint8_t _commodity, std::uint8_t _held, std::uint8_t _onSale, std::uint8_t _usedTonnes)
{
  return Elite::CargoLot{
    .commodity = _commodity, .held = _held, .onSale = _onSale, .usedTonnes = _usedTonnes, .legalStatus = 0, .creditsTenths = 10'000};
}

// A word, widened as the suite compares words.
[[nodiscard]] std::uint32_t Wide(std::uint16_t _word) noexcept
{
  return _word;
}

} // namespace

// The market's rules (D53, D74), by known answers: what the original routines decide, with no state and no screen (ADR-043).
// MarketAnswerTests checks their prices over every market.
TEST_CLASS(MarketRulesTests)
{
public:
  // An item sells back for its price less one and less a 32nd of it, the 32nd halved until it is below 100.
  TEST_METHOD(ResaleIsAThirtySecondLessAndATenth)
  {
    Assert::AreEqual(std::uint32_t{0}, Wide(Elite::ResaleTenths(0)), L"nothing for nothing");
    Assert::AreEqual(std::uint32_t{0}, Wide(Elite::ResaleTenths(1)), L"a tenth");
    Assert::AreEqual(std::uint32_t{96}, Wide(Elite::ResaleTenths(100)), L"10.0 credits: 3 and 1 off");
    Assert::AreEqual(std::uint32_t{3149}, Wide(Elite::ResaleTenths(3200)), L"a 32nd of 100, halved to 50");
    Assert::AreEqual(std::uint32_t{65471}, Wide(Elite::ResaleTenths(0xFFFF)), L"the most a word holds: 2047 halved to 63");
  }

  // The market prices by the system's tech level up to 9, and in this build no commodity's price moves with it.
  TEST_METHOD(TheTechLevelIsCappedAtNine)
  {
    Assert::AreEqual(std::uint8_t{0}, Elite::MarketTechLevel(0));
    Assert::AreEqual(std::uint8_t{9}, Elite::MarketTechLevel(9));
    Assert::AreEqual(std::uint8_t{9}, Elite::MarketTechLevel(10));
    Assert::AreEqual(std::uint8_t{9}, Elite::MarketTechLevel(0xFF));
    const Elite::CommodityPrices low = Elite::PricesOf(FOOD, Elite::MarketPlace{.economy = 0, .government = 0, .techLevel = 0});
    const Elite::CommodityPrices high = Elite::PricesOf(FOOD, Elite::MarketPlace{.economy = 0, .government = 0, .techLevel = 15});
    Assert::AreEqual(std::uint32_t{22}, Wide(low.buyTenths), L"food, rich industrial under anarchy");
    Assert::AreEqual(std::uint32_t{21}, Wide(low.sellTenths));
    Assert::AreEqual(Wide(low.buyTenths), Wide(high.buyTenths), L"every tech-level factor is 0");
  }

  // Credits are paid only when there are enough, and earnings wrap at 32 bits, as the original's two-word sum wraps them.
  TEST_METHOD(CreditsArePaidWhenEnoughAndWrapWhenEarned)
  {
    Assert::IsTrue(Elite::CreditsAfterPaying(100, 100) == std::optional<std::uint32_t>{0}, L"the last tenth");
    Assert::IsFalse(Elite::CreditsAfterPaying(99, 100).has_value(), L"a tenth short");
    Assert::IsTrue(Elite::CreditsAfterPaying(0xFFFF'FFFF, 0x1'0000) == std::optional<std::uint32_t>{0xFFFE'FFFF}, L"across the words");
    Assert::AreEqual(std::uint32_t{0x1'0005}, Elite::CreditsAfterEarning(0xFFFF, 6), L"a carry into the high word");
    Assert::AreEqual(std::uint32_t{1}, Elite::CreditsAfterEarning(0xFFFF'FFFF, 2), L"past 32 bits");
  }

  // The hold is 20 tonnes, or 35 with the extension; gold, platinum and gem-stones are weighed apart from it.
  TEST_METHOD(TheHoldWeighsAllButThePreciousInTonnes)
  {
    Assert::AreEqual(std::uint8_t{20}, Elite::CargoBayTonnes(false));
    Assert::AreEqual(std::uint8_t{35}, Elite::CargoBayTonnes(true));
    for (std::uint8_t commodity = 0; commodity < Elite::COMMODITY_KINDS; ++commodity)
    {
      const bool precious = commodity == GOLD || commodity == PLATINUM || commodity == GEM_STONES;
      Assert::AreEqual(!precious, Elite::WeighedInTonnes(commodity), L"precious or in tonnes");
    }
  }

  // Selling slaves, narcotics or firearms adds 1 to the legal status in 8 bits, and a sum of 0 leaves it as it was, so a
  // commander at FFh stays there.
  TEST_METHOD(ContrabandRaisesTheLegalStatus)
  {
    Assert::AreEqual(std::uint8_t{1}, Elite::LegalStatusAfterSelling(0, SLAVES));
    Assert::AreEqual(std::uint8_t{8}, Elite::LegalStatusAfterSelling(7, NARCOTICS));
    Assert::AreEqual(std::uint8_t{0x41}, Elite::LegalStatusAfterSelling(0x40, FIREARMS));
    Assert::AreEqual(std::uint8_t{5}, Elite::LegalStatusAfterSelling(5, FOOD), L"no penalty");
    Assert::AreEqual(std::uint8_t{0xFF}, Elite::LegalStatusAfterSelling(0xFF, SLAVES), L"the sum wraps to 0, which is not stored");
  }

  // What the market refuses before a quantity is asked for, in the order it asks.
  TEST_METHOD(TheMarketRefusesBeforeTheQuantity)
  {
    using Elite::CargoRefusal;
    Assert::IsTrue(Elite::RefuseToBuy(Lot(FOOD, 0, 0, 0), false) == CargoRefusal::NothingOnSale);
    Assert::IsTrue(Elite::RefuseToBuy(Lot(ALIEN_ITEMS, 0, 5, 0), false) == CargoRefusal::NotForSale);
    Assert::IsTrue(Elite::RefuseToBuy(Lot(GOLD, 250, 5, 20), false) == CargoRefusal::FullToCapacity);
    Assert::IsTrue(Elite::RefuseToBuy(Lot(GOLD, 249, 5, 20), false) == CargoRefusal::None, L"a full hold leaves gold");
    Assert::IsTrue(Elite::RefuseToBuy(Lot(FOOD, 0, 5, 20), false) == CargoRefusal::BayFull);
    Assert::IsTrue(Elite::RefuseToBuy(Lot(FOOD, 0, 5, 20), true) == CargoRefusal::None, L"the extension's room");
    Assert::IsTrue(Elite::RefuseToBuy(Lot(FOOD, 0, 5, 21), false) == CargoRefusal::None, L"only a hold exactly full");
    Assert::IsTrue(Elite::RefuseToSell(Lot(FOOD, 0, 5, 0)) == CargoRefusal::NothingToSell);
    Assert::IsTrue(Elite::RefuseToSell(Lot(ALIEN_ITEMS, 1, 0, 1)) == CargoRefusal::None, L"alien items sell");
  }

  // A purchase: refused when the market has less, when the hold cannot carry it, the tonnes added in 8 bits, or when the
  // credits fall short; otherwise the hold, the market, the tonnes and the credits move.
  TEST_METHOD(CargoIsBoughtWithinTheHoldAndTheCredits)
  {
    using Elite::CargoRefusal;
    Assert::IsTrue(Elite::CargoBought(Lot(FOOD, 0, 5, 0), false, 6, 10).refusal == CargoRefusal::NotEnoughOnSale);
    Assert::IsTrue(Elite::CargoBought(Lot(FOOD, 0, 50, 15), false, 6, 10).refusal == CargoRefusal::CannotCarry, L"21 of 20");
    Assert::IsTrue(Elite::CargoBought(Lot(FOOD, 0, 50, 15), false, 5, 10).refusal == CargoRefusal::None, L"20 of 20");
    Assert::IsTrue(Elite::CargoBought(Lot(FOOD, 0, 250, 10), false, 250, 0).refusal == CargoRefusal::None,
                   L"260 tonnes are 4 in 8 bits, as the original adds them");
    Assert::IsTrue(Elite::CargoBought(Lot(GOLD, 240, 50, 20), false, 11, 10).refusal == CargoRefusal::CannotCarry, L"251 kg");
    Assert::IsTrue(Elite::CargoBought(Lot(FOOD, 0, 50, 0), false, 5, 2001).refusal == CargoRefusal::NotEnoughCredits);

    const Elite::CargoTrade food = Elite::CargoBought(Lot(FOOD, 2, 50, 3), false, 5, 2000);
    Assert::IsTrue(food.refusal == CargoRefusal::None);
    Assert::AreEqual(std::uint8_t{7}, food.lot.held);
    Assert::AreEqual(std::uint8_t{45}, food.lot.onSale);
    Assert::AreEqual(std::uint8_t{8}, food.lot.usedTonnes);
    Assert::AreEqual(std::uint32_t{0}, food.lot.creditsTenths, L"5 at 200.0, every credit");

    const Elite::CargoTrade gold = Elite::CargoBought(Lot(GOLD, 240, 50, 3), false, 10, 1);
    Assert::AreEqual(std::uint8_t{250}, gold.lot.held);
    Assert::AreEqual(std::uint8_t{3}, gold.lot.usedTonnes, L"kilograms, outside the tonnes");
  }

  // A sale: refused when the hold has less; otherwise the hold empties into the market, which holds at FFh, the tonnes
  // fall for what is weighed in them, alien items too, the legal status takes the penalty, and the credits are paid.
  TEST_METHOD(CargoIsSoldIntoTheMarket)
  {
    using Elite::CargoRefusal;
    Assert::IsTrue(Elite::CargoSold(Lot(FOOD, 4, 0, 4), 5, 10).refusal == CargoRefusal::NotEnoughHeld);

    const Elite::CargoTrade slaves = Elite::CargoSold(Lot(SLAVES, 10, 250, 10), 10, 30);
    Assert::IsTrue(slaves.refusal == CargoRefusal::None);
    Assert::AreEqual(std::uint8_t{0}, slaves.lot.held);
    Assert::AreEqual(std::uint8_t{0xFF}, slaves.lot.onSale, L"260 held at FFh");
    Assert::AreEqual(std::uint8_t{0}, slaves.lot.usedTonnes);
    Assert::AreEqual(std::uint8_t{1}, slaves.lot.legalStatus);
    Assert::AreEqual(std::uint32_t{10'300}, slaves.lot.creditsTenths);

    Assert::AreEqual(std::uint8_t{2}, Elite::CargoSold(Lot(ALIEN_ITEMS, 3, 0, 3), 1, 10).lot.usedTonnes, L"alien items are tonnes");
    Assert::AreEqual(std::uint8_t{3}, Elite::CargoSold(Lot(GEM_STONES, 3, 0, 3), 1, 10).lot.usedTonnes, L"gem-stones are grams");
  }
};

} // namespace GameLogicTests
