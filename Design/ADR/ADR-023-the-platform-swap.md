# ADR-023 — The platform swap: every device native, and `Machine` deleted (D33)

**Status:** accepted 2026-10-11, with the change that implements it: the PC's devices moved into `Elite::Platform`, then the `Machine` and `MachineTests` projects deleted. It is the second piece of D33's route ([Reverse-Engineering-Plan.md §5](../Reverse-Engineering-Plan.md#phase-5--the-native-platform-and-the-natural-layout)), and finishes what ADR-021 began. The owner played main at `7242663`, with it, and reported that it works perfectly (2026-10-11). **Amended 2026-10-11 (D37):** the dead-code sweep removed what of the devices the game never reaches: the timer's other modes and its count written while running, the BIOS's key buffer and teletype control codes, the mouse driver's unread answers, the Amstrad paths, the CGA modes and status bits never used, `StopReason::Unported` and `Fault`, and `ServiceFault.h`. A state the game cannot reach now throws `std::logic_error`. **Amended 2026-10-13 by [ADR-041](ADR-041-the-devices-go.md) and [ADR-042](ADR-042-the-sound-is-the-games.md) (D70, D72):** the 8259, the 8253, the keyboard's controller, the BIOS's data area, the vector table and the ROM are gone; the scheduler, the time of day, the video services and the speaker's synth hold what the game observes of them.

## Context

**What was still emulated.** After ADR-021 the game's files were native, and nothing else was. Native code reached the emulated PC through `Hardware`:
- the `Pc` with its clock, scheduler and `Dispatcher`;
- `Memory` and its mapping of the `GameState`'s regions (ADR-018);
- the timer (PIT), the interrupt controller (PIC), the keyboard controller, the CGA, the game port, the mouse driver and the speaker;
- the BIOS, DOS's other services, the executable loader and the firmware.

Each device was a register-level model behind a port bus, with interrupt vectors in memory.

**What had to be kept.** A replay is a list of inputs timed against the game clock (ADR-008). The corpus, the twins and the guard (ADR-022) mean something only while replays play out exactly as before. So D33 asked for the swap to keep paced time's rules exactly, in the same unit, and for every one of the 134 corpus digests and 356 known answers to hold.

## Decision

**1. The devices are the `Platform`'s.** Each is the emulated PC's model moved without its port bus, its registers or its interrupt-vector plumbing. Each keeps the behaviour the game reaches.

| Device | From | What it lost |
|---|---|---|
| The clock, scheduler, run loop and native thread | `Pc`, `Dispatcher`, `NativeCode` | the processor interface, the memory map, the port router, the "overran" stop |
| `InterruptController` | `Pic` | the initialisation words, mask writes, specific end of interrupt and register reads |
| `IntervalTimer` | `Pit` | ports, the latch, reads, single-byte access and read-back. It has one operation, `Program(control, divisor)`, taking both bytes of a count |
| `Keyboard` | `Keyboard` | port I/O, now `Data`, `SystemControl` and `SetSystemControl`; port 62h |
| `Speaker` | `Speaker` | nothing; `SetPortB` is now `SetSystemControl` |
| `GamePort` | `GamePort` | `In8` and `Out8`, now `Read` and `Fire` |
| `Mouse` | `Mouse` | the register-level int 33h, now typed `Reset`, `Presses` and `Motion` |
| `Cga` | `Cga` | port aliasing and register reads. Registers are written by name; `ReadStatus` keeps the retrace seen once (ADR-008 item 2); it renders from the `GameState`'s video memory |
| `Bios` | `Bios` and the part of `Firmware` the game reads | the data area as memory: it is private fields, whose changed bytes and port writes are counted. The equipment word and memory size, which the game never reads |
| The BIOS's timer and keyboard handlers | `NativeFirmware` | their stack pushes. They are `Platform::RunBiosTimerHandler` and `RunBiosKeyboardHandler` |
| DOS | `Dos` | the PSP, the environment, the default transfer area, and the vectors restored on terminate. `DosFiles` (ADR-021), `Platform::PrintDosString` and `Hardware`'s version, time and read-line calls remain |
| The vector table and ROM | `Memory`, `Firmware` | memory. They are `Platform::m_vectors` and `RomByte`: the IRET stubs, the mouse driver's entry and the model byte; every other ROM byte reads 0 |
| The executable loader | `ExeLoader` | everything. `ReferenceLoadImage` puts the generated tables (ADR-019) straight into the `GameState`, relocated, with the D5 byte |

`Sha256`, `CgaFont`, `FileStore`, `DirectoryFileStore` and `Timing.h` moved into `GameLogic` unchanged. That settles what ADR-021 item 7 left for this change.

**2. Paced time's rules are kept, each in one place.**
- **8088 cycles, and work takes no time.** The clock moves only in `Platform::Idle`, reached from `LoopTurn` (through `NoteTurn`) and from `Spend`.
- **A wait ends at the next event.** `Idle` moves the clock to the earliest of the timer's next interrupt, the keyboard's next delivery, the CGA's next status change and the run limit.
- **Idle loops skip ahead as `Pc::NoteTurn` decided it** (ADR-015). `LoopTurn` and `NoteTurn` compare the turn signature, `MemoryChanges()` and `PortWrites()`, and both counts stay exact:
  - memory changes are counted where each write lands: `GameState::Store`, `Bios::Store` and `SetInterruptVector`;
  - port writes are counted per `Hardware` operation, as many as the original's OUTs:

    | Operation | Port writes |
    |---|---|
    | the BIOS's mode set | 35 |
    | the tone divisor, the tick divisor | 3 each |
    | the keyboard's acknowledge, the cursor, a CRTC register | 2 each |
    | end of interrupt, system control, the game port, mode control or colour select | 1 each |
- **Pacing points charge their cost** (ADR-013): `Hardware::Spend` calls `Platform::Spend`.
- **Interrupts are taken where they fall due** (ADR-014 item 10): at STI (`EnableInterrupts`), at end of interrupt, when the timer is reprogrammed, at pacing points, and at the end of each wait and turn (`EndTurn`). The controller keeps the PIC's rules: in service, end of interrupt, masking.
- **The BIOS's handlers stand in** whenever the game puts the BIOS's vectors back: they are hooked at F000:E000 and F000:E040.
- **The timer's handler gets the direction flag** it needs (ADR-017 item 6) through `Platform::Backward()`.
- **Routines that wait run on the native thread** with ADR-010 item 8's strict handoff: two binary semaphores, `HandToNative` and `ReachedRunLimit`. Destroying the `Platform` unwinds the thread with `NativeAbandoned`.

**3. What lived outside the `GameState` became `Hardware` operations.** Only the data segment, the code segment's image and video memory are memory now. Each native access to the rest became an explicit call:

| Routine | It now uses |
|---|---|
| `IsMouseDriverInstalled` | `InterruptVector` and `HandlerOpcode` |
| `InstallDivideAndKeyboardInterrupts`, `RestoreDivideAndKeyboardInterrupts` | `InterruptVector` and `SetInterruptVector`. The vectors the game saves still go into its data segment, so the digest still sees them |
| `InstallTimerInterrupt`, `RestoreTimerInterrupt` | the vector operations, with `ReadBiosClock` and `SetBiosClock` |
| `ChainToBiosTimer` | `RunBiosTimerTick` |
| `CheckCheatArgument` | `CommandTail()`, in place of reading the PSP |
| `Start`'s Amstrad check | `RomByte` |
| the int 24h swaps in `Start`'s disk path and in `SaveScreenshot` | the vector operations |

The stack pushes of the entry frame, of interrupt frames and of the BIOS's handlers are no longer written. Nothing read them: every interrupt the game takes also writes the end-of-interrupt port, and the turn probe (item 6) saw no turn change.

**4. Seven differences, none of which the game reaches.**
- With no mouse driver, a mouse call returns CX and DX as 0.
- `RunBiosTimerTick` no longer swaps vector 8. Only the Amstrad's path takes it.
- DOS's terminate no longer restores vectors 22h to 24h.
- A find before the transfer area is set throws `std::logic_error`; it used to default to PSP:0080, which the game never used (ADR-021 item 3).
- A DOS read or write past the data segment's end throws `std::out_of_range`, and a refused read writes nothing.
- `ServiceFault` no longer carries CS:IP, and `StopReason::Overran` is gone: there is no instruction pointer to report.
- `ReferenceRunner`'s `ran` line counts interrupts taken instead of instructions, and the unmapped-port lines are gone.

**5. `MachineTests`' 133 tests became 86 in `GameLogicTests`.** Each test class is named for the class it tests (R7), with `PlatformRig.h` as its rig.

| Class (tests) | What became of it |
|---|---|
| `Sha256Tests` (5), `CgaFontTests` (3) | moved unchanged |
| `SpeakerTests` (7), `GamePortTests` (5) | moved, with the calls renamed |
| `CgaTests` (12) | re-expressed through named register writes; the port-aliasing test became `RegistersKeepTheBitsTheCardHas`, and one test of the retrace seen once was added |
| `PitTests` (9) | 7 re-expressed in `IntervalTimerTests`, plus `OnlyBothBytesAreProgrammed`. The latch test went with the latch; a count written while running cannot happen when every count comes with its control word |
| `KeyboardTests` (6) | 5 moved; the test of port 62h went with the port |
| `PicTests` (10) | 6 re-expressed in `InterruptControllerTests`, without specific end of interrupt; OCW3, initialisation and automatic end of interrupt deleted; the dispatcher's test became `PlatformTests.HardwareInterruptsAreTakenAsThe8088TakesThem` |
| `BiosTests` (7) | 5 re-expressed; the background colour and the unknown video function deleted. `PowerOnStateIsWhatTheBiosLeavesForDos` added, absorbing the firmware's data-area test |
| `FirmwareTests` (8) | the vector, model byte and mouse tests became `PlatformTests`' vector-table and mouse-check tests; the three handler tests became `BiosHandlersDoWhatTheRomDid`, which keeps the exact data-area changes, port writes and cycles of each path; the equipment word and memory size deleted |
| `DispatcherTests` (4), `PcTests` (8) | re-expressed in `PlatformTests`, without the registers and the stack |
| `NativeCodeTests` (11) | 10 re-expressed in `PlatformTests`; an unmarked routine that waits cannot stop the run any more |
| `MouseTests` (5) | 3 moved as typed calls, the no-driver test to `PlatformTests`, the unknown function deleted |
| `DosTests` (16) | 7 re-expressed in `PlatformTests` (version, printing, buffered input, time, the first fault). The 9 file tests deleted: `DosFilesTests` covers them, and ran side by side with the emulated DOS until this change |
| `ExeLoaderTests` (8) | deleted. `ReferenceImageTests` and `StateDigestTests` hold their facts; the return to the PSP became `ProgramEndsWhenItsStartReturns` |
| `MemoryTests` (5), `PortRouterTests` (4) | deleted with what they tested |

**6. Measured 2026-10-11,** on each commit and on the two together over ADR-022's guard.

| Check | Result |
|---|---|
| `GameLogicTests` | 150 of 150 under g++ and clang++: ADR-022's 64 and `MachineTests`' 86. `DosFilesTests` keeps its 7, now checking the values each states outright, as ADR-021 item 5 made them able to. After the first commit, with `MachineTests` still built: 64 of 64 and 133 of 133 |
| Warnings | none under g++ `-Wshadow` or clang++ `-Wimplicit-int-conversion -Wshadow` |
| The corpus | after each commit, all 26 replays meet all 134 state digests under `ReferenceRunner` built with each compiler; 134 pictures and 134 outcome lines checked, 0 differ |
| The twins | all 356 known answers met; 356 pictures and 356 outcome lines checked, 0 differ; each answer checked exactly once (ADR-018 item 6's probe) |
| Timing | a probe hashed every turn's signature and the clock before and after it, in `a97fd78` and in this change. Over the corpus the 26 hashes are identical: 42,696,850 turns, 21,943,863 of them moving the clock. The whole first-commit test suite hashes identically too, over 74,575,525 turns |
| Run time (g++, Release, all 26 replays in sequence, medians of 15 rounds) | 1.930 s before, 1.621 s after, wall time; 1.749 s and 1.481 s user time |
| Size | `Machine` 54 files and 9,283 lines, `MachineTests` 24 files and 4,582 lines, deleted. With what moved and was added, the tree is 5,710 lines smaller |
| clang-tidy | clean on all 76 portable translation units, with `HeaderFilterRegex` no longer naming `Machine` |

The first build of the moved code was 38% slower on the corpus. callgrind found 30% of the instructions in the out-of-line `GameState::Locate`, mostly from `CopyLinePairs`. Inlining the region lookup, data segment first, brought the corpus to 2.228 G instructions against the emulated PC's 2.395 G.

**7. Not verified here.**
- **The MSVC build and the shell.** `Outpost/Application.cpp` changes in `Describe` (it takes the `Platform`; there is no CS:IP to name, so a spin counts turns, and the unported stop names `UnportedAt()`) and in `Application::Run`, which names Elite's types and draws with `Platform::Render`. A translation unit that makes the same calls type-checks under g++, but the file itself is compiled only by CI.
- **Playing it.** A green build says nothing about whether the game draws, sounds and reads the keys (AGENTS.md §3). The owner plays this build. *The owner ruled the same day that the natural layout does not wait for it (D34).*

## What this forecloses

- **An emulated device.** A device the game uses lives in the `Platform`, typed, with no port bus and no vector in memory. `Hardware` stays the stateless boundary (ADR-021).
- **A timing change without a ruling.** Paced time's rules and both counts are kept exactly, so every replay still plays as recorded. Changing them moves the corpus, and needs the owner's ruling and re-recorded replays (D33).
- **The machine-level tests.** Ports, the latch, OCW3, the loader and the memory map are not tested because they do not exist; the devices are tested at the level the game uses them.
