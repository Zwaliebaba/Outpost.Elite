#include "pch.h"

#include "AnswerFile.h"
#include "DataOverlay.h"
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

using Elite::DS;

/// The markets' known answers, from the repository's root. The file's header documents its format and states the count.
constexpr std::string_view MARKET_ANSWERS_FILE = "GameLogicTests/MarketAnswers.txt";
constexpr std::size_t ECONOMIES = 8;
constexpr std::size_t GOVERNMENTS = 8;
// The tech levels the prices are computed at: none, the first, the 9 that ComputeMarketPrices caps it at, and the most a
// nibble holds. The galaxies give 0 to 11. In this build the tech level changes no price: every commodity's tech-level
// factor in productTradeRecords is 0 (Symbols.tsv), so these four check that it still reads the level, and nothing more.
constexpr auto TECH_LEVELS = std::to_array<std::uint8_t>({0, 1, 9, 15});
constexpr std::size_t COMMODITIES = 17;
// The random states the quantities are rolled from: the loaded commander's and the next 255 NextRandom makes, and three edges.
constexpr std::size_t WALKED_RANDOM_STATES = 256;
constexpr std::array<std::array<std::uint16_t, 3>, 3> EDGE_RANDOM_STATES = {
  {{0, 0, 0}, {0xFFFF, 0xFFFF, 0xFFFF}, {0x8000, 0x7FFF, 0x0001}}};

// The prices ComputeMarketPrices gives the current system's market for _economy, _government and _techLevel: each
// commodity's price and resale price, as screenPrices holds them, in tenths of a credit.
[[nodiscard]] std::string Prices(Elite::GameState& _state, std::uint8_t _economy, std::uint8_t _government, std::uint8_t _techLevel)
{
  _state.Data().commanderBlock.currentEconomy = _economy;
  _state.Data().commanderBlock.currentGovernment = _government;
  _state.Data().commanderBlock.currentTechLevel = _techLevel;
  Elite::ComputeMarketPrices(_state);
  std::string line = std::format("prices {} {} {}", _economy, _government, _techLevel);
  for (std::size_t commodity = 0; commodity < COMMODITIES; ++commodity)
  {
    const std::uint16_t at = DS.screenPrices.At(commodity);
    line += std::format(" {}/{}", _state.Word(at), _state.Word(static_cast<std::uint16_t>(at + 2)));
  }
  return line;
}

// The quantities on sale ShowBuyCargoScreen rolls the first time it shows after an arrival, randomState0-2 being _random:
// StartMarketRandom, then RollQuantityOnSale for each commodity in turn, '-' for none.
[[nodiscard]] std::string Quantities(Elite::GameState& _state, const std::array<std::uint16_t, 3>& _random)
{
  _state.Data().commanderBlock.randomState0 = _random[0];
  _state.Data().commanderBlock.randomState1 = _random[1];
  _state.Data().commanderBlock.randomState2 = _random[2];
  Elite::StartMarketRandom(_state);
  std::string line = std::format("quantities {:04x} {:04x} {:04x}", _random[0], _random[1], _random[2]);
  for (std::size_t commodity = 0; commodity < COMMODITIES; ++commodity)
  {
    const std::optional<std::uint8_t> onSale = Elite::RollQuantityOnSale(_state);
    line += onSale ? std::format(" {}", *onSale) : std::string(" -");
  }
  return line;
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

    std::array<std::uint16_t, 3> random = {state.Data().commanderBlock.randomState0, state.Data().commanderBlock.randomState1,
                                           state.Data().commanderBlock.randomState2};
    for (std::size_t walked = 0; walked < WALKED_RANDOM_STATES; ++walked)
    {
      answers.push_back(Quantities(state, random));
      state.Data().commanderBlock.randomState0 = random[0];
      state.Data().commanderBlock.randomState1 = random[1];
      state.Data().commanderBlock.randomState2 = random[2];
      (void)Elite::NextRandom(state);
      random = {state.Data().commanderBlock.randomState0, state.Data().commanderBlock.randomState1,
                state.Data().commanderBlock.randomState2};
    }
    for (const std::array<std::uint16_t, 3>& edge : EDGE_RANDOM_STATES)
    {
      answers.push_back(Quantities(state, edge));
    }
    CheckSubsystemAnswers(MARKET_ANSWERS_FILE, answers,
                          ECONOMIES * GOVERNMENTS * TECH_LEVELS.size() + WALKED_RANDOM_STATES + EDGE_RANDOM_STATES.size());
  }
};

} // namespace GameLogicTests
