# ADR-046 — No 8086 in the game's code: the flag, ES, BP, AL and the line's registers go

**Status:** accepted 2026-10-14, with the change that implements it, step 6 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D76. **Amended 2026-10-14:** DX, BX and DI go too, by D84 (items 6 to 8).

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

## Decision, amended: DX, BX and DI (D84)

6. **BX goes** (D84). It never reached the state: the explosions handed it on only for the divide trap of a barrel count to save, and the count's two divides take the cargo, not BX. `ExplodeObject`, `DropCargo`, `SpawnFragments` and `UpdateMissileAi` return nothing, and `Explosion`, `MissileFlight` and the crosshair search's `bx` go. A struck missile's target is the one it flew at, read before it explodes, where the original read the target of the record ExplodeObject left DI on: the missile's own, as no missile carries cargo.
7. **DX goes** (D84). The AI's handlers passed it from one ship to the next, and the traders, wolves and hunters took its low byte as the low byte of the box their range is measured by (`WithinRange`). That box is now the range byte in 256ths, its low byte 0. `MovedObject`'s and `Velocity`'s `dx` go.
8. **DI goes** (D84). A launched missile was copied from wherever DI was left: a ship slot, or the 64 bytes past the dashboard's cache, the stardust or the drawing buffer as the 1987 data segment held them (`LaunchDi`, `BytesAfterDrawBuffer`). Its slot is now cleared first, then made a missile (`InitMissile`). The tables that only those bytes read go: `AMSTRAD_PRESENT`, `AMSTRAD_SIGNATURE`, `DOS_VERSION_MESSAGE` and `EQUIPMENT_LOSS_MESSAGE`.

## What moved with items 6 to 8, and how it was measured

The state was dumped by member at every digest point of the corpus and the twins before the change (26 replays and 33 twins, 451 points) and after it, and compared by path:

| What | Result |
|---|---|
| Points that moved | 11, in three replays; every twin's point as it was |
| `attack-the-station` | from its point `missile` (line 18): the launched missile's `compassZ`, where its slot had held the bytes DI was left on; then the fight, which the missile's flight changes: its next four points, aft shield and ships in play |
| `masking-device-and-anti-ecm` | the same from its missile launch (line 24), and its next three points |
| `hyperspace-and-fight` | from `pirates` (line 18): the pirates' paths, the random numbers they drew and the fore shield, as their range is measured without DX's low byte; at `after-the-fight` the recorded fight no longer destroys a pirate: kills 0 and credits 100.0 where they were 1 and 120.0. Its header says so |
| Pictures | six, at those points |

The digests were re-based from the dump after the change, the pictures and outcomes recorded by `OUTPOST_ELITE_RECORD_GUARD`, and a second recording reproduced every file byte for byte. The suite then passes in check mode under g++: 178 tests.

## What this forecloses, and what it leaves

- **A register as a parameter** for these, BX, DX and DI among them. A routine that needs a value takes it by name.
- **The 1987 data segment past a buffer.** Nothing reads it any more; a missile is a missile from its template.
