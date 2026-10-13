# ADR-040 — The native game loop, second step's end: the fields stop counting, and `Counted` becomes `Field`

**Status:** accepted 2026-10-13, with the two changes that implement it, the second part of step b of piece 4 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), under D70, and the owner's ruling D71. It supersedes [ADR-024](ADR-024-members-that-count-their-changes.md)'s change count; the members on the 1987 layout stay.

## Context

ADR-024 made each byte and word of the game's state a `Counted`: a write that changed the value counted in its GameState's change count, which the member found by rounding its own address down to the block the GameState kept aligned to 128 KiB. The BIOS's data area counted its own changes, the vector table its own, and every port write the original made was counted where it was made. All of it served one reader: the rule that found a wait from what a loop's turn changed (ADR-015).

[ADR-039](ADR-039-the-waits-are-declared.md) declared the waits, so the rule is gone and nothing reads a count. The type keeps two jobs that are not counting: a byte or a word at its 1987 offset with an alignment of 1, little-endian, and every write and copy of the state explicit. Its name would no longer say what it is. The owner ruled:

- **D71 (2026-10-13).** The type is renamed, in a commit of its own, so that the counting's removal stays small to review.

## Decision

1. **Nothing counts.** A member's write stores its value (`Counted::operator=`), and `ChangeCountAt`, `COUNTED_BLOCK_ALIGNMENT`, `GameState::ChangeCount` and its aligned block go: `GameData` is on the heap through a plain `std::unique_ptr`. `Bios::ChangeCount` and `PortWrites`, `Bios::Store`, the vector table's count, `Platform::MemoryChanges`, `PortWrites` and `NotePortWrites`, and the nine `Hardware` operations' port-write counts go with them. Video memory's writes store their byte.
2. **The rules the count needed stay**, as they keep every write and every copy of the state explicit: no copy-assignment between members (`a = T{b}`), no compound assignment or increment, a copy only through the explicit copy constructor. Loosening them is a change of its own.
3. **The tests that read the counts read the state instead.** `CountedTests` checks that each way of writing a member reaches its bytes; `StateDigestTests` that a write lands; the DOS rig counts the bytes a call changed by comparing the memory before and after it, where it read the count's difference; the BIOS's and the platform's tests keep what they check of the devices and drop the counts: the mode-setting tests their numbers of port writes, the handlers' test the data area's changed bytes and the end of interrupt's port write, and the vector's test the table's changed bytes. A run's repeat compares the state's digest, as it did besides the counts.
4. **`Counted` becomes `Field`** (D71), in the next commit: `Field<T>` in `Field.h`, `IsFieldValue`, the generator's output and its words, and `CountedTests` as `FieldTests`. The design record keeps the old name where it tells what was decided then.

### Measured 2026-10-13

| Check | Result |
|---|---|
| The idle trajectory | The probe of ADR-038 on the tree with no counting: 166 tests, 43,158,003 idles, each test's count and hash identical to ADR-038's. After the rename: identical again, but for the order the renamed `FieldTests` run in. |
| The suite | `GameLogicTests` passes under g++, 166 tests, the corpus's digests, pictures, outcomes and answers among them, with no re-base |
| `GameData` | `Tools/GenerateGameData.py --check`: up to date after regenerating, after each commit; the headers change only in their words and in `Field` for `Counted` |
| The rename | `Counted` is gone from `GameLogic`, `Tests`, `Outpost`, `Engine`, `Tools` and `Build`; the checkers, clang-format and clang 18's warnings over `GameLogic` are clean |
| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |

## What this forecloses

- **A count of changes as a test's measure.** A test that needs to know what a call changed compares the state before and after it.
- **The 128 KiB-aligned block.** A member finds nothing from its own address.
