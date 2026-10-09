# How the reference works

**Status:** Phase 1, in progress (plan §5). This is the written half of the map, and [`Symbols.tsv`](Symbols.tsv) is the named half. `python Tools/MapReference.py --listing <file>` regenerates the annotated disassembly from both, and every claim below names the addresses it rests on, so it can be checked against that listing. Under AGENTS.md's preamble the reference is the design. This document says what the reference does; it does not say what the port should do, except where a ruling is needed, and then it says so.

## Coverage

`python Tools/MapReference.py --gaps` reaches 13,130 instructions and 33,753 of the code segment's 36,704 bytes, in 386 routines. It walks from the program entry, the four interrupt handlers, the three jump tables and the two blueprint handlers. It also finds 425 data-segment addresses that the code references by absolute address or as a table base.

What stays unreached is listed by `--gaps`:

- **The protection's failure path** at 0x053F. The crack cut it off (ADR-001).
- **A 1,990-byte block at 0x1B7A–0x233F**, plus the routines it alone calls at 0x32E7 and 0x3440. It draws lines into CGA memory (it loads 0xB800) and patches about 30 of its own instructions through `CS:`. No instruction in the reached code calls it, jumps to it, patches a branch towards it, or loads its address. Either it is entered some way this walk cannot see, or it is dead code from another version of the renderer. Phase 2's traces settle which. If it never runs, plan §7.2's self-modifying-code hazard does not apply.
- **Fourteen short routines** of 4 to 85 bytes, each following a `ret` or a jump and with no branch to it. One is the uncalled rate limiter at 0x777D (ADR-001).

## Memory

- **Space view.** DS:0x0040–0x1FBF (`spaceViewBuffer`) holds the space view off screen: 256×126 pixels, 2 bits per pixel, 64 bytes a line. `PresentSpaceView` (0x0599) copies it into CGA memory at B800:01E8, which places it at x=32, y=12. The loop at 0x05BC copies 63 pairs of lines, the even bank then the odd one.
- **Objects in space.** They live in 64-byte records from DS:0x6580 (`shipSlots`), and `shipSlotCount` (DS:0x433D) says how many there are. Routine3D43 walks them every frame.
- **Ship blueprints.** `blueprintTable` (DS:0x651D) points to 30 blueprints at DS:0x4D70–0x64xx. Each blueprint starts with a handler address and a byte. Type 0's handler is 0x38BF, and types 1–29 all use 0x377A, with bytes from 0x0F to 0xC8. `RunBlueprintHandler` (0x3CF5) jumps to the handler and arranges for it to return to 0x3D0A, which reads the rest of the blueprint.
- **Variables** sit from DS:0x2000 upwards. The plan §1 table, ADR-001 and `Symbols.tsv` name the ones identified so far.

## The three Phase 1 questions

### 1. What the interrupt handlers share with the main program

Measured with `python Tools/MapReference.py --shared 0201,0215`, which walks the call graph from each handler and compares what the two sides read and write. Seven routines run from the handlers, and 325 from the main program.

**The keyboard handler** (`ReadScanCode`, 0x7463) acknowledges the key on port 0x61 and then updates three things:

- **`keyDown`** (DS:0x9C60, 128 bytes): set on a make code and cleared on a break code, indexed by scan code − 1.
- **`keyBuffer`**: every make code is appended to this 16-byte ring buffer at DS:0x9CE0, with its pointer at DS:0x9CF0 and its count at DS:0x9CF4. When it is full, a new key is dropped. The code is OR-ed with 0x80 while the key-table entries for scan codes 0x2B or 0x37 are set.
- **Four flags:**
  - DS:0x9CF6 on any make code;
  - DS:0x9CF5 on scan code 2;
  - DS:0x74BC on scan code 0x12 while scan code 0x39 is held;
  - DS:0xA1E6 when scan code 0x21 is released while DS:0xA420 is 1.

The main program reads all of these, and it is the only one that consumes the buffer.

**The timer handler** (`TimerTick`, 0x7170, about 1,000 times a second) does four things:

- **Counters.** It increments three: `millisecondCounter` (DS:0x9D68), `timerTicks` (DS:0x9CF7) and `msSinceFrame` (DS:0x9D6A).
- **Protection.** While a question is pending it runs the copy-protection answer check.
- **Sound.** It runs the whole sound system, unless `soundEnabled` (DS:0xA83B) is 0 or DS:0xA83F is 1. That has two parts:
  - **Music:** `musicPlaying`, `musicPointer` and `notePeriods` program PIT channel 2 with a note's divisor and count its duration in ticks.
  - **Sound effects:** several countdown channels (DS:0x9D65–0x9D7D) toggle bit 1 of port 0x61 directly, through `speakerPortImage`. This works the speaker cone by hand at up to 500 Hz.
  - **Noise:** Routine7AB3 makes noise by sending the speaker bit 1 of successive bytes of the program's own machine code, CS:0x07D0–0x0BCF (`noiseSource`), a 1,024-byte cycle. A faithful port carries those bytes as data, because the noise is literally the shape of the code.
- **Inputs from the main program.** The main program starts an effect by writing its counters, and the handler only reads five trigger bytes (DS:0x9D6E, 0x9D72, 0x9D7C, 0x9D80, 0x9D85), `soundEnabled`, DS:0xA83F, DS:0xA420, `amstradPresent` and `protectionQuestion`.

**What this means for Phase 4.** The shared state comes in two kinds:

- **Counters and keyboard records.** The main program polls them, so delivering ticks and keys at defined points changes nothing it can observe beyond timing.
- **The sound engine.** This is the exception. Its effects are made by toggling the speaker at the tick rate, so the sound is the timing. The engine has to run on the audio clock at 1 kHz, not once a frame.

### 2. How the game paces itself

**It advances once a frame, with a minimum frame time and no scaling by elapsed time.**

- **The wait.** `PresentSpaceView` (0x0599) spins until `msSinceFrame` reaches `minimumFrameMs` (DS:0xA92B) and then clears it. It then waits for vertical retrace plus a fixed 700-iteration delay (`WaitRetraceThenDelay`, 0x4618) before copying the frame out. A frame therefore takes the longer of its own work and `minimumFrameMs`.
- **The value.** `minimumFrameMs` is 50 in the file, which is 20 frames a second. The pause screen sets it with F1–F10, from `frameTimeChoices` (DS:0xA921): 200, 125, 100, 83, 67, 56, 50, 40, 30 and 1 ms.
- **No elapsed-time use.** No other code reads `msSinceFrame`. `millisecondCounter` is read only for a blink at 0x2914, and `timerTicks` only by `WaitForTimerTick` and the uncalled `RateLimit`.

**What this means for D6.** On a real 4.77 MHz PC the game ran at 20 frames a second whenever a frame's work fitted in 50 ms, and slowed down whenever it did not. On a modern machine every frame fits. Running the original's own frame-time setting therefore gives exactly the speed the original intended, and the speed it actually reached on any PC fast enough. Reproducing the 1987 slowdowns would need a cycle-cost model of every frame, and would only be worth it if the owner wants them. **Ruled (D6):** "the same speed" is the original's frame-time setting, default 50 ms, selectable with F1–F10 as the original's is. No model of the 1987 slowdowns.

### 3. Mid-frame palette writes

**There are none.** The colour-select register (0x3D9) is written in three places, and none of them waits on the raster:

- **0x49F8, in `UpdateDamageFlash`.** Once a frame, while a countdown runs, it sets background colour 4 with the bright palette, then restores DS:0xA83D.
- **0x7CB7, in `DrawDockedFrame`.** It sets a docked screen's border colour from a table byte, after switching to text mode.
- **0x7D2A, in `SetGraphicsMode`.** It runs after the BIOS sets mode 4.

The waits on vertical retrace at 0x4618 and 0x05CC precede block copies to the screen. They time the copy against the beam to avoid tearing, which in the port is simply presenting a finished frame.

## Video modes, and a ruling the docked screens need

The game uses four BIOS modes:

| Mode | Used for | Where |
|---|---|---|
| 4 (320×200, 4 colours) | flight and the title screen | `SetGraphicsMode`, 0x7D1E |
| 1 (40×25 colour text) | every docked screen: market, equipment, status, charts' text, inventory | `SetTextMode`, 0x7D31, called by `DrawDockedFrame` |
| 0 (40×25 monochrome text) | the protection question | 0x04B2 |
| 2 (80×25 text) | on exit | 0x00AD |

**The docked screens are text, drawn by the CGA's character generator, and their glyphs are not in `ELITEL.EXE`.** `DrawDockedFrame` writes code-page 437 characters into text memory: 0xBA and 0xCD, the double-line box pieces, then the screen's text. On the original machine the CGA turned those bytes into pixels with the 8×8 font in its own ROM. A faithful picture of the docked screens therefore needs that font, and nothing in the reference supplies it. **Ruled (D14):** the port draws its own 8×8 code-page 437 font, matching the CGA's for the characters the game uses. It is near-identical, not byte-exact, and carries no third-party bytes.

## Control flow

The top-level loop, highest level first:

- **`Start` (0x0000)** checks the DOS version and detects the Amstrad, then sets up the timer and keyboard handlers and the copy protection. It runs `GameLoop` inside a loop that restarts at 0x003B.
- **`GameLoop` (0x7D50)** repeats two steps forever:
  - with DS:0xA420 = 0, it runs Routine7DA1: the title screen's rotating ship (F9 and F10 cycle the ship type), then the docked screens;
  - with DS:0xA420 = 1, it runs Routine7EBB: flight.
- **The docked screens** are chosen at `DockedKeyDispatch` (0x0B40). F1 returns, which launches the ship. F2 to F10 and Esc each call one screen:

| Key | Routine | Key | Routine | Key | Routine |
|---|---|---|---|---|---|
| F2 | 5A50 | F5 | 0CAE | F8 | 5E4C |
| F3 | 5B09 | F6 | 0E52 | F9 | 5EC9 |
| F4 | 5C12 | F7 | 5CFE | F10 | 6040 |
| | | | | Esc | 662B |

  What each screen is gets recorded in `Symbols.tsv` as the routines are named.
- **The pause screen**, from 0x8DAF, toggles the option bytes DS:0xA833, 0xA835, 0xA837, 0xA839 and `soundEnabled` with R, D, Y, B and S, sets `minimumFrameMs` with F1–F10, and resumes with A.

## Naming status

Of 386 routines, 18 have names. Of 425 data addresses the code references, 24 have names. The rest carry their address (`Routine04A3`, `data25E4`) until a name is earned by reading what they do. A name is a claim, and a claim with no evidence in its notes is a defect in the table.
