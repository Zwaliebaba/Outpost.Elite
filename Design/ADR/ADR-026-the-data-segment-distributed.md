# ADR-026 — The data segment distributed: groups of state, and constants out of it (D38)

**Status:** accepted 2026-10-11, with the change that implements it: `Design/DataPartition.tsv`, `GameData` generated as groups of state loaded by name, the constants as generated `constexpr` tables, the value dump by name, and the state digests re-based. It is change 3a of the natural layout, piece 3 of D33's route, by the owner's ruling D38 ([Reverse-Engineering-Plan.md §5](../Reverse-Engineering-Plan.md#phase-5--beyond-the-original-outside-this-plan)). Change 3b finishes it. **Amended 2026-10-11 by change 3b's first part:** the value dump missed one regression, because no replay reaches it. M's launch copies its missile from wherever DI was left, and past the dashboard's cache or the drawing buffer those 64 bytes were no longer held together, so the launch threw. They are now built from the members and tables that hold them.

## Context

After change 2 (ADR-025), native code reached the game's state as members, but of one struct: `GameData`, the 1987 data segment, 64 KiB at its original offsets with its gaps.

The owner asked for the state to live with the code that uses it. They proposed process-wide globals, file-local where one file uses a variable. They ruled instead (D38):
- **constants** become `constexpr` tables beside their readers;
- **mutable state** becomes groups that the `GameState` holds in its counted block.

The block keeps every write counted, so paced time is unchanged (ADR-024). A process-wide global could not be counted per game, reset per game, or digested.

## Decision

**1. `Design/DataPartition.tsv` says where every member lives.** It covers the 480 top-level members of the 1987 `GameData`, 54,570 bytes. Its header documents the columns.
- **Readers and writers.** They were measured by a static scan, then confirmed in the code and by the bytes the corpus and the tests read and write.
- **Where the scan was wrong.** It missed writes made through spans, raw accesses, member pointers and helpers, and it missed some readers, for example of the texts printed as one run. Each correction is in the notes column.
- **One writer** puts a member in that subsystem's group: 18 groups, Ai to Video.
- **Several writers** put it in a shared group:
  - `PlayerShip` (24 members), `Messages`, `Audio` (Sound and Timer), `Menus`, `Disk`, `Drawing` (the space view's buffer among them), `World` (the ship slots), `Flow`, `Jump`, `Keyboard`;
  - `Commander`: `CommanderState`, kept whole at its exact 1987 layout with its asserts, because it is the `.cdr` file format and the replays load such files.
- **Nothing writes it:** a `constant`. **Nothing reads it:** `unused`. The 105 unused members (8,995 bytes) wait in `UnusedState` for change 3b, which removes them with the turn probe.
- **Kept together.** Members read as one text or span stay together in their 1987 order. Four constants stay in the state because a text runs on from them into bytes the game writes; 19 gaps stay for the same reason.

**2. `GameData` is generated as 30 group structs,** 394 members and 24,447 bytes, down from 65,536.
- Each group is a struct of `Counted` members with the explicit copy constructor (ADR-024), and the block is still below its 128 KiB alignment.
- `Tools/GenerateGameData.py` reads the partition beside `Symbols.tsv`. `DataPlaces.h` maps each member's name to its 1987 offset and its new place.
- `ReferenceLoadImage` loads every member by name from the unchanged image, which keeps its measured hash.
- **Still by 1987 address, until 3b:** the `GameState` answers a read of a byte that no member holds from the image, and refuses a write to one.

**3. The constants leave the state.**
- 86 members, 30,123 bytes, are now `constexpr` tables generated from the checked-in image, in the header of the subsystem that reads them. Four that several subsystems read are in `SharedTables.h`.
- None turned out to be written: each compiled as a constant, and each equals the image at all 490 points.
- Eighteen routines now read only constants, so they no longer take the `GameState`.
- Ten reads in native code still go by 1987 address, where a DOS call, a loop turn or the original's indexing past a table's end needs it. Change 3b converts them.

**4. Native code and the tests were rewritten to the groups.** A script made 1,542 `Data().x` uses `Data().<group>.x` and 543 `commanderBlock` uses `commander`. It turned 75 constant uses into tables; 22 more were done by hand.

**5. The proof: values by name, then the re-base.**
- **The value dump.** `Tools/CompareStateValues.py`, which replaces `CompareStateDumps.py`, dumps every member's value by name at every corpus digest point and every twin answer: on the base from the 1987 layout, on the change from the groups.
  - At 490 points, 1,248,030 values: 0 differ, the unused members included, at each of the three commits that move the layout.
  - Pictures and outcomes: 0 differ.
- **The turn probe:** identical to the base at each of those commits, over the replays' 42,696,850 turns and the suite's 74,617,140. No write the idle test saw was removed.
- **The re-base** (D32): the state digest hashes the block's bytes, which moved.
  - The 134 corpus digests and the 356 answers were recorded in one process, gated on the value dump, never in CI.
  - A check confirmed that only the digests changed, line ends included.
  - A second recording reproduced all 27 files byte for byte.
- **Failing commits.** The commits between the first and the re-base fail only the 37 state-digest tests, as in change 2.

**6. Measured 2026-10-11.**

| Check | Result |
|---|---|
| `GameLogicTests` | 140 of 140 under g++ and clang++ and in a g++ Debug build with `_GLIBCXX_ASSERTIONS`, on the change and on main with it |
| The corpus | all 26 replays meet all 134 re-based digests; pictures and outcomes, 0 differ |
| Run time (g++, Release, all 26 replays, medians of 7 alternating rounds) | 2.26 s before, 2.24 s after |
| Checkers | `GenerateGameData --check`, `GenerateTables --check`, `CheckFormat`, `CheckProjectFiles` clean; clang-tidy clean on all 42 changed portable translation units |

**7. What change 3b does.** It runs in two parallel parts that move no byte, then one that does.
- **The parallel parts** convert the last accesses by 1987 address: the raw offsets and the constants still read by address. Each is held to digests that stay identical.
- **The last part does the layout's remainder:**
  - the four constants still in the state, the unions, the unused members, the gaps, and naming the unnamed buffers;
  - removing the `GameState`'s answer by address;
  - `_backward`;
  - D37's second round and the stale includes.

  It ends with one re-base.

## What this forecloses

- **The 1987 layout as the state's shape.** Members live by owner, and only `CommanderState` keeps its bytes, as a file format.
- **A constant in the state.** What nothing writes is a `constexpr` table where it is read.
- **Process-wide game state.** Every group is held by its `GameState`'s block, counted and digested per game (D38).
