# ADR-068 — D09, a mission's briefing and debriefing: a view over D08 while the original routines wait

**Status:** accepted 2026-10-17, with the change that implements it: task T15 (D09) of Phase 6's piece 5 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It follows the owner's rulings:
- D82, the screens as drawn;
- D83, the mouse while docked;
- D101 and D105, this build's three missions in D09's layout, from the game's texts as they stand;
- D121 to D123, the tab, the supernova's question and the boxes.

## Context

The original briefs a mission from the docked status screen. `ShowMissionBriefing` draws the EMERGENCY text page, and `ShowMissionDebriefing` the TASK COMPLETE one, each with the mission's lines. Each waits for a key: Y or N for the supernova's refugees, any key otherwise. Each changes the commander as it goes: the hold emptied for the refugees, the reward paid, the masking device or the anti-ECM fitted, the title awarded.

D09, `Design/Canvas/docked/09-mission-briefing.png`, draws a modal over D08 with:
- six tabs, M1 BRIEF to M3 DEBRIEF;
- `INCOMING TRANSMISSION` and `MISSION 1 · BRIEFING`;
- the sender and a title;
- the text;
- three boxes, OBJECTIVE, LAST SEEN and REWARD;
- `SPACE · ACKNOWLEDGE`.

Its mission 1 is not this build's (D101).

## Decision

1. **The original routines stay, and D09 is a view of them**, as D08 is of the status screen (ADR-063).
   - While a briefing or a debriefing waits for its key, it publishes a `BriefingView` (`Host::ShowBriefing`), and takes it down when it returns. The view holds:
     - D08 under it, as the game stands then;
     - the mission and whether it is a briefing or a debriefing;
     - the game's text as paragraphs;
     - what the transmission waits for;
     - what a debriefing gives.
   - Nothing of the game's state, its key reads or its text page changes, so no replay, twin, picture or outcome moved.
2. **The text is the game's** (D105). `BriefingParagraphs` joins the lines the original prints a row each:
   - by a space, or by nothing after a hyphen, so `near-` and `by` read `near-by`;
   - into paragraphs at its blank lines;
   - leaving out `Press any key to continue!`, which the button stands for.

   The supernova's reply follows its question, as the original prints it below.
3. **The owner's rulings.**
   - **The tab (D121).** Only the transmission delivered now is shown, as its own tab: `M1 BRIEF`. The game keeps no record of earlier ones to step back through.
   - **The question (D122).** The supernova's briefing answers with `Y · TAKE THE REFUGEES` and `N · DECLINE` in place of `SPACE · ACKNOWLEDGE`. Its reply then asks to be acknowledged.
   - **The boxes (D123).** OBJECTIVE and LAST SEEN are not drawn: the game's texts give neither. A debriefing draws REWARD with what it gives:
     - the credits the supernova's pays, 1000.0 or 1400.0;
     - `Masking device fitted`;
     - `Anti-ECM and Archangel`.

     A briefing, and a mask ship destroyed or fled from, draw none.
4. **The shell draws it** (`Outpost/DockedBriefing`): D08 under a modal, as the drawing has it.
   - **The header.** `INCOMING TRANSMISSION`, or `TASK COMPLETE` for a debriefing as the original's frame says, with `MISSION n · BRIEFING` or `DEBRIEFING`.
   - **The words the drawing leaves open** are this ADR's, for the owner to replace as data:
     - the titles, `Supernova`, `The masking device` and `The Thargoid invasion`;
     - the senders, `THE PEOPLE OF LAVE` for the supernova and `NAVY INTELLIGENCE` for the other two.
   - **A click on a button** sends its key, Space, Y or N, as the routine waits for a key. A click on the key bar sends nothing while D09 is up: any key answers the routine, so the bar's F2 would only acknowledge.

## How it was measured

| Measure | Value |
|---|---|
| Modal | 360, 130, 1200 by 800, over D08 dimmed between the bars |
| Tab | 409, 167, 177 by 44 |
| Header | its dot at 415.5, 243.5, radius 6.5; `INCOMING TRANSMISSION` from 438 at 243; the mission right at 1511; the rule at 273.5 |
| Text | the sender at 305; the title at 349, 52; the text from 420, 39 apart, 26, at most 1100 wide |
| Boxes | REWARD 409, 730, 357 by 82; its label 22.5 down and its value 52.5 |
| Buttons | `SPACE · ACKNOWLEDGE` 1215, 836, 296 by 58; `Y · TAKE THE REFUGEES` 1155, 836, 356 by 58; `N · DECLINE` 949, 836, 190 by 58 |

The positions, sizes and colours were read from the drawing's pixels. D09 was drawn by the shell's code, compiled with g++ against the portable canvas and rasterised with the Inter typeface, then compared by eye with the drawing.

**The tests.**
- `BriefingViewTests` (3 tests) covers:
  - the paragraphs;
  - the supernova's briefing docked: its question over D08, Space no answer, Y and the reply with the refugees under it, Space to D08;
  - its debriefing's 1000.0 credits;
  - the masking device's and the invasion's debriefings.
- `DockedBriefingTests` draws `D09-Supernova-Briefing`, `D09-Invasion-Briefing` and `D09-Supernova-Debriefing` on WARP for the owner to make golden (D94). It also checks the keys the buttons send and the words. Those two checks were run here, through the same code, outside the test framework: 0 failures.

The suite passes, 178 tests under g++, on two runs in check mode, with no replay, known answer, picture or outcome changed. `CheckFormat` and `CheckProjectFiles` are clean. g++ (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`) and clang-tidy 18 are clean on the changed files.

| Not run here | the MSVC build and clang-tidy 22, which CI runs; `Application`'s clicks and drawing of D09, which only Windows compiles; D09 on WARP, whose images CI draws; the game played through a mission |
|---|---|

## What this forecloses

- The briefings are still the original's routines, its text page drawn under D09. They go with the CGA's docked text mode, after piece 5 (D76).
- Earlier transmissions cannot be read again.
- D09's titles and senders are placeholders until the owner's texts replace them.
