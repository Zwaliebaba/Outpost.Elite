# ADR-005 — The 8086 interpreter

**Status:** accepted 2026-10-09, with the change that implements it: `Machine::Cpu`, `Machine::Memory`, `Machine::PortBus`, `Machine::HostServices` and the `CpuConformance` runner (ADR-004).

## Context

Phase 2 hosts the unmodified reference in an interpreter of our own (plan §3 C), and Phases 2 and 3 judge every native routine against that interpreter (ADR-003). Its behaviour therefore has to be the 8088's wherever the game can observe it. Its shape also fixes how the rest of the host — the PC devices, the BIOS and DOS services, the replay recorder — attaches to it.

The game is hand-written real-mode 8086 code, and it relies on three things a general emulator might get wrong:

- the 8088's divide-error convention, which it uses as saturating arithmetic (plan §7.1);
- hardware interrupts arriving between its instructions, the 1 kHz timer and the keyboard (plan §7.3);
- the BIOS and DOS, at the level of their calls.

## Decision

1. **One `Step()` is one instruction, its prefixes included.** It returns an approximate clock count, and `InstructionCount()` counts steps. Replays are recorded against that count (ADR-003 item 2).

2. **A REP string instruction runs to completion within one `Step()`.** A real 8088 can take an interrupt between iterations, and this interpreter cannot. The game's string instructions are short block copies and fills, and nothing it does depends on the difference. If a trace ever shows otherwise, this item is reopened.

3. **Hardware interrupts are queued and taken at an instruction boundary.** `RequestInterrupt(vector)` queues one, and it is taken at the start of the next step when IF is set and no interrupt shadow is in force. A taken interrupt's entry and its handler's first instruction run in the same step. Loading any segment register with MOV or POP shadows the next instruction, as on the 8088, and STI's effect is delayed by one instruction.

4. **The divide error is the 8088's.** It vectors through entry 0 with the address of the next instruction pushed; later processors push the faulting one. The flags pushed are those of the microcode's last ALU step, which the conformance suite pins down: `high − divisor` for DIV and AAM, and the last trial subtraction, with CF clear, for IDIV. The game's own handler at 0x025E then does the saturating, operand-size mistakes included (Reference-Map.md).

5. **BIOS and DOS are reached through `HostServices`.** INT n, INT 3 and INTO consult it first. If it services the interrupt, the CPU does not vector through the table; otherwise the CPU vectors as normal. CPU exceptions and hardware interrupts always vector through the table, so the game's own handlers run exactly as the reference installs them.

6. **Timing is approximate and deterministic.** The count is Intel's documented best case, plus effective-address cycles, plus 4 clocks per word over the 8088's 8-bit bus, plus 2 per prefix. MUL, IMUL, DIV, IDIV and AAM get Intel's minimum, and nothing models the prefetch queue, wait states or refresh. There is no wall clock and no randomness, so a run is a function of its inputs. The timer, the retrace-timed delays and the joystick read take their timing from this count (Reference-Map.md), so it is "about right", not exact. The game's speed comes from its own frame-time setting (D6), not from this count.

7. **What checks it.** The whole SingleStepTests 8088 v2 suite passes: 3,007,000 of 3,007,000 tests in 323 files, measured on 2026-10-09 with `python3 Tools/CpuConformance.py` against runners built by g++ 13.3 and clang++ 18.1. That includes all 207 files for the opcodes the game executes, which `--listing` derives from `Tools/MapReference.py`. Every register, every byte of the 1 MiB address space, and every flag except those the suite's metadata marks undefined are compared. `MachineTests` holds eight unit tests on top. They were run on Linux against a stand-in for the test framework and have not yet run under vstest.

8. **What the suite does not reach**, and is therefore unverified:
   - POP CS; 8F with a non-zero reg field; FE /2–7;
   - the register forms of LEA, LES, LDS and FF /3, FF /5;
   - WAIT, HLT and LOCK;
   - REP before IMUL;
   - the trap flag, and interrupt timing.

   The game executes none of these opcodes. Its use of interrupts is checked by ADR-003's boot trace and replays, not by the suite.

## What this forecloses

- Interrupts landing inside a REP string instruction (item 2), unless this ADR is reopened.
- A cycle-exact model of the 8088. The count is good enough for the timer cadence, not for racing the beam, and nothing in the game needs more (Reference-Map.md, question 3).
- Emulating the BIOS or DOS below the call level: no BIOS ROM, no DOS kernel. A call the services do not implement fails loudly (plan §5, Phase 2).
