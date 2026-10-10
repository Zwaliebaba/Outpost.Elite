# ADR-011 — Detaching the interpreter: the Dispatcher

**Status:** accepted 2026-10-10, with the change that implements it, Phase 4's first step. It records D17, the owner's ruling of 2026-10-10 on how far Phase 4 goes ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)).

## Context

**Phase 3 ended with every routine native** (ADR-010 item 7). The six-replay corpus runs none of the original's instructions, and every digest is unchanged.

**The shell still ran on the interpreter.** `Outpost` built its `Pc` on `Cpu`, which by then had three jobs left:
- finding the hooked entry at each step;
- entering a hardware interrupt;
- running the machine's own code: the ROM's stubs and handlers, and the PSP's `INT 20h`.

Plan §5 says the interpreter leaves the shipping executable in Phase 4, and D7 deletes it when Phase 4 ends.

**Phase 4 is more than the plan described.** Plan §3 expected the data segment's re-layout as typed state to be mechanical. It is not:
- the 21,252 lines of native code still work through the original's registers and computed data-segment addresses;
- they hold 4,991 register uses and 512 computed addresses, measured by `grep` over `GameLogic/*.cpp`;
- typed state therefore means de-assembling every routine.

Asked, the owner ruled Phase 4 in full and detach first (D17), in this order:
1. The shell runs without the interpreter, with the digests unchanged.
2. The native routines are de-assembled subsystem by subsystem onto typed views of the data segment. The tests keep comparing each one with the original.
3. The views are backed by plain structs, and the devices become native.
4. The corpus is completed, and the interpreter is deleted (D7).

**The ROM's timer handler does run in a native game.**
- **Why.** `RestoreTimerInterrupt` puts the BIOS's int 8 back, then writes the PIT's control word. The PIT raises IRQ 0 at once on a control word (`Pit.cpp`).
- **Where it was seen.** The interpreted twins of `TwinScreenshots`, `TwinFlightScreenshot` and `TwinDiscMenuUncompared` ran F000:E000 after the next STI.
- **Why it went unseen.** With `Cpu` underneath, those 16 ROM instructions ran silently.
- **The corpus never reaches it.** Whether IRQ 0 comes while the BIOS's vector is in place depends on timing.

## Decision

**1. A `Pc` runs its program on a `Processor`** (`Machine/Processor.h`).
- **What it is.** The registers, the step, the interrupt line and the hook map.
- **What runs through it, unchanged:** paced time, the hooks, the native thread and the comparison.
- **Who picks it.** The caller passes a factory (`ProcessorFactory`):
  - `MakeCpu`, the interpreter;
  - `MakeDispatcher`.

**2. The `Dispatcher` executes none of the program's instructions** (`Machine/Dispatcher.h`).
- **A step** takes a due hardware interrupt exactly as `Cpu` does: the acknowledge, the three pushes, IF and TF cleared, the vector loaded and the same cycles. Then it stops at the hooked entry, and the `Pc` runs the native routine.
- **Any other code** stops the run as `StopReason::Unported`, at the address. There is no fallback to interpreting it.
- **Two single instructions it does run,** because they are the machine's own and not the program's, each at the cycles `Cpu` charges:
  - an `IRET` in the ROM: the stubs that vectors nothing serves point to, such as int 33h with no mouse driver;
  - the `INT 20h` at a PSP's offset 0, where `Start`'s last `RETF` lands.

**3. The ROM's two hardware handlers are native on a `Pc` that does not interpret** (`Machine/NativeFirmware.h`).
- **Which:** the timer handler at F000:E000 and the keyboard handler at F000:E040.
- **Hooked by** `Pc` itself.
- **Exact in what anything can observe:**
  - every byte, the stack and the flags pushed for int 1Ch included;
  - every port access;
  - every register;
  - the cycles of the instruction clock the game port runs on.
- **How it is checked.** `DispatcherTests` runs each against the ROM's code, interpreted, from the same state, on all five paths: four through the timer handler and the keyboard's one.
- **The one difference that remains.** Paced time counts the native handler as one step toward the spin limit where the ROM's code is 12 to 22 steps, one per instruction.

**4. `Cpu` moves to a project of its own, `Interpreter`,** a static library in namespace `Machine` (ADR-004).
- **Who references it:** `MachineTests`, `GameLogicTests`, `ReferenceRunner` and `CpuConformance`.
- **Who does not: `Outpost`.** The shipping executable cannot contain the interpreter, and anything that reached it would fail to link.
- **What `HostServices` takes.** The registers, not the `Cpu`, so that `Machine` no longer names it.

**5. Where each processor runs.**

| Run | Processor |
|---|---|
| `Outpost` | `Dispatcher` |
| `CorpusTests`' native run, which is therefore also the proof that no original instruction runs | `Dispatcher` |
| Every uncompared native twin (`TwinRig`) | `Dispatcher` |
| `ReferenceRunner --native` | `Dispatcher` |
| Comparisons: `--compare`, compared twins, `ComparisonRig` | `Cpu`. They interpret the original, and `Pc` refuses to compare on a processor that does not interpret. |
| The interpreted reference runs | `Cpu` |

**6. Measured 2026-10-10,** g++ and clang++ Release, and g++ Debug.
- **The corpus.** The six replays reproduce all 40 digests on the `Dispatcher` (`ReferenceRunner --native`). They execute no instruction, not even a stub, and do not stop.
- **The twins.** The six uncompared twin tests now run their native twin on the `Dispatcher`. Their digests and the files they write agree with the interpreted twin's.
- **The suites.** `MachineTests` 154 pass and `GameLogicTests` 173 pass. The coverage check is unchanged (ADR-010 item 5).
- **The interpreter itself.** `Cpu`, moved, passes all 3,007,000 SingleStepTests 8088 cases (`Tools/CpuConformance.py`).

**7. The rest of Phase 4, in D17's order.** Each step gets its own ADR when it is taken:
- **De-assembly.** Ordinary C++ functions with parameters and results replace register contracts and flags, subsystem by subsystem, on typed views of the data segment. The register contract stays only at the entries the comparison still needs.
- **Plain state and native devices.** The views get plain structs behind them, and the digests are computed from them. Video, sound, the tick scheduler and the files become native.
- **The end.** The corpus is completed and the interpreter deleted.

## What this forecloses

- **The interpreter in the shipping executable,** or an edge from `Outpost` to `Interpreter`.
- **A `Dispatcher` that interprets.** Unported code stops the run. It is never run some other way.
- **Comparing native code with the original on a `Dispatcher`.**
- **More of the ROM natively than its two hardware handlers and its `IRET` stubs.** Anything else in the ROM stops a `Dispatcher` run as `Unported`. That includes the `HLT` at F000:E070, which only a program that has already ended reaches.
