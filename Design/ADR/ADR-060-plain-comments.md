# ADR-060 — Plain comments: no narrated instructions, no registers, no "1987"

**Status:** accepted 2026-10-14, with the change that implements it, under the owner's ruling D95. It finishes what step 9 of Phase 7 left ([ADR-056](ADR-056-no-addresses-in-comments.md), [Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)).

## Context

ADR-056 took the reference's addresses out of the comments and left two things on purpose:

- the comments that narrate the reference's instructions (`MOV AX,…`, `REP STOSW`, `PUSH SI / POP SI round it`), with the registers and flags they name;
- the word "1987" itself ("as in 1987", "1987's").

Since step 6, the game's code holds no register and no flag, so a comment naming `CX` or `CF` describes a machine the code no longer has. A reader has to know the 8086 to read what a line of C++ does.

## Decision

1. **A comment says what the code does, in the code's terms.** It names the member, the parameter or the value, not the register that held it. `MOV CX,_ticks, then WaitForTimerTick and the LOOP back to it` becomes `WaitForTimerTick _ticks times, counted down after each`. A flag becomes what it means: `IsStation's CF` becomes `IsStation's dodo`.
2. **A comment that only narrated the instructions under it goes**, when the code says the same.
3. **Where an answer came from stays.** "The original", "the reference" and "as the original does" stay where a behaviour is kept because the reference has it: a wrap, an order, a value a later routine reads. That is the reason for the code, not a description of it. "1987" goes from every comment; "the original" says the same.
4. **What models an instruction keeps naming it.** These say what they reproduce, as `Arithmetic.h` does, since that is what they are for:
   - `Arithmetic.h` and the divide family, with the trap's handler;
   - the helpers that stand for one instruction: `Text.cpp`'s shift and word store, `Equipment.cpp`'s signed byte product, the CGA's word fill, `Scene.cpp`'s `Loop`;
   - `Flight.h`'s two array indices.
5. **Only comments change.** No code, no identifier and no string changes, and every digest stays as it was.

## How it was done, and measured

The comments were rewritten by hand, file by file, in five groups. A script then checked every changed file: with each line's comment cut off outside string literals, the old and new code match, in all 104 files.

| Measure | Before | After |
|---|---|---|
| Comment lines naming an instruction, a register or a flag, or "1987" | 699 in 105 files | 11 in 5 files, each one under point 4 or a false match |
| Comment lines with "1987" | 89 | 0 |

The count is made by `narr.py`, kept with the session's scratch work and not in the tree. It matches a mnemonic followed by an operand, a register or flag name as a word, and "1987", in the text after `//` outside string literals.

The suite passes, 152 tests under g++, on two runs in check mode, every digest unchanged. `CheckFormat` and `CheckProjectFiles` are clean, and no changed comment adds a line over 140 columns.

| Not run here | the MSVC build and clang-tidy 22, which CI runs |
|---|---|

## What this forecloses

A new comment does not narrate the reference's instructions or name a register, and does not say "1987". Where the reference's instruction sequence matters to a reader, it goes in `Reference-Map.md` or an ADR, and the comment says what the code does and that the original does it.
