# How the reference works

**Status:** Phase 1, in progress (plan §5). This is the written half of the map, and [`Symbols.tsv`](Symbols.tsv) is the named half. `python Tools/MapReference.py --listing <file>` regenerates the annotated disassembly from both, and every claim below names the addresses it rests on, so it can be checked against that listing. Under AGENTS.md's preamble the reference is the design. This document says what the reference does; it says what the port must do only where a ruling exists or a behaviour has to be kept.

## Coverage

`python Tools/MapReference.py --gaps` reaches 13,130 instructions and 33,753 of the code segment's 36,704 bytes, in 386 routines. It walks from the program entry, the four interrupt handlers, the three jump tables and the two blueprint handlers. It also finds 425 data-segment addresses that the code references by absolute address or as a table base.

What stays unreached:

- **The protection's failure path** at 0x053F. The crack cut it off (ADR-001).
- **A dead triangle filler at 0x1B7A–0x233F (1,990 bytes).** It holds:
  - `FillTriangle` (0x1BFB) and a clipped variant, `FillClippedTriangle` (0x1E6E), which fill dithered triangles into the drawing buffer;
  - debug leftovers: a fixed test triangle (0x22FB), a register printer (0x22E0, which calls 0x32E7) and a routine that marks the screen corner (0x2324).

  It patches 18 two-byte instructions with 42 `CS:` writes, which is the self-modifying code plan §7.2 was about.

  Nothing enters it. Decoding from every byte offset outside it finds no branch, call, `CS:` reference or pushed immediate that lands inside. No data word holds one of its entry points, nothing can fall into it, and no reached instruction touches its private data. The line drawer the game actually uses, `DrawLine` (0x16D1), does not modify itself.
- **Number printers at 0x32E7–0x3406**, seven entry points. Only 0x32E7 is called, and only from the dead block. Also **`DrawGaugeBar` at 0x3440**, which nothing calls.
- **Thirteen short routines** of 4 to 85 bytes, each following a `ret` or a jump, with no branch to them:
  - the uncalled rate limiter `RateLimit` at 0x777D (ADR-001);
  - six sound starters at 0x7B36–0x7BC8 for effects nothing else triggers: the hum, the continuous and slow noise, the two-tone and the siren;
  - `StartContinuousNoise` (0x7B7C), which carries an MZ relocation inside its immediate.

Phase 2's execution traces are the final word on all of these. Until a trace contradicts it, the port carries none of them.

## Memory

- **Drawing buffer.** DS:0x0000–0x1FFF is the 256×128-pixel, 2-bit drawing buffer, 64 bytes a line, which `ClearDrawBuffer` (0x060D) clears every frame.
  - **Space view.** `PresentSpaceView` (0x0599) copies rows 1–126 (`spaceViewBuffer`, from DS:0x0040) into CGA memory at B800:01E8, which places the view at x=32, y=12. Rows 0 and 127 are drawn into and never shown.
  - **Charts.** `CopyChartBufferToScreen` (0x05CC) presents the same buffer for the chart screens, starting from an 8-line band that AL selects.
- **Cockpit.** A 16,000-byte CGA image fills DS:0xAD90–0xEC0F, just above the stack. `ShowCockpitScreen` (0x7BE0) copies it to the screen through the relocated segment constant 0x13CF. It holds the dashboard panel and its labels.
- **Objects in space.** They live in 36 records of 64 bytes from DS:0x6580 (`shipSlots`), set up by Routine29D0:

  | Slots | Holds |
  |---|---|
  | 0 | the sun (type 30) |
  | 1 | the planet (type 31) |
  | 2 | the station (type 0 or 1) |
  | 3–19 | ships, scanned up to `objectSlotCount` (20) |
  | 20–35 | debris (`debrisSlotCount`, 16) |

  `shipSlotCount` (DS:0x433D) is 36 in flight and 3 on the title. A new ship is filled from a 10-byte record in the spawn table: byte 0 is the type, and bytes 1–9 go to +18, +1D, +31, +32, +2C, +2D, +2B, +3F and +1C, in that order (Routine4E3B).

  | Offset | Size | Meaning |
  |---|---|---|
  | +00 | 1 | Bit 0 active; bits 1–5 the type (`& 0x3E` indexes `blueprintTable`, and the names are at DS:0x4B95); bit 6 in range this frame; bit 7 visible this frame |
  | +01/+04 | 1+2 | x, a 24-bit value: high byte at +01, low word at +04, relative to the player |
  | +02/+06 | 1+2 | y, the same way |
  | +03/+08 | 1+2 | z, the same way |
  | +0A/+0C/+0E | 2 each | Pitch, yaw and roll, 2048 units a turn. The sun and planet reuse +0A–+0B for scale and disc colour, and the station reuses bit 0 of +0C as an "inside the box last frame" latch |
  | +10/+12/+14 | 2 each | View-space x, y and z. z is the sort key, and bit 7 of byte +15 means behind the viewer |
  | +16, +17 | 1 each | AI timer and AI state |
  | +18 | 1 | Speed |
  | +19–+1B | 1 each | Velocity per frame |
  | +1C, +1D | 1 each | AI parameters from the spawn table |
  | +1E | 1 | Flags (below) |
  | +1F | 1 | Ships still to launch: the station's 10–17, or a Thargoid's Thargons |
  | +20–+25 | | Station only: the compass target, in view space at reduced scale |
  | +26–+28 | 1 each | Scanner blip x, y, z for ships; compass dot and in-front flag for the station; spin rates for debris |
  | +29 | 2 | A missile's target slot |
  | +2B | 1 | Energy. Laser damage subtracts from it, and the object explodes on underflow unless +1E bit 2 is set |
  | +2C | 1 | Most cargo canisters dropped; also the masking device's flicker count |
  | +2D | 1 | Explosion fragment count |
  | +2E, +2F | 1 each | Debris lifetime and age |
  | +30 | 1 | Accumulated laser damage, saturating |
  | +31 | 1 | Bounty, in tenths of a credit |
  | +32 | 1 | Missiles carried |
  | +33 | 1 | Behaviour class, which indexes `table75C0` (the per-class update) and the class names |
  | +34 | 1 | Off-scanner counter; the object is despawned when it wraps |
  | +35–+39 | | AI fields, not yet understood |
  | +3A | 2 | A slot pointer written at launch, probably the launcher |
  | +3C | 1 | High byte of camera-frame z |
  | +3D | 1 | Scale shift, the first sort key |
  | +3E | 1 | Distance measure, (x²+y²+z²)>>22 |
  | +3F | 1 | Level-of-detail limit: the object is drawn as a dot when +3F < +3E |

  The flag bits at +1E:

  | Bit | Meaning |
  |---|---|
  | 0 | hostile: set when the player's laser hits it; required before a ship fires missiles; a station with it set refuses docking |
  | 1 | scanner blip drawn |
  | 2 | indestructible: the station, the sun and the planet |
  | 3 | set on debris and canisters; meaning unknown |
  | 4 | a splinter carrying precious metals |
  | 5 | carries a masking device: drawn for 25 frames, hidden for 20 |
  | 6 | the flicker's hidden phase; on a Barrel, the barrel holds the masking device |
  | 7 | drawn this frame |

- **Ship blueprints.** `blueprintTable` (DS:0x651D) points to 30 blueprints that lie back to back over DS:0x4D70–0x651C. Each blueprint holds, in order:
  - **a handler and box size**: a handler address, then the half-width p. For handler 0x377A only, the half-height q and the half-length h follow, and the handler writes the 8 box corners as vertices 34–41. Type 0, the Dodo station, has handler 0x38BF, which builds a dodecahedron and its docking slot itself.
  - **a vertex program**: a count, then ops whose bits 0–5 are a vertex index and bits 6–7 load, store, average or add;
  - **the projected-vertex count**;
  - **fixed edges**, always drawn (only the Plate has any);
  - **face edges**, each two bytes of vertex×4, with a mark bit in the first and the color in the low bits of the second;
  - **faces**: three vertex bytes, then the face's edges. A face that passes the winding test marks its edges, and only marked edges are drawn.

  `RunBlueprintHandler` (0x3CF5) jumps to the handler with 0x3D0A (`RenderBlueprintBody`) pushed as its return address. Blueprints use at most vertex 41 of the 42-vertex buffer at DS:0x6E80, and `sqrtTable` lies directly after it.

## The three Phase 1 questions

### 1. What the interrupt handlers share with the main program

Measured with `python Tools/MapReference.py --shared 0201,0215`, which walks the call graph from each handler and compares what the two sides read and write. Seven routines run from the handlers, and 325 from the main program.

**The keyboard handler** (`ReadScanCode`, 0x7463) acknowledges the key on port 0x61 and then updates three things:

- **`keyDown`** (DS:0x9C60, 128 bytes): set on a make code and cleared on a break code, indexed by the scan code itself. The `inc ah` at 0x7472 and the `dec al` at 0x7483 cancel out, and `HandleFlightFunctionKeys` confirms it by testing index 0x3B for F1.
- **`keyBuffer`**: every make code is appended to this 16-byte ring buffer at DS:0x9CE0, with its pointer at DS:0x9CF0 and its count at DS:0x9CF4. When it is full, a new key is dropped. The code is OR-ed with 0x80 while either Shift key (scan code 0x2A or 0x36) is held.
- **Four flags:**
  - DS:0x9CF6 on any make code;
  - DS:0x9CF5 on Esc (scan code 1);
  - `forceMisjump` (DS:0x74BC) on W (0x11) while Alt (0x38) is held, which forces a witch-space jump at 0x47AC;
  - DS:0xA1E6 when D (0x20), the docking-computer key, is released in flight (`inFlight`, DS:0xA420).

  The main program reads `keyDown`, the buffer, `forceMisjump` and DS:0xA1E6. Nothing reads DS:0x9CF5 or DS:0x9CF6.

**The timer handler** (`TimerTick`, 0x7170, about 1,000 times a second) does four things:

- **Counters.** It increments three: `millisecondCounter` (DS:0x9D68), `timerTicks` (DS:0x9CF7) and `msSinceFrame` (DS:0x9D6A).
- **Protection.** While a question is pending it runs the copy-protection answer check.
- **Sound.** It runs the whole sound system, unless `soundEnabled` (DS:0xA83B) is 0 or `gamePaused` (DS:0xA83F) is 1:
  - **Music:** `musicPlaying`, `musicPointer` and `notePeriods` program PIT channel 2 with a note's divisor and count its duration in ticks.
  - **Sound effects:** countdown channels in DS:0x9D65–0x9D7D toggle bit 1 of port 0x61 directly, through `speakerPortImage`. This works the speaker cone by hand at up to 500 Hz. The main program starts an effect by writing a channel's counters (the sweep starters at 0x7AE3 and their shared tails at 0x7AEE and 0x7AFD), and the handler counts them down. The live triggers are DS:0x9D65, 0x9D66, 0x9D6B, 0x9D70 and 0x9D7D, and DS:0x9D7C is a sweep parameter. Five of the ten channels, at DS:0x9D6E, 0x9D72, 0x9D73, 0x9D76 and 0x9D80 (the hum, the continuous and slow noise, the two-tone and the siren), are started only by the unreached starters at 0x7B36–0x7BC8, so they never sound.
  - **Music format:** a duration unit is 36 ticks. Rests and the gaps between notes are played as PIT divisor 0x32, about 23.9 kHz, which is inaudible.
  - **Noise:** `EmitNoiseSample` (0x7AB3) sends the speaker bit 1 of successive bytes of the program's own machine code, CS:0x07D0–0x0BCF (`noiseSource`), in a 1,024-byte cycle. A faithful port carries those bytes as data, because the noise is literally the shape of the code.

**What this means for Phase 4.** The shared state comes in two kinds:

- **Counters and keyboard records.** The main program polls them, so delivering ticks and keys at defined points changes nothing it can observe beyond timing.
- **The sound engine.** This is the exception. Its effects are made by toggling the speaker at the tick rate, so the sound is the timing. The engine has to run on the audio clock at 1 kHz, not once a frame.

### 2. How the game paces itself

**It advances once a frame, with a minimum frame time and no scaling by elapsed time.**

- **The wait.** `PresentSpaceView` (0x0599) spins until `msSinceFrame` reaches `minimumFrameMs` (DS:0xA92B) and then clears it. It then waits for vertical retrace plus a fixed 700-iteration delay (`WaitRetraceThenDelay`, 0x4618) before copying the frame out. A frame therefore takes the longer of its own work and `minimumFrameMs`.
- **The value.** `minimumFrameMs` is 50 in the file, which is 20 frames a second. The pause screen sets it with F1–F10, from `frameTimeChoices` (DS:0xA921): 200, 125, 100, 83, 67, 56, 50, 40, 30 and 1 ms. The file's 50 is F7's value, but the pause screen's own text names F8 (40 ms) as the default. The port follows the value, which is what the reference does.
- **No elapsed-time use.** No other code reads `msSinceFrame`. `millisecondCounter` is read only for a blink at 0x2914, and `timerTicks` only by `WaitForTimerTick` and the uncalled `RateLimit`.
- **Waits count ticks or frames**, never CPU loops:
  - **Timer ticks.** The station tunnel (`PlayStationTunnel`, 0x2D5B) and the docked screens' menu cursors count ticks; a cursor step is `mov cx,100` / `WaitForTimerTick` / `loop`, which is 100 ms (0x61AE, 0x6422, 0x64EF, 0x6BC2). The timer therefore has to keep ticking while a menu is up.
  - **Frames.** Hyperspace, the escape pod and game over count frames.

**Ruled (D6):** "the same speed" is the original's frame-time setting, default 50 ms, selectable with F1–F10 as the original's is. There is no model of the 1987 slowdowns. On a real 4.77 MHz PC the game slowed down whenever a frame's work overran 50 ms; on a modern machine every frame fits.

### 3. Mid-frame palette writes

**There are none.** The colour-select register (0x3D9) is written in three places, and none of them waits on the raster:

- **0x49F8, in `UpdateDamageFlash`.** Once a frame, while a countdown runs, it sets background colour 4 with the bright palette, then restores DS:0xA83D.
- **0x7CB7, in `DrawDockedFrame`.** It sets a docked screen's border to the background colour, the high nibble of the screen's attribute byte, after switching to text mode.
- **0x7D2A, in `SetGraphicsMode`.** It runs after the BIOS sets mode 4.

The waits on vertical retrace at 0x4618 and 0x05CC precede block copies to the screen. They time the copy against the beam to avoid tearing, which in the port is simply presenting a finished frame.

## Video modes

| Mode | Used for | Where |
|---|---|---|
| 4 (320×200, 4 colours) | flight, the title screen and both charts, whose text is drawn with the game's own font at DS:0x4350 | `SetGraphicsMode`, 0x7D1E |
| 1 (40×25 colour text) | the docked screens other than the two charts | `SetTextMode`, 0x7D31, called by `DrawDockedFrame` |
| 0 (40×25 monochrome text) | the protection question | 0x04B2 |
| 2 (80×25 text) | on exit | 0x00AD |

**`SetTextMode` turns blinking off and hides the cursor.** It writes 0x08 to the mode-control register 0x3D8, so attribute bit 7 selects a bright background instead of blinking, and it moves the cursor off the page through CRTC register 14. The presenter has to honour both.

**The docked screens are drawn by the CGA's character generator, and their glyphs are not in `ELITEL.EXE`.** `DrawDockedFrame` writes code-page 437 characters into text memory: 0xBA and 0xCD, the double-line box pieces, six corner and tee characters from DS:0xA403, and then the screen's text. On the original machine the CGA turned those bytes into pixels with the 8×8 font in its own ROM. **Ruled (D14):** the port draws its own 8×8 code-page 437 font, matching the CGA's for the characters the game uses. It is near-identical, not byte-exact, and carries no third-party bytes.

## Control flow

- **`Start` (0x0000)** checks the DOS version, detects the Amstrad, and reads the command tail for a hidden ` cheat` argument (`CheckCheatArgument`, 0x02A5); with it, flight ignores the player's death (0x7F0C). From `RestartPlay` (0x003B) it installs the handlers and runs the copy protection, then calls `GameLoop`.
- **`GameLoop` never returns normally.** The Esc screen's disk operations reach `LeaveGameLoopForDisk` (0x7EB0). It stores `diskOperation` (DS:0x2009: 0 exit, 1 load, 2 save, 3 delete, 4 catalogue), discards two return addresses and lands back in `Start` at 0x0065. There, `PerformDiskRequest` (0x02FF) runs with int 24h hooked, and play restarts at `RestartPlay`. Exit is operation 0, and it too calls `WipeProgram` before returning to DOS.
- **`GameLoop` (0x7D50)** alternates two steps:
  - with `inFlight` = 0, `RunTitleAndDocked` (0x7DA1): the title's rotating ship (F9 and F10 cycle the type), skipped once DS:0xA52E is set and until death or Abort, then the docked screens;
  - with `inFlight` = 1, the flight routine (0x7EBB).

  The ship launches when the docked dispatcher returns on F1.
- **The docked screens** are chosen at `DockedKeyDispatch` (0x0B40). `RunTitleAndDocked` enters it at 0x0B96 (the F9 screen first) or at 0x0BAC (the disk menu), not at its head. F2 to F10 and Esc each call one screen:

  | Key | Routine | Screen |
  |---|---|---|
  | F2 | 0x5A50 `ShowSellCargoScreen` | SELL CARGO |
  | F3 | 0x5B09 `ShowBuyCargoScreen` | BUY CARGO |
  | F4 | 0x5C12 `ShowEquipShipScreen` | EQUIP SHIP |
  | F5 | 0x0CAE `ShowGalacticChart` | GALACTIC CHART n (graphics) |
  | F6 | 0x0E52 `ShowShortRangeChart` | SHORT RANGE CHART (graphics) |
  | F7 | 0x5CFE `ShowSystemDataScreen` | DATA ON <system>, with the generated description |
  | F8 | 0x5E4C `ShowMarketPricesScreen` | <system> MARKET PRICES |
  | F9 | 0x5EC9 `ShowCommanderStatusScreen` | COMMANDER <name>; also runs the missions |
  | F10 | 0x6040 `ShowInventoryScreen` | INVENTORY |
  | Esc | 0x662B `ShowDiscControlScreen` | DISC/CONTROL: load, save, delete, catalogue, exit to DOS, and the input device |

  Each screen returns the key that closed it in AH, and the dispatcher acts on it. **This version has three missions of its own**, run from the F9 screen by `ShowMissionBriefing` (0x6E12) and `ShowMissionDebriefing` (0x6ED7): supernova refugees; a masking device; and a Thargoid-invaded station, which rewards an anti-ECM device and the title "Archangel".

  The same screens can be reached in flight through `HandleFlightFunctionKeys` (0x0BB3), with `inFlight` cleared except for F9's status screen.
- **The pause screen** (from 0x8DAF, entered at 0x8164) does three things:
  - **toggles options:** `keyboardRecenter` (R), `keyboardDamping` (D), `reverseYControl` (Y), `reverseXAndY` (B) and `soundEnabled` (S);
  - **sets `minimumFrameMs`** with F1–F10;
  - **leaves:** Space resumes, and A aborts to the title.

  Recenter and damping apply to the keyboard and the Amstrad joystick; the two reverse options apply to every device. Sound stops during a pause, because `TimerTick` returns while `gamePaused` is set.
- **Other hidden features:**
  - Alt+W forces a misjump into witch space.
  - Alt+PrtSc writes the screen to `eliteNN.hi`/`.lo` (`SaveScreenshot`, 0x01B7, with int 24h hooked).

## Input devices

- **Keyboard**, through `keyDown` and `keyBuffer` (above).
- **IBM joystick.** `ReadJoystickAxes` (0x779E) runs with interrupts off. It fires the game port's one-shots with `out 0x201`, then counts polling iterations until bit 0 (X) drops. It waits, with no time-out, for both bits to drop, refires, and counts until bit 1 (Y) drops. Each count starts at 60,000 and times out at the 16-bit wrap. The result depends on CPU speed, but it is scaled against centre readings taken the same way when the stick is chosen (0x67DA). The port's game-port model therefore has to time the one-shots in instruction cycles.
- **Mouse**, through int 33h (AX = 0, 5 and 0x0B).
- **The Amstrad's joystick and mouse**, as scan codes 0x77–0x7C.
- **Which device** is chosen by `inputDevice` (DS:0x8467) and `joystickIsAmstrad` (DS:0x8466) at the "I..IBM A..Amstrad ?" prompt (0x679C).

## Arithmetic the port must reproduce exactly

**The divide-overflow trap is part of the arithmetic (plan §7.1).** These divides reach it:

| Where | Routine | When |
|---|---|---|
| 0x2369, 0x2392 | `ProjectVertices` | \|x\| or \|y\| ≥ 256·z |
| 0x2502 | `RatioArcTangent` | 0/0, so `ArcTangent2(0,0)` comes out as 45° |
| 0x405F, 0x407F | `DrawSunOrPlanet` | \|coordinate\| ≥ 256·z, z = 0, or x = −32768 |
| 0x422E, 0x424A | `UpdateCompass` | x or y = −32768 |
| 0x7805, 0x782B, 0x7855, 0x787B | `ReadJoystickSteering` | a reading ≥ 256× its centre |
| 0x8A94, 0x8AAC, 0x8AC3 | `FindShipInCrosshairs` | near or off-axis objects, and z = 0 |
| 0x8D69, 0x8D74 | `ProjectToScreen` | \|x\| or \|y\| ≥ 256·z |
| 0x87D7–0x87EC | `RunDockingComputer` | possibly, if distance < speed |

**The handler gets the operand size wrong for three-byte divides.** On the 8088 path it reads the byte two before the return address to choose between 0x7F and 0x7FFF. For the three-byte `div word [si+4]` at 0x2369 and 0x2392, that byte is the ModRM byte 0x74, which is even, so the quotient becomes AL = 0x7F with AH and DX unchanged, not 0x7FFF. The port must give exactly those results.

**Other quirks that change what the player sees, and so must be kept:**
- **`TriangleWindingSign` (0x3B4B)** takes its sign from the high-word difference. When the high words are equal it takes it from the low word, which is not the true 32-bit sign, and face culling depends on it.
- **The vertex program's average** adds in 16 bits before shifting, so it can overflow.
- **Sun radius on the death frame.** The frame `KillPlayer` runs, the sun is drawn with radius 60, because the routine leaves AL = 0x3C.
- **`VectorWithinBox`** compares only the low 16 bits of 24-bit positions, so a rare false collision is possible.
- **`TakeDamage`** only ever reduces the fore shield.
- **Shield bars.** Their caches at DS:0x4234 and 0x4235 are never written, so both bars are redrawn every frame.
- **Gold from rich splinters** gets the same random amount as gems, because the `and ah,3` at 0x4526 is unused.
- **`UpdateCompass`** returns with DI = 0x6600, and the object loop continues from there. This works only because the station is always in slot 2.
- **Draw order:** objects are drawn far to near, by rescanning every slot after each one is drawn.
- **System data:** tech level and population use this version's own formulas, which differ from the 6502 versions (ADR-003 item 4).

**Other behaviour the port keeps because the reference has it:**
- **Supernova mission:** declining it still pays 1,400 Cr at the next dock.
- **Ships' ECM** is only counted down inside the station's AI, so it never works in a system without a station.
- **The planet description generator** never capitalises y or z.
- **The rock split** at 0x56D1 can never run.

**A probable latent bug, to confirm with a Phase 2 trace:** `ShowShortRangeChart` calls `PresentChartFrame` with whatever AL `DrawLine` left, which `CopyChartBufferToScreen` then uses as a band offset.

## Naming status

Every range except 0x4690–0x55B3 is named, and that one is still in progress. `python Tools/MapReference.py` prints the current counts. A name is a claim, and a claim with no evidence in its notes is a defect in the table.
