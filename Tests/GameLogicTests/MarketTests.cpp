#include "pch.h"

#include "OracleImage.h"

#include "LookupTables.h"
#include "Market.h"
#include "Universe.h"

#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::MarketState;
using Elite::SystemSeeds;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

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
bool OracleMissing()
{
  const OracleImage& oracle = OracleImage::Instance();
  if (oracle.Available())
  {
    return false;
  }
  Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
  return true;
}

std::wstring Context(const wchar_t* _what, std::uint32_t _a, std::uint32_t _b, std::uint32_t _c)
{
  return std::wstring(_what) + L" (item " + std::to_wstring(_a) + L", economy " + std::to_wstring(_b)
         + L", randomiser " + std::to_wstring(_c) + L")";
}
} // namespace

TEST_CLASS(MarketAgainstTheShippedGame)
{
public:
  /// The extracted table against the game's own bytes, so a regenerated or edited copy fails.
  TEST_METHOD(MarketTableMatchesTheImage)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const Cpu6502 cpu = oracle.Fresh();
    const std::uint16_t table = oracle.Label("QQ23");

    for (std::size_t offset = 0; offset < Elite::MARKET_TABLE.size(); ++offset)
    {
      Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(table + offset)],
                                      Elite::MARKET_TABLE[offset],
                                      (L"QQ23 differs at offset " + std::to_wstring(offset)).c_str());
    }
  }

  /*
   * 6502: var -- the economy's contribution, over every gradient byte and every economy.
   *
   * Every one of the 256 gradient values, not just the eighteen the table uses, because the
   * routine masks and branches on bits rather than on the table's contents and the port must
   * agree with it everywhere the flags can go.
   */
  TEST_METHOD(EconomyAdjustmentMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t routine = oracle.Label("var");
    const std::uint16_t gradient = 0x8F; // the zero-page byte var reads
    const std::uint16_t qq28 = oracle.Label("QQ28");
    const std::uint16_t result = 0x91;

    for (std::uint32_t value = 0; value < 256; ++value)
    {
      for (std::uint32_t economy = 0; economy < 8; ++economy)
      {
        Cpu6502 cpu = oracle.Fresh();
        cpu.memory[gradient] = static_cast<std::uint8_t>(value);
        cpu.memory[qq28] = static_cast<std::uint8_t>(economy);
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;

        Assert::IsTrue(cpu.CallSubroutine(routine, 50'000).completed, L"var should return");

        const std::uint8_t ours = Elite::EconomyAdjustment(static_cast<std::uint8_t>(value),
                                                           static_cast<std::uint8_t>(economy));
        Assert::AreEqual<std::uint32_t>(cpu.memory[result], ours, Context(L"adjustment", value, economy, 0).c_str());
      }
    }
  }

  /*
   * Every price the game can quote: seventeen goods, eight economies, every value of the market's
   * random byte. 34,816 of them, compared against TT151's arithmetic through the byte it leaves
   * in QQ24.
   *
   * TT151 also prints, and printing needs a screen and a cursor -- so it is STEPPED to the point
   * where the price is final rather than called, which is the same technique the G1 test uses.
   * The alternative would have been to compare a screenful of pixels for an arithmetic bug.
   */
  TEST_METHOD(EveryPriceMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t routine = oracle.Label("TT151");
    const std::uint16_t priced = oracle.Label("TT156"); // where QQ24 holds the final price
    const std::uint16_t qq24 = oracle.Label("QQ24");
    const std::uint16_t qq26 = oracle.Label("QQ26");
    const std::uint16_t qq28 = oracle.Label("QQ28");
    const std::uint16_t mj = oracle.Label("MJ");

    std::uint32_t compared = 0;

    for (std::uint32_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
    {
      for (std::uint32_t economy = 0; economy < 8; ++economy)
      {
        for (std::uint32_t randomiser = 0; randomiser < 256; ++randomiser)
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.memory[qq26] = static_cast<std::uint8_t>(randomiser);
          cpu.memory[qq28] = static_cast<std::uint8_t>(economy);
          cpu.memory[mj] = 0; // not in witchspace, so the routine takes its ordinary path
          cpu.a = static_cast<std::uint8_t>(item);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.pc = routine;

          // Step until the price is settled. TT151 prints on the way, which needs a cursor and a
          // canvas; stopping at TT156 gets the number without any of that.
          bool reached = false;
          for (int instruction = 0; instruction < 20'000; ++instruction)
          {
            if (cpu.pc == priced)
            {
              reached = true;
              break;
            }
            if (!cpu.Step())
            {
              break;
            }
          }
          Assert::IsTrue(reached, Context(L"TT151 should reach its price", item, economy, randomiser).c_str());

          // 6502: TT156 stores A into QQ24, so A is the price at this point.
          const std::uint8_t theirs = cpu.a;
          const std::uint8_t ours = Elite::MarketPrice(static_cast<int>(item), static_cast<std::uint8_t>(economy),
                                                       static_cast<std::uint8_t>(randomiser));

          Assert::AreEqual<std::uint32_t>(theirs, ours, Context(L"price", item, economy, randomiser).c_str());
          ++compared;
        }
      }
    }

    Logger::WriteMessage(("TT151: " + std::to_string(compared) + " prices compared\n").c_str());
    Assert::AreEqual<std::uint32_t>(17u * 8u * 256u, compared, L"the sweep should not have been narrowed");
  }

  /*
   * 6502: GVL -- a whole market, rolled the way arrival at a system rolls it.
   *
   * Driven through the RNG rather than through a supplied byte, because the original calls DORND
   * itself and where the randomness comes from is part of the behaviour. Both sides start from
   * the same four bytes of RNG state.
   *
   * Alien Items are checked too, and they are the interesting case. GVL's loop stops one short
   * of them -- and they come back ZERO anyway, because `var` writes AVL+16 on its way out. A
   * routine that computes an economy adjustment also enforces that one commodity cannot be
   * bought, sixteen times over, and nothing about either name suggests it.
   */
  TEST_METHOD(GeneratedMarketsMatchTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t routine = oracle.Label("GVL");
    const std::uint16_t avl = oracle.Label("AVL");
    const std::uint16_t qq26 = oracle.Label("QQ26");
    const std::uint16_t qq28 = oracle.Label("QQ28");
    const std::uint16_t rand = oracle.Label("RAND");

    std::uint32_t compared = 0;

    for (std::uint32_t economy = 0; economy < 8; ++economy)
    {
      for (std::uint32_t seed = 0; seed < 64; ++seed)
      {
        const std::array<std::uint8_t, 4> state = { static_cast<std::uint8_t>(seed * 7u + 1u),
                                                    static_cast<std::uint8_t>(seed * 13u + 5u),
                                                    static_cast<std::uint8_t>(seed * 29u + 11u),
                                                    static_cast<std::uint8_t>(seed * 61u + 23u) };
        constexpr std::uint8_t ALIEN_MARKER = 0x2A;

        Cpu6502 cpu = oracle.Fresh();
        for (int index = 0; index < 4; ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(rand + index)] = state[index];
        }
        cpu.memory[qq28] = static_cast<std::uint8_t>(economy);
        cpu.memory[static_cast<std::uint16_t>(avl + 16)] = ALIEN_MARKER;
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        cpu.c = false;

        Assert::IsTrue(cpu.CallSubroutine(routine, 200'000).completed, L"GVL should return");

        Elite::Rng rng;
        rng.SetState(state);
        MarketState market;
        market.availability[16] = ALIEN_MARKER;
        Elite::GenerateMarket(rng, static_cast<std::uint8_t>(economy), market);

        const std::wstring where = L"economy " + std::to_wstring(economy) + L" seed " + std::to_wstring(seed);

        Assert::AreEqual<std::uint32_t>(cpu.memory[qq26], market.randomiser, (where + L": randomiser").c_str());

        for (int item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
        {
          Assert::AreEqual<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(avl + item)],
                                          market.availability[item],
                                          (where + L": availability of item " + std::to_wstring(item)).c_str());
        }

        Assert::AreEqual<std::uint32_t>(0u, market.availability[16],
                                        (where + L": Alien Items must be zeroed, as var does").c_str());
        ++compared;
      }
    }

    Logger::WriteMessage(("GVL: " + std::to_string(compared) + " markets generated and compared\n").c_str());
  }

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
    rng.SetState({ 1, 2, 3, 4 });
    MarketState low;
    Elite::GenerateMarket(rng, 0, low);

    rng.SetState({ 1, 2, 3, 4 });
    MarketState high;
    Elite::GenerateMarket(rng, 7, high);

    Assert::IsTrue(high.availability[0] > low.availability[0],
                   L"the same negative gradient should make the quantity RISE as economy rises");
  }
};

} // namespace GameLogicTests
