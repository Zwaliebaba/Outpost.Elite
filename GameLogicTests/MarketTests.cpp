#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Guest.h"

#include <array>
#include <initializer_list>
#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t ADD_CREDITS = 0x65EE;
constexpr std::uint16_t COMPUTE_RESALE_PRICE = 0x6995;
constexpr std::uint16_t COMPUTE_MARKET_PRICES = 0x69CE;
constexpr std::uint16_t NEXT_MARKET_RANDOM = 0x6A85;

constexpr std::uint16_t COMMODITY_COUNT = 17;
constexpr std::uint16_t TRADE_RECORD_BYTES = 3;

/// A credit balance and what is added to it, in tenths of credits.
struct Credits
{
  std::uint32_t balance;
  std::uint32_t added;
};

[[nodiscard]] Elite::Guest GuestOf(ComparisonRig& _rig)
{
  return Elite::Guest(_rig.Host(), _rig.Program().loadSegment, Elite::DataSegment(_rig.Program()));
}

[[nodiscard]] std::wstring Hex(std::uint16_t _value)
{
  constexpr std::wstring_view DIGITS = L"0123456789ABCDEF";
  std::wstring text = L"CS:";
  for (int shift = 12; shift >= 0; shift -= 4)
    text += DIGITS[static_cast<std::size_t>((_value >> shift) & 0xF)];
  return text;
}

// Every offset in _offsets ran in the original while a call of _entry was being compared.
void AssertExecuted(ComparisonRig& _rig, std::uint16_t _entry, std::initializer_list<std::uint16_t> _offsets)
{
  const auto& hooks = _rig.Host().Native().Hooks();
  const auto found = hooks.find(Machine::Memory::Linear(_rig.Program().loadSegment, _entry));
  Assert::IsTrue(found != hooks.end(), L"the routine is ported");
  for (const std::uint16_t offset : _offsets)
  {
    const std::wstring message = Hex(offset) + L" ran in a comparison";
    Assert::IsTrue(found->second.executed.Contains(offset), message.c_str());
  }
}

// A fixed sequence of words, for inputs the replays do not give.
class Words
{
public:
  [[nodiscard]] std::uint16_t Next() noexcept
  {
    m_state = m_state * 1103515245u + 12345u;
    return static_cast<std::uint16_t>(m_state >> 16);
  }

private:
  std::uint32_t m_state = 1;
};

} // namespace

// Constructed inputs for the ported market routines (plan §6.3): prices at their edges, every economy,
// government and tech level, and trade records the file leaves at 0.
TEST_CLASS(MarketTests)
{
public:
  TEST_METHOD(ResalePriceAgreesAcrossItsRange)
  {
    ComparisonRig rig("ComputeResalePrice");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<std::uint16_t, 14> PRICES = {0, 1, 2, 31, 32, 33, 100, 3199, 3200, 3232, 6400, 0x7FFF, 0x8000, 0xFFFF};
    Words words;
    constexpr std::uint64_t MORE = 200;
    for (const std::uint16_t price : PRICES)
    {
      guest.Set(DS.resalePriceInput, price);
      rig.Call(COMPUTE_RESALE_PRICE, {.ax = 0x1111, .bx = 0x2222});
    }
    for (std::uint64_t call = 0; call < MORE; ++call)
    {
      guest.Set(DS.resalePriceInput, words.Next());
      rig.Call(COMPUTE_RESALE_PRICE, {.ax = 0x1111, .bx = 0x2222});
    }
    rig.AssertAllAgreed(COMPUTE_RESALE_PRICE, PRICES.size() + MORE);
  }

  // Every economy and government, tech levels past 9 (CS:6A03), the species branch's other arm
  // (CS:69F9), and signed price adjustments and tech factors.
  TEST_METHOD(MarketPricesAgreeInEveryEconomy)
  {
    ComparisonRig rig("ComputeMarketPrices");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<std::uint8_t, 5> TECH_LEVELS = {0, 5, 9, 10, 15};
    Words words;
    std::uint64_t calls = 0;
    for (std::uint8_t economy = 0; economy < 8; ++economy)
    {
      for (std::uint8_t government = 0; government < 8; ++government)
      {
        guest.Set(DS.currentEconomy, economy);
        guest.Set(DS.currentGovernment, government);
        guest.Set(DS.currentTechLevel, TECH_LEVELS[(economy + government) % TECH_LEVELS.size()]);
        guest.Set(DS.data75BC, static_cast<std::uint8_t>(government % 2 == 0 ? 0xFF : economy));
        if (economy >= 4)
        {
          for (std::uint16_t commodity = 0; commodity < COMMODITY_COUNT; ++commodity)
          {
            const auto record = static_cast<std::uint16_t>(DS.productTradeRecords.offset + commodity * TRADE_RECORD_BYTES);
            guest.SetWord(record, words.Next());
          }
        }
        rig.Call(COMPUTE_MARKET_PRICES, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
        ++calls;
      }
    }
    rig.AssertAllAgreed(COMPUTE_MARKET_PRICES, calls);
    AssertExecuted(rig, COMPUTE_MARKET_PRICES, {0x69F9, 0x6A03});
  }

  // A carry into the high word, a negative amount, and the largest balance wrapping.
  TEST_METHOD(CreditsAgreeThroughTheCarry)
  {
    ComparisonRig rig("AddCredits");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<Credits, 6> SUMS = {
      {{0, 0}, {0xFFFF, 1}, {1000, 0xFFFFFC18}, {0x12345678, 0x0000EDCB}, {0xFFFFFFFF, 1}, {0x0001FFFF, 0x0001FFFF}}};
    for (const Credits& sum : SUMS)
    {
      guest.SetWord(DS.creditsTenths.offset, static_cast<std::uint16_t>(sum.balance));
      guest.Set(DS.data75F5, static_cast<std::uint16_t>(sum.balance >> 16));
      rig.Call(ADD_CREDITS, {.ax = static_cast<std::uint16_t>(sum.added),
                             .bx = static_cast<std::uint16_t>(sum.added >> 16),
                             .cx = 0x3333,
                             .dx = 0x4444,
                             .si = 0x5555,
                             .di = 0x6666,
                             .bp = 0x7777});
    }
    rig.AssertAllAgreed(ADD_CREDITS, SUMS.size());

    Words words;
    constexpr std::uint64_t STEPS = 20;
    guest.SetWord(DS.marketRandomState.offset, words.Next());
    guest.Set(DS.data8C20, words.Next());
    guest.Set(DS.data8C22, words.Next());
    for (std::uint64_t step = 0; step < STEPS; ++step)
      rig.Call(NEXT_MARKET_RANDOM, {.ax = 0x1111, .bx = 0x2222});
    rig.AssertAllAgreed(NEXT_MARKET_RANDOM, STEPS);
  }
};

} // namespace GameLogicTests
