# ADR-057 — The tests run the game on native rigs, with no PC load addresses

**Status:** accepted 2026-10-14, with the change that implements it, step 10 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)).

## Context

By the end of step 9 the rigs already ran the game natively. `ReferenceRig` started a `Game` on its `Host`, no test read an image, and D79 had removed `ReferenceImage.cpp` at step 5. What was left of the 1987 PC was smaller:

- **Load addresses.** `Game::Start` took the PSP's segment: `PSP_SEGMENT`, 0813h, the address DOSBox-X loaded at. From it `GameState` derived a code segment and a data segment, `DATA_SEGMENT_PARAGRAPH` paragraphs on, and kept both. Nothing in the game read either one. They were not in the digest, and only `StateDigestTests` asserted them.
- **Names.** `ReferenceRig` and `Reference.h` still carried the name of a reference the tests no longer load.
- **Comments.** The rigs' comments still spoke of the generated image, DOSBox-X's PSP, native routines at the program's entries, and interrupts sent to code no native routine stands in for. The last of these went with D91.

AGENTS.md was already current: its CI steps match `.github/workflows/build.yml`, and it says D79's rule, that the reference is measured against the owner's own copy.

## Decision

1. **No load addresses.** `Game::Start()` and `GameState::Start()` take nothing. `CodeSegment()`, `DataSegment()`, `PSP_SEGMENT`, `PSP_PARAGRAPHS` and `DATA_SEGMENT_PARAGRAPH` go.
2. **`ReferenceRig` becomes `GameRig`**, in `GameRig.h`: the game on its Host in paced time, as replays and the shell run it.
3. **`Reference.h` becomes `Repository.h`.** It holds only `FindInRepository` and `ReadWholeFile`. `START_MOMENT`, DOS's clock at power-on, moves to `FileStore.h` beside `DosDateTime`.
4. **The rigs' comments say what the rigs do.** Comments that say where an answer came from stay, as its record: the twins' known answers were given by the interpreted original, and the start state's hash was measured when the image still loaded it.

## What moved, and how it was measured

Nothing. The segments were in no digest and no outcome, so every digest, picture and outcome is as recorded. The suite passes, 148 tests under g++; the one test that asserted the segments keeps its other checks. `CheckFormat` and `CheckProjectFiles` are clean.

| Not run here | the MSVC build, including `Outpost`, whose `Application.cpp` g++ does not compile: CI builds it |
|---|---|

## What stays, and why

Phase 7's exit asks that nothing reach the 1987 layout, image, memory map or PC. Three things remain:

- **The CGA**, for piece 5, as the exit allows.
- **DOS's calendar and file attributes**, in `FileStore`. A commander file is still the original's `.CDR`, stamped and filtered as DOS does it (D78, ADR-049), so this is the file format, not the PC.
- **The speaker's divisor.** The game's note tables hold pitches as divisors, and the synth converts each to a frequency (ADR-055).

## What this forecloses

A test or the game does not name a segment, a load address or the reference image. A rig is the game on its Host.
