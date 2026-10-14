# ADR-055 — The speaker is a synth: a tone's frequency, not the timer's ticks

**Status:** accepted 2026-10-14, with the change that implements it, the last part of step 7 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D77 (the sound routines drive the synth).

## Context

After ADR-052 the speaker's log was stamped in milliseconds, but its tone was still the PC timer's channel 2: a `SquareWave` counted in timer ticks, its output computed tick by tick from its count and its phase, reloaded on the tick after a write, and rendered against the 8088's cycle clock, kept privately inside `Speaker.cpp` for that alone.

## Decision

1. **The tone is a frequency.** The game still gives a pitch as the timer's divisor, as its note tables hold them; the speaker takes it as 1,193,182 Hz over the divisor (0 is 65,536) and keeps the frequency. The wave is a square wave high for its first half period, starting high where the tone is set and where the gate rises.
2. **Rendering integrates the wave exactly** over each sample's interval, in seconds, by its phase; a tone above 20 kHz or the output's Nyquist frequency is its mean, as before. Sample bounds come from whole milliseconds and whole samples, so a stream rendered in pieces is the stream rendered at once.
3. **`SquareWave`, the cycle clock and the timer's tick go.** An odd divisor's one-tick asymmetry, which no listener hears, goes with them.

## What moved, and how it was measured

Nothing of the game: the speaker is not part of the state or its digest, and every digest, picture and outcome is as recorded; the suite passes in check mode, 148 tests under g++. The sound was compared by rendering every sample at 44.1 kHz through two replays with the speaker before and after the change:

| Replay | Samples | Correlation | RMS before | RMS after | RMS of the difference |
|---|---|---|---|---|---|
| `title` (the music) | 1,058,400 | 1.0000 | 6,505 | 6,505 | 40 |
| `combat-orerve` (the effects) | 1,349,460 | 1.0000 | 5,986 | 5,986 | 5 |

The difference is the tone starting at the moment it is set, not at the next tick of the timer, under a microsecond later. `SpeakerTests` is written for the synth.

| Not run here | the MSVC build and the game, by ear: CI builds and runs the tests, and the owner plays it |
|---|---|

## What this forecloses

- **The timer's channel 2.** No count, phase or reload of the PC's timer is modelled; a tone is its frequency and where it started.
