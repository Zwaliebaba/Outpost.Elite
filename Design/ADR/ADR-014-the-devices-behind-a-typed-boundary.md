# ADR-014 — The devices behind a typed boundary

**Status:** accepted 2026-10-10, with the change that implements it: `Elite::Hardware`, with the timer's tick and the speaker's routines de-assembled onto it. It records how D17's third step makes the devices native ([Reverse-Engineering-Plan.md §5, Phase 4](../Reverse-Engineering-Plan.md#phase-4--detach)). Amended the same day with Input's and StartUp's devices (item 8).

## Context

**Where the plan ends.** Phase 4 ends with no PC left in the game:
- the video memory becomes a native buffer for the presenter;
- the speaker becomes a native synthesiser;
- the timer and keyboard handlers become a fixed-tick scheduler;
- DOS's files become `std::filesystem`, with `.cdr` files kept byte for byte.

**Where the code is.** Native code drives an emulated PC:
- `Guest::In8` and `Guest::Out8` reach its ports;
- `Guest::Interrupt` reaches the BIOS, DOS and the mouse driver;
- the interrupt flag is set and cleared in its registers.

A de-assembled routine takes a `GameState`, which has no ports (ADR-012 item 9). So every routine that touches a device has stayed at the bottom of each level. After level 2 there are 43 of them:
- 24 for a port or a service;
- the rest for the stack or for waiting, which are separate decisions.

**Why not native devices straight away.** The interpreter stays in the tests until the corpus is complete (D7). While it does, the interpreted original drives the emulated devices through their ports, and the native run has to drive them the same way:
- the PIT's channel 0 decides when IRQ0 falls;
- a port write makes a loop's turn count as work, not waiting (ADR-008 item 1);
- the CGA's registers are digested.

A native device the interpreted run did not also drive would move the digests.

## Decision

**1. A de-assembled routine touches a device only through `Elite::Hardware`** (`GameLogic/Hardware.h`). A `Guest` hands it out beside its `GameState` (`Guest::Devices()`). Its operations are named for what the game does, not by port:

| Operation | What it does |
|---|---|
| `SetToneDivisor` | the speaker's tone, on the PIT's channel 2 |
| `SetTickDivisor` | the timer's tick, on the PIT's channel 0 |
| `SystemControl`, `SetSystemControl` | port 61h: the speaker's gate and data |
| `EndOfInterrupt` | the end of an interrupt, to the interrupt controller |
| `EnableInterrupts`, `DisableInterrupts` | STI and CLI |

More operations come as the routines that need them convert: the keyboard, the game port, the CGA's mode and status, the BIOS clock, DOS's files and the mouse. Each is added with its first user.

**2. For now, each operation is the original's own sequence of port accesses,** in its order and at its width, through the path `Guest::In8` and `Guest::Out8` take. So the emulated devices, paced time's count of port writes and the comparison see exactly what they saw. Every port access the original makes stays, as every memory write does (ADR-012 item 3).

**3. When D7 deletes the interpreter, `Hardware`'s implementation becomes the native devices, and the routines do not change.** `Hardware` is a concrete class. Today's implementation and the one that replaces it are never needed together, so it has no interface beside it (AGENTS.md R2).

**4. The interrupt flag belongs to the devices.** STI and CLI are `Hardware` operations, not a register a routine sets. In the end state they gate the scheduler's ticks.

**5. Interrupt vectors stay memory.** `InstallTimerInterrupt` writes int 8's vector into the table at 0000:0020 through the `GameState`, because the interpreter's interrupt dispatch reads the table while it lasts. The writes go when the scheduler replaces the handlers.

**6. Converted with this decision: the timer's tick and the speaker.**
- **Sound:** `StartMusic`, `StopAllSound`, `SilenceSpeakerTimer`, `EmitNoiseSample` and `ToggleSpeaker`. `StopAllSound` keeps `StopSoundEffects`' STI, as `EnableInterrupts`.
- **Timer:**
  - `TimerTick`, with the music, the sweeps, the siren, the two-tone and the effects it drives;
  - `InstallTimerInterrupt`.
  - A sweep's sample is now a call of `EmitNoiseSample` or `ToggleSpeaker`'s value routine, not a call by address.
- **Still register code:** `RestoreTimerInterrupt` and `TimerInterrupt` read the BIOS clock and chain to the BIOS's handler, so they keep their register code. They now drive the ports through `Hardware`.
- **Entries:** the five Sound entries now poison what their contracts leave (ADR-012 item 5). No caller reads it.

**7. Measured 2026-10-10,** g++ and clang++ Release, with poisoning on:
- `GameLogicTests` passes 176 of 176. This was run without the three combat replays that are being re-recorded under D18 (ADR-008 item 8): the corpus test stops at its first failing replay, and leaving them out lets it check all 16 others in all three runs.
- The coverage check is clean.
- clang++ builds without warnings.
- No digest moved.

**8. Input's and StartUp's devices, 2026-10-10.**

| Operation | What it is |
|---|---|
| `KeyboardData` | IN AL,60h |
| `AcknowledgeKeyboard` | the XT's pulse on bit 7 of port 61h: IN, then OUT with the bit set and OUT with it clear |
| `GamePortButtons` | IN AL,201h |
| `ResetMouse`, `ReadMousePresses`, `ReadMouseMotion` | int 33h functions 0, 5 and 0Bh |
| `SetVideoMode` | int 10h AH=0 |
| `PrintDosString` | int 21h AH=9 |
| `PeekBiosKey`, `ReadBiosKey` | int 16h AH=1 and AH=0 |

- **A service is a typed result.** The operation sets the registers the service reads, calls it, returns what it gives as a small aggregate, and puts the processor's registers back, so it has no register effect of its own. An entry whose contract compares what the service left rebuilds it from the result. With no mouse driver, int 33h is the ROM's IRET, and the result is the registers as they were sent; the header says so, and the tests rely on it.
- **Converted.** `ReadScanCode`, `ReadFireButton`, `ReadMouseSteering` and `ResetMouseIfSelected` are value routines. `KeyboardInterrupt`, `ReadSteering` and `ExitToDos` keep their register code but drive the devices through `Hardware`. `ExitToDos` still sets the registers its `JumpBack` compares, as ADR-010 item 8 requires.
- **STI and CLI.** Every STI and CLI in native code now goes through `Hardware`, as item 4 says: Sound's entries, `ResetKeyboard`, `RestoreTimerInterrupt`, the start-up and exit routines, and the joystick's.
- **Left, and why.** The joystick routines count instruction cycles and wait. `GetKey` and `WaitForKeyPress` wait. `PollScreenDumpKey` calls `SaveScreenshot`, which is not converted. `Start` and `CopyProtection` use the stack and call routines that wait.
- **Measured.** No digest moved, the coverage check is clean, and no contract changed. The leftovers the converted routines no longer produce are poisoned, and no caller reads them.

## What this forecloses

- **Port numbers or interrupt vectors in a de-assembled routine.** A routine that needs a device takes a `Hardware`.
- **A device operation that is not the original's own port sequence, while the interpreter remains.**
- **An interface with a second implementation of the devices before one is needed.**
