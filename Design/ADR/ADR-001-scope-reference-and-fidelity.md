# ADR-001 — Scope, reference binary and fidelity

**Status:** accepted 2026-10-09, from the owner's rulings D1, D4, D5, D8 and D9 in [Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner).

## Context

The goal is to make `ELITEL.EXE` run on modern Windows without major functionality change. DOSBox already does that with no work, so the project is only worth doing for a native source base that can be read and changed (plan §2.1). There is one copy of the original, and it was measured for this ADR with `python Tools/ScanReference.py` (Capstone 5.0.7). The scan disassembles the code segment by recursive descent from the program entry, the four interrupt handlers and the three jump tables the code indexes.

**What the file is.** `ELITEL.EXE`, 97,200 bytes, SHA-256 `440b06de18c121855635d55e7a95308d5748144d8c4c63f082cbb864af95e55e`. It is the 1987 Firebird *Elite* for the IBM PC, CGA build, version string `Release:1? 1-10-87`. Its credits give the game copyright to Bell & Braben and the code copyright to Realtime Games Software Ltd, name Andy Onions as the writer, and Firebird Software as the publisher. It is a DOS MZ executable with one code segment (36,704 bytes) and one data segment (60,432 bytes). The plan's §1 has the rest.

**It is a cracked copy.** The scan reaches 12,474 instructions and 32,223 of the 36,704 code-segment bytes. Only four of the NOPs it reaches are not assembler padding after a jump, and they sit at two sites:

| Site | What is there | What was there, inferred | Effect |
|---|---|---|---|
| 0x0054–0x0055 | `cmp dh,ch` then `nop nop` | a conditional branch back into the clock-reading loop at 0x004A | the seven-second wait at start-up, and on every return through 0x003B, is gone |
| 0x0532 | `mov byte [0x25E3],1` (`C6 06 E3 25 01`) | `cmp byte [0x25E3],1` (`80 3E E3 25 01`): same length and operands, and otherwise nothing in the program would read the flag | the "answer correct" flag is forced on |
| 0x0537–0x0538 | `nop nop` | `jnz 0x053F`, a jump to the failure path | any answer is accepted; the failure path 0x053F–0x0553 (decrement the three tries, then wipe the program) is unreachable |

The protection works like this, which is how the last two rows were inferred. The routine at 0x04A3 shows the question, reads the answer, and then waits one timer tick (the call to 0x7792). During that tick the timer handler decodes the expected word (0x73BA–0x7414) and sets DS:0x25E3 if the answer matches. That flag has two references in the code: the write at 0x7415 and the instruction at 0x0532. The pattern — the check's branch removed and the flag it tests forced — is a crack. Who made the 0x0054 change, and when, cannot be told from the file.

**What the scan cannot rule out.** A patch that replaces code with other working code leaves no NOP. The 4,481 bytes the scan does not reach are 35 single padding bytes, 12 short or zero-filled data areas, and 23 code regions. All 23 code regions follow a `ret` or an unconditional jump, which is how a separate routine looks rather than how a patch looks. Most are probably entered through data: the `jmp ax` at 0x3D08 takes its targets from data, and the scan follows neither it nor any other code address held in data. The largest, 2,110 bytes at 0x1B7A, holds the self-modifying stepping loops of plan §7.2. None of the 23 lies next to a patch site except the protection's own failure path, and Phase 2's execution traces settle which of them ever run. Two of them matter beyond provenance and go to Phase 1: 0x777D, which, when the flag at DS:0x9CF9 is set, waits until the 1 kHz timer has counted 50 ticks since it last ran and then restarts the count, a 20-per-second rate limiter with no caller in the scan (D6); and 0x0A9A, 166 bytes with no caller.

The MZ header's checksum field (0x1399) does not satisfy the documented definition, and restoring the 0x0054 branch does not make it satisfy it, so it says nothing either way.

## Decision

1. **The goal is a native C++ source port (D1)**, built the way plan §3 C describes. The original runs first under an in-house 8086 interpreter in a native Win32, D3D12 and XAudio2 shell. Its routines are then replaced with C++ one at a time under differential tests, and finally the interpreter is removed.

2. **The reference is this file, byte for byte, with its patches (D4).** It is the only copy the owner has; no unpatched copy and no other display build is available. The EGA build named in an earlier ruling the same day does not exist here, so the second-build question (D9) is moot. "Without functionality change" is measured against this file plus the one change in item 3. The seven-second wait stays out, as it is in the reference.

3. **The copy protection is removed by one in-memory patch (D5).** When the host loads the reference, it sets the byte at DS:0x25E4 (file offset 0xB584) from 0x00 to 0x01. That byte is the protection routine's "already shown" flag, so the routine at 0x04A3 returns before drawing anything. The two flags it would otherwise write need nothing: DS:0x25E1 is already 0xFF in the file, which keeps the timer's answer check from running, and DS:0x25E3 has no reader once 0x0532 is a store. The file on disk stays byte-identical to the hash above, so every comparison is against the reference plus exactly this byte. Phase 2 confirms that the hosted original starts without the screen and plays normally. If it does not, this ADR is amended rather than the patch quietly widened.

4. **The picture is CGA mode 4 as this build draws it (D8):** its 320×200 four-colour frame and palette, scaled by a whole number and corrected to a 4:3 shape. Anything sharper, smoother or larger is Phase 5.

5. **Only the IBM PC behaviour is ported** (ruled by the owner, D12). The Amstrad path — the ROM check at start-up, the flag at DS:0x2000 and the timer chaining that depends on it — stays in the hosted original as the reference has it. It never executes there, because the emulated PC has no Amstrad ROM string at FC00:0016, and the port does not carry it.

6. **Speed is not defined here.** What "the same speed" means (D6) is decided in ADR-005, after Phase 1 shows how the game paces itself and Phase 2 measures it at 4.77 MHz.

## What this forecloses

- A DOSBox package as the product, and porting from decompiler output or static recompilation (plan §3 A and B).
- Any reference other than this file. An unpatched copy or another build turning up later is a new ADR, not an edit to this one, because every recorded digest would move.
- Restoring the seven-second wait or the copy protection in the port without a new ruling.
- Raising fidelity claims about the original 1987 release: the claims this project can make are about this copy.
