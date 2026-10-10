# ADR-010 — Native routines, and how each is proved

**Status:** accepted 2026-10-10, with the change that implements it. That change contains:

- the hooks and the comparison in `Machine` (`NativeCode`, `Pc::Hook`, `Pc::CallNear`);
- the data overlay and `Guest` in `GameLogic`;
- the corpus run three ways;
- the first 23 entries ported: the maths leaves.

It is the machinery plan §5's Phase 3 and §6.3 describe, and ADR-003 item 3 requires. **Amended 2026-10-10:** native code waits on a thread of its own (item 8), and each subsystem has its own file (item 9).

## Context

**Phase 3 replaces the original's routines with C++, bottom-up through the call graph** (plan §5). The interpreter keeps running whatever is not yet ported.

**The test that admits a replacement** (ADR-003 item 3, plan §6.3) compares it with the original:

- on the calls a replay makes;
- on every memory write, port write and register its callers read;
- with basic-block coverage of the original, or a written reason for each block it leaves out.

**The digests must not move** (plan §5's exit). Paced time (ADR-008) already lands interrupts where the program waits, whatever its work costs. So a native routine changes no digest if it does exactly what the original did.

**Plan §3 asked for the data segment to be named from the first routine**, as a packed struct overlay, "so native code reads `m_ds.shipEnergy` rather than `Byte(0x3A1F)`".

## Decision

**1. A native routine stands in for the original at an entry.**

- **Hooking.** `Pc::Hook(segment, offset, name, routine, contract)` puts a C++ function at an address.
- **Where the CPU stops.** `Cpu` stops at a hooked entry, at the start of a step, after taking any interrupt that was due. It executes nothing there, so an interrupt handler's entry can be hooked too.
- **Running it.** `Pc` then runs the routine. It takes no time, which is what paced time expects of work (ADR-008).
- **What `Machine` knows.** Only that native code is at an address. The game's routines, names and contracts live in `GameLogic` (`NativeRoutines.cpp`), and R9 holds.

**2. Bodies and entries are separate.**

- **The body.** A ported routine's body is a function of `Elite::Guest&`. It works on the registers of the contract Symbols.tsv gives the routine, and touches no return address.
- **Calling native from native.** Native code calls a native body as a plain C++ call.
- **Calling the original.** Native code calls code still interpreted through `Guest::Call` (`Pc::CallNear`):
  - a near call with a return offset nothing executes (FFFFh);
  - the program runs until it comes back there.
- **The hooked entry** is the body, followed by the return the original makes (`Pc::ReturnNear`).
- **Shared bodies.** Where several entries share one body, they share its C++ function: the nine `SetSinCosN` and nine `RotateBySinCosN` entries.

**3. The data overlay is generated, and it is not a struct laid over the bytes.**

- **The generator.** `Tools/GenerateOverlay.py` writes `GameLogic/DataOverlay.h` from Symbols.tsv. Every DS row of kind data, table or text becomes a field, typed from its size and a table's element type: `DataField<std::uint8_t>`, `DataField<std::uint16_t>`, `DataBlock<N>`, `DataTable<entry bytes, entries>`, or `DataAt` when the table does not say.
- **How native code uses it.** It reads `guest.Get(DS.randomState0)` through `Guest`.
- **Kept current in CI.** CI fails if the header in the tree is not what the tool would write (`--check`).
- **Why not a struct over the bytes.** Plan §3's packed struct is set aside because every write must pass through `Machine::Memory`:
  - paced time decides that a loop is waiting when no byte has changed (ADR-008);
  - item 4's journal records every byte a run changes;
  - a struct laid over the bytes would bypass both.
- **What carries over.** The names, and the readable tree they were meant to give, are the same. Phase 4 re-lays the overlay as typed state, as the plan says.

**4. Every call can be compared with the original, inline, as it is made.**

When `NativeCode::SetVerifying(true)` is on, a call of a native routine that is not already inside a comparison goes through these steps:

1. **The original runs first, all of it, with no hook in force.** Every native routine it reaches is therefore compared through it. That includes routines only native code calls, such as `QuadrantArcTangent` inside `ArcTangent2`. Every byte it changes is journalled (`WriteJournal`, with what the byte held before), and every port access it makes is logged.
2. **Memory is undone and the registers are put back.**
3. **The native routine runs, with the bus in replay** (`PortRouter::StartReplay`). Each access must be the next one the original made: a read returns the value the original read, and a write is compared but not made again.
4. **The two are compared:**
   - **Registers:** every register but those the contract says the routine clobbers. SP, SS, CS and IP are always compared.
   - **Flags:** those the contract names as results.
   - **Memory:** every byte either run changed, except the dead stack below SS:SP once the routine has returned.
   - **Ports:** the sequence of accesses.
5. **On a mismatch,** it is recorded (`NativeCode::Mismatches`), and the run carries on from the original's outcome, so one mismatch does not hide the next.

A call whose original cannot be undone is not compared: its outcome stands, and it counts as unverifiable. The original cannot be undone when it waited (the clock moved), took a hardware interrupt, or called the BIOS, DOS or mouse services.

**Why inline rather than captured states.** Compared inline, every call in the corpus is a test case. It needs no state files and no snapshot of the devices. Measured 2026-10-10, g++ -O2, the five replays:

| Run | Time |
|---|---|
| Interpreted | 2.7 s |
| Native | 2.5 s |
| Compared (118,228 calls, all verified, none unverifiable, no mismatch) | 2.8 s |

**5. When a routine is accepted.** A routine is accepted when every compared call agrees, and those calls ran every reachable instruction of the routine and of everything it calls or runs into. Each instruction left out needs one of:

- **A constructed input.** A `GameLogicTests` case that calls the entry directly from a state the reference reached itself, such as `MathsTests`, which drives `RatioArcTangent` through the divide trap.
- **A written reason** in `Design/NativeCoverage.tsv`.

`ReferenceRunner --compare --native-report` writes what each comparison executed, and `Tools/RoutineCoverage.py` checks it against MapReference's static walk. For the 23 entries here every instruction is covered, so no reason is written yet.

**6. What gates in CI.** `GameLogicTests.CorpusTests` runs the corpus three ways:

| Run | Must hold |
|---|---|
| Interpreted | Every digest. |
| Native | Every digest, unchanged. |
| Compared | No mismatch, and no overrun (item 8). |

**7. How far Phase 3 has got** is the number of the original's instructions the corpus still executes, from `ReferenceRunner --native --coverage`. The phase's exit is zero. Measured 2026-10-10: interpreted, the corpus executes 8,717 distinct instructions; with this change's native routines, 8,500.

**8. Native code waits on a thread of its own.**

- **Which routines.** A routine hooked as one that waits (`NativeEntry::waits`, `Pc::Hook`) runs on the native thread, and so does everything it calls. It waits through `Pc::Wait`, one idle turn of a waiting loop: `Idle`, then the end of the run if that is where the clock now is, then any interrupt now due, taken as the CPU takes it at the loop's next instruction.
- **How a run ends inside one.** When the clock reaches the end of a run there, in `Wait` or in original code it calls, the native thread hands the machine back and `RunUntil` returns. The next `RunUntil` carries on where it stopped.
- **Why it stays deterministic.** The host thread and the native thread take turns through two semaphores, so only one runs at a time and a run is still a function of its inputs (ADR-008). `MachineTests` is clean under ThreadSanitizer.
- **Teardown.** A machine destroyed while a routine waits unwinds the native thread by an exception.
- **Every other native routine is a plain call,** with no thread switch. If one waits past the end of a run, the call goes on to its return and counts as an overrun. The corpus tests fail on any overrun, which is how a routine that should have been marked as waiting is found.
- **A routine that waits is never compared.** Its original could not be undone, and the original of `Start` or `GameLoop` would never return. What it calls through hooks is compared instead, each call on its own. So native code that waits calls the work routines through `Guest::Call`, and work routines call each other directly.
- **The alternatives weighed:**
  - C++20 coroutines would have made every routine above a wait a coroutine.
  - Platform fibers do not exist where the Linux tools run.
  - A state machine for each routine is the Phase 4 scheduler, built too early.

**9. Each subsystem has its own file.**

- **Lists and registration.** The 19 subsystems of Symbols.tsv each have a source file in `GameLogic` with a list of `NativeEntry`, and `InstallNativeRoutines` walks every list. A port therefore touches only its own subsystem's file.
- **Returns.** An entry says how the original returns: `RET n`, `RETF n`, or `IRET` for an interrupt handler's entry, which is hooked like any other because the CPU takes a due interrupt before it stops at a hook.
- **What native code can do.** It makes interrupts through `Pc::CallInterrupt`, as it makes calls through `Pc::CallNear`. It reaches the code segment, any segment, CGA memory, the stack and the ports through `Guest`.

## What this forecloses

- **Native code that bypasses the machine.** Native code changes the program's memory and ports only through `Machine::Memory` and the `PortRouter`. Anything else would be invisible to paced time and to the comparison.
- **A port admitted by its digests alone.** The digests are the last line of defence; the comparison and its coverage are the test.
- **Hooks in clocked runs compared with DOSBox-X** (ADR-003). Native code takes no cycles, so such a comparison would mean nothing.
- **Editing `GameLogic/DataOverlay.h` by hand.**
