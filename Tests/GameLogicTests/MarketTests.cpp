#include "pch.h"

#include "OracleImage.h"

#include "LookupTables.h"
#include "Market.h"
#include "Universe.h"

#include <cstdint>
#include <string>
#include <vector>

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

/// A window of both sequences around the first place they differ, which is the only part worth
/// reading when a whole screen is being compared.
std::wstring FirstDifference(const std::vector<std::uint32_t>& _game, const std::vector<std::uint32_t>& _port)
{
  std::size_t at = 0;
  while (at < _game.size() && at < _port.size() && _game[at] == _port[at])
  {
    ++at;
  }

  const auto window = [](const std::vector<std::uint32_t>& _seq, std::size_t _from) {
    std::wstring text = L"[";
    for (std::size_t index = _from; index < _seq.size() && index < _from + 14; ++index)
    {
      const std::uint8_t character = static_cast<std::uint8_t>(_seq[index]);
      text += (character >= 32 && character < 127) ? static_cast<wchar_t>(character) : L'.';
      text += L'@';
      text += std::to_wstring((_seq[index] >> 8) & 0xFFu);
      text += L',';
      text += std::to_wstring((_seq[index] >> 16) & 0xFFu);
      text += L' ';
    }
    return text + L"]";
  };

  const std::size_t from = (at > 6) ? at - 6 : 0;
  return L"first difference at " + std::to_wstring(at) + L" of " + std::to_wstring(_game.size()) + L"/"
         + std::to_wstring(_port.size()) + L"\n  game: " + window(_game, from) + L"\n  port: " + window(_port, from);
}

std::wstring DescribeStamped(const std::vector<std::uint32_t>& _stamped)
{
  std::wstring text = L"[";
  for (std::size_t index = 0; index < _stamped.size() && index < 40; ++index)
  {
    const std::uint8_t character = static_cast<std::uint8_t>(_stamped[index]);
    text += (character >= 32 && character < 127) ? static_cast<wchar_t>(character) : L'.';
    text += L'@';
    text += std::to_wstring((_stamped[index] >> 8) & 0xFFu);
    text += L',';
    text += std::to_wstring((_stamped[index] >> 16) & 0xFFu);
    text += L' ';
  }
  if (_stamped.size() > 40)
  {
    text += L"...";
  }
  return text + L"]";
}

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

  /*
   * 6502: LCASH and MCASH -- spending and receiving, over the boundary that matters.
   *
   * The boundary is "exactly affordable". LCASH subtracts unconditionally and adds the amount
   * back when the top byte borrowed, so the interesting cases are cash equal to the price, one
   * tenth under it, and one over -- and the port must leave the cash untouched on the failure.
   */
  TEST_METHOD(SpendingAndReceivingCashMatchTheShippedRoutines)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t cashAt = oracle.Label("CASH");

    std::uint32_t compared = 0;
    std::uint32_t refused = 0;

    for (const std::uint32_t cash : { 0u, 1u, 255u, 256u, 1000u, 65535u, 65536u, 100000u, 0xFFFFFFFFu })
    {
      for (const std::uint32_t amount : { 0u, 1u, 255u, 256u, 999u, 1000u, 1001u, 65535u })
      {
        for (const bool spending : { true, false })
        {
          Cpu6502 cpu = oracle.Fresh();
          cpu.memory[cashAt] = static_cast<std::uint8_t>(cash >> 24);
          cpu.memory[static_cast<std::uint16_t>(cashAt + 1)] = static_cast<std::uint8_t>(cash >> 16);
          cpu.memory[static_cast<std::uint16_t>(cashAt + 2)] = static_cast<std::uint8_t>(cash >> 8);
          cpu.memory[static_cast<std::uint16_t>(cashAt + 3)] = static_cast<std::uint8_t>(cash);

          cpu.a = 0;
          cpu.x = static_cast<std::uint8_t>(amount);
          cpu.y = static_cast<std::uint8_t>(amount >> 8);
          cpu.sp = 0xFD;
          const std::uint16_t routine = oracle.Label(spending ? "LCASH" : "MCASH");
          Assert::IsTrue(cpu.CallSubroutine(routine, 10'000).completed, L"the cash routine should return");

          const std::uint32_t after =
            (static_cast<std::uint32_t>(cpu.memory[cashAt]) << 24)
            | (static_cast<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(cashAt + 1)]) << 16)
            | (static_cast<std::uint32_t>(cpu.memory[static_cast<std::uint16_t>(cashAt + 2)]) << 8)
            | cpu.memory[static_cast<std::uint16_t>(cashAt + 3)];

          Elite::CommanderBlock block;
          block.SetCash(cash);
          bool ourAnswer = true;
          if (spending)
          {
            ourAnswer = Elite::SpendCash(block, static_cast<std::uint16_t>(amount));
          }
          else
          {
            Elite::ReceiveCash(block, static_cast<std::uint16_t>(amount));
          }

          const std::wstring where = std::wstring(spending ? L"LCASH(" : L"MCASH(") + std::to_wstring(cash)
                                     + L", " + std::to_wstring(amount) + L")";
          Assert::AreEqual<std::uint32_t>(after, block.Cash(), (where + L": cash").c_str());

          if (spending)
          {
            // 6502: the carry on exit -- set when it was affordable, and cleared by MCASH's tail
            // when it was not.
            Assert::AreEqual(cpu.c, ourAnswer, (where + L": affordable").c_str());
            if (!ourAnswer)
            {
              Assert::AreEqual<std::uint32_t>(cash, block.Cash(),
                                              (where + L": a refused purchase must leave the cash alone").c_str());
              ++refused;
            }
          }
          ++compared;
        }
      }
    }

    Logger::WriteMessage(("LCASH and MCASH: " + std::to_string(compared) + " compared, " + std::to_string(refused)
                          + " purchases refused")
                           .c_str());
    Assert::IsTrue(refused > 0, L"the failure path must be exercised");
  }

  /// 6502: GCASH -- the product times four, over every price against a spread of quantities.
  TEST_METHOD(TotalPriceMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t p = oracle.Label("P");
    const std::uint16_t q = oracle.Label("Q");

    std::uint32_t compared = 0;
    for (std::uint32_t price = 0; price < 256; ++price)
    {
      for (const std::uint32_t quantity : { 0u, 1u, 2u, 3u, 17u, 64u, 100u, 127u, 128u, 200u, 255u })
      {
        Cpu6502 cpu = oracle.Fresh();
        cpu.memory[p] = static_cast<std::uint8_t>(price);
        cpu.memory[q] = static_cast<std::uint8_t>(quantity);
        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        Assert::IsTrue(cpu.CallSubroutine(oracle.Label("GCASH"), 10'000).completed, L"GCASH should return");

        const std::uint16_t expected = static_cast<std::uint16_t>(cpu.x | (cpu.y << 8));
        Assert::AreEqual<std::uint32_t>(expected,
                                        Elite::TotalPrice(static_cast<std::uint8_t>(price),
                                                          static_cast<std::uint8_t>(quantity)),
                                        (L"GCASH(" + std::to_wstring(price) + L", " + std::to_wstring(quantity)
                                         + L")")
                                          .c_str());
        ++compared;
      }
    }

    Logger::WriteMessage(("GCASH: " + std::to_string(compared) + " totals compared").c_str());
  }

  /*
   * 6502: tnpr -- does this much more of an item fit?
   *
   * Both rules, and the boundary of each. The tonne path is where the two off-by-ones cancel --
   * the count comes out one high because the CPX left the carry set, and CRGO holds two more than
   * the capacity -- so a hold filled to exactly its capacity is the case that catches a port that
   * corrected either one.
   */
  TEST_METHOD(CargoCapacityMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t qq20 = oracle.Label("QQ20");
    const std::uint16_t qq29 = oracle.Label("QQ29");
    const std::uint16_t crgo = oracle.Label("CRGO");
    const std::uint16_t tribble = oracle.Label("TRIBBLE");

    std::uint32_t compared = 0;
    std::uint32_t fitted = 0;

    /*
     * The amount is swept over every value a byte can hold rather than sampled.
     *
     * That is what reaches the case where the thirteen additions WRAP and the tribble addition's
     * carry then decides the answer -- one value of the amount in each configuration, and a
     * sampled grid misses it. Found by mutation: dropping that carry passed a coarser sweep.
     */
    for (const std::uint32_t capacity : { 22u, 37u })
    {
      for (const std::uint32_t filled : { 0u, 20u, 35u, 36u, 200u, 250u })
      {
       for (const bool concentrated : { false, true })
       {
        for (const std::uint32_t tribbles : { 0u, 1024u })
        {
          for (const std::uint32_t item : { 0u, 1u, 6u, 12u, 13u, 14u, 16u })
          {
            for (std::uint32_t amount = 0; amount < 256; ++amount)
            {
              Elite::CommanderBlock block;
              block.At(Elite::Field::CargoCapacity) = static_cast<std::uint8_t>(capacity);
              block.bytes[static_cast<std::size_t>(Elite::Field::Tribbles)] =
                static_cast<std::uint8_t>(tribbles);
              block.bytes[static_cast<std::size_t>(Elite::Field::Tribbles) + 1u] =
                static_cast<std::uint8_t>(tribbles >> 8);

              /*
               * Two layouts, and the second is not decoration.
               *
               * The carry the tribble addition consumes is the one the LAST addition produced --
               * item 0, not the running total -- so it can only be set when item 0's own byte is
               * large. Spreading the tonnage keeps it small and the carry is then always clear,
               * which is how a port that dropped it passed a sweep of all 256 amounts. Found by
               * mutation.
               */
              const std::size_t hold = static_cast<std::size_t>(Elite::Field::CargoHold);
              if (concentrated)
              {
                block.bytes[hold + 0] = static_cast<std::uint8_t>(filled);
              }
              else
              {
                block.bytes[hold + 0] = static_cast<std::uint8_t>(filled / 3u);
                block.bytes[hold + 5] = static_cast<std::uint8_t>(filled / 3u);
                block.bytes[hold + 12] = static_cast<std::uint8_t>(filled - 2u * (filled / 3u));
              }
              block.bytes[hold + 14] = 90; // some gold already aboard, for the kilo path

              Cpu6502 cpu = oracle.Fresh();
              for (std::size_t index = 0; index < Elite::MARKET_ITEM_COUNT; ++index)
              {
                cpu.memory[static_cast<std::uint16_t>(qq20 + index)] = block.bytes[hold + index];
              }
              cpu.memory[crgo] = static_cast<std::uint8_t>(capacity);
              cpu.memory[tribble] = static_cast<std::uint8_t>(tribbles);
              cpu.memory[static_cast<std::uint16_t>(tribble + 1)] = static_cast<std::uint8_t>(tribbles >> 8);
              cpu.memory[qq29] = static_cast<std::uint8_t>(item);

              cpu.a = static_cast<std::uint8_t>(amount);
              cpu.x = cpu.y = 0;
              cpu.sp = 0xFD;
              Assert::IsTrue(cpu.CallSubroutine(oracle.Label("tnpr"), 10'000).completed, L"tnpr should return");

              // 6502: the carry on exit is set when there is NO room.
              const bool gameFits = !cpu.c;
              const bool ourFits =
                Elite::CargoFits(block, static_cast<std::uint8_t>(item), static_cast<std::uint8_t>(amount));

              Assert::AreEqual(gameFits, ourFits,
                               (L"tnpr(item=" + std::to_wstring(item) + L" amount=" + std::to_wstring(amount)
                                + L" filled=" + std::to_wstring(filled) + L" capacity=" + std::to_wstring(capacity)
                                + L" tribbles=" + std::to_wstring(tribbles) + L")")
                                 .c_str());

              // A also comes back unchanged, because the routine pushes and pulls it.
              Assert::AreEqual<std::uint32_t>(amount, cpu.a, L"tnpr should preserve A");
              if (ourFits)
              {
                ++fitted;
              }
              ++compared;
            }
          }
        }
       }
      }
    }

    Logger::WriteMessage(("tnpr: " + std::to_string(compared) + " capacity checks compared, "
                          + std::to_string(fitted) + " fitted")
                           .c_str());
    Assert::IsTrue(fitted > 0 && fitted < compared, L"both answers must be exercised");
  }

  /*
   * 6502: TT167 -- the whole market screen, compared as the sequence of characters that reach
   * CHPR and the cursor each line starts at.
   *
   * Characters rather than a screen, because TT167 opens with TRADEMODE and that is TT66: it
   * resets the view and draws the border, which is screen work rather than market work. Trapping
   * CHPR means neither side draws, so what is left to compare is exactly what the market screen
   * decides -- every name, every price, every unit, every quantity, and where each goes.
   *
   * Swept over all eight economies and a spread of market randomisers, which is what varies both
   * the prices and which items run out and print a dash instead of a number.
   */
  TEST_METHOD(TheMarketScreenMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t chpr = oracle.Label("CHPR");

    std::uint32_t compared = 0;
    std::uint32_t dashes = 0;

    for (std::uint32_t economy = 0; economy < 8; ++economy)
    {
      for (const std::uint32_t randomiser : { 0u, 1u, 37u, 128u, 200u, 255u })
      {
        Elite::MarketState market;
        market.randomiser = static_cast<std::uint8_t>(randomiser);
        for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
        {
          // A spread of stock levels including zero, so the dash path is reached.
          market.availability[item] =
            static_cast<std::uint8_t>((item * 17u + randomiser) % 5u == 0u ? 0u : (item * 13u + randomiser) & 0x3Fu);
          market.price[item] =
            Elite::MarketPrice(static_cast<int>(item), static_cast<std::uint8_t>(economy),
                               static_cast<std::uint8_t>(randomiser));
        }

        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
        cpu.AddTrap(oracle.Label("TRADEMODE"));
        cpu.watch = { oracle.Label("XC"), oracle.Label("YC"), 0, 0 };

        cpu.memory[oracle.Label("QQ28")] = static_cast<std::uint8_t>(economy);
        cpu.memory[oracle.Label("QQ26")] = static_cast<std::uint8_t>(randomiser);
        cpu.memory[oracle.Label("MJ")] = 0;
        cpu.memory[oracle.Label("QQ17")] = 0x80;
        cpu.memory[oracle.Label("XC")] = 1;
        cpu.memory[oracle.Label("YC")] = 1;
        cpu.memory[oracle.Label("DTW1")] = 0;
        cpu.memory[oracle.Label("DTW2")] = 0xFF;
        cpu.memory[oracle.Label("DTW3")] = 0;
        cpu.memory[oracle.Label("DTW4")] = 0;
        cpu.memory[oracle.Label("DTW5")] = 0;
        cpu.memory[oracle.Label("DTW6")] = 0;
        cpu.memory[oracle.Label("DTW8")] = 0xFF;
        for (std::size_t item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
        {
          cpu.memory[static_cast<std::uint16_t>(oracle.Label("AVL") + item)] = market.availability[item];
        }

        cpu.a = cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;
        const auto run = cpu.CallSubroutine(oracle.Label("TT167"), 5'000'000);
        Assert::IsTrue(run.completed && !run.illegalOpcode, L"TT167 should return");

        // Every character, with the cursor it was printed at -- so a line in the right words at
        // the wrong column fails.
        std::vector<std::uint32_t> expected;
        for (const auto& hit : cpu.trapHits)
        {
          if (hit.address == chpr)
          {
            expected.push_back(static_cast<std::uint32_t>(hit.a) | (static_cast<std::uint32_t>(hit.watched[0]) << 8)
                               | (static_cast<std::uint32_t>(hit.watched[1]) << 16));
          }
        }

        RecordingSink sink;
        Elite::TextState text;
        text.column = 1;
        text.row = 1;
        text.caseFlags = 0x80;
        sink.cursor = &text;
        Elite::CharacterPrinter characters(sink);
        characters.state.sentenceStart = 0xFF;
        Elite::TokenPrinter printer(characters);
        printer.SetCaseFlags(0x80);

        Elite::PrintMarketScreen(printer, characters, text, static_cast<std::uint8_t>(economy), market, false);

        const std::wstring where = L"TT167 (economy " + std::to_wstring(economy) + L", randomiser "
                                   + std::to_wstring(randomiser) + L")";
        if (sink.stamped != expected)
        {
          Assert::Fail((where + L" differs, " + FirstDifference(expected, sink.stamped)).c_str());
        }

        for (const std::uint8_t stock : market.availability)
        {
          if (stock == 0)
          {
            ++dashes;
          }
        }
        ++compared;
      }
    }

    Logger::WriteMessage(("TT167: " + std::to_string(compared) + " market screens compared character for character, "
                          + std::to_string(dashes) + " sold-out lines among them")
                           .c_str());
    Assert::IsTrue(dashes > 0, L"the sold-out path must be reached");
  }

  /*
   * 6502: gnum's body -- one keystroke of a typed number, over every value against every key.
   *
   * The loop around it reads the keyboard, so the shipped routine is STEPPED from the instruction
   * after the key read until it reaches one of its five exits, and the exit and the accumulator
   * are what get compared. That tests the step exactly, without a stub standing in for the
   * keyboard: the real branches run.
   *
   * Exhaustive in both dimensions -- 65,536 pairs at each availability -- because the multiply by
   * ten threads a carry through two additions that have no CLC, and the values where that shows
   * are the ones a sample would miss.
   */
  TEST_METHOD(TypingANumberMatchesTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();

    // 6502: TT223's body starts after the key read, so that is where the stepping begins.
    const std::uint16_t afterKeyRead = static_cast<std::uint16_t>(oracle.Label("TT223") + 3);
    const std::uint16_t out = oracle.Label("OUT");
    const std::uint16_t bay2 = oracle.Label("BAY2");
    const std::uint16_t tt226 = oracle.Label("TT226");
    const std::uint16_t nwdav1 = oracle.Label("NWDAV1");
    const std::uint16_t nwdav3 = oracle.Label("NWDAV3");
    const std::uint16_t r = oracle.Label("R");
    const std::uint16_t qq25 = oracle.Label("QQ25");

    std::array<std::uint32_t, 5> outcomes{};
    std::uint32_t compared = 0;

    for (const std::uint32_t available : { 0u, 1u, 25u, 26u, 60u, 255u })
    {
      for (std::uint32_t value = 0; value < 256; ++value)
      {
        for (std::uint32_t key = 0; key < 256; ++key)
        {
          Cpu6502 cpu = oracle.Fresh();

          // The Y and N paths echo the key through DASC before they set the accumulator, and
          // DASC reaches the screen. Trapping it keeps the stepping on gnum's own instructions,
          // and the trap clears the carry because that is what DASC does.
          cpu.AddTrap(oracle.Label("DASC"), Cpu6502::TrapExit::ClearCarry);
          cpu.memory[r] = static_cast<std::uint8_t>(value);
          cpu.memory[qq25] = static_cast<std::uint8_t>(available);
          cpu.a = static_cast<std::uint8_t>(key);
          cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          cpu.pc = afterKeyRead;

          Elite::DigitResult gameResult = Elite::DigitResult::Accepted;
          bool reached = false;
          for (int step = 0; step < 200; ++step)
          {
            if (cpu.pc == out)
            {
              gameResult = Elite::DigitResult::Complete;
              reached = true;
              break;
            }
            if (cpu.pc == bay2)
            {
              gameResult = Elite::DigitResult::LeaveScreen;
              reached = true;
              break;
            }
            if (cpu.pc == tt226)
            {
              gameResult = Elite::DigitResult::Accepted;
              reached = true;
              break;
            }
            if (cpu.pc == nwdav1)
            {
              gameResult = Elite::DigitResult::TakeAll;
              reached = true;
              break;
            }
            if (cpu.pc == nwdav3)
            {
              gameResult = Elite::DigitResult::TakeNone;
              reached = true;
              break;
            }
            Assert::IsTrue(cpu.Step(), L"gnum should not reach an unimplemented opcode");
          }
          Assert::IsTrue(reached, L"gnum's body should reach one of its five exits");

          /*
           * NWDAV1 and NWDAV3 set R after the branch, so the shipped accumulator is only
           * comparable once those two instructions have run. Stepping them is a line each and
           * keeps the comparison on the value rather than on the label.
           */
          if (gameResult == Elite::DigitResult::TakeAll || gameResult == Elite::DigitResult::TakeNone)
          {
            for (int step = 0; step < 200 && cpu.pc != out; ++step)
            {
              Assert::IsTrue(cpu.Step(), L"gnum's Y and N paths should reach OUT");
            }
          }

          std::uint8_t ours = static_cast<std::uint8_t>(value);
          const Elite::DigitResult ourResult =
            Elite::TypeDigit(ours, static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(available));

          const std::wstring where = L"gnum(value=" + std::to_wstring(value) + L", key=" + std::to_wstring(key)
                                     + L", available=" + std::to_wstring(available) + L")";
          Assert::AreEqual<std::uint32_t>(static_cast<std::uint32_t>(gameResult),
                                          static_cast<std::uint32_t>(ourResult), (where + L": the exit").c_str());
          Assert::AreEqual<std::uint32_t>(cpu.memory[r], ours, (where + L": the accumulator").c_str());

          ++outcomes[static_cast<std::size_t>(ourResult)];
          ++compared;
        }
      }
    }

    std::string report = "gnum: " + std::to_string(compared) + " keystrokes compared. Exits:";
    const char* names[] = { " accepted=", " complete=", " take-all=", " take-none=", " leave-screen=" };
    for (std::size_t index = 0; index < outcomes.size(); ++index)
    {
      report += names[index] + std::to_string(outcomes[index]);
    }
    Logger::WriteMessage(report.c_str());

    for (std::size_t index = 0; index < outcomes.size(); ++index)
    {
      Assert::IsTrue(outcomes[index] > 0, L"every exit of gnum should be reached");
    }
  }
};

} // namespace GameLogicTests
