# ADR-029 — The guard restored (D59)

**Status:** accepted 2026-10-12, with the change that implements it. It records the owner's ruling D59 ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)). It supersedes [ADR-028](ADR-028-the-match-to-elites-exe-retired.md) for D57, and leaves D58 as that ADR records it. It undoes ADR-028's amendments to ADR-008, ADR-016, ADR-020 and ADR-022.

## Context

D57 deleted the tests that held the port to `ELITES.EXE` because two hand edits to the load image made 38 of them fail: `3eeaca6` fitted the docking computer at the start (`DS:75EC`, `00` to `01`, marked a cheat in its comment), and `4450185` then raised the starting credits (`DS:75F3`, 1,000 tenths to 61,056). Neither was a ruling, and ADR-028 says what the deletion foreclosed. Nothing checked a state against 1987's any more. The 102 tests left covered the devices, the replay format and the subsystems' value routines, and none of them covered flight, combat, docking, hyperspace, the missions, or saving and loading.

That left the plan's next two steps without the gates they were written against:
- Change 3c, the end of Phase 5, removes the `GameState`'s answer by 1987 address, the unused members, the gaps and the unions, and re-bases once. Changes 3a and 3b proved the same kind of work at every digest point of the corpus and the twins.
- Phase 6's pieces 1 to 3 exit when "every replay, outcome and answer holds", and D52 put the renderer first so that it would be built while they do.

## Decision

**The two edits are reverted, and the guard is restored as it stood before D57.** With both bytes back at the file's values, every recorded answer holds again, so nothing is re-recorded and no answer changes.

| Restored | From |
|---|---|
| `ReferenceImageTests.cpp`: the image held to the file's hash | `91fe737`, the parent of D57's change |
| `CorpusTests.cpp` and `Replays/`: 26 replays, 18 prepared commanders, 134 digest points with their pictures and outcomes | `91fe737` |
| The 36 twins in `AiTests`, `DockedTests`, `EquipmentTests`, `FlightTests`, `HyperspaceTests`, `InputTests`, `MarketTests`, `SaveLoadTests`, `StartUpTests`, `TextTests` and `TwinRigTests`, and `GalaxyTests`' three | `91fe737` |
| `TwinRig.h`, `StateDumps.h`, `GuardReport.h`, `AnswerFile.h`'s `TwinAnswers`, and `KnownAnswers.txt` (356 answers), `KnownPictures.txt`, `KnownOutcomes.txt` | `91fe737` |
| `RecordCheckpoints` and `RecordDigests` (`Replay.h`) and their two tests; `Tools/PrepareCommanders.py`; `Tools/CompareStateValues.py` | `91fe737` |
| `Build/RunTests.py`'s corpus shard and its warning for a logged difference | `91fe737` |

**D58 stands.** `ReplayRunner`, the shell's session recording, `ReplayRecorder` and `ReplayTests.ARecordedSessionPlaysBack` stay deleted: the corpus is played by `CorpusTests` and needs none of them.

**A change to the game's starting state is a ruling.** A cheat for play-testing does not go in the load image, which every digest hashes. If one is wanted, it is a ruling and its own change, outside the state the guard checks.

## Measured 2026-10-12

These were measured in a Linux session with g++ 13.3 through `Tools/BuildPortable.py` and a stand-in for `CppUnitTest.h` outside the tree (ADR-004 item 4). MSVC is the build that gates, and CI runs it.

| Check | Result |
|---|---|
| `91fe737` with `DS:75EC` set back to `00` | 142 tests, all pass: the twins and the corpus meet every 1987 answer. |
| This change, `-O2` | 141 tests, all pass, in 2.7 s on one thread. The one missing is D58's `ARecordedSessionPlaysBack`. |
| This change, `-O0` | 141 tests, all pass, in 27.8 s on one thread. That agrees with the 28.3 s `Build/RunTests.py`'s groups were balanced on, so the corpus keeps its own shard. |
| Checkers | `CheckProjectFiles`, `CheckFormat` (clang-format 18.1.3), `GenerateGameData --check` and `GenerateTables --check` are clean. clang-tidy needs the MSVC environment and runs in CI. |

## What this forecloses

- **Changing the starting commander in the image.** While the corpus and the twins gate, that byte is 1987's. A different start belongs in a prepared commander or a ruled change.
- **D26's retirement, on D57's terms.** The corpus and the known answers retire when a change on purpose moves them, as D26 and D44 already say, and each such change designs the guard that replaces what it retires.
