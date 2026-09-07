#include "pch.h"

#include "MarketScreen.h"

#include "EliteTypes.h"
#include "Ports.h"
#include "Universe.h"
#include "ViewChange.h"
#include "TextPrint.h"
#include "SoundEffects.h"
#include "Presenter.h"

/*
 * The docked trading screens (slice 2c).
 */

namespace Elite
{

  namespace
  {
    /// 6502: the tokens the retry loop complains with.
    constexpr std::uint8_t QUANTITY_TOKEN = 176; ///< recursive token 16, "QUANTITY"
    constexpr std::uint8_t CARGO_TOKEN = 206;    ///< recursive token 46, " CARGO{sentence case}"
    constexpr std::uint8_t CASH_TOKEN = 197;     ///< recursive token 37, "CASH"

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

    /// 6502: LDA #14 / JSR DOXC -- where the quantity goes on a cargo listing.
    constexpr std::uint8_t QUANTITY_COLUMN = 14;

    /// 6502: LDA #205 / JSR TT27 and LDA #206 / JSR DETOK -- "SELL" and "{all caps}(Y/N)?".
    constexpr std::uint8_t SELL_TOKEN = 205;
    constexpr std::uint8_t YES_NO_TOKEN = 206;

    /// 6502: LDA #176 / JSR prq -- NWDAV4's complaint, the same token TT219's TQ4 uses.
    constexpr std::uint8_t ITEM_TOKEN = 176;

    /// 6502: LDX #255 / STX QQ17 -- printing off, so TT151 can be called for its arithmetic alone.
    constexpr std::uint8_t PRINTING_OFF = 255;

    /// 6502: the inventory screen's furniture.
    constexpr std::uint8_t INVENTORY_TITLE_COLUMN = 11;
    constexpr std::uint8_t INVENTORY_TITLE_TOKEN = 164; ///< recursive token 4, "INVENTORY{crlf}"
    constexpr std::uint8_t LARGE_CARGO_BAY_TOKEN = 107;
    constexpr std::uint8_t LARGE_HOLD_THRESHOLD = 26; ///< 6502: CMP #26, against CRGO as stored

    /// 6502: LDA #198 / JSR DETOK, and the four at 111 to 114 that DORND chooses between. All of them
    /// are blank in this version of Elite, which is why the Trumble line is a bare number.
    constexpr std::uint8_t TRUMBLE_ADJECTIVE_FIRST = 111;
    constexpr std::uint8_t TRUMBLE_NOUN_TOKEN = 198;

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
    void PrintCashLeft(Universe& _universe, Ports& _ports) noexcept
    {
      PrintSpace(_ports.printer);
      PrintThenSpace(_ports.printer, CASH_LINE_TOKEN);
      (void)Beep(_universe.sound, false);
      _ports.present.WaitFrames(BEEP_PAUSE_FRAMES);
    }

    /*
     * 6502: Tc -- JSR TT162 / TYA / JSR prq, then TTX224's JSR dn2.
     *
     * One space, a one-word complaint, a question mark, a beep and a pause. Then it falls into TT224
     * and asks again.
     */
    void Complain(Universe& _universe, Ports& _ports, std::uint8_t _token) noexcept
    {
      PrintSpace(_ports.printer);
      PrintThenQuestion(_ports.printer, _token);
      (void)Beep(_universe.sound, false);
      _ports.present.WaitFrames(BEEP_PAUSE_FRAMES);
    }
  } // namespace

  void BuyScreen(Universe& _universe, Ports& _ports, bool _misJumped) noexcept
  {
    // 6502: LDA #2 / JSR TRADEMODE.
    SetUpScreen(_universe, _ports, BUY_CARGO_VIEW);
    _ports.keyboard.Flush();

    _universe.text.column = 1;
    _universe.text.row = 1;
    _universe.text.caseFlags = 0;

    // 6502: JSR TT163.
    PrintColumnHeadings(_ports.printer, _universe.text);

    // 6502: LDA #%10000000 / STA QQ17 -- and this one is written out rather than being a JSR TT69,
    // so it does NOT print the newline that TT69 would.
    _ports.printer.SetCaseFlags(0x80);

    // 6502: LDA #0 / STA QQ29.
    for (int item = 0; item < MARKET_ITEM_COUNT; ++item)
    {
      // 6502: TT220 -- JSR TT151, which prints the line and leaves the price in QQ24 and the
      // availability in QQ25.
      PrintMarketItem(_ports.printer, _ports.characters, _universe.text, item, _universe.current.economy, _universe.market, _misJumped);

      /*
       * 6502: LDA QQ25 / BNE TT224 / JMP TT222.
       *
       * QQ25 and QQ24 are recomputed here rather than returned by the line printer, because they
       * are the same two values from the same two inputs. The availability is read AFTER the line
       * is printed, which matters for the seventeenth item: printing it is what zeroes it.
       */
      const std::uint8_t available = _universe.market.availability[item];
      const std::uint8_t price = MarketPrice(item, _universe.current.economy, _universe.market.randomiser);

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
          ClearMessageRows(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message);

          // 6502: LDA #204 / JSR TT27 -- "QUANTITY OF ".
          _ports.printer.Print(QUANTITY_OF_TOKEN);

          // 6502: LDA QQ29 / CLC / ADC #208 / JSR TT27 -- the item's name.
          _ports.printer.Print(static_cast<std::uint8_t>(FIRST_ITEM_TOKEN + item));

          // 6502: LDA #'/' / JSR TT27 / JSR TT152 / LDA #'?' / JSR TT27 / JSR TT67.
          _ports.printer.Print('/');
          PrintMarketUnits(_ports.printer, _ports.characters, MarketItemAt(item).gradient);
          _ports.printer.Print('?');
          PrintNewline(_ports.printer);

          // 6502: TT223K -- JSR gnum. The LDX #0 / STX R / LDX #12 / STX T1 before it have no
          // effect: gnum repeats both at its own start. The original's comment wonders whether
          // they were left behind when code moved, and they are not reproduced here.
          const NumberEntry entry = ReadNumber(_ports.keyboard, _ports.characters, _universe.text, available);

          // 6502: gnum's `JMP BAY2` -- a letter leaves the screen entirely, not just this item.
          if (entry.outcome == DigitResult::LeaveScreen)
          {
            return;
          }

          // 6502: BCS TQ4 -- too large a number asks for the quantity again.
          if (entry.outcome == DigitResult::TooBig)
          {
            Complain(_universe, _ports, QUANTITY_TOKEN);
            continue;
          }

          /*
           * 6502: STA P / JSR tnpr / LDY #206 / LDA R / BEQ P%+4 / BCS Tc.
           *
           * The `BEQ P%+4` steps over the `BCS`, so a quantity of ZERO never hears about the hold
           * being full. It costs nothing and buys nothing, which is how the original lets a player
           * press RETURN past an item.
           */
          const bool fits = CargoFits(_universe.commander, static_cast<std::uint8_t>(item), entry.value);
          if (entry.value != 0 && !fits)
          {
            Complain(_universe, _ports, CARGO_TOKEN);
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
          if (!SpendCash(_universe.commander, cost))
          {
            Complain(_universe, _ports, CASH_TOKEN);
            continue;
          }

          /*
           * 6502: CLC / ADC QQ20,Y / STA QQ20,Y, then LDA AVL,Y / SEC / SBC R / STA AVL,Y.
           *
           * Both carries are set explicitly, so neither is one of the chained ones this port keeps
           * finding. The hold and the market move by the same amount in opposite directions.
           */
          std::uint8_t& aboard = _universe.commander.cargoHold[static_cast<std::size_t>(item)];
          aboard = AddWithCarry(aboard, entry.value, false).value;
          _universe.market.availability[item] = static_cast<std::uint8_t>(_universe.market.availability[item] - entry.value);

          // 6502: PLA / BEQ TT222 -- buying nothing prints no confirmation and makes no sound.
          if (entry.value != 0)
          {
            PrintCashLeft(_universe, _ports);
          }
          break;
        }
      }

      // 6502: TT222 -- LDA QQ29 / CLC / ADC #5 / JSR DOYC / LDA #0 / JSR DOXC.
      _universe.text.row = static_cast<std::uint8_t>(item + FIRST_ITEM_ROW_OFFSET);
      _universe.text.column = 0;
    }
  }

  void ListCargo(Universe& _universe, Ports& _ports, std::uint8_t _view) noexcept
  {

    // 6502: LDY #0 / TT211: STY QQ29.
    for (int item = 0; item < MARKET_ITEM_COUNT; ++item)
    {
      /*
       * 6502: NWDAVxx -- and NWDAV4 comes back HERE, not to the question.
       *
       * So a refused quantity reprints the whole line: the name, the amount held, the units and the
       * prompt. That is why the retry is a loop around the line rather than around the question.
       */
      for (;;)
      {
        // 6502: LDX QQ20,Y / BEQ TT212 -- nothing of this item, nothing to print.
        const std::uint8_t held = _universe.commander.cargoHold[static_cast<std::size_t>(item)];
        if (held == 0)
        {
          break;
        }

        // 6502: TYA / ASL A / ASL A / TAY / LDA QQ23+1,Y / STA QQ19+1 -- the gradient byte, which is
        // all TT152 reads to decide the units.
        const MarketItem entry = MarketItemAt(item);

        // 6502: JSR TT69 -- sentence case AND a newline, because TT69 falls into TT67.
        SetSentenceCaseAndNewline(_ports.printer);

        // 6502: CLC / LDA QQ29 / ADC #208 / JSR TT27.
        _ports.printer.Print(static_cast<std::uint8_t>(FIRST_ITEM_TOKEN + item));

        // 6502: LDA #14 / JSR DOXC / PLA / TAX / STA QQ25 / CLC / JSR pr2.
        _universe.text.column = QUANTITY_COLUMN;
        PrintByteValue(_ports.characters, held, false);

        // 6502: JSR TT152.
        PrintMarketUnits(_ports.printer, _ports.characters, entry.gradient);

        // 6502: LDA QQ11 / CMP #4 / BNE TT212 -- only the sell screen asks.
        if (_view != SELL_CARGO_VIEW)
        {
          break;
        }

        // 6502: LDA #205 / JSR TT27 / LDA #206 / JSR DETOK.
        _ports.printer.Print(SELL_TOKEN);
        _ports.tokens.Print(YES_NO_TOKEN);

        // 6502: JSR gnum -- and QQ25 is the amount HELD, so "Y" sells the lot.
        const NumberEntry number = ReadNumber(_ports.keyboard, _ports.characters, _universe.text, held);

        // 6502: gnum's JMP BAY2.
        if (number.outcome == DigitResult::LeaveScreen)
        {
          return;
        }

        /*
         * 6502: BEQ TT212 / BCS NWDAV4, and the order is the original's.
         *
         * The BEQ reads the Z flag `LDA R` at gnum's exit left, so a quantity of zero moves on
         * before the carry is looked at. The two cannot both be true here -- a zero cannot exceed
         * an amount held that the loop has already established is not zero -- but the order is kept
         * because it is what the routine does, and because that argument depends on a fact about
         * the loop rather than about gnum.
         */
        if (number.value == 0)
        {
          break;
        }

        if (number.outcome == DigitResult::TooBig)
        {
          // 6502: NWDAV4 -- JSR TT67 / LDA #176 / JSR prq / JSR dn2 / LDY QQ29 / JMP NWDAVxx.
          PrintNewline(_ports.printer);
          PrintThenQuestion(_ports.printer, ITEM_TOKEN);
          (void)Beep(_universe.sound, false);
          _ports.present.WaitFrames(BEEP_PAUSE_FRAMES);
          continue;
        }

        /*
         * 6502: LDA QQ29 / LDX #255 / STX QQ17 / JSR TT151.
         *
         * The line is printed AGAIN with printing switched off, purely so that TT151 leaves the
         * price in QQ24. A routine whose job is to print is being called for its arithmetic.
         *
         * QQ17 lived in two places in this port until M5-e-2c -- the token printer's copy and the
         * byte CHPR reads for 255 -- and this site set both, with a note that setting one and not
         * the other prints a line the original suppresses. One byte now.
         */
        _universe.text.caseFlags = PRINTING_OFF;

        PrintMarketItem(_ports.printer, _ports.characters, _universe.text, item, _universe.current.economy, _universe.market, false);
        const std::uint8_t price = MarketPrice(item, _universe.current.economy, _universe.market.randomiser);

        // 6502: LDA #0 / STA QQ17 -- printing back on, and it is ALL CAPS afterwards rather than
        // whatever it was before.
        _universe.text.caseFlags = 0;

        // 6502: LDA QQ20,Y / SEC / SBC R / STA QQ20,Y.
        _universe.commander.cargoHold[static_cast<std::size_t>(item)] = static_cast<std::uint8_t>(held - number.value);

        // 6502: LDA R / STA P / LDA QQ24 / STA Q / JSR GCASH / JSR MCASH.
        ReceiveCash(_universe.commander, TotalPrice(price, number.value));
        break;
      }
    }

    /*
     * 6502: LDA QQ11 / CMP #4 / BNE P%+8 / JSR dn2 / JMP BAY2.
     *
     * The sell screen ends with a beep and a jump to the INVENTORY screen -- it does not return.
     * The port returns instead and leaves the jump to the caller, because BAY2 is the docked
     * dispatch and that belongs to 2e; a screen that called the next screen would be a loop this
     * layer has no way out of.
     */
    if (_view == SELL_CARGO_VIEW)
    {
      // 6502: .dn2 JSR BEEP / LDY #50 / JMP DELAY
      (void)Beep(_universe.sound, false);
      _ports.present.WaitFrames(BEEP_PAUSE_FRAMES);
      return;
    }

    /*
     * 6502: the Trumble tail, which only the inventory screen reaches.
     *
     * Every extended token it prints is blank in this version of Elite, so what a player sees is a
     * count and possibly an "s". It still calls DORND, which moves the random state -- so the tail
     * is not a no-op even when it prints almost nothing.
     */
    SetSentenceCaseAndNewline(_ports.printer);

    const std::uint16_t trumbles =
      static_cast<std::uint16_t>(_universe.commander.tribbles.lo | (_universe.commander.tribbles.hi << 8));

    // 6502: LDA TRIBBLE / ORA TRIBBLE+1 / BNE P%+3 / zebra: RTS.
    if (trumbles == 0)
    {
      return;
    }

    // 6502: CLC / LDA #0 / LDX TRIBBLE / LDY TRIBBLE+1 / JSR TT11 -- no padding, no decimal point.
    PrintValue(_ports.characters, trumbles, 0, false);

    /*
     * 6502: JSR DORND / AND #3 / CLC / ADC #111 / JSR DETOK.
     *
     * DORND rather than DORND2, so the carry on entry participates -- and what it is here is
     * whatever TT11 left, which is why the random state after this is worth comparing rather than
     * assuming.
     */
    const RngResult roll = _universe.rng.Next(false);
    _ports.tokens.Print(static_cast<std::uint8_t>(TRUMBLE_ADJECTIVE_FIRST + (roll.value & 0x03u)));
    _ports.tokens.Print(TRUMBLE_NOUN_TOKEN);

    // 6502: LDA TRIBBLE+1 / BNE DOANS / LDX TRIBBLE / DEX / BEQ zebra / DOANS: LDA #'s' / JMP DASC.
    if (trumbles == 1)
    {
      return;
    }
    _ports.characters.Put('s');
  }

  void InventoryScreen(Universe& _universe, Ports& _ports) noexcept
  {
    // 6502: LDA #8 / JSR TRADEMODE -- which sets the cursor and the case flags too.
    SetUpScreen(_universe, _ports, INVENTORY_VIEW);
    _ports.keyboard.Flush();

    // 6502: LDA #11 / JSR DOXC / LDA #164 / JSR TT60 -- and TT60 is four routines deep.
    _universe.text.column = INVENTORY_TITLE_COLUMN;
    PrintTitleLine(_ports.printer, _universe.text, INVENTORY_TITLE_TOKEN);

    // 6502: JSR NLIN4 -- the rule is the canvas's, so a caller draws it.

    // 6502: JSR fwl -- which is control code 5, so it goes through the token printer.
    _ports.printer.Print(5);

    // 6502: LDA CRGO / CMP #26 / BCC P%+7 / LDA #107 / JSR TT27.
    if (_universe.commander.cargoCapacity >= LARGE_HOLD_THRESHOLD)
    {
      _ports.printer.Print(LARGE_CARGO_BAY_TOKEN);
    }

    // 6502: JMP TT210 -- a jump rather than a call, so the listing IS the rest of this screen.
    ListCargo(_universe, _ports, INVENTORY_VIEW);
  }

} // namespace Elite
