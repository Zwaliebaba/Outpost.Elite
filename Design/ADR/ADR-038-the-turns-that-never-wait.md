# ADR-038 — The native game loop, first step: the loop turns that never wait go

**Status:** accepted 2026-10-13, with the change that implements it, the first step of piece 4 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It carries out the owner's ruling D70 for its first step. It amends [ADR-015](ADR-015-waiting-loops-de-assembled.md) item 1: a de-assembled loop reports its turns only where it can wait.

## Context

Paced time finds a wait by what a loop's turn changed (ADR-015). Every de-assembled loop that might wait called `Hardware::LoopTurn` at each backward jump the original takes, with a signature of the values it carries, and the `Platform` idled a turn that had the last turn's signature and changed no field and no port. When ADR-015 was written, which loops could wait was not known, so every backward jump reported a turn: the key waits, and also the copies, the searches, the menus' row loops and the jumps back into shared code.

Piece 4 removes the PC model (D40, D52). The owner ruled how it is guarded:

- **D70 (2026-10-13).** Each step keeps every replay's digests, pictures, outcomes and answers identical; a step that cannot is brought to the owner, not re-recorded. CGA memory and the 1987 drawing stay as the picture buffer until piece 5. Sound is synthesised from the game's state and checked sample for sample against the speaker model.

The first thing in the way is the turns themselves. Before a wait can say what it waits for (step b), the turns that are not waits have to be told apart from those that are, and taken out.

## Decision

1. **A loop reports its turns only where it can wait.** Ten calls of `Hardware::LoopTurn` stay, and 108 go. The ten are:

   | Where | What it waits for |
   |---|---|
   | `Docked.cpp`, `WaitForKey` | a key, on the docked screens |
   | `Docked.cpp`, `WaitForScreenExitKey` | Esc or another screen's key |
   | `Flight.cpp`, `WaitForKey` | a key, on the function keys' screens, the Ctrl+Esc freeze and the pause screen |
   | `Input.cpp`, `WaitForKeyPress` | a key |
   | `Input.cpp`, `ReadJoystickAxes` | the stick's two bits down |
   | `SaveLoad.cpp`, `WaitForKey` | a key, on the disc menu |
   | `Timer.cpp`, `WaitForTimerTick` | the timer's tick |
   | `Video.cpp`, `WaitForRetrace` | the vertical retrace |
   | `Video.cpp`, `PresentSpaceView` | the frame's time, `minimumFrameMs` |
   | `Galaxy.cpp`, `AddShortRangeSystem` | nothing: a label above the chart's top loops for ever, as the original does, and its turns are what reaches the run limit |

2. **What only the turns needed goes with them:** the 95 loop addresses that named them, with the two mount choosers' four each and the two delays' counts; the helpers that existed to report a turn (`TypedLineTurn`, `SpinDelay`, `TradeRowTurn`, `ToEquipMessage`, `ReportNotEnoughCredits`, `ToCargoMessage`, `RefuseBuy`) or to place one's carried values (`TradePricesPlace` and both `ProductNameRow`s); the parameters that carried a loop's address, the `Hardware&` parameters no longer read, two fields of the charts' table, and the comments that said what each turn carried. Where a function's type is fixed by a table of pointers, the parameter stays, unnamed (`DrawGalacticChart`). `GameState::PlaceOf` stays: it is tested, and step b decides its end.

3. **The two delays after a retrace take no paced time,** as before: `MOV AX,7D0h / DEC AX / JNZ` and `MOV AX,2BCh` carried their count in every turn, so they never idled. They are now a comment.

### Why it cannot move the clock

Paced time moves only at an idle and at a `Spend`. A turn that does not idle takes no time: it only becomes the turn the next one is compared with. So taking out a turn that never idles changes only what the next turn is compared with.

- **The next turn is at another loop that never idles,** or at a wait that changed something since: nothing changes.
- **The next turn is a wait's, and nothing changed between the two.** The wait's turn may now idle where it did not, because the turn before it is an earlier one with the same signature; or not idle where it did, because the removed turn had its signature and nothing changed between them. In both cases nothing changed since the turn it is compared with, so the next turn of the wait computes the same values and makes the same decision a turn later or sooner, at the same paced time and the same state. This is ADR-015 item 3's argument.

Paced time therefore idles at the same clock values in the same order, and the interrupts land where they did. The measurement below checks it rather than trusting it.

### Measured 2026-10-13

| Check | Result |
|---|---|
| Which turns wait | A scratch copy of the tree recorded, for each call of `Hardware::LoopTurn`, its file and line and whether the `Platform` idled the turn, over the 165 tests of `GameLogicTests` on `main` (`ca00c7c`). Of the 118 calls in `GameLogic`, 112 were reached: 76,682,589 turns, of which 41,226,583 idled, all at the nine waits above. The other 103 reached calls made 34,166,209 turns and idled none. The six calls no test reaches were read: two are jumps back into shared code taken once (`PostHyperspaceRefusal`, a full hold in `BuyCargo`), one the docked dispatch's jump just before its wait's own first turn, two loops whose carried count changes every turn (the status screen's rating search, the short-range chart's label moved up from the bottom), and one the endless loop that stays. |
| The idle trajectory | A scratch copy of each tree hashed, per test, the paced clock after every idle (FNV-1a over `m_clock`), the clock's whole trajectory, since it moves nowhere else. On the branch before main was merged, all 165 tests: 42,286,707 idles, each test's count and hash identical to `main`'s at `ca00c7c`. After merging `main` at `58a855f`, which added a test: 166 tests, 43,158,003 idles, each identical to that `main`'s. |
| The suite | `GameLogicTests` passes under g++, 166 tests after the merge, the corpus's digests, pictures, outcomes and answers among them, with no re-base |
| Unused code | clang 18, `-Wall -Wextra -Wunused-const-variable -Wunused-function -Wunused-parameter -Wunused-member-function`, over every file of `GameLogic`: no warning |
| clang-tidy | the root `.clang-tidy` with clang-tidy 18 on every changed file: no finding but the base's own |
| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |

## What this forecloses

- **A turn reported where nothing can wait.** A new de-assembled loop that cannot wait reports none; one that can is a wait, and step b gives it an explicit form.
- **Re-recording the replays for piece 4** (D70). A step that cannot keep them exact is brought to the owner.
