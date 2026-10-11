# ADR-021 — The game's files native, on a Platform the Game owns (D27)

**Status:** accepted 2026-10-11, with the change that implements it: `Elite::Platform` and `Elite::DosFiles`, and `Hardware`'s file operations moved off the emulated DOS. It is Phase 5's step 1 (D27). D33 then replaced the rest of D27's steps with its route, and this step stays ([Reverse-Engineering-Plan.md §5](../Reverse-Engineering-Plan.md#phase-5--beyond-the-original-outside-this-plan)).

## Context

**How the game reached its files.** Through eleven `Hardware` operations:
- set the transfer area;
- create, open, read, write, close and delete;
- read and set attributes;
- find-first and find-next.

Each ran int 21h on the `Pc`: `CallService` → `Pc::CallInterrupt` → `PcServices` → `Machine::Dos`, over the host's `Machine::FileStore`. `SaveLoad` uses them for commanders and the catalogue, and `Video` for screenshots.

**Why a home was needed first.** A `Hardware` is rebuilt for each entry's call (`Guest`), so it holds no state. A native file service needs somewhere long-lived for its open handles and its transfer area, and so will the devices that follow it.

## Decision

**1. `Elite::Platform` is the native platform, and the `Game` owns it.**
- **Its contents.** For now it holds the file service. The devices that become native later join it.
- **Construction order.** The `Game`'s members are, in order: the `GameState`, the `Platform`, the `Pc`, the loaded program.
  - The `Platform` writes through the `GameState`, so it comes after it.
  - The `Pc`'s hooks reach the `Platform`, so the `Pc` is built after it and destroyed first.
- **The store.** The `Game` gives the same `FileStore` to the `Platform` and to the `Pc`, so no host changed. The shell changed by 0 lines.
- **How native code reaches it.** `Hardware(Pc&, Platform&)`: the hooks capture the `Platform` beside the `GameState`, and `Guest` passes both.

**2. `Elite::DosFiles` is `Machine::Dos`'s file half, ported call for call.**
- **What it holds:**
  - the 20-entry handle table, handing out 5 to 19;
  - the transfer area's address, PSP:0080 until the program sets one.
- **The search in progress** stays where DOS keeps it, in the transfer area's reserved bytes 00h-14h.
- **Memory.** Reads and the transfer area are written through the `GameState`'s accessors, so paced time's change count sees every byte, as `Memory::Load` and DOS's writes made it.
- **Stamps** take DOS's time at the machine's clock (`Pc::Clock`), as before.

**3. What was measured on the emulated DOS, and is matched.** A probe logged every memory write during 288 int 21h file calls, 111 of them in the corpus, plus the tests:
- **No byte is written twice:** 9,062 writes to as many addresses.
- **No write outside the data segment.** Each went either to the transfer area (DS:206F-2099: 43 bytes after a find, 21 after a failed one) or to `commanderBlock` (208 bytes a read). The only route outside the `GameState` is a find before any set-transfer-area, into PSP:0080. The game never takes it, and the native side matches it anyway.
- **No time passes and no port is touched.** The clock is the same before and after every call.

**4. Three differences, which the game reaches none of:**
- **AX and CX are 0.** Where the emulated DOS left AX or CX as they went in (a successful close, delete or set-attributes; CX after a failed read of attributes), the native call returns 0. No caller reads them.
- **The fault cases throw `std::logic_error`.** These are a close, read or write on handles 0-4, and a write of 0 bytes, which stopped an emulated run as a fault. The game only uses handles from a successful open or create.
- **The emulated program exit no longer closes the native handles.** The game never holds a file open across a call.

**5. A side-by-side test while both exist** (`DosFilesTests`, 7 tests). Each runs the same calls on the emulated DOS and on `DosFiles`, each with its own rig and store, and compares after every call:
- CF, AX and CX;
- the change count;
- all 1 MiB of memory;
- at the end, the two stores' files.

**What the sequences cover:**
- save and load at the game's own places, including a load over the same bytes, which must change none;
- a search over hidden, system, read-only and directory entries, and patterns the store refuses;
- a find into the PSP;
- delete and attributes;
- every error the game can reach, and those it cannot;
- a transfer across the data segment's end;
- DOS's clock across a year's end.

Every test also states its expected values outright, so it outlives the emulated DOS. Two deliberate breaks fail it:
- names padded with spaces instead of NULs;
- each byte of a read written twice.

**6. Measured 2026-10-11.**

| Check | Result |
|---|---|
| `GameLogicTests` | 56 of 56 pass under g++ and clang++ (49 before, plus 7 new) |
| `MachineTests` | 133 of 133 pass under g++ and clang++ |
| clang++ warnings | none |
| The corpus | all 26 replays meet all 134 digests under `ReferenceRunner`, built with each compiler |
| The known answers | each of the 356 is checked once and met (the worker's probe build) |
| SaveLoad's twins | their files' SHA-256s still match |
| clang-tidy | clean on every changed `.cpp` |

**7. What the platform swap inherits.** `DosFiles` still uses `Machine::FileStore`, `Machine::Dos::DateTime` and `Machine::Cycles`, and its clock still comes from `Pc::Clock()`. These move when `Machine` goes (D33, piece 2).

## What this forecloses

- **A device kept in `Hardware`.** A device's native state lives in the `Platform`; `Hardware` stays the stateless boundary.
- **The game's file calls going back through int 21h.**
