#include "pch.h"

#include "Market.h"

#include "Arith.h"
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

/*
 * 6502: LCASH and MCASH -- one routine with two entry points, because the failure path of the
 * first IS the second.
 */
bool SpendCash(CommanderBlock& _commander, std::uint16_t _tenths) noexcept
{
  /*
   * 6502: four SBCs from the low byte up, then BCS.
   *
   * This is the one place in the port a wider type is used for a byte chain rather than modelled
   * a byte at a time, and it is safe for a reason worth stating: the four subtractions have no
   * intermediate the caller can see, and the final carry is exactly the borrow out of a 32-bit
   * subtract. Elsewhere the chains matter because a shift or a comparison feeds them; here
   * nothing does.
   */
  const std::uint32_t cash = _commander.Cash();
  if (cash >= _tenths)
  {
    _commander.SetCash(cash - _tenths);
    return true;
  }

  // 6502: the fall-through into MCASH, which adds the same amount back. The subtraction has
  // already happened and is undone, so there is nothing to model but the answer.
  return false;
}

void ReceiveCash(CommanderBlock& _commander, std::uint16_t _tenths) noexcept
{
  // 6502: MCASH -- four ADCs from the low byte up. Cash wraps at four bytes rather than
  // saturating, which no legitimate amount reaches.
  _commander.SetCash(_commander.Cash() + _tenths);
}

std::uint16_t TotalPrice(std::uint8_t _price, std::uint8_t _quantity) noexcept
{
  // 6502: JSR MULTU -- (A P) = P * Q. Which operand is which does not matter to the product, and
  // the callers do not agree on it either.
  MathWorkspace work;
  work.p = _price;
  work.q = _quantity;
  std::uint8_t high = MultiplyUnsigned(work);

  // 6502: GC2 -- ASL P / ROL A, twice.
  for (int shift = 0; shift < 2; ++shift)
  {
    const ShiftResult low = RotateLeft(work.p, false);
    work.p = low.value;
    high = RotateLeft(high, low.carry).value;
  }

  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8) | work.p);
}

bool CargoFits(const CommanderBlock& _commander, std::uint8_t _item, std::uint8_t _amount) noexcept
{
  const std::size_t hold = static_cast<std::size_t>(Field::CargoHold);

  // 6502: LDX #12 / CPX QQ29 / BCC kg.
  constexpr std::uint8_t LAST_TONNE_ITEM = 12;

  if (LAST_TONNE_ITEM < _item)
  {
    /*
     * 6502: kg -- LDY QQ29 / ADC QQ20,Y / CMP #200.
     *
     * The carry is CLEAR here, because the BCC that arrived is what cleared it. And the addition
     * is not checked for overflow, so 200 kilos of gold plus 100 more comes to 44 and fits --
     * which is the original's behaviour and not something a caller can reach, because the buy
     * screen will not offer more than the market holds.
     */
    const AddResult sum = AddWithCarry(_amount, _commander.bytes[hold + _item], false);
    return sum.value < 200u;
  }

  /*
   * 6502: Tml -- ADC QQ20,X for X counting 12 down to 0, with the carry from the CPX above SET on
   * the first pass and threaded from each addition after that.
   */
  bool carry = true;
  std::uint8_t total = _amount;

  for (int item = LAST_TONNE_ITEM; item >= 0; --item)
  {
    const AddResult sum = AddWithCarry(total, _commander.bytes[hold + static_cast<std::size_t>(item)], carry);
    total = sum.value;
    carry = sum.carry;
  }

  // 6502: ADC TRIBBLE+1 -- and it takes the loop's last carry, not a cleared one.
  total = AddWithCarry(total, _commander.bytes[static_cast<std::size_t>(Field::Tribbles) + 1u], carry).value;

  // 6502: CMP CRGO -- carry set means A >= CRGO, which the routine reports as "no room".
  return total < _commander.At(Field::CargoCapacity);
}

} // namespace Elite
