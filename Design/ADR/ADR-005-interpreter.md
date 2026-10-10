# ADR-005 — The 8086 interpreter

**Status:** accepted 2026-10-09, with the change that implements it: `Machine::Cpu`, `Machine::Memory`, `Machine::PortBus`, `Machine::HostServices` and the `CpuConformance` runner (ADR-004). **Amended 2026-10-09** with the PC host (ADR-006): the 8259 replaces the interrupt queue (item 3), item 7's figures are brought up to date, and the execution map and the instruction observer are added (items 9 and 10). **Superseded 2026-10-10 by [ADR-017](ADR-017-the-interpreter-deleted.md):** the interpreter is deleted (D7). This ADR records what it was.

## Context

Phase 2 hosts the unmodified reference in an interpreter of our own (plan §3 C), and Phases 2 and 3 judge every native routine against that interpreter (ADR-003). Its behaviour therefore has to be the 8088's wherever the game can observe it. Its shape also fixes how the rest of the host — the PC devices, the BIOS and DOS services, the replay recorder — attaches to it.

The game is hand-written real-mode 8086 code, and it relies on three things a general emulator might get wrong:

- the 8088's divide-error convention, which it uses as saturating arithmetic (plan §7.1);
- hardware interrupts arriving between its instructions, the 1 kHz timer and the keyboard (plan §7.3);
- the BIOS and DOS, at the level of their calls.

## Decision

1. **One `Step()` is one instruction, its prefixes included.** It returns an approximate clock count, and `InstructionCount()` counts steps. Replays are recorded against that count (ADR-003 item 2).

2. **A REP string instruction runs to completion within one `Step()`.** A real 8088 can take an interrupt between iterations, and this interpreter cannot. The game's string instructions are short block copies and fills, and nothing it does depends on the difference. If a trace ever shows otherwise, this item is reopened.

3. **Hardware interrupts come from an `InterruptSource`, at an instruction boundary.** The CPU's INTR line is wired to the 8259 with `SetInterruptSource`. At the start of a step where IF is set and no interrupt shadow is in force, the CPU asks `InterruptPending()`; when the answer is yes it runs the acknowledge cycle, `AcknowledgeInterrupt()`, which returns the vector and puts the request in service, and vectors through it. A taken interrupt's entry and its handler's first instruction run in the same step. Loading any segment register with MOV or POP shadows the next instruction, as on the 8088, and STI's effect is delayed by one instruction. Devices raise their requests in `Advance()`, which the PC calls after every step against its cycle count (ADR-006), so when an interrupt arrives depends only on the run's inputs.

4. **The divide error is the 8088's.** It vectors through entry 0 with the address of the next instruction pushed; later processors push the faulting one. The flags pushed are those of the microcode's last ALU step, which the conformance suite pins down: `high − divisor` for DIV and AAM, and the last trial subtraction, with CF clear, for IDIV. The game's own handler at 0x025E then does the saturating, operand-size mistakes included (Reference-Map.md).

5. **BIOS and DOS are reached through `HostServices`.** INT n, INT 3 and INTO consult it first. If it services the interrupt, the CPU does not vector through the table; otherwise the CPU vectors as normal. CPU exceptions and hardware interrupts always vector through the table, so the game's own handlers run exactly as the reference installs them.

6. **Timing is approximate and deterministic.** The count is Intel's documented best case, plus effective-address cycles, plus 4 clocks per word over the 8088's 8-bit bus, plus 2 per prefix. MUL, IMUL, DIV, IDIV and AAM get Intel's minimum, and nothing models the prefetch queue, wait states or refresh. There is no wall clock and no randomness, so a run is a function of its inputs. The timer, the retrace-timed delays and the joystick read take their timing from this count (Reference-Map.md), so it is "about right", not exact. The game's speed comes from its own frame-time setting (D6), not from this count.

7. **What checks it.** The whole SingleStepTests 8088 v2 suite passes: 3,007,000 of 3,007,000 tests in 323 files, measured on 2026-10-09 with `python3 Tools/CpuConformance.py` against runners built by g++ 13.3 and clang++ 18.1. That includes all 209 files for the opcodes the game executes, which `--listing` derives from `Tools/MapReference.py`: 1,952,000 tests, run again on `ELITES.EXE` (ADR-007), whose live triangle filler brings two more files than `ELITEL.EXE` needed. Every register, every byte of the 1 MiB address space, and every flag except those the suite's metadata marks undefined are compared. `MachineTests` holds ten tests of the interpreter on top, among 123 for the whole machine, and they run under vstest in CI. The suite was run again, in full and with the same result, after the interrupt source and the execution map were added.

8. **What the suite does not reach**, and is therefore unverified:
   - POP CS; 8F with a non-zero reg field; FE /2–7;
   - the register forms of LEA, LES, LDS and FF /3, FF /5;
   - WAIT, HLT and LOCK;
   - REP before IMUL;
   - the trap flag, and interrupt timing.

   The game executes none of these opcodes. Its use of interrupts is checked by ADR-003's boot trace and replays, not by the suite.

9. **An optional execution map.** `SetExecutionMap` gives the CPU one byte per linear address, and the CPU marks the first byte of every instruction it starts. The headless runner writes it out as code coverage (ADR-006), which is how Phase 1's open question of which code runs is settled. Without a map it costs one branch per step; a 10⁸-instruction benchmark and the conformance runner ran no slower with the branch in place (measured 2026-10-09).

10. **An instruction observer.** `SetInstructionObserver` tells an `InstructionObserver` about every instruction as it starts, with the registers before it executes, at the point where the execution map is marked. When a step takes an interrupt, the observer sees the handler's first instruction, not the interrupted one, so a trace made from it lists the instructions that ran. ADR-003's boot trace is recorded through it. A record taken before `Step()` would be wrong exactly there.

## What this forecloses

- Interrupts landing inside a REP string instruction (item 2), unless this ADR is reopened.
- A cycle-exact model of the 8088. The count is good enough for the timer cadence, not for racing the beam, and nothing in the game needs more (Reference-Map.md, question 3).
- Emulating the BIOS or DOS below the call level: no BIOS ROM, no DOS kernel. A call the services do not implement fails loudly (plan §5, Phase 2).
