# ADR-044 — Wholly native, the state: written by name, then free of the 1987 layout

**Status:** accepted 2026-10-14, with the change that implements it, step 1 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's rulings D76 and D78. Each later step of 2 to 5 amends it as it lands. **Amended 2026-10-14, step 4:** the start state is set in C++, and nothing loads the 1987 image. **Step 5:** the tables and the state's headers are source, and the image, the symbol table, the partition and the tools that read them go. **Step 2:** no struct of the state, and no run of its members, is read or written as bytes. **Step 3:** `Field<>` goes, and the state is plain data. The steps land in the order 1, 4, 5, 2, 3: once the digest no longer depends on the layout, the generators can go before the layout changes, and steps 2 and 3 are then edits of plain source.

## Context

The game's state is GameData: 29 groups of `Field`s at their 1987 offsets, generated from the symbol table, 14,420 bytes with no padding. Two things read it as bytes, by its layout: the state digest, which hashes GameData's bytes, and the commander file, which is the commander group's 208 bytes written out and read back over it. Every replay's digests and every saved commander depend on that layout, so the layout cannot change until neither does.

## Decision, step 1: the state written by name

1. **Each struct of the state lists its members** by name, in their order (`Visit`): every group, every struct nested in one, `GameData` over its groups, and `SpaceObject` over its fields. The generator writes the groups' lists; `SpaceObject`'s is by hand.
2. **`StateWriter` writes the state from those lists**: each byte and word little-endian, an array's elements in turn, a struct's members within it. `StateReader` reads it back. What they write depends on the members and their order, not on how a struct lays them out.
3. **The digest hashes the state as `StateWriter` writes it** (`GameStateDigest`), then video memory and the CGA's registers as before.
4. **The commander file is the commander as `StateWriter` writes it** (D78), the `.cdr` format: saved from it, and read over it, the file's bytes in place of the commander's for as many as the read gives, and the commander read back.
5. **DOS's service no longer refuses a buffer outside the game's state**, a guard the change count needed (ADR-024): the commander file is read into its own bytes, not the state's.

While the structs lay their members out as the 1987 data segment did, the state written by name is GameData's bytes, so nothing a digest or a file holds changes.

### Measured 2026-10-14, step 1

| Check | Result |
|---|---|
| Written by name is the bytes | `StateDigestTests.TheStateWrittenByItsMembersIsItsBytes`: GameData filled with a pattern unlike itself at every place is written by its members as the same 14,420 bytes; read back over a zeroed GameData, they are its bytes again; the commander writes 208 |
| The suite | `GameLogicTests` passes under g++, 185 tests, every digest, picture, outcome and answer of the corpus among them, with no re-base |
| Not run here | the MSVC build and the game: CI builds and runs the tests |

## Decision, step 4: the start state in C++

1. **`SetStartState` sets the state as the program starts** (`StartState.cpp`): each member the image's load left other than 0, by name, a text as its characters and a buffer as its bytes, written once from the image's load (1,066 of 14,420 bytes) and kept as source. `GameState::Start` zeroes the state and calls it; `Game::Start` takes the place of `Game::Load`, which could fail only if the image did.
2. **`ReferenceLoadImage` and `DataPlaces.h` go**, with the D5 byte and the image's three stored addresses the load translated; their meanings are in the start state already. `Tools/GenerateGameData.py` no longer writes `DataPlaces.h`.
3. **The re-base checks compare by member, not by 1987 offset** (`StateDumps.h`): a dump is each byte and word of the state, its path from GameData and its value (`StateLeaves`), and a re-base holds every one to the dump before the change by its path. `Tools/CompareStateValues.py`, which read the 1987-offset dumps, goes. This supersedes D73.

### Measured 2026-10-14, step 4

| Check | Result |
|---|---|
| The start state | the state `SetStartState` sets, written by its members, hashes to `b5d7f036…b397`, the hash of the state the image loaded, measured before the loader went; `StateDigestTests.TheGameStateHoldsTheGamesMemory` holds it there |
| The suite | `GameLogicTests` passes under g++, 185 tests, the corpus's digests among them, with no re-base |
| The re-base checks | a dump of the corpus by member (26 replays, 13,747 values a point) and a re-base against it pass, and leave every replay as it was |

## Decision, step 5: the tables and the headers as source

1. **The state's headers and the 18 tables' headers are source** (`GameData.h`, the 29 groups', `Message.h`, each `*Tables.h`): edited as any header is, with no generator behind them. Their opening comments no longer name the tools or the image.
2. **What only built them goes, with what only it read** (D79): `GameLogic/ReferenceImage.cpp`'s 13,289 lines and its header, `Design/Symbols.tsv`, `Design/DataPartition.tsv`, `Tools/GenerateGameData.py`, `Tools/GenerateTables.py`, `Tools/MapReference.py` and `Tools/PrepareCommanders.py`, and the two CI steps that checked the generated headers. The prepared commanders in `Replays/` stay, as the corpus's data.
3. **What the table generator checked of the tables, tests check** (`TableTests`): that every blueprint indexes within the vertex buffer, the fill patterns and its own edges, every face ending where its count says; and that every description text holds only the codes its expansion handles. The test of the image's hash, `ReferenceImageTests`, goes with the image, and `GalaxyTests` holds the ninth galaxy's seeds to the values the image held.
4. **AGENTS.md says the tree holds none of the reference's bytes**: a design question is measured against the owner's `ELITES.EXE` (D79).

### Measured 2026-10-14, step 5

| Check | Result |
|---|---|
| The suite | `GameLogicTests` passes under g++, 186 tests, the corpus's digests among them, with no re-base |
| What is left of the image | no file of `GameLogic`, `Tests`, `Outpost`, `Engine`, `Tools` or `Build` names the image, the symbol table, the partition or a tool that read them; the checkers are clean |

## Decision, step 2: no bytes of the state

1. **Every struct the 1987 layout cut into gaps and fields is a record or an array** of what it holds, its members in the same order: one `SystemRecord` for the current, the selected and the hyperspace target's system, which an arrival and the countdown copy whole; the hold as 17 lots of held and on sale, then the refugees and the tonnes in use; the credits one double word (`Field<std::uint32_t>`); the screen prices, the chart items, the stardust, the stored sines and cosines and the circle's points records of words; the vertex buffer 126 words; the behaviour classes' counts, the keys held, the market's random state, the distance digits and the texts arrays. The keys are named by scan code (`Keys`).
2. **No run of members is read as one**: the cash's text holds its NUL, the selected name its ninth byte; the equipment items are a table of member pointers (`EQUIPMENT_ITEMS`), which the shipyard's rows, the status screen and the equipment loss index; the dashboard's cache is set member by member; the status screen's title is read from the rank on, member by member, to its NUL.
3. **Whole records are copied and cleared by their members** (`CopyMembers`, `ZeroMembers`, `StateWriter.h`): the slots, the system records, the commander and its startup copy; a missile launched past a buffer is read from the bytes there as a record's members.
4. **What remains bytes is bytes**: texts and buffers are arrays of bytes, `BytesOf` takes nothing else, and `BytesThrough`, the offset macros, their static_asserts and `GameState::DataBytes` go. The vertex buffer is still reached at the blueprints' byte offsets, which are even (`TableTests`); the drawing buffer and video memory stay CGA's (D76); the DOS transfer area goes with `DosFiles` at step 7; the constant tables read as words are not the state, and stay for a later step.

The members keep their order and their widths, so the state written by its members, every digest and the commander file are what they were.

### Measured 2026-10-14, step 2

| Check | Result |
|---|---|
| The suite | `GameLogicTests` passes under g++, 186 tests, the corpus's digests, pictures, outcomes and answers among them and the start hash, with no re-base |
| What is left | no `BytesOf` of anything but a byte array, no `BytesThrough`, no `GAME_DATA_OFFSET`, `COMMANDER_STATE_OFFSET` or `SPACE_OBJECT_OFFSET`; the checkers are clean |
| Not run here | the MSVC build and the game: CI builds and runs the tests |

## Decision, step 3: no `Field<>`

1. **Every member is a plain type at the width it had** (`std::uint8_t`, `std::uint16_t`, `std::uint32_t`, or an enumeration of a byte or a word), so every sum wraps where it did. `Field.h` goes, with its rules: a member copies from another by assignment, and a struct copies and clears as a value (`a = b`, `slot = {}`); `CopyMembers` and `ZeroMembers`, which stood in for that, go.
2. **The structs are aggregates with no packing**: their explicit constructors and `#pragma pack` go, so a struct lays its members out as the compiler likes. Nothing reads the layout (step 2); the state written by its members is 14,420 bytes whatever it is.
3. **The last twelve members named by their 1987 addresses are named for what they hold**: the commander file's signature, the rank's end, a spare byte, the flag that lets the missions count in the first galaxy, the data screen's tech-level digits, the status screen's fuel and missile digits, and the market's three walk indexes.
4. **`FieldTests` becomes `GameDataTests`**, its tests of the copy rules gone; `StateDigestTests` holds the writer to 14,420 bytes and to reading back what it wrote, in place of GameData's memory.

### Measured 2026-10-14, step 3

| Check | Result |
|---|---|
| The suite | `GameLogicTests` passes under g++, 186 tests, the corpus's digests, pictures, outcomes and answers among them and the start hash, with no re-base |
| What is left | no `Field`, no `#pragma pack` in `GameLogic`; the shell's sources compile under mingw-w64 g++; the checkers are clean |
| Not run here | the MSVC build and the game: CI builds and runs the tests |

## What this forecloses

- **The state's layout as a format.** What the digest and the commander file hold is the members in their order; a struct may lay them out as it likes.
- **Loading the state from the 1987 image.** The start state is source.
- **Generating the tables or the state from the 1987 program.** Both are source, and a change to either is an edit.
