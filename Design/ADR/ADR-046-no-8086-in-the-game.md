# ADR-046 — No 8086 in the game's code: the flag, ES, BP, AL and the line's registers go

**Status:** accepted 2026-10-14, with the change that implements it, step 6 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D76.

## Context

After steps 1 to 5 and 8 (ADR-044, ADR-045), the game's code still carried five things of the 8086 from routine to routine because the 1987 code left them in registers: the direction flag, as a `bool _backward` on 99 of the functions defined in `GameLogic`'s sources and on the structs they returned; ES, as a `_segment` on 21; BP, as a `_countIfNone` on 22, threaded from DOS's entry through the title, the docked screens, flight and the charts; AL, shifted by each key and carried through the docked menus, which the joystick's fire read; and DrawLine's registers, as a record named for them (`LineState`: `al`, `ah`, `bl`, `bh`, `dl`, `dh`, `cx`, `di`, `bp`).

## Decision

1. **The direction flag goes.** Every routine runs its string work forwards; the parameters, the `backward` members of the exits, `FacesEnd`'s flag and the "whether DrawLine filled" results that existed only to clear it go, and `TransformAndDrawObjects`, `DrawObject`, `RunBlueprintHandler`, `DrawLine`, `DrawClippedLine`, `UpdateStardust`, `ResolveLaserFire`, `ApplyEnemyLaserHit` and the rest return nothing. The one STD of the original, MaskOutsideTunnel's, stays as what it is: a store that runs down the buffer (`Store`'s `_backward`, which its two calls pass as constants).
2. **ES goes.** Every print goes to the CGA. `CheckScreenSegment` threw when a print was handed another segment; `ChartSegment` handed one only to a chart drawn while its frame already showed, which flight never reaches (the cockpit always shows when its keys run).
3. **BP goes.** It reached the game in one place: the count `SearchNearestSystem` makes the selected index from when no system is on the chart, and a system always is (the measurement recorded in Flight.cpp before this change: on the galactic chart, every galaxy has one near enough from any cursor; on the short-range chart, the current system at its centre). That case now selects the current system. `RunTitleAndDocked` returns whether the disc menu left for the disk; `RunFlight`, `GameLoop` and `RunTitle` return nothing.
4. **AL goes.** After D81 nothing read it: it was the value the joystick's fire was polled with. `ScreenKey`, `DiscMenuExit` and the menus' loops keep the key and the cursor alone; `AlAfterKey`, `MenuLoop` and the chart's recentring through AL go.
5. **The line is drawn with a pen, not registers.** `LinePen` names what DrawLine holds: the byte, the row step, the pixels across and the rows down, the keep mask, the pixel, the byte being drawn and the fill. Each of the five line routines keeps its loop, its error term and its counts as locals, in the original's order of writes.

## What moved, and how it was measured

The counts above are of parameter declarations in `GameLogic/*.cpp` on `main` at `9459a57`, by `grep`.


Nothing. The state, its layout and its writes are untouched, so every digest of the corpus and the twins is the one recorded before the change, and so is every picture and outcome: the suite passes in check mode under g++, 178 tests, with no re-base and no recording. Before removing the flag, each place that branched on it was made to trap when it was set (the five string steps in Docked.cpp, Galaxy.cpp twice, Text.cpp and Video.cpp), and the whole suite ran without one firing.

| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |
|---|---|

Two cases change only where the game failed or cannot go: a screen print through the data segment, which threw `std::out_of_range`, now draws on the CGA; and a chart with no system on it, which cannot happen, selects the current system instead of an index made from BP.

## What this forecloses, and what it leaves

- **A register as a parameter** for these five. A routine that needs a value takes it by name.
- **Left for later steps, or for the owner:** DX and BX still pass between the AI's handlers, the explosions and the missiles as 1987 left them in registers (`RunBehaviorHandler`'s `_dx`, `ExplodeObject`'s `_bx`, `Explosion`, `MissileFlight`), and DI still decides where a launched missile's bytes are copied from (`LaunchDi`, D38), which reads the bytes past the drawing buffer, the stardust or the dashboard cache as the 1987 data segment held them. The plan's step 6 named the flag, BP, AL and the line's records; these are the same kind of thing, and DI's reaches the state, so they are raised with the owner rather than taken here.
