# ADR-030 — Change 3c: nothing by the original's address, and the layout's remainder

**Status:** accepted 2026-10-12, with the change that implements it, the first part of change 3c of D33's piece 3 ([Reverse-Engineering-Plan.md §5, Phase 5](../Reverse-Engineering-Plan.md#phase-5--the-native-platform-and-the-natural-layout)). The later parts amend it: the layout's clean-up with one re-base, D37's second round, and D43's clamp at load. It amends ADR-015, ADR-018 and ADR-026. **Amended 2026-10-12 by the second part,** the layout's clean-up and its one re-base, **and by the third part,** D37's second round (below).

## Context

After change 3b, game code reached its state by member almost everywhere. The `GameState` still answered by the original's address, through `OriginalLayout`, its map of the 1987 data segment onto `GameData`:
- **The accessors by offset and the far ones:** `Byte`, `Word`, `SetByte`, `SetWord`, `FarByte`, `SetFarByte`, `LinearByte` and their kin, `ObjectAt`, `TextAtOffset` and `BytesAt`.
- **The views from a member:** `TextAt`, `TextAfter`, `BytesFrom`, `WordOf` and `SetWordOf`. These read on past a member's end wherever `GameData` held the next bytes beside it.
- **`OffsetOf`, `SlotOffset` and `SlotAt`:** the 1987 offset of a member or slot. The waiting loops carried these in their turns, and the register-like results of Combat and Flight held them.

The plan's list for change 3c named the runs that still cross members and the direction-flag branches that still went by address. The guard ([ADR-029](ADR-029-the-guard-restored.md)) holds again, so this change could be proved against every 1987 digest.

## Decision

**1. The `GameState` holds members and video memory, and nothing else.**
- **What is deleted:** `OriginalLayout`, every accessor by offset or far address, `ObjectAt`, `OffsetOf`, `SlotOffset`, `SlotAt` and the text and span views.
- **What stays:** `Slot`, and `IndexOf`, which finds a slot's number from its record, never from an offset. The video accessors work on video memory directly and refuse an offset past its 16 KiB.
- **What is new:** `PlaceOf(member)`, a member's offset in `GameData`.

**2. A turn carries a member's place where it carried its 1987 offset (ADR-015).** A turn's signature is only ever compared with the last turn's. A place is one-to-one with the offset it replaces, so every turn idles exactly where it did. 15 turn sites, in Docked, Flight, Hyperspace, Market and SaveLoad, changed.

**3. Every run the original reads across members is named.** Each run uses `BytesThrough(first, last)` (`Text.h`) with a `static_assert` that its members still follow one another as they did in 1987. A text or span inside a run or member uses `TextWithin` or `BytesWithin`, which throw rather than read on.

Two probes found the runs:
- **A probe of every run across top-level members** found eight, in all of the corpus and the twins.
- **The suite with the views made strict** found two more, inside `commanderBlock`, which the first probe could not see.

| Run | Members | Reader |
|---|---|---|
| The drawing buffer, 8 KiB | `gap0000` to `gap1FC0` | `DrawingBuffer` (Video), now also Docking's `ViewBuffer` |
| The fuel's text and digits | `gap827F` to `data840B` | `FuelRun` (Docked) |
| The status screen's missiles | `gap84EC` to `gap84F6` | `MissilesRun` (Docked) |
| The supernova's debriefing | `taskCompleteFrame` to `gap907F` | `TaskCompleteRun` (Docked) |
| The dashboard's cache | `missileCountShown` to `speedShown` | `InvalidateDashboard` (Flight) |
| The pause screen's text and options | `pauseMenuText` to `gapAC0C` | `PauseRun` (Flight) |
| The cash's text and its NUL | `creditBalanceText`, `gap759A` | `CashRun` (Docked) |
| The selected system's name and its NUL | `selectedSystemName`, `gap75CC` | `SelectedNameRun` (Docked) |

A text that steps from one NUL to the next (`PrintTextLines`) walks its run's bytes. A docked frame's descriptor (`DrawDockedFrame`) is the bytes from where it starts to the end of the member that holds it. The commander's equipment and system records are reached as bytes of `commanderBlock`, at `COMMANDER_STATE_OFFSET`.

**4. The direction flag is clear wherever the game calls a routine, so the branches that ran down by address go.**
- **The binary has one `STD`,** at CS:2E4A in `MaskOutsideTunnel`, and its `CLD` is at CS:2E5C. Between them the code runs straight on, apart from a `jnz` back inside the pair, and there is no `POPF`. Interrupts land only in waits, and there is no wait there (ADR-015). This was measured from the checked-in image's code segment, disassembled with `ndisasm`.
- **In the native code,** every value the direction flag is passed as comes from `Start`'s `false` (DOS's `CLD`), a literal `false`, an `&&` with one of those, or a callee's own parameter handed back.

So the downward branches of these routines could never run, and they go: `ClearAllObjects`, `CopyObject`, `SaveStartupCommander`, `StartNewGame`, `ResetHyperspaceRings`, `UpdateObjectsAndSpawn`'s clear, M's launch from past a buffer filled downward, and the first half of `MaskOutsideTunnel`.

Each routine whose only use of the flag was to pass it on loses the parameter: 33 functions in all. Where the flag still decides something (a screen's copy, a fill), it stays, and a later change can remove it on the same proof.

**5. `MaskOutsideTunnel`'s `STD` half keeps its downward stores,** in the buffer. For each of the ten tunnel rectangles, both halves stay within the 8 KiB buffer: from 382 to 7,872, measured from `tunnelRectangles`. So a store outside it throws, where the original would have run on into the data segment.

**6. The register-like values that carried offsets are what they mean.**
- **M's launch:** DI before the launch is a `LaunchDi`: a slot, or which buffer it is past. `MissileSource` holds a slot.
- **Combat's results:** `Explosion::slot` and `CrosshairTarget::diSlot` are `SlotIndex`. `Explosion`'s SI and ES, `FragmentsLeft`'s ES and `MissileFlight`'s DI were never read, and go.

**7. The PC model reads the game's memory by address nowhere.**
- `Platform::PrintDosString(segment, offset)` had no caller; `Hardware::PrintDosString(text)` is the game's. It goes, and its test prints through `Hardware`.
- `HandlerOpcode` reads the ROM only. The disc-menu twin's simulated mouse driver points the int 33h vector at the ROM's driver entry instead of into the data segment, and its digest is unchanged.

## Measured 2026-10-12

In a Linux session with g++ 13.3 and clang 18 through `Tools/BuildPortable.py`, and a stand-in for `CppUnitTest.h` outside the tree (ADR-004 item 4):

| Check | Result |
|---|---|
| The suite, g++ `-O2` | 141 tests, all pass, in 2.6 s: every corpus digest, picture and outcome and every twin's known answer is the 1987 one, with no re-base |
| The suite, g++ `-O0` | 141 tests, all pass, in 28.1 s |
| The suite, clang++ 18 `-O2` | 141 tests, all pass |
| The change | 46 files, 686 lines added and 1,200 deleted; `GameState.h` is 408 lines shorter |
| Checkers | `CheckProjectFiles`, `CheckFormat` (clang-format 18.1.3), `GenerateGameData --check` and `GenerateTables --check` are clean. clang-tidy and MSVC run in CI. |

## The second part: the layout's remainder (2026-10-12)

What ADR-026 item 7 left for the layout's last change: the unused members, the unions, the four constants still in the state and the 19 gaps, ending with one re-base.

**8. The unused members leave the state.** The 105 members nothing reads, 8,995 bytes, and `UnusedState` with them, go. A partition row marked `unused` is now in no group and has no table, as a constant has none in the state. Of M's three launch reads (ADR-030 item 6), `equipmentLossMessage` and `dosVersionMessage`, which nothing writes, are constants. `amstradPresent`, which StartUp writes, is in `StartUpState`. The writes to the other 16 written members go, with `StopContinuousNoise`, whose only write was one. D5's byte, `protectionShown`, is one of them: the loader no longer writes it, and still checks that the image holds the file's 00 there.

**9. No unions.**
- **The generated words:** a word the symbol table also names a byte of is the word alone. The byte is reached with `High`, `WithHigh` and the named readers beside their code: `SystemX`, `SystemY` and `SystemSeed2High`. A word written whole counts one change exactly when a write of its byte would.
- **SpaceObject's second names:** they are accessors on the one member: `SpinRoll` and `SpinPitch` for `blipX` and `blipY`, `FlashFrames` for `cargo` and `FramesAway` for `scanned`.

**10. The four constants and the 19 gaps leave the state.** Measured at every digest point of the corpus and the twins, every gap byte but the drawing buffer's rows and three digit fields was never written. Each run of item 3 is now its written fields, as members, and its constant text, as generated tables. Its printer composes the text the original read as one (`JoinTexts`).

| Run | Constant text | Written fields |
|---|---|---|
| The drawing buffer | — | `drawingBuffer`, one 8 KiB member: rows 0 and 127 and the space view between |
| The galactic chart's title | `galacticChartTitle`, `galacticChartTitleEnd` | `galacticChartNumber` |
| The system data screen | `radiusLabel`; its units, read up to their NULs | `data7D48`, `data7D49`, `selectedDistanceText`, `populationDigits`, `productivityDigits`, `radiusDigits` |
| The fuel text | `fuelTextPrefix`, `fuelTextPoint`, `fuelTextSuffix` | `data83F7`, `data83F9`, `fuelDigits` |
| The missiles' name | `missileNameOpen`, `missileNameClose` | `data84F5` |
| The supernova's debriefing | `taskCompleteFrame`, `supernovaDebriefOpen`, `supernovaDebriefClose` | `refugeeRewardDigit` |
| The hyperspace countdown | `hyperspaceCountdownMessage` | `hyperspaceCountdownDigits` |
| The pause screen | `pauseMenuText`, the five options' letters | `frameRateKeyLabel`, the five options' flags |

The symbol table names each new row; a text with no NUL of its own, or several texts one after another, is the generator's `chars` shape, a sized `std::string_view`. The bytes of a gap no reader reaches, such as the 362 bytes of other texts before the fuel text in `gap827F`, go with it. `PrintTextLines` walks NUL-separated texts in a `std::string_view`, and `MessageText` returns a `std::string`, so that it can return the countdown it composes. The pause screen's turns carry the menu's offset and the next option's flag's place, each one to one with the address the original holds.

The gaps nested inside a named record (37, in nine groups' records) are a record's unnamed bytes, not runs, and stay; `CommanderState`, the file's format, keeps its 1987 layout whole.

**11. One re-base.** `StateDumps`' gate matches each byte by its 1987 offset, not by its member's name, so a change that renames, merges, splits or drops members is still held to the dump before it. A byte the tree before held as a constant is compared with the image.

### Measured 2026-10-12

| Check | Result |
|---|---|
| The state | 24,447 bytes in 30 groups and 394 members before; 14,420 bytes in 29 groups and 268 members after |
| The gate | at all 490 digest points, every byte the state holds equals the dump of `main` before the change at its 1987 offset, and every picture and outcome is exact; at each of the change's steps, and at its end |
| The turn probe | each test's sequence of idle and working turns, 73,089,965 turns in all, identical to `main`'s; at the unused members' removal and at the end |
| The re-base | the 490 state digests change (134 in `Replays/`, 356 in `KnownAnswers.txt`), and no picture or outcome line; a second re-base writes every file byte for byte the same |
| The suite | 141 tests pass in check mode under g++ `-O2` (2.7 s), g++ `-O0` (27.0 s) and clang++ 18 |
| Checkers | `CheckProjectFiles`, `CheckFormat`, `GenerateGameData --check` and `GenerateTables --check` are clean; clang-tidy and MSVC run in CI |

## The third part: D37's second round (2026-10-12)

**12. The results no caller reads are gone.** D37 made void every native routine whose result no caller reads, and the dead-code sweep found 47. What the later changes left are 15 more. The inventory is a clang-tidy run of `bugprone-unused-return-value` over every value-returning function of `GameLogic`, against the references in the AST of every translation unit of `GameLogic` and its suite. It finds the 15 routines below, each with every caller discarding its result, in game code and tests alike:

- `BlankHelpRows`, `BlankLeadingZeros`, `ClassifyViewPosition`, `DrawCircle`, `DrawCross`
- `DrawViewString`, `FillClippedTriangle`, `FormatDecimal5`, `OffsetVertexByCenter`, `PostDockingMessage`
- `PrintCountedTextLines`, `PrintEquipmentSellColumn`, `RepeatStoreVideoWords`, `RunVertexProgram`, `ShowMessage`

All of them are now void, and `PrintTextLines` with them: `PrintCountedTextLines` was the last of its readers. A second run finds no more. The result types only they returned go with them: `BlankedZeros` and `ViewTest`. Nothing else changes. Every write each routine makes stays, in the same order, so no write the idle test sees goes, and no ruling is needed. Two routines discard their result in most calls but keep it:

- `ComputeVelocity` is read by `Combat` and `Ai`.
- `InterruptController::Acknowledge` is read by `Platform`.

### Measured 2026-10-12

| Check | Result |
|---|---|
| The gate | every digest, answer, picture and outcome unchanged, in check mode; no re-base |
| The turn probe | each test's sequence of idle and working turns, 73,089,965 turns in all, identical to `main`'s |
| The suite | 141 tests pass under g++ `-O2`, g++ `-O0` and clang++ 18 |
| Checkers | `CheckProjectFiles`, `CheckFormat`, `GenerateGameData --check` and `GenerateTables --check` are clean; clang-tidy and MSVC run in CI |

## What this forecloses

- **Reaching on past a member.** A path that reads a text or span past its member's end, or its run's, throws `std::out_of_range` where the original would read on. The corpus and the twins reach none. A hand-edited `.cdr` whose name has no NUL now throws on the status screen. D43's clamp, the last part of change 3c, terminates the name at load.
- **A wait that idles on a turn the original does not.** That cannot come from this change: a place stands for an offset one to one.
- **A downward string operation outside `MaskOutsideTunnel`'s two instructions.** Should a change on purpose ever set the flag elsewhere, it designs its own copy.
- **Constant text in the state.** A text the original reads as one, partly constant and partly written, is composed where it is printed; only what the game writes is state.
- **A gap or a union in the state's groups.** The 1987 offsets survive in `DATA_PLACES`, which the image's loader and the re-base gate read, and nowhere native code reaches the state.
