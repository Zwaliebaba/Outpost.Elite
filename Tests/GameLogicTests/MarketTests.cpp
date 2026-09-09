#include "pch.h"


#include "LookupTables.h"
#include "Market.h"
#include "Galaxy.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::MarketState;
using Elite::SystemSeeds;

/*
 * The sign of the economy gradient (slice 2c, the price model).
 *
 * Every one of the seventeen goods at every economy against every value of the market's random
 * byte -- 34,816 prices -- was compared against the routine that computes them, and went with the
 * oracle (M6-b-5). The one thing left is the one a comparison of numbers could never state: the
 * gradient subtracts from a price and adds to a quantity, and a port that got both branches the
 * same way round would give a game where agricultural worlds sell machinery cheaply. That is
 * asserted directly, with a message that says what it broke.
 */
namespace GameLogicTests
{

  namespace
  {
  } // namespace

  TEST_CLASS(TheEconomyGradient)
  {
  public:
    /*
     * The sign of the gradient does opposite things to price and to quantity, and that is the whole
     * of Elite's economy. This asserts the direction directly rather than leaving it implied by the
     * comparisons above, so that a future change which got both branches the same way round fails
     * with a message saying what it broke.
     */
    TEST_METHOD(TheEconomyGradientPushesPriceAndQuantityOppositeWays)
    {
      // Item 0 is Food, whose gradient is negative: cheap and plentiful on agricultural worlds.
      const Elite::MarketItem food = Elite::MarketItemAt(0);
      Assert::IsTrue((food.gradient & 0x80u) != 0u, L"item 0 should have a negative gradient");

      const std::uint8_t cheapEconomy = Elite::MarketPrice(0, 0, 0);
      const std::uint8_t richEconomy = Elite::MarketPrice(0, 7, 0);
      Assert::IsTrue(richEconomy < cheapEconomy, L"a negative gradient should make the price FALL as economy rises");

      Elite::Rng rng;
      rng.SetState({1, 2, 3, 4});
      MarketState low;
      Elite::GenerateMarket(rng, 0, low);

      rng.SetState({1, 2, 3, 4});
      MarketState high;
      Elite::GenerateMarket(rng, 7, high);

      Assert::IsTrue(high.availability[0] > low.availability[0],
                     L"the same negative gradient should make the quantity RISE as economy rises");
    }

  };

} // namespace GameLogicTests
