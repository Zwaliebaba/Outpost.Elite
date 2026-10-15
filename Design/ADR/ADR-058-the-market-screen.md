# ADR-058 — D01, the market: its own state and rules, a view the shell draws, clicks the shell hit-tests

**Status:** accepted 2026-10-14, with the change that implements it, task T7 of Phase 6's piece 5 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), with T3, the market's logic, and the parts of T1 and T2 that D01 needs. Under the owner's rulings D82 (the screen as drawn), D83 (the mouse on while docked), D92 (how it lands) and D93 (what MAX does).

## Context

1987 trades on three text screens: Sell Cargo (F2), Buy Cargo (F3) and Market Prices (F8). Each asks for a typed quantity, and refuses in one of nine short messages. ADR-043 made the rules functions with no screen code: `PricesOf`, `RefuseToBuy`, `RefuseToSell`, `CargoBought` and `CargoSold`. The 1987 screens called them, so the corpus held them while those screens stood.

D82 replaces all three with one screen, D01, `Design/Canvas/docked/01-market.png`:

- all 17 commodities, with their prices, stock and hold;
- the selected row, with a quantity set by −, + and MAX;
- [B] BUY and [S] SELL;
- the hold's bar;
- a message strip whose figures say why (D11's Market column).

The mouse is on while docked (D83). Nothing in the game or the shell drew a docked screen except as CGA text, and the shell had no mouse.

## Decision

1. **The screen's logic is values in, values out (T3).** It lives in `GameLogic/MarketScreen.h`.
   - `MarketGoods` is the market and the hold.
   - `MarketScreen` holds D01's own state: the row, the quantity and the strip. 1987 has no counterpart for it, so it lives in the screen's routine, not in `GameData`, and is in no digest.
   - `StepMarket` applies an action:
     - ↑ and ↓ stop at the ends;
     - a new row starts at a quantity of 1;
     - − and + keep the quantity between 1 and 255;
     - MAX is D93's rule (`MostToBuy`, then `MostToSell`, then 1);
     - B and S trade through ADR-043's rules, unchanged, and leave the lot to store.
   - A refusal leaves its strip with its figures (`RefusalMessage`), and any other action clears the strip.
   - `MarketKey` maps the footer's keys: ↑ ↓, + and − on the main keys and the keypad, B and S. The footer names no key for MAX, so there is none, and a click reaches it.
2. **The view is plain values for the shell (R9).** `GameLogic/MarketView.h` holds:
   - `MarketView`: the system's name, Coriolis or Dodo, the credits, each commodity's two prices, stock and hold, the row, the quantity, whether the trade at that quantity would happen (a lit or dimmed button), and the hold's tonnes;
   - `MarketMessage`: the strip's kind and figures. The shell words it, so the game holds no presentation strings.
   - The `Host` keeps the latest view while the screen runs (`ShowMarket`, `HideMarket`, `ShownMarket`), and the shell draws it in place of the CGA picture.
3. **Clicks are hit-tested by the shell and acted on by the game.**
   - The shell owns the layout in design units, so it knows which control a click lands on. It tells the game which one (`MarketClick`) through `Host::Click`.
   - The screen takes the clicks queued at each tick, with a key, and acts on them as on the key that does the same.
   - A click while the screen is not shown is dropped, and the queue empties when it closes. Hover is the shell's own: it draws a highlight and tells the game nothing.
4. **The routine is `ShowMarketScreen`.**
   - On entry it does what Buy Cargo did: it computes the prices (`ComputeMarketPrices`), starts the market's random state, and rolls the stock the first time after an arrival.
   - Then, each tick, it acts on the clicks and one key, stores a trade (`FormatCredits` too), and publishes the view.
   - F2, F3 and F8 keep it open. Esc and every other F-key close it, as the docked dispatcher expects.
   - While docked, F2, F3 and F8 all open it until the new key bar is complete (D92). F8 in flight is still 1987's Market Prices.
5. **The 1987 Sell Cargo and Buy Cargo screens go.** With them go their trade menu, the typed quantity (`ReadQuantity`, `ParseQuantity`), their messages and their tables. Market Prices stays, for flight. `DockedState::tradeScreenIsBuy` and the menus' quantity text stay in the state, which they are part of, and are written no longer.

6. **The shell draws it (T1, T2's mouse, T7).**
   - `Outpost/DockedFrame` is the common frame and its widgets, read off `docked/`: the background and its 48-unit grid, the top bar with where the ship is and the credits, the title on its tab between rules measured by DirectWrite, the key bar with the open screen's key lit, the 35% amber panel with its 24-unit chamfers, and the message strip.
   - `Outpost/DockedMarket` draws D01 into it:
     - the table, a row lit when selected and shaded when under the pointer;
     - the Selected panel;
     - the quantity's controls, lit under the pointer;
     - [B] BUY lit as the first action, as drawn, unless the pointer is on [S] SELL;
     - a button off when its trade at the quantity would be refused;
     - the hold's bar of 9-unit segments with its glow;
     - the footer.
   - The strip, which `01-market.png` does not place, sits in the space under the hold's panel. D11's one-line words are split at the dash into a headline and the figures under it, as the strip is narrower there. Strips not drawn yet (a sale, nothing in stock or held, a precious commodity's limit) follow D11's form.
   - `Engine::Window` reports the mouse's moves, left button and leaving. The shell maps them into design units by `PlaceDesign`'s placement, tracks the pointer for hover, and sends a click either to the game (`Host::Click`) or to the key bar.
   - A key on the bar is pressed as a key. While D02 to D08 are 1987's (D92), EQUIP sends F4, GALAXY F5, LOCAL F6 and STATUS F9, the keys 1987 shows those screens on.
   - While a market view is shown, the shell draws it in place of the CGA picture. `OutpostTests` draws it on WARP against its golden images (ADR-059).

## What moved, and how it was measured

Following D92, `trading.replay` and three twins go: `TwinCargoBuy`, `TwinCargoSell` and `TwinMarketPrices`. They take their known answers, pictures and outcomes with them. Four more were re-recorded. Each was dumped member by member before and after the change (`OUTPOST_ELITE_DUMP_STATE`), the dumps were compared, and the re-base was run from the dumps after (`OUTPOST_ELITE_REBASE_DIGESTS`, then `OUTPOST_ELITE_RECORD_GUARD`):

| Test | What changed in it | What moved |
|---|---|---|
| `docked-screens.replay` | F2, F3 and F8 removed | Every later digest. The members that differ are only those the three screens used to set: the stock rolled and the market's random state, the prices and their indices, the trade screen's flag and the typed quantity's text, the key buffer, and the 3 s less of time. No picture and no outcome moved. |
| `TwinDockedCargo` | its F2 and F3 visits removed | The same members. Its answers, pictures and outcomes are the old ones from its third, two ordinals earlier. |
| `TwinScoop` | buys its 11 t of food and 7 t of textiles on D01, by + and B | Its credits and cargo are as recorded at every answer. The members that differ are the 1987 menu's working members (the row, the attributes, the cursor's keys and the typed text) and the key buffer. Its cargo digest's picture is the status screen, which the CGA still shows under D01. |
| `TwinBriefing` | none: its F8 now opens D01 | F8 rolls the stock and starts the market's random state, where Market Prices only formatted prices. No picture and no outcome moved. |

The suite passes, 152 tests under g++, on two runs in check mode. That counts the seven new tests of `MarketTests.cpp`:

- MAX's bounds;
- the quantity and the row kept in range;
- a trade's lot and strip;
- every refusal of D11's Market column with its figures;
- the view's lit and dimmed trades;
- the keys;
- the screen as the game runs it, opened by F2, traded by keys and by a click, kept open by F3 and F8, closed by F9, its stock not rolled twice.

`CheckFormat` and `CheckProjectFiles` are clean.

| Not run here | the MSVC build and clang-tidy 22, which CI runs; the game played with the screen drawn, which the owner does; the screen on WARP, whose images CI draws and the owner makes golden (ADR-059) |
|---|---|

## What this forecloses

The other docked screens follow the same shape: their logic as values, a view the shell draws, and clicks named by control. A docked screen's own state, a row or a quantity, is not added to `GameData` while its 1987 counterpart's members are still there; when the 1987 docked routines go (D76), what is left of them is ruled with D73.

The flight F8's Market Prices is now held only by `flight-screens.replay`. A system name with a space in it, which `TwinMarketPrices` covered, is not covered until D01 replaces that screen in flight too.
