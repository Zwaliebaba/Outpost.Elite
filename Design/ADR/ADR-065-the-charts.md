# ADR-065 — D04 to D07, the charts: the cursor by the mouse, a view the shell draws, clicks in the chart's units

**Status:** accepted 2026-10-17, with the change that implements it: tasks T5 (the charts' logic) and T10 to T13 (D04 to D07) of Phase 6's piece 5 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It follows the owner's rulings:
- D82, the screens as drawn;
- D83, the mouse while docked;
- D96, the key bar's keys;
- D98 to D100, Space, a click, and the scales;
- D109 to D111, the cursor by click and drag, the flight's keys and the bar in flight, and the replays' click step;
- D112 to D115, F7 in flight, the badge at the current system, the countdown's banner, and `hyperspace-and-fight`'s fight.

## Context

The original's two charts are CGA screens of their own:
- **The galactic chart** (F5 in flight, the original's) shows 256 dots.
- **The short-range chart** (F6) shows the systems near the current one, with labels placed round each other.

Both are steered by the keyboard's steering, the same routine that rolls and pitches the ship. Each closes on Esc or another F-key, selecting the system nearest the cursor as the hyperspace target, unless the countdown runs. The original's Find (F) reads a name with `ReadTextLine`, and D marks the nearest system and its distance. Data on System is a screen of its own (F7). D96 left the original's charts behind F4 and F5 docked, with F7's Data on System, until the charts took it in.

D04 to D07 (`Design/Canvas/docked/04-…` to `07-…`) replace them:
- the chart in a panel, with the current system at the centre of the fuel ring;
- the cursor, and the nearest system bracketed;
- the legend and Find;
- Data on System as the side panel;
- in flight, the countdown's lock.

D83 moves the cursor by the mouse alone, and D109 by a click or a drag, never by hovering.

## Decision

1. **The chart's own logic is values (T5)** (`GameLogic/ChartScreen.h`).
   - `ChartKey` reads the footer's keys: Space and keypad 5 recentre (D98), D marks the nearest system, F opens Find. In Find, it reads the characters (`TypedCharacter`, which `ReadTextLine` now shares), Backspace, Enter and Esc.
   - `StepFind` keeps what is typed, at most eight letters.
   - The original's rules stay the game's, as functions with no drawing (`Galaxy.h`):
     - `EnterChart` and `LeaveChart`: what the chart did on opening and as a key closed it, the selection at the cursor included;
     - `RecenterChartCursor` and `PlaceChartCursor`;
     - `NoteNearestSystem`, which was `ShowNearestSystemDistance`;
     - `FindTypedSystem`, which was `FindSystemByName` without its prompt.
   - The jump's range is `WithinJumpRange`, which `PressHyperspace` shares through `FuelReachTenths`.
2. **The view is plain values (R9)** (`GameLogic/ChartView.h`). `ChartViewOf` computes it on a copy of the game's data, so nothing of the game moves while the chart is only looked at. It holds:
   - the stars in the chart's units, with names on the short-range chart;
   - the current system and the fuel's reach, as tenths and as the ring's radius;
   - the cursor;
   - the target: the system nearest the cursor, or, while the countdown runs, the one latched for the jump. Each comes with its facts, as Data on System prints them, its place, whether the jump would go, and how far short it is.
   - Find's state and name.
3. **The routine is `ShowChartScreen`**, which F4 and F5 open docked and F5 and F6 in flight.
   - Each tick it moves the cursor to the clicks the shell sent (`Host::Click(ChartClick)`), reads one key, and publishes the view when it changed.
   - The key that closes it selects as the original's did.
   - It draws nothing on the CGA but the chart's screen layout (`DrawChartFrame`), which flight reads.
   - In flight it blocks the flight loop, as the original's chart did.
4. **The original's charts go**, with their drawing, labels, items, prompts and texts. `GalaxyState` keeps the members they wrote, `chartItems` and `pendingChartLabels` among them, and nothing writes them. F7 does nothing docked (D96) or in flight (D112): the side panel is Data on System. The original's Data on System goes with its working code, texts and constants; `GalaxyState` keeps the fields it wrote, and nothing writes them.
5. **The shell draws them (T10 to T13)** (`Outpost/DockedChart`), in the docked frame with F4 or F5 lit:
   - the header and legend;
   - the fuel ring, filled and dashed, and the stars, named on the short-range chart;
   - the line from the current system to the target, the bracket and its label, and the cursor's cross;
   - Find's box, with its caret, or `NOT ON THIS CHART`;
   - the footer (D83, D98): `CLICK A STAR select it`, `F find by name`, `D distance`, `5 / SPACE recentre on current system`;
   - the side panel: the distance's bar against 10.0 light years, with the fuel's reach marked, the badge, the facts and the description.

   A target out of range is red, with how far short it is (D05).
   - **In flight** (D07, D110):
     - The top bar says `IN FLIGHT · LAVE SYSTEM`, with `· HYPERSPACE COUNTDOWN` in light red while it runs, and the banner `HYPERSPACE 7 TARGET LOCKED` stands beside the legend.
     - The key bar reads `F1 FRONT`, with ESC DISC and F3 EQUIP off.
     - A click on the bar sends the flight's own key for the screen the bar names: F1, F8, F5, F6 and F9.
   - **The mouse** (D109): a press on the chart sends its point, and a drag sends each new point under it. `Engine::Window` now reports the button's release and holds the mouse's capture while it is down. Hovering moves nothing of the game's; the cursor's cross is drawn at the pointer while it is on the chart, and at the cursor otherwise (D116, amended 2026-10-17).
   - **The words the drawings leave open:**
     - `CURRENT SYSTEM` is the badge when the target is the system the ship is at (D113).
     - `WITCH SPACE` takes the system's name in witch space, this ADR's.
6. **The replays click (D111).** `click X Y` is a replay step: a click on the chart open, in its units, as the shell sends it. Replays that steered the cursor with held arrows now click where the old cursor stopped.

## How it was measured

| Measure | Value |
|---|---|
| Chart panel | 60, 110, 1281 by 852, as D01's table; header at 85, 137.5; footer rule at 914.5, its text at 938 |
| Galactic chart (D100) | five units to a galaxy unit, both ways: the drawn ring's radius of 90 is 7.0 light years, 17.5 units, so 5.1, and 256 at 5 fills the panel's 1280 across; rows from 222, centred in the 743 between the header and the footer |
| Short-range chart (D100) | about 800, 537, the drawn ring's middle; 340 / 61.25 = 5.55 to a chart unit, the ring's 340 being 7.0 light years at 3.5 chart units to a galaxy unit |
| Legend | 819, 131, 497 by 121; rows at 157.5, 190.5 and 224 |
| Side panel | 1380, 110, 479 by 850; name at 184; distance's bar 1415 to 1825 at 292, 10.0 light years long; badge 315 to 343, right at 1825; facts from 390, 43 apart, values at 1577; the description's last line at 917, 30 apart |
| Find | its label at 85, 871.5; box 242, 850, 259 by 43; its strip from 516 |

The positions, sizes and colours were read from the drawings' pixels. Two findings shaped the scales:
- **D04's stars are illustrative.** Of 228 dots found in it, 14 fall within 2.5 units of the first galaxy's systems under the best fit of scale and offset. D04's scale is therefore taken from its ring.
- **D06's systems are the real ones, but anisotropic.** They sit 17.3 units to a galaxy unit across and 20.7 down, while its ring is a circle of 340. The chart keeps the ring true to the fuel range, one scale both ways, as D100 rules. Each system lands within about 30 units of where D06 draws it.

D04 to D07 were drawn by the shell's code, compiled with g++ against the portable canvas and rasterised with the Inter typeface, then compared by eye with the drawings. Two corrections came of it:
- The target's own name is no longer drawn under its label.
- The countdown's banner moved clear of the header. D07 draws it over the legend; here it ends before the legend (D114).

The view's sizes and the text's widths are the renderer's measure on Windows, so the WARP images are the check.

**The tests.**
- `ChartScreenTests` (5 tests) covers:
  - the keys and Find's typing;
  - the galactic chart docked: clicks, the target and its range, the shortfall, a click held to the rows, Space, Esc's selection, and a click with no chart dropped;
  - the short-range chart: Find found, not on the chart and cleared by a click, D, and F1's selection;
  - the lock in flight.
- `ReplayTests` parses the click step and refuses its mistakes.
- `DockedKeysTests` has F7 doing nothing docked, and closing no screen in flight.
- `DockedChartTests` draws `D04-Galactic-Chart`, `D05-Find-Out-Of-Range`, `D06-Short-Range-Chart` and `D07-In-Flight-Locked` on WARP for the owner to make golden (D94). It also checks a click's point, the flight's keys and the panel's words. Those three checks were run here, through the same code, outside the test framework: 0 failures.

**What retires (D108's way).**
- `GalacticChartAgreesOnEveryKey` and `ShortRangeChartAgreesOnEveryKey` tested the original's chart keys, and go with their known answers, pictures and outcomes.
- `CrowdedShortRangeChartsStayInTheirBuffers` tested the label buffers no chart writes now.
- `docked-screens`' `data-on-riedquat` digest goes with docked F7.
- `SystemDataScreenAgreesForTwoDigitTechLevels` goes with flight F7 (D112): the two-digit tech level is `ChartScreenTests`' Leesti, as the side panel shows it.
- `flight-screens` presses F7 on the short-range chart, which stays, and its system-data shot goes. Compared by position with the run before, its digest points differ only in the data screen's working fields: no time, random state or outcome moved.

**What is rewritten.**
- `ReadTextLineAgreesWhileEditing` types at the disc menu's Load Commander.

**The re-recording.** Every replay and twin that opens a chart was dumped member by member on main and after the change, and compared at each digest by name. What differs is:
- the original chart's working state: its items, labels, drawing buffer and seeds;
- the keys the old chart left in the buffer;
- the keyboard state its steering left.

The digests were re-recorded from the run's own values, and the pictures and outcomes with the guard. The suite passes, 170 tests under g++, on two runs in check mode.

**What the timing changed, and how it was kept.**
- Find with F then Enter ran through the original's prompt, after which the old chart read its next key a frame later. The combat replays' F1 therefore launched 100 ms after the step that pressed it, at 6300. They now press it 99 ms later and wait as much less, and launch at 6300 with the same random state. Their outcomes and their fights are unchanged.
- `hyperspace-and-fight` and `TwinInvasion` close the chart at the moment recorded.

**What did not hold.** The original's chart moved its cursor through the flight's keyboard steering, so a held arrow left `keyboardRollRate` at −9 in `hyperspace-and-fight` and +12 in `TwinInvasion` as F1 launched, and the ship left the station rolling.
- A click leaves no such roll, and no key reproduces it.
- `TwinInvasion`'s progress outcomes held.
- `hyperspace-and-fight`'s scripted fight now destroys one pirate, as it did before ADR-046: `kills` 0 → 1 and `creditsTenths` 1000 → 1200 at `pirates` and `after-the-fight`.

The replay says so, and the owner accepted it as re-recorded (D115). Every other progress outcome is as recorded.

`CheckFormat` and `CheckProjectFiles` are clean. g++ (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`) and clang-tidy 18 are clean on the changed files. clang-tidy 18's `EnumCastOutOfRange` reports on `SpaceObject.h:42` and `AiTests.cpp:82` are on lines this change does not touch.

| Not run here | the MSVC build and clang-tidy 22, which CI runs; `Engine::Window`'s release and capture, and `Application`'s drag, which only Windows compiles; D04 to D07 on WARP, whose images CI draws; the game played with the charts drawn |
|---|---|

## What this forecloses

The charts' cursor has no key: a replay or a test moves it with `click`. The keyboard's steering no longer carries from a chart into flight, which the original's did. A chart's starfield is the galaxy, not the drawing. Data on System is the charts' side panel, docked and in flight, and has no key of its own.
