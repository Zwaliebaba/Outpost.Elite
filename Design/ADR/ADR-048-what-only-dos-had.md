# ADR-048 — What only DOS had: the cheat is a flag, leaving is a quit, and the screenshots go

**Status:** accepted 2026-10-14, with the change that implements it, part of step 7 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's rulings D80 and D86.

## Context

Three things of the 1987 game reached DOS for what Windows has no use for: the ` cheat` command tail, which Start read from its PSP; Alt+PrtSc, which wrote the screen's bytes to `eliteNN.hi` or `.lo` through int 21h, with the timer, keyboard and divide handlers taken out round the write; and Exit to DOS, which set 80-column text and printed "Thank you for playing ELITE - Have a nice DOS!". D80 ruled their Windows counterparts; D86 then ruled that the screenshots are not wanted at all.

## Decision

1. **The cheat is a flag** (D80). `Platform::Desc::cheat` replaces the command tail, and `CheckCheatArgument` sets `cheatEnabled` from it; the shell turns it on with `--cheat` on its command line. `cheatArgumentKey`, the XOR key and the tail go.
2. **Exit to DOS is a quit** (D80). Start ends the program when the disc menu's E and Y leave, as before, with no mode change and no farewell: `ExitToDos`, `exitMessage` and `Hardware::PrintDosString` go. The shell already closes when the program ends.
3. **The screenshots go** (D86), where D80 had made them a PNG the shell saves. GetKey and the flight loop no longer test Alt and PrtSc (`SaveScreenshotIfAsked`, `PollScreenDumpKey`), `SaveScreenshot`, `WriteScreenshotFile` and `ShowDiskError`, which only a screenshot showed, go, and so do the state's `screenshotNumber` and the two file names, 24 bytes: the state is 14,385, its start hash `411a7a23…595f`. `KeyPress` loses its `screenshot`, and a wait spins after a key it took and idles otherwise, as it did when no screenshot was taken.

## What moved, and how it was measured

The state was dumped by member at every digest point of the corpus and the twins before and after the change and compared by path, the 24 bytes aside:

| What | Result |
|---|---|
| Members that moved | none, at any point of the 26 replays or of the twins kept |
| Twins | `TwinScreenshots` and `TwinFlightScreenshot` retired with their 15 answers between them; `TwinQuit` added in their place for what of them stays: a commander saved from the chart's Escape, then E and Y, which end the program; the commander file it writes is the one `TwinScreenshots` wrote, `X.CDR`, by its SHA-256 |
| Pictures | two, both of the screen the program ends on: `leave-for-dos`'s `at-dos` and `TwinStartAndExit`'s fourth, now the disc menu's page where they were the farewell's |

Every digest moved, as the state is 24 bytes shorter. They were re-based from the dump after the change, once the comparison above held; the pictures and outcomes were recorded by `OUTPOST_ELITE_RECORD_GUARD`, and a second recording reproduced every file byte for byte. The suite passes in check mode under g++: 176 tests.

| Not run here | the MSVC build and the game, `--cheat` among it: CI builds and runs the tests, and the owner plays it |
|---|---|

## What this forecloses

- **A screenshot key.** Windows' own capture serves; the game has none, unless the owner rules one back in.
- **The command tail.** The shell's command line is the only way in, and `--cheat` the only word it reads.
