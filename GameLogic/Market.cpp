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

constexpr std::uint8_t SCAN_F8 = 0x42;

// The text page: offsets in B800, 80 bytes a row.
constexpr std::uint16_t ROW_BYTES = 0x50;
constexpr std::uint16_t TITLE_OFFSET = 0x54;
constexpr std::uint16_t PRICES_HEADER_OFFSET = 0x144;
constexpr std::uint16_t FIRST_PRODUCT_OFFSET = 0x1E4;
constexpr std::uint16_t BUY_PRICE_COLUMN = 4;
constexpr std::uint16_t SELL_PRICE_COLUMN = 2;

constexpr std::uint16_t RESALE_PRICE_CEILING = 100;
constexpr std::uint16_t PRODUCT_NAME_BYTES = 0x11;
constexpr std::size_t LEGAL_PENALTY = 2; // a trade record's third byte
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

// The commodities, from 0, in cargoHold's order and the cargo menu's, whose rows count from 1: each with the amount held and
// the amount the market has. From gold on they are counted in kilograms or grams, outside the hold's tonnes; alien items, the
// last, cannot be bought.
constexpr std::uint8_t FIRST_PRECIOUS_COMMODITY = 0x0D;
constexpr std::uint8_t ALIEN_ITEMS = 0x10;

// _value times _factor over 256, in 16 bits: the middle two bytes of the 32-bit product.
[[nodiscard]] constexpr std::uint16_t ScaledProduct(std::uint16_t _value, std::uint16_t _factor) noexcept
{
  return static_cast<std::uint16_t>((std::uint32_t{_value} * _factor) >> 8);
}

// screenPrices: for each product its buy price and then its sell price, in tenths.
[[nodiscard]] std::span<MenusState::ScreenPrice> ScreenPrices(GameState& _state)
{
  return _state.Data().menus.screenPrices;
}

// Product _product's name and unit, up to its NUL.
[[nodiscard]] std::string_view ProductName(const GameState& _state, std::uint16_t _product)
{
  return TextWithin(BytesOf(_state.Data().docked.productNames), std::size_t{_product} * PRODUCT_NAME_BYTES);
}

// The commander's credits, in tenths: the low word, then the high.
[[nodiscard]] std::uint32_t Credits(const GameState& _state)
{
  return _state.Data().commander.creditsTenths;
}

// The credits put at _tenths, the low word and then the high, and the balance reformatted (FormatCredits).
void StoreCredits(GameState& _state, std::uint32_t _tenths)
{
  _state.Data().commander.creditsTenths = _tenths;
  FormatCredits(_state);
}

} // namespace

[[nodiscard]] Task<ScreenKey> ShowMarketPricesScreen(GameState& _state, Host& _host)
{
  const std::string_view title = DrawDockedFrame(_state, _host, MARKET_PRICES_FRAME);
  // The system's name, cut for good at its first space after the first letter.
  const std::span<std::uint8_t> name(_state.Data().commander.currentSystem.name);
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

  // Each commodity's name, buy price and sell price.
  const std::span<const MenusState::ScreenPrice> prices = ScreenPrices(_state);
  std::uint16_t row = FIRST_PRODUCT_OFFSET;
  for (std::uint16_t product = 0, left = COMMODITY_KINDS;;)
  {
    const std::uint16_t nameEnd = PrintTextModeString(_state, ProductName(_state, product), row);
    FormatTenths(_state, prices[product].price);
    const std::uint16_t buy = PrintTextModeString(_state, TextWithin(_state.Data().menus.priceText, 0), Offset(nameEnd, BUY_PRICE_COLUMN));
    FormatTenths(_state, prices[product].resale);
    PrintTextModeString(_state, TextWithin(_state.Data().menus.priceText, 0), Offset(buy, SELL_PRICE_COLUMN));
    row = Offset(row, ROW_BYTES);
    ++product;
    if (--left == 0)
    {
      break;
    }
  }
  co_return co_await WaitForScreenExitKey(_state, _host, SCAN_F8, ScreenPlace::Flight);
}

bool SubtractCredits(GameState& _state, std::uint32_t _tenths)
{
  const std::optional<std::uint32_t> left = CreditsAfterPaying(Credits(_state), _tenths);
  if (!left)
  {
    return false;
  }
  StoreCredits(_state, *left);
  return true;
}

bool SpendCredits(GameState& _state, std::uint32_t _tenths)
{
  return SubtractCredits(_state, _tenths);
}

void AddCredits(GameState& _state, std::uint32_t _tenths)
{
  StoreCredits(_state, CreditsAfterEarning(Credits(_state), _tenths));
}

void ComputeMarketPrices(GameState& _state)
{
  const MarketPlace place{.economy = _state.Data().commander.currentSystem.economy,
                          .government = _state.Data().commander.currentSystem.government,
                          .techLevel = _state.Data().commander.currentSystem.techLevel};
  // economyFactorIndex and governmentFactorIndex walk the economy's and the government's column of the price factors, a row of eight words to a
  // product, and tradeRecordIndex productTradeRecords, each holding the index of the word or the record it is on where the original
  // holds its address: they are left past the last commodity. marketTechLevel keeps the tech level the prices are by, and
  // resalePriceInput the last buy price, from which its resale price was computed.
  _state.Data().market.economyFactorIndex = static_cast<std::uint16_t>(place.economy + COMMODITY_KINDS * PRICE_FACTOR_ROW_WORDS);
  _state.Data().market.governmentFactorIndex = static_cast<std::uint16_t>(place.government + COMMODITY_KINDS * PRICE_FACTOR_ROW_WORDS);
  _state.Data().market.tradeRecordIndex = COMMODITY_KINDS;
  _state.Data().market.marketTechLevel = MarketTechLevel(place.techLevel);
  const std::span<MenusState::ScreenPrice> prices = ScreenPrices(_state);
  for (std::uint8_t commodity = 0; commodity < COMMODITY_KINDS; ++commodity)
  {
    const CommodityPrices price = PricesOf(commodity, place);
    prices[commodity].price = price.buyTenths;
    prices[commodity].resale = price.sellTenths;
    _state.Data().menus.resalePriceInput = price.buyTenths;
  }
}

std::uint16_t NextMarketRandom(GameState& _state)
{
  std::uint16_t(&random)[3] = _state.Data().market.marketRandomState;
  const std::uint16_t a = random[0];
  const std::uint16_t b = random[1];
  const std::uint16_t c = random[2];
  const auto sum = static_cast<std::uint16_t>(a + b);
  // In the order the original's two swaps and its sum write them.
  random[1] = c;
  random[0] = b;
  random[2] = static_cast<std::uint16_t>(c + sum);
  return sum;
}

void StartMarketRandom(GameState& _state)
{
  _state.Data().market.marketRandomState[0] = std::uint16_t{_state.Data().commander.randomState0};
  _state.Data().market.marketRandomState[1] = std::uint16_t{_state.Data().commander.randomState1};
  _state.Data().market.marketRandomState[2] = std::uint16_t{_state.Data().commander.randomState2};
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

// ── The market's rules ──

std::uint16_t ResaleTenths(std::uint16_t _priceTenths) noexcept
{
  if (_priceTenths == 0)
  {
    return 0;
  }
  auto cut = static_cast<std::uint16_t>(_priceTenths >> 5);
  while (cut >= RESALE_PRICE_CEILING)
  {
    cut = static_cast<std::uint16_t>(cut >> 1);
  }
  return static_cast<std::uint16_t>(_priceTenths - cut - 1);
}

std::uint8_t MarketTechLevel(std::uint8_t _techLevel) noexcept
{
  return _techLevel > MARKET_TECH_LEVEL_MOST ? MARKET_TECH_LEVEL_MOST : _techLevel;
}

CommodityPrices PricesOf(std::uint8_t _commodity, const MarketPlace& _place) noexcept
{
  // The economy's factor, the government's, 1.0 and the base price, each a product over 256.
  const std::size_t row = std::size_t{_commodity} * PRICE_FACTOR_ROW_WORDS;
  std::uint16_t price = ScaledProduct(UNIT_FACTOR, ECONOMY_PRICE_FACTORS[row + _place.economy]);
  price = ScaledProduct(price, GOVERNMENT_PRICE_FACTORS[row + _place.government]);
  price = ScaledProduct(price, UNIT_FACTOR);
  price = ScaledProduct(price, PRODUCT_BASE_PRICES[_commodity]);

  // 256 + the price adjustment + the tech-level factor times the market's tech level, both signed bytes.
  const std::size_t record = std::size_t{_commodity} * TRADE_RECORD_BYTES;
  const auto techFactor = static_cast<std::int8_t>(PRODUCT_TRADE_RECORDS[record + 1]);
  const auto techTerm = static_cast<std::uint16_t>(techFactor * static_cast<std::int8_t>(MarketTechLevel(_place.techLevel)));
  const auto adjustment = static_cast<std::int8_t>(PRODUCT_TRADE_RECORDS[record]);
  const auto factor = static_cast<std::uint16_t>(static_cast<std::uint16_t>(adjustment) + UNIT_FACTOR + techTerm);
  price = ScaledProduct(price, factor);
  return CommodityPrices{.buyTenths = price, .sellTenths = ResaleTenths(price)};
}

std::optional<std::uint32_t> CreditsAfterPaying(std::uint32_t _creditsTenths, std::uint32_t _priceTenths) noexcept
{
  // The price taken off in 32 bits, and put back when that borrows.
  if (_creditsTenths < _priceTenths)
  {
    return std::nullopt;
  }
  return _creditsTenths - _priceTenths;
}

std::uint32_t CreditsAfterEarning(std::uint32_t _creditsTenths, std::uint32_t _earnedTenths) noexcept
{
  // The low words added, then the high words with the carry that makes: a 32-bit sum that wraps.
  return _creditsTenths + _earnedTenths;
}

std::uint8_t CargoBayTonnes(bool _largeBay) noexcept
{
  return _largeBay ? LARGE_CARGO_BAY_TONNES : CARGO_BAY_TONNES;
}

bool WeighedInTonnes(std::uint8_t _commodity) noexcept
{
  return _commodity == ALIEN_ITEMS || _commodity < FIRST_PRECIOUS_COMMODITY;
}

std::uint8_t LegalStatusAfterSelling(std::uint8_t _legalStatus, std::uint8_t _commodity) noexcept
{
  // The commodity's trade record, three bytes, found in 8 bits: its third byte is the legal penalty.
  const auto record = static_cast<std::uint8_t>(_commodity * TRADE_RECORD_BYTES);
  const auto status = static_cast<std::uint8_t>(PRODUCT_TRADE_RECORDS[std::size_t{record} + LEGAL_PENALTY] + _legalStatus);
  return status != 0 ? status : _legalStatus;
}

CargoRefusal RefuseToBuy(const CargoLot& _lot, bool _largeBay) noexcept
{
  if (_lot.onSale == 0)
  {
    return CargoRefusal::NothingOnSale;
  }
  if (_lot.commodity == ALIEN_ITEMS)
  {
    return CargoRefusal::NotForSale;
  }
  if (!WeighedInTonnes(_lot.commodity))
  {
    return _lot.held == PRECIOUS_HELD_MOST ? CargoRefusal::FullToCapacity : CargoRefusal::None;
  }
  return _lot.usedTonnes == CargoBayTonnes(_largeBay) ? CargoRefusal::BayFull : CargoRefusal::None;
}

CargoRefusal RefuseToSell(const CargoLot& _lot) noexcept
{
  return _lot.held == 0 ? CargoRefusal::NothingToSell : CargoRefusal::None;
}

CargoTrade CargoBought(const CargoLot& _lot, bool _largeBay, std::uint8_t _amount, std::uint16_t _buyTenths) noexcept
{
  if (_lot.onSale < _amount)
  {
    return CargoTrade{.refusal = CargoRefusal::NotEnoughOnSale, .lot = _lot};
  }
  const bool tonnes = WeighedInTonnes(_lot.commodity);
  if (tonnes)
  {
    // The tonnes after it must fit the hold, in 8 bits as the original adds them.
    const auto after = static_cast<std::uint8_t>(_lot.usedTonnes + _amount);
    if (after >= static_cast<std::uint8_t>(CargoBayTonnes(_largeBay) + 1))
    {
      return CargoTrade{.refusal = CargoRefusal::CannotCarry, .lot = _lot};
    }
  }
  else if (_lot.held + _amount > PRECIOUS_HELD_MOST) // an 8-bit sum: a carry, or more than 250
  {
    return CargoTrade{.refusal = CargoRefusal::CannotCarry, .lot = _lot};
  }
  const std::optional<std::uint32_t> credits = CreditsAfterPaying(_lot.creditsTenths, std::uint32_t{_amount} * _buyTenths);
  if (!credits)
  {
    return CargoTrade{.refusal = CargoRefusal::NotEnoughCredits, .lot = _lot};
  }
  CargoLot after = _lot;
  after.held = static_cast<std::uint8_t>(_lot.held + _amount);
  after.onSale = static_cast<std::uint8_t>(_lot.onSale - _amount);
  if (tonnes)
  {
    after.usedTonnes = static_cast<std::uint8_t>(_lot.usedTonnes + _amount);
  }
  after.creditsTenths = *credits;
  return CargoTrade{.refusal = CargoRefusal::None, .lot = after};
}

CargoTrade CargoSold(const CargoLot& _lot, std::uint8_t _amount, std::uint16_t _sellTenths) noexcept
{
  if (_lot.held < _amount)
  {
    return CargoTrade{.refusal = CargoRefusal::NotEnoughHeld, .lot = _lot};
  }
  CargoLot after = _lot;
  after.held = static_cast<std::uint8_t>(_lot.held - _amount);
  // The market's amount goes up, and is put at FFh if that carries.
  const auto onSale = static_cast<std::uint16_t>(_lot.onSale + _amount);
  after.onSale = onSale > 0xFF ? std::uint8_t{0xFF} : static_cast<std::uint8_t>(onSale);
  if (WeighedInTonnes(_lot.commodity))
  {
    after.usedTonnes = static_cast<std::uint8_t>(_lot.usedTonnes - _amount);
  }
  after.legalStatus = LegalStatusAfterSelling(_lot.legalStatus, _lot.commodity);
  after.creditsTenths = CreditsAfterEarning(_lot.creditsTenths, std::uint32_t{_amount} * _sellTenths);
  return CargoTrade{.refusal = CargoRefusal::None, .lot = after};
}

} // namespace Elite
