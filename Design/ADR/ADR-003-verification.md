# ADR-003 — Verification

**Status:** accepted 2026-10-09, from [Reverse-Engineering-Plan.md §6](../Reverse-Engineering-Plan.md#6-verification) and the owner's ruling D7 in its §8. **Amended 2026-10-09:** item 1's boot trace and static screens, after the first DOSBox-X traces and the CGA showed that neither could be compared the way this ADR first said. **Amended 2026-10-10:** item 2's replays are recorded in paced time and digested as ADR-008 says. **Amended 2026-10-10 again:** item 3's test runs inline, on every call a replay makes, as ADR-010 says: the original runs on the live state and is undone, in place of a captured state restored. **Superseded in part 2026-10-10 by [ADR-017](ADR-017-the-interpreter-deleted.md):** D7 deleted the interpreter, its harness and the per-routine differential tests. The replay corpus and the known answers (ADR-016) are the guard, and DOSBox-X remains the reference for screens.

## Context

The port claims to be the game in the reference binary (ADR-001), so every claim needs something outside the port to check it against. In Phases 2 and 3 that something is the original's own code, running under our 8086 interpreter beside the C++ that replaces it. But the interpreter is new code too, and a bug in it looks exactly like game behaviour. It has to be checked against references that are independent of us before it can judge anything.

The owner has ruled that the interpreter does not outlive Phase 4 (D7). The C64 attempt in this repository's history did the same with its 6502 oracle, and its suite went from 469 tests to 131 (`Design/README.md` at `3da3590`). Whatever the port is held to after Phase 4 therefore has to be recorded, in full, before the oracle goes.

## Decision

**1. The interpreter is checked against three independent references before anything is ported against it.** None of them is linked into the build (R14); they are development tools.

- **Per-instruction tests.** The SingleStepTests 8088 suite, about 3 million tests generated on real 8088 hardware, run against the interpreter one instruction at a time. It is fetched by a script under `Tools/` rather than committed, unless its repository's own licence file is confirmed to allow committing it. Every flag the 8088 defines for an instruction is compared, AF and PF included: computing them costs little, and an exclusion list is somewhere for a real bug to hide. Flags Intel documents as undefined for an instruction are masked, and any use of one by this binary is a Phase 1 finding.
- **The boot trace.** Every instruction from the program entry to the first call of `GetKey` (CS:7616), with the ADR-001 patch applied, compared register for register with the trace of the same binary in DOSBox-X's heavy debugger. `Tools/ReferenceTrace.py` records that trace, and `Tools/CompareTrace.py` compares two.

  The two traces cannot be compared raw. Each emulator is deterministic once its clock is pinned: the game reads the time of day twice in `RestartPlay`, so both start at 00:00:00 on 1 January 1980, as a PC/XT without a clock card does. But the two are not aligned with each other, because the 1 kHz timer lands wherever each one's cycle accounting puts it. The comparison therefore sets aside what is not the program's:
  - DOSBox-X's own BIOS and DOS code, in segment F000;
  - each run of the game's interrupt handlers;
  - the record per iteration that DOSBox-X writes for a REP instruction;
  - the game's three polling waits on time, at 0599, 05D0 and 4602.

  It also treats as inputs what does not come from the program:
  - what the game reads from ports that move with time (the timer, port 0x61, the CGA status, the joystick), and the two variables where it keeps such a value;
  - interrupt vectors, which point wherever each emulator's ROM keeps its handlers, and the copies the game saves;
  - what the video BIOS leaves in AX, and the OEM byte in BH after DOS's version call, both of which belong to the BIOS or DOS that answers.

  An input register may differ until it agrees again; if the difference reaches any other register, that is a divergence. Every other register must agree at every remaining instruction. Flags are reported, not failed, because they include flags the 8088 leaves undefined and flags computed from those inputs.

  **Measured 2026-10-09 on `ELITES.EXE` (ADR-007), DOSBox-X 2024.03.01 against itself:**
  - Its `cycles` set to 310, 200 and 1000 give traces of 45,539, 50,636 and 43,590 instructions. All three reduce to the same 28,652 records.
  - Two runs at 310 are byte-identical.

  **Measured 2026-10-09 on `ELITES.EXE`, the host (ADR-006) against DOSBox-X:** the host's 39,255 instructions reduce to the same 28,652 records as DOSBox-X's, and every register agrees at every one of them; in 320 records only input registers differ. Defined flags differ in 6,740 records, and every one is accounted for: undefined flags (AF after shifts; SF, ZF, PF and AF after MUL and IMUL; everything after DIV and the divide trap), flags computed from inputs, and two flag bugs in DOSBox-X itself (ADR-006 item 7). The comparison passes.
- **Static screens.** Compared with DOSBox-X screenshots (`Tools/ReferenceScreens.py`), taken by the same step script that drives the host:
  - **Both charts** are drawn by the game in mode 4 and are compared pixel for pixel.
  - **The title's ship turns**, so a screenshot catches whichever frame the wall clock gives. It is compared with the host frame that matches it, and finding no match is the failure.
  - **The status, market, equipment and inventory screens** are text mode, drawn with our own font (D14), which is close to the CGA's but not the same. They are compared by character and attribute in video memory, not by pixel.

**2. Replays are the regression gate from Phase 2 onward.** A replay is a list of inputs played in paced time (ADR-008), not against wall-clock time or the interpreter's instruction count, so it reproduces bit for bit however long the code between two waits takes. At points the replay names, it records a digest of the data segment and video memory (ADR-008 item 5). Phases 2 and 3 never change a digest, and Phase 4 changes one only through ADR-008, which records the cause. The corpus is built to reach every subsystem:

- trading, and every equipment item;
- combat with every ship type;
- docking by hand and by docking computer;
- hyperspace, galactic hyperspace and witch space;
- death and the escape pod;
- saving and loading a commander.

**3. Every replaced routine passes a differential test.** During any replay the interpreter can capture the whole machine state at each call of a chosen routine. The test restores a captured state and runs the original, recording every memory write, every port write, the registers, and the flags the callers read. It then restores the same state, runs the C++ version, and compares the two. Basic-block coverage of the original routine, measured from the same captures, must be complete, or each uncovered block gets a constructed input or a written reason. A routine is not replaced in the shipping path until its test passes.

**4. Known answers from outside the interpreter.** The galaxy seeds and the system-name digrams are checked against Elite's published facts. Derived system data is not: this version computes tech level and population by formulas that differ from the 6502 versions (Phase 1, `Symbols.tsv` at 0x1199). Lave's government, economy, tech level and market prices are therefore checked against the values the reference itself stores in its default commander, which agree with its own code. Under AGENTS.md the reference is the design, so a published 6502 value is not a reason to change anything.

**5. The oracle ends with Phase 4 (D7).** At the end of Phase 4 the interpreter, the capture harness and the per-routine differential tests are deleted from the tree. Before that:

- the replay corpus has to reach everything in item 2;
- its digests have to be re-based under ADR-008;
- the known-answer tests have to be in the suite.

From then on, the replays and the known answers are what the port is held to.

**6. Everything runs in CI.** The tests live in a `*Tests` project per library (ADR-004) and run under `vstest.console.exe` on Debug|x64, as `.github/workflows/build.yml` already does for every `*Tests.vcxproj`. The reference binary is in the tree (ADR-002), so tests that need it never skip. Each test project keeps its `SuiteSmoke` placeholder until its first real test lands (AGENTS.md §3).

## What this forecloses

- Porting a routine against the interpreter before item 1 holds.
- Re-basing a digest to get a red build green. A digest moves only through ADR-008, with its cause.
- Any per-routine comparison against the original after Phase 4. A behaviour question raised later is answered from the replays, from the known answers, or by restoring the interpreter from history, not by asking the original directly.
