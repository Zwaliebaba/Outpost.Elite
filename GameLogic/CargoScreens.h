#pragma once

#include "Canvas.h"
#include "Commander.h"
#include "Market.h"
#include "MarketScreen.h"
#include "Universe.h"

#include <cstdint>

namespace Elite
{

  /*
   * The modernised trading screens (ADR-006).
   *
   * NOT A PORT. Nothing in this file has a `6502:` label of its own, because the original never had
   * these screens: a Buy Cargo screen that lists the market with a selection bar and asks for a
   * quantity only when `B` is pressed, and a read-only Market Prices screen with a buy column and
   * a sell column. The faithful `TT219` and `TT167` stay in `MarketScreen.h` and `Market.h` with
   * their oracle tests; these two are what the executable wires to the keys instead.
   *
   * What IS the original's is every number. Prices come from `MarketPrice`, the room check is
   * `CargoFits`, the cash is `SpendCash` and `TotalPrice`, and each market line is printed by
   * `PrintMarketItem` -- so the kilo/gram spacing, the dash for an empty item and the Alien Items
   * side effect (§6.16) are all exactly what the oracle-compared routines produce.
   */

  /*
   * One key, as the Buy Cargo screen reads it.
   *
   * A second seam beside `KeySource`, and it exists because `KeySource::NextKey` returns the
   * CHARACTER `TRANTABLE` gives a matrix position -- and the arrow keys have no character. The
   * window queues an arrow as the C64 roll or pitch key it also stands for, which translates to
   * `X`, `S`, `<` and `>`; making the screen read `X` as "up" would put the executable's key layout
   * into GameLogic. So the executable interprets the key and the screen reads the meaning.
   *
   * `Other` carries the matrix position, because a key the screen does not use may be one `TT102`
   * does: the screen asks `ActionForKey` and, if the key reaches a screen, leaves and hands the
   * position back so that the dispatch performs it. That replaces `BAY2`'s forced f9 with the key
   * the player actually pressed.
   */
  enum class ListKey
  {
    Up,
    Down,
    Buy,    ///< "B"
    Digit,  ///< "0" to "9", with the character in `character`
    Delete, ///< character 127, which is Backspace on a PC keyboard
    Return, ///< character 13
    Escape, ///< character 27
    Other,  ///< anything else, with the matrix position in `position`
  };

  struct ListKeyPress
  {
    ListKey key = ListKey::Other;
    std::uint8_t character = 0; ///< the digit, for Digit; the translated character otherwise
    std::uint8_t position = 0;  ///< the C64 matrix position, for `ActionForKey`
  };

  class ListKeySource
  {
  public:
    virtual ~ListKeySource() = default;

    /// Blocks until a key is pressed, as `TT217` does, and returns what it means.
    virtual ListKeyPress NextListKey() = 0;
  };

  /*
   * The quantity being typed at the top of the Buy Cargo screen.
   *
   * Three digits at most and a value that fits a byte, because the hold, the market and every
   * routine that moves them are eight bits wide. `digits` is kept separately from `value` so that
   * "0" and "00" are things that can be deleted one keystroke at a time.
   */
  struct QuantityEntry
  {
    std::uint8_t value = 0;
    std::uint8_t digits = 0;
  };

  inline constexpr std::uint8_t QUANTITY_MAX_DIGITS = 3;

  /// What one keystroke did to the quantity.
  enum class QuantityStep
  {
    Typed,     ///< a digit went in; echo it
    Deleted,   ///< the last digit came out; rub it out
    Refused,   ///< a fourth digit, a value past 255, or Delete on an empty prompt; beep
    Committed, ///< Return with a value of one or more
    Cancelled, ///< Return with nothing or zero typed, or Escape
    Ignored,   ///< a key the prompt does not use
  };

  /// The prompt's state machine, one key at a time. Pure, so the whole input space can be swept.
  [[nodiscard]] QuantityStep EditQuantity(QuantityEntry& _entry, const ListKeyPress& _key) noexcept;

  /// The rows the screens print on. Named so the tests and the screens agree on where a line is.
  inline constexpr std::uint8_t TRADE_TITLE_ROW = 1;
  inline constexpr std::uint8_t BUY_CARGO_HEADING_ROW = 4;
  inline constexpr std::uint8_t BUY_CARGO_FIRST_ITEM_ROW = 5;
  inline constexpr std::uint8_t BUY_CARGO_CASH_ROW = 23;
  inline constexpr std::uint8_t BUY_CARGO_PROMPT_COLUMN = 17;
  inline constexpr std::uint8_t MARKET_PRICES_HEADING_ROW = 3;
  inline constexpr std::uint8_t MARKET_PRICES_FIRST_ITEM_ROW = 5;

  /*
   * The Buy Cargo screen.
   *
   * Draws the market, puts the bar on the first item and then loops on `_keys` until a key that
   * `TT102` would act on is pressed -- which is the ONLY way out, and the returned value is that
   * key's matrix position for the dispatch to perform. Up and Down move the bar and wrap; `B` on a
   * row with stock opens the prompt, and on a row showing a dash rings the bell; while the prompt
   * is open, digits, Delete, Return and Escape are the prompt's and everything else is ignored.
   * While it is closed a digit is a screen key if `TT102` says so -- F4 and "7" are one position
   * (ADR-005 §4), and "7" on the list means the market, not seven of anything.
   *
   * A committed quantity runs `TT219`'s three checks in `TT219`'s order -- more than the market
   * has, no room in the hold, not enough cash -- and the first that fails prints the original's
   * one-word complaint where the prompt was, beeps and pauses (`dn2`), and returns to selecting.
   * One that passes moves the hold and the market by the same amount in opposite directions,
   * reprints the row and the cash line, and beeps.
   *
   * `_canvas` is taken directly because the bar and the prompt are cell operations on the bitmap,
   * and `TradeScreen` carries no canvas: the ported screens never needed one.
   */
  [[nodiscard]] std::uint8_t BuyCargoScreen(TradeScreen& _screen, ListKeySource& _keys, Canvas& _canvas, CommanderBlock& _commander,
                                            MarketState& _market, std::uint8_t _economy, std::uint8_t _dockedFlag) noexcept;

  /*
   * The Market Prices screen: `<SYSTEM> MARKET PRICES`, a heading row and seventeen lines of
   * product, unit and buy price. Read-only, so it draws and returns, exactly as `PrintMarketScreen`
   * does; `TT167` sits above `TT102`'s docked split, so this is reachable in flight too.
   *
   * The sell column is left empty because this game has one price a good: selling goes through
   * the same `MarketPrice` as buying. The heading keeps both columns so the screen does not change
   * shape on the day a sell price exists.
   *
   * `_misJumped` is MJ: in witchspace there is no market, and the lines print nothing.
   */
  void MarketPricesScreen(TradeScreen& _screen, Canvas& _canvas, const SystemSeeds& _currentSystem, std::uint8_t _economy,
                          MarketState& _market, bool _misJumped) noexcept;

} // namespace Elite
