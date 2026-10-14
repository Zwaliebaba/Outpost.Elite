# ADR-052 — Time is milliseconds, and a key is an event

**Status:** accepted 2026-10-14, with the change that implements it, part of step 7 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's rulings D87 and D88.

## Context

Until now the platform counted the 8088's cycles: the game's tick was the timer's channel 0 at divisor 04A9h, about 1000.15 Hz; the CGA's retrace came every 79,648 cycles, about 59.92 Hz; a key was a scan code, latched by the XT's keyboard a millisecond after the last was acknowledged on port 61h's bit 7; and the BIOS's 18.2 Hz tick counted the time of day before the game installed its handler and after it put the BIOS's back. D87 ruled that time is whole milliseconds, the tick 1 ms and frames 60 a second, with every digest re-recorded once and the outcomes checked; D88 that a key reaches the game as its own event, one queued event a tick, in order.

## Decision

1. **`Milliseconds` replaces `Cycles`** (`Timing.h`). The platform's clock, `RunUntil`, `Spend`, the snapshots' `pacedTime`, the replays' `ReplayTime` and the file stamps count whole milliseconds. The crystal, the CPU's clock and the timer's tick period leave `Timing.h`.
2. **The tick is 1 ms** (`Scheduler::SetTickRunning`). `InstallTimerInterrupt` starts it and `RestoreTimerInterrupt` stops it, where they programmed channel 0 to 04A9h and back to 0. It requests the timer's interrupt at every whole millisecond after it starts, and none while it is stopped.
3. **Frames are 60 a second** (`FrameStart`, `FrameAt`): frame n starts at n × 1000 / 60 ms, rounded down, and the retrace is its first millisecond. The status's bit 3 and bit 0 mark it, and nothing else; the CRTC's sync position is no longer read, as both of the game's modes put it at the same place. Paced time's rule stands: each retrace is reported to the first read that sees it.
4. **A key is a `KeyEvent`**: the key, by the number `keyDown` indexes it by, and whether it went down. The host queues events (`Scheduler::Inject`); when the latch is empty the next whole millisecond delivers one and requests the keyboard's interrupt; `ReadScanCode` takes it (`Hardware::TakeKey`), which empties the latch. So one event arrives a millisecond at most, in order (D88). The scan code's break bit, the overrun code, port 61h's keyboard hold and the acknowledge pulse go. The keys keep the XT's numbers as their identities, which step 9 may rename.
5. **The BIOS's time of day goes** (`TimeOfDay`, `Platform::Day`), since nothing has read it since ADR-049. Where the game has no handler of its own, the host's default ends the interrupt and drops a key.
6. **The speaker keeps the timer's clock privately.** Its log is stamped in milliseconds and its rendering API takes them; the tone, channel 2, still runs on the IBM PC's timer ticks inside `Speaker.cpp`, a millisecond starting at the tick it falls in, until the synth replaces it.
7. **The charts' pacing is whole milliseconds**: 109 ms for the galactic chart's frame and 84 ms for the short-range chart's, the cycles ADR-013 measured, rounded down.

## What moved, and how it was measured

Every digest, picture and outcome moved, as every interrupt lands at a new time. Before re-basing, each replay and twin was played and every outcome compared with the recorded one. Outcomes of flight timing moved by a few units (a shield point, a degree of heat, the slots in use, the fuel the supernova's leak takes in its frames). Six progress outcomes moved because a scenario no longer did what its replay says, and each replay was put right:

| Replay | What broke | What was done |
|---|---|---|
| `hyperspace-and-fight` | Left held 0.55 s put the cursor on system 147, not Riedquat | held 0.65 s, which selects Riedquat (46) |
| `mask-mission` | the second launch's escorts killed the commander | the fight flown again by the pilot below, from the same commander: two mask Asps destroyed, the Constrictor and the Cougar met, alive |
| `witch-space` | one kill fewer, and no Thargoid missile for the ECM | flown again: the ram and one laser kill, a missile the ECM removes (twice, counted in `RemoveAllMissiles`), killed; the title's wait 3 s, not 2 |
| `scooping-an-escape-pod` | no pod spawned: the draws spent docked changed | the commander 31 draws further on (154,569), and flown again: the pod scooped, SCOOP RETRIEVAL INACTIVE on the boulder, then its ram for 0.1 credits |

The pilot is a scratch program, not committed, as the original steering program was: every 50 ms it reads each object's position in the view's frame, rolls the target onto the vertical axis and pitches it to the aim (the nearest ship for a fight, 100 units under the nose for a scoop), fires when the target is within a twenty-fifth or a sixth of its range of the sights, and fires the ECM while a missile is out. The draw counts, the pilot's tolerances and, for witch space, a delay before the fight of up to 0.6 s were searched until one met each replay's account.

Four progress outcomes still differ, each from the random state, which the flight's frames advance: `scooping-a-canister`'s barrel holds luxuries where it held alloys; `supernova-mission`'s galactic jump to galaxy 3 lands at system 211 where it landed at 243, and the mission is completed and paid as before; and `witch-space`'s ECM and `TwinJumps`' third missile survive the random loss of equipment below one energy bank (`Flight.cpp`), which took them before.

The ship types each replay meets were sampled every 10 ms before and after: the corpus meets the same 27 types. `hyperspace-and-fight` no longer meets a plate and `supernova-mission` no longer meets a Viper, and other replays meet both.

The digests were then re-based from a dump of the state at every point, and the pictures and outcomes recorded by `OUTPOST_ELITE_RECORD_GUARD`; a second recording reproduced every file byte for byte. The suite passes in check mode, 158 tests, under g++ in Debug and Release and under clang++ 18, which also reports twelve warnings in files this change does not touch. `TimeOfDayTests` and the BIOS handlers' test go with what they tested; the scheduler's, the CGA's status, the speaker's and the platform's tests are written for milliseconds.

| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |
|---|---|

## What this forecloses

- **The PC's rates.** Nothing in the game runs at 1000.15 Hz or 59.92 Hz, and a replay recorded before this change does not play the same.
- **Scan codes from the host.** The shell gives the game key events; a break code or the XT's acknowledge is not a way in.
- **A clock finer than a millisecond** outside the speaker, which keeps the timer's for the tone until the synth replaces it.
