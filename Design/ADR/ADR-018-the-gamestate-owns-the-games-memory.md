# ADR-018 — The GameState owns the game's memory (D19, D23)

**Status:** accepted 2026-10-10, with the change that implements it: `GameData`, generated from the symbol table; `Elite::Game`, which owns a `GameState` and the `Pc` that maps it; and `Machine::Memory::Map`. It records D19 and D23 ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)), and settles the choice ADR-015 item 5 left to D19's ADR. **Amended 2026-10-11 by [ADR-023](ADR-023-the-platform-swap.md):** `Memory` and its mapping are deleted; the `GameState` counts its own changed bytes, and the `Game` owns the `Platform` in the `Pc`'s place (D33). **Amended 2026-10-11 by [ADR-024](ADR-024-members-that-count-their-changes.md):** `GameData`'s members are `Counted` fields and native code writes them as members; the `GameState` holds `GameData` in a block with its change count, allocated at 128 KiB alignment, and the ship slots are `SpaceObject` records. **Amended 2026-10-11 by [ADR-026](ADR-026-the-data-segment-distributed.md):** `GameData` is no longer the 1987 layout but 30 groups of state, loaded from the image by name, and the read-only tables are `constexpr` outside the state (D38). **Amended 2026-10-12 by [ADR-030](ADR-030-change-3c-nothing-by-address.md):** the `GameState` answers nothing by the original's address: its view of the 1987 layout, the accessors by offset and the far ones are deleted.

## Context

**What D19 rules.** The game's state becomes plain structs, generated from the symbol table at the original layout. Each field sits at its 1987 offset, checked at compile time, and the `GameState` owns them. The digests and the save files read their bytes, so neither changes. A natural layout is Phase 5's.

**What D23 rules.** Phase 4 ends with two things:
- D19: the `GameState` owns the game's memory;
- D3's generated tables, so that `ELITES.EXE` is not read at runtime.

The native devices of D17's third step move to Phase 5. Until then the emulated devices stay behind `Hardware` (ADR-014). They read and write the game's memory by linear address: the loader, DOS's file reads and writes, the BIOS's teletype, the CGA's picture, and the stack's pushes and pops.

**What was there before.** The `GameState` was a view: a reference to the `Pc`'s `Memory` and two segment numbers, built again for each entry's call. Every read and write went through `Memory`, which owned all 1 MiB.

## Decision

**1. The data segment is a generated struct, `GameData`** (`GameLogic/GameData.h`, written by `Tools/GenerateOverlay.py`).
- **The layout.** It is packed, and exactly 0x10000 bytes. Every DS data, table and text row of `Design/Symbols.tsv` is a member at its offset.
  - A row whose children tile it is a nested struct named for it. `commanderBlock` holds its 93 rows, and `selectedSystemName`, `creditsTenths` and `cargoHold` are nested inside it.
  - A word with a named byte inside it is an anonymous union of the word and a `<Word>Bytes` struct, for example `rollRateBytes.pitchRate`.
  - Bytes no row names are gaps, named for their offset (`gapAD70`).
- **The figures.**

  | What | Count |
  |---|---|
  | Named members | 659 |
  | Gaps | 146, of 12,910 bytes; the largest is `gapEFE0`, the 4,128 bytes past the image |
  | Nested structs | 22 |
  | Unions | 5: `systemSeed0`, `systemSeed1`, `systemSeed2`, `messageFrames`, `rollRate` |
  | Assertions in `GameData.h` | 838: 810 member offsets, 27 struct sizes and the 64 KiB |
  | Cross-checks in `DataOverlay.h` | 659: each `DS.name` field's offset equals its member's |

- **The overlay stays.** Native code still names fields through `DataOverlay.h` (`DS.playerDocked`). The cross-checks make the two fail to compile if they drift; a drifted field was tried, and it fails.
- **`--check`** covers both headers, and CI runs it.
- **Offsets under Clang.** The assertions use `GAME_DATA_OFFSET`, which is `__builtin_offsetof` under Clang and `offsetof` elsewhere. CI's clang-tidy reads the MSVC headers, where `vcruntime.h` defines `offsetof` as a `reinterpret_cast` of a null pointer. MSVC folds that to a constant; Clang does not. Forcing that definition ahead of the headers reproduced it: Clang stopped at its 20-error limit before the macro, and finds no error with it.
- **No references.** Native code reads and writes the bytes through the `GameState`'s accessors, never through a reference to a member, which may be unaligned.
- **The generator's parser, corrected.** It read `frameCorners` ("word cell offset, byte CP437 character") as two bytes an entry; it is three. It also sized `pauseMenuText`, whose entries are zero-terminated. Records of words and bytes are now read, and zero-terminated elements are refused. So `frameCorners` became `DataTable<3, 6>` and `pauseMenuText` became `DataAt`. Nothing reads either through `At()`, so no native code changed.
- **85 rows have no size in the symbol table,** and each runs to the next row. Two of them are containers whose size is missing:
  - `shipSlots` comes out as 64 bytes, but holds the 36 slots of 64 bytes from `planetSlot` to `debrisSlots`;
  - `protectionInput` comes out as 1 byte, but is the whole input buffer, to DS:25E0.

  Both are therefore flat in `GameData`, with their children as siblings at the right offsets. Native code uses only their `.offset`. Giving them sizes in `Symbols.tsv` would nest them, with no other change. *D24's clean-up gave them their sizes from the listing: `shipSlots` 2,304 bytes (36 slots of 64, the count `SetUpLocalSpace` writes and `ClearAllObjects` clears), `protectionInput` 24 bytes (DS:25C9-25E0). Both nest now. That makes 148 gaps of 12,975 bytes, 24 nested structs and 842 assertions; no native code changed. The sun's slot (DS:6930) and the buffer's capacity byte (DS:25C9) stay unnamed gaps, as the first bytes of all 29 containers are: the symbol table holds one row per address, so a container and the row that starts it cannot both be named. Changing that is a table-format decision, not made.*

**2. The `GameState` owns three regions.**
- **What they are:**
  - the data segment, as `GameData`;
  - the code segment's image, 8F40h bytes. Native code reads data from it: the noise source, the triangle filler's opcode words, and the bytes at divide returns. It also writes to it: the saved timer vector, the divide trap's scratch, the filler's patches, and `WipeProgram`;
  - the CGA's 16 KiB of video memory.
- **`Elite::Game` owns it** (`GameLogic/Game.h`). A Game holds the `GameState`, then the `Pc`, then the loaded program.
  - `Game::Load` loads the reference as `LoadReference` does, maps the `GameState` into the `Pc`, and installs the native routines on it.
  - The hooks capture the one `GameState`. `Guest` takes it rather than building one.
- **Lifetime by construction.**
  - Only a Game can map a `GameState`: `GameState::Map` is private, with `Game` its friend.
  - The `Pc` is a later member than the `GameState`, so it is built after it and destroyed before it.
  - A Game is over 100 KiB, so hosts make it on the heap.
- **The hosts.**
  - The Windows shell, `ReferenceRunner` and the tests' `ReferenceRig` each make a Game.
  - `ReplayPlayer` and the digest take the `GameState`.
  - Eight twin set-ups that built a `GameState` from the PC's memory now use the rig's.
  - Native subsystem code did not change.

**3. `Machine::Memory` maps a region to storage it does not own.**
- **The table.** `Memory` keeps one pointer for each of the 65,536 paragraphs. Every read and write, by linear address or by segment and offset, looks up its paragraph.
- **`Map(first, storage)`.**
  - **What it refuses.** A region that is not whole paragraphs from a paragraph boundary, not inside the 1 MiB, or overlapping one mapped before, throws `std::invalid_argument`.
  - **What it does.** It copies the bytes already there into the storage, uncounted, and points the region's paragraphs at it.
- **The order.** The reference is loaded first, and mapping takes over what the load wrote. The segments come from the loaded program (`loadSegment`, `DataSegment(program)`), and video memory from `VIDEO_SEGMENT`, which a `static_assert` ties to `Cga::VIDEO_MEMORY_LINEAR`.
- **What stays in `Memory`'s own bytes:** the vector table, the BIOS data area, the PSP, the environment and the ROM. `Memory::Bytes()` is now only those bytes; nothing that reads the game's state uses it.
- **`Machine` knows nothing of `GameLogic`** (AGENTS.md R9).

**4. The change count stays where it is, and ADR-015 item 5's choice is not yet needed.**
- **Writes.** Every write the `GameState` makes still goes through `Memory::Write8`. Paced time's change count (ADR-015 item 5) therefore counts exactly what it counted: a byte write that changes the value, anywhere.
- **Reads.** A read changes nothing. So `Get`, `Byte`, `Word`, `Code*` and `Video*` read the `GameState`'s own bytes; a word at DS:FFFFh still takes its high byte from offset 0. `Far*` reads, and code or video reads past their region, go through `Memory`, because they can land anywhere in the 1 MiB.
- **What ADR-015 item 5 asked.** It expected D19's structs to replace `Memory`, which would take the change count with it, and left to this ADR the choice of another way to tell a turn that changed nothing. Under D23 the structs do not replace `Memory`; the devices still read the game's memory through it. The count is unchanged, and the choice moves to Phase 5's native platform, the first change in which writes stop going through `Memory`.

**5. The digest reads the `GameState`** (`GameLogic/StateDigest.cpp`). It hashes the same bytes in the same order:
- DS:0000-AD5F and DS:B160-FFFF, from `GameData`;
- the 16 KiB of video memory;
- the CGA's registers.

A `.cdr` file is still `commanderBlock`'s bytes, written and read through DOS on the mapped memory. Two stale comments were corrected; nothing hashed changed:
- `PatchStep` does rewrite the triangle filler's instructions. The code segment is still left out of the digest, because nothing reads the patched sites back.
- The stack range the digest skips also holds `triangleEdgeMasks` (DS:AD60-AD6F), which is constant.

**6. Measured 2026-10-10.**

| Check | Result |
|---|---|
| `GameLogicTests` | 48 of 48 pass under g++ and clang++. New: `StateDigestTests.TheGameStateHoldsWhatThePcMaps`. |
| `MachineTests` | 133 of 133 pass under g++ and clang++. New: `MemoryTests.MappedRegionReachesItsStorage` and `MemoryTests.MapRefusesWhatIsNotWholeParagraphsOrOverlaps`. |
| clang++ warnings | none, with `-Wimplicit-int-conversion -Wshadow` |
| The corpus | all 26 replays meet all 134 digests under `ReferenceRunner`, built with each compiler |
| The known answers | each of the 356 is checked once and met. A probe build printed every check: 356 checks, 356 distinct answers, none unmet, none unchecked. |
| The corpus's time | +5%. g++ Release `ReferenceRunner`, all 26 replays in sequence, 15 alternating rounds on a shared 4-core machine: median wall time 2.230 s before and 2.343 s after; median user time 2.015 s and 2.119 s. The paragraph lookup on every access through `Memory` costs more than the direct reads save. |
| clang-tidy | clean on every changed `.cpp` outside the shell |
| The checkers | `GenerateOverlay --check`, CheckFormat and CheckProjectFiles are clean |
| MSVC | not built here: CI's Debug build is the first MSVC build of `GameData.h`'s `#pragma pack` and offsets, and of the shell's change |

**7. For D3, the generated tables.** *Done: [ADR-019](ADR-019-the-reference-as-generated-tables.md).*
- **Where the image comes from today.** Each host reads `ELITES.EXE` from disk, and `LoadReference` checks its SHA-256. `ExeLoader` then:
  - writes the 98,080-byte image at load segment 0823h;
  - applies its 7 relocations;
  - writes the PSP, the environment and the entry frame;
  - takes the entry registers and the allocation from the MZ header: CS:IP 0000:0000, SS:SP 13CA:03F8, minimum allocation 28 paragraphs, maximum FFFFh.

  `LoadReference` then writes D5's byte at DS:25E4.
- **The relocations** all fall in the code segment, at CS:0007, 020A, 021E, 02F2 and 060E (08F4h plus the load segment), 7B5D (0000h plus it) and 7BE3 (140Ah plus it). None is in a range native code reads.
- **What reads the image:**
  - all of DS:0000-EFDF, through `DS.*`: tables, texts, blueprints, fonts, the sine and tangent tables, the initial variables and `cockpitImage` (segment CS+140Ah, which is DS:B160);
  - in the code segment: the noise source (CS:07D0-0BCF), the triangle filler's opcode words (CS:1CF3-1CFA and CS:1FF8-2017), and the bytes at seven divide returns.
- **The code segment's image must be generated whole and byte-exact,** not only the ranges read. `WipeProgram` writes CS:0564-8F30, and the change count counts a write only where it changes a byte. An image with zeros where the original has code would change the count, and paced time with it.
- **A replacement for the SHA-256 check:** a test that the generated image equals the one `ELITES.EXE` loads.

**8. D23 is recorded here.** The plan's §5 and its D17 row say the native devices are Phase 5's. ADR-014 item 3 says `Hardware`'s implementation is still the emulated devices until then.

## What this forecloses

- **A `GameState` that is a view built per call.** There is one per run, owned by a Game.
- **A `Pc` that outlives the storage it maps,** and mapping by anything but a Game.
- **Native code holding references to `GameData`'s members.**
- **A natural layout in Phase 4** (D19).
- **Moving the change count before the writes stop going through `Memory`** (item 4).
