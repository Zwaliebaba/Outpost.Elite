# ADR-054 — No interrupts: the tick's work and the keys run at the game's waits

**Status:** accepted 2026-10-14, with the change that implements it, part of step 7 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D91.

## Context

After ADR-052 the Host still modelled the PC's interrupts: an interrupt flag the game set and cleared round its critical sections, two request lines with the 8259's in-service rules, the end of interrupt that the game's two handlers sent, handlers installed by identity (the game's, the BIOS's, a stray one) and a latch the keyboard handler emptied. All of it guarded the game's work against code that could only ever run inside its waits, since the clock moves nowhere else. Port 61h survived too, as the byte the speaker took its two bits from. D91 ruled that the interrupt model goes, with a second re-recording if it moved the digests.

## Decision

1. **What falls due runs when a wait goes on** (`Host::RunDue`): the game's tick work if a tick fell due, then one key event. The tick runs while the game has it running (`SetTickRunning`, by `InstallTimerInterrupt` and `RestoreTimerInterrupt`); keys go to the game's handler while it takes them (`SetKeysToGame`, by `InstallDivideAndKeyboardInterrupts` and `RestoreDivideAndKeyboardInterrupts`) and are dropped otherwise. One key is delivered a millisecond, in order (D88).
2. **`Game` installs the two routines by what they do**: `SetTickHandler` for `TimerInterrupt` and `SetKeyHandler` for `KeyboardInterrupt`, which takes the event it is given. `ReadScanCode`, `ResetKeyboard` and `GetKey` no longer take the Host.
3. **The interrupt model goes**: the interrupt flag and every CLI and STI in the game's code, the requests and their in-service rules, the end of interrupt, the handlers by identity and the stray one, the BIOS's default handlers, and the `Scheduler`, whose tick time and key queue are the Host's.
4. **Port 61h goes.** The game sets the speaker's two bits (`Host::SetSpeakerBits`) and reads them back (`SpeakerBits`), both clear at power-on; the bits the port held besides, 4Ch's, are not kept. `speakerPortImage` and `savedSpeakerPort` hold the two bits.
5. **What returned only for an STI returns nothing**: `TakeDamage` and `StartLaserSound`, and the eleven routines that took the Host only to set the flag no longer take it.

## What moved, and how it was measured

The state was dumped by member at every digest point of the corpus and the twins before and after the change and compared by path:

| What | Result |
|---|---|
| Members that moved | two, at every point: `timer.savedSpeakerPort`, 4Ch to 0, and `audio.speakerPortImage`, 4Ch to 0 or 4Eh to 2: port 61h's bits other than the speaker's |
| Pictures and outcomes | none moved, in the corpus or the twins |

Dropping the interrupts moved nothing else: they were taken only in the game's waits, where the tick's work and the keys now run, in the same order. The digests were re-based from the dump after the change, and the pictures and outcomes recorded by `OUTPOST_ELITE_RECORD_GUARD`; a second recording reproduced every file byte for byte. The suite passes in check mode, 148 tests, under g++ in Debug and Release and under clang++ 18. `SchedulerTests` goes with the scheduler; `HostTests` covers the tick, the keys one a millisecond in order, their drop before the game takes them, and the speaker's bits.

| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |
|---|---|

## What this forecloses

- **Work inside the game's work.** Nothing runs between two of the game's waits, so nothing in it needs guarding, and a routine that needs something to happen waits for it.
- **The PC's port 61h.** Only the speaker's two bits are the game's to set.
