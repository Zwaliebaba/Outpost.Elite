# ADR-017 — The interpreter deleted (D7)

**Status:** accepted 2026-10-10, with the change that implements it. This is the first half of D7 ([Reverse-Engineering-Plan.md §5, Phase 4](../Reverse-Engineering-Plan.md#phase-4--detach)): the interpreter and everything that served only to compare the native code with it. The second half follows separately: the entries and hooks nothing reaches any more, and D22's two routines.

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

## What this forecloses

- **Settling a question about the original by running it here.** The listing, the reference map, the corpus's digests and the known answers are the record now. DOSBox-X still runs the original for screens (`Tools/ReferenceScreens.py`, ADR-003).
- **A test that compares native code with the original.**
- **Recording a digest or an answer without a ruling.**
