# ADR-041 — The native game loop, third step: the PC's devices give way to what the game uses of them

**Status:** accepted 2026-10-13 for its first part, with the change that implements it, step c of piece 4 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), under D70. Each later part amends it as it lands.

## Context

After [ADR-040](ADR-040-the-fields-stop-counting.md) the platform is the IBM PC the game drove in 1987, without its processor and its memory map: a vector table of 256 far addresses and a ROM that answers bytes, the 8259, the 8253, the keyboard's controller and the 8255, the BIOS's services and data area, and DOS's file services. The game reaches it through `Hardware`, in the PC's terms: vectors, ports, divisors, the ROM's bytes.

D70 asks that the devices go a device at a time with every replay exact. So each part keeps what the game observes, to the cycle, and drops what it never observes. The steps are:

1. **The vector table and the ROM** (this part).
2. The interrupt controller, as the scheduler's two request lines.
3. The timer, as the two programmings the game makes.
4. The keyboard's controller, as a timed queue of scan codes.
5. The BIOS's services, as native video, clock and teletype.
6. DOS's file services, as native file operations.

## Decision, first part: handlers by identity, and the ROM's answers as questions

1. **The game's two hardware interrupts are served by a handler, not by a far address** (`Irq`, `IrqHandler`). Int 8 (the timer) and int 9 (the keyboard) each have the BIOS's handler, the game's own, or a stray one: a vector that names code no routine stands in for. At power-on the BIOS's serve both. The game installs its own by name (`Hardware::SetHandler`) and puts the BIOS's back by name, where the original wrote the vector and later wrote back what it kept. The game's own handlers are given to the platform by what they are (`Platform::SetGameHandler`, `EntryKind`), not hooked at their addresses in the code segment.
2. **Int 8's segment written again** (`InstallDivideAndKeyboardInterrupts`) keeps the game's handler there. Were the BIOS's handler there, its offset would land in the code segment, at code no routine stands in for: the handler becomes `Stray`, and an interrupt taken there throws, as the hook's absence did.
3. **The vectors nothing dispatches go.** Nothing raises int 0 (the divide trap: each divide calls the overflow routine itself) or int 24h (DOS answers a failed call with CF set), so the game's writes of them, and the reads that kept the old entries to write back, go. Int 22h, DOS's terminate address, was never read.
4. **The ROM's bytes become two questions.** Whether a mouse driver is loaded (`Hardware::MouseDriverLoaded`) is what the game's test of int 33h's vector found: a vector at the driver's entry, whose first byte is not an IRET, exactly when a driver was loaded. Whether the ROM is an Amstrad's (`Hardware::AmstradRom`) is never: FC00:0016 held zeros. Int 33h with no mouse behind it (`Platform::CallAbsentMouse`) still costs the ROM's IRET on the instruction clock, which the game port measures, and still throws when a driver is loaded with no mouse behind it, as a test can arrange.

### Measured 2026-10-13, the first part

| Check | Result |
|---|---|
| The idle trajectory | The probe of ADR-038, per test, the paced clock after every idle hashed: every test of the branch before it identical in count and hash, but the platform tests this part rewrote: `AnInterruptRunsTheHandlerItsVectorNames` and `TheVectorTableIsAsTheBiosLeavesIt` gone with the software interrupt and the vector table, `AStrayHandlerIsRefused` and `TheBiosServesBothInterruptsAtPowerOn` new. |
| The suite | `GameLogicTests` passes under g++, 166 tests, the corpus's digests, pictures, outcomes and answers among them, with no re-base |
| Unused code | clang 18's warnings over `GameLogic`: none |
| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |

## What this forecloses

- **A far address as the name of a handler.** A handler is the BIOS's, the game's, or stray.
- **Reading the ROM.** What the game read of it is a question the platform answers.
