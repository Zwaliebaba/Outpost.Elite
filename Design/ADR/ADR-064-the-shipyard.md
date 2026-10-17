# ADR-064 — D02 and D03, the shipyard and its mount picker: its own state and rules, a view the shell draws, clicks the shell hit-tests

**Status:** accepted 2026-10-16, with the change that implements it: tasks T4 (the equipment's logic and the mount picker), T8 (D02) and T9 (D03) of Phase 6's piece 5 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It follows the owner's rulings:
- D82, the screens as drawn;
- D83, the mouse while docked;
- D96, F3;
- D103 and D104, the picker's title and keys;
- D106 to D108, the trade-in, a mount that already carries the laser, and the tests.

## Context

The original's Equip Ship is a text menu:
- It reads a key every 100 timer ticks (`PollMenuKey`).
- B on a laser pays first, then asks for a mount with the arrow keys and Space. It refuses a mount that carries a laser, and nothing cancels it.
- S on a laser pays back first, then asks for the mount the laser comes off.
- ADR-043 made its rules functions with no screen code: `EquipmentRowsOnSale`, `EquipmentPriceTenths`, `FuelPriceTenths`, `RefuseToFit`, `RefuseToRemove`, `CountAfterFitting` and the mounts' rules.

D02, `Design/Canvas/docked/02-equip-ship-tl6.png`, and D03, `03-equip-ship-tl10-laser-mount.png`, replace it:
- the list at the system's tech level, with buy, sell and what is fitted;
- the Selected panel, with [B] BUY and [S] SELL;
- the mount picker, opened by B or S on a laser.

The picker:
- numbers the mounts 1 to 4 (D104);
- has ESC CANCEL;
- trades in a laser on the chosen mount at its sell price.

D11's Equipment column words the strips. It refuses a laser only when every mount already carries it: `FOUR LASERS — EVERY MOUNT ALREADY CARRIES A BEAM LASER`.

## Decision

1. **The screen's logic is values in, values out (T4)** (`GameLogic/EquipScreen.h`).
   - `EquipGoods` is the shipyard and the ship:
     - the rows on sale and every row's price at this system;
     - ADR-043's `Fitting`, the mounts and the credits.
   - `EquipScreen` is D02's own state: the row, the picker, and the strip the last action left. It is in no digest.
   - `StepEquip` applies an action:
     - ↑ and ↓ stop at the list's ends, and a click selects a row. A move closes the picker.
     - B and S on anything but a laser trade through ADR-043's rules, unchanged: fuel fills the tank at `FuelPriceTenths`, and one more or one fewer of anything else, sold back at `ResaleTenths` of its price.
     - B and S on a laser open the picker (D103) and trade nothing yet. A mount then trades, or says why not and leaves the picker open. Cancel closes it.
   - Buying a laser:
     - It is refused when every mount already carries that laser (D82, D11), and a mining laser without fuel scoops (ADR-043).
     - On the mount picked, a laser of another type is traded in at its sell price. The credits need cover only the difference (D106), and the strip's "needed" is that difference.
     - A mount carrying the same laser is refused with its strip (D107).
   - Selling a laser takes it off a mount that carries it, and is refused on any other mount.
   - The payment is made when the mount is picked, so Cancel trades nothing.
   - ADR-043's `RefuseToFit` keeps the original's four-laser rule for its known answers. The screen asks it about everything but a laser.
2. **The keys are the footer's** (`EquipKey`):
   - ↑ ↓, B and S;
   - with the picker open, 1 to 4 for front, left, right and rear, the original's fore, left, right and aft (D104, `MOUNT_OF_PICKER_KEY`), and Esc to cancel.

   With the picker closed, Esc and every F-key but F3 close the screen, as the docked host expects (D96).
3. **The view is plain values for the shell (R9)** (`GameLogic/EquipView.h`).
   - `EquipView` holds:
     - the frame's system and credits, the tech level as shown (one more than stored), and the rows on sale;
     - each row's prices, count and laser mounts, and the fuel in tenths of a light year;
     - the selected row, and whether its buy and sell would happen;
     - the picker: each mount's laser and whether picking it would happen;
     - the strip, `EquipMessage`, which the shell words.
   - The `Host` keeps the latest view while the screen runs (`ShowEquip`, `HideEquip`, `ShownEquip`) and queues the shell's `EquipClick`s.
4. **The routine is `ShowEquipScreen`, which F3 opens docked.**
   - Each tick it acts on the clicks and one key, stores a trade (`FormatCredits` too), and publishes the view.
   - It draws nothing on the CGA. Under D02 the CGA still shows the screen before it, as under D01.
5. **The original's Equip Ship goes**, as Buy and Sell Cargo went with D01 (ADR-058):
   - `ShowEquipShipScreen`, `RunEquipShipMenu`, the two mount choosers and their boxes;
   - the menus' cursor (`StartMenu`, `MoveMenuCursorUp`, `MoveMenuCursorDown`, `SteerMenuCursor`, `PollMenuKey`);
   - `PayForEquipmentItem`, the resale column's routines and `ComputeResalePrice`;
   - `ToggleMenuRowHighlight`, `ClearDockedMessageLine` and `PrintCreditsOnMessageLine`;
   - the screen's texts and tables.

   `SelectLaserType` stays, for the status screen. The members of the state it wrote (`menuSelectedRow`, `menuRowCount`, its prices in `screenPrices`, `selectedLaserType`) stay in the state, and the shipyard writes them no longer.
6. **The shell draws them (T8, T9)** (`Outpost/DockedEquip`), in the docked frame with F3 lit.
   - The list:
     - It shares D01's table and its columns: buy, sell, and fitted (`7.0 LY`, `3 / 4`, a laser's mounts in D104's order, `FITTED`, or a dash).
     - Its rows are 64 units apart, or closer so that the 14 take 728 units, as D03 draws them 52 apart.
   - The Selected panel and its two buttons.
   - The picker, over the right-hand column:
     - its title broken to fit beside ESC CANCEL;
     - the four mounts round the ship's dashed place, a mount that would be refused dimmed;
     - the trade-in's note when buying.
   - The strip sits under the Selected panel, or under the picker while it is open. D11's words are split at the dash, as D01's are.
   - D01's button and edge moved into `DockedFrame` (`Button`, `Edge`). D01 draws the same items with them.
   - `DockedNames` gives the list's, the mounts' and the laser types' names.

   `FITTED`, for an item fitted that is not counted, is this ADR's: the drawings show none fitted.

## How it was measured

| Measure | Value |
|---|---|
| List | D01's table, 60 across, 110 down, 1281 by 852; rows from 175, 81 to 1321; 64 apart for 8 rows (D02), 52 for 14 (D03) |
| Columns | name at 97; buy, sell and fitted right at 884, 1084 and 1305; row text 25 at 64 apart and 23.5 at 52, from the capitals' 18 and 17 units |
| Selected panel | 1380 to 1859, 110 to 432; [B] BUY and [S] SELL 200 by 64 at 1413 and 1627, 340 down |
| Picker | 1380 to 1859, 453 to 803, its edge full amber; mounts 138 by 64: front at 1551, 537; left at 1405 and right at 1697, 609; rear at 1551, 681; ESC CANCEL 1730 to 1834, 482 to 513 |
| Footer | two lines, 1380 across, 921 and 947 down |

The positions and colours were read from the drawings' pixels. The type sizes come from the capitals' heights at D01's ratio.

D02 and D03 were drawn by the shell's code, compiled with g++ against the portable canvas and rasterised with the Inter typeface, then compared by eye with the drawings:
- D02 as drawn;
- D03 buying;
- the picker selling, with a strip;
- a laser fitted with a trade-in;
- the longest strip.

D01 was drawn by the same means before and after the button moved: its canvas items are identical.

**The tests (D108).** `TwinEquipmentMenu` and `TwinLaserMounts` tested the original's menu itself, and go with their known answers, pictures and outcomes. `EquipScreenTests` replaces them (11 tests):
- each row's buy and sell;
- every refusal of D11's column with its figures;
- the picker by its keys: a free mount, a trade-in covered by the difference and refused a tenth short, the same laser refused, Cancel, and selling;
- the view;
- the keys;
- the screen as the game runs it, by keys and by clicks.

`DockedEquipTests` draws D02 and D03 on WARP for the owner to make golden (D94): `D02-Equip`, `D03-Mount-Picker`, `D03-Sell-Picker` and `D02-Fitted`. It also checks the clicks, the Fitted column and the strips' words. Those three checks were run here, through the same code, outside the test framework.

**The re-recording.** Each test that buys through the shipyard was dumped member by member on main and after the change, and compared at each digest by name:
- `buy-every-equipment`, `docked-screens`, `invasion-mission`, `save-and-load` and `supernova-mission`;
- `TwinInvasion` and `TwinSupernovaDeclined`.

What differs is the original menu's working state, which it no longer writes:
- its price table and rows;
- the text attribute;
- the laser type and mount it selected;
- the steering keys it read;
- and the key buffer.

At `buy-every-equipment`'s last screen, the timer differs too. No outcome moved: every progress outcome was held in a dump run, and no outcome line changed. The digests were then re-recorded from the run's own values and the pictures with the guard, where the shipyard's points show what the CGA shows under D02. The suite passes, 169 tests under g++, on two runs in check mode.

**What the timing changed, and how it was kept.** The original menu read a key every 100 ticks, D02 every tick, so the key that leaves the screen now takes effect sooner. The moment each test left the shipyard was measured in both trees:
- In `invasion-mission`, `supernova-mission` and `TwinSupernovaDeclined`, F1 launched 50 ms earlier, and `TwinInvasion`'s F5 came 50 ms earlier.
- In `supernova-mission`, that moved the galactic jump's random arrival from system 211 to 179.

Those four now press the key 50 ms later and shorten the wait after it by as much, so the launch, its random state and every key after it fall where they were recorded. Each launch was measured again, identical in time and random state.

In `buy-every-equipment`, the last key left the screen 650 ms sooner, where the old menu's polling had lagged behind its keys. That is left: only the status screen's digest follows it.

The key changes:
- In `buy-every-equipment`, the picker's keys replace the arrows and Space: 1 for the sale off the front mount, then 2, 4, 3 and 1 for left, rear, right and front.
- Everywhere else ↓ and B, which D02 keeps.

`CheckFormat` and `CheckProjectFiles` are clean. g++ (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`) and clang-tidy 18 are clean on the changed files.

| Not run here | the MSVC build and clang-tidy 22, which CI runs; D02 and D03 on WARP, whose images CI draws; the game played with the screens drawn |
|---|---|

## What this forecloses

A laser is fitted only through the picker: the original's rule that refuses an occupied mount is the screen's no longer, though `RefuseToFitMount` keeps it for its known answers. The remaining docked screens follow D01's and D02's shape. A screen that reads keys faster than the routine it replaces moves the moment it closes. Where a test flies on from it, the test's keys are moved to keep the recorded moment, and the change is measured, not assumed.
