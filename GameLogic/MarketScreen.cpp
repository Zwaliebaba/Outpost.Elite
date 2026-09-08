#include "pch.h"

#include "MarketScreen.h"

#include "TextPrint2x.h"

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
    /// The tokens the retry loop complains with.
    constexpr std::uint8_t QUANTITY_TOKEN = 176; ///< recursive token 16, "QUANTITY"
    constexpr std::uint8_t CARGO_TOKEN = 206;    ///< recursive token 46, " CARGO{sentence case}"
    constexpr std::uint8_t CASH_TOKEN = 197;     ///< recursive token 37, "CASH"

    /// Recursive token 44, "QUANTITY OF ".
    constexpr std::uint8_t QUANTITY_OF_TOKEN = 204;

    /// The item names run from token 208 ("FOOD") to token 224 ("ALIEN ITEMS").
    constexpr std::uint8_t FIRST_ITEM_TOKEN = 208;

    /// Recursive token 95, the two lines of column headings.
    constexpr std::uint8_t COLUMN_HEADINGS_TOKEN = 255;

    /// Recursive token 119, "CASH:" and the amount, which `dn` prints.
    constexpr std::uint8_t CASH_LINE_TOKEN = 119;

    /// Item N's line ends by putting the cursor on row N + 5, which is where item
    /// N + 1 is printed.
    constexpr std::uint8_t FIRST_ITEM_ROW_OFFSET = 5;

    /// Column 14, where the quantity goes on a cargo listing.
    constexpr std::uint8_t QUANTITY_COLUMN = 14;

    /// "SELL" through `TT27` and "{all caps}(Y/N)?" through `DETOK`.
    constexpr std::uint8_t SELL_TOKEN = 205;
    constexpr std::uint8_t YES_NO_TOKEN = 206;

    /// NWDAV4's complaint, the same token `TT219`'s `TQ4` uses.
    constexpr std::uint8_t ITEM_TOKEN = 176;

    /// Printing off, so `TT151` can be called for its arithmetic alone.
    constexpr std::uint8_t PRINTING_OFF = 255;

    /// The inventory screen's furniture.
    constexpr std::uint8_t INVENTORY_TITLE_COLUMN = 11;
    constexpr std::uint8_t INVENTORY_TITLE_TOKEN = 164; ///< recursive token 4, "INVENTORY{crlf}"
    constexpr std::uint8_t LARGE_CARGO_BAY_TOKEN = 107;
    constexpr std::uint8_t LARGE_HOLD_THRESHOLD = 26; ///< Compared against CRGO as stored

    /// Token 198, and the four at 111 to 114 that `DORND` chooses between. All of them
    /// are blank in this version of Elite, which is why the Trumble line is a bare number.
    constexpr std::uint8_t TRUMBLE_ADJECTIVE_FIRST = 111;
    constexpr std::uint8_t TRUMBLE_NOUN_TOKEN = 198;

    /*
     * Column 17, then token 255 through a branch into `TT162+2`.
     *
     * That branch is used as a jump: the accumulator has just been loaded with 255, so it is always
     * taken, and `TT162+2` is the jump to `TT27` inside the space-printing routine. Two bytes saved
     * over a jump of its own.
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
     * A space, token 119 through `spc`, and then it FALLS INTO dn2.
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
     * A space, the token in Y through `prq`, then `TTX224`'s call to `dn2`.
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

  void SetUpTradeScreen(Universe& _universe, Ports& _ports, std::uint8_t _view) noexcept
  {
    SetUpTradeScreen(_universe, _ports, _view, LayoutForView(_view));
  }

  void SetUpTradeScreen(Universe& _universe, Ports& _ports, std::uint8_t _view, TextLayout _layout) noexcept
  {
    SetUpScreen(_universe, _ports, _view, _layout);
    _ports.keyboard.Flush();                        // FLKB, as a tail call
  }

  void BuyScreen(Universe& _universe, Ports& _ports, bool _misJumped) noexcept
  {
    // TRADEMODE on view 2. The layout is named here for the reason `StatusScreen` names
    // its own: nothing downstream could work out which screen view 2 is (Resolution.md section 6.2).
    SetUpTradeScreen(_universe, _ports, BUY_CARGO_VIEW, BUY_LAYOUT);

    _universe.text.column = 1;
    _universe.text.row = 1;
    _universe.text.caseFlags = 0;

    PrintColumnHeadings(_ports.printer, _universe.text);

    // Sentence case, written out rather than called through `TT69`, so it does NOT print the
    // newline that `TT69` would.
    _ports.printer.SetCaseFlags(0x80);

    // The item counter, from zero.
    for (int item = 0; item < MARKET_ITEM_COUNT; ++item)
    {
      // `TT151` prints the line and leaves the price in `QQ24` and the availability
      // in `QQ25`.
      PrintMarketItem(_ports.printer, _ports.characters, _universe.text, item, _universe.current.economy, _universe.market, _misJumped);

      /*
       * Nothing available skips straight to TT222.
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
         * TT224 through TTX224 -- ask, and on any complaint ask again for the SAME item.
         *
         * There is no limit on the retries in the original and none here: the only ways out are a
         * quantity the screen accepts, or a letter, which `gnum` answers by jumping to `BAY2` and which
         * abandons the whole screen rather than this item.
         */
        for (;;)
        {
          ClearMessageRows(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message,
                       &_universe.picture, _universe.screenLayout);

          // Token 204 -- "QUANTITY OF ".
          _ports.printer.Print(QUANTITY_OF_TOKEN);

          // The item counter plus 208 -- the item's name.
          _ports.printer.Print(static_cast<std::uint8_t>(FIRST_ITEM_TOKEN + item));

          // A slash, the units, a question mark and a newline.
          _ports.printer.Print('/');
          PrintMarketUnits(_ports.printer, _ports.characters, MarketItemAt(item).gradient);
          _ports.printer.Print('?');
          PrintNewline(_ports.printer);

          // `gnum`. The four instructions before it have no effect: `gnum` repeats
          // both stores at its own start. The original's comment wonders whether they were left
          // behind when code moved, and they are not reproduced here.
          const NumberEntry entry = ReadNumber(_ports.keyboard, _ports.characters, _universe.text, available);

          // `gnum` jumps to `BAY2` -- a letter leaves the screen entirely, not just this item.
          if (entry.outcome == DigitResult::LeaveScreen)
          {
            return;
          }

          // Too large a number asks for the quantity again.
          if (entry.outcome == DigitResult::TooBig)
          {
            Complain(_universe, _ports, QUANTITY_TOKEN);
            continue;
          }

          /*
           * The hold checked through `tnpr`, with a zero quantity branching past the test.
           *
           * That branch steps over the carry test, so a quantity of ZERO never hears about the hold
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
           * The price through `GCASH` and `LCASH`, and a clear carry goes to Tc.
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
           * The quantity added to the hold, then taken off what is available.
           *
           * Both carries are set explicitly, so neither is one of the chained ones this port keeps
           * finding. The hold and the market move by the same amount in opposite directions.
           */
          std::uint8_t& aboard = _universe.commander.cargoHold[static_cast<std::size_t>(item)];
          aboard = AddWithCarry(aboard, entry.value, false).value;
          _universe.market.availability[item] = static_cast<std::uint8_t>(_universe.market.availability[item] - entry.value);

          // Buying nothing goes to TT222 -- no confirmation and no sound.
          if (entry.value != 0)
          {
            PrintCashLeft(_universe, _ports);
          }
          break;
        }
      }

      // The cursor to row N + 5, column 0.
      _universe.text.row = static_cast<std::uint8_t>(item + FIRST_ITEM_ROW_OFFSET);
      _universe.text.column = 0;
    }
  }

  void ListCargo(Universe& _universe, Ports& _ports, std::uint8_t _view) noexcept
  {

    // The item counter, from zero.
    for (int item = 0; item < MARKET_ITEM_COUNT; ++item)
    {
      /*
       * NWDAVxx -- and NWDAV4 comes back HERE, not to the question.
       *
       * So a refused quantity reprints the whole line: the name, the amount held, the units and the
       * prompt. That is why the retry is a loop around the line rather than around the question.
       */
      for (;;)
      {
        // Nothing of this item, nothing to print.
        const std::uint8_t held = _universe.commander.cargoHold[static_cast<std::size_t>(item)];
        if (held == 0)
        {
          break;
        }

        // The item index scaled by four to reach its table row, for the gradient byte -- which
        // is all `TT152` reads to decide the units.
        const MarketItem entry = MarketItemAt(item);

        // Sentence case AND a newline, because it falls into `TT67`.
        SetSentenceCaseAndNewline(_ports.printer);

        // The item counter plus 208, printed.
        _ports.printer.Print(static_cast<std::uint8_t>(FIRST_ITEM_TOKEN + item));

        // Column 14, then the amount held through `pr2` with no padding.
        _universe.text.column = QUANTITY_COLUMN;
        PrintByteValue(_ports.characters, held, false);

        PrintMarketUnits(_ports.printer, _ports.characters, entry.gradient);

        // Any view but 4 goes to TT212 -- only the sell screen asks.
        if (_view != SELL_CARGO_VIEW)
        {
          break;
        }

        // Token 205, then token 206 through `DETOK`.
        _ports.printer.Print(SELL_TOKEN);
        _ports.tokens.Print(YES_NO_TOKEN);

        // JSR gnum -- and QQ25 is the amount HELD, so "Y" sells the lot.
        const NumberEntry number = ReadNumber(_ports.keyboard, _ports.characters, _universe.text, held);

        // `gnum` jumps to `BAY2`.
        if (number.outcome == DigitResult::LeaveScreen)
        {
          return;
        }

        /*
         * TT212 on a zero, NWDAV4 on a set carry, and the order is the original's.
         *
         * The zero test reads the flag `gnum`'s exit load left, so a quantity of zero moves on
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
          // A newline, the complaint through `prq`, `dn2`, and back to NWDAVxx.
          PrintNewline(_ports.printer);
          PrintThenQuestion(_ports.printer, ITEM_TOKEN);
          (void)Beep(_universe.sound, false);
          _ports.present.WaitFrames(BEEP_PAUSE_FRAMES);
          continue;
        }

        /*
         * `TT151` called again with printing switched off.
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

        // Printing back on, and it is ALL CAPS afterwards rather than whatever it was
        // before.
        _universe.text.caseFlags = 0;

        // The quantity taken off the hold.
        _universe.commander.cargoHold[static_cast<std::size_t>(item)] = static_cast<std::uint8_t>(held - number.value);

        // The total through `GCASH`, then paid in through `MCASH`.
        ReceiveCash(_universe.commander, TotalPrice(price, number.value));
        break;
      }
    }

    /*
     * View 4 alone falls through to `dn2` and then jumps to `BAY2`.
     *
     * The sell screen ends with a beep and a jump to the INVENTORY screen -- it does not return.
     * The port returns instead and leaves the jump to the caller, because BAY2 is the docked
     * dispatch and that belongs to 2e; a screen that called the next screen would be a loop this
     * layer has no way out of.
     */
    if (_view == SELL_CARGO_VIEW)
    {
      // A beep, then fifty frames of `DELAY`
      (void)Beep(_universe.sound, false);
      _ports.present.WaitFrames(BEEP_PAUSE_FRAMES);
      return;
    }

    /*
     * The Trumble tail, which only the inventory screen reaches.
     *
     * Every extended token it prints is blank in this version of Elite, so what a player sees is a
     * count and possibly an "s". It still calls DORND, which moves the random state -- so the tail
     * is not a no-op even when it prints almost nothing.
     */
    SetSentenceCaseAndNewline(_ports.printer);

    const std::uint16_t trumbles =
      static_cast<std::uint16_t>(_universe.commander.tribbles.lo | (_universe.commander.tribbles.hi << 8));

    // A population of zero returns at `zebra`.
    if (trumbles == 0)
    {
      return;
    }

    // The pair through `TT11` -- no padding, no decimal point.
    PrintValue(_ports.characters, trumbles, 0, false);

    /*
     * Two random bits added to 111, and the token printed.
     *
     * DORND rather than DORND2, so the carry on entry participates -- and what it is here is
     * whatever TT11 left, which is why the random state after this is worth comparing rather than
     * assuming.
     */
    const RngResult roll = _universe.rng.Next(false);
    _ports.tokens.Print(static_cast<std::uint8_t>(TRUMBLE_ADJECTIVE_FIRST + (roll.value & 0x03u)));
    _ports.tokens.Print(TRUMBLE_NOUN_TOKEN);

    // Anything but exactly one gets an "s".
    if (trumbles == 1)
    {
      return;
    }
    _ports.characters.Put('s');
  }

  void InventoryScreen(Universe& _universe, Ports& _ports) noexcept
  {
    /*
     * TRADEMODE on view 8, which sets the cursor and the case flags too.
     *
     * View 8 is the status screen's as well, so the layout is this screen's own and is named here.
     * That collision is the whole reason `SetUpTradeScreen` takes one (Resolution.md section 6.2).
     */
    SetUpTradeScreen(_universe, _ports, INVENTORY_VIEW, INVENTORY_LAYOUT);

    // Column 11, then token 164 through `TT60` -- which is four routines deep.
    _universe.text.column = INVENTORY_TITLE_COLUMN;
    PrintTitleLine(_ports.printer, _universe.text, INVENTORY_TITLE_TOKEN);

    // The rule is the canvas's, so a caller draws it.

    // JSR fwl -- which is control code 5, so it goes through the token printer.
    _ports.printer.Print(5);

    // A hold of 26 or more gets token 107 as well.
    if (_universe.commander.cargoCapacity >= LARGE_HOLD_THRESHOLD)
    {
      _ports.printer.Print(LARGE_CARGO_BAY_TOKEN);
    }

    // TT210, as a jump rather than a call, so the listing IS the rest of this screen.
    ListCargo(_universe, _ports, INVENTORY_VIEW);
  }

} // namespace Elite
