# ADR-042 — The native game loop, fourth step: the sound is synthesised from what the game's sound routines set

**Status:** accepted 2026-10-13, with the change that implements it, step d of piece 4 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), under D70 and D72.

## Context

D70 ruled the sound synthesised from the game's state and checked sample for sample against the speaker model over the corpus before the model goes. After [ADR-041](ADR-041-the-devices-go.md) the sound was two classes: the tone timer, the 8253's channel 2 as the game programs it, and the speaker, which logged the tone's state and the system control port's two bits and rendered PCM from the log.

What the game's state holds of its sound is the music's and the effects' progress (`AudioState`): the note's ticks left, the tune's position, the beeps, the sweeps. Its sound routines, `TickMusic` and `TickSoundEffects`, run from that state at each 1 kHz tick and set the tone and the speaker's bits. The tone's divisor itself is not in the state: the routines hand it to the timer and keep nothing of it. So the step has two readings:

- **a synth that reads `AudioState` itself**, which would compute again what the game's routines compute, from the same state, at the same ticks. It would duplicate them, and the divisor would have to come into `GameData`, which moves every digest;
- **a synth that takes what the routines set**, the tone by its divisor and the gate and data bits, and renders it, with no timer between.

## Decision

1. **The speaker is the synth** (`Speaker`), driven in the game's terms: the tone by its divisor (`SetTone`, from `Hardware::SetToneDivisor`), and the gate and data bits (`SetBits`, from the system control port's bits 0 and 1). The tone is the timer's channel 2 in mode 3, its square wave the synth's oscillator (`SquareWave`): loaded on the next timer tick, held high by a low gate, reloaded on a rising one. The speaker logs each change of the whole state, the tone included, and renders PCM from the log as before.
2. **The tone timer goes** (`ToneTimer`). Its arithmetic is the speaker's.
3. **The second reading is not taken.** It re-derives what the game's routines already derive from the same state, and costs the digests.

### Measured 2026-10-13

| Check | Result |
|---|---|
| The sound, sample for sample | The rendered samples are a function of the log and of `RenderSamples`, which is unchanged. A scratch copy of each tree hashed, per test, every event of the speaker's log once a later one was pushed past it, so each as it stands in the log, after the changes at one cycle have collapsed into it: the 164 tests the two trees share identical, 54,983 events on each side. The tone timer's own test is `SpeakerTests.GateRestartsTheToneAndTheLogHearsIt`. |
| The idle trajectory | Every game test identical in count and hash to ADR-041's third part. |
| The suite | `GameLogicTests` passes under g++, 165 tests, with no re-base |
| Not run here | the MSVC build and the game, which the owner hears: CI builds and runs the tests |

## What this forecloses

- **A timer between the game and its sound.** The game's routines set a tone and two bits; the speaker sounds them.
- **The tone's divisor in the game's state**, until something else needs it.
