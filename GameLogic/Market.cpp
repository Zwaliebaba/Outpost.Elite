#include "pch.h"

#include "Market.h"

#include "Arith.h"
#include "EliteTypes.h"
#include "LookupTables.h"

namespace Elite
{

namespace
{
/*
 * 6502: MAG2 = $40 (elite-source.asm) and the &10 that OUT stores over it.
 *
 * The first is a multicolour palette byte for screen RAM -- purple on black -- and gnum uses it
 * to mark what the player is typing. The second is white, which is what the rest of a text screen
 * is drawn in. They are colour cell values rather than indices, so they go into COL2 as they are.
 */
constexpr std::uint8_t TEXT_COLOUR_TYPING = 0x40;
constexpr std::uint8_t TEXT_COLOUR_NORMAL = 0x10;

/// 6502: LDX #12 / STX T1 -- how many keys gnum will take before ending the number itself.
constexpr int KEY_LIMIT = 12;
} // namespace

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
   * The addition's carry comes from `var`, not from the mask's ADC: TT152 prints the units and
   * `var` computes the adjustment between the two, and both leave the carry somewhere of their
   * own. `var`'s last instruction to touch it is an `ADC` whose sum cannot exceed 217 -- seven
   * economies times a magnitude of at most 31 -- so it is always CLEAR, and when the economy is
   * zero the loop does not run and the `CLC` before it stands. Hence the false below rather than
   * a threaded carry, and the exhaustive comparison is what says so.
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

void PrintMarketUnits(TokenPrinter& _printer, CharacterPrinter& _characters, std::uint8_t _gradient) noexcept
{
  // 6502: TT152 -- LDA QQ19+1 / AND #%01100000 -- two bits of the gradient, and nothing else in
  // the byte says what the item is measured in.
  const std::uint8_t units = static_cast<std::uint8_t>(_gradient & 0x60u);

  if (units == 0x20u)
  {
    // 6502: TT161 -- 'k' and then a fall-through into TT16a, whose JMP DASC returns. So "kg" and
    // no trailing space, unlike the other two.
    _characters.Put('k');
    _characters.Put('g');
    return;
  }

  // 6502: TT160 prints 't' and TT16a prints 'g', and both then reach TT162's space -- the first
  // through a BCC that DASC's CLC guarantees, the second by falling into it.
  _characters.Put((units == 0u) ? std::uint8_t{ 't' } : std::uint8_t{ 'g' });

  // 6502: TT162 -- LDA #' ' / JMP TT27. Through the TOKEN printer, not through DASC, so the
  // space is subject to the case flags like any other token character.
  _printer.Print(' ');
}

void PrintMarketItem(TokenPrinter& _printer, CharacterPrinter& _characters, TextState& _text, int _item,
                     std::uint8_t _economy, MarketState& _market, bool _misJumped) noexcept
{
  // 6502: LDA MJ / BNE TT151q -- in witchspace there is no market, and the line prints nothing.
  if (_misJumped)
  {
    return;
  }

  const MarketItem item = MarketItemAt(_item);

  /*
   * 6502: LDA #1 / JSR DOXC / PLA / ADC #&D0 / JSR TT27.
   *
   * The name is token 208 plus the item number, and the ADC has no CLC -- it takes the carry the
   * second `ASL A` left when the item number was multiplied by four for the table index. With
   * seventeen items that shift cannot overflow, so the carry is always clear; the addition is
   * written as one anyway because that is what it is.
   */
  _text.column = 1;
  const ShiftResult indexOnce = RotateLeft(static_cast<std::uint8_t>(_item), false);
  const ShiftResult indexTwice = RotateLeft(indexOnce.value, indexOnce.carry);
  _printer.Print(AddWithCarry(static_cast<std::uint8_t>(_item), 0xD0u, indexTwice.carry).value);

  // 6502: LDA #14 / JSR DOXC -- the price column.
  _text.column = 14;

  const std::uint8_t price = MarketPrice(_item, _economy, _market.randomiser);

  /*
   * 6502: `var`, which the price above went through, ends `LDA #0 / STA AVL+16`.
   *
   * So working out ANY price makes Alien Items unavailable, and the seventeenth line of the
   * screen is always a dash however much stock the market was generated with. The port's
   * EconomyAdjustment is arithmetic and has no market to reach into, so the store surfaces here
   * -- which is why this takes the market by reference and printing it changes it.
   */
  _market.availability[MARKET_ITEM_COUNT - 1] = 0;

  // 6502: JSR TT152 -- the units come BEFORE the price is finished being worked out, because the
  // routine interleaves the printing and the arithmetic.
  PrintMarketUnits(_printer, _characters, item.gradient);

  /*
   * 6502: STA P / LDA #0 / JSR GC2 / SEC / JSR pr5.
   *
   * GC2 is GCASH without the multiply, so the price is quadrupled and printed as five digits with
   * one after the point. That is where the quoted price comes from: the byte holds four-tenths of
   * a credit each.
   */
  MathWorkspace work;
  work.p = price;
  std::uint8_t high = 0;
  for (int shift = 0; shift < 2; ++shift)
  {
    const ShiftResult low = RotateLeft(work.p, false);
    work.p = low.value;
    high = RotateLeft(high, low.carry).value;
  }
  PrintValue(_characters, static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8) | work.p), 5, true);

  // 6502: LDY QQ19+4 / LDA #5 / LDX AVL,Y / STX QQ25 / CLC / BEQ TT172.
  const std::uint8_t available = _market.availability[static_cast<std::size_t>(_item)];
  if (available == 0)
  {
    // 6502: TT172 -- LDA #25 / JSR DOXC / LDA #45 / JMP TT27. A dash, further right than the
    // number would have been, so an empty market reads as a column of dashes.
    _text.column = 25;
    _printer.Print(45);
    return;
  }

  PrintValue(_characters, available, 5, false);
  PrintMarketUnits(_printer, _characters, item.gradient);
}

void PrintMarketScreen(TokenPrinter& _printer, CharacterPrinter& _characters, TextState& _text,
                       std::uint8_t _economy, MarketState& _market, bool _misJumped) noexcept
{
  // 6502: LDA #5 / JSR DOXC / LDA #167 / JSR NLIN3 -- the title, and NLIN3's own rule at row 19.
  // The rule itself is the canvas's; a caller that wants it draws DrawSeparator.
  _text.column = 5;
  _printer.Print(167);

  // 6502: LDA #3 / JSR DOYC / JSR TT163 -- the column headings, which are token 255.
  _text.row = 3;
  _text.column = 17;
  _printer.Print(255);

  // 6502: LDA #6 / JSR DOYC / LDA #0 / STA QQ29.
  _text.row = 6;

  /*
   * 6502: TT168 -- CMP #17 / BCC TT168, so seventeen lines.
   *
   * GenerateMarket fills only sixteen, so the last line is Alien Items with whatever `var` left
   * in its availability -- which is zero, and prints as a dash. The loop bound and the generator's
   * are different numbers in the original and stay different here.
   */
  for (int item = 0; item < MARKET_ITEM_COUNT; ++item)
  {
    // 6502: LDX #128 / STX QQ17 -- sentence case for every line, reset each time round.
    _printer.SetCaseFlags(0x80);
    PrintMarketItem(_printer, _characters, _text, item, _economy, _market, _misJumped);

    // 6502: JSR INCYC.
    ++_text.row;
  }
}

DigitResult TypeDigit(std::uint8_t& _value, std::uint8_t _key, std::uint8_t _available) noexcept
{
  /*
   * 6502: LDX R / BNE NWDAV2 / CMP #'Y' / BEQ NWDAV1 / CMP #'N' / BEQ NWDAV3.
   *
   * The test is on the VALUE, not on how many keys have been pressed, so "Y" is still accepted
   * after typing a zero.
   */
  if (_value == 0)
  {
    if (_key == 'Y')
    {
      _value = _available;
      return DigitResult::TakeAll;
    }
    if (_key == 'N')
    {
      _value = 0;
      return DigitResult::TakeNone;
    }
  }

  // 6502: NWDAV2 -- STA Q / SEC / SBC #'0' / BCC OUT. Anything below '0' ends the number.
  if (_key < '0')
  {
    return DigitResult::Complete;
  }

  const std::uint8_t digit = static_cast<std::uint8_t>(_key - '0');

  // 6502: CMP #10 / BCS BAY2 -- and a letter does not end the number, it leaves the screen.
  if (digit >= 10u)
  {
    return DigitResult::LeaveScreen;
  }

  // 6502: LDA R / CMP #26 / BCS OUT -- past 26 no further digit is taken, and the carry the CMP
  // leaves is SET, which is what tells the caller the number was refused rather than finished.
  if (_value >= 26u)
  {
    return DigitResult::TooBig;
  }

  /*
   * 6502: ASL A / STA T / ASL A / ASL A / ADC T / ADC S.
   *
   * Twice, kept; times eight; add the kept copy; add the digit. Neither addition clears the carry
   * first, so each takes what the instruction before it left.
   *
   * BOTH of those carries are dead, and provably so: the cap above means the value here is at
   * most 25, so the three shifts reach 200 without overflowing and the first addition reaches
   * 250. Neither can carry. They are written as a chain anyway because that is what the six
   * instructions are, and because the proof depends on the cap -- move the cap and the carries
   * come alive.
   *
   * This is the second dead carry the port has kept rather than simplified; the justification
   * routine's `LSR SC+1` is the other. Mutation testing finds both, and the answer in each case
   * is that the mutation is EQUIVALENT rather than that the test is weak.
   */
  const ShiftResult twice = RotateLeft(_value, false);
  const ShiftResult fourTimes = RotateLeft(twice.value, false);
  const ShiftResult eightTimes = RotateLeft(fourTimes.value, false);

  const AddResult tenTimes = AddWithCarry(eightTimes.value, twice.value, eightTimes.carry);
  const AddResult withDigit = AddWithCarry(tenTimes.value, digit, tenTimes.carry);
  _value = withDigit.value;

  /*
   * 6502: CMP QQ25 / BEQ TT226 / BCS OUT.
   *
   * A value equal to what is available carries on; one that exceeds it finishes, KEEPING the
   * value. So the number the caller gets can be larger than the market holds, and refusing it is
   * the caller's job.
   */
  if (_value != _available && _value > _available)
  {
    return DigitResult::TooBig;
  }

  return DigitResult::Accepted;
}

NumberEntry ReadNumber(KeySource& _keys, CharacterPrinter& _characters, TextState& _text,
                       std::uint8_t _available) noexcept
{
  // 6502: LDA #MAG2 / STA COL2 -- purple for what the player types.
  _text.cellColour = TEXT_COLOUR_TYPING;

  NumberEntry entry{};

  // 6502: LDX #0 / STX R / LDX #12 / STX T1.
  for (int remaining = KEY_LIMIT; remaining > 0; --remaining)
  {
    // 6502: TT223 -- JSR TT217, which does not return until a key is pressed.
    const std::uint8_t key = _keys.NextKey();
    entry.outcome = TypeDigit(entry.value, key, _available);

    /*
     * 6502: TT226's `LDA Q / JSR TT26`, and the same call at the top of NWDAV1 and NWDAV3.
     *
     * Only these three echo. The exits that END the number print nothing, which is why a
     * refused quantity leaves the line as the player typed it rather than adding the key that
     * refused it.
     */
    if (entry.outcome == DigitResult::Accepted || entry.outcome == DigitResult::TakeAll
        || entry.outcome == DigitResult::TakeNone)
    {
      _characters.Put(key);
    }

    if (entry.outcome != DigitResult::Accepted)
    {
      break;
    }

    /*
     * 6502: DEC T1 / BNE TT223, and what happens when it does NOT branch.
     *
     * The twelfth accepted key leaves T1 at zero and the loop falls straight into OUT -- so the
     * number ends because the counter ran out, not because the player ended it, and the carry is
     * whatever the last comparison left. That last comparison is `CMP QQ25 / BEQ TT226`, taken,
     * which is how TT226 was reached; but TT226's `JSR TT26` clears the carry on the way past.
     * So the exit is a clear carry: the number is finished and usable.
     */
    if (remaining == 1)
    {
      entry.outcome = DigitResult::Complete;
    }
  }

  /*
   * 6502: OUT -- LDA #&10 / STA COL2.
   *
   * BAY2 is the one exit that does not pass through OUT, so the colour is left purple when a
   * letter abandons the screen. The screen that follows sets it again, which is presumably why
   * nobody noticed.
   */
  if (entry.outcome != DigitResult::LeaveScreen)
  {
    _text.cellColour = TEXT_COLOUR_NORMAL;
  }

  return entry;
}

} // namespace Elite
