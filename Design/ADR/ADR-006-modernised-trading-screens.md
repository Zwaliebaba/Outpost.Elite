# ADR-006 — Modernised Trading Screens Ahead of Phase 6

**Status:** Accepted · 2026-09-05 (owner ruling on all four questions in
[Trading-Screens-Design.md](../Trading-Screens-Design.md) §8)
**Depends on:** ADR-001 (what this is an exception to), ADR-005 §4 (the key map it extends)
**Feeds:** the Buy Cargo and Market Prices screens in the executable

## Context

ADR-001 §4 defers every modernisation to phase 6 and gates phase 6 on the whole faithful build
being green. The owner asked, before that gate is met, for two screens the original never had: a
Buy Cargo screen that lists the market with a selection bar and asks for a quantity only when `B`
is pressed, and a read-only Market Prices screen with a buy column and a sell column. Both black
and white; none of the purple `gnum` types in.

ADR-005 §4's key-map amendment is the nearest precedent and does not cover this. That amendment
argued that which physical key reaches `KEYLOOK+17` is not part of what the machine computes.
These screens change what the machine computes: the canvas differs, the sequence of `TT217`
reads differs, and `TT219` and `TT167` lose their only caller.

## Decision

1. **The two screens are built now, as an owner-ruled exception to ADR-001 §4.** The exception
   is this ADR and nothing wider: every other modernisation still waits for phase 6 and its own
   decision.
2. **The faithful routines stay, proven and unwired.** `BuyScreen` (`TT219`),
   `PrintMarketScreen` (`TT167`) and `ReadNumber` (`gnum`) remain in `GameLogic` with their
   oracle tests. They are the proof that the price model and the buy arithmetic match the
   original, and restoring fidelity in the executable is two `case` labels in `Perform`.
3. **The new screens are a separate module, `CargoScreens.h/.cpp`, and they compute nothing of
   their own.** Prices are `MarketPrice`, the room check is `CargoFits`, the money is `SpendCash`
   and `TotalPrice`, and each market line is `PrintMarketItem`. What is new is the layout, the
   selection bar, the prompt and the loop around them. The file carries no `6502:` labels of its
   own because there is nothing to name.
4. **Input gets a second seam, `ListKeySource`, and the executable interprets the key.**
   `KeySource::NextKey` returns `TRANTABLE`'s character, and the arrows have none: the window
   queues an arrow as the C64 pitch key it is bound to, which types `X` or `S`. Rather than teach
   `GameLogic` that `X` means "up", `Outpost::ListKeyFor` maps the queued position to what the
   screen reads — Up, Down, Buy, Digit, Delete, Return, Escape, Other — and `Other` and `Digit`
   carry the matrix position so the screen can ask `TT102` whether the key reaches a screen.
5. **Leaving the screen goes to the key that was pressed, not to a forced f9.** `BAY2` forces the
   inventory; the new screen returns the position of the screen key that left it and the docked
   loop presses it on its next pass. Pressed by the loop rather than from inside `Perform` so that
   `1` on the buy screen redraws it without nesting.
6. **`B` and `0` are bound.** `B` to position 36 and `0` to position 29; neither is a `TT102` key
   or a flight key, so both are one-to-one. `0` was unbound before and the prompt needs it for
   ten and up.
7. **The sell column is empty and the heading keeps it.** This game has one price a good —
   selling goes through the same `MarketPrice` — so the SELL cells print nothing until something
   makes the two differ. The Market Prices title names the system from `QQ2`, the seeds `DOENTRY`
   copies on arrival, and prints the words verbatim rather than through token 167, which carries a
   system name of its own from the value tokens.
8. **After a refused purchase the screen returns to the list.** The complaint is `TT219`'s own
   token — `QUANTITY?`, `CARGO?` or `CASH?` — shown where the prompt was for `dn2`'s beep and
   pause, then cleared; the bar stays on the item. The original's retry loop is not reproduced.
9. **Only the new screens are black and white.** The commander-name entry in the disk menu keeps
   `MT26`'s purple and its oracle comparison.

## What this does not decide

The Sell Cargo screen, the Inventory screen and the equipment shop are untouched and remain
`TT210`, `TT213` and `EQSHP`. The many-to-one key map (F1–F6 typing digits while the prompt is
open) is ADR-005 §4's cost and is unchanged.

## Consequences

- The Buy Cargo and Market Prices screens are not oracle-comparable and never will be. Their
  tests (`CargoScreenTests.cpp`) are behavioural: they read the canvas back through the font and
  check hold, market and cash against the routines the oracle suites prove.
- `Source-Inventory.md` marks `tt219` and `tt167` as ported and proven but superseded in the
  executable by this ADR, so the coverage ledger still says what is ported.
- A future "faithful mode" switch, if phase 6 wants one, has both implementations to choose
  between and nothing to rewrite.
