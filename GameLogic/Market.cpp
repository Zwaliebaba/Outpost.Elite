#include "pch.h"

#include "Docked.h"
#include "Market.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Equipment.h"
#include "Text.h"

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_ZERO;
using Machine::Registers;

// The routines these call by their entries: whatever is hooked there runs, so each work routine that a routine that
// waits calls is compared on its own (ADR-010 item 8).
constexpr std::uint16_t PRINT_TEXT_MODE_STRING = 0x60D2;
constexpr std::uint16_t PRINT_CREDITS_ON_MESSAGE_LINE = 0x658F;
constexpr std::uint16_t SPEND_CREDITS = 0x65EC;
constexpr std::uint16_t ADD_CREDITS = 0x65EE;
constexpr std::uint16_t FORMAT_TENTHS = 0x69B3;
constexpr std::uint16_t COMPUTE_MARKET_PRICES = 0x69CE;
constexpr std::uint16_t PARSE_QUANTITY = 0x6A99;
constexpr std::uint16_t PRINT_CARGO_QUANTITY = 0x6AD9;
constexpr std::uint16_t ADD_CONTRABAND_PENALTY = 0x6DC1;
constexpr std::uint16_t READ_TEXT_LINE = 0x7694;
constexpr std::uint16_t DRAW_DOCKED_FRAME = 0x7C88;

// Where the original's backward jumps land, for the turns of its loops (JumpBack).
constexpr std::uint16_t SYSTEM_NAME_SCAN = 0x5E39;
constexpr std::uint16_t PRICE_ROW = 0x5E69;
constexpr std::uint16_t CARGO_STEER = 0x6B43;
constexpr std::uint16_t CARGO_CURSOR_UP = 0x6B4E;
constexpr std::uint16_t CARGO_CURSOR_DOWN = 0x6B7D;
constexpr std::uint16_t CARGO_TICK_LOOP = 0x6BA5;
constexpr std::uint16_t CARGO_MESSAGE = 0x6BE8;
constexpr std::uint16_t NOT_ENOUGH_TO_SELL = 0x6C50;
constexpr std::uint16_t CARGO_MESSAGE_JUMP = 0x6CA6;
constexpr std::uint16_t BUY_REFUSED = 0x6D37;
constexpr std::uint16_t BUY_PAYMENT = 0x6D6A;
constexpr std::uint16_t BUY_MESSAGE_JUMP = 0x6DA2;

constexpr std::uint8_t SCAN_S = 0x1F;
constexpr std::uint8_t SCAN_B = 0x30;
constexpr std::uint8_t SCAN_F2 = 0x3C;
constexpr std::uint8_t SCAN_F8 = 0x42;
constexpr std::uint8_t SCAN_UP = 0x48;
constexpr std::uint8_t SCAN_DOWN = 0x50;

// The text page: offsets in B800, 80 bytes a row.
constexpr std::uint16_t ROW_BYTES = 0x50;
constexpr std::uint16_t TITLE_OFFSET = 0x54;
constexpr std::uint16_t MESSAGE_OFFSET = 0x78;
constexpr std::uint16_t QUANTITY_INPUT_OFFSET = 0x8C;
constexpr std::uint16_t PRICES_HEADER_OFFSET = 0x144;
constexpr std::uint16_t FIRST_PRODUCT_OFFSET = 0x1E4;
constexpr std::uint16_t CARGO_MENU_FIRST_ROW = 0x1E5;
constexpr std::uint16_t BUY_PRICE_COLUMN = 4;
constexpr std::uint16_t SELL_PRICE_COLUMN = 2;
constexpr std::uint16_t QUANTITY_COLUMN = 0x31;

constexpr std::uint8_t CARGO_MESSAGE_ATTRIBUTE = 0x4E;
constexpr std::uint8_t QUANTITY_ATTRIBUTE = 0x7C;

constexpr std::uint16_t PRICES_HEADER_TEXT = 0x7BCC;
constexpr std::uint16_t NO_QUANTITY_TEXT = 0x8222;
// The cargo menu's messages, 19 characters each.
constexpr std::uint16_t NOTHING_TO_SELL_TEXT = 0x8CB2;
constexpr std::uint16_t SOLD_TEXT = 0x8CC6;
constexpr std::uint16_t QUANTITY_PROMPT_TEXT = 0x8CDA;
constexpr std::uint16_t NOTHING_SOLD_TEXT = 0x8CEE;
constexpr std::uint16_t QUANTITY_ERROR_TEXT = 0x8D02;
constexpr std::uint16_t NOT_ENOUGH_TO_SELL_TEXT = 0x8D16;
constexpr std::uint16_t NOTHING_TO_BUY_TEXT = 0x8D2A;
constexpr std::uint16_t BOUGHT_TEXT = 0x8D3E;
constexpr std::uint16_t CARGO_BAY_FULL_TEXT = 0x8D52;
constexpr std::uint16_t NOTHING_BOUGHT_TEXT = 0x8D66;
constexpr std::uint16_t FULL_TO_CAPACITY_TEXT = 0x8D7A;
constexpr std::uint16_t CANNOT_BUY_TEXT = 0x8D8E;
constexpr std::uint16_t NOT_ENOUGH_CREDITS_TEXT = 0x8DA2;
constexpr std::uint16_t CANNOT_CARRY_TEXT = 0x8DB6;
constexpr std::uint16_t NOT_ENOUGH_TO_BUY_TEXT = 0x8DCA;

constexpr std::uint16_t RESALE_PRICE_CEILING = 100;
constexpr std::uint16_t COMMODITY_COUNT = 0x11;
constexpr std::uint16_t PRODUCT_NAME_BYTES = 0x11;
// A price factor of 1.0, in 256ths.
constexpr std::uint16_t UNIT_FACTOR = 0x100;
// The price factor tables are by commodity, 16 bytes to a row.
constexpr std::uint16_t PRICE_FACTOR_ROW_BYTES = 0x10;
constexpr std::uint16_t TRADE_RECORD_BYTES = 3;
constexpr std::uint8_t MARKET_TECH_LEVEL_MOST = 9;

// The cargo menu's rows, from 1: the commodities in cargoHold's order, each with the amount held and the amount
// the market has. From gold on they are counted in kilograms or grams, outside the hold's tonnes; alien items, the
// last, cannot be bought.
constexpr std::uint8_t FIRST_PRECIOUS_ROW = 0x0E;
constexpr std::uint8_t ALIEN_ITEMS_ROW = 0x11;
constexpr std::uint8_t CARGO_BAY_TONNES = 0x14;
constexpr std::uint8_t LARGE_CARGO_BAY_TONNES = 0x23;
constexpr std::uint8_t PRECIOUS_MOST = 0xFA;
constexpr std::uint8_t QUANTITY_DIGITS = 3;
constexpr std::uint8_t QUANTITY_DIGITS_BLANKED = 4;
constexpr std::uint16_t QUANTITY_MOST = 0xFA;
constexpr std::uint8_t QUANTITY_BASE = 10; // ParseQuantity's decimal, which the original keeps in BH

// MUL by _factor, then MOV AL,AH / MOV AH,DL: the product over 256, in 16 bits.
[[nodiscard]] constexpr std::uint16_t ScaledProduct(std::uint16_t _value, std::uint16_t _factor) noexcept
{
  return static_cast<std::uint16_t>((std::uint32_t{_value} * _factor) >> 8);
}

// MUL r/m16: DX:AX = AX * _factor.
void MultiplyWord(Machine::Registers& _regs, std::uint16_t _factor) noexcept
{
  const std::uint32_t product = std::uint32_t{_regs.ax} * _factor;
  _regs.dx = static_cast<std::uint16_t>(product >> 16);
  _regs.ax = static_cast<std::uint16_t>(product);
}

// The row's cargoHold entry: the amount held, then the amount the market has. MOV BL,[menuSelectedRow] / XOR BH,BH /
// DEC BX / SHL BX,1 / ADD BX,cargoHold.
[[nodiscard]] std::uint16_t HoldEntry(Guest& _guest) noexcept
{
  return static_cast<std::uint16_t>((_guest.Get(DS.menuSelectedRow) - 1) * 2 + DS.cargoHold.offset);
}

// 6BE8: the message at SI on the message line in 4Eh, textAttribute and SI put back, and the jump back to the
// steering.
void ShowCargoMessage(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.di = MESSAGE_OFFSET;
  _guest.Set(DS.textAttribute, CARGO_MESSAGE_ATTRIBUTE);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.ax = _guest.Pop();
  _guest.Set(DS.textAttribute, Low(regs.ax));
  regs.si = _guest.Pop();
  _guest.JumpBack(CARGO_STEER);
}

// 6C05 and 6CB3: SI and textAttribute saved for ShowCargoMessage, and the message attribute set.
void OpenCargoMessage(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Push(regs.si);
  SetLow(regs.ax, _guest.Get(DS.textAttribute));
  _guest.Push(regs.ax);
  _guest.Set(DS.textAttribute, CARGO_MESSAGE_ATTRIBUTE);
}

// 6C25 and 6D10: "Quantity?", up to three characters typed into quantityText, and ParseQuantity on them.
void ReadQuantity(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.si = QUANTITY_PROMPT_TEXT;
  regs.di = MESSAGE_OFFSET;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = DS.quantityText.offset;
  regs.di = QUANTITY_INPUT_OFFSET;
  regs.cx = QUANTITY_DIGITS;
  _guest.Call(READ_TEXT_LINE);
  regs.di = DS.quantityText.offset;
  _guest.Call(PARSE_QUANTITY);
}

// 6BFB: S on the cargo menu, on the sell screen only: a typed quantity from the hold to the market, for the sell price.
void SellCargo(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.tradeScreenIsBuy) == 1)
  {
    _guest.JumpBack(CARGO_STEER);
    return;
  }
  OpenCargoMessage(_guest);
  regs.bx = HoldEntry(_guest);
  SetLow(regs.ax, _guest.Byte(regs.bx));
  regs.si = NOTHING_TO_SELL_TEXT;
  if (Low(regs.ax) == 0)
  {
    _guest.JumpBack(CARGO_MESSAGE);
    ShowCargoMessage(_guest);
    return;
  }
  ReadQuantity(_guest);
  if (_guest.Flag(FLAG_CARRY) || regs.ax == 0)
  {
    regs.si = _guest.Flag(FLAG_CARRY) ? QUANTITY_ERROR_TEXT : NOTHING_SOLD_TEXT;
    _guest.JumpBack(CARGO_MESSAGE);
    ShowCargoMessage(_guest);
    return;
  }
  regs.bx = HoldEntry(_guest);
  const std::uint8_t quantity = Low(regs.ax);
  if (_guest.Byte(regs.bx) < quantity)
  {
    _guest.JumpBack(NOT_ENOUGH_TO_SELL);
    regs.si = NOT_ENOUGH_TO_SELL_TEXT;
    _guest.JumpBack(CARGO_MESSAGE);
    ShowCargoMessage(_guest);
    return;
  }
  _guest.Push(regs.ax);
  _guest.SetByte(regs.bx, static_cast<std::uint8_t>(_guest.Byte(regs.bx) - quantity));
  // The market's amount goes up, and is put at FFh if that carries.
  const auto available = static_cast<std::uint16_t>(_guest.Byte(static_cast<std::uint16_t>(regs.bx + 1)) + quantity);
  _guest.SetByte(static_cast<std::uint16_t>(regs.bx + 1), static_cast<std::uint8_t>(available));
  if (available > 0xFF)
  {
    _guest.SetByte(static_cast<std::uint16_t>(regs.bx + 1), 0xFF);
  }
  const std::uint8_t row = _guest.Get(DS.menuSelectedRow);
  if (row == ALIEN_ITEMS_ROW || row < FIRST_PRECIOUS_ROW)
  {
    _guest.Set(DS.cargoUsedTonnes, static_cast<std::uint8_t>(_guest.Get(DS.cargoUsedTonnes) - quantity));
  }
  SetLow(regs.ax, _guest.Byte(regs.bx));
  _guest.Call(PRINT_CARGO_QUANTITY);
  regs.ax = _guest.Pop();
  _guest.Call(ADD_CONTRABAND_PENALTY);
  // The quantity times the sell price, the word after the row's buy price.
  regs.bx = static_cast<std::uint16_t>((_guest.Get(DS.menuSelectedRow) * 2 - 1) * 2 + DS.screenPrices.offset);
  MultiplyWord(regs, _guest.Word(regs.bx));
  regs.bx = regs.dx;
  _guest.Call(ADD_CREDITS);
  regs.si = SOLD_TEXT;
  _guest.JumpBack(CARGO_MESSAGE);
  ShowCargoMessage(_guest);
}

// 6D37, reached by a jump back: the refusal at SI, by way of the jump back to 6BE8.
void RefuseBuy(Guest& _guest)
{
  _guest.JumpBack(BUY_REFUSED);
  _guest.JumpBack(CARGO_MESSAGE);
  ShowCargoMessage(_guest);
}

// 6CA9: B on the cargo menu, on the buy screen only: a typed quantity from the market into the hold, if there is room
// and the cash, for the buy price.
void BuyCargo(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  if (_guest.Get(DS.tradeScreenIsBuy) != 1)
  {
    _guest.JumpBack(CARGO_STEER);
    return;
  }
  OpenCargoMessage(_guest);
  regs.bx = HoldEntry(_guest);
  SetLow(regs.ax, _guest.Byte(static_cast<std::uint16_t>(regs.bx + 1)));
  regs.si = NOTHING_TO_BUY_TEXT;
  if (Low(regs.ax) == 0)
  {
    _guest.JumpBack(CARGO_MESSAGE_JUMP);
    _guest.JumpBack(CARGO_MESSAGE);
    ShowCargoMessage(_guest);
    return;
  }
  regs.si = CARGO_BAY_FULL_TEXT;
  SetLow(regs.ax, _guest.Get(DS.largeCargoBayFitted) == 1 ? LARGE_CARGO_BAY_TONNES : CARGO_BAY_TONNES);
  if (_guest.Get(DS.menuSelectedRow) == ALIEN_ITEMS_ROW)
  {
    regs.si = CANNOT_BUY_TEXT;
    _guest.JumpBack(CARGO_MESSAGE);
    ShowCargoMessage(_guest);
    return;
  }
  if (_guest.Get(DS.menuSelectedRow) >= FIRST_PRECIOUS_ROW)
  {
    regs.bx = HoldEntry(_guest);
    if (_guest.Byte(regs.bx) == PRECIOUS_MOST)
    {
      regs.si = FULL_TO_CAPACITY_TEXT;
      _guest.JumpBack(CARGO_MESSAGE);
      ShowCargoMessage(_guest);
      return;
    }
  }
  else if (_guest.Get(DS.cargoUsedTonnes) == Low(regs.ax))
  {
    _guest.JumpBack(CARGO_MESSAGE_JUMP);
    _guest.JumpBack(CARGO_MESSAGE);
    ShowCargoMessage(_guest);
    return;
  }
  ReadQuantity(_guest);
  regs.si = QUANTITY_ERROR_TEXT;
  if (_guest.Flag(FLAG_CARRY))
  {
    _guest.JumpBack(CARGO_MESSAGE);
    ShowCargoMessage(_guest);
    return;
  }
  if (regs.ax == 0)
  {
    regs.si = NOTHING_BOUGHT_TEXT;
    _guest.JumpBack(CARGO_MESSAGE);
    ShowCargoMessage(_guest);
    return;
  }
  regs.bx = HoldEntry(_guest);
  regs.si = NOT_ENOUGH_TO_BUY_TEXT;
  const std::uint8_t quantity = Low(regs.ax);
  if (_guest.Byte(static_cast<std::uint16_t>(regs.bx + 1)) < quantity)
  {
    RefuseBuy(_guest);
    return;
  }
  SetLow(regs.cx, _guest.Byte(regs.bx));
  if (_guest.Get(DS.menuSelectedRow) >= FIRST_PRECIOUS_ROW)
  {
    // Up to 250 of each, kept apart from the tonnes.
    const auto held = static_cast<std::uint16_t>(Low(regs.cx) + quantity);
    SetLow(regs.cx, static_cast<std::uint8_t>(held));
    regs.si = CANNOT_CARRY_TEXT;
    if (held > 0xFF || Low(regs.cx) > PRECIOUS_MOST)
    {
      RefuseBuy(_guest);
      return;
    }
  }
  else
  {
    // The tonnes after it must fit the hold, in 8 bits as the original adds them.
    SetLow(regs.cx, static_cast<std::uint8_t>(_guest.Get(DS.cargoUsedTonnes) + quantity));
    SetHigh(regs.cx, static_cast<std::uint8_t>((_guest.Get(DS.largeCargoBayFitted) == 1 ? LARGE_CARGO_BAY_TONNES : CARGO_BAY_TONNES) + 1));
    regs.si = CANNOT_CARRY_TEXT;
    if (Low(regs.cx) >= High(regs.cx))
    {
      _guest.JumpBack(BUY_MESSAGE_JUMP);
      _guest.JumpBack(CARGO_MESSAGE);
      ShowCargoMessage(_guest);
      return;
    }
    _guest.JumpBack(BUY_PAYMENT);
  }
  _guest.Push(regs.ax);
  _guest.Push(regs.bx);
  regs.bx = static_cast<std::uint16_t>((_guest.Get(DS.menuSelectedRow) - 1) * 4 + DS.screenPrices.offset);
  MultiplyWord(regs, _guest.Word(regs.bx));
  regs.bx = regs.dx;
  _guest.Call(SPEND_CREDITS);
  regs.bx = _guest.Pop();
  regs.ax = _guest.Pop();
  regs.si = NOT_ENOUGH_CREDITS_TEXT;
  if (_guest.Flag(FLAG_CARRY))
  {
    RefuseBuy(_guest);
    return;
  }
  _guest.SetByte(regs.bx, static_cast<std::uint8_t>(_guest.Byte(regs.bx) + quantity));
  if (_guest.Get(DS.menuSelectedRow) < FIRST_PRECIOUS_ROW)
  {
    _guest.Set(DS.cargoUsedTonnes, static_cast<std::uint8_t>(_guest.Get(DS.cargoUsedTonnes) + quantity));
  }
  const auto left = static_cast<std::uint8_t>(_guest.Byte(static_cast<std::uint16_t>(regs.bx + 1)) - quantity);
  _guest.SetByte(static_cast<std::uint16_t>(regs.bx + 1), left);
  SetLow(regs.ax, left);
  _guest.Call(PRINT_CARGO_QUANTITY);
  regs.si = BOUGHT_TEXT;
  _guest.JumpBack(CARGO_MESSAGE);
  ShowCargoMessage(_guest);
}

} // namespace

void ShowMarketPricesScreen(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.si = DS.marketPricesFrame.offset;
  _guest.Call(DRAW_DOCKED_FRAME);
  regs.di = TITLE_OFFSET;
  _guest.Push(regs.si);
  // The system's name, cut for good at its first space after the first letter.
  regs.si = DS.currentSystemName.offset;
  for (;;)
  {
    ++regs.si;
    SetLow(regs.ax, _guest.Byte(regs.si));
    if (Low(regs.ax) == 0)
    {
      break;
    }
    if (Low(regs.ax) == ' ')
    {
      _guest.SetByte(regs.si, 0);
      break;
    }
    _guest.JumpBack(SYSTEM_NAME_SCAN);
  }
  regs.si = DS.currentSystemName.offset;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = _guest.Pop();
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = PRICES_HEADER_TEXT;
  regs.di = PRICES_HEADER_OFFSET;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.Call(COMPUTE_MARKET_PRICES);

  // Each commodity's name, buy price and sell price.
  regs.si = DS.productNames.offset;
  regs.di = FIRST_PRODUCT_OFFSET;
  regs.bx = DS.screenPrices.offset;
  regs.cx = COMMODITY_COUNT;
  for (;;)
  {
    _guest.Push(regs.cx);
    _guest.Push(regs.si);
    _guest.Push(regs.di);
    _guest.Push(regs.bx);
    _guest.Call(PRINT_TEXT_MODE_STRING);
    regs.bx = _guest.Pop();
    regs.ax = _guest.Word(regs.bx);
    _guest.Push(regs.bx);
    _guest.Push(regs.di);
    _guest.Call(FORMAT_TENTHS);
    regs.si = DS.priceText.offset;
    regs.di = static_cast<std::uint16_t>(_guest.Pop() + BUY_PRICE_COLUMN);
    _guest.Call(PRINT_TEXT_MODE_STRING);
    regs.bx = _guest.Pop();
    regs.ax = _guest.Word(static_cast<std::uint16_t>(regs.bx + 2));
    _guest.Push(regs.bx);
    _guest.Push(regs.di);
    _guest.Call(FORMAT_TENTHS);
    regs.si = DS.priceText.offset;
    regs.di = static_cast<std::uint16_t>(_guest.Pop() + SELL_PRICE_COLUMN);
    _guest.Call(PRINT_TEXT_MODE_STRING);
    regs.bx = static_cast<std::uint16_t>(_guest.Pop() + 4);
    regs.di = static_cast<std::uint16_t>(_guest.Pop() + ROW_BYTES);
    regs.si = static_cast<std::uint16_t>(_guest.Pop() + PRODUCT_NAME_BYTES);
    regs.cx = static_cast<std::uint16_t>(_guest.Pop() - 1);
    if (regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(PRICE_ROW);
  }
  SetLow(regs.dx, SCAN_F8);
  WaitForScreenExitKey(_guest);
}

bool SubtractCredits(GameState& _state, std::uint32_t _tenths)
{
  // SUB, then SBB: the 32-bit credits less _tenths, then ADD and ADC to put them back if that borrows.
  const std::uint32_t credits = _state.Word(DS.creditsTenths.offset) | (std::uint32_t{_state.Get(DS.data75F5)} << 16);
  const std::uint32_t left = credits - _tenths;
  _state.SetWord(DS.creditsTenths.offset, static_cast<std::uint16_t>(left));
  _state.Set(DS.data75F5, static_cast<std::uint16_t>(left >> 16));
  if (credits < _tenths)
  {
    _state.SetWord(DS.creditsTenths.offset, static_cast<std::uint16_t>(credits));
    _state.Set(DS.data75F5, static_cast<std::uint16_t>(credits >> 16));
    return false;
  }
  FormatCredits(_state);
  return true;
}

bool SpendCredits(GameState& _state, std::uint32_t _tenths)
{
  return SubtractCredits(_state, _tenths);
}

void AddCredits(GameState& _state, std::uint32_t _tenths)
{
  // ADD the low word, then ADC the high word with the carry it makes.
  const std::uint32_t low = std::uint32_t{_state.Word(DS.creditsTenths.offset)} + (_tenths & 0xFFFF);
  _state.SetWord(DS.creditsTenths.offset, static_cast<std::uint16_t>(low));
  _state.Set(DS.data75F5, static_cast<std::uint16_t>(_state.Get(DS.data75F5) + (_tenths >> 16) + (low >> 16)));
  FormatCredits(_state);
}

std::uint16_t ComputeResalePrice(const GameState& _state)
{
  const std::uint16_t price = _state.Get(DS.resalePriceInput);
  if (price == 0)
  {
    return 0;
  }
  auto cut = static_cast<std::uint16_t>(price >> 5);
  while (cut >= RESALE_PRICE_CEILING)
  {
    cut = static_cast<std::uint16_t>(cut >> 1);
  }
  return static_cast<std::uint16_t>(price - cut - 1);
}

void ComputeMarketPrices(GameState& _state)
{
  _state.Set(DS.data8C10, static_cast<std::uint16_t>(_state.Get(DS.currentEconomy) * 2 + DS.economyPriceFactors.offset));
  _state.Set(DS.data8C12, static_cast<std::uint16_t>(_state.Get(DS.currentGovernment) * 2 + DS.governmentPriceFactors.offset));
  // The species branch at 0x69EE (data75BC, data75BF) loads AL only for the tech level to replace it.
  std::uint8_t techLevel = _state.Get(DS.currentTechLevel);
  if (techLevel > MARKET_TECH_LEVEL_MOST)
  {
    techLevel = MARKET_TECH_LEVEL_MOST;
  }
  _state.Set(DS.marketTechLevel, techLevel);
  _state.Set(DS.data8C14, DS.productTradeRecords.offset);
  std::uint16_t prices = DS.screenPrices.offset;
  std::uint16_t basePrice = DS.productBasePrices.offset;
  for (std::uint16_t commodity = 0; commodity < COMMODITY_COUNT; ++commodity)
  {
    const std::uint16_t economyFactor = _state.Get(DS.data8C10);
    std::uint16_t price = ScaledProduct(UNIT_FACTOR, _state.Word(economyFactor));
    _state.Set(DS.data8C10, Offset(economyFactor, PRICE_FACTOR_ROW_BYTES));
    const std::uint16_t governmentFactor = _state.Get(DS.data8C12);
    price = ScaledProduct(price, _state.Word(governmentFactor));
    _state.Set(DS.data8C12, Offset(governmentFactor, PRICE_FACTOR_ROW_BYTES));
    price = ScaledProduct(price, UNIT_FACTOR);
    price = ScaledProduct(price, _state.Word(basePrice));
    basePrice = Offset(basePrice, 2);

    // 256 + the price adjustment + the tech-level factor times marketTechLevel, both signed bytes.
    const std::uint16_t record = _state.Get(DS.data8C14);
    const auto techFactor = static_cast<std::int8_t>(_state.Byte(Offset(record, 1)));
    const auto techTerm = static_cast<std::uint16_t>(techFactor * static_cast<std::int8_t>(_state.Get(DS.marketTechLevel)));
    const auto adjustment = static_cast<std::int8_t>(_state.Byte(record));
    const auto factor = static_cast<std::uint16_t>(static_cast<std::uint16_t>(adjustment) + UNIT_FACTOR + techTerm);
    _state.Set(DS.data8C14, Offset(record, TRADE_RECORD_BYTES));
    price = ScaledProduct(price, factor);

    _state.SetWord(prices, price);
    _state.Set(DS.resalePriceInput, price);
    _state.SetWord(Offset(prices, 2), ComputeResalePrice(_state));
    prices = Offset(prices, 4);
  }
}

std::uint16_t NextMarketRandom(GameState& _state)
{
  const std::uint16_t a = _state.Word(DS.marketRandomState.offset);
  const std::uint16_t b = _state.Get(DS.data8C20);
  const std::uint16_t c = _state.Get(DS.data8C22);
  const auto sum = static_cast<std::uint16_t>(a + b);
  // In the order the original's two XCHGs and its ADD write them.
  _state.Set(DS.data8C20, c);
  _state.SetWord(DS.marketRandomState.offset, b);
  _state.Set(DS.data8C22, static_cast<std::uint16_t>(c + sum));
  return sum;
}

Quantity ParseQuantity(const GameState& _state, std::uint16_t _text)
{
  Quantity quantity{.value = 0, .valid = false, .end = _text, .lastRead = 0};
  const auto next = [&_state, &quantity]()
  {
    quantity.lastRead = _state.Byte(quantity.end);
    quantity.end = Offset(quantity.end, 1);
    return quantity.lastRead;
  };
  std::uint8_t character = 0;
  do
  {
    character = next();
  } while (character == ' ');
  while (character != 0)
  {
    // A digit: AL times ten, plus it. MUL BH takes only AL, so a number past 255 has lost its high byte.
    quantity.lastRead = static_cast<std::uint8_t>(character - '0');
    if (character < '0' || quantity.lastRead >= 10)
    {
      return quantity;
    }
    quantity.value = static_cast<std::uint16_t>(Low(quantity.value) * QUANTITY_BASE + quantity.lastRead);
    character = next();
    if (character == ' ')
    {
      // Only spaces may follow.
      do
      {
        character = next();
      } while (character == ' ');
      if (character != 0)
      {
        return quantity;
      }
    }
  }
  quantity.valid = quantity.value <= QUANTITY_MOST;
  return quantity;
}

PrintedText PrintCargoQuantity(GameState& _state, std::uint16_t _quantity)
{
  std::uint16_t text = NO_QUANTITY_TEXT;
  if (_quantity != 0)
  {
    FormatDecimal5(_state, _quantity, DS.quantityText.offset);
    BlankLeadingZeros(_state, DS.quantityText.offset, QUANTITY_DIGITS_BLANKED);
    text = DS.quantityText.offset;
  }
  const std::uint8_t attribute = _state.Get(DS.textAttribute);
  _state.Set(DS.textAttribute, QUANTITY_ATTRIBUTE);
  // (row-1)*80 bytes, as (row-1)*256/4 and that /4 again.
  const auto quarter = static_cast<std::uint16_t>((static_cast<std::uint8_t>(_state.Get(DS.menuSelectedRow) - 1) << 8) >> 2);
  const auto cell = static_cast<std::uint16_t>(_state.Get(DS.menuFirstRowAttr) + QUANTITY_COLUMN + quarter + (quarter >> 2));
  const PrintedText printed = PrintTextModeString(_state, text, cell);
  _state.Set(DS.textAttribute, attribute);
  return printed;
}

void RunCargoTradeMenu(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  _guest.Set(DS.menuFirstRowAttr, CARGO_MENU_FIRST_ROW);
  _guest.Set(DS.menuRowCount, static_cast<std::uint8_t>(COMMODITY_COUNT));
  StartMenuOnRegisters(_guest);
  for (;;)
  {
    SteerMenuCursor(_guest);
    for (;;)
    {
      if (!PollMenuKey(_guest, CARGO_TICK_LOOP))
      {
        _guest.JumpBack(CARGO_STEER);
        break;
      }
      _guest.Push(regs.ax);
      _guest.Call(PRINT_CREDITS_ON_MESSAGE_LINE);
      regs.ax = _guest.Pop();
      const std::uint8_t key = High(regs.ax);
      if (key == SCAN_B)
      {
        BuyCargo(_guest);
        break;
      }
      if (key == SCAN_S)
      {
        SellCargo(_guest);
        break;
      }
      if (key == SCAN_UP)
      {
        _guest.JumpBack(CARGO_CURSOR_UP);
        MoveMenuCursorUpOnRegisters(_guest);
        continue;
      }
      if (key == SCAN_DOWN)
      {
        _guest.JumpBack(CARGO_CURSOR_DOWN);
        MoveMenuCursorDownOnRegisters(_guest);
        continue;
      }
      // The screen's own key, F2 to sell or F3 to buy, does nothing here.
      SetLow(regs.dx, static_cast<std::uint8_t>(SCAN_F2 + _guest.Get(DS.tradeScreenIsBuy)));
      if (key != Low(regs.dx) && IsScreenKey(key))
      {
        return;
      }
      _guest.JumpBack(CARGO_STEER);
      break;
    }
  }
}

std::uint8_t AddContrabandPenalty(GameState& _state)
{
  // The row's trade record, three bytes from data8BDD, in 8 bits: its third byte is the commodity's legal penalty.
  const auto index = static_cast<std::uint8_t>(_state.Get(DS.menuSelectedRow) - 1);
  const auto record = static_cast<std::uint8_t>(index * 2 + index);
  const auto status = static_cast<std::uint8_t>(_state.Byte(Offset(DS.data8BDF.offset, record)) + _state.Get(DS.legalStatus));
  if (status != 0)
  {
    _state.Set(DS.legalStatus, status);
  }
  return status;
}

// ── Their entries ──

namespace
{

using Machine::NativeContract;
using Machine::NativeReturn;
using Machine::NativeWait;
using Machine::REGISTER_ALL;
using Machine::REGISTER_BP;
using Machine::REGISTER_DS;

constexpr NativeContract CLOBBERS_ALL{REGISTER_ALL, 0};
// ComputeMarketPrices': the original leaves DS alone, and code that runs after it uncompared reads DS (ADR-012).
constexpr NativeContract CLOBBERS_ALL_BUT_DS_BP{static_cast<std::uint16_t>(REGISTER_ALL & ~REGISTER_DS & ~REGISTER_BP), 0};
constexpr NativeContract RETURNS_CARRY{0, FLAG_CARRY};
constexpr NativeContract RETURNS_ZERO{0, FLAG_ZERO};

} // namespace

void SubtractCreditsEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const bool paid = SubtractCredits(_guest.State(), regs.ax | (std::uint32_t{regs.bx} << 16));
  if (paid)
  {
    // FormatCredits leaves SI on the balance's text.
    regs.si = DS.creditBalanceText.offset;
  }
  _guest.SetFlag(FLAG_CARRY, !paid);
  _guest.Clobber(RETURNS_CARRY);
}

void SpendCreditsEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const bool paid = SpendCredits(_guest.State(), regs.ax | (std::uint32_t{regs.bx} << 16));
  if (paid)
  {
    // FormatCredits leaves SI on the balance's text.
    regs.si = DS.creditBalanceText.offset;
  }
  _guest.SetFlag(FLAG_CARRY, !paid);
  _guest.Clobber(RETURNS_CARRY);
}

void AddCreditsEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  AddCredits(_guest.State(), regs.ax | (std::uint32_t{regs.bx} << 16));
  // FormatCredits leaves SI on the balance's text.
  regs.si = DS.creditBalanceText.offset;
  _guest.Clobber(PRESERVES_ALL);
}

void ComputeResalePriceEntry(Guest& _guest)
{
  const std::uint16_t price = ComputeResalePrice(_guest.State());
  _guest.Regs().ax = price;
  _guest.SetFlag(FLAG_ZERO, price == 0);
  _guest.Clobber(RETURNS_ZERO);
}

void ComputeMarketPricesEntry(Guest& _guest)
{
  ComputeMarketPrices(_guest.State());
  _guest.Clobber(CLOBBERS_ALL_BUT_DS_BP);
}

void NextMarketRandomEntry(Guest& _guest)
{
  _guest.Regs().ax = NextMarketRandom(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void ParseQuantityEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const Quantity quantity = ParseQuantity(_guest.State(), regs.di);
  regs.ax = quantity.value;
  // The original reads each character into BL, with BH the base it multiplies by, and leaves DI past the last.
  regs.bx = Join(QUANTITY_BASE, quantity.lastRead);
  regs.di = quantity.end;
  _guest.SetFlag(FLAG_CARRY, !quantity.valid);
  _guest.Clobber(RETURNS_CARRY);
}

void PrintCargoQuantityEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint16_t quantity = regs.ax;
  const std::uint8_t attribute = _guest.Get(DS.textAttribute);
  const PrintedText printed = PrintCargoQuantity(_guest.State(), quantity);
  regs.si = printed.end;
  regs.di = printed.nextCell;
  regs.es = Guest::VIDEO_SEGMENT;
  if (quantity != 0)
  {
    regs.cx = BlankedLeadingZeros(_guest.State(), DS.quantityText.offset, QUANTITY_DIGITS_BLANKED).triesLeft;
  }
  // The original pops the attribute it kept into AL, AH FormatDecimal5's units' high byte, 0 below 10, or the 0 it
  // tested.
  regs.ax = attribute;
  _guest.Clobber(PRESERVES_ALL);
}

void AddContrabandPenaltyEntry(Guest& _guest)
{
  // The original sums in BL, BH clear, and leaves the sum there.
  _guest.Regs().bx = AddContrabandPenalty(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x5E2C, "ShowMarketPricesScreen", &ShowMarketPricesScreen, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x65EC, "SpendCredits", &SpendCreditsEntry, RETURNS_CARRY},
  NativeEntry{0x65EE, "AddCredits", &AddCreditsEntry, PRESERVES_ALL},
  NativeEntry{0x6995, "ComputeResalePrice", &ComputeResalePriceEntry, RETURNS_ZERO},
  NativeEntry{0x69CE, "ComputeMarketPrices", &ComputeMarketPricesEntry, CLOBBERS_ALL_BUT_DS_BP},
  NativeEntry{0x6A85, "NextMarketRandom", &NextMarketRandomEntry, PRESERVES_ALL},
  NativeEntry{0x6A99, "ParseQuantity", &ParseQuantityEntry, RETURNS_CARRY},
  NativeEntry{0x6AD9, "PrintCargoQuantity", &PrintCargoQuantityEntry, PRESERVES_ALL},
  NativeEntry{0x6B1E, "RunCargoTradeMenu", &RunCargoTradeMenu, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x6DC1, "AddContrabandPenalty", &AddContrabandPenaltyEntry, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> MarketEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
