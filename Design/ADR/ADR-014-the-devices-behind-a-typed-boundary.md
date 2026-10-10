# ADR-014 — The devices behind a typed boundary

**Status:** accepted 2026-10-10, with the change that implements it: `Elite::Hardware`, with the timer's tick and the speaker's routines de-assembled onto it. It records how D17's third step makes the devices native ([Reverse-Engineering-Plan.md §5, Phase 4](../Reverse-Engineering-Plan.md#phase-4--detach)). Amended the same day with Input's and StartUp's devices (item 8), with level 4's (item 9), and with taking interrupts where they fall due (item 10).

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

**9. The devices level 4 needed, 2026-10-10** (ADR-012 item 14).

| Operation | What it is |
|---|---|
| `CgaStatus` | IN AL,3DAh |
| `SetColorSelect`, `SetModeControl` | OUT 3D9h and OUT 3D8h |
| `SetCursorAddressHigh` | OUT 3D4h,0Eh, then OUT 3D5h |
| `SelectPalette` | int 10h AH=0Bh, BH=1 |
| `FireGamePort`, `GamePortOneShots` | OUT 201h, and IN 201h for the one-shots |
| `CountInstructionCycles` | the 8088's cycles for the game port's one-shots, which a stick read counts in polls (ADR-008 item 1) |
| `ReadBiosClock`, `SetBiosClock` | int 1Ah AH=0 and AH=1 |
| `RunBiosTimerTick` | the BIOS's int 8 handler, run as the game's handler chains to it |
| `ReadDosLine`, `SetDiskTransferArea` | int 21h AX=0C0Ah and AH=1Ah |
| `CreateFile`, `OpenFile`, `ReadFile`, `WriteFile`, `CloseFile`, `DeleteFile`, `FindFirstFile`, `FindNextFile` | int 21h 3Ch, 3Dh, 3Fh, 40h, 3Eh, 41h, 4Eh and 4Fh, each a `DosAnswer`: CF and AX |
| `ReadFileAttributes`, `SetFileAttributes` | int 21h AX=4300h and 4301h |

- **Who wrote them.** Two workers added DOS's file services and the CGA's status separately. They were merged into one set when level 4 was integrated.
- **The vector swap.** `RunBiosTimerTick` reaches the BIOS's handler by pointing int 8 at it for the call and back after it. That is the port's mechanism for a far jump with the interrupt's frame in place, not a write the original makes. The vector is not digested.
- **What it replaced.** Every DOS file call in native code now goes through `Hardware`. SaveLoad's `CallDos` is gone, and the disc menu's register code puts back the AX, CF and CX that DOS leaves.

**10. Interrupts are taken where they fall due, 2026-10-10** (ADR-012 items 15 and 21).

**The problem.** A native routine's hook call ends with the `Pc` taking the interrupts that are due (ADR-010 item 10). So an interrupt that falls due while interrupts are off is taken at the next hook return or loop turn, not at the STI that lets it in. Each de-assembled routine that calls a callee as a value has one hook return fewer. The interrupt then arrives later still.

**The first answer, 2026-10-10, superseded the same day.** A per-site `Hardware::TakeDueInterrupts` was placed where a hook call used to take them. Its users were:
- `SaveScreenshot`, after `RestoreTimerInterrupt` and after `InstallTimerInterrupt`. Without it, the game's own handler took the IRQ 0 that the BIOS's had taken, and the two screenshot twins diverged.
- `GetKey`, at its end.
- `ProcessFlightKeys`, after `ReadSteering`.

By level 5's last slices, value code turned interrupts on in some 25 places, and only these three took what was due. The rule had become a judgment made per site, and it was being applied unevenly.

**The decision.** `Hardware` takes due interrupts where the CPU does:
- **`EnableInterrupts`** (STI) takes the interrupts due as soon as they are on. The original takes them after the instruction that follows STI.
- **`SetTickDivisor`**: a control word to the PIT can raise IRQ 0 at once, and with interrupts on it is taken there.
- **`EndOfInterrupt`**: with interrupts on, an interrupt the controller held back is taken there.

Each runs `Pc::TakeDueInterrupts`, which is public for it. The three per-site calls and `Hardware::TakeDueInterrupts` are gone.

**What it changes.** The native code now takes these interrupts where the original does, one instruction early at most. The difference ADR-010 item 10 records for `RestoreTimerInterrupt`'s clock read is closed with it: the IRQ 0 its PIT reprogramming raises is taken at its STI, before the clock is read, as in the original.

**Measured.** `GameLogicTests` passes 188 of 188 with poisoning on, under g++ and clang++, with the per-site calls removed. The coverage check is clean, and no digest moved.

**In the end state** these three operations run the scheduler's due ticks.

**`Hardware::Spend`** charges a pacing point's cost (ADR-013 item 3), as `Guest::Spend` does for register code. The charts' frame loop calls it.

## What this forecloses

- **Port numbers or interrupt vectors in a de-assembled routine.** A routine that needs a device takes a `Hardware`.
- **A device operation that is not the original's own port sequence, while the interpreter remains.**
- **An interface with a second implementation of the devices before one is needed.**
