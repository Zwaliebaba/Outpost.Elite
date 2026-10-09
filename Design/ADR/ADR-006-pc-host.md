# ADR-006 — The PC host

**Status:** accepted 2026-10-09, with the change that implements it: `Machine::Pc` and the devices and services it puts together, and the `ReferenceRunner` (ADR-004).

## Context

Phase 2 runs the unmodified reference on a PC of our own. On it the reference can be traced, checked against DOSBox-X (ADR-003), and in Phase 3 compared routine by routine with the C++ that replaces it.

The interpreter (ADR-005) is one part of that PC. The rest is the machine the game talks to:

- the interrupt controller and timer that drive its 1 kHz tick;
- the keyboard controller and speaker it programs directly;
- the CGA it draws into and whose status it polls;
- the game port;
- the BIOS and DOS it calls.

Each is a choice about how much of the real part to model. The game's own use of the hardware (Reference-Map.md) sets the bar. Anything it never touches is left out rather than guessed at.

## Decision

**1. One machine, one clock.**

- `Machine::Pc` owns the memory, the devices, the services and the CPU, wires the devices to one I/O bus, and attaches the 8259 and the services to the CPU.
- Time is the 8088's cycle count, a third of the 14.31818 MHz crystal (`Timing.h`).
- A step is one CPU step. The clock then advances by its cycles, and the timer and the keyboard catch up with it, raising whatever interrupt falls due. The interrupt is seen at the next instruction boundary.
- The CGA's status and the game port's one-shots are computed from the clock when they are read, not ticked.
- There is no wall clock and no randomness, so a run is a function of the program, its start moment and its inputs.
- Measured 2026-10-09: two runs, and builds by g++ 13 and clang++ 18, write byte-identical boot traces.

**2. The devices, and what they leave out.** Each class's header states its model and its omissions in full. In short:

- **The 8259** starts as the BIOS leaves it: vector base 8, edge-triggered, single controller, mask BCh. Only IRQ0, IRQ1 and IRQ6 are unmasked. Level triggering, cascading, polling and special mask mode are not modelled.
- **The 8253's timing.** Tick *k* is cycle 4*k* from power-on, so tick counts come straight from the clock and nothing drifts.
  - Modes 0, 2 and 3 are modelled, with the gate and every access mode.
  - It starts as the BIOS programs it: channel 0 divides by 65,536, so the first IRQ0 is at cycle 262,144.
  - Modes 1, 4 and 5 count but their outputs are not modelled; neither are BCD counting or read-back.
- **The 8253's channel 2** is gated by port 61h bit 0 and drives the speaker.
- **The keyboard side of the XT's 8255** has three parts:
  - Port 60h is the scan-code latch.
  - Port 61h is an output latch, and a read returns what was written, as on the XT. DOSBox-X returns time-dependent refresh and timer bits there instead, which is why ADR-003 counts that read as an input.
  - A queued code arrives 1 ms (4,772 cycles) after the latch empties. That figure is chosen, not measured: it is the order of the XT keyboard's serial byte and of the game's tick.
- **The speaker** logs every change of port 61h bits 0–1 and of channel 2, stamped with its cycle.
  - It renders PCM with a box filter on an exact sample grid.
  - A channel 2 wave above 20 kHz, or above Nyquist, counts as its mean level. Otherwise the game's rests, divisor 32h at 23.9 kHz, would come out as a loud tone.
- **The game port** times its one-shots at 24.2 µs plus 0.011 µs per ohm, to the cycle. They are not retriggerable, and an axis with no stick never times out.
- **The CGA's status is computed from the clock.**
  - A frame is 79,648 cycles.
  - Status bit 0 is set for the last 91 of each line's 304 cycles and throughout lines 200–261.
  - Bit 3, vertical sync, is set for 16 lines from line R7 × (R9 + 1).
- **The CGA's rendering** produces 640×200 colour indices: text through our own font (D14) and both graphics modes.
- **The port router** decodes all 16 address bits and models no aliases. An unmapped port reads FFh, and every unmapped access is counted and reported by the runner.

**3. The ROM, BIOS and DOS, at the call level** (ADR-005 item 5).

- **The firmware is an IBM PC's.** The model byte is FFh, and FC00:0016 does not hold "Amstrad" (D12).
  - Every vector points at its own IRET, F000:FD00 + *n*, so a trace shows which vector was taken.
  - Int 8 and int 9 are short real handlers. Int 8 counts the tick, calls int 1Ch and sends EOI. Int 9 reads, acknowledges and discards the key.
  - The program's end halts the CPU at F000:E070.
- **The BIOS:** int 10h modes 0–6 and the palette call, int 16h and int 1Ah, as the IBM listing does them.
- **DOS 3.30 provides exactly the int 21h functions the game calls**, and int 20h.
  - Its clock runs on machine time from the start moment. Real DOS's would stand still while the game owns int 8.
  - Files live in a directory. Their dates come from DOS's clock, never from the host's.
  - Attributes are kept per file in memory, defaulting to 0. The game will not list or load a commander file with any attribute set, so a default of "archive" would make every save unloadable in the next session.
  - Int 21h 0Ch faults rather than reading a line. Its only caller is the copy-protection prompt, which D5 skips.
- **A call the services do not implement is refused.** Nothing changes, the first refusal is kept with the address of its INT, and the run stops (`StopReason::Fault`). The services never drift into a general BIOS or DOS.
- **Whether a mouse driver is present** is fixed at power-on.

**4. Loading and starting the reference.**

- `ExeLoader` loads as DOS 3.30's EXEC does: the environment below the PSP, the image at PSP + 10h, and the allocation the header asks for.
- The program starts in the registers MS-DOS gives it, which DOSBox-X copies.
- Before anything else, the host checks the file's SHA-256 against ADR-001. It writes the D5 byte in memory, and only if that byte holds 00.
- To match DOSBox-X for ADR-003, the runner puts the PSP at 0813h, where DOSBox-X puts it. The clock starts at 00:00 on 1 January 1980, where ADR-003 pins DOSBox-X's, and DOS reports 3.30 in both.

**5. The headless runner.** `ReferenceRunner` reads the step script that `Tools/ReferenceScreens.py` reads: `wait`, `key`, `shot` and `digest`.

- **A key** is its make and break codes queued together. The keyboard delivers them as the game's handler acknowledges each one.
- **A shot** is a 640×200 palette PNG on the CGA's dot grid, the grid of DOSBox-X's shots.
- **A digest** is SHA-256 over all of memory and the registers.
- **The boot trace** is written through the instruction observer (ADR-005 item 10), and **the code coverage** from the execution map (item 9).
- **Speed:** 30 s of emulated time, 10.65 million instructions of `ELITES.EXE`, ran in 0.41 s with a g++ -O2 build (measured 2026-10-09).

**6. What checks it.**

- **123 tests in `MachineTests`:** DOS 16, CGA 12, PIC 10, CPU 10, PIT 9, firmware 8, BIOS 7, loader 7, speaker 7, PC 6, keyboard 6, game port 5, mouse 5, SHA-256 5, port router 4, memory 3, font 3.
- **`PcTests.ReferenceBootsToItsFirstKeyRead` pins the boot of `ELITES.EXE` (ADR-007):** 39,255 instructions and 761,619 cycles from the entry to the first `GetKey`, the CGA in mode 4, and the SHA-256 of memory at that point. A change to the CPU, a device or a service that alters the boot by one instruction, one cycle or one byte fails CI, and has to say why.
- **The boot trace matches DOSBox-X's** register for register at all 28,652 comparable records (ADR-003 item 1).
- **The screens match DOSBox-X's shots** taken by the same script. Measured with ImageMagick at 10% fuzz:
  - The palettes are identical.
  - The status and market screens differ in 1,602 and 1,756 of 128,000 pixels, all in glyphs (our font, D14).
  - The title differs only inside a 308×98 box in the 3D view, where the filled ship turns.

**7. Two flag errors in DOSBox-X, not in the host.** The trace comparison found them, and DOSBox-X 2024.03.01's source confirms them:

- **PF over the whole word.** `get_PF` computes PF over a whole 16-bit result (`src/cpu/flags.cpp`, `PARITY16`), where the 8086 takes the low byte only.
- **OF after a one-bit SHR.** It sets OF only when the operand is above 80h (`lf_var1b > 0x80`), where the 8086 copies the operand's top bit.

The interpreter's flags are the 8088's, as the SingleStepTests suite checks on real hardware. Neither error changes a register during the boot, so DOSBox-X remains the independent reference; its flags are reported, and not trusted.

## What this forecloses

- Modelling hardware the game does not use: DMA, the floppy controller, the serial and parallel ports, a second interrupt controller, the AT's refresh bit. Such a port reads FFh and is reported.
- A wall clock inside the machine. Real time maps to cycles only in a presenter, at one seam.
- A BIOS or DOS below the call level, or calls beyond those the game makes (ADR-005).
- Running the reference without the hash check, or with the D5 byte written to the file rather than into memory.
