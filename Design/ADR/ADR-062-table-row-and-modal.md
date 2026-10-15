# ADR-062 — T1's last widgets: the table row and the modal

**Status:** accepted 2026-10-15, with the change that implements it, task T1 of Phase 6's piece 5 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), under the owner's ruling D97. It adds to the widgets ADR-058 put in `Outpost/DockedFrame`.

## Context

T1 is D82's shared widgets: the panel, the table row, the message strip, the key bar and the modal, and the frame's top bar. ADR-058 built all but two of them for D01:
- the table row, which D01 drew in its own code;
- the modal, which D01 has no use for.

D03, D09 and D10 use the modal. D01, D02, D03 and D10's catalogue use the row. None of D02 to D10 is built yet. The owner ruled that both widgets be built now, ahead of the screens that need them (D97).

## Decision

1. **The table row is the frame's** (`DockedFrame::RowLook`, `RowLookOf`, `Row`, `RowText`).
   - A row's look is one of four:
     - Selected, when it is the selected row, whether or not the pointer is on it;
     - Hovered, when it is under the pointer;
     - Striped, every second row of a striped table;
     - Plain otherwise.
   - `Row` fills the row's rectangle for its look; a plain row has no fill, and the table's panel shows through.
   - `RowText` is the text's colour: light amber on the selected row, the text's colour on every other.
   - The columns, their alignment and the secondary colours, such as D01's units, stay each screen's.
   - D01 now draws its rows through it.
2. **The modal is the frame's** (`DockedFrame::Modal`). It is drawn over the screen as drawn so far and leaves its content to the screen:
   - the whole screen dimmed;
   - a panel in the modal's rectangle, with a 1-unit border of full amber and 28-unit chamfers at its top-right and bottom-left corners.

   It has no title, no keys and no hit-testing of its own. D09's tabs and D10's menu are different, and each screen draws and hit-tests its own.
3. **The panel and the modal share one chamfered panel**, which takes its chamfer, fill and edge. `Panel` is that panel with ADR-058's measures, unchanged.

## How it was measured

The measures were read off the designs' pixels, D09 `docked/09-mission-briefing.png` and D10 `docked/10-disc-menu.png`.

| Measure | Value | Where |
|---|---|---|
| The modal's rectangle | 360 to 1560 across; 150 to 890 down in D10, 130 to 930 in D09 | the border's pixels |
| Border | 1 unit of `#FFB43C` | every edge, both designs |
| Chamfers | 28 units, at the top-right and the bottom-left | where the edges end: 1532 and 178 at the top-right, 388 and 861 at the bottom-left |
| Fill | `#14110D` | inside both modals, away from their content |
| The screen under it | dimmed to about 22% | D01's pixels under D10's: text `#E8BC73` to `#372C1B`, a striped row `#161411` to `#090706`, the background `#070605` to `#060403` |
| The dim, as drawn | `#050403` at 78% (`0xC7050403`) | the closest fit to all three: every channel within 1 level of the design's |
| Rows | stripe `#161411`, selected `#453416`, D01's hover `#221C14` | ADR-058's, which D03's and D10's rows match to a level |

D01 was drawn before and after the row moved into the frame, by the shell's own drawing code compiled with g++ against the portable canvas. Its canvas items, as drawn, as refusing and as bought, are identical item for item: 280, 287 and 287 items.

The modal was drawn over D01 by the same means, rasterised by the stand-in with the Inter typeface, and compared by eye with D10. `OutpostTests` draws it on WARP as `T1-Modal`, which the owner makes golden from CI's artifact (D94). `DockedMarketTests` checks each row's look and its text's colour.

g++ (`-Wall -Wextra -Wconversion -Wshadow`) and clang-tidy 18 are clean on the changed files. `CheckFormat` and `CheckProjectFiles` are clean.

| Not run here | the MSVC build and clang-tidy 22, which CI runs; `T1-Modal` on WARP, whose image CI draws |
|---|---|

## What this forecloses

A docked screen with a table draws its rows' fills with `Row`. A screen that opens over another, D03's picker excepted, draws its frame with `Modal`. D03's picker sits in the right-hand column of D03 as a panel, not over the screen, so it uses `Panel`.
