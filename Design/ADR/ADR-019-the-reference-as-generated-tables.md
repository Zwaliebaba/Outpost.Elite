# ADR-019 — The reference's image as generated tables (D3, D23)

**Status:** accepted 2026-10-10, with the change that implements it: `Tools/GenerateReference.py`, the generated `GameLogic/ReferenceImage.h` and `.cpp`, `ExeLoader` split into parsing and placing, and every host loading the generated image. It records the last change D23 ends Phase 4 with ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)). **Amended 2026-10-11 by [ADR-025](ADR-025-addresses-become-what-they-mean.md):** `Tools/GenerateReference.py` is deleted; `ReferenceImage.cpp` is checked-in source, and `Tools/GenerateTables.py` generates typed tables from it per subsystem.

## Context

**What D3 rules.** Generated tables are checked in (ADR-002 item 3). The shipping executable embeds them and does not need the reference at run time.

**What D23 rules.** Phase 4 ends with D19, done in ADR-018, and with D3's tables, so that `ELITES.EXE` is not read at runtime.

**What D19 rules.** The layout stays the original's, and a natural layout is Phase 5's. So the tables are the original's bytes at their original offsets, annotated by symbol. Ship blueprints, fonts and texts as types of their own come with the natural layout.

**What was there before.** Each host found `ELITES.EXE` on disk:
- the shell, beside its executable or in the repository it was built from;
- `ReferenceRunner`, from `--exe` or the working directory;
- the tests, in the repository.

`LoadReference` checked the file's SHA-256, and `ExeLoader` parsed its MZ header and placed it. ADR-018 item 7 surveyed what the load needs.

## Decision

**1. The load image is generated** (`Tools/GenerateReference.py`).
- **Its source.** It reads `ELITES.EXE` and refuses it unless its SHA-256 is `REFERENCE_SHA256`. It reads that hash, `CODE_SEGMENT_BYTES` and `DATA_SEGMENT_PARAGRAPH` from `GameLogic/Reference.h`, so there is one copy of each.
- **`ReferenceImage.h`** (45 lines) holds what the MZ header says the load uses:
  - entry CS:IP 0000:0000 and SS:SP 13CA:03F8, relative to the load segment;
  - the allocation, 001Ch to FFFFh paragraphs;
  - the image's size, 98,080 bytes;
  - the 7 relocations, each commented with its site, the routine it is in and the word it holds.

  It also declares the image and `ReferenceExecutable()`, which describes the reference as `ExeLoader` places it.
- **`ReferenceImage.cpp`** (13,302 lines, 847,082 bytes) defines the image once.
  - **Coverage:** the whole image, the code segment CS:0000-8F3F, then the data segment's image DS:0000-EFDF.
  - **Byte-exact and unpatched:** DS:25E4 holds 00. The code segment is whole, because `WipeProgram`'s writes are counted only where they change a byte (ADR-018 item 7).
  - **Readable, not a blob:**
    - eight bytes a row, each row ending in its address;
    - a row ends where a row of `Design/Symbols.tsv` begins, and all 1,202 rows are named where their bytes start (`// CS:07D0 noiseSource`, `// DS:25E4 protectionShown`);
    - the 105 text rows show their text.
  - **Compilation:** g++ -O2 takes 0.8 s. The image is read-only data, with no dynamic initializer.
- **Layout.** The rows are not bin-packed, because clang-format 18 needs 40 s and 7.1 GiB to lay out a bin-packed initializer of this size; `// clang-format off` is worse. Ending each row with a trailing comment makes it columns, which takes 0.74 s and 184 MiB. The generator emits exactly what `CheckFormat --fix` leaves. Comments are capped at 90 characters, because at 91 clang-format reflows every row.
- **`--check`** fails on a stale output: it was tried with one byte edited. CI runs it in the Windows job, beside the data overlay's check.

**2. `ExeLoader` places a description as well as a file.**
- **`Machine::Executable`.** A plain, generic MZ description:
  - the entry registers;
  - the allocation;
  - the image, as a span it does not own;
  - the relocations, as `Machine::Relocation {offset, segment}`.
- **`ExeLoader::Parse(file, executable)`** reads a file into one. It fails with `NotExecutable` or `Truncated` and changes nothing.
- **`ExeLoader::Load(memory, executable, desc, program)`** places one, as the file load did:
  - the image through `Memory::Load`, so the change count sees the same writes;
  - then the relocations, the PSP, the environment, the allocation and the registers.

  On failure it writes nothing.
- **`Load(memory, file, desc, program)`** is parse, then place, with the same errors in the same order. It is no longer `noexcept`, because parsing allocates the relocations.
- **`Pc::Load`** takes either. `Machine` knows nothing of *Elite* (R9).

**3. No host reads `ELITES.EXE`.**
- **`LoadReference` and `Game::Load`** load `ReferenceExecutable()`, then write D5's byte with `PatchByte` as before (ADR-001). Both return `bool`. `ReferenceFailure` and its `NotTheReference` are gone, because nothing at run time can be the wrong file.
- **The shell** loses `FindReference` and the read. Its one failure message is "The game would not load."
- **`ReferenceRunner`** loses `--exe`. Its usage is `ReferenceRunner [--out DIR] [--steps TEXT | --replay FILE]`.
- **The tests' `ReferenceRig`** loads the tables and asserts the load. The nine "ELITES.EXE at the repository root" assertions went with `Loaded()`.
- **What still reads the file:**
  - the generator;
  - the test in item 4;
  - `MachineTests`' four loader tests on the reference, because `Machine` cannot use `GameLogic`'s tables;
  - `Tools/MapReference.py`, `ReferenceScreens.py` and `ScanReference.py`.

  So `ELITES.EXE` stays at the root (ADR-002 item 2), as the tables' source and the test's reference. *Amended by D28 ([ADR-020](ADR-020-elites-exe-leaves-the-tree.md)): the file left the tree. The generator reads a local copy, the loader tests use synthetic files, and CI no longer runs `--check`.*

**4. The tables are checked against the file.** *Since D28 the test holds the image to the SHA-256 measured from the file instead (ADR-020 item 2), as `TheGeneratedImageIsTheReferences`.* `ReferenceImageTests.TheGeneratedImageLoadsAsTheReferenceDoes`:
- reads `ELITES.EXE` and asserts its SHA-256;
- loads the file into one `Pc` and the tables into another, with the same descriptions;
- compares all 1 MiB, the change count, every field of `LoadedProgram` and the registers.

With DS:7B50 changed in the generated source, it fails at linear 18CC0h, which is DS:7B50 at load segment 0823h.

**5. Measured 2026-10-10.**

| Check | Result |
|---|---|
| `GameLogicTests` | 49 of 49 pass under g++ and clang++ (the new test is the 49th) |
| `MachineTests` | 134 of 134 pass under g++ and clang++ (new: `ExeLoaderTests.ParsesAFileIntoTheDescriptionItLoads`) |
| clang++ warnings | none, with `-Wimplicit-int-conversion -Wshadow` |
| The corpus | all 26 replays meet all 134 digests under `ReferenceRunner`, built with each compiler |
| The known answers | each of the 356 is checked once and met (a probe build printing every check) |
| No read at run time | every replay, under strace, with `ELITES.EXE` hidden: no call names it, and every digest is met. The previous runner under the same conditions fails to open it and exits 2. |
| clang-tidy | clean on every changed `.cpp` outside the shell, the generated one included (2.5 s) |
| The checkers | both generators' `--check`, CheckFormat and CheckProjectFiles are clean |
| MSVC | not built here: CI's Debug build is the first MSVC build of the 98,080-byte initializer and of the shell's change |

## What this forecloses

- **Reading `ELITES.EXE` at run time,** or shipping without the generated image.
- **Editing the generated image by hand.** It changes only by regenerating it from the file, and the test and `--check` hold it to the file.
- **A partial code segment,** while paced time counts `WipeProgram`'s writes.
- **Typed tables in Phase 4.** Blueprints, fonts and texts as types of their own come with Phase 5's natural layout (D19).
