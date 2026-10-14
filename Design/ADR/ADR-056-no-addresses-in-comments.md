# ADR-056 — No 1987 addresses in the code's comments

**Status:** accepted 2026-10-14, with the change that implements it, step 9 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)).

## Context

The port was written against the 1987 image, and its comments said where: a routine's `CS:` offset beside its name, a variable's `DS:` place beside its member, a table row's address at its end, a jump's target in a sentence. Once the game's code stopped holding the 8086's registers and memory (steps 2 to 7), those numbers named nothing in the tree. A reader can't follow one anywhere but into the reference, and `Reference-Map.md` and the ADRs already keep that map.

## Decision

1. **A comment names a place in the program by what is there**: the routine, the member or the table, not its address. Where an address was the only name (a label in the middle of a routine, a jump's target), it is replaced by what the code does there ("back to the GetKey", "the shared tail", "one loop, in two copies"), or it goes.
2. **A table row's trailing address becomes its index in the array**, `// [N]`: the record's index in an array of records, otherwise the index of the row's first element. Each row stays on its own line, which the trailer was what kept it on under `.clang-format`, and the index is the table's own layout.
3. **Values stay.** A number the code computes with or the hardware holds is not an address: `8000h`, `7FFFh`, a divisor, a seed, a fill pattern, a screen cell, `B800:_cell` in the CGA's memory that `GameState` owns, the MZ header's segments.
4. **Out of scope, and left:** comments that narrate the reference's instructions (`MOV AX,…`, `REP STOSW`, about 970 lines) and the 87 remaining uses of the word "1987" (for example, "as in 1987"). They describe behaviour the port keeps, not places in the image. The plan's step 9 asks for the addresses alone.

## How it was done, and measured

The change was made by a script and then read by hand. The script removed parentheticals holding only addresses, leading `ADDR:` labels, and `at`/`from ADDR` phrases. The hand-reading covered every changed comment whose removal was not a whole parenthetical (123 hunks), plus a second pass for bare four-digit hex offsets (66 changes). About 60 comments were rewritten by hand where the address carried the meaning.

| Measure | Before | After |
|---|---|---|
| Address-shaped tokens in comments (`CS:`/`DS:`/`ES:`/`SS:` offsets, `0xNNNN`, `NNNNh`, `NNNN-NNNN`) | 5,508 in 84 files | 54 in 24 files, every one a value or a screen position |
| "1987" in comments | 192 | 87 |
| Table rows given `// [N]` in place of an address | — | 4,163 |

The comments were tokenised by `//` outside string literals. A check confirmed that no non-comment text changed in any of the 83 files: with each line's comment cut off, the old and new code match. The suite passes, 148 tests under g++, and every digest is unchanged. `CheckFormat` and `CheckProjectFiles` are clean.

| Not run here | the MSVC build: CI builds and runs the tests |
|---|---|

## What this forecloses

A new comment does not cite an address in the reference. Where a reader needs the reference's place, it goes in `Reference-Map.md` or an ADR, and the comment names the routine.
