# How the reference works

**Status:** Phase 1, in progress (plan §5). This is the written half of the map, and [`Symbols.tsv`](Symbols.tsv) is the named half. `python Tools/MapReference.py --listing <file>` regenerates the annotated disassembly from both, and every claim below names the addresses it rests on, so it can be checked against that listing. Under AGENTS.md's preamble the reference is the design. This document says what the reference does; it says what the port must do only where a ruling exists or a behaviour has to be kept.

## Coverage

This map describes `ELITES.EXE`, the reference since ADR-007; its addresses were translated from the map of `ELITEL.EXE`, the wireframe build of the same release, and the solid renderer was mapped afresh. `python Tools/MapReference.py --gaps` reaches 13,831 instructions and 35,580 of the code segment's 36,672 bytes, in 387 routines. It walks from the program entry, the four interrupt handlers, the three jump tables and the two blueprint handlers. It also finds 444 data-segment addresses that the code references by absolute address or as a table base.

What stays unreached:

- **The protection's failure path** at 0x053F. The crack cut it off (ADR-001).
- **The triangle filler's debug leftovers at 0x22E0–0x233F**: a fixed test triangle (0x22FB), a register printer (0x22E0, which calls 0x32E7) and a routine that marks the screen corner (0x2324). The filler around them is live, self-modifying code and all ([The solid renderer](#the-solid-renderer)). Its two tables of instruction templates, at 0x1CF3 and 0x1FF8, are read and never executed.
- **A stray `ret` at 0x3B8C** in the face loop, after an unconditional jump.
- **Number printers at 0x32E7–0x3406**, seven entry points. Only 0x32E7 is called, and only from the filler's register printer, which nothing enters. Also **`DrawGaugeBar` at 0x3440**, which nothing calls.
- **Thirteen short routines** of 4 to 85 bytes, each following a `ret` or a jump, with no branch to them:
  - the uncalled rate limiter `RateLimit` at 0x775D (ADR-001);
  - six sound starters at 0x7B16–0x7BA8 for effects nothing else triggers: the hum, the continuous and slow noise, the two-tone and the siren;
  - `StartContinuousNoise` (0x7B5C), which carries an MZ relocation inside its immediate.

Phase 2's execution traces are the final word on all of these. Until a trace contradicts it, the port carries none of them.

## Memory

- **Drawing buffer.** DS:0x0000–0x1FFF is the 256×128-pixel, 2-bit drawing buffer, 64 bytes a line, which `ClearDrawBuffer` (0x060D) clears every frame.
  - **Space view.** `PresentSpaceView` (0x0599) copies rows 1–126 (`spaceViewBuffer`, from DS:0x0040) into CGA memory at B800:01E8, which places the view at x=32, y=12. Rows 0 and 127 are drawn into and never shown.
  - **Charts.** `CopyChartBufferToScreen` (0x05CC) presents the same buffer for the chart screens, starting from an 8-line band that AL selects.
- **Cockpit.** A 16,000-byte CGA image fills DS:0xB160–0xEFDF, just above the stack. `ShowCockpitScreen` (0x7BC0) copies it to the screen through the relocated segment constant 0x140A. It holds the dashboard panel and its labels.
- **Objects in space.** They live in 36 records of 64 bytes from DS:0x6930 (`shipSlots`), set up by `SetUpLocalSpace` (0x29D0):

  | Slots | Holds |
  |---|---|
  | 0 | the sun (type 30) |
  | 1 | the planet (type 31) |
  | 2 | the station (type 0 or 1) |
  | 3–19 | ships, scanned up to `objectSlotCount` (20) |
  | 20–35 | debris (`debrisSlotCount`, 16) |

  `shipSlotCount` (DS:0x433D) is 36 in flight and 3 on the title. A new ship is filled from a 10-byte record in the spawn table: byte 0 is the type, and bytes 1–9 go to +18, +1D, +31, +32, +2C, +2D, +2B, +3F and +1C, in that order (`InitObjectFromTemplate`, 0x4E1B).

  | Offset | Size | Meaning |
  |---|---|---|
  | +00 | 1 | Bit 0 active; bits 1–5 the type (`& 0x3E` indexes `blueprintTable`, and the names are at DS:0x4B95); bit 6 in range this frame; bit 7 visible this frame |
  | +01/+04 | 1+2 | x, a 24-bit value: high byte at +01, low word at +04, relative to the player |
  | +02/+06 | 1+2 | y, the same way |
  | +03/+08 | 1+2 | z, the same way |
  | +0A/+0C/+0E | 2 each | Pitch, yaw and roll, 2048 units a turn. The sun and planet reuse +0A–+0B for scale and disc colour, and the station reuses bit 0 of +0C as an "inside the box last frame" latch |
  | +10/+12/+14 | 2 each | View-space x, y and z. z is the sort key, and bit 7 of byte +15 means behind the viewer |
  | +16, +17 | 1 each | AI timer (used by class 5) and AI state |
  | +18 | 1 | Speed |
  | +19–+1B | 1 each | Velocity per frame |
  | +1C, +1D | 1 each | Range (high byte) and turn rate, from the spawn table |
  | +1E | 1 | Flags (below) |
  | +1F | 1 | Ships still to launch: the station's 10–17, or a Thargoid's Thargons |
  | +20–+25 | | Station only: the compass target, in view space at reduced scale |
  | +26–+28 | 1 each | Scanner blip x, y, z for ships; compass dot and in-front flag for the station; spin rates for debris |
  | +29 | 2 | A missile's target slot |
  | +2B | 1 | Energy. Laser damage subtracts from it, and the object explodes on underflow unless +1E bit 2 is set |
  | +2C | 1 | Most cargo canisters dropped; also the masking device's flicker count |
  | +2D | 1 | Explosion fragment count |
  | +2E, +2F | 1 each | Debris lifetime and age |
  | +30 | 1 | Aggression: the chance in 256 each frame that it fires, raised (saturating) when the player's laser hits it |
  | +31 | 1 | Bounty, in tenths of a credit; 0xFF marks a protected ship, whose killing is an offence |
  | +32 | 1 | Missiles carried |
  | +33 | 1 | Behaviour class. It indexes `behaviorHandlers` (DS:0x7990) and the class names at DS:0x4B3D: 0 None, 1 Station, 2 Attack (missiles), 3 Debris (pods, barrels, rocks), 4 Trader (and police), 5 Wolf, 6 Hunter, 7 Debris (fragments) |
  | +34 | 1 | Off-scanner counter; the object is despawned when it wraps |
  | +35/+36/+38 | 1, 2, 2 | Weaving timer and offsets |
  | +3A | 2 | A Viper's police flag, or a Thargon's parent slot |
  | +3C | 1 | High byte of camera-frame z; bit 7 chooses the aft shield when it hits the player |
  | +3D | 1 | Scale shift, the first sort key |
  | +3E | 1 | Distance measure, (x²+y²+z²)>>22 |
  | +3F | 1 | Level-of-detail limit: the object is drawn as a dot when +3F < +3E |

  The flag bits at +1E:

  | Bit | Meaning |
  |---|---|
  | 0 | hostile: set when the player's laser hits it; required before a ship fires missiles; a station with it set refuses docking |
  | 1 | scanner blip drawn |
  | 2 | indestructible: the station, the sun and the planet |
  | 3 | a fragment or a dropped barrel |
  | 4 | scoopable minerals (a splinter) |
  | 5 | carries a masking device: drawn for 25 frames, hidden for 20 |
  | 6 | the flicker's hidden phase; on a Barrel, the barrel holds the masking device |
  | 7 | drawn this frame |

- **Ship blueprints.** `blueprintTable` (DS:0x68C9) points to 30 blueprints that lie back to back over DS:0x4D70–0x68C8, and all 30 parse to exactly that range. Each blueprint holds, in order:
  - **a handler and box size**: a handler address, then the half-width p. For handler 0x377A only, the half-height q and the half-length h follow, and the handler writes the 8 box corners as vertices 34–41. Type 0, the Dodo station, has handler 0x38BF, which builds a dodecahedron and its docking slot itself.
  - **a vertex program**: a count, then ops whose bits 0–5 are a vertex index and bits 6–7 load, store, average or add;
  - **the projected-vertex count**;
  - **fixed edges**: a count, which is 0 in every blueprint;
  - **face edges**: a count, then two bytes each: vertex A×4, then vertex B×4 with the colour in the low two bits;
  - **faces**: a count, then for each face three winding bytes (vertex×4), a byte count k and k bytes of items, which [the solid renderer](#the-solid-renderer) describes.

  The 30 blueprints hold 291 faces, which carry 500 filled triangles and 148 edge items. The faces use 119 of the 678 face edges. The other 559 are never drawn, among them every edge of the Dodo, the Coriolis and the Python.

  `RunBlueprintHandler` (0x3CDD) jumps to the handler with 0x3CF2 (`RenderBlueprintBody`) pushed as its return address. Blueprints use at most vertex 41 of the 42-vertex buffer at DS:0x7230, and `sqrtTable` lies directly after it.

## The solid renderer

**How this was established.** It was read from the listing and then checked by execution on the PC host (ADR-006).

- **The model.** A model of the triangle filler, written from the listing, reproduced the drawing buffer byte for byte in 40,499 calls:
  - 11,499 calls from game runs: the title, a launch and the rear view, all 28 title ships, and a ship forced close;
  - 29,000 direct calls to 0x1BFB with random vertices.
- **Coverage.** Together they took all eight paths through the filler.
- **What a call touches.** The last 6,000 direct calls ran with interrupts off. They changed nothing outside four places: the drawing buffer, the scratch at DS:0x2F7C–0x2F9E, the stack below the call and the 18 patch sites.
- **Where the tools are.** The model and its harness were working tools and are not in the tree. Phase 3's differential tests take their place.

**Where it sits.**

- `TransformAndDrawObjects` (0x3D25) draws the objects far to near.
- For a ship, `RunBlueprintHandler` (0x3CDD) builds the box vertices. `RenderBlueprintBody` (0x3CF2) then does four things:
  1. runs the vertex program (`RunVertexProgram`, 0x3A40);
  2. projects the first projected-vertex-count vertices in place (`ProjectVertices`, 0x2340);
  3. stores the edge lists;
  4. calls `DrawVisibleFaces` (0x3AB3) with the face list.

**The face loop.** `DrawVisibleFaces` takes each face in turn.

1. **It culls.** The three winding bytes pick three projected points, and `TriangleWindingSign` (0x3A9B) decides: SF = 1 is facing. A hidden face is skipped whole (`stc; adc si,cx` at 0x3B8D).
2. **It draws the face's k bytes of items in order.**
   - **An edge** is a byte below 0x80 and counts 1. It is twice the edge's index in the face-edge list. That record gives the two vertices and the colour, and `DrawClippedLine` (0x1603) draws it.
   - **A filled triangle** is a byte 0x80|n and counts 4. The word at `faceFillPatterns` + n becomes `triangleFillPattern`. Three bytes of vertex×2 follow, and `FillTriangle` (0x1BFB) fills the triangle.
   - **Either is skipped** when one of its points has x = 0x7FFF.

**What the loop does not do.**

- **Faces are not sorted by depth.** They are drawn in the order the blueprint lists them, and back-face culling is the only hidden-surface removal.
- **Shared edges are drawn more than once.** An edge listed by two visible faces is drawn twice: 104 of the 1,339 lines drawn during the title run were repeats.
- **Fixed edges are never drawn, and half the ships carry no lines at all.** 15 of the 30 blueprints list no edge items, so their adjacent faces are told apart only by colour and dither phase. Where a face does list edges, they are drawn before or after its fills, in the order the face lists them.

**Worked example: the Cobra Mk III** (type 26, the title ship), at DS:0x64CB–0x664F.

- **Header:** 28 projected vertices, 40 face edges and 13 faces.
- **Face 0** is `08 10 00 22` followed by 34 item bytes:
  - five fills, `92 02 04 06` to `92 02 0C 00`, a solid colour-3 fan from vertex 1;
  - fourteen black detail edges, `34` to `4E` (edges 26–39).
- **Edge 10 is drawn up to four times a frame.** It is a colour-3 line between vertices 9 and 10, listed by faces 1, 4, 8 and 9. Face 4 draws it before its fill and the others after theirs.
- **Faces 11 and 12** are two triangles each, in solid colour 2.

**Colours and dither.** `faceFillPatterns` (DS:0x74B9) holds 16 words: the four solid colours, and the six two-colour checkerboards in both phases. The low byte fills the buffer's even rows and the high byte its odd rows; the pixels below are colour indices, four to a byte.

| Item | Word | Even rows | Odd rows | | Item | Word | Even rows | Odd rows |
|---|---|---|---|---|---|---|---|---|
| 80 | 0000 | 0000 | 0000 | | 90 | BBEE | 3232 | 2323 |
| 82 | 5555 | 1111 | 1111 | | 92 | FFFF | 3333 | 3333 |
| 84 | 2288 | 2020 | 0202 | | 94 | CC33 | 0303 | 3030 |
| 86 | 1144 | 1010 | 0101 | | 96 | EEBB | 2323 | 3232 |
| 88 | AAAA | 2222 | 2222 | | 98 | 8822 | 0202 | 2020 |
| 8A | 33CC | 3030 | 0303 | | 9A | 4411 | 0101 | 1010 |
| 8C | 6699 | 2121 | 1212 | | 9C | 9966 | 1212 | 2121 |
| 8E | DD77 | 1313 | 3131 | | 9E | 77DD | 3131 | 1313 |

- **How the pattern is applied.** `DrawStackedSpansFromRow` (0x1CD5) takes the word and swaps its bytes when the bottom row is odd. It swaps them again after every row, and `FillTriangleSpan` writes the current byte across the span.
- **The dither belongs to the screen, not the ship.** It follows the buffer's absolute rows and byte columns, not the triangle. Two triangles of one colour therefore meet without a seam, and the dither does not move with the ship.
- **Every pattern is used.** All 16 occur in the blueprints, and in the title run every pattern word was a table entry.

**The filler's contract.** `FillTriangle` (0x1BFB) takes three vertices as signed words, each an x and a buffer row:

- A in AX, DX; B in BX, BP; C in CX, DI;
- the pattern in `triangleFillPattern`;
- DF = 0.

It clobbers every general register. It returns with ES = DS, except when the clipped path rejects a triangle outright, which leaves ES as it was. It writes four things and nothing else:

- rows 0–127 of the drawing buffer;
- the scratch at DS:0x2F7C–0x2F9E;
- its own 18 patch sites;
- the stack: one span per row, at most 128.

**Triangles inside the buffer (0x1C12).**

- **The test.** It doubles the rows and ORs the high bytes of the six coordinates. If all six high bytes are zero, so that x is 0–255 and the row 0–127, the triangle stays on this path.
- **Sorting.** It sorts the vertices by row into four cases: flat bottom, flat top, one row, and general.
- **Edges.** It traces the two edges top to bottom in 8.8 fixed point. Each starts at fraction 0, with slope floor(|Δx|·256/Δrows).
- **The stack is the span buffer.** It pushes one span per row, and `DrawStackedSpans` (0x1CD1) pops them and draws from the bottom row up.
- **The general case.** The lower short edge restarts at the middle vertex with fraction 0 and is stepped once before the first row below it.
- **A short edge.** Because slopes are floored, an edge can end one pixel short of its end vertex.

**Triangles that cross the edge of the buffer** go to `FillClippedTriangle` (0x1E6E).

- **Rejection.** It returns at once when all three x are below 0 or all are 256 or above, or when all three rows are below 0 or all are 128 or above.
- **Setup.** It halves the rows back with `sar`, so a row of magnitude 0x4000 or more loses its top bit. It then sorts into the same four cases.
- **Edges.** It traces them in 16.16 fixed point: the integer part in AX and BX, the fraction in SI and DI, and the slope from two unsigned divides.
- **The row walk.** It starts at the top vertex's row, however far off the buffer that is. A row is drawn only when it is 0–127 and its span meets 0–255, and the span is clamped to 0–255. The walk stops at the first hidden row after a drawn one.

**`FillTriangleSpan` (0x1B7A)** fills DL..DH inclusive on row SI/64 with the byte BL.

- **The two end bytes** are masked through `triangleEdgeMasks`. The masks are read through BP, which addresses the stack segment, so they sit at SS:0000 = DS:0xAD60. The stack would have to grow 0x3E8 bytes to reach them.
- **The bytes between** are written with a `stosb` to an even address, then `rep stosw`.

**The filler modifies itself.** Each edge step is an add, or a subtract when the edge runs leftward. The filler writes the right instruction over the step once per triangle, instead of testing the direction every row.

| Path | Patched steps | `CS:` writes | Templates |
|---|---|---|---|
| `FillTriangle` | 6 two-byte steps (`add`/`sub` `ax,si` and `bx,di`) | 14 | 0x1CF3 |
| `FillClippedTriangle` | 12 four-byte steps (`add`/`adc` or `sub`/`sbb` with a memory operand) | 28 | 0x1FF8 |

- **The clipped writes copy only the opcode and ModRM word**, so each patched step keeps its own displacement.
- **No state carries from one call to the next.** Every path writes the add forms first and then the subtract forms it needs.
- **Why it patches.** `div` is unsigned, and the slope's magnitude needs all 16 bits.
- **No prefetch hazard on the 8088.** The nearest forward write lands 18 bytes past the instruction that makes it, beyond the 4-byte queue. The backward writes are reached only through jumps, which flush the queue.
- **Both forms ran.** Every site the game reached ran in both forms.
- **In the port** the patch is a sign per edge. What has to be kept is the arithmetic and the quirks below.

**Two other changes that come with the solid renderer.**

- **`DrawDistantStation` (0x45C6)** draws the far station as a filled disc (`DrawDisc`) rather than a circle's outline. This is read from the code; no run has reached it yet.
- **`nearClipZ` (DS:0x73D7) is 100.** `ClassifyViewPosition` (0x3CA5) does not draw an object whose centre is nearer than that.

**Still to establish**, with Phase 2's replays:

- whether a near-plane vertex (below) occurs in play;
- why the delay after vertical retrace is 2,000 iterations;
- whether a solid frame matches DOSBox-X's.

## The three Phase 1 questions

### 1. What the interrupt handlers share with the main program

Measured with `python Tools/MapReference.py --shared 0201,0215`, which walks the call graph from each handler and compares what the two sides read and write. Seven routines run from the handlers, and 325 from the main program.

**The keyboard handler** (`ReadScanCode`, 0x7443) acknowledges the key on port 0x61 and then updates three things:

- **`keyDown`** (DS:0xA030, 128 bytes): set on a make code and cleared on a break code, indexed by the scan code itself. The `inc ah` at 0x7452 and the `dec al` at 0x7463 cancel out, and `HandleFlightFunctionKeys` confirms it by testing index 0x3B for F1.
- **`keyBuffer`**: every make code is appended to this 16-byte ring buffer at DS:0xA0B0, with its pointer at DS:0xA0C0 and its count at DS:0xA0C4. When it is full, a new key is dropped. The code is OR-ed with 0x80 while either Shift key (scan code 0x2A or 0x36) is held.
- **Four flags:**
  - DS:0xA0C6 on any make code;
  - DS:0xA0C5 on Esc (scan code 1);
  - `forceMisjump` (DS:0x788C) on W (0x11) while Alt (0x38) is held, which forces a witch-space jump at 0x478C;
  - DS:0xA5B6 when D (0x20), the docking-computer key, is released in flight (`inFlight`, DS:0xA7F0).

  The main program reads `keyDown`, the buffer, `forceMisjump` and DS:0xA5B6. Nothing reads DS:0xA0C5 or DS:0xA0C6.

**The timer handler** (`TimerTick`, 0x7150, about 1,000 times a second) does four things:

- **Counters.** It increments three: `millisecondCounter` (DS:0xA138), `timerTicks` (DS:0xA0C7) and `msSinceFrame` (DS:0xA13A).
- **Protection.** While a question is pending it runs the copy-protection answer check.
- **Sound.** It runs the whole sound system, unless `soundEnabled` (DS:0xAC0B) is 0 or `gamePaused` (DS:0xAC0F) is 1:
  - **Music:** `musicPlaying`, `musicPointer` and `notePeriods` program PIT channel 2 with a note's divisor and count its duration in ticks.
  - **Sound effects:** countdown channels in DS:0xA135–0xA14D toggle bit 1 of port 0x61 directly, through `speakerPortImage`. This works the speaker cone by hand at up to 500 Hz. The main program starts an effect by writing a channel's counters (the sweep starters at 0x7AC3 and their shared tails at 0x7ACE and 0x7ADD), and the handler counts them down. The live triggers are DS:0xA135, 0xA136, 0xA13B, 0xA140 and 0xA14D, and DS:0xA14C is a sweep parameter. Five of the ten channels, at DS:0xA13E, 0xA142, 0xA143, 0xA146 and 0xA150 (the hum, the continuous and slow noise, the two-tone and the siren), are started only by the unreached starters at 0x7B16–0x7BA8, so they never sound.
  - **Music format:** a duration unit is 36 ticks. Rests and the gaps between notes are played as PIT divisor 0x32, about 23.9 kHz, which is inaudible.
  - **Noise:** `EmitNoiseSample` (0x7A93) sends the speaker bit 1 of successive bytes of the program's own machine code, CS:0x07D0–0x0BCF (`noiseSource`), in a 1,024-byte cycle. A faithful port carries those bytes as data, because the noise is literally the shape of the code.

**What this means for Phase 4.** The shared state comes in two kinds:

- **Counters and keyboard records.** The main program polls them, so delivering ticks and keys at defined points changes nothing it can observe beyond timing.
- **The sound engine.** This is the exception. Its effects are made by toggling the speaker at the tick rate, so the sound is the timing. The engine has to run on the audio clock at 1 kHz, not once a frame.

### 2. How the game paces itself

**It advances once a frame, with a minimum frame time and no scaling by elapsed time.**

- **The wait.** `PresentSpaceView` (0x0599) spins until `msSinceFrame` reaches `minimumFrameMs` (DS:0xACFB) and then clears it. It then waits for vertical retrace plus a fixed 2,000-iteration delay (`WaitRetraceThenDelay`, 0x45FF) before copying the frame out. A frame therefore takes the longer of its own work and `minimumFrameMs`.
- **The value.** `minimumFrameMs` is 50 in the file, which is 20 frames a second. The pause screen sets it with F1–F10, from `frameTimeChoices` (DS:0xACF1): 200, 125, 100, 83, 67, 56, 50, 40, 30 and 1 ms. The file's 50 is F7's value, but the pause screen's own text names F8 (40 ms) as the default. The port follows the value, which is what the reference does.
- **No elapsed-time use.** No other code reads `msSinceFrame`. `millisecondCounter` is read only for a blink at 0x2914, and `timerTicks` only by `WaitForTimerTick` and the uncalled `RateLimit`.
- **Waits count ticks or frames**, never CPU loops:
  - **Timer ticks.** The station tunnel (`PlayStationTunnel`, 0x2D5B) and the docked screens' menu cursors count ticks; a cursor step is `mov cx,100` / `WaitForTimerTick` / `loop`, which is 100 ms (0x618E, 0x6402, 0x64CF, 0x6BA2). The timer therefore has to keep ticking while a menu is up.
  - **Frames.** Hyperspace, the escape pod and game over count frames.

**Ruled (D6):** "the same speed" is the original's frame-time setting, default 50 ms, selectable with F1–F10 as the original's is. There is no model of the 1987 slowdowns. On a real 4.77 MHz PC the game slowed down whenever a frame's work overran 50 ms; on a modern machine every frame fits.

### 3. Mid-frame palette writes

**There are none.** The colour-select register (0x3D9) is written in three places, and none of them waits on the raster:

- **0x49D8, in `UpdateFuelLeak`.** Once a frame. While the fuel leak runs ("FUEL LEAK!", fuel −5 a frame for 51 frames, armed on arrival in missions 1 and 3), it sets background colour 4 with the bright palette. Otherwise it writes `maskingBackgroundColor` (DS:0xAC0D), which is 0 normally and 9, light blue, while the masking device's key N is held.
- **0x7C97, in `DrawDockedFrame`.** It sets a docked screen's border to the background colour, the high nibble of the screen's attribute byte, after switching to text mode.
- **0x7D0A, in `SetGraphicsMode`.** It runs after the BIOS sets mode 4.

The waits on vertical retrace at 0x45FF and 0x05CC precede block copies to the screen. They time the copy against the beam to avoid tearing, which in the port is simply presenting a finished frame.

## Video modes

| Mode | Used for | Where |
|---|---|---|
| 4 (320×200, 4 colours) | flight, the title screen and both charts, whose text is drawn with the game's own font at DS:0x4350 | `SetGraphicsMode`, 0x7CFE |
| 1 (40×25 colour text) | the docked screens other than the two charts | `SetTextMode`, 0x7D11, called by `DrawDockedFrame` |
| 0 (40×25 monochrome text) | the protection question | 0x04B2 |
| 2 (80×25 text) | on exit | 0x00AD |

**`SetTextMode` turns blinking off and hides the cursor.** It writes 0x08 to the mode-control register 0x3D8, so attribute bit 7 selects a bright background instead of blinking, and it moves the cursor off the page through CRTC register 14. The presenter has to honour both.

**The docked screens are drawn by the CGA's character generator, and their glyphs are not in `ELITES.EXE`.** `DrawDockedFrame` writes code-page 437 characters into text memory: 0xBA and 0xCD, the double-line box pieces, six corner and tee characters from DS:0xA7D3, and then the screen's text. On the original machine the CGA turned those bytes into pixels with the 8×8 font in its own ROM. **Ruled (D14):** the port draws its own 8×8 code-page 437 font, matching the CGA's for the characters the game uses. It is near-identical, not byte-exact, and carries no third-party bytes.

## Control flow

- **`Start` (0x0000)** checks the DOS version, detects the Amstrad, and reads the command tail for a hidden ` cheat` argument (`CheckCheatArgument`, 0x02A5); with it, flight ignores the player's death (0x7EEC). From `RestartPlay` (0x003B) it installs the handlers and runs the copy protection, then calls `GameLoop`.
- **`GameLoop` never returns normally.** The Esc screen's disk operations reach `LeaveGameLoopForDisk` (0x7E90). It stores `diskOperation` (DS:0x2009: 0 exit, 1 load, 2 save, 3 delete, 4 catalogue), discards two return addresses and lands back in `Start` at 0x0065. There, `PerformDiskRequest` (0x02FF) runs with int 24h hooked, and play restarts at `RestartPlay`. Exit is operation 0, and it too calls `WipeProgram` before returning to DOS.
- **`GameLoop` (0x7D30)** alternates two steps:
  - with `inFlight` = 0, `RunTitleAndDocked` (0x7D81): the title's rotating ship (F9 and F10 cycle the type), skipped once DS:0xA8FE is set and until death or Abort, then the docked screens;
  - with `inFlight` = 1, the flight routine (0x7E9B).

  The ship launches when the docked dispatcher returns on F1.
- **The docked screens** are chosen at `DockedKeyDispatch` (0x0B40). `RunTitleAndDocked` enters it at 0x0B96 (the F9 screen first) or at 0x0BAC (the disk menu), not at its head. F2 to F10 and Esc each call one screen:

  | Key | Routine | Screen |
  |---|---|---|
  | F2 | 0x5A30 `ShowSellCargoScreen` | SELL CARGO |
  | F3 | 0x5AE9 `ShowBuyCargoScreen` | BUY CARGO |
  | F4 | 0x5BF2 `ShowEquipShipScreen` | EQUIP SHIP |
  | F5 | 0x0CAE `ShowGalacticChart` | GALACTIC CHART n (graphics) |
  | F6 | 0x0E52 `ShowShortRangeChart` | SHORT RANGE CHART (graphics) |
  | F7 | 0x5CDE `ShowSystemDataScreen` | DATA ON <system>, with the generated description |
  | F8 | 0x5E2C `ShowMarketPricesScreen` | <system> MARKET PRICES |
  | F9 | 0x5EA9 `ShowCommanderStatusScreen` | COMMANDER <name>; also runs the missions |
  | F10 | 0x6020 `ShowInventoryScreen` | INVENTORY |
  | Esc | 0x660B `ShowDiscControlScreen` | DISC/CONTROL: load, save, delete, catalogue, exit to DOS, and the input device |

  Each screen returns the key that closed it in AH, and the dispatcher acts on it. **This version has three missions of its own**, run from the F9 screen by `ShowMissionBriefing` (0x6DF2) and `ShowMissionDebriefing` (0x6EB7): supernova refugees; a masking device; and a Thargoid-invaded station, which rewards an anti-ECM device and the title "Archangel".

  The same screens can be reached in flight through `HandleFlightFunctionKeys` (0x0BB3), with `inFlight` cleared except for F9's status screen.
- **The pause screen** (from 0x8D8F, entered at 0x8144) does three things:
  - **toggles options:** `keyboardRecenter` (R), `keyboardDamping` (D), `reverseYControl` (Y), `reverseXAndY` (B) and `soundEnabled` (S);
  - **sets `minimumFrameMs`** with F1–F10;
  - **leaves:** Space resumes, and A aborts to the title.

  Recenter and damping apply to the keyboard and the Amstrad joystick; the two reverse options apply to every device. Sound stops during a pause, because `TimerTick` returns while `gamePaused` is set.
- **Other hidden features:**
  - Alt+W forces a misjump into witch space.
  - Alt+PrtSc writes the screen to `eliteNN.hi`/`.lo` (`SaveScreenshot`, 0x01B7, with int 24h hooked).

## Input devices

- **Keyboard**, through `keyDown` and `keyBuffer` (above).
- **IBM joystick.** `ReadJoystickAxes` (0x777E) runs with interrupts off. It fires the game port's one-shots with `out 0x201`, then counts polling iterations until bit 0 (X) drops. It waits, with no time-out, for both bits to drop, refires, and counts until bit 1 (Y) drops. Each count starts at 60,000 and times out at the 16-bit wrap. The result depends on CPU speed, but it is scaled against centre readings taken the same way when the stick is chosen (0x67BA). The port's game-port model therefore has to time the one-shots in instruction cycles.
- **Mouse**, through int 33h (AX = 0, 5 and 0x0B).
- **The Amstrad's joystick and mouse**, as scan codes 0x77–0x7C.
- **Which device** is chosen by `inputDevice` (DS:0x8837) and `joystickIsAmstrad` (DS:0x8836) at the "I..IBM A..Amstrad ?" prompt (0x677C).

## Arithmetic the port must reproduce exactly

**The divide-overflow trap is part of the arithmetic (plan §7.1).** These divides reach it:

| Where | Routine | When |
|---|---|---|
| 0x2369, 0x2392 | `ProjectVertices` | \|x\| or \|y\| ≥ 256·z |
| 0x2502 | `RatioArcTangent` | 0/0, so `ArcTangent2(0,0)` comes out as 45° |
| 0x4041, 0x4061 | `DrawSunOrPlanet` | \|coordinate\| ≥ 256·z, z = 0, or x = −32768 |
| 0x4210, 0x422C | `UpdateCompass` | x or y = −32768 |
| 0x77E5, 0x780B, 0x7835, 0x785B | `ReadJoystickSteering` | a reading ≥ 256× its centre |
| 0x8A74, 0x8A8C, 0x8AA3 | `FindShipInCrosshairs` | near or off-axis objects, and z = 0 |
| 0x8D49, 0x8D54 | `ProjectToScreen` | \|x\| or \|y\| ≥ 256·z |
| 0x87B7–0x87CC | `RunDockingComputer` | possibly, if distance < speed |

**The handler gets the operand size wrong for three-byte divides.** On the 8088 path it reads the byte two before the return address to choose between 0x7F and 0x7FFF. For the three-byte `div word [si+4]` at 0x2369 and 0x2392, that byte is the ModRM byte 0x74, which is even, so the quotient becomes AL = 0x7F with AH and DX unchanged, not 0x7FFF. The port must give exactly those results.

**Other quirks that change what the player sees, and so must be kept:**
- **`TriangleWindingSign` (0x3A9B)** takes its sign from the high-word difference. When the high words are equal it takes it from the low word, which is not the true 32-bit sign, and face culling depends on it.
- **The near-plane marker is missed.** `ProjectVertices` marks a vertex nearer than `nearPlaneZ` (50) with x = 0x8000 (0x23B2) and leaves its y stale. `DrawVisibleFaces` skips a point only when x = 0x7FFF (0x3B19–0x3B71).
  - **The effect.** A near vertex reaches `FillTriangle` and `DrawClippedLine` as x = −32768, and its faces are drawn as wedges out to the left edge of the view.
  - **The evidence.** It took a title ship forced to distance 120 to show it: 129 triangles and 28 lines over 12 seconds. Forced to 144 there were 21, and at 160 none.
  - **What is inferred.** That 0x7FFF was meant as 0x8000 is an inference.
- **Clipped triangles carry two register slips.** Both move edge pixels by one, and the model of the filler matches the reference only with both in.
  - **SI is overwritten.** In `FillClippedTriangle`'s general case, when the bottom vertex is not right of the middle one, the subtract forms for the lower edge are written through SI (0x2225–0x2234). That replaces the long edge's fraction with 0x1E1B.
  - **DI is not reset.** At 0x2249 only the integer part of the lower short edge is reloaded, so DI keeps the upper edge's fraction.
- **The two filler paths disagree by a pixel.** The same triangle filled at 8.8 inside the buffer and at 16.16 when clipped can differ at its edges, so a ship's edge pixels can change as it crosses the border of the view.
- **The vertex program's average** adds in 16 bits before shifting, so it can overflow.
- **Sun radius on the death frame.** The frame `KillPlayer` runs, the sun is drawn with radius 60, because the routine leaves AL = 0x3C.
- **`VectorWithinBox`** compares only the low 16 bits of 24-bit positions, so a rare false collision is possible.
- **`TakeDamage`** only ever reduces the fore shield.
- **Shield bars.** Their caches at DS:0x4234 and 0x4235 are never written, so both bars are redrawn every frame.
- **Gold from rich splinters** gets the same random amount as gems, because the `and ah,3` at 0x4508 is unused.
- **`UpdateCompass`** returns with DI = 0x69B0, and the object loop continues from there. This works only because the station is always in slot 2.
- **Draw order:** objects are drawn far to near, by rescanning every slot after each one is drawn.
- **System data:** tech level and population use this version's own formulas, which differ from the 6502 versions (ADR-003 item 4).

**Other behaviour the port keeps because the reference has it:**
- **"Hyperspaced into WITCH SPACE!"** never shows, because 0x4807 compares `witchspaceCountdown` with 1 and it is always 0 or 100 there.
- **The mask ship's escorts become Asps**, because `PlaceEscortNear` copies the type byte. `IsMaskShipPresent` does not check the active bit.
- **A police Viper made by `SpawnRandomTrader`** writes a word at +30, which zeroes its bounty, so killing it brings neither a bounty nor an offence.
- **`ClampTurnStep` (0x5166) does not wrap at 2048**, so ships turn the long way across 0.
- **Stale state is read.** A new ship's spawn direction (0x4E75) uses the sin/cos pairs the last `ComputeVelocity` left, and `LaunchPlayerMissile` copies 64 bytes from whatever DI the key handler left. `ExplodeObject` loses DI after a failed slot search.
- **The "9th Galaxy"** is reached from galaxy 7 with a chance of 300 in 65,536.
- **Supernova mission:** declining it still pays 1,400 Cr at the next dock.
- **Ships' ECM** is only counted down inside the station's AI, so it never works in a system without a station.
- **The planet description generator** never capitalises y or z.
- **The rock split** at 0x56B1 can never run.

**A probable latent bug, to confirm with a Phase 2 trace:** `ShowShortRangeChart` calls `PresentChartFrame` with whatever AL `DrawLine` left, which `CopyChartBufferToScreen` then uses as a band offset.

## Naming status

Every range is named: 386 of 387 routines, with Routine8C51, which the replay corpus does not run, left by its address and described in its notes, and 412 of the 444 data addresses the code references. `python Tools/MapReference.py` prints the current counts. A name is a claim, and a claim with no evidence in its notes is a defect in the table.
