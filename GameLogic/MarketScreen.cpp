#include "pch.h"

#include "MarketScreen.h"

#include "EliteTypes.h"

/*
 * The docked trading screens (slice 2c).
 */

namespace Elite
{

namespace
{
/// 6502: LDA #2 / JSR TRADEMODE -- QQ11 = 2 is the Buy Cargo screen.
constexpr std::uint8_t BUY_VIEW = 2;

/// 6502: the tokens the retry loop complains with.
constexpr std::uint8_t QUANTITY_TOKEN = 176;  ///< recursive token 16, "QUANTITY"
constexpr std::uint8_t CARGO_TOKEN = 206;     ///< recursive token 46, " CARGO{sentence case}"
constexpr std::uint8_t CASH_TOKEN = 197;      ///< recursive token 37, "CASH"

/// 6502: LDA #204 -- recursive token 44, "QUANTITY OF ".
constexpr std::uint8_t QUANTITY_OF_TOKEN = 204;

/// 6502: ADC #208 -- the item names run from token 208 ("FOOD") to token 224 ("ALIEN ITEMS").
constexpr std::uint8_t FIRST_ITEM_TOKEN = 208;

/// 6502: LDA #255 -- recursive token 95, the two lines of column headings.
constexpr std::uint8_t COLUMN_HEADINGS_TOKEN = 255;

/// 6502: LDA #119 -- recursive token 119, "CASH:" and the amount, which `dn` prints.
constexpr std::uint8_t CASH_LINE_TOKEN = 119;

/// 6502: TT222 -- LDA QQ29 / CLC / ADC #5 / JSR DOYC. Item N's line ends by putting the cursor on
/// row N + 5, which is where item N + 1 is printed.
constexpr std::uint8_t FIRST_ITEM_ROW_OFFSET = 5;

/*
 * 6502: TT163 -- LDA #17 / JSR DOXC / LDA #255 / BNE TT162+2.
 *
 * The BNE is a branch used as a jump: A has just been loaded with 255, so it is always taken, and
 * TT162+2 is the `JMP TT27` inside the space-printing routine. Two bytes saved over a JMP.
 *
 * The token itself carries the newlines, so this leaves the cursor two rows further down than it
 * found it -- which is what puts the first item's line where it goes.
 */
void PrintColumnHeadings(TokenPrinter& _printer, TextState& _text) noexcept
{
  _text.column = 17;
  _printer.Print(COLUMN_HEADINGS_TOKEN);
}

/*
 * 6502: dn -- JSR TT162 / LDA #119 / JSR spc, and then it FALLS INTO dn2.
 *
 * Eight bytes from `dn` to `dn2` in the assembled build, which is exactly those three
 * instructions with no RTS. So printing the cash after a purchase also beeps and pauses for a
 * second, and the beep a player hears on buying something comes from here rather than from the
 * buy screen.
 */
void PrintCashLeft(TradeScreen& _screen) noexcept
{
  PrintSpace(_screen.printer);
  PrintThenSpace(_screen.printer, CASH_LINE_TOKEN);
  _screen.effects.BeepAndPause();
}

/*
 * 6502: Tc -- JSR TT162 / TYA / JSR prq, then TTX224's JSR dn2.
 *
 * One space, a one-word complaint, a question mark, a beep and a pause. Then it falls into TT224
 * and asks again.
 */
void Complain(TradeScreen& _screen, std::uint8_t _token) noexcept
{
  PrintSpace(_screen.printer);
  PrintThenQuestion(_screen.printer, _token);
  _screen.effects.BeepAndPause();
}
} // namespace

void BuyScreen(TradeScreen& _screen, CommanderBlock& _commander, MarketState& _market,
               std::uint8_t _economy, bool _misJumped) noexcept
{
  // 6502: LDA #2 / JSR TRADEMODE.
  _screen.effects.SetUpTradeScreen(BUY_VIEW);

  // 6502: TTX66's tail -- `LDX #1 / STX XC / STX YC / DEX / STX QQ17`. The seam draws; the text
  // state is the port's, the same split CLYNS uses.
  _screen.text.column = 1;
  _screen.text.row = 1;
  _screen.printer.SetCaseFlags(0);
  _screen.text.caseFlags = 0;

  // 6502: JSR TT163.
  PrintColumnHeadings(_screen.printer, _screen.text);

  // 6502: LDA #%10000000 / STA QQ17 -- and this one is written out rather than being a JSR TT69,
  // so it does NOT print the newline that TT69 would.
  _screen.printer.SetCaseFlags(0x80);

  // 6502: LDA #0 / STA QQ29.
  for (int item = 0; item < MARKET_ITEM_COUNT; ++item)
  {
    // 6502: TT220 -- JSR TT151, which prints the line and leaves the price in QQ24 and the
    // availability in QQ25.
    PrintMarketItem(_screen.printer, _screen.characters, _screen.text, item, _economy, _market, _misJumped);

    /*
     * 6502: LDA QQ25 / BNE TT224 / JMP TT222.
     *
     * QQ25 and QQ24 are recomputed here rather than returned by the line printer, because they
     * are the same two values from the same two inputs. The availability is read AFTER the line
     * is printed, which matters for the seventeenth item: printing it is what zeroes it.
     */
    const std::uint8_t available = _market.availability[item];
    const std::uint8_t price = MarketPrice(item, _economy, _market.randomiser);

    if (available != 0)
    {
      /*
       * 6502: TT224 through TTX224 -- ask, and on any complaint ask again for the SAME item.
       *
       * There is no limit on the retries in the original and none here: the only ways out are a
       * quantity the screen accepts, or a letter, which gnum answers with `JMP BAY2` and which
       * abandons the whole screen rather than this item.
       */
      for (;;)
      {
        // 6502: JSR CLYNS.
        _screen.effects.ClearBottomRows();

        // 6502: LDA #204 / JSR TT27 -- "QUANTITY OF ".
        _screen.printer.Print(QUANTITY_OF_TOKEN);

        // 6502: LDA QQ29 / CLC / ADC #208 / JSR TT27 -- the item's name.
        _screen.printer.Print(static_cast<std::uint8_t>(FIRST_ITEM_TOKEN + item));

        // 6502: LDA #'/' / JSR TT27 / JSR TT152 / LDA #'?' / JSR TT27 / JSR TT67.
        _screen.printer.Print('/');
        PrintMarketUnits(_screen.printer, _screen.characters, MarketItemAt(item).gradient);
        _screen.printer.Print('?');
        PrintNewline(_screen.printer);

        // 6502: TT223K -- JSR gnum. The LDX #0 / STX R / LDX #12 / STX T1 before it have no
        // effect: gnum repeats both at its own start. The original's comment wonders whether
        // they were left behind when code moved, and they are not reproduced here.
        const NumberEntry entry = ReadNumber(_screen.keys, _screen.characters, _screen.text, available);

        // 6502: gnum's `JMP BAY2` -- a letter leaves the screen entirely, not just this item.
        if (entry.outcome == DigitResult::LeaveScreen)
        {
          return;
        }

        // 6502: BCS TQ4 -- too large a number asks for the quantity again.
        if (entry.outcome == DigitResult::TooBig)
        {
          Complain(_screen, QUANTITY_TOKEN);
          continue;
        }

        /*
         * 6502: STA P / JSR tnpr / LDY #206 / LDA R / BEQ P%+4 / BCS Tc.
         *
         * The `BEQ P%+4` steps over the `BCS`, so a quantity of ZERO never hears about the hold
         * being full. It costs nothing and buys nothing, which is how the original lets a player
         * press RETURN past an item.
         */
        const bool fits = CargoFits(_commander, static_cast<std::uint8_t>(item), entry.value);
        if (entry.value != 0 && !fits)
        {
          Complain(_screen, CARGO_TOKEN);
          continue;
        }

        /*
         * 6502: LDA QQ24 / STA Q / JSR GCASH / JSR LCASH / LDY #197 / BCC Tc.
         *
         * LCASH has already subtracted by the time the carry is read. When it could not be
         * afforded it fell into MCASH and added the same amount back, so the cash here is exactly
         * what it was -- but the commander was, for the length of four subtractions, in debt.
         */
        const std::uint16_t cost = TotalPrice(price, entry.value);
        if (!SpendCash(_commander, cost))
        {
          Complain(_screen, CASH_TOKEN);
          continue;
        }

        /*
         * 6502: CLC / ADC QQ20,Y / STA QQ20,Y, then LDA AVL,Y / SEC / SBC R / STA AVL,Y.
         *
         * Both carries are set explicitly, so neither is one of the chained ones this port keeps
         * finding. The hold and the market move by the same amount in opposite directions.
         */
        const std::size_t hold = static_cast<std::size_t>(Field::CargoHold) + static_cast<std::size_t>(item);
        _commander.bytes[hold] = AddWithCarry(_commander.bytes[hold], entry.value, false).value;
        _market.availability[item] =
          static_cast<std::uint8_t>(_market.availability[item] - entry.value);

        // 6502: PLA / BEQ TT222 -- buying nothing prints no confirmation and makes no sound.
        if (entry.value != 0)
        {
          PrintCashLeft(_screen);
        }
        break;
      }
    }

    // 6502: TT222 -- LDA QQ29 / CLC / ADC #5 / JSR DOYC / LDA #0 / JSR DOXC.
    _screen.text.row = static_cast<std::uint8_t>(item + FIRST_ITEM_ROW_OFFSET);
    _screen.text.column = 0;
  }
}

} // namespace Elite
