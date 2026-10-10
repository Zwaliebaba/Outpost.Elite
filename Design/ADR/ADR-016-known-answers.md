# ADR-016 — Known answers: the twins after D7

**Status:** accepted 2026-10-10, with the change that implements it: recorded answers for every twin in `GameLogicTests/KnownAnswers.txt`, `NativeTwin`, and eight twins for the gaps D20 leaves to known answers. It records how D20's second half is met ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner), ADR-008 item 8).

## Context

**What guards the port after D7.** D7 deletes three things (plan §5):
- the interpreter;
- its harness;
- the per-routine differential tests.

From then on two guards are left:
- the replay corpus: 26 replays and 134 digests, recorded from the original (ADR-008);
- the known answers.

**D20 split the gaps the corpus leaves** (ADR-008 item 8):
- what play reaches in a short run is closed by replay;
- the rest is guarded by known answers, recorded from the interpreter before it goes.

**What a twin is.** A twin (`GameLogicTests/TwinRig.h`) plays a scenario on the interpreted original and on the native code, both from the same state. It compares the game-state digest (ADR-008 item 5) of the two at every digest step and at the end. A twin may set up a state no replay reaches, through `Both`. There were 30 twins. After D7 there is no original to compare with.

## Decision

**1. Every twin's digests are recorded from the original.**
- **The file.** `GameLogicTests/KnownAnswers.txt` holds one answer per line: the twin's name, the ordinal of the digest within the twin, and the SHA-256. The file's header documents the format.
- **The ordinal** is counted from 1 across all of a twin's `Play` calls. Within each `Play` it counts the digest steps in order, then the state the `Play` ends in.
- **The check.** While the interpreter exists, a twin asserts that the original's digest is the recorded answer, and then that the native digest is the original's. A missing answer fails the twin and names it.

**2. Recording is one run, and never in CI.**
- **How.** With `OUTPOST_ELITE_RECORD_KNOWN_ANSWERS` set, each twin records the original's digests instead of checking them, and still requires the native digests to agree.
- **When it writes.** Only a twin that passed writes its answers, and it replaces only its own lines.
- **One process.** Recording runs in one process, not in `Build/RunTests.py`'s parallel shards, which would write the file at once.
- **Never in CI.** With `CI` also set, every twin fails.
- **Stable.** A second recording reproduces the file byte for byte.

**3. `NativeTwin` is the twin after D7.**
- **What it is.** It runs the native machine alone, on a Dispatcher, and checks every digest against the recorded answers (`NativeTwin::Answer`).
- **`TwinRig` wraps it.** `TwinRig` is now the interpreted half around a `NativeTwin`. The native code runs once, the same way in both.
- **What D7 does to the twins:**
  - deletes `TwinRig`'s original half;
  - gives `NativeTwin` the name `TwinRig`;
  - edits the few call sites that reach the original: SaveLoad's file comparisons, and the `compared` option.
  
  No twin's steps change.
- **Tested today.** `TwinRigTests.NativeTwinAloneMeetsTheKnownAnswers` runs a twin's scenario on a `NativeTwin` against its recorded answers.

**4. After D7, an answer changes only by a ruling recorded with its cause,** as a corpus digest does (ADR-008 item 7). An answer is never re-recorded from the native code to make a failing twin pass. The native code is what it guards.

**5. The gaps of D20 that no replay reached have twins.** Each sets its state only to values the game's own code writes, and says in a comment what writes each.

| Twin | What it reaches |
|---|---|
| `TwinMouseFlight` | flight steered by the mouse, with the PC's own driver (`TwinOptions::mousePresent`) |
| `TwinStickFlight` | flight steered by the IBM stick |
| `TwinAmstradFlight` | flight steered by the Amstrad joystick, which sends keyboard codes 77h–7Ch, not the game port |
| `TwinStationShuttle` | a Shuttle the station launches at an offender, then a police Viper |
| `TwinInvasion` | the invasion's Thargoids spawning in the safe zone, after a jump that follows the briefing |
| `TwinScoop` | the scoop's rare paths: the masking device's barrel, a Thargon, a splinter carrying minerals, and a full hold |
| `TwinShipIdentity` | `ShowShipIdentity`'s labels Trader, Hermit, Police, Hunter, Simple, Wolf and Attack |
| `TwinSupernovaDeclined` | mission 1 declined, and its 1,400-credit reward, by play |

- **Two labels are not reachable.**
  - `ShowShipIdentity`'s "None" belongs to class 0, and the only class-0 ship is the Cobra an escape capsule leaves behind. While the capsule flies, `ProcessFlightKeys` reads no key.
  - "Station" is never shown, because `HandleIdentifyKey` skips a station.
- **The mouse twin runs uncompared.** Every mouse read is a service call, and a compared call that makes one keeps the original's outcome without running the native code. Uncompared, the native reads run.
- **The others run compared.** Each also passes on a Dispatcher.

**6. Measured 2026-10-10.**
- **Answers:** 398 for 38 twins. That is 282 for the 30 existing twins and 116 for the 8 new ones.
- **Time:** the new twins take about 6.5 s together (g++ Release), 4 s of it the two mission twins.
- **The suites:** `GameLogicTests` passes 203 of 203 under g++ and clang++, without warnings. The coverage check is clean, and no digest moved.
- **Wrong answers fail clearly.** A deliberately wrong answer fails its twin with both digests and the answer's key.

**7. Not detected: a stale answer.** A twin that loses digest steps, or is deleted, leaves its lines in the file. They are removed by hand with the twin.

## What this forecloses

- **Recording answers from the native code,** or in CI.
- **A twin without recorded answers.**
- **Re-recording an answer to make a twin pass,** before or after D7.
