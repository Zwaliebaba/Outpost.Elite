#include "pch.h"

#include "Market.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

// The original routine AddCredits calls, which the text subsystem ports.
constexpr std::uint16_t FORMAT_CREDITS = 0x3543;

constexpr std::uint16_t RESALE_PRICE_CEILING = 100;
constexpr std::uint16_t COMMODITY_COUNT = 0x11;
// A price factor of 1.0, in 256ths.
constexpr std::uint16_t UNIT_FACTOR = 0x100;
// The price factor tables are by commodity, 16 bytes to a row.
constexpr std::uint16_t PRICE_FACTOR_ROW_BYTES = 0x10;
constexpr std::uint16_t TRADE_RECORD_BYTES = 3;
constexpr std::uint8_t MARKET_TECH_LEVEL_MOST = 9;

// mul by _factor, then mov al,ah / mov ah,dl: AX = DX:AX >> 8, DX the high word.
void MultiplyScaled(Machine::Registers& _regs, std::uint16_t _factor) noexcept
{
  const std::uint32_t product = std::uint32_t{_regs.ax} * _factor;
  _regs.dx = static_cast<std::uint16_t>(product >> 16);
  _regs.ax = static_cast<std::uint16_t>(product >> 8);
}

} // namespace

void AddCredits(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  const std::uint32_t low = std::uint32_t{_guest.Word(DS.creditsTenths.offset)} + regs.ax;
  _guest.SetWord(DS.creditsTenths.offset, static_cast<std::uint16_t>(low));
  _guest.Set(DS.data75F5, static_cast<std::uint16_t>(_guest.Get(DS.data75F5) + regs.bx + (low >> 16)));
  _guest.Call(FORMAT_CREDITS);
}

void ComputeResalePrice(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t price = _guest.Get(DS.resalePriceInput);
  regs.ax = price;
  if (price != 0)
  {
    regs.ax = static_cast<std::uint16_t>(regs.ax >> 5);
    while (regs.ax >= RESALE_PRICE_CEILING)
    {
      regs.ax = static_cast<std::uint16_t>(regs.ax >> 1);
    }
    regs.ax = static_cast<std::uint16_t>(price - regs.ax - 1);
  }
  _guest.SetFlag(Machine::FLAG_ZERO, regs.ax == 0);
}

void ComputeMarketPrices(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = static_cast<std::uint16_t>(_guest.Get(DS.currentEconomy) * 2 + DS.economyPriceFactors.offset);
  _guest.Set(DS.data8C10, regs.bx);
  regs.bx = static_cast<std::uint16_t>(_guest.Get(DS.currentGovernment) * 2 + DS.governmentPriceFactors.offset);
  _guest.Set(DS.data8C12, regs.bx);
  // The species branch at 0x69EE (data75BC, data75BF) loads AL only for the tech level to replace it.
  std::uint8_t techLevel = _guest.Get(DS.currentTechLevel);
  if (techLevel > MARKET_TECH_LEVEL_MOST)
  {
    techLevel = MARKET_TECH_LEVEL_MOST;
  }
  regs.ax = static_cast<std::uint16_t>((regs.ax & 0xFF00) | techLevel);
  _guest.Set(DS.marketTechLevel, techLevel);
  regs.bx = DS.productTradeRecords.offset;
  _guest.Set(DS.data8C14, regs.bx);
  regs.di = DS.screenPrices.offset;
  regs.si = DS.productBasePrices.offset;
  for (regs.cx = COMMODITY_COUNT; regs.cx != 0; --regs.cx)
  {
    regs.ax = UNIT_FACTOR;
    regs.bx = _guest.Get(DS.data8C10);
    MultiplyScaled(regs, _guest.Word(regs.bx));
    regs.bx = static_cast<std::uint16_t>(regs.bx + PRICE_FACTOR_ROW_BYTES);
    _guest.Set(DS.data8C10, regs.bx);
    regs.bx = _guest.Get(DS.data8C12);
    MultiplyScaled(regs, _guest.Word(regs.bx));
    regs.bx = static_cast<std::uint16_t>(regs.bx + PRICE_FACTOR_ROW_BYTES);
    _guest.Set(DS.data8C12, regs.bx);
    regs.bx = UNIT_FACTOR;
    MultiplyScaled(regs, regs.bx);
    MultiplyScaled(regs, _guest.Word(regs.si));
    regs.si = static_cast<std::uint16_t>(regs.si + 2);

    // 256 + the price adjustment + the tech-level factor times marketTechLevel, both signed bytes.
    regs.bx = _guest.Get(DS.data8C14);
    const std::uint16_t price = regs.ax;
    const auto techFactor = static_cast<std::int8_t>(_guest.Byte(static_cast<std::uint16_t>(regs.bx + 1)));
    regs.dx = static_cast<std::uint16_t>(techFactor * static_cast<std::int8_t>(_guest.Get(DS.marketTechLevel)));
    const auto adjustment = static_cast<std::int8_t>(_guest.Byte(regs.bx));
    regs.ax = static_cast<std::uint16_t>(static_cast<std::uint16_t>(adjustment) + UNIT_FACTOR + regs.dx);
    regs.bx = static_cast<std::uint16_t>(regs.bx + TRADE_RECORD_BYTES);
    _guest.Set(DS.data8C14, regs.bx);
    regs.bx = regs.ax;
    regs.ax = price;
    MultiplyScaled(regs, regs.bx);

    _guest.SetWord(regs.di, regs.ax);
    _guest.Set(DS.resalePriceInput, regs.ax);
    ComputeResalePrice(_guest);
    _guest.SetWord(static_cast<std::uint16_t>(regs.di + 2), regs.ax);
    regs.di = static_cast<std::uint16_t>(regs.di + 4);
  }
}

void NextMarketRandom(Guest& _guest)
{
  const std::uint16_t a = _guest.Word(DS.marketRandomState.offset);
  const std::uint16_t b = _guest.Get(DS.data8C20);
  const std::uint16_t c = _guest.Get(DS.data8C22);
  const auto sum = static_cast<std::uint16_t>(a + b);
  _guest.SetWord(DS.marketRandomState.offset, b);
  _guest.Set(DS.data8C20, c);
  _guest.Set(DS.data8C22, static_cast<std::uint16_t>(c + sum));
  _guest.Regs().ax = sum;
}

namespace
{

using Machine::REGISTER_ALL;
using Machine::REGISTER_BP;

constexpr std::array ENTRIES = {
  NativeEntry{0x65EE, "AddCredits", &AddCredits, PRESERVES_ALL},
  NativeEntry{0x6995, "ComputeResalePrice", &ComputeResalePrice, Machine::NativeContract{0, Machine::FLAG_ZERO}},
  NativeEntry{0x69CE, "ComputeMarketPrices", &ComputeMarketPrices,
              Machine::NativeContract{static_cast<std::uint16_t>(REGISTER_ALL & ~REGISTER_BP), 0}},
  NativeEntry{0x6A85, "NextMarketRandom", &NextMarketRandom, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> MarketEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
