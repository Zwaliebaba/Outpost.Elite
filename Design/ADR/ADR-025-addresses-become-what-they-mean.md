# ADR-025 — Stored addresses become what they mean, and the code segment leaves the game's memory (D33)

**Status:** accepted 2026-10-11, with the change that implements it: generated typed tables, messages, slot indexes and indexes in place of the addresses the data segment held; native code reaching the data by member and record instead of by offset; the code segment's image out of the game's memory; the copy protection deleted; and the state digests re-based. It is the second of the natural layout's three changes, piece 3 of D33's route ([Reverse-Engineering-Plan.md §5](../Reverse-Engineering-Plan.md#phase-5--beyond-the-original-outside-this-plan)).

## Context

After change 1 (ADR-024), native code read and wrote the game's state as members, but much of that state was still addresses:
- words pointing into the data segment, the code segment or video memory;
- tables of such words;
- offsets handed from routine to routine.

What remained of `DataOverlay.h`'s use was these: 497 `DS.` uses, and 453 byte and word accessor calls through held offsets. The code segment's image was still in the game's memory, for the divide trap's answers, the filler's opcode words, its own patches and the noise source. The copy protection, which the D5 byte makes unreachable, was still there.

A converted word changes value: an offset becomes an index. So the 1987 state digests move, and D32 allows them to be re-based where the layout changes, which ADR-022 item 6 said this piece would record.

## Decision

**1. The reference's tables of addresses are generated typed tables.**
- **The generator.** `Tools/GenerateTables.py` reads the checked-in image, `GameLogic/ReferenceImage.cpp`, and `Design/Symbols.tsv`. It has a `--check` mode, which CI runs.
- **Where the tables go.** Each subsystem's tables go into its own `GameLogic/<Subsystem>Tables.h`, included by that subsystem's `.cpp` alone, with `Message.h` shared (D38).
- **What became of each table:**

| Table | What it is now |
|---|---|
| 13 text tables (`economyNames` … `diskOperationNames`) | `std::array<std::string_view, N>` |
| 195 description phrases and their 39 lists | phrase indexes |
| 30 ship blueprints | a shape and the parts of its body |
| `frameCorners` | `FRAME_CORNERS`: row, column, character |
| 53 messages and the warnings | `enum Message`, with `MESSAGE_TEXTS` and `WARNING_MESSAGES` |
| 84 texts that no symbol starts and nothing writes | constants in their subsystem's header |
| the noise source, CS:07D0 | `NOISE_SOURCE`, 1,024 bytes |
| `textControlCodes`, `warningChecks`, `behaviorHandlers` | not read: the control code dispatches directly, and native code never read the other two |

`Tools/GenerateReference.py`, which read `ELITES.EXE`, is deleted with `REFERENCE_SHA256` (D36). Nothing in the tree reads the file now. `ReferenceImage.cpp` is checked-in source, held to its measured hash by `ReferenceImageTests` (ADR-020).

**2. The stored addresses hold what they mean.**

| Field(s) | Now |
|---|---|
| `messagePointer`, `messageShown`, `warningMessage` | `Message` |
| `missileTarget`, `playerHitBy`, `maskShipSlot`, a slot's `target` and `owner` | `SlotIndex`, with the sentinels named: player or none 0, police 1 |
| `chartItemEnd`, `chartLabelCursor`, a chart label's +4 word | indexes |
| `commanderFileListNext`, `cargoRowPointer`, `data8C10`-`data8C14`, `keyBufferWrite`, `keyBufferRead`, `musicPointer` | indexes |
| `menuFirstRowAttr` | a row |
| `spawnOddsOffset`, `spawnLimitOffset` | the government |
| `pspSegment`, `fixedEdgeList`, `faceEdgeList`, the saved int 0 and int 9 vectors | not written: nothing read them. The vectors are put back from the BIOS's own |
| `savedTimerVector` (CS:025A) | gone: the timer chains to the BIOS's handler |

`ReferenceLoadImage` loads the image's three non-zero stored addresses (`messagePointer`, `keyBufferWrite`, `keyBufferRead`) as what they mean, after checking that each held its original address.

**3. Native code reaches the data by member and record.**
- **Before and after.** `DS.` uses in `GameLogic` go from 497 to 0. Byte and word accessor calls through offsets go from 453 to 47.
- **What remains goes in change 3.**
  - The 47 calls: the drawing buffer's addresses (Video and Text), the description writer and printer (Galaxy), string operations that follow the direction flag (Ships, Ai), and four others.
  - 11 uses of unnamed buffers: digit and text buffers, and the commander file extension. Change 3 names them.
  - 20 `#include "DataOverlay.h"` left unused. Change 3 deletes the header.

**4. The code segment leaves the game's memory.**
- **The divide trap.** Its answers are constants (`DivideTrap`), checked against the opcodes in the image: CS:236C and 2395 saturate the low byte, the other trapping divides the word (plan §7.1).
- **The filler.** The one opcode word that reaches a picture is `SBB_BX_OPCODES` (1E1Bh), a constant. The filler's patches are gone.
- **The cockpit** is copied from `cockpitImage`.
- **What else went.** `WipeProgram`'s fill (its CLI stays), `m_code`, the `Code*` accessors and the code segment's region.
- **What stays.** `CodeSegment()` remains as a segment number for five interrupt-vector uses. The CS rows of `ReferenceImage.cpp` stay as source, unchanged, so the image keeps its measured hash.

**5. The copy protection is deleted.**
- **Why it was unreachable.** The image carries the D5 byte (`protectionShown` = 1), and nothing writes 0. `protectionQuestion` stays FFh, so the timer never checks an answer. The replays never run either body.
- **What went.** `CopyProtection`, `CheckProtectionAnswer`, `Hardware::ReadDosLine` and their constants; `Platform::Backward` and `Guest::Backward`, with the direction-flag path in the timer's handler that only they reached.
- **What stays.** Its data bytes, until change 3.

**6. A latent bug, found and fixed.** `UpdateMissileAi` read a slot's target word raw and used it as an offset. No corpus point or twin reaches that path.

**7. The proof, then the re-base.**
- **Only converted fields differ.** The data segment was dumped at every corpus digest point and every twin answer (490 points), on the base and on the change.
  - **The second commit**, which converts the stored addresses, differs from the base only inside the converted fields: 0 bytes elsewhere, with 128 ranges masked. The fields, and at how many points each differed:
    - `messagePointer`, `keyBufferWrite`/`keyBufferRead`, `musicPointer`, the two edge lists, `pspSegment` and the two saved vectors: at all 490;
    - `messageShown` 217, `spawnOddsOffset` 193, `spawnLimitOffset` 186, `menuFirstRowAttr` 116, `data8C10`-`8C14` 69, `cargoRowPointer` 57, `commanderFileListNext` 37;
    - `missileTarget`, `chartItemEnd`, `chartLabelCursor` and the label words 36 each, `playerHitBy` 22, `warningMessage` 14, slot `owner` 12, slot `target` 9, `maskShipSlot` 4.
  - **Every later commit** (raw uses, code segment, protection, the last) is equal to the second at all 490 points, with nothing masked.
- **Pictures and outcomes:** 490 of each checked, 0 differ.
- **Subsystem answers:** no value changed. Six test classes changed their set-up or the API they call:
  - the divide answers carry the trap's kind;
  - messages compare as `Message`;
  - slots are `SlotIndex`;
  - the code-segment and MZ-header asserts went with the code segment;
  - one test was deleted with the protection.
- **The turn probe:** identical at every proof. The suite gives 77,460,360 turns with hash `5c0a1df325cfd7bb`; the corpus, 42,696,850 turns. Removing the writes nothing read changed no idle decision.
- **The re-base.**
  - **What changed:** the 134 corpus digests and all 356 known answers, and nothing else in the files, line ends included.
  - **How:** one process with `OUTPOST_ELITE_REBASE_DIGESTS`, never in CI. It records a digest only where that point's data segment equals the base's under the mask, and the pictures and outcomes match exactly.
  - **Reproduction:** a second recording was byte for byte the same.
- **The commits fail in between.** The six commits between the first and the re-base fail the 37 tests that check state digests, as the brief expected. The re-base is last and alone.

**8. Measured 2026-10-11.**

| Check | Result |
|---|---|
| `GameLogicTests` | 154 of 154 under g++ and clang++, with no warnings, on the change and on main with it |
| The corpus | all 26 replays meet all 134 re-based digests; 134 pictures and outcome lines, 0 differ |
| The twins | 356 answers met; 356 pictures and outcomes, 0 differ |
| Run time (g++, Release, all 26 replays, medians of 15 alternating rounds) | 1.864 s before, 1.794 s after, wall time |
| Checkers | `GenerateOverlay --check`, `GenerateTables --check`, `CheckFormat` and `CheckProjectFiles` clean; clang-tidy clean on every portable translation unit |
| Where the time went | almost all into editing; a full proof takes about five minutes |

**9. Not verified here:** the MSVC build. The shell uses no API this change touched.

## What this forecloses

- **An address in the game's state.** Messages, slots, cursors and rows are what they mean; a value read from the state is never used as an offset.
- **The code segment as memory.** Its few answers are constants, and nothing writes it.
- **Reading `ELITES.EXE` in the build or its tools.** The tables come from the checked-in image. `Tools/MapReference.py`, kept by hand for research (D36), is the one exception.
