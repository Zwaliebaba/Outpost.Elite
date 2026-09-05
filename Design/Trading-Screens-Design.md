# Trading screens — design for the combined Buy Cargo screen and the Market Prices screen

**Status:** draft for owner review, 2026-09-05. Nothing in this document is built. It becomes
ADR-006 and a plan slice once the open questions in §8 are answered.

## 1. What is asked

Two screens replace two.

- **Buy Cargo** replaces the ported `TT219` buy screen *and* the ported `TT167` market price
  screen as the docked screen on key `1`. It lists all seventeen goods with product, unit, price
  and quantity for sale; a highlight bar marks one row; the cursor keys move it; `B` opens a
  quantity prompt at the top of the screen; digits, Backspace and Return edit and commit it;
  Return on an empty prompt or on `0` cancels.
- **Market Prices** (`<SYSTEM> MARKET PRICES`) is a new read-only screen on key `7` / F4, docked
  and in flight, listing product, unit, buy price and sell price. This game has one price per
  good — selling goes through the same `MarketPrice` as buying — so the sell column is empty
  until something makes the two differ.

Both are black and white. The purple that `gnum` types quantities in does not appear.

The Sell Cargo screen, the Inventory screen and the equipment shop are not touched.

## 2. The weakest point first: this is modernisation before the gate

ADR-001 §4 says *"Modernisation comes after, and each item is its own decision ... The gate for
starting phase 6 is: every oracle suite, every golden and the replay suite green on the faithful
build."* Phase 4 (hyperspace, the charts' jump) is not closed, so the gate is not met.

ADR-005 §4's 2026-09-05 key-map amendment is the nearest precedent, and it does **not** cover
this. Its argument was that *which physical key reaches `KEYLOOK+17`* is not part of what the
machine computes. A new screen layout, a selection bar and a different buy loop **are** what the
machine computes: the canvas differs, the sequence of `TT217` reads differs, and the two ported
routines lose their only caller.

So this needs an owner ruling, recorded as **ADR-006**, and the design below is shaped to make
the ruling cheap:

- `BuyScreen` (`TT219`), `PrintMarketScreen` (`TT167`), `ReadNumber` (`gnum`) and their oracle
  tests **stay in `GameLogic` and stay green**. They remain the proof that the price model, the
  hold arithmetic and the cash arithmetic match the original. They are simply no longer what the
  executable wires to keys `1` and `7`.
- The new screens are a **separate module** that calls the same `MarketPrice`, `CargoFits`,
  `SpendCash`, `TotalPrice` and `PrintMarketItem`. Every number a player sees or pays is still
  the oracle-proven one; what changes is presentation and the input loop around it.
- Restoring fidelity is two `case` labels in `Perform`.

If the owner would rather wait for phase 6, the document is filed and nothing else changes.

## 3. Buy Cargo — layout

Text cells: 32 columns (`XC` 0–31), rows 1–23. The border box is `TT66`'s as today.

```
row  1  BUY CARGO                QUANTITY? 11_
row  2  Use the cursor keys to select an item.
row  3  Press B to buy the selected item.
row  4  PRODUCT     UNIT   PRICE    QTY
row  5  Food        t        3.6    16t          <- highlight bar on the selected row
row  6  Textiles    t        6.0    15t
 ...
row 21  Alien Items t       51.2      -
row 22
row 23  CASH: 100.0 CR
```

- Rows 5–21 are the seventeen items, each printed by the existing `PrintMarketItem` (`TT151`):
  name at column 1, units and price at column 14, quantity, or a dash at column 25 when there
  is none. That keeps the line format, the kilo/gram spacing quirk and the Alien Items side
  effect (§6.16 — printing a price zeroes `AVL+16`) exactly as they are. The picture's single
  heading row replaces token 255's two rows so that the list, the instructions and the cash line
  all fit.
- The **highlight bar** inverts the bitmap of the 32 cells of one row (256 bytes XORed with
  `&FF`). Text screens are standard bitmap mode, so inverting a cell swaps its two colours and
  produces black text on a white bar with no colour RAM change. Drawing and erasing are the same
  operation, which is the canvas's own idiom. This is a new helper, `InvertTextRow`, in
  `TextPrint`.
- The **prompt** lives on row 1 from column 17: `QUANTITY? ` then the digits typed so far. It is
  cleared by clearing those cells, not by reprinting (`CHPR` draws by EOR, so "print spaces" is
  not an erase).
- **Cash** on row 23 is token 119 (`CASH:` and the amount), reprinted after each purchase.
  Complaints replace the prompt text for the duration of `dn2`'s beep-and-pause and use the
  original's tokens: `QUANTITY?` (176), `CARGO?` (206), `CASH?` (197).

## 4. Buy Cargo — behaviour

Two modes, one loop.

**Selecting.** Up/Down move the bar, wrapping at both ends. Rows showing a dash can be selected;
`B` on one beeps and does nothing. `B` on a row with stock enters the prompt. Any key that
`TT102` would dispatch on this view — a screen key, a view key, F7 launch — leaves the screen and
is handed back to the dispatch so that it is performed as if pressed on any other screen. This
replaces `BAY2`'s forced f9: leaving Buy Cargo for Market Prices goes to Market Prices, not to
the inventory. Everything else is ignored.

**Typing.** Digits append, at most three, value capped at 255. Backspace removes the last digit
and beeps on an empty prompt (`OSW05`'s rule). Return with an empty prompt or a value of 0
cancels: prompt cleared, back to selecting. Escape cancels the same way. Return with a value
runs the original's three checks in the original's order — more than is available (`TT224`'s
`BCS TQ4`), no room (`tnpr`), cannot afford (`LCASH`) — and on the first failure shows the
complaint, beeps and pauses, then returns to selecting with the prompt cleared (see §8 Q3). On
success: hold and market move by the same amount in opposite directions, the row is reprinted so
the quantity column changes, the cash line is reprinted, `dn2` beeps, back to selecting.

While typing, the number row types digits — and so do F1–F6, because the key map is
many-to-one (ADR-005 §4). That cost is already accepted and is unchanged here.

## 5. Market Prices — layout and behaviour

```
row  1  LAVE MARKET PRICES
row  3  PRODUCT     UNIT    BUY   /  SELL
row  5  Food        t        3.6
row  6  Textiles    t        6.0
 ...
row 21  Alien Items t       51.2
```

- The title is the current system's name (`PrintSystemName` on the current seeds) followed by
  ` MARKET PRICES`, all caps as every title is.
- Each line is name, units and price at the same columns as `PrintMarketItem`, without the
  quantity. The price still goes through `MarketPrice` and the Alien Items zeroing is kept,
  because in the original every path that computes a price does it and this screen computes
  seventeen.
- The sell column is empty because buy and sell are the same number today. The heading stays
  so that the screen does not change shape when a sell price exists.
- Read-only: it draws and returns, exactly as `PrintMarketScreen` does now, so it works in
  flight where `TT167` sits above `TT102`'s docked split.

## 6. Input: a second seam, and why not the first

`KeySource::NextKey` returns the **character** `TRANTABLE` gives a matrix position. That cannot
carry the arrow keys: the window queues an arrow as the position of the C64 roll or pitch key it
also stands for, which translates to `X`, `S`, `<` and `>`, and the C64's own cursor positions
never enter the queue at all. Making `NextKey` return `X` for Up would push the executable's key
layout into `GameLogic`.

So the new screens read a **`ListKeySource`**:

```cpp
enum class ListKey { Up, Down, Buy, Digit, Delete, Return, Escape, Other };

struct ListKeyPress
{
  ListKey key = ListKey::Other;
  std::uint8_t character = 0; ///< the digit, for Digit
  std::uint8_t position = 0;  ///< the C64 matrix position, for Other, so the dispatch can act on it
};

class ListKeySource
{
public:
  virtual ~ListKeySource() = default;
  virtual ListKeyPress NextListKey() = 0; ///< blocks, as TT217 does
};
```

`GameShell` implements it beside `NextKey`: same blocking pump, then a mapping from the queued
position — pitch up/down to Up/Down, `B` to Buy, `0`–`9` to Digit, 127 to Delete, 13 to
Return, 27 to Escape, everything else Other with its position. The screen decides whether an
Other leaves by asking `ActionForKey` (`TT102`), which is already in `GameLogic`. Tests script
`ListKeyPress` values directly, the way `ScriptedKeys` scripts characters today.

`B` gets a binding in `KeyMap.cpp` (VK_B to matrix position 36, whose translation is `B`). It is
not a `TT102` key and not a flight key, so it is one-to-one.

## 7. Files, wiring and tests

| Where | Change |
|---|---|
| `GameLogic/CargoScreens.h/.cpp` (new) | `ListKey`, `ListKeySource`, `QuantityEditor` (the digit/backspace/return state machine as a pure step function), `BuyCargoScreen`, `MarketPricesScreen`. Both `.vcxproj` and `.filters` updated. |
| `GameLogic/TextPrint.h/.cpp` | `InvertTextRow` and `ClearTextCells`; nothing existing changes. |
| `GameLogic/MarketScreen.*`, `Market.*` | Unchanged. `BuyScreen`, `PrintMarketScreen` and `ReadNumber` keep their oracle tests and lose their executable caller. |
| `Outpost/Shell.h/.cpp` | `GameShell` implements `ListKeySource`. |
| `Outpost/KeyMap.cpp` | `B` bound. |
| `Outpost/Main.cpp` | `BuyCargo` performs the new screen and then `PressKey`s the position it returned; `MarketPrice` performs the new read-only screen. |
| `Tests/GameLogicTests/CargoScreenTests.cpp` (new) | Scripted `ListKeyPress` runs against a recording sink: bar movement and wrap; `B` on a dash beeps; digits, three-digit cap, backspace, backspace on empty beeps; Return on empty and on `0` cancel; a purchase moves hold, market and cash by the oracle-proven amounts; each of the three refusals; a dispatch key leaves with its position; the prices screen's title and seventeen lines. Plus `QuantityEditor` exhaustively over its inputs. |
| `Design/ADR/ADR-006-*.md`, `Elite-Conversion-Plan.md` §6.116, `Source-Inventory.md` | The ruling, the slice write-up, and `TT219`/`TT167` marked "ported and proven; superseded in the executable by ADR-006". |

The new tests are not oracle tests — there is no original to compare a screen the original never
had. They are behavioural, and the numbers they assert are computed by the routines the oracle
suites already prove.

`check_outpost.py` passes throughout: nothing in `GameLogic` is renamed or removed.

## 8. Open questions

1. **The ruling in §2.** Proceed now as ADR-006 with the faithful routines kept and unwired
   (recommended), proceed and delete them, or park this until phase 6?
2. **The empty column.** Keep `BUY / SELL` headings and leave SELL blank (recommended, and what
   §5 shows), or fill SELL and blank BUY?
3. **After a refused purchase.** Show the complaint, beep and pause, and return to selecting with
   the prompt cleared (recommended), or keep the prompt open with the typed digits so they can be
   edited, or re-ask from an empty prompt as `TT219` does?
4. **Purple elsewhere.** The only other purple is the commander-name entry in the disk menu
   (`MT26`, oracle-compared). Leave it (recommended), or make it white too and adjust that test?

Assumptions taken without asking, overridable: the bar wraps; the cash line is on row 23; the
heading is the picture's single row rather than token 255's two; Escape cancels the prompt; the
new Market Prices screen is also what F4 shows in flight.
