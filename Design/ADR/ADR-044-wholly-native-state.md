# ADR-044 — Wholly native, the state: written by name, then free of the 1987 layout

**Status:** accepted 2026-10-14, with the change that implements it, step 1 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's rulings D76 and D78. Each later step of 2 to 5 amends it as it lands.

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

## What this forecloses

- **The state's layout as a format.** What the digest and the commander file hold is the members in their order; a struct may lay them out as it likes.
