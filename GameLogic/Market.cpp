#include "pch.h"

#include "Market.h"

#include "EliteTypes.h"
#include "LookupTables.h"

namespace Elite
{

MarketItem MarketItemAt(int _item) noexcept
{
  const int offset = _item * 4;
  return MarketItem{ MARKET_TABLE[offset], MARKET_TABLE[offset + 1], MARKET_TABLE[offset + 2],
                     MARKET_TABLE[offset + 3] };
}

std::uint8_t EconomyAdjustment(std::uint8_t _gradient, std::uint8_t _economy) noexcept
{
  // 6502: AND #%00011111 -- only the magnitude; bit 7 is the direction and is read by the caller.
  const std::uint8_t magnitude = static_cast<std::uint8_t>(_gradient & 0x1Fu);

  // 6502: LDY QQ28 / CLC / LDA #0 / TT153: DEY / BMI / ADC $90 / JMP TT153 -- the CLC is outside
  // the loop, so every addition after the first carries in whatever the one before left.
  std::uint8_t total = 0;
  bool carry = false;
  for (std::uint8_t remaining = _economy; remaining != 0; --remaining)
  {
    const AddResult step = AddWithCarry(total, magnitude, carry);
    total = step.value;
    carry = step.carry;
  }
  return total;
}

std::uint8_t MarketPrice(int _item, std::uint8_t _economy, std::uint8_t _randomiser) noexcept
{
  const MarketItem item = MarketItemAt(_item);

  // 6502: LDA QQ26 / AND QQ23+3 / CLC / ADC QQ23 -- the base, nudged by as much of the market's
  // random byte as this item's mask lets through.
  const std::uint8_t nudged =
    AddWithCarry(static_cast<std::uint8_t>(_randomiser & item.mask), item.basePrice, false).value;

  const std::uint8_t adjustment = EconomyAdjustment(item.gradient, _economy);

  /*
   * 6502: LDA $8F / BMI TT155 -- and here is the sign. A NEGATIVE gradient SUBTRACTS from the
   * price, and the same sign will ADD to the quantity in GenerateMarket. Reading either branch
   * off the other gives an economy that is exactly backwards and entirely plausible.
   *
   * The addition takes the carry the mask's ADC left; the subtraction sets it first.
   */
  if ((item.gradient & 0x80u) != 0u)
  {
    // 6502: TT155 -- LDA QQ24 / SEC / SBC $91.
    return static_cast<std::uint8_t>(nudged - adjustment);
  }

  return AddWithCarry(nudged, adjustment, false).value;
}

void GenerateMarket(Rng& _rng, std::uint8_t _economy, MarketState& _outMarket) noexcept
{
  // 6502: JSR DORND / STA QQ26 -- one byte perturbs the whole market.
  _outMarket.randomiser = _rng.Next(false).value;

  /*
   * 6502: hy9 ... CMP #63 / BCC hy9.
   *
   * The index steps by four and the loop continues while it is below 63, so it runs sixteen
   * times and stops one item short of the seventeen the market screen shows. Alien Items keep
   * whatever availability they had, which is why they cannot be bought.
   */
  for (int item = 0; item < 16; ++item)
  {
    const MarketItem entry = MarketItemAt(item);
    const std::uint8_t adjustment = EconomyAdjustment(entry.gradient, _economy);

    // 6502: LDA QQ23+3,X / AND QQ26 / CLC / ADC QQ23+2,X -- the same nudge as the price uses,
    // applied to the base quantity instead.
    std::uint8_t quantity =
      AddWithCarry(static_cast<std::uint8_t>(entry.mask & _outMarket.randomiser), entry.baseQuantity, false).value;

    // 6502: LDY $8F / BMI TT157 -- the sign, the other way round from the price.
    if ((entry.gradient & 0x80u) != 0u)
    {
      quantity = AddWithCarry(quantity, adjustment, false).value;
    }
    else
    {
      quantity = static_cast<std::uint8_t>(quantity - adjustment);
    }

    // 6502: BPL TT159 / LDA #0 -- a quantity that went negative is nothing, not a large number.
    if ((quantity & 0x80u) != 0u)
    {
      quantity = 0;
    }

    // 6502: AND #%00111111 -- the shelf holds at most 63 of anything.
    _outMarket.availability[item] = static_cast<std::uint8_t>(quantity & 0x3Fu);
  }

  /*
   * 6502: var's LDA #0 / STA AVL+16.
   *
   * Alien Items are never for sale, and the original enforces it from inside the routine that
   * works out the economy adjustment -- so it happens once per item, sixteen times over, for a
   * value the loop above never touches. Found by an assertion that expected this byte to survive
   * the call and watched the game clear it.
   */
  _outMarket.availability[MARKET_ITEM_COUNT - 1] = 0;

  for (int item = 0; item < MARKET_ITEM_COUNT; ++item)
  {
    _outMarket.price[item] = MarketPrice(item, _economy, _outMarket.randomiser);
  }
}

} // namespace Elite
