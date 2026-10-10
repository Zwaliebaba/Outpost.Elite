# ADR-015 — Waiting loops de-assembled: turn signatures

**Status:** accepted 2026-10-10, with the change that implements it: `Pc::LoopTurn` taking a turn signature, `Hardware::LoopTurn`, and `WaitForTimerTick` de-assembled onto it. Amended the same day: a value that changes only with memory or a port may be left out of a signature (item 2).

## Context

**Paced time recognises a wait by what a loop's turn changed** (ADR-008 item 1). At each taken backward jump, a turn that changed no register, no byte of memory and no port since the last turn idles: the clock moves to the next device event, and the interrupts that fall due are taken there. Interrupts land only in waits. So a native loop that idled on a different turn from the original's would move where they land, and with them the digests.

**Native loops so far keep the registers.** The loops ported in Phase 3 call `Guest::JumpBack` with the registers as the original has them at its jump (ADR-010 item 8). That is exact by construction. There are 163 such sites, most of them in the docked menus, the charts and the input prompts.

**A de-assembled routine has no registers to hand over** (ADR-012). So every routine with a loop that can wait has been left at the bottom of each level.

## Decision

**1. A de-assembled loop that can wait calls `Hardware::LoopTurn(loop, {carried...})`** at every backward jump the original takes in it.
- **The signature.** The loop's address and the carried values make a signature, which stands in for the register file. `Pc::LoopTurn` compares it with the last turn's signature, as it compares registers for the original: a turn with the same signature that changed no byte and no port idles.
- **No cross-matching.** A signature never equals a register file.
- **The limit.** A signature holds at most 16 words, the address included (`Pc::MOST_TURN_WORDS`). More is a programming error, and throws.

**2. What a loop carries.** Two kinds of value the original holds in registers at its jump:
- **What the next turn reads before writing it:** a count, a cursor, the value it compares with. A delay loop that counts CX down without touching memory carries CX. Without it, every turn would look idle.
- **What the turn read from a device and still holds there:** a port, or a service's result. Reads can differ without the clock moving: paced time reports a retrace to the first status read only (ADR-008 item 2).

A value that cannot change from turn to turn may be left out. So may a value that changes only on turns that also change memory or write a port. On a turn that changes neither, the value is unchanged, so leaving it out cannot change either case of item 3. `WaitForKeyPress` and `ReadTextLine` leave out AL, which changes only when a code is taken from `keyBuffer`, and taking one changes its count. A carried value must be one the original has in a register there, nothing else.

**3. Why the native loop idles where the original does.** Take a turn that changed no byte and no port.
- **When the original idles,** its registers are equal. The signature is a selection of those registers, so it is equal too, and the native loop idles.
- **When the native loop idles and the original does not,** the registers that differ are ones the next turn writes before it reads them, because everything it reads first is carried, and the carried values are equal. Its memory is unchanged, and a device read that differed would be carried and keep both from idling. So the original's next turn computes the same registers, writes the same values and idles. Typically this is a loop's first turn, with a register left over from another loop or a call. The native loop moves the clock one turn sooner, at the same state and on the same cycle.

So in both cases the clock moves, and the interrupts are taken, at the same point of the game's state.

**4. A stack save that a loop's register code needs is not kept.** `WaitForTimerTick` pushes and pops AX around its loop only to preserve it, and a de-assembled loop never touches AX.
- **Interrupt frames move.** The frames that interrupts push during the wait land two bytes higher.
- **Nothing reads them.** The stack is neither state the game reads there nor in the digest (ADR-008 item 5).
- **When a push carries data.** A push whose value another routine reads is a different case: the stack carries data between routines, which a later decision covers.

**5. When D19's structs replace `Machine::Memory`,** the change count goes with it, because the structs are written directly. The rule then needs another way to tell a turn that changed nothing. One option is to compare the data segment and video memory with their copy at the last turn, but that misses a turn that writes a byte and writes it back. That choice is made with D19's ADR, and measured against the corpus.

**6. Measured 2026-10-10, with the pilot:**
- **The routine.** `WaitForTimerTick` is de-assembled. It is reached by address from Docking, Equipment, StartUp and Text, among them the docking computer's delays and the menus' cursor delays.
- **`GameLogicTests`:** 176 of 176 pass under g++ and clang++, with poisoning on and the coverage check clean. As in ADR-014, this was run without the three combat replays that are being re-recorded under D18. Every digest of the other 16 replays is unchanged.
- **`MachineTests`:** 157 of 157 pass, two of them new:
  - `SignatureTurnWaitsWhereTheOriginalDoes`: a signature wait ends on the original's cycle.
  - `SignatureTurnThatNeverRepeatsSpins`: a signature that never repeats stops the run as `Spinning`, as the original's loop would.

## What this forecloses

- **A de-assembled loop that waits through `Guest::JumpBack`,** or that idles by a rule of its own.
- **A signature carrying anything the original does not hold in a register at its jump.** That would make the native loop miss an idle turn the original takes.
- **Explicit `Wait` calls in place of a loop's turns.** Where the original idles is found from what the turn changed, not declared.
