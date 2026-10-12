# ADR-028 — The tests no longer hold the port to ELITES.EXE (D57)

**Status:** accepted 2026-10-11, with the change that implements it: 38 tests deleted, with the twins' answer files and the helpers only they used. A second change the same day removed what nothing used after them, at the owner's request: the replay corpus, the tool that prepared its commanders, and the functions that recorded it. A third change the same day deleted `ReplayRunner` and the shell's session recording, at the owner's ruling D58. **Superseded 2026-10-12 by [ADR-029](ADR-029-the-guard-restored.md) (D59), except for D58:** the two bytes that made the tests fail are reverted, and everything this ADR deleted for D57 is restored; `ReplayRunner` and the shell's recording stay deleted. It records the owner's ruling D57 ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)) that matching `ELITES.EXE` is no longer needed. It amends ADR-008 item 8, ADR-016, ADR-020 and ADR-022, and with D58 ADR-004 and ADR-009.

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

**Then what nothing used.** After the tests went, the owner asked what was left unused, and it went too:

| Removed | Why nothing used it |
|---|---|
| `Replays/`: 26 replays and 18 prepared commanders | No test plays them. `ReplayRunner` would fail every one at its first digest, since their digests are 1987's. |
| `Tools/PrepareCommanders.py` | It only wrote the commanders in `Replays/` |
| `RecordCheckpoints` and `RecordDigests` (`Replay.h`), and their two tests | They recorded the corpus's pictures and outcomes and re-based its digests, which `CorpusTests` did. Only their own tests called them. |
| `Build/RunTests.py`'s warning for a logged difference | Only the guard logged differences |

**One change in a tool.** `ReplayRunner --steps` took the sources of `file` steps from `Replays/`, and now takes them from the working directory. A `--replay` run takes them from beside the replay, as before.

**What was checked and kept.** No function in `GameLogic` was left without a caller by the deletion. Eight declarations the twins reached through headers are now used only inside their own `.cpp`, but they are used, so they stay. The functions that only tests call are the devices' test hooks, which were tests-only before.

**What stays:**
- **The other 102 tests:** the devices, the replay format, the outcomes, the platform and the rest.
- **The subsystems' known answers** for the galaxies, the markets and the maths (D33), which still hold.
- **`ReplayRunner`**, for the sessions the shell records: they are recorded on the game as it is, and play headless. D58 deleted it after all (below).

**Then `ReplayRunner` (D58).** Asked whether `ReplayRunner` was still used: no test and no CI step ran it, and its one use was playing back a session the shell had recorded, by hand. The owner ruled that it goes. Without it a recorded session has no player, so the recording goes with it:

| Removed | Why |
|---|---|
| `ReplayRunner/`, the project, and its line in the solution | Nothing ran it. `PngWriter`, which only it used, goes with it. |
| The shell's recording: the recorder, the digest every 10 seconds, the `end` digest and the file in `Saved Games\Outpost.Elite\Replays` | Nothing could play the file. |
| `ReplayRecorder` (`Replay.h`) and `ReplayTests.ARecordedSessionPlaysBack` | The shell was its only caller outside its test. |

The shell still sends only the keys `Elite::KeyNameOf` names, which it now asks directly, and DOS's clock still starts at `Elite::START_MOMENT`. It runs the machine to the wall clock's moment in one `RunUntil` rather than stopping at each 10-second digest on the way. The replay format, `ReplayPlayer` and the digests stay: the tests drive the game with steps and check it with digests. `.clang-tidy`'s header filter, `Tools/GenerateTables.py` and `Tools/BuildPortable.py` no longer name the project.

## Measured 2026-10-11

| Check | Result |
|---|---|
| The suite, Debug x64, `Build/RunTests.py` | 102 tests, all pass, in one shard of 8 s on a 20-thread machine. Before: 142 tests, 38 failing. |
| Checkers | `CheckProjectFiles`, `CheckFormat`, `GenerateGameData --check`, `GenerateTables --check` and clang-tidy are clean. |
| `ReplayRunner --steps` | Plays from the title to the dock, exit 0. |

## What this forecloses

- **Proof that the port is the original.** Nothing checks the image against `ELITES.EXE`, or any state against 1987's. A change to what the game does is caught only by a remaining test's own asserts.
- **The deleted scenarios' own checks.** Those asserts went with them, for example that a declined supernova mission pays 1,400 credits, and that a system data screen shows a two-digit tech level.
- **The replay corpus.** D35 kept it so that CI played it on every run. It is deleted, and git's history holds it. A session the shell records can still be played, but nothing checks that it reproduces.
- **A session played back (D58).** The shell records nothing, so a bug seen in play is no longer a file that reproduces it headless (ADR-009 item 8). Recording would come back with a player to run it.
- **Phase 6's exit** (§5) asks that "every replay, outcome and answer holds". Of those, only the subsystems' answers are left to hold.
