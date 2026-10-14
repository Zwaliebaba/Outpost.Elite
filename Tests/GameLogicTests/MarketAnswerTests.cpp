#include "pch.h"

#include "AnswerFile.h"
#include "Market.h"
#include "Maths.h"
#include "ReferenceRig.h"

#include <array>
#include <format>
#include <optional>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// The markets' known answers, from the repository's root. The file's header documents its format and states the count.
constexpr std::string_view MARKET_ANSWERS_FILE = "Tests/GameLogicTests/MarketAnswers.txt";
constexpr std::size_t ECONOMIES = 8;
constexpr std::size_t GOVERNMENTS = 8;
// The tech levels the prices are computed at: none, the first, the 9 that ComputeMarketPrices caps it at, and the most a
// nibble holds. The galaxies give 0 to 11. In this build the tech level changes no price: every commodity's tech-level
// factor in productTradeRecords is 0 (Symbols.tsv), so these four check that it still reads the level, and nothing more.
constexpr auto TECH_LEVELS = std::to_array<std::uint8_t>({0, 1, 9, 15});
constexpr std::size_t COMMODITIES = 17;
// A commodity's entry in screenPrices: its price, then its resale price, a word each.
constexpr std::size_t PRICE_ENTRY_BYTES = 4;
// The random states the quantities are rolled from: the loaded commander's and the next 255 NextRandom makes, and three edges.
constexpr std::size_t WALKED_RANDOM_STATES = 256;
constexpr std::array<std::array<std::uint16_t, 3>, 3> EDGE_RANDOM_STATES = {
  {{0, 0, 0}, {0xFFFF, 0xFFFF, 0xFFFF}, {0x8000, 0x7FFF, 0x0001}}};

// The prices ComputeMarketPrices gives the current system's market for _economy, _government and _techLevel: each
// commodity's price and resale price, as screenPrices holds them, in tenths of a credit.
[[nodiscard]] std::string Prices(Elite::GameState& _state, std::uint8_t _economy, std::uint8_t _government, std::uint8_t _techLevel)
{
  _state.Data().commander.currentEconomy = _economy;
  _state.Data().commander.currentGovernment = _government;
  _state.Data().commander.currentTechLevel = _techLevel;
  Elite::ComputeMarketPrices(_state);
  std::string line = std::format("prices {} {} {}", _economy, _government, _techLevel);
  const std::span<const Elite::Field<std::uint8_t>> prices = Elite::BytesOf(_state.Data().menus.screenPrices);
  for (std::size_t commodity = 0; commodity < COMMODITIES; ++commodity)
  {
    const std::size_t at = commodity * PRICE_ENTRY_BYTES;
    line += std::format(" {}/{}", Elite::WordIn(prices, at), Elite::WordIn(prices, at + 2));
  }
  return line;
}

// The same line from the market's rules (PricesOf), with no state: each commodity's buy and sell price at _economy,
// _government and _techLevel.
[[nodiscard]] std::string RulePrices(std::uint8_t _economy, std::uint8_t _government, std::uint8_t _techLevel)
{
  const Elite::MarketPlace place{.economy = _economy, .government = _government, .techLevel = _techLevel};
  std::string line = std::format("prices {} {} {}", _economy, _government, _techLevel);
  for (std::uint8_t commodity = 0; commodity < Elite::COMMODITY_KINDS; ++commodity)
  {
    const Elite::CommodityPrices prices = Elite::PricesOf(commodity, place);
    line += std::format(" {}/{}", prices.buyTenths, prices.sellTenths);
  }
  return line;
}

// The quantities on sale ShowBuyCargoScreen rolls the first time it shows after an arrival, randomState0-2 being _random:
// StartMarketRandom, then RollQuantityOnSale for each commodity in turn, '-' for none.
[[nodiscard]] std::string Quantities(Elite::GameState& _state, const std::array<std::uint16_t, 3>& _random)
{
  _state.Data().commander.randomState0 = _random[0];
  _state.Data().commander.randomState1 = _random[1];
  _state.Data().commander.randomState2 = _random[2];
  Elite::StartMarketRandom(_state);
  std::string line = std::format("quantities {:04x} {:04x} {:04x}", _random[0], _random[1], _random[2]);
  for (std::size_t commodity = 0; commodity < COMMODITIES; ++commodity)
  {
    const std::optional<std::uint8_t> onSale = Elite::RollQuantityOnSale(_state);
    line += onSale ? std::format(" {}", *onSale) : std::string(" -");
  }
  return line;
}

// The quantities' answers, onto _answers: the loaded commander's random state and the 255 NextRandom steps it through, then the
// edges.
void AppendQuantities(Elite::GameState& _state, std::vector<std::string>& _answers)
{
  std::array<std::uint16_t, 3> random = {_state.Data().commander.randomState0, _state.Data().commander.randomState1,
                                         _state.Data().commander.randomState2};
  for (std::size_t walked = 0; walked < WALKED_RANDOM_STATES; ++walked)
  {
    _answers.push_back(Quantities(_state, random));
    _state.Data().commander.randomState0 = random[0];
    _state.Data().commander.randomState1 = random[1];
    _state.Data().commander.randomState2 = random[2];
    (void)Elite::NextRandom(_state);
    random = {_state.Data().commander.randomState0, _state.Data().commander.randomState1, _state.Data().commander.randomState2};
  }
  for (const std::array<std::uint16_t, 3>& edge : EDGE_RANDOM_STATES)
  {
    _answers.push_back(Quantities(_state, edge));
  }
}

} // namespace

// Known answers per subsystem (D33): the markets, from the faithful code while the corpus still vouched for it.
TEST_CLASS(MarketAnswerTests)
{
public:
  // The prices for every economy and government at four tech levels, 256 markets, and the quantities on sale rolled from
  // 259 random states, as the game's own routines compute them, are the answers recorded in MARKET_ANSWERS_FILE: 515
  // lines. The quantities depend on the random state alone; no economy enters them.
  TEST_METHOD(EveryMarketIsTheOriginals)
  {
    ReferenceRig rig("MarketAnswers");
    Elite::GameState& state = rig.State();
    std::vector<std::string> answers;
    for (std::size_t economy = 0; economy < ECONOMIES; ++economy)
    {
      for (std::size_t government = 0; government < GOVERNMENTS; ++government)
      {
        for (const std::uint8_t techLevel : TECH_LEVELS)
        {
          answers.push_back(Prices(state, static_cast<std::uint8_t>(economy), static_cast<std::uint8_t>(government), techLevel));
        }
      }
    }

    AppendQuantities(state, answers);
    CheckSubsystemAnswers(MARKET_ANSWERS_FILE, answers,
                          ECONOMIES * GOVERNMENTS * TECH_LEVELS.size() + WALKED_RANDOM_STATES + EDGE_RANDOM_STATES.size());
  }

  // The market's rules give the same 256 markets with no state (D53, D74): PricesOf, for each economy, government and tech
  // level, is the price lines' answer, the quantities' lines being the routines' as above.
  TEST_METHOD(TheRulesPriceEveryMarketAsRecorded)
  {
    ReferenceRig rig("MarketRuleAnswers");
    Elite::GameState& state = rig.State();
    std::vector<std::string> answers;
    for (std::size_t economy = 0; economy < ECONOMIES; ++economy)
    {
      for (std::size_t government = 0; government < GOVERNMENTS; ++government)
      {
        for (const std::uint8_t techLevel : TECH_LEVELS)
        {
          answers.push_back(RulePrices(static_cast<std::uint8_t>(economy), static_cast<std::uint8_t>(government), techLevel));
        }
      }
    }
    AppendQuantities(state, answers);
    CheckSubsystemAnswers(MARKET_ANSWERS_FILE, answers,
                          ECONOMIES * GOVERNMENTS * TECH_LEVELS.size() + WALKED_RANDOM_STATES + EDGE_RANDOM_STATES.size());
  }
};

} // namespace GameLogicTests
