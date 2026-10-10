# ADR-017 — The interpreter deleted (D7)

**Status:** accepted 2026-10-10, with the change that implements it. D7 ([Reverse-Engineering-Plan.md §5, Phase 4](../Reverse-Engineering-Plan.md#phase-4--detach)) in two changes. The first deleted the interpreter and everything that served only to compare the native code with it. The second, the same day, deleted the entries and hooks nothing reaches any more, and D22's two routines (item 6).

## Context

**What D7 rules.** At the end of Phase 4 the interpreter, its harness and the per-routine differential tests are deleted. From then on the replay corpus and the known answers are the only guard.

**What had to be true first.**
- **The de-assembly is complete.** No routine is register code (ADR-012 item 22), so the interpreter ran only to check the native code.
- **The corpus is complete.** It reaches every subsystem in plan §6.2, in 26 replays, and D20's replays are in (ADR-008 item 8).
- **The known answers are recorded.** Every twin's digests are recorded from the original, and the twins have a native-only form (ADR-016).

## Decision

**1. What went.** 13,961 lines in 98 files.

| Where | What |
|---|---|
| The solution | the `Interpreter` and `CpuConformance` projects |
| `Machine` | the `Processor` interface and its factory: the `Pc` owns a `Dispatcher` |
| `Machine` | `NativeCode`'s comparison: calls run on both machines and compared, mismatches, and the poisoning of what a contract leaves (ADR-012) |
| `Machine` | `Memory`'s write journal; the port router's replay |
| `Machine` | calls into the original (`CallNear`) and the host call beside a waiting routine |
| `Machine` | the interpreted pacing points |
| `Machine` | paced time's watch on backward jumps and HLT; `StopReason::Deadlocked` |
| `Machine` | the execution map and the coverage reports |
| `GameLogicTests` | `ComparisonRig.h` and every per-routine comparison test; with the corpus's two other runs and three duplicates, 156 tests |
| `GameLogicTests` | the twins' interpreted half; the corpus's interpreted and compared runs |
| `MachineTests` | `CpuTests`; the tests of the comparison machinery |
| `Tools` | `CpuConformance.py`, `RoutineCoverage.py` and its CI step, `CompareTrace.py` and `ReferenceTrace.py` (DOSBox-X's trace against the interpreter's) |
| `ReferenceRunner` | its modes that need the original: `--update`, `--compare`, `--trace`, `--coverage`, `--native-report` |
| `Design/NativeCoverage.tsv` | the coverage check's written reasons, which no tool reads now |

**2. What guards the port now.**
- **The corpus's native run.** It plays 26 replays and checks 134 digests: in `CorpusTests.NativeCodeKeepsEveryDigest`, and in `ReferenceRunner --replay`.
- **36 twins and their 356 known answers** (ADR-016). Two twin pairs became one each:
  - `TwinDiscMenuUncompared` folded into `TwinDiscMenu` (30 answers);
  - `TwinStickCompared` folded into `TwinStickNative` (12 answers).
  
  Each pair's answers were identical, digest for digest. No answer changed.
- **Two recorded tables, taken from the original before the deletion,** of what a twin used to compare with the original twin's:
  - the files SaveLoad's twins write, as SHA-256s;
  - the native firmware's outcomes (`DispatcherTests.NativeFirmwareDoesWhatTheRomDid`).
- **`MachineTests`' device and service tests:** the PIT, PIC, CGA, keyboard, game port, speaker, mouse, DOS, BIOS, firmware, memory, the loader, SHA-256 and the port router.
- **`MachineTests`' tests of the machinery the native game keeps,** re-expressed on a Dispatcher with native routines where they ran guest code on the interpreter:
  - paced time;
  - loop turns, by signature and otherwise;
  - waiting routines on the native thread;
  - interrupts taken as due;
  - run limits and stop reasons.

**3. A digest or an answer cannot be recorded from the original any more.** `ReferenceRunner` plays a replay natively, prints each digest and checks it against the recorded one. A new replay or twin can no longer be finished by recording. A recorded value changes only by a ruling with its cause (ADR-008 item 7, ADR-016 item 4).

**4. Left for D7's second half.**
- **The entries and hooks nothing reaches.** The Dispatcher enters native code only at the program's entry and through the interrupt vectors. Every other entry exists only for comparisons that are gone.
- **What only the entries use:** `NativeContract` and `Hook::contract`, kept so that the entries compile; `Guest::Clobber`, now a no-op; the `…Out` helpers.
- **`Guest`, which two twins' set-ups still use** (`FlightTests`, `HyperspaceTests`).
- **`InitEscapePod` and `InitKraitHunter` (D22).**
- **`TimeMode::Clocked`,** which has no meaning on a Dispatcher.

**5. Measured 2026-10-10.**

| Check | Result |
|---|---|
| `GameLogicTests` | 47 tests (203 before), all pass under g++ and clang++. Release run time 4.2 s, against 77.7 s before. |
| `MachineTests` | 131 tests (157 before), all pass under g++ and clang++ |
| clang++ warnings | none |
| The corpus | all 26 replays meet all 134 digests under `ReferenceRunner`. A replay with one digest corrupted exits 1. |
| The known answers | each of the 356 is checked exactly once, and each is met |
| clang-tidy | clean on every changed `.cpp` |
| The checkers | CheckFormat and CheckProjectFiles are clean |
| `Build/RunTests.py` | two shards: the corpus, and the rest |

**6. D7's second half: what only the comparison reached, deleted 2026-10-10.** 6,524 lines in 57 files.

**Hooks.** Of the 387 entries, three remain, with the firmware's two handlers. Hook calls were counted over all 47 tests and all 26 replays, before the change and after it, and the counts are identical.

| Hook | Why it stays |
|---|---|
| `Start` (CS:0000) | the program's entry |
| `TimerInterrupt` (CS:0215) | int 8, whose vector `InstallTimerInterrupt` writes |
| `KeyboardInterrupt` (CS:0201) | int 9, whose vector `InstallDivideAndKeyboardInterrupts` writes |
| the BIOS's timer and keyboard handlers (F000:E000, F000:E040) | the machine's own firmware, which int 8 and int 9 reach whenever the game puts the BIOS's vectors back |

**Two vectors the game writes have no hook.**
- **Int 0, the divide trap.** The Dispatcher executes no divide: each divide calls `DivideOverflowInterrupt` itself.
- **Int 24h, DOS's critical-error handler.** The emulated DOS answers a failure with CF set and never raises int 24h. `CriticalErrorInterrupt`, whose only caller was its hook, went with it.
- Both vector writes stay, as the original's writes. If either vector were ever taken, the run would stop as `Unported`.

**What went:**
- every other entry, its `NativeContract` and its table row;
- the `…Out` helpers that rebuilt registers;
- `Guest::Clobber`, `Hook::contract` and the register-contract bits;
- the value routines that only entries called: four warning checks `UpdateWarnings` runs itself, and three that only read back register leftovers.

**What is left of `Guest`** is what the three entries use: `Start` reads DS, BP and the direction flag and pushes the far return to PSP:0000, and the timer's handler passes on the direction flag.

**D22.** `InitEscapePod`, `InitKraitHunter` and `RandomizeOrientation`, which only the escape pod's branch called, are deleted. `LaunchShipFromObject`'s branches for DL=15h (CS:5386) and DL=5 (CS:53B5) went with them. Its reachable paths make the same writes in the same order.

**`TimeMode::Clocked` is deleted.** A `Pc` is paced from power-on. Clocked time had no meaning on a Dispatcher, and every caller already set paced time.

**Measured.**
- `GameLogicTests` passes 47 of 47 and `MachineTests` 131 of 131, under g++ and clang++, without warnings.
- All 26 replays meet all 134 digests.
- Each of the 356 known answers is checked once and met.

**For the native devices.**
- The timer's handler takes the direction flag from the registers at the interrupt. A scheduler must supply it.
- Interrupts still reach native code through the vector table in memory (ADR-014 item 5).

## What this forecloses

- **Settling a question about the original by running it here.** The listing, the reference map, the corpus's digests and the known answers are the record now. DOSBox-X still runs the original for screens (`Tools/ReferenceScreens.py`, ADR-003).
- **A test that compares native code with the original.**
- **Recording a digest or an answer without a ruling.**
