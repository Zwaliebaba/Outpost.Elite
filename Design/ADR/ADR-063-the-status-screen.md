# ADR-063 — D08, the commander's status: a view the shell draws, over the original's routine

**Status:** accepted 2026-10-16, with the change that implements it, task T14 of Phase 6's piece 5 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), under the owner's rulings D82 (the screen as drawn), D75 (the cargo on it) and D96 (F6, and F10 once D08 takes in the inventory).

## Context

D08, `Design/Canvas/docked/08-commander-status.png`, is the original's status screen and its inventory on one screen. It has three panels:
- the commander: the title and the name, where the ship is and its hyperspace target, the fuel, the cash, the legal status and the rating;
- the equipment fitted, a laser with its mounts;
- the cargo, with the hold's tonnes.

D08 differs from D01 in two ways:
- The original's status screen has no rules of its own to take out. It prints what the state holds.
- Docked, the status screen is where every replay and twin lands: at the start of a game, back from a disk request, and after every docking. It also shows the mission briefings and debriefings first, which D09 replaces later. Replacing its routine would move the picture and the state at nearly every point the corpus records.

## Decision

1. **The original's status routine stays, and D08 is a view of the state it prints** (`GameLogic/StatusView.h`, `StatusViewOf`).
   - `StatusView` is plain values (R9); the shell words them:
     - the title and the name, without the original's padding;
     - the present system and the target, or witch space;
     - the fuel in tenths of a light year and the credits;
     - the legal status, `LegalStanding`;
     - the rating, 0 to 8;
     - each item fitted, `FittedItem`, with a laser's mounts as a bit each;
     - the 17 commodities held, the refugees, and the hold's tonnes in use and in all.
   - `LegalStandingOf` and `RatingOf` are the original's thresholds, and the original's routine now prints through them.
   - A laser's mounts are the mounts whose type bits are that laser's, as the original's routine lists them.
2. **Docked, the status routine publishes the view while it waits for its key** (`Host::ShowStatus`, `HideStatus`, `ShownStatus`).
   - It publishes after any briefing or debriefing it shows first, which stay the original's until D09.
   - It takes the view down when the screen closes.
   - In flight it publishes none: F9's screen stays the original's.
   - The routine draws on the CGA as before, so no replay's picture, digest or outcome moves.
3. **The shell draws D08 from the view** (`Outpost/DockedStatus`):
   - three panels, read off the drawing's pixels, in the docked frame with F6 lit;
   - the commander panel's six facts, each a label over its value;
   - the legal status in green when clean, light amber for an offender and light red for a fugitive;
   - the equipment with amber bullets, the missiles with their count, and a laser's mounts named front, left, right and rear in D03's order (D104);
   - the cargo held, the refugees last, with the hold's tonnes against the bay's.

   While the view is shown, the shell draws it in place of the CGA picture. The key bar's keys are pressed by a click.
4. **The commodities' and the equipment's names are the shell's, once** (`Outpost/DockedNames`): D01's table moved there, and D08 reads the same.

## How it was measured

| Measure | Value |
|---|---|
| Panels | 60, 680 and 1280 across, 600, 580 and 580 wide, 110 to 960 down. The commander's has its top-right and bottom-left chamfers; the equipment's none; the cargo's its top-right one. |
| Rules | 45 units in from each panel's sides: 261.5 down in the commander's, 189.5 in the others |
| Type | Each line's capitals measured and set at D01's ratio of capital height to size (0.71 to 0.73): labels 15 (D01's), the name 62, values 30, headings 17.5, rows 25 |
| Rows | Labels 81 apart from 296; values 30 below their labels; list rows 46 apart from 221.5 |
| Bullets | 10-unit amber squares, 26 units left of their text |
| Colours | `#C9BCA6` labels, `#FFCF80` the name, the cash, the hold's tonnes and the cargo's amounts, `#F3E6CF` values, `#9FE07A` Clean, `#FFB43C` headings, `#64491D` rules |

The colours and positions were read from the drawing's pixels; the type sizes from the capitals' heights.

D08 was drawn by the shell's code, compiled with g++ against the portable canvas, rasterised by the stand-in with the Inter typeface, and compared by eye with `docked/08`. It was drawn as drawn and for a fugitive with a laser on two mounts and refugees aboard.

D01, whose names moved to `DockedNames`, was drawn by the same means before and after. Its canvas items are identical.

The tests:
- `StatusViewTests` covers:
  - the rating's thresholds and the legal status's;
  - a new game's view;
  - lasers on several mounts, the extension, cargo and refugees;
  - the view shown docked, a new one each time the screen opens, taken down for another screen and absent in flight.
- `DockedStatusTests` draws D08 on WARP as `D08-Status` and `D08-Status-Fugitive`, for the owner to make golden (D94), and checks the equipment's words.

The suite passes, 160 tests under g++, on two runs in check mode. No replay or twin moved: the original's routine is unchanged in what it prints and writes. g++ (`-Wall -Wextra -Wconversion -Wshadow`) and clang-tidy 18 are clean on the changed files, and `CheckFormat` and `CheckProjectFiles` are clean.

| Not run here | the MSVC build and clang-tidy 22, which CI runs; D08 on WARP, whose images CI draws; the game played with the screen drawn |
|---|---|

## What this forecloses

The docked screens whose original routines stand behind them follow this shape until those routines go (D76): the routine keeps the state and the CGA as they were, and publishes a view the shell draws. When the original's status routine goes, `StatusViewOf` is what the new screen is built from. D09 replaces the briefings and debriefings the routine still shows first.
