# ADR-039 — The native game loop, second step: the waits are declared

**Status:** accepted 2026-10-13, with the change that implements it, the first part of step b of piece 4 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), under the owner's ruling D70. It supersedes [ADR-015](ADR-015-waiting-loops-de-assembled.md): a wait is declared, not found from what a turn changed. The fields still count their changes ([ADR-024](ADR-024-members-that-count-their-changes.md)), but nothing reads the counts now; the second part of step b removes them.

## Context

After [ADR-038](ADR-038-the-turns-that-never-wait.md), ten places reported a loop's turn to the platform: nine waits and one loop the original never leaves. Each turn carried a signature, and the `Platform` idled a turn whose signature matched the last turn's and that changed no field and wrote no port. That rule is what needs the change counting: every field of the state, the BIOS's data area, the vector table and every port write is counted, only so that a turn can tell it changed nothing.

ADR-015 ruled out the alternative, explicit waits, because where the original idles was to be found, not declared. With the waits now known, the question is whether a declared wait idles at the same clock values as the rule did. D70 requires that it does.

## Decision

1. **A wait says when its poll found nothing** (`Hardware::Wait`): the clock moves to the next device event, the run ends there if that is its end, and the interrupts that fall due are taken, as an idle turn did.
2. **A turn that waits for nothing says that instead** (`Hardware::Spin`): the clock stands still, and past `SpinLimit()` such steps with no wait between, the run stops as `Spinning`, as before.
3. **The nine waits:**

   | Wait | Waits (`Wait`) | Spins (`Spin`) |
   |---|---|---|
   | The five key waits: `WaitForKeyPress`, the three `WaitForKey`s (docked, flight, disc menu) and `WaitForScreenExitKey` | when `GetKey` took no code and saved no screenshot (`AwaitNextKey`) | when it took a code the wait does not want, or saved a screenshot |
   | `WaitForTimerTick` | while the tick is the one it started at | — |
   | `WaitForRetrace` | while the status shows no retrace | — |
   | `PresentSpaceView`'s wait for the frame's time | while `msSinceFrame` is below `minimumFrameMs` | — |
   | `ReadJoystickAxes`'s wait for both bits | from its second poll on | its first poll |

   The short-range chart's endless label loop spins.
4. **What only the turns needed goes:** `LoopTurn` in `Hardware` and `Platform`, the turn's record and its comparison, `MOST_TURN_WORDS`, the loop addresses that named the waits (kept in comments where a reader needs them), the waits' loop parameters, and `GameState::PlaceOf`, which put a member's place in a signature.

### Why it idles at the same clock values

Under the old rule, a wait's turn idled when the turn before it had the same signature and nothing had changed since. A declared wait idles at every poll that found nothing. The two differ only at a poll that found nothing and was not idled: the first poll after something changed, or after a different loop's turn.

- **The polls that read paced time only:** the key waits, the timer, the retrace and the frame's time. A poll that found nothing changes nothing: `GetKey` with an empty buffer and no screenshot asked writes nothing, the timer's and the frame's are reads, and the CGA's status read only records again a retrace it has seen already (`Cga::ReadStatus`). So the old rule's next poll came at the same clock, found the same nothing, and idled. The declared wait idles one poll sooner, at the same clock and in the same state: the same idles, in the same order.
- **A poll that took something it does not want** changed the state, so the old rule did not idle it, and the next poll comes at once: a second key already in the buffer is taken at the same clock. The declared wait spins there.
- **The joystick's wait** is the exception, because its port counts the instructions executed (`InstructionCycles`), not the clock, so each poll moves the one-shot on and a second poll is not the first again. Its first poll followed the port write that fired the one-shots, a change, so the old rule never idled it, and idled every poll after it until both bits dropped. The declared wait does exactly that.

### Measured 2026-10-13

| Check | Result |
|---|---|
| The idle trajectory | The probe of ADR-038, per test, the paced clock after every idle hashed (FNV-1a over `m_clock`): 166 tests, 43,158,003 idles, each test's count and hash identical to the branch before it (ADR-038's, merged with `main` at `58a855f`). |
| The probe sees a wrong rule | A mutant whose joystick wait idles its first poll too, which the argument above says adds an idle to each read of the stick, changed two tests' trajectories, and `InputTests.IbmStickSteersTheChart` failed its known answer: `TwinStickNative`'s eleventh digest, left and down, differed. |
| The suite | `GameLogicTests` passes under g++, 166 tests, the corpus's digests, pictures, outcomes and answers among them, with no re-base. `PlatformTests`' paced-time tests now wait and spin: a wait for the BIOS's tick ends at the first IRQ 0, a wait with interrupts off runs to the run's end, work that spins takes no time, and a loop that only spins stops as `Spinning`. |
| Unused code | clang 18, `-Wall -Wextra -Wunused-const-variable -Wunused-function -Wunused-parameter`, over every file of `GameLogic`: no warning |
| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |

## What this forecloses

- **Finding a wait from what a turn changed** (ADR-015). A new wait declares itself, and says what it waits for.
- **A signature of carried values.** Nothing compares turns.
- **The spin limit's count of turns as before.** A spin is counted where a wait's poll took something, and where a loop never ends; the work loops' turns, gone with ADR-038, no longer count. A run that spins still stops as `Spinning`, at the same paced time.
