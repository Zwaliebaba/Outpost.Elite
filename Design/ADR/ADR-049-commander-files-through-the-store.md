# ADR-049 — The commander files go through the store: no DOS between the game and its files

**Status:** accepted 2026-10-14, with the change that implements it, part of step 7 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D78.

## Context

Since D27 the disc menu's requests reached the platform's `FileStore` through `DosFiles`, DOS's file half ported call for call: handles from 5, CF and AX on every path, a transfer area of 2Bh bytes in the game's state that a search filled and the listing read back (the found name at 1Eh, the attributes at 15h), and a file's stamp from DOS's clock. ADR-041 kept all of it, because the game observed it. D78 rules that the commander file keeps its `.cdr` format through an explicit serializer; what stood between that serializer and the store was DOS, and nothing of the game needs DOS once the handle and the transfer area are not its state.

## Decision

1. **The four requests call the store.** `LoadCommanderFile`, `SaveCommanderFile`, `DeleteCommanderFile` and `ListCommanderFiles` take the `FileStore` (`Hardware::Files`) and use its `Attribute`, `Open`, `Create`, `SetAttribute`, `Delete` and `List`. Any error the store gives is a failure, `diskError`, which the disc menu shows as before. A read or write that moves fewer bytes than asked is not a failure, as it was not under DOS.
2. **What each did under DOS, it still does.** A load refuses a file with any attribute; a save creates the file with none, writes it and clears its attributes again; the listing takes, in the store's order, the files with no attribute whose extension is `.CDR`, up to 40, and an empty disc is no failure, as DOS's "no more files" was not.
3. **The handle and the transfer area leave the state.** `disk.fileHandle` and `saveLoad.diskTransferArea` go, 45 bytes: the state is 14,340, its start hash `2cd124d6…877d`. So does the listing's search attribute, which the original took from what `RestoreTimerInterrupt` left in CX, a byte of the BIOS's tick count less a day; the listing skipped every file it found with an attribute, so which ones the search found never mattered. `RestoreTimerInterrupt` no longer reads the time of day.
4. **A file's stamp is the platform's** (`Platform::FileStampNow`): DOS's calendar at the clock, from the start moment, by `MomentAfter` and `StampOf`, which moved from `DosFiles` to `FileStore.h` unchanged.
5. **`DosFiles` goes**, with `DosAnswer`, the `.cdr` search pattern, `Hardware`'s eleven DOS calls and its read of the time of day and `DosFilesTests`; the store's own tests stay.

## What moved, and how it was measured

The state was dumped by member at every digest point of the corpus and the twins before and after the change and compared by path, the 45 bytes aside:

| What | Result |
|---|---|
| Members that moved | none, at any point of the 26 replays or of the twins |
| Commander files written | unchanged: `TwinQuit`'s `X.CDR` matches its recorded SHA-256, and `save-and-load` loads what it saved |
| Pictures and outcomes | none moved |

Every digest moved, as the state is 45 bytes shorter. They were re-based from the dump after the change, once the comparison above held, and recorded by `OUTPOST_ELITE_RECORD_GUARD`; a second recording reproduced every file byte for byte. The suite passes in check mode under g++: 169 tests, the seven of `DosFilesTests` gone.

| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |
|---|---|

## What this forecloses

- **DOS's file semantics as the game's.** Handles, the transfer area, find-first and find-next, and DOS's error codes as results are gone; what the game sees of a file is what the store answers.
- **A search attribute.** The listing has no attribute to search with: a commander file with any attribute is never listed or loaded, as before, and no longer because of what the clock left in a register.
