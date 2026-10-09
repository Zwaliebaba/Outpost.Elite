# ELITEL.EXE — reverse-engineering and native port plan

**Status:** proposed 2026-10-09; the owner ruled D1–D5, D7 and D8 the same day, D10–D13 after Phase 0's work, and D6 and D14 from Phase 1's findings (§8). D4 was first ruled for the EGA build, then reverted to this file the same day because no EGA build is available, which also made D9 moot. A ruling becomes a decision when the ADR in §9 records it (AGENTS.md §6). **Phase 0's work is done on its branch, and closes when that branch is merged:** ADR-001 to ADR-003 are written, the provenance check found this copy cracked (ADR-001), and the three checkers are in `Build/`, with their CI steps no longer guarded.

**The goal as stated:** reverse-engineer `ELITEL.EXE` so the game runs in a modern Windows environment without major functionality change. **The goal as this plan reads it:** a native x64 C++23 / Direct3D 12 program, built under AGENTS.md, that plays the same game as the DOS original and whose source can be read and changed. §2.1 explains the difference: the first half of the stated goal can be met today without any reverse engineering.

## 1. What the binary is

Measured on 2026-10-09 from the file at the repository root (SHA-256 `440b06de18c121855635d55e7a95308d5748144d8c4c63f082cbb864af95e55e`) with an MZ header parse and an `ndisasm` linear sweep of the code segment. The sweep decodes some data as code, so its counts are approximate; Phase 1 replaces them with recursive-descent figures.

It is the 1987 Firebird *Elite* for the IBM PC, CGA build. The version string is `Release:1? 1-10-87` (DS:0x7780) and the exit message is "Thank you for playing ELITE - Have a nice DOS!".

| | |
|---|---|
| Format | DOS MZ, 97,200 bytes, no overlay, 64-byte header, 7 relocations, entry 0000:0000, stack 138F:03F8 |
| Layout | one code segment (image 0x0000–0x8F5F, 36,704 bytes); data segment 0x08F6 (image 0x8F60 to the end, 60,432 bytes); one further segment value, 0x13CF, fixed up at CS:0x7C03 |
| Code | hand-written 8086 assembly, 8086 instruction set only, no x87; about 14,300 instructions, 1,370 call sites and 550 returns, so of the order of 400–500 routines |
| Video | CGA mode 4 (320×200, 4 colours) written directly at B800; CRTC at 0x3D4, mode at 0x3D8, colour select at 0x3D9, retrace polled at 0x3DA; BIOS modes 0, 1, 2 and 4 and the palette set through int 10h |
| Time | PIT channel 0 reprogrammed to divisor 0x04A9 (about 1 kHz), serviced by the game's own int 8 handler at 0x0215 |
| Sound | PC speaker only: PIT channel 2 and port 0x61 |
| Input | the game's own int 9 handler at 0x0201 reads port 0x60, so the BIOS keyboard is unused during play; joystick on port 0x201; mouse through int 33h |
| DOS | int 21h functions 09, 0C, 1A, 2C, 30, 3C, 3D, 3E, 3F, 40, 41, 43, 4E, 4F — the commander files (`*.cdr`, default commander `JAMESON` at DS:0x7229) and the start-up checks; int 24h hooked to 0x02F0 around disk operations and the screenshot save; exits with `retf` to PSP:0 |
| Other machines | detects an Amstrad PC by its ROM string at FC00:0016 (compared with "Amstrad" at DS:0x2001, flag at DS:0x2000) and chains the timer to the BIOS only on that path |
| Protection | a word from the printed manual ("Program Protection", DS:0x2424), three attempts, then "the program is cleared": the routine at 0x0554 zero-fills CS from 0x0564 to 0x8F50. `Start` calls it at 0x0072 whenever `diskOperation` (DS:0x2009) is 0, which is every exit to DOS as well as a failed protection. **Defeated in this copy** by a patch at 0x0532–0x0538 (ADR-001) |

The interface text is plain ASCII — screen titles, ship names, government and economy names, the system-name digram table — rather than the tokenised text of the 6502 versions, which makes the data segment comparatively quick to map.

## 2. Four things settled before the plan was worth running

### 2.1 DOSBox already meets the stated goal

DOSBox Staging or DOSBox-X runs this file on Windows 11 today with no functional change, and packaging it behind a launcher is an afternoon's work. If "runs on modern Windows" is the whole goal, the right plan is that one. Reverse engineering pays only if the goal is a native source base that can be read, fixed, extended and reshaped. The rest of this plan assumes that is the real goal.

**Ruled (D1):** a native source port.

### 2.2 The binary is a commercial copyright work on a public `main`

`Zwaliebaba/Outpost.Elite` is public and `ELITEL.EXE` is on `main` at `4cfcee3`. That is distribution of the original program now, not an exposure that might arise later. The C64 attempt in this repository's history carried the same risk as its R1 and, at its last revision, was still waiting on an answer from the rights holders. Removing the file from the tip stops further distribution but does not recall forks or caches, and rewriting a public history is drastic and only partly effective. A faithful port inherits the problem a second time through its data: the ship models, font, text and tables are the original's bytes.

**Ruled (D2, D3):** the repository becomes private, so the reference binary and generated data tables may be committed. As of 2026-10-09 it is still public; the change is the owner's to make in the repository settings, and what forks and caches took during the public period stays out there.

### 2.3 This copy is not pristine

From 0x003B the start-up code reads the DOS clock, adds seven seconds, reads the clock again and compares. The conditional branch that would close that loop is two `NOP`s at 0x0054, so a seven-second wait — taken at start-up and again every time play returns through 0x003B — has been patched out, by the publisher or by someone later. One visible patch means there may be others, and the protection path is the obvious candidate. "Without functionality change" needs a known build to be measured against. If the owner has the rest of the distribution — the other display builds, the loader, the manual — the EGA build may also be a better reference than a four-colour CGA one.

**Ruled (D4):** this file is the reference, because it is the only copy the owner has; no unpatched copy and no other display build is available. The patch at 0x0054 is documented, not reverted, so the port starts without the seven-second wait, as the reference does. Phase 0 looks for further patches, and the protection path is where to look first: if this copy already bypasses the protection, that bypass is what D5's patch documents rather than a second change.

**Found in Phase 0 (ADR-001):** it does. The answer check's branch is two NOPs at 0x0537 and the flag it tested is forced at 0x0532, so the question screen still appears but any answer passes. That makes two patch sites in all, with this file's provenance a cracked copy. D5 then needs one byte more, not a second bypass: the host sets the protection's "already shown" flag at DS:0x25E4 in the loaded image, so the screen never appears.

### 2.4 This repository has done this once already

Until `4cfcee3` the tree held a near-complete C++ port of *Commodore 64* Elite from Mark Moxon's annotated 6502 source: nine ADRs, more than 20,000 lines of design documents, a 6502 oracle and a replay digest. `Sync` deleted all of it, and AGENTS.md says that history binds nothing here. Two of its lessons are still cheap to keep and expensive to relearn. Running the original beside the port is what made that port possible, and deleting the oracle took the suite from 469 tests to 131 (`Design/README.md` at `3da3590`). And its documents grew faster than its game did; this plan is meant to stay short. One thing is new and makes this attempt harder: there is no annotated source. Naming several hundred routines from a bare binary is a cost the C64 attempt never paid, and it dominates Phases 1 and 3. **Ruled:** the reset was because the PC version was wanted; the method was not the problem. The C64 attempt's verification ideas — the replay digest, the differential oracle — are reused here as ideas, not as code.

## 3. Approach

There are three ways from this binary to native source.

**A. Decompile and rewrite.** Run Ghidra's decompiler over the code and hand-clean its output into C++. On this binary it is the weakest option: hand-written 16-bit assembly passes values in registers, returns status in the carry flag (about 50 `stc` and `clc`), rewrites its own instructions and relies on a divide-overflow trap (§7.1). The decompiler's C for that is a reading aid, not a starting point. Nothing runs until nearly everything is written, and there is nothing to compare it with.

**B. Static recompilation.** A translator turns each instruction into C++ statements on a register file and a 1 MB array, giving a native executable that is bit-exact from the start. The usual reason to recompile rather than interpret is speed, and it does not apply: interpreting a 4.77 MHz 8088 is a negligible load for a modern core. The costs remain — the self-modifying line drawer, four indirect-branch sites, interrupts that can arrive between any two instructions — and a translator bug is silent, because its output has nothing to be checked against.

**C. A hosted interpreter, then replacement routine by routine (recommended).** Build a small 8086 interpreter and exactly the PC this program touches — §1's table is the whole list — inside a native Win32, D3D12 and XAudio2 shell. The unmodified original then runs as a native Windows program at the end of Phase 2, which meets the stated goal early, with the original's own code. After that, the original's routines are replaced with C++ one at a time: the interpreter hands control to a native function when execution reaches a hooked entry address, and each replacement is proved by running both versions from the same captured state and comparing everything they wrote. When the whole replay corpus executes no original instruction, the interpreter leaves the shipping executable, and at the end of Phase 4 it leaves the tree (D7). This is the pattern OpenRCT2 used on RollerCoaster Tycoon 2, with an interpreter in place of DLL injection, because 64-bit Windows cannot execute 16-bit code at all.

C is recommended because it is the only option in which the game always runs and every step is checked against the original. Its weak points, stated so they can be watched:

- **The interpreter is new code with its own bugs**, and a bug there looks exactly like game behaviour. It is validated against an independent reference (§6.1) before anything is ported against it.
- **Native code passes through an assembly-shaped stage.** While interpreted and native routines share state, native routines have to work on the original's data segment and register contract. The mitigation is to name the data segment from the first day — a packed struct overlay with a `static_assert` on every field offset — so native code reads `m_ds.shipEnergy` rather than `Byte(0x3A1F)`, and the later re-layout is mechanical rather than a rewrite.
- **Interrupt interleaving changes when the interpreter goes.** The timer and keyboard handlers can run between any two instructions of the original; native code runs them at defined points. Phase 1 has to establish what state they share with the main loop before Phase 4 can show that the change is safe.
- **Hand-written assembly ignores routine boundaries.** Shared tails, fall-through into the next routine and entry points in the middle of a routine are normal. Hooking needs an exact call graph, which is why Phase 1 comes before any porting.

## 4. Tooling

**Ghidra is the static workbench.** Its MZ loader handles the segmented 16-bit image, and with DS set to 0x08F6 across the code segment its cross-references resolve into the data segment. It is used for disassembly, the call graph, cross-references and data typing, not for its decompiler output (§3 A). IDA is not needed.

**The knowledge lives in a text file, not in a Ghidra project.** A checked-in symbol table, one row per address — kind, name, registers in and out, flags returned, notes — is the source of truth, with two small Ghidra scripts to import and export it. A Ghidra project is a binary database that does not diff, review or merge, and the naming of several hundred routines is the most expensive thing this project produces; it has to be reviewable in a pull request.

**DOSBox-X with its debugger is the independent dynamic reference**: instruction traces, breakpoints on memory writes, and screenshots of known screens. It validates our interpreter and is never shipped or linked.

**Python under `Tools/`** does extraction, symbol import and export, and trace comparison. R14 binds what the executable is built from, not development tools.

## 5. Phases

Each phase ends on a criterion that can be checked.

### Phase 0 — Decisions and ground

Finish the provenance check of §2.3: compare the code against what the binary itself implies (dead branches, `NOP` runs, jumps over intact code) to find any further patches, and establish whether the copy protection is still live in this copy. Write ADR-001 to ADR-003 (§9) from the rulings in §8. Write the three checkers that AGENTS.md §6 calls early work — `Build/CheckFormat.py`, `Build/CheckProjectFiles.py` and `Build/RunClangTidy.py` — because the first C++ lands in Phase 2 and until then every rule they would enforce is review's problem.

**Exit:** the provenance check is written up, with every patch found listed by address; the ADRs are merged; the checkers gate in CI; the reference binary's hash and the D5 patch's byte list are recorded in ADR-001.

### Phase 1 — Map the binary

Disassemble by recursive descent in Ghidra. Classify every byte of the code segment as code or data; bound, name and give a register contract to every routine; list every irregular construct — the int 8, int 9, int 0 and int 24h handlers, the four indirect branches and their tables (`jmp [bx+0x4D15]`, `call [bx+0x75C0]`, `jmp ax` at 0x3D08, `jmp [bx]` at 0x7049), the self-modified instructions, the routines with more than one entry and the shared tails. Then map the data segment: the text, ship blueprints, font and dashboard bitmaps, maths tables, the digram table, the default commander, and the market and equipment tables. Group the routines into subsystems: raster and lines, 3D and ships, planet and sun, text, dashboard and scanner, input, sound, timer, flight, AI, combat, docking, hyperspace, galaxy, market, equipment, save and load, protection, start-up.

The annotated 6502 sources are a legitimate aid for recognising an algorithm — galaxy generation, market prices, the ship AI. They are never the PC version's truth: this is a separate implementation for a different machine, and where the two differ the binary wins.

Three questions have to be answered here because later phases depend on them: what state the timer and keyboard handlers share with the main loop (§3, §7.3); whether the game advances by elapsed timer ticks or once per frame (§7.6, D6); and whether any palette write is timed to land mid-frame (§7.5).

Phase 1 runs alongside Phase 2: static analysis bounds the routines, and the interpreter's traces show which addresses actually run.

**Exit:** no unclassified byte in the code segment; every routine executed by the replay corpus is in the symbol table with its contract; the three questions are answered in writing.

### Phase 2 — The host

An 8086 interpreter covering the instructions this binary uses. Every flag the 8088 defines is exact, AF and PF included, even though the code never tests those two (no `pushf`, `lahf`, parity branch or BCD instruction appears in the sweep): they cost little and ADR-003 compares them. Instructions carry approximate 8088 cycle counts so that the timer cadence and the calibrated delays (§7.4) behave. Around it: a 1 MB address space and the devices in §1 — CGA mode 4 with retrace timing, PIT channels 0 and 2, the PIC, the keyboard at port 0x60, the speaker gate, the joystick port and an int 33h mouse. BIOS and DOS are emulated at the call level, for exactly the functions §1 lists, and anything else fails loudly, so the surface cannot drift into a general DOS emulator.

The shell is a Win32 window with a D3D12 presenter (the original's resolution and palette, integer scale, corrected to 4:3, D8), XAudio2 synthesising the speaker from time-stamped port writes, the keyboard mapped to scan codes, the joystick through XInput, the mouse through raw input, and `.cdr` files in a user folder. The reference binary is read from the repository at start-up, not embedded. The protection patch (D5) is applied to the loaded image from the byte list in ADR-001, so the file keeps its recorded hash and every comparison is against the original plus exactly that patch.

Interrupts are injected at instruction-count boundaries rather than wall-clock moments, so a session recorded as inputs against instruction count replays bit for bit. That replay is the regression gate for everything after it.

ADR-004 settles the projects when the first one is created. A starting point to argue with: an interpreter library; an `Engine` library for the window, D3D12, audio and input; a `GameLogic` library (namespace `Elite`) for the port; the executable; a test project per library, each added to `.clang-tidy`'s `HeaderFilterRegex`.

**Exit:** the reference binary, with only the D5 patch applied, boots, skips the protection, docks, launches, flies, fights, hyperspaces, saves and reloads a commander; its boot trace matches DOSBox-X's (§6.1); a seed corpus of recorded replays reproduces identical digests on every run. *This meets the stated goal.*

### Phase 3 — Replace routines with C++

Hook and replace bottom-up through the call graph, so that each native routine calls only native routines or interpreted ones it has not yet displaced. A sensible order is: arithmetic and table look-ups; text and number printing; galaxy generation and the market; line drawing and the rasteriser; 3D projection and ship drawing; planet, sun and stardust; dashboard and scanner; the hardware-facing routines for sound, keyboard and video, whose native versions write to the host's devices; flight, AI, combat, docking and hyperspace; the docked screens; and the main loop and the interrupt handlers last.

A replacement is accepted when it matches the original on every captured call (§6.3) and those calls covered every reachable basic block of the original routine. Native code uses the named data-segment overlay (§3), so the tree is readable from the first routine ported.

**Exit:** the full replay corpus executes no original instruction, and every digest is unchanged.

### Phase 4 — Detach

The interpreter leaves the shipping executable. Video memory becomes a native index buffer handed to the presenter; the speaker becomes a native synthesiser; the timer and keyboard handlers become a fixed-tick scheduler at the rate D6 sets; DOS file calls become `std::filesystem`, with the `.cdr` format kept byte-compatible so that commanders saved by the original still load. The protection is not ported (D5). The data-segment overlay is re-laid as typed state, now that nothing interpreted reads its bytes, and the ship models, font, text and tables become C++ tables generated from the reference by a script under `Tools/` and checked in (D3).

At the end of the phase the interpreter, its harness and the per-routine differential tests are deleted (D7). From then on the replay corpus and §6.4's known answers are the only guard, so the corpus has to be complete before the deletion: every subsystem in §6.2 reached, and every digest re-based under ADR-006.

This is the phase in which replay digests are expected to change, because the interleaving and the clock change. Each change is a ruling recorded in ADR-006 with its cause, never a re-baseline to make CI green.

**Exit:** the interpreter is gone from the shipping executable and from the tree; the replay corpus, re-based under ADR-006, passes; the game has been played and looked at, not only built (AGENTS.md §3).

### Phase 5 — Beyond the original (outside this plan)

Higher resolution, smoother motion, a modern control scheme: each is a design decision for the owner after Phase 4, and each changes functionality on purpose. They are named only to mark where the faithful port ends.

## 6. Verification

### 6.1 The interpreter against an independent reference

First, a published per-instruction test corpus for the 8088 (such as the SingleStepTests 8088 suite, used as development tooling under its own licence) checks each instruction in isolation. Second, the boot trace — every instruction from entry to the first keyboard read, which needs no input and is therefore deterministic — is compared register for register with DOSBox-X's trace of the same binary. Third, static screens (title, status, market, both charts, equipment) are compared pixel for pixel with DOSBox-X screenshots. The interpreter is not trusted as an oracle until all three hold.

### 6.2 Replays

Inputs are recorded against instruction count, with a digest of the data segment and video memory at fixed intervals. Phases 2 and 3 never change a digest; Phase 4 changes them only by recorded ruling. The corpus is built to reach every subsystem: trading, every equipment item, combat with each ship type, docking by hand and by computer, hyperspace and galactic hyperspace, witch space, death, the escape pod, saving and loading.

### 6.3 Differential tests per routine

During any replay the interpreter can capture the whole machine state at each call of a given routine. The harness restores each captured state, runs the original and records every memory write, every port write, the registers and the flags its callers read; then restores again, runs the native version and compares. Basic-block coverage of the original routine, measured from the same captures, shows whether they exercised all of it; an uncovered block gets a constructed input or a written reason.

### 6.4 Known answers from outside the binary

Elite's procedural galaxy is thoroughly documented: the starting seeds, Lave's economy, government and tech level, the prices in Lave's market. These are ground truth independent of both the interpreter and the port, and cost almost nothing to check.

## 7. Hazards found in the binary

1. **Divide overflow is part of the arithmetic.** The int 0 handler at 0x025E inspects the faulting instruction, sets AL to 0x7F or AX to 0x7FFF, and resumes, so the game relies on the trap to saturate quotients. It copes with both the 8086 convention (return address after the `div`) and the 286-and-later one (return address at it), but on the later path it skips exactly two bytes, so a `div` with a memory operand would resume mid-instruction on a 286 or newer. Every one of the roughly 60 `div` and `idiv` sites must reproduce the saturation explicitly in C++. The interpreter models the 8088 convention, the machine the code was written for. *Phase 1:* the handler mis-sizes three-byte divides: for `div word [si+4]` at 0x2369 and 0x2392 it reads the ModRM byte, which is even, and returns AL = 0x7F with AH and DX unchanged. Reference-Map.md lists every divide that reaches the trap.
2. **Self-modifying code is dead.** The code that rewrites itself (42 `CS:` writes to 18 instructions) is a triangle filler at 0x1B7A–0x233F that nothing can enter; Phase 1 decoded from every byte offset to be sure (Reference-Map.md). The line drawer the game uses, `DrawLine` (0x16D1), does not modify itself. The interpreter still executes self-modifying code correctly, so a Phase 2 trace that contradicts this costs nothing.
3. **Two interrupt handlers run inside the game.** The timer handler, at about 1 kHz, calls 0x7170; the keyboard handler calls 0x7463 and never chains to the BIOS. Whatever they write, the main loop reads asynchronously (§3). *Phase 1:* what they share is counters, the keyboard's tables and buffer, and the sound engine. The sound engine's effects are speaker toggles at the tick rate, so it has to run on the audio clock, not once a frame (Reference-Map.md).
4. **Some delays are calibrated for a 4.77 MHz 8088.** The routines at 0x4618 and 0x05CC wait for vertical retrace and then spin 700 iterations of `dec ax / jnz`. On a modern CPU that spin takes no time; in the interpreter it takes as long as the cycle model says. What the delay protects — a tear-free blit, a palette change, a copy inside the blanking interval — decides how the native version replaces it. *Phase 1:* both precede block copies to the screen, timed against the beam to avoid tearing; in the port they become presenting a finished frame.
5. **The palette is written outside mode changes.** Colour-select writes to 0x3D9 occur at 0x49F8, at 0x7CB7 (stepping through a table) and at 0x7D2A. If any of them is timed within a frame, the presenter must model a split palette rather than one palette per frame. *Phase 1:* none of the three is timed within a frame: a fuel-leak flash once a frame, a docked screen's border colour, and the set-up after mode 4.
6. **Game speed may have depended on the CPU.** If flight and AI advance once per frame rather than per timer tick, the original slowed down on a real PC as the screen filled with ships, and "without functionality change" needs a definition before Phase 4 can meet it (D6). Phase 0 found a 20-per-second rate limiter at 0x777D with no caller anywhere in the code (ADR-001); whether something reaches it through data is a Phase 1 question that bears directly on D6. *Phase 1:* it advances once a frame with a minimum frame time of 50 ms, selectable with F1–F10 on the pause screen, and ruled as the port's speed (D6).
7. **The Amstrad path is a second machine.** It changes the timer's behaviour and probably input. The port keeps only the IBM PC behaviour unless the owner says otherwise.
8. **The docked screens are CGA text mode.** Mode 1, 40×25: the CGA's character ROM draws their glyphs, and that font is not in the binary (D14).
9. **Noise is made from the program's own code.** Routine7AB3 sends bit 1 of successive bytes of CS:0x07D0–0x0BCF to the speaker, so those 1,024 bytes are sound data as well as code.

## 8. Decisions for the owner

| # | Decision | Recommendation | Ruling, 2026-10-09 |
|---|---|---|---|
| D1 | Is the goal a native source port, or is a DOSBox package enough? | A native source port (§2.1). | **Native source port.** |
| D2 | `ELITEL.EXE` on the public `main` | Remove it from the tip and keep the reference outside the tree. | **Make the repository private.** The owner makes the change; it was still public on 2026-10-09. |
| D3 | Original data in the port | Read it from the user's copy at start-up (the OpenRCT2 model). | **Follows from D2:** generated tables are checked in, and the reference binary is committed so CI can run the oracle until Phase 4 ends. |
| D4 | The reference binary | An unpatched copy with a recorded hash. | **This file, `ELITEL.EXE`, with its known patch documented** — the only copy available. Phase 0 looks for further patches (§2.3). |
| D5 | Copy protection | Remove it from the port; answer it in the hosted original without altering the binary. | **Remove it by patching.** One byte, DS:0x25E4, set in the loaded image so the file keeps its hash (ADR-001). |
| D6 | What "the same speed" means | The original's own minimum frame time, once Phase 1 showed how it paces itself. | **The game's own frame time:** 50 ms by default, selectable with F1–F10 as on the original's pause screen. No model of 1987 slowdowns (Reference-Map.md). ADR-006 records it with the change that implements it. |
| D7 | The oracle after Phase 4 | Keep the interpreter in the test project for as long as the code changes. | **Delete it after Phase 4.** The replay corpus must be complete before it goes (Phase 4). |
| D8 | The picture | As the original drew it, at integer scale, corrected to 4:3. | **CGA mode 4 as this build draws it, at integer scale, corrected to 4:3.** |
| D9 | A second build beside the reference | Keep one if available: diffing two builds separates the display code from the game logic. | **Moot:** there is no second build. Phase 1 finds the hardware boundary by hand. |
| D10 | Code shared between shaders | Keep §2 as written: vertex and pixel shaders only. | **Allow `<Name>.hlsli` in `Shader/`,** registered as a `None` item; AGENTS.md §2 and the checker say so. |
| D11 | `.clang-tidy`'s example command defines the Windows macros AGENTS.md §4 reserves for one header | Remove the two defines. | **Removed.** |
| D12 | The Amstrad path (§7.7) | Port the IBM PC behaviour only. | **IBM PC only** (ADR-001 item 5). |
| D13 | What answers a design question | The reference is the design. | **The reference is the design.** AGENTS.md names ADR-001's reference as what the game is and this plan as what sequences the work; the owner rules only where the reference is silent or a change is wanted. |
| D14 | The 8×8 font for the docked screens' text mode, which the binary does not contain | Draw our own lookalike. | **Draw our own** 8×8 code-page 437 font matching the CGA's, for the characters the game uses. |

## 9. ADRs this plan produces

| ADR | Subject | When |
|---|---|---|
| [ADR-001](ADR/ADR-001-scope-reference-and-fidelity.md) | Scope, reference binary and fidelity (D1, D4, D5, D8, D9) | Phase 0 — written |
| [ADR-002](ADR/ADR-002-original-data-and-the-repository.md) | Original data and the repository (D2, D3) | Phase 0 — written |
| [ADR-003](ADR/ADR-003-verification.md) | Verification: interpreter validation, replays, differential tests, the oracle's lifetime (§6, D7) | Phase 0 — written |
| ADR-004 | Projects and layout | Phase 2, when the first project is created |
| [ADR-005](ADR/ADR-005-interpreter.md) | The 8086 interpreter: what one step is, interrupts, timing, and what checks it | Phase 2 — written |
| ADR-006 | Time, pacing and the replay digests (D6) | Phase 4, with the change that implements it |
