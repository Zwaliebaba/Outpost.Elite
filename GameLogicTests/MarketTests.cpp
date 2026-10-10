#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Guest.h"
#include "TwinRig.h"

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
constexpr std::uint16_t SPEND_CREDITS = 0x65EC;
constexpr std::uint16_t PARSE_QUANTITY = 0x6A99;
constexpr std::uint16_t PRINT_CARGO_QUANTITY = 0x6AD9;
constexpr std::uint16_t ADD_CONTRABAND_PENALTY = 0x6DC1;
constexpr std::uint16_t CARGO_MENU_FIRST_ROW = 0x1E5;

constexpr std::uint16_t COMMODITY_COUNT = 17;
constexpr std::uint16_t TRADE_RECORD_BYTES = 3;

/// A credit balance and what is added to it, in tenths of credits.
struct Credits
{
  std::uint32_t balance;
  std::uint32_t added;
};

// The data segment byte at _offset, set to _value on both twins.
void SetByte(TwinRig& _rig, std::uint16_t _offset, std::uint8_t _value)
{
  _rig.Both([_offset, _value](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _offset, _value); });
}

void SetByte(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  SetByte(_rig, _field.offset, _value);
}

// The cash, in tenths of credits, on both twins.
void SetCredits(TwinRig& _rig, std::uint32_t _tenths)
{
  _rig.Both(
    [_tenths](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      const std::uint16_t data = Elite::DataSegment(_program);
      _pc.Ram().Write16(data, DS.creditsTenths.offset, static_cast<std::uint16_t>(_tenths));
      _pc.Ram().Write16(data, DS.data75F5.offset, static_cast<std::uint16_t>(_tenths >> 16));
    });
}

// The cargo menu row _row's cargoHold entry: the amount held, then (+1) the amount on sale.
[[nodiscard]] std::uint16_t Held(std::uint8_t _row)
{
  return DS.cargoHold.At(_row - 1u);
}

[[nodiscard]] std::uint16_t OnSale(std::uint8_t _row)
{
  return static_cast<std::uint16_t>(DS.cargoHold.At(_row - 1u) + 1);
}

// B or S on the cargo menu, then the keys _typed names at "Quantity?", separated by spaces, and Return: Replay.h's
// steps.
[[nodiscard]] std::string Trade(std::string_view _key, std::string_view _typed)
{
  std::string steps = "key " + std::string(_key) + "; wait 0.3\n";
  std::size_t start = 0;
  while (start < _typed.size())
  {
    std::size_t end = _typed.find(' ', start);
    if (end == std::string_view::npos)
    {
      end = _typed.size();
    }
    steps += "key " + std::string(_typed.substr(start, end - start)) + "; wait 0.05\n";
    start = end + 1;
  }
  return steps + "key Return; wait 0.3\n";
}

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

  // Credits spent: less than the cash, all of it, a tenth more, and borrows across the high word.
  TEST_METHOD(SpendCreditsAgreesThroughTheBorrow)
  {
    ComparisonRig rig("SpendCredits");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<Credits, 7> SPENT = {
      {{1000, 999}, {1000, 1000}, {1000, 1001}, {0x10000, 0xFFFF}, {0x10000, 0x10001}, {0x12345678, 0x00010000}, {0, 1}}};
    for (const Credits& spent : SPENT)
    {
      guest.SetWord(DS.creditsTenths.offset, static_cast<std::uint16_t>(spent.balance));
      guest.Set(DS.data75F5, static_cast<std::uint16_t>(spent.balance >> 16));
      rig.Call(SPEND_CREDITS, {.ax = static_cast<std::uint16_t>(spent.added),
                               .bx = static_cast<std::uint16_t>(spent.added >> 16),
                               .cx = 0x3333,
                               .dx = 0x4444,
                               .si = 0x5555,
                               .di = 0x6666,
                               .bp = 0x7777});
    }
    rig.AssertAllAgreed(SPEND_CREDITS, SPENT.size());
  }

  // Quantities as typed: empty, spaces around and between, letters, the characters either side of the digits, the
  // limit of 250, and numbers whose multiply by ten loses its high byte.
  TEST_METHOD(ParseQuantityAgreesOnEveryKindOfText)
  {
    ComparisonRig rig("ParseQuantity");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<std::string_view, 22> TEXTS = {"",     "   ", "7",  "07", "250", "251", "255", "300", "2600", " 12", "12 ",
                                                        " 12 ", "1 2", "1a", "a",  "/",   ":",   "999", "0",   "25 x", "1  ", "  0"};
    for (const std::string_view text : TEXTS)
    {
      for (std::size_t index = 0; index < DS.quantityText.SIZE_BYTES; ++index)
      {
        guest.SetByte(DS.quantityText.At(index), index < text.size() ? static_cast<std::uint8_t>(text[index]) : std::uint8_t{0});
      }
      rig.Call(PARSE_QUANTITY,
               {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = DS.quantityText.offset, .bp = 0x7777});
    }
    rig.AssertAllAgreed(PARSE_QUANTITY, TEXTS.size());
  }

  // Quantities printed in the first, a middle and the last row, "-" for none.
  TEST_METHOD(PrintCargoQuantityAgreesInEveryRow)
  {
    ComparisonRig rig("PrintCargoQuantity");
    Elite::Guest guest = GuestOf(rig);
    guest.Set(DS.menuFirstRowAttr, CARGO_MENU_FIRST_ROW);
    guest.Set(DS.textAttribute, 0x4E);
    constexpr std::array<std::uint8_t, 3> ROWS = {1, 9, 17};
    constexpr std::array<std::uint16_t, 4> QUANTITIES = {0, 1, 42, 250};
    for (const std::uint8_t row : ROWS)
    {
      guest.Set(DS.menuSelectedRow, row);
      for (const std::uint16_t quantity : QUANTITIES)
      {
        rig.Call(PRINT_CARGO_QUANTITY,
                 {.ax = quantity, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
      }
    }
    rig.AssertAllAgreed(PRINT_CARGO_QUANTITY, ROWS.size() * QUANTITIES.size());
  }

  // Every commodity's penalty onto a clean record, a record already marked, and one whose sum wraps to 0.
  TEST_METHOD(ContrabandPenaltyAgreesForEveryCommodity)
  {
    ComparisonRig rig("AddContrabandPenalty");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<std::uint8_t, 3> STATUSES = {0, 0x40, 0xFF};
    for (const std::uint8_t status : STATUSES)
    {
      for (std::uint8_t row = 1; row <= COMMODITY_COUNT; ++row)
      {
        guest.Set(DS.legalStatus, status);
        guest.Set(DS.menuSelectedRow, row);
        rig.Call(ADD_CONTRABAND_PENALTY, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333});
      }
    }
    rig.AssertAllAgreed(ADD_CONTRABAND_PENALTY, STATUSES.size() * COMMODITY_COUNT);
  }

  // Every refusal and purchase of the buy screen: nothing on sale, a full hold, a quantity that is no number (a
  // shifted letter), 0, more than is on sale or fits, with and without the cargo bay extension, short of cash, alien
  // items, and gems at and past their cap; the quantity edited with Backspace and typed past its three characters;
  // the keys the menu ignores, and the cursor round both ends by keys and by the steering.
  TEST_METHOD(CargoIsBoughtByTypedQuantity)
  {
    TwinRig rig("TwinCargoBuy");
    rig.Play("key space; wait 4");
    SetCredits(rig, 1000000);
    rig.Play("key F3; wait 1");
    SetByte(rig, OnSale(1), 0);
    rig.Play("key b; wait 0.2");
    SetByte(rig, OnSale(1), 20);
    SetByte(rig, DS.cargoUsedTonnes, 20);
    rig.Play("key b; wait 0.2");
    SetByte(rig, DS.cargoUsedTonnes, 0);
    rig.Play("key b; wait 0.3\ndown Shift_L; wait 0.05\nkey a; wait 0.05\nup Shift_L; wait 0.05\nkey Return; wait 0.3\n" + Trade("b", "0") +
             Trade("b", "3 0") + Trade("b", "2 5") + Trade("b", "BackSpace 1 0 0 0 BackSpace BackSpace"));
    SetByte(rig, DS.largeCargoBayFitted, 1);
    rig.Play(Trade("b", "1"));
    SetCredits(rig, 0);
    rig.Play(Trade("b", "1") + "digest tonnes\nkey s; wait 0.2\nkey F3; wait 0.2\nkey x; wait 0.2\nkey Up; wait 0.2\nkey b; wait 0.2\n"
                               "key Up; wait 0.2");
    SetCredits(rig, 1000000);
    SetByte(rig, Held(16), 0xFA);
    SetByte(rig, OnSale(16), 50);
    rig.Play("key b; wait 0.2");
    SetByte(rig, Held(16), 0xF0);
    rig.Play(Trade("b", "2 0") + Trade("b", "1 2"));
    SetByte(rig, Held(16), 0);
    rig.Play(Trade("b", "5") + "key Down; wait 0.2\nkey Down; wait 0.2");
    SetByte(rig, DS.keyboardPitchRate, 5);
    rig.Play("wait 0.3");
    SetByte(rig, DS.keyboardPitchRate, 0xFB);
    rig.Play("wait 0.3\ndigest gems\nkey F9; wait 0.5\ndigest left");
  }

  // Every refusal and sale of the sell screen: nothing held, a quantity that is no number, 0 or more than is held, a
  // sale that fills the market's 255, contraband, alien items, and gold outside the tonnes; and the keys it ignores.
  TEST_METHOD(CargoIsSoldByTypedQuantity)
  {
    TwinRig rig("TwinCargoSell");
    rig.Play("key space; wait 4\nkey F2; wait 1");
    SetByte(rig, Held(1), 0);
    rig.Play("key s; wait 0.2");
    SetByte(rig, Held(1), 10);
    SetByte(rig, OnSale(1), 250);
    SetByte(rig, DS.cargoUsedTonnes, 10);
    rig.Play(Trade("s", "x") + Trade("s", "0") + Trade("s", "2 0") + Trade("s", "1 0") + "key Down; wait 0.2\nkey Down; wait 0.2");
    SetByte(rig, Held(3), 5);
    rig.Play(Trade("s", "5") + "key Up; wait 0.2\nkey Up; wait 0.2\nkey Up; wait 0.2");
    SetByte(rig, Held(17), 3);
    SetByte(rig, DS.cargoUsedTonnes, 3);
    rig.Play(Trade("s", "3") + "key Up; wait 0.2\nkey Up; wait 0.2\nkey Up; wait 0.2");
    SetByte(rig, Held(14), 10);
    rig.Play(Trade("s", "5") + "digest sold\nkey b; wait 0.2\nkey F2; wait 0.2\nkey F9; wait 0.5\ndigest left");
  }

  // The market prices with a system name that has a space in it, which the screen cuts there for good, and the keys
  // its wait ignores: its own F8, and keys that are neither Esc nor an F-key.
  TEST_METHOD(MarketPricesCutTheSystemNameAtASpace)
  {
    TwinRig rig("TwinMarketPrices");
    rig.Play("key space; wait 4");
    rig.Both(
      [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
      {
        constexpr std::string_view NAME = "LA VE";
        for (std::size_t index = 0; index < DS.currentSystemName.SIZE_BYTES; ++index)
        {
          _pc.Ram().Write8(Elite::DataSegment(_program), DS.currentSystemName.At(index),
                           index < NAME.size() ? static_cast<std::uint8_t>(NAME[index]) : std::uint8_t{0});
        }
      });
    rig.Play("key F8; wait 1\nkey F8; wait 0.2\nkey x; wait 0.2\nkey Home; wait 0.2\ndigest prices\nkey F9; wait 0.5\ndigest left");
  }
};

} // namespace GameLogicTests
