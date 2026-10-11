# ADR-028 — The tests no longer hold the port to ELITES.EXE (D57)

**Status:** accepted 2026-10-11, with the change that implements it: 38 tests deleted, with the twins' answer files and the helpers only they used. It records the owner's ruling D57 ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)) that matching `ELITES.EXE` is no longer needed. It amends ADR-008 item 8, ADR-016, ADR-020 and ADR-022.

## Context

Three things in the suite held the port to the 1987 original:
- **The image's hash.** `ReferenceImageTests` held the checked-in load image to the hash measured from `ELITES.EXE` (ADR-020).
- **The twins.** 36 scenarios each compared the game's state digest, at every point, with the answer recorded from the interpreted original before D7 deleted it (`KnownAnswers.txt`, ADR-016). Beside each answer were its picture and its outcomes (`KnownPictures.txt`, `KnownOutcomes.txt`, ADR-022).
- **The corpus.** `CorpusTests` played every replay in `Replays/` and checked each of its state digests, which were recorded the same way (ADR-008 item 8).

`3eeaca6` fits the docking computer at the start: it changes one byte of the load image, `DS:75EC dockingComputerFitted`, from `00` to `01`. Every state digest hashes that byte. So on `main` at `1f84b1d`, 38 of 142 tests failed:
- **The image's hash** no longer matches the file.
- **The 36 twins** each fail at their first answer, the state they start in: `26500ea3…` where `cdda6262…` is recorded.
- **The corpus** fails at the first digest of its first replay.

D26 had already said the corpus and the known answers retire once the game changes on purpose. D33 then kept them gating.

## Decision

**The 38 failing tests are deleted.** The owner ruled this over two alternatives: keeping each scenario's own checks without the comparisons, or re-recording every answer from the game as it is now.

| Deleted | What it was |
|---|---|
| `ReferenceImageTests.cpp` | The image held to the file's hash |
| `CorpusTests.cpp` | The replay corpus, played and checked digest by digest |
| `AiTests`, `DockedTests`, `EquipmentTests`, `FlightTests`, `HyperspaceTests`, `InputTests`, `MarketTests`, `SaveLoadTests`, `StartUpTests`, `TextTests`, `TwinRigTests` (`.cpp`) | Every test in them was a twin |
| `GalaxyTests`' `GalacticChartAgreesOnEveryKey`, `ShortRangeChartAgreesOnEveryKey` and `SystemDataScreenAgreesForTwoDigitTechLevels` | Twins; its two other tests stay |
| `TwinRig.h`, `StateDumps.h`, `GuardReport.h`, and `TwinAnswers` in `AnswerFile.h` | Used by nothing else |
| `KnownAnswers.txt` (356 answers), `KnownPictures.txt`, `KnownOutcomes.txt` | The twins' answers, which go with them (ADR-016 item 7) |
| `Tools/CompareStateValues.py` | It read the state dumps `StateDumps.h` wrote |

`Build/RunTests.py` has no groups now. The corpus had a shard of its own, and what is left runs as one shard.

**What stays:**
- **The other 104 tests:** the devices, the replay format, the outcomes, the platform and the rest.
- **The subsystems' known answers** for the galaxies, the markets and the maths (D33), which still hold.
- **`Replays/` and `ReplayRunner`.** A recorded session still plays headless, but no test plays the corpus.

## Measured 2026-10-11

| Check | Result |
|---|---|
| The suite, Debug x64, `Build/RunTests.py` | 104 tests, all pass, in one shard of 7 s on a 20-thread machine. Before: 142 tests, 38 failing. |
| Checkers | `CheckProjectFiles`, `CheckFormat`, `GenerateGameData --check` and `GenerateTables --check` are clean. |

## What this forecloses

- **Proof that the port is the original.** Nothing checks the image against `ELITES.EXE`, or any state against 1987's. A change to what the game does is caught only by a remaining test's own asserts.
- **The deleted scenarios' own checks.** Those asserts went with them, for example that a declined supernova mission pays 1,400 credits, and that a system data screen shows a two-digit tech level.
- **Replays as a gate.** D35 kept the corpus so that CI played it on every run, and it no longer does. A session the shell records can still be played, but nothing checks that it reproduces.
- **Phase 6's exit** (§5) asks that "every replay, outcome and answer holds". Of those, only the subsystems' answers are left to hold.
