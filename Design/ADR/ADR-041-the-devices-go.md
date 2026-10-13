# ADR-041 — The native game loop, third step: the PC's devices give way to what the game uses of them

**Status:** accepted 2026-10-13 for its first part, with the change that implements it, step c of piece 4 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), under D70. Each later part amends it as it lands. **Amended 2026-10-13, the second part (D72):** the interrupt controller, the timer's channel 0 and the keyboard's latch are one scheduler. **The third:** the BIOS is its time of day and its video services. **The fourth:** DOS's file services stay as D27 made them, already native.

## Context

After [ADR-040](ADR-040-the-fields-stop-counting.md) the platform is the IBM PC the game drove in 1987, without its processor and its memory map: a vector table of 256 far addresses and a ROM that answers bytes, the 8259, the 8253, the keyboard's controller and the 8255, the BIOS's services and data area, and DOS's file services. The game reaches it through `Hardware`, in the PC's terms: vectors, ports, divisors, the ROM's bytes.

D70 asks that the devices go a device at a time with every replay exact. So each part keeps what the game observes, to the cycle, and drops what it never observes. The steps are:

1. **The vector table and the ROM** (this part).
2. The interrupt controller, the timer's tick and the keyboard's controller, as one scheduler (the second part).
3. The BIOS's services, as native video, clock and teletype.
4. DOS's file services, as native file operations.

After the first part showed that, with the replays exact, these parts mostly re-express the PC's own rules under other names, the owner ruled:

- **D72 (2026-10-13).** Steps c to e go on as planned.

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

## Decision, second part: one scheduler for what paced time waits for

1. **The 8259, channel 0 of the 8253 and the XT keyboard's latch are one class** (`Scheduler`), in place of `InterruptController`, `IntervalTimer` and `Keyboard`. It holds the tick, the keys' queue and latch, and the timer's and the keyboard's requests, with the rules the game met them by: a mode 3 square wave whose rising edge requests the tick, a control word that sets a low output high being an edge, edges that coalesce while one is pending, a code delivered 1 ms after the latch empties and held until the acknowledge, the timer's request outranking the keyboard's, a level in service holding back its own and lower ones, and the end of interrupt ending the highest in service. The scheduler names the two interrupts (`Irq`); vectors 8 and 9, the mask and the other six lines, which nothing raises, go.
2. **The mode 3 arithmetic is a record of its own** (`SquareWave`, the old `IntervalTimer::Channel`), which the tick, channel 2 and the speaker's log share. **Channel 2 is the tone timer** (`ToneTimer`), programmed by its divisor alone, until the sound is synthesised (step d).
3. **The system control port is the platform's** (`Platform::SetSystemControl`): bit 0 gates the tone timer, bits 0 and 1 reach the speaker, and bit 7 holds the keyboard (`Scheduler::HoldKeyboard`), in the order the 8255's model passed them on.
4. **The tick is programmed by its divisor** (`Hardware::SetTickDivisor`): the game writes only control word 36h. The checks of the control words it never writes, and their test, go with the control word.

### Measured 2026-10-13, the second part

| Check | Result |
|---|---|
| The idle trajectory | Every game test identical in count and hash to the first part's. The device tests moved: `InterruptControllerTests`, `IntervalTimerTests` and `KeyboardTests` are `SchedulerTests`, `ToneTimerTests` and `PlatformTests.SystemControlReadsBackAndDrivesGateAndSpeaker`, each assertion carried over, but for the vector base and mask, a masked line's request, and the control words the game never writes, which nothing can express any more. |
| The speaker | A scratch copy of each tree hashed, per test, every event the speaker's log appended (its cycle, the two bits and channel 2's state): the 150 tests the two trees share identical, 56,979 events in all |
| The suite | `GameLogicTests` passes under g++, 164 tests, with no re-base |
| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |

## Decision, third part: the BIOS as its time of day and its video services

1. **The BIOS's tick count and rollover flag are the time of day** (`TimeOfDay`), which its timer handler counts and int 1Ah reads and sets; `ClockReading` is what int 1Ah gives (`Hardware::ReadTimeOfDay`).
2. **Int 10h's mode, palette and teletype are the video services** (`VideoModes`), with the three things of the data area that reach what the game sees: the mode's columns and the cursor, where the teletype writes, and the colour select's copy, which the palette call reads back whatever the game wrote to the card since. The page and the start address, which the game never sets, are 0 throughout; the mode number, the page size, the cursor shape, the CRTC's address and the mode control's copy, which nothing read, go.

### Measured 2026-10-13, the third part

| Check | Result |
|---|---|
| The idle trajectory | Every game test identical to the second part's. `BiosTests` is `VideoModesTests`, with its clock's tests in `TimeOfDayTests`. |
| The speaker | the 158 tests the two trees share identical, event for event |
| The suite | `GameLogicTests` passes under g++, 165 tests, with no re-base |

## Decision, fourth part: DOS's file services are native already

`DosFiles` has been native since D27: DOS's file half ported call for call over the `FileStore`, with no interrupt, port or memory of the PC's left in it. What it gives is what the game observes, so under D70 all of it stays: the handles from 5, CF and AX on every path, the transfer area's 2Bh bytes, which the disc menu reads back from the game's state (the found name at 1Eh, its attributes at 15h), and the files' stamps from DOS's clock. Its names (`DosFiles`, `DosAnswer`, `DosDateTime`) say what it answers like; renaming them would change no behaviour, so they stay. Step c is done.

## What this forecloses

- **A far address as the name of a handler.** A handler is the BIOS's, the game's, or stray.
- **Reading the ROM.** What the game read of it is a question the platform answers.
- **An 8259 or an 8253 of their own.** What paced time waits for is the scheduler's, with only the two interrupts the game takes.
- **A BIOS data area.** What the game read back of it is the video services' and the time of day's.
