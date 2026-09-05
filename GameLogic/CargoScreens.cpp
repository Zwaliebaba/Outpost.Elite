#include "pch.h"

#include "CargoScreens.h"

#include "Arith.h"
#include "DockedKeys.h"
#include "EliteTypes.h"
#include "TextPrint.h"

/*
 * The modernised trading screens (ADR-006). Not a port; see the header.
 */

namespace Elite
{

  namespace
  {
    /// 6502: the tokens the screens share with TT219 and TT167 -- the same numbers, so the same words.
    constexpr std::uint8_t QUANTITY_TOKEN = 176;   ///< recursive token 16, "QUANTITY"
    constexpr std::uint8_t CARGO_TOKEN = 206;      ///< recursive token 46, " CARGO{sentence case}"
    constexpr std::uint8_t CASH_TOKEN = 197;       ///< recursive token 37, "CASH"
    constexpr std::uint8_t CASH_LINE_TOKEN = 119;  ///< "CASH:" and the amount, which `dn` prints
    constexpr std::uint8_t FIRST_ITEM_TOKEN = 208; ///< "FOOD" to "ALIEN ITEMS"
    constexpr std::uint8_t BELL = 7;               ///< CHPR's bell, which is R5's beep

    /// 6502: LDX #128 / STX QQ17 -- the case every market line prints in.
    constexpr std::uint8_t SENTENCE_CASE = 0x80;
    constexpr std::uint8_t ALL_CAPS = 0;

    /// The columns of a market line, which are `PrintMarketItem`'s: name at 1, units at 14, the
    /// price in the five cells from 16, the quantity in the five from 21. The headings sit over them.
    constexpr std::uint8_t NAME_COLUMN = 1;
    constexpr std::uint8_t UNIT_COLUMN = 14;
    constexpr std::uint8_t UNIT_HEADING_COLUMN = 12;
    constexpr std::uint8_t PRICE_HEADING_COLUMN = 17;
    constexpr std::uint8_t QUANTITY_HEADING_COLUMN = 23;
    constexpr std::uint8_t BUY_HEADING_COLUMN = 17;
    constexpr std::uint8_t SLASH_COLUMN = 22;
    constexpr std::uint8_t SELL_HEADING_COLUMN = 24;

    /// How wide the prompt is: "QUANTITY? " and three digits, from column 17 to the right margin.
    constexpr std::uint8_t PROMPT_WIDTH = 32 - BUY_CARGO_PROMPT_COLUMN;
    constexpr std::uint8_t TEXT_WIDTH = 32;

    /// The multiply that turns a price byte into tenths of a credit: GC2 is GCASH's two shifts.
    constexpr std::uint16_t PRICE_SCALE = 4;

    /// Print verbatim characters at a cell. Through CHPR directly, so no token or case machinery
    /// touches them: this is how the screens print text the original has no token for.
    void PrintText(TextPrinter& _raw, TextState& _text, std::uint8_t _row, std::uint8_t _column, const char* _string) noexcept
    {
      _text.row = _row;
      _text.column = _column;
      for (const char* at = _string; *at != '\0'; ++at)
      {
        (void)_raw.Print(static_cast<std::uint8_t>(*at));
      }
    }

    /// One market line, exactly as TT151 prints it, on the row an item belongs to.
    void PrintItemLine(TradeScreen& _screen, std::uint8_t _row, int _item, std::uint8_t _economy, MarketState& _market) noexcept
    {
      _screen.text.row = _row;
      _screen.printer.SetCaseFlags(SENTENCE_CASE);
      PrintMarketItem(_screen.printer, _screen.characters, _screen.text, _item, _economy, _market, false);
    }

    /// The cash line on the last row: clear it first, because CHPR draws by EOR.
    void PrintCashLine(TradeScreen& _screen, Canvas& _canvas) noexcept
    {
      ClearTextCells(_canvas, BUY_CARGO_CASH_ROW, 0, TEXT_WIDTH);
      _screen.text.row = BUY_CARGO_CASH_ROW;
      _screen.text.column = NAME_COLUMN;
      _screen.printer.SetCaseFlags(ALL_CAPS);
      PrintThenSpace(_screen.printer, CASH_LINE_TOKEN);
    }

    void ClearPrompt(Canvas& _canvas) noexcept
    {
      ClearTextCells(_canvas, TRADE_TITLE_ROW, BUY_CARGO_PROMPT_COLUMN, PROMPT_WIDTH);
    }

    /// "QUANTITY? " where the prompt goes, leaving the cursor after it for the digits.
    void OpenPrompt(TradeScreen& _screen, Canvas& _canvas) noexcept
    {
      ClearPrompt(_canvas);
      _screen.text.row = TRADE_TITLE_ROW;
      _screen.text.column = BUY_CARGO_PROMPT_COLUMN;
      _screen.printer.SetCaseFlags(ALL_CAPS);
      PrintThenQuestion(_screen.printer, QUANTITY_TOKEN);
      PrintSpace(_screen.printer);
    }

    /// Tc's shape in the prompt's place: the one-word complaint, a question mark, a beep and a pause.
    void Complain(TradeScreen& _screen, Canvas& _canvas, std::uint8_t _token) noexcept
    {
      ClearPrompt(_canvas);
      _screen.text.row = TRADE_TITLE_ROW;
      _screen.text.column = BUY_CARGO_PROMPT_COLUMN;
      _screen.printer.SetCaseFlags(ALL_CAPS);
      PrintThenQuestion(_screen.printer, _token);
      _screen.effects.BeepAndPause();
      ClearPrompt(_canvas);
    }

    [[nodiscard]] constexpr std::uint8_t ItemRow(int _item) noexcept
    {
      return static_cast<std::uint8_t>(BUY_CARGO_FIRST_ITEM_ROW + _item);
    }

    /*
     * Whether a key the screen does not use is one `TT102` would send somewhere.
     *
     * Only the actions that reach a screen count. `CountdownOnly` is what the dispatch answers for
     * any unknown key on a non-chart view, and `ShowDistance`, `SearchBySystemName` and the
     * crosshair actions are the charts' -- none of them is a reason to leave.
     */
    [[nodiscard]] bool LeavesTheScreen(KeyAction _action) noexcept
    {
      switch (_action)
      {
      case KeyAction::StatusMode:
      case KeyAction::LongRangeChart:
      case KeyAction::ShortRangeChart:
      case KeyAction::DataOnSystem:
      case KeyAction::Inventory:
      case KeyAction::MarketPrice:
      case KeyAction::Launch:
      case KeyAction::EquipShip:
      case KeyAction::BuyCargo:
      case KeyAction::DiskAccess:
      case KeyAction::SellCargo:
        return true;
      default:
        return false;
      }
    }

    /*
     * The prompt, from `B` to a purchase or a cancel. Returns whether anything was bought, which is
     * what decides whether the row and the cash line need reprinting.
     */
    [[nodiscard]] bool AskQuantity(TradeScreen& _screen, ListKeySource& _keys, Canvas& _canvas, TextPrinter& _raw,
                                   CommanderBlock& _commander, MarketState& _market, std::uint8_t _economy, int _item) noexcept
    {
      OpenPrompt(_screen, _canvas);

      QuantityEntry entry{};
      for (;;)
      {
        const ListKeyPress key = _keys.NextListKey();
        const QuantityStep step = EditQuantity(entry, key);

        switch (step)
        {
        case QuantityStep::Typed:
          (void)_raw.Print(key.character);
          continue;

        case QuantityStep::Deleted:
          // CHPR's own DELETE: back one cell and clear it, which is what OSW05 prints too.
          (void)_raw.Print(127);
          continue;

        case QuantityStep::Refused:
          _screen.printer.Print(BELL);
          continue;

        case QuantityStep::Ignored:
          continue;

        case QuantityStep::Cancelled:
          ClearPrompt(_canvas);
          return false;

        case QuantityStep::Committed:
          break;
        }

        // 6502: TT224's `BCS TQ4` -- more than the market holds asks for the quantity again. Here
        // it complains and goes back to the list; the bar is still on the item.
        const std::uint8_t available = _market.availability[static_cast<std::size_t>(_item)];
        if (entry.value > available)
        {
          Complain(_screen, _canvas, QUANTITY_TOKEN);
          return false;
        }

        // 6502: JSR tnpr / LDY #206 / BCS Tc -- and the value is never zero here, so the branch
        // TT219 steps over for a zero does not arise.
        if (!CargoFits(_commander, static_cast<std::uint8_t>(_item), entry.value))
        {
          Complain(_screen, _canvas, CARGO_TOKEN);
          return false;
        }

        // 6502: JSR GCASH / JSR LCASH / LDY #197 / BCC Tc. LCASH puts the money back itself.
        const std::uint8_t price = MarketPrice(_item, _economy, _market.randomiser);
        if (!SpendCash(_commander, TotalPrice(price, entry.value)))
        {
          Complain(_screen, _canvas, CASH_TOKEN);
          return false;
        }

        // 6502: CLC / ADC QQ20,Y / STA QQ20,Y and LDA AVL,Y / SEC / SBC R / STA AVL,Y.
        const std::size_t hold = static_cast<std::size_t>(Field::CargoHold) + static_cast<std::size_t>(_item);
        _commander.bytes[hold] = AddWithCarry(_commander.bytes[hold], entry.value, false).value;
        _market.availability[static_cast<std::size_t>(_item)] = static_cast<std::uint8_t>(available - entry.value);

        ClearPrompt(_canvas);
        return true;
      }
    }
  } // namespace

  QuantityStep EditQuantity(QuantityEntry& _entry, const ListKeyPress& _key) noexcept
  {
    switch (_key.key)
    {
    case ListKey::Digit:
    {
      if (_entry.digits >= QUANTITY_MAX_DIGITS)
      {
        return QuantityStep::Refused;
      }
      const std::uint16_t next = static_cast<std::uint16_t>(_entry.value * 10u + (_key.character - '0'));
      if (next > 255u)
      {
        return QuantityStep::Refused;
      }
      _entry.value = static_cast<std::uint8_t>(next);
      ++_entry.digits;
      return QuantityStep::Typed;
    }

    case ListKey::Delete:
      if (_entry.digits == 0u)
      {
        return QuantityStep::Refused;
      }
      _entry.value = static_cast<std::uint8_t>(_entry.value / 10u);
      --_entry.digits;
      return QuantityStep::Deleted;

    case ListKey::Return:
      return (_entry.value == 0u) ? QuantityStep::Cancelled : QuantityStep::Committed;

    case ListKey::Escape:
      return QuantityStep::Cancelled;

    case ListKey::Up:
    case ListKey::Down:
    case ListKey::Buy:
    case ListKey::Other:
      return QuantityStep::Ignored;
    }
    return QuantityStep::Ignored;
  }

  std::uint8_t BuyCargoScreen(TradeScreen& _screen, ListKeySource& _keys, Canvas& _canvas, CommanderBlock& _commander, MarketState& _market,
                              std::uint8_t _economy, std::uint8_t _dockedFlag) noexcept
  {
    // 6502: LDA #2 / JSR TRADEMODE -- the same view number TT219 uses, so the dispatch sees the
    // same screen it always did.
    _screen.effects.SetUpTradeScreen(BUY_CARGO_VIEW);

    // Black and white, whatever the last screen left in COL2 (gnum's BAY2 exit leaves it purple).
    _screen.text.cellColour = TEXT_COLOUR_WHITE;

    TextPrinter raw(_canvas, _screen.text);

    PrintText(raw, _screen.text, TRADE_TITLE_ROW, NAME_COLUMN, "BUY CARGO");
    PrintText(raw, _screen.text, 2, NAME_COLUMN, "Use the cursor keys to select");
    PrintText(raw, _screen.text, 3, NAME_COLUMN, "an item. Press B to buy it.");
    PrintText(raw, _screen.text, BUY_CARGO_HEADING_ROW, NAME_COLUMN, "PRODUCT");
    PrintText(raw, _screen.text, BUY_CARGO_HEADING_ROW, UNIT_HEADING_COLUMN, "UNIT");
    PrintText(raw, _screen.text, BUY_CARGO_HEADING_ROW, PRICE_HEADING_COLUMN, "PRICE");
    PrintText(raw, _screen.text, BUY_CARGO_HEADING_ROW, QUANTITY_HEADING_COLUMN, "QTY");

    for (int item = 0; item < MARKET_ITEM_COUNT; ++item)
    {
      PrintItemLine(_screen, ItemRow(item), item, _economy, _market);
    }

    PrintCashLine(_screen, _canvas);

    int selected = 0;
    InvertTextRow(_canvas, ItemRow(selected));

    for (;;)
    {
      const ListKeyPress key = _keys.NextListKey();

      switch (key.key)
      {
      case ListKey::Up:
      case ListKey::Down:
      {
        const int step = (key.key == ListKey::Up) ? (MARKET_ITEM_COUNT - 1) : 1;
        InvertTextRow(_canvas, ItemRow(selected));
        selected = (selected + step) % MARKET_ITEM_COUNT;
        InvertTextRow(_canvas, ItemRow(selected));
        continue;
      }

      case ListKey::Buy:
      {
        if (_market.availability[static_cast<std::size_t>(selected)] == 0u)
        {
          _screen.printer.Print(BELL);
          continue;
        }

        if (AskQuantity(_screen, _keys, _canvas, raw, _commander, _market, _economy, selected))
        {
          // The row changed, so it is reprinted -- bar off, cells cleared, line, bar on -- and so
          // did the cash. The beep is dn2's: `dn` falls into it after printing the cash.
          InvertTextRow(_canvas, ItemRow(selected));
          ClearTextCells(_canvas, ItemRow(selected), 0, TEXT_WIDTH);
          PrintItemLine(_screen, ItemRow(selected), selected, _economy, _market);
          InvertTextRow(_canvas, ItemRow(selected));
          PrintCashLine(_screen, _canvas);
          _screen.effects.BeepAndPause();
        }
        continue;
      }

      case ListKey::Digit:
      case ListKey::Other:
      {
        /*
         * A digit is asked about too, because the key map is many-to-one (ADR-005 §4): F4 and "7"
         * are one position, and while nothing is being typed that position is the market screen's.
         * The buy screen is docked-only, so no hyperspace countdown can be running: 0 and false.
         */
        const KeyOutcome outcome = ActionForKey(key.position, _dockedFlag, BUY_CARGO_VIEW, 0, false);
        if (LeavesTheScreen(outcome.action))
        {
          return key.position;
        }
        continue;
      }

      case ListKey::Delete:
      case ListKey::Return:
      case ListKey::Escape:
        continue;
      }
    }
  }

  void MarketPricesScreen(TradeScreen& _screen, Canvas& _canvas, const SystemSeeds& _currentSystem, std::uint8_t _economy,
                          MarketState& _market, bool _misJumped) noexcept
  {
    // 6502: TT167's TRADEMODE, with the view number the executable has always given it.
    _screen.effects.SetUpTradeScreen(BUY_CARGO_VIEW);
    _screen.text.cellColour = TEXT_COLOUR_WHITE;

    TextPrinter raw(_canvas, _screen.text);

    /*
     * "<SYSTEM> MARKET PRICES". The name is cpl's, from the seeds this screen is GIVEN -- QQ2, the
     * system the market belongs to -- and cpl leaves the seeds as it found them, which the copy
     * makes certain of. The words after it are printed verbatim rather than through token 167,
     * because that token carries a system name of its own, taken from the value tokens' idea of
     * the current system rather than from the argument here.
     */
    _screen.text.row = TRADE_TITLE_ROW;
    _screen.text.column = NAME_COLUMN;
    _screen.printer.SetCaseFlags(ALL_CAPS);
    SystemSeeds seeds = _currentSystem;
    (void)PrintSystemName(_screen.printer, seeds);
    PrintText(raw, _screen.text, TRADE_TITLE_ROW, _screen.text.column, " MARKET PRICES");

    PrintText(raw, _screen.text, MARKET_PRICES_HEADING_ROW, NAME_COLUMN, "PRODUCT");
    PrintText(raw, _screen.text, MARKET_PRICES_HEADING_ROW, UNIT_HEADING_COLUMN, "UNIT");
    PrintText(raw, _screen.text, MARKET_PRICES_HEADING_ROW, BUY_HEADING_COLUMN, "BUY");
    PrintText(raw, _screen.text, MARKET_PRICES_HEADING_ROW, SLASH_COLUMN, "/");
    PrintText(raw, _screen.text, MARKET_PRICES_HEADING_ROW, SELL_HEADING_COLUMN, "SELL");

    // 6502: TT151q -- in witchspace there is no market, and the lines print nothing.
    if (_misJumped)
    {
      return;
    }

    for (int item = 0; item < MARKET_ITEM_COUNT; ++item)
    {
      _screen.text.row = static_cast<std::uint8_t>(MARKET_PRICES_FIRST_ITEM_ROW + item);
      _screen.text.column = NAME_COLUMN;
      _screen.printer.SetCaseFlags(SENTENCE_CASE);
      _screen.printer.Print(static_cast<std::uint8_t>(FIRST_ITEM_TOKEN + item));

      _screen.text.column = UNIT_COLUMN;
      const MarketItem entry = MarketItemAt(item);
      PrintMarketUnits(_screen.printer, _screen.characters, entry.gradient);

      // The price the way TT151 prints it: the byte times four, five wide, one digit after the
      // point. And `var`'s side effect with it: working out a price zeroes Alien Items (§6.16).
      const std::uint8_t price = MarketPrice(item, _economy, _market.randomiser);
      _market.availability[MARKET_ITEM_COUNT - 1] = 0;
      PrintValue(_screen.characters, static_cast<std::uint16_t>(price * PRICE_SCALE), 5, true);

      // The sell column: nothing, until buy and sell differ.
    }
  }

} // namespace Elite
