#include "pch.h"

#include "Docked.h"
#include "Market.h"

#include "Arithmetic.h"
#include "Equipment.h"
#include "MarketTables.h"
#include "Text.h"

namespace Elite
{

namespace
{

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
constexpr std::uint16_t CARGO_MENU_FIRST_ROW = 6; // on the text page
constexpr std::uint16_t BUY_PRICE_COLUMN = 4;
constexpr std::uint16_t SELL_PRICE_COLUMN = 2;
constexpr std::uint16_t QUANTITY_COLUMN = 0x31;

constexpr std::uint8_t CARGO_MESSAGE_ATTRIBUTE = 0x4E;
constexpr std::uint8_t QUANTITY_ATTRIBUTE = 0x7C;

// The cargo menu's messages, 19 characters each.

constexpr std::uint16_t RESALE_PRICE_CEILING = 100;
constexpr std::uint16_t COMMODITY_COUNT = 0x11;
constexpr std::uint16_t PRODUCT_NAME_BYTES = 0x11;
constexpr std::size_t TRADE_PRICES_BYTES = 4; // a product's buy price and then its sell price, in screenPrices
constexpr std::size_t LEGAL_PENALTY = 2;      // a trade record's third byte
// A price factor of 1.0, in 256ths.
constexpr std::uint16_t UNIT_FACTOR = 0x100;
// The price factor tables are by commodity, 16 bytes to a row.
constexpr std::uint16_t PRICE_FACTOR_ROW_WORDS = 8; // a product's row of economyPriceFactors and governmentPriceFactors
constexpr std::size_t TRADE_RECORD_BYTES = 3;
constexpr std::uint8_t MARKET_TECH_LEVEL_MOST = 9;
// The quantity on sale from a market random number r: ((r & 1Fh) - 7) xor (r >> 8 & 3).
constexpr std::uint8_t QUANTITY_RANDOM_MASK = 0x1F;
constexpr std::uint8_t QUANTITY_RANDOM_BIAS = 7;
constexpr std::uint8_t QUANTITY_RANDOM_HIGH_MASK = 3;

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

// cargoHold's entries: for each product, the amount held, then the amount the market has.
[[nodiscard]] std::span<Counted<std::uint8_t>> Hold(GameState& _state)
{
  return BytesOf(_state.Data().commander.cargoHold);
}

// The index in cargoHold of the row's entry. MOV BL,[menuSelectedRow] / XOR BH,BH / DEC BX / SHL BX,1 / ADD BX,cargoHold.
[[nodiscard]] std::size_t HoldEntry(const GameState& _state) noexcept
{
  return static_cast<std::uint16_t>((_state.Data().menus.menuSelectedRow - 1) * 2);
}

// screenPrices: for each product its buy price and then its sell price, in tenths, a word each.
[[nodiscard]] std::span<Counted<std::uint8_t>> ScreenPrices(GameState& _state)
{
  return BytesOf(_state.Data().menus.screenPrices);
}

// Product _product's name and unit, up to its NUL.
[[nodiscard]] std::string_view ProductName(const GameState& _state, std::uint16_t _product)
{
  return TextWithin(BytesOf(_state.Data().docked.productNames), std::size_t{_product} * PRODUCT_NAME_BYTES);
}

// 6C05 and 6CB3: SI and textAttribute pushed for ShowCargoMessage, and the message's attribute set. Returns the attribute, which
// the original pushes in AL.
[[nodiscard]] std::uint8_t OpenCargoMessage(GameState& _state)
{
  const std::uint8_t saved = _state.Data().drawing.textAttribute;
  _state.Data().drawing.textAttribute = CARGO_MESSAGE_ATTRIBUTE;
  return saved;
}

// 6BE8: the message _text on the message line in 4Eh, and textAttribute put back to _saved, which the original pops into AL
// before its jump back to the steering.
void ShowCargoMessage(GameState& _state, std::string_view _text, std::uint8_t _saved)
{
  _state.Data().drawing.textAttribute = CARGO_MESSAGE_ATTRIBUTE;
  PrintTextModeString(_state, _text, MESSAGE_OFFSET);
  _state.Data().drawing.textAttribute = _saved;
}

// 6C25 and 6D10: "Quantity?", up to three characters typed into quantityText, and ParseQuantity on them.
[[nodiscard]] Quantity ReadQuantity(GameState& _state, Hardware& _hardware)
{
  PrintTextModeString(_state, QUANTITY_PROMPT_TEXT, MESSAGE_OFFSET);
  ReadTextLine(_state, _hardware, _state.Data().menus.quantityText, QUANTITY_DIGITS, GameState::VIDEO_SEGMENT, QUANTITY_INPUT_OFFSET);
  return ParseQuantity(_state.Data().menus.quantityText);
}

// 6C0F: S on the sell screen, once OpenCargoMessage has saved the attribute: a typed quantity from the hold to the market, for
// the sell price. Returns the message for ShowCargoMessage, after the jumps back the original takes to it.
[[nodiscard]] std::string_view SellCargo(GameState& _state, Hardware& _hardware)
{
  if (Hold(_state)[HoldEntry(_state)] == 0)
  {
    return NOTHING_TO_SELL_TEXT;
  }
  const Quantity quantity = ReadQuantity(_state, _hardware);
  if (!quantity.valid)
  {
    return QUANTITY_ERROR_TEXT;
  }
  if (quantity.value == 0)
  {
    return NOTHING_SOLD_TEXT;
  }
  Counted<std::uint8_t>& held = Hold(_state)[HoldEntry(_state)];
  Counted<std::uint8_t>& market = Hold(_state)[HoldEntry(_state) + 1];
  const std::uint8_t amount = Low(quantity.value);
  if (held < amount)
  {
    return NOT_ENOUGH_TO_SELL_TEXT;
  }
  held = static_cast<std::uint8_t>(held - amount);
  // The market's amount goes up, and is put at FFh if that carries.
  const auto available = static_cast<std::uint16_t>(market + amount);
  market = static_cast<std::uint8_t>(available);
  if (available > 0xFF)
  {
    market = 0xFF;
  }
  const std::uint8_t row = _state.Data().menus.menuSelectedRow;
  if (row == ALIEN_ITEMS_ROW || row < FIRST_PRECIOUS_ROW)
  {
    _state.Data().commander.cargoHold.cargoUsedTonnes =
      static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes - amount);
  }
  PrintCargoQuantity(_state, held);
  AddContrabandPenalty(_state);
  // The quantity times the sell price, the word after the row's buy price, by MUL into DX:AX.
  const std::size_t price = (std::size_t{_state.Data().menus.menuSelectedRow} * 2 - 1) * 2;
  AddCredits(_state, std::uint32_t{quantity.value} * WordIn(ScreenPrices(_state), price));
  return SOLD_TEXT;
}

// 6CBD: B on the buy screen, once OpenCargoMessage has saved the attribute: a typed quantity from the market into the hold, if
// there is room and the cash, for the buy price. Returns the message for ShowCargoMessage, after the jumps back the original
// takes to it.
[[nodiscard]] std::string_view BuyCargo(GameState& _state, Hardware& _hardware)
{
  if (Hold(_state)[HoldEntry(_state) + 1] == 0)
  {
    return NOTHING_TO_BUY_TEXT;
  }
  const std::uint8_t bay = _state.Data().commander.largeCargoBayFitted == 1 ? LARGE_CARGO_BAY_TONNES : CARGO_BAY_TONNES;
  if (_state.Data().menus.menuSelectedRow == ALIEN_ITEMS_ROW)
  {
    return CANNOT_BUY_TEXT;
  }
  if (_state.Data().menus.menuSelectedRow >= FIRST_PRECIOUS_ROW)
  {
    if (Hold(_state)[HoldEntry(_state)] == PRECIOUS_MOST)
    {
      return FULL_TO_CAPACITY_TEXT;
    }
  }
  else if (_state.Data().commander.cargoHold.cargoUsedTonnes == bay)
  {
    return CARGO_BAY_FULL_TEXT;
  }
  const Quantity quantity = ReadQuantity(_state, _hardware);
  if (!quantity.valid)
  {
    return QUANTITY_ERROR_TEXT;
  }
  if (quantity.value == 0)
  {
    return NOTHING_BOUGHT_TEXT;
  }
  Counted<std::uint8_t>& held = Hold(_state)[HoldEntry(_state)];
  Counted<std::uint8_t>& market = Hold(_state)[HoldEntry(_state) + 1];
  const std::uint8_t amount = Low(quantity.value);
  if (market < amount)
  {
    return NOT_ENOUGH_TO_BUY_TEXT;
  }
  if (_state.Data().menus.menuSelectedRow >= FIRST_PRECIOUS_ROW)
  {
    // Up to 250 of each, kept apart from the tonnes: ADD CL,AL, which may carry.
    const auto after = static_cast<std::uint16_t>(held + amount);
    if (after > PRECIOUS_MOST) // a carry, or more than 250
    {
      return CANNOT_CARRY_TEXT;
    }
  }
  else
  {
    // The tonnes after it must fit the hold, in 8 bits as the original adds them.
    const auto tonnes = static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + amount);
    const auto limit =
      static_cast<std::uint8_t>((_state.Data().commander.largeCargoBayFitted == 1 ? LARGE_CARGO_BAY_TONNES : CARGO_BAY_TONNES) + 1);
    if (tonnes >= limit)
    {
      return CANNOT_CARRY_TEXT;
    }
  }
  // The quantity times the buy price, by MUL into DX:AX, the original pushing the quantity and the entry round SpendCredits.
  const std::size_t price = (std::size_t{_state.Data().menus.menuSelectedRow} - 1) * 4;
  if (!SpendCredits(_state, std::uint32_t{quantity.value} * WordIn(ScreenPrices(_state), price)))
  {
    return NOT_ENOUGH_CREDITS_TEXT;
  }
  held = static_cast<std::uint8_t>(held + amount);
  if (_state.Data().menus.menuSelectedRow < FIRST_PRECIOUS_ROW)
  {
    _state.Data().commander.cargoHold.cargoUsedTonnes =
      static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + amount);
  }
  const auto left = static_cast<std::uint8_t>(market - amount);
  market = left;
  PrintCargoQuantity(_state, left);
  return BOUGHT_TEXT;
}

} // namespace

ScreenKey ShowMarketPricesScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone)
{
  const std::string_view title = DrawDockedFrame(_state, _hardware, MARKET_PRICES_FRAME, _backward);
  // The system's name, cut for good at its first space after the first letter.
  const std::span<Counted<std::uint8_t>> name(_state.Data().commander.currentSystemName);
  for (std::size_t letter = 1;; ++letter)
  {
    const std::uint8_t character = name[letter];
    if (character == 0)
    {
      break;
    }
    if (character == ' ')
    {
      name[letter] = 0;
      break;
    }
  }
  const std::uint16_t named = PrintTextModeString(_state, TextIn(name), TITLE_OFFSET);
  PrintTextModeString(_state, title, named);
  PrintTextModeString(_state, PRICES_HEADER_TEXT, PRICES_HEADER_OFFSET);
  ComputeMarketPrices(_state);

  // Each commodity's name, buy price and sell price, the original holding the name, the row and the prices in SI, DI and BX.
  const std::span<Counted<std::uint8_t>> prices = ScreenPrices(_state);
  std::uint16_t row = FIRST_PRODUCT_OFFSET;
  for (std::uint16_t product = 0, left = COMMODITY_COUNT;;)
  {
    const std::uint16_t nameEnd = PrintTextModeString(_state, ProductName(_state, product), row);
    FormatTenths(_state, WordIn(prices, product * TRADE_PRICES_BYTES));
    const std::uint16_t buy =
      PrintTextModeString(_state, TextWithin(BytesOf(_state.Data().menus.priceText), 0), Offset(nameEnd, BUY_PRICE_COLUMN));
    FormatTenths(_state, WordIn(prices, product * TRADE_PRICES_BYTES + 2));
    PrintTextModeString(_state, TextWithin(BytesOf(_state.Data().menus.priceText), 0), Offset(buy, SELL_PRICE_COLUMN));
    row = Offset(row, ROW_BYTES);
    ++product;
    if (--left == 0)
    {
      break;
    }
  }
  // The last print leaves AL 0.
  return WaitForScreenExitKey(_state, _hardware, SCAN_F8, 0, _countIfNone);
}

bool SubtractCredits(GameState& _state, std::uint32_t _tenths)
{
  // SUB, then SBB: the 32-bit credits less _tenths, then ADD and ADC to put them back if that borrows.
  CommanderState::CreditsTenths& tenths = _state.Data().commander.creditsTenths;
  const std::uint32_t credits = WordIn(BytesOf(tenths), 0) | (std::uint32_t{tenths.data75F5} << 16);
  const std::uint32_t left = credits - _tenths;
  SetWordIn(BytesOf(tenths), 0, static_cast<std::uint16_t>(left));
  _state.Data().commander.creditsTenths.data75F5 = static_cast<std::uint16_t>(left >> 16);
  if (credits < _tenths)
  {
    SetWordIn(BytesOf(tenths), 0, static_cast<std::uint16_t>(credits));
    _state.Data().commander.creditsTenths.data75F5 = static_cast<std::uint16_t>(credits >> 16);
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
  CommanderState::CreditsTenths& tenths = _state.Data().commander.creditsTenths;
  const std::uint32_t low = std::uint32_t{WordIn(BytesOf(tenths), 0)} + (_tenths & 0xFFFF);
  SetWordIn(BytesOf(tenths), 0, static_cast<std::uint16_t>(low));
  _state.Data().commander.creditsTenths.data75F5 =
    static_cast<std::uint16_t>(_state.Data().commander.creditsTenths.data75F5 + (_tenths >> 16) + (low >> 16));
  FormatCredits(_state);
}

std::uint16_t ComputeResalePrice(const GameState& _state)
{
  const std::uint16_t price = _state.Data().menus.resalePriceInput;
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
  // data8C10 and data8C12 walk the economy's and the government's column of the price factors, a row of eight words to a
  // product, and data8C14 productTradeRecords: each holds the index of the word or the record it is on, where the original
  // holds its address.
  _state.Data().market.data8C10 = std::uint16_t{_state.Data().commander.currentEconomy};
  _state.Data().market.data8C12 = std::uint16_t{_state.Data().commander.currentGovernment};
  // The species branch at 0x69EE (data75BC, data75BF) loads AL only for the tech level to replace it.
  std::uint8_t techLevel = _state.Data().commander.currentTechLevel;
  if (techLevel > MARKET_TECH_LEVEL_MOST)
  {
    techLevel = MARKET_TECH_LEVEL_MOST;
  }
  _state.Data().market.marketTechLevel = techLevel;
  _state.Data().market.data8C14 = 0;
  const std::span<Counted<std::uint8_t>> prices = ScreenPrices(_state);
  for (std::uint16_t commodity = 0; commodity < COMMODITY_COUNT; ++commodity)
  {
    const std::uint16_t economyFactor = _state.Data().market.data8C10;
    std::uint16_t price = ScaledProduct(UNIT_FACTOR, ECONOMY_PRICE_FACTORS[economyFactor]);
    _state.Data().market.data8C10 = static_cast<std::uint16_t>(economyFactor + PRICE_FACTOR_ROW_WORDS);
    const std::uint16_t governmentFactor = _state.Data().market.data8C12;
    price = ScaledProduct(price, GOVERNMENT_PRICE_FACTORS[governmentFactor]);
    _state.Data().market.data8C12 = static_cast<std::uint16_t>(governmentFactor + PRICE_FACTOR_ROW_WORDS);
    price = ScaledProduct(price, UNIT_FACTOR);
    price = ScaledProduct(price, PRODUCT_BASE_PRICES[commodity]);

    // 256 + the price adjustment + the tech-level factor times marketTechLevel, both signed bytes.
    const std::uint16_t recordIndex = _state.Data().market.data8C14;
    const std::span<const std::uint8_t> record =
      std::span<const std::uint8_t>(PRODUCT_TRADE_RECORDS).subspan(recordIndex * TRADE_RECORD_BYTES, TRADE_RECORD_BYTES);
    const auto techFactor = static_cast<std::int8_t>(std::uint8_t{record[1]});
    const auto techTerm = static_cast<std::uint16_t>(techFactor * static_cast<std::int8_t>(_state.Data().market.marketTechLevel));
    const auto adjustment = static_cast<std::int8_t>(std::uint8_t{record[0]});
    const auto factor = static_cast<std::uint16_t>(static_cast<std::uint16_t>(adjustment) + UNIT_FACTOR + techTerm);
    _state.Data().market.data8C14 = static_cast<std::uint16_t>(recordIndex + 1);
    price = ScaledProduct(price, factor);

    SetWordIn(prices, std::size_t{commodity} * TRADE_PRICES_BYTES, price);
    _state.Data().menus.resalePriceInput = price;
    SetWordIn(prices, std::size_t{commodity} * TRADE_PRICES_BYTES + 2, ComputeResalePrice(_state));
  }
}

std::uint16_t NextMarketRandom(GameState& _state)
{
  const std::uint16_t a = WordIn(BytesOf(_state.Data().market.marketRandomState), 0);
  const std::uint16_t b = _state.Data().market.marketRandomState.data8C20;
  const std::uint16_t c = _state.Data().market.marketRandomState.data8C22;
  const auto sum = static_cast<std::uint16_t>(a + b);
  // In the order the original's two XCHGs and its ADD write them.
  _state.Data().market.marketRandomState.data8C20 = c;
  SetWordIn(BytesOf(_state.Data().market.marketRandomState), 0, b);
  _state.Data().market.marketRandomState.data8C22 = static_cast<std::uint16_t>(c + sum);
  return sum;
}

void StartMarketRandom(GameState& _state)
{
  SetWordIn(BytesOf(_state.Data().market.marketRandomState), 0, _state.Data().commander.randomState0);
  _state.Data().market.marketRandomState.data8C20 = std::uint16_t{_state.Data().commander.randomState1};
  _state.Data().market.marketRandomState.data8C22 = std::uint16_t{_state.Data().commander.randomState2};
}

std::optional<std::uint8_t> RollQuantityOnSale(GameState& _state)
{
  // ((r & 31) - 7) xor (r >> 8 & 3), none when that borrows.
  const std::uint16_t random = NextMarketRandom(_state);
  const auto drawn = static_cast<std::uint8_t>(Low(random) & QUANTITY_RANDOM_MASK);
  if (drawn < QUANTITY_RANDOM_BIAS)
  {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>((drawn - QUANTITY_RANDOM_BIAS) ^ (High(random) & QUANTITY_RANDOM_HIGH_MASK));
}

Quantity ParseQuantity(std::span<const Counted<std::uint8_t>> _text)
{
  Quantity quantity{.value = 0, .valid = false, .end = 0, .lastRead = 0};
  const auto next = [&_text, &quantity]()
  {
    quantity.lastRead = _text[quantity.end];
    ++quantity.end;
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

void PrintCargoQuantity(GameState& _state, std::uint16_t _quantity)
{
  std::string_view text = NO_QUANTITY_TEXT;
  if (_quantity != 0)
  {
    FormatDecimal5(_quantity, _state.Data().menus.quantityText);
    BlankLeadingZeros(_state.Data().menus.quantityText, QUANTITY_DIGITS_BLANKED);
    text = TextWithin(BytesOf(_state.Data().menus.quantityText), 0);
  }
  const std::uint8_t attribute = _state.Data().drawing.textAttribute;
  _state.Data().drawing.textAttribute = QUANTITY_ATTRIBUTE;
  // (row-1)*80 bytes, as (row-1)*256/4 and that /4 again.
  const auto quarter = static_cast<std::uint16_t>((static_cast<std::uint8_t>(_state.Data().menus.menuSelectedRow - 1) << 8) >> 2);
  const auto cell = static_cast<std::uint16_t>(MenuFirstRowCell(_state) + QUANTITY_COLUMN + quarter + (quarter >> 2));
  PrintTextModeString(_state, text, cell);
  _state.Data().drawing.textAttribute = attribute;
}

ScreenKey RunCargoTradeMenu(GameState& _state, Hardware& _hardware)
{
  _state.Data().menus.menuFirstRowAttr = CARGO_MENU_FIRST_ROW;
  _state.Data().menus.menuRowCount = static_cast<std::uint8_t>(COMMODITY_COUNT);
  // StartMenu's last ToggleMenuRowHighlight leaves AL the attribute it toggled the row to, which it left in textAttribute.
  MenuLoop loop{StartMenu(_state).row, 0};
  loop.al = _state.Data().drawing.textAttribute;
  for (;;)
  {
    // 6B43: the steering; then the keys.
    loop = SteerMenuCursor(_state, _hardware, loop);
    for (;;)
    {
      const KeyPress key = PollMenuKey(_state, _hardware);
      loop.al = AlAfterKey(loop.al, key);
      const std::uint8_t code = key.scanCode;
      if (code == 0)
      {
        break;
      }
      // PUSH AX and POP AX round it.
      PrintCreditsOnMessageLine(_state);
      if (code == SCAN_B || code == SCAN_S)
      {
        // B on the buy screen and S on the sell screen: a typed quantity bought or sold, and its message, after which AL is
        // the attribute it pops back. On the other screen the key does nothing.
        const bool buy = code == SCAN_B;
        if ((_state.Data().docked.tradeScreenIsBuy == 1) == buy)
        {
          const std::uint8_t saved = OpenCargoMessage(_state);
          ShowCargoMessage(_state, buy ? BuyCargo(_state, _hardware) : SellCargo(_state, _hardware), saved);
          loop.al = saved;
        }
        break;
      }
      if (code == SCAN_UP || code == SCAN_DOWN)
      {
        loop.row = (code == SCAN_UP ? MoveMenuCursorUp(_state, loop.row) : MoveMenuCursorDown(_state, loop.row)).row;
        loop.al = _state.Data().drawing.textAttribute;
        continue;
      }
      // The screen's own key, F2 to sell or F3 to buy, does nothing here.
      const auto own = static_cast<std::uint8_t>(SCAN_F2 + _state.Data().docked.tradeScreenIsBuy);
      if (code != own && IsScreenKey(code))
      {
        return ScreenKey{code, loop.al, std::nullopt};
      }
      break;
    }
  }
}

void AddContrabandPenalty(GameState& _state)
{
  // The row's trade record, three bytes from data8BDD, in 8 bits: its third byte is the commodity's legal penalty.
  const auto index = static_cast<std::uint8_t>(_state.Data().menus.menuSelectedRow - 1);
  const auto record = static_cast<std::uint8_t>(index * 2 + index);
  const std::span<const std::uint8_t> records(PRODUCT_TRADE_RECORDS);
  const auto status = static_cast<std::uint8_t>(records[std::size_t{record} + LEGAL_PENALTY] + _state.Data().commander.legalStatus);
  if (status != 0)
  {
    _state.Data().commander.legalStatus = status;
  }
}

} // namespace Elite
