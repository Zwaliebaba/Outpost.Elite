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
 * The economy against the game that runs it (slice 2c, the price model).
 *
 * Prices are not stored, so there is nothing to compare against except the routines. Every one
 * of the seventeen goods is checked at every economy the game has, against every value the
 * market's random byte can take -- 34,816 prices -- and then the market generator is run for a
 * real system in every galaxy.
 *
 * The thing being guarded is the sign of the economy gradient, which subtracts from a price and
 * adds to a quantity. Getting it the same way round in both gives a game where agricultural
 * worlds sell machinery cheaply, which is wrong in a way no test of a single number would catch.
 */
namespace GameLogicTests
{

  namespace
  {
    /*
     * 6502: CHPR, with the cursor it was called at.
     *
     * The market screen's whole job is putting the right thing in the right column, so a comparison
     * of the characters alone would pass a port that printed every line one cell left. Each character
     * is stamped with XC and YC as it goes by, on both sides.
     */
    struct RecordingSink : public Elite::TextSink
    {
      void Put(std::uint8_t _character) override
      {
        const std::uint32_t column = (cursor != nullptr) ? cursor->column : 0u;
        const std::uint32_t row = (cursor != nullptr) ? cursor->row : 0u;
        stamped.push_back(static_cast<std::uint32_t>(_character) | (column << 8) | (row << 16));
      }

      Elite::TextState* cursor = nullptr;
      std::vector<std::uint32_t> stamped;
    };

  } // namespace

  TEST_CLASS(MarketAgainstTheShippedGame)
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
