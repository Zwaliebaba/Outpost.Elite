# Resolution — the game at 640×400

**Status:** Proposed · 2026-09-07 · **eight owner rulings taken the day it was opened** — four on
the shape (§1) and four on what the shape left open (§11). **RS-0 is built, 2026-09-07** (§13): the
surface, the presenter, the upscale and eleven tests, with the suite at
<!--count:tests-->427 green against the oracle and all <!--count:checks-->18 repository checks
passing. Three things the building corrected are marked **CORRECTED** below. Reads after [Modernize.md](Modernize.md), because it starts
where that plan's rules end and obeys them.
**Depends on:** ADR-001 (fidelity — §1 and §4 amended by this design, §2), ADR-002 (the numeric
model — unchanged for the canvas, and §4's "parallel path" is this), ADR-005 (presentation — §1
amended), ADR-006 and ADR-007 (where the new state lives), Modernize.md §4 and §5 (the layers, the
four ports, the ratchet).
**Feeds:** ADR-008 (to be written when the last slice lands, §9), the risk register (§8), the plan's
Phase 6 row, `AGENTS.md` §6 (two new checks).

**What this is.** The executable presents a 640×400 picture instead of the C64's 320×200. The space
view is drawn at twice the line resolution with the same field of view; the dashboard is redrawn at
twice its detail; text keeps its 8×8 glyphs on an 80×50 grid, and every docked screen is laid out
again for that grid. The C64 canvas the game draws today is **kept, drawn on every frame as it is
now, and never presented**: it is the oracle's and the fixtures' view of the game, and every test
that compares it stays exactly as it is.

**What this is not.** Not a change to the game. Same universe, same RNG consumption, same AI, same
bytes on the C64 canvas, same replay digest. A frame's picture is a second *rendering* of decisions
the faithful routines have already taken, never a second taking of them (§4, rule T1). It is also
not an aspect-ratio or fullscreen design: 640×400 is 8:5 as 320×200 is, the integer scale and the
letterbox stay, and ADR-005 §1's "5:4 or 4:3 option is phase 6" stays where it is.

---

## 0. Summary

One new object, one rule, and a build order that keeps the game playable at every step.

- **`Elite::Picture`** is a second drawing surface in `GameLogic`, 640×400, integer, deterministic,
  Linux-testable. It is a VIC-II-shaped canvas at twice the geometry — a one-bit bitmap plane with a
  cell palette, so that the game's exclusive-or drawing works on it unchanged — plus an index plane
  for the dashboard, where two colours a cell is not enough (§3). **CORRECTED at RS-0**: it was
  `Elite::Screen` until the compiler found `Universe::screen` already taken by `ScreenState`, the
  6502 raster bytes. Two near-identical type names in one namespace for unrelated things is the
  wrong answer, and `Picture` is the word §3 had already reached for in prose.
- **Every routine that puts a pixel on the canvas puts the matching pixels on the screen at the same
  call site**, from the same inputs, using one more bit of precision where the inputs carry it, and
  erasing through a twin heap wherever the canvas erases through a heap (§4). The faithful routine
  decides everything; the twin computes coordinates and nothing else.
- **Text** is the same font at the same size on an 80×50 grid; a per-view **layout** maps the
  faithful cursor to the wide one, and each docked screen gets its own (§6).
- **The presenter** uploads the picture instead of the canvas; the window opens at 1280×800 (§7).
- **The picture starts as a 2× upscale of the canvas and is taken over region by region**, so the
  tree plays at every slice and the last slice removes the upscale (§10).

Four rulings shaped it (§1) and four more closed what it left open (§11). The largest cost is not
the arithmetic but the docked screens, and §6 says why and how much.

---

## 1. The rulings, and what "640×400" buys where

Opened as "change the resolution from 320×200 to 640×400, focus on the visibility of the dashboard
and the 3D scene, fonts can stay, the dashboard needs to be double". Three of those four clauses
were ambiguous in ways that changed the design by an order of magnitude, and one premise needed
saying out loud before anything else: **a bigger texture buys nothing by itself.** The presenter
already integer-scales the 320×200 canvas (4× on a 1080p display); a 640×400 canvas holding the
same picture pixel-doubled is that picture at half the scale factor. The resolution only means
something where a pixel is *decided* at 640×400, and where that decision has a more precise input
to be made from. Those places are the whole of this document.

| # | Question | Ruling (2026-09-07) | What it decides |
|---|---|---|---|
| 1 | "Double in size" — the dashboard | **Redrawn at 2× detail.** Bottom 112 of 400 rows, the same share of the screen as today; artwork re-authored at 640×112, bars at 32 steps, blips, sticks and compass at full resolution | A twin of every dashboard routine and a new picture (§5). Pixel-doubling was the cheap reading and it would have looked exactly as today |
| 2 | "Visibility of the 3D scene" | **2× line resolution, same field of view.** Ships, planet, sun, stardust, lasers, explosions and the tunnel rasterised at 512×288 | A twin projection at scale 512 and a twin clipper and line drawer (§4.1). NOT a wider view: the four-cell margins stay margins, so nothing becomes visible that was not |
| 3 | "Fonts can stay" | **8×8 glyphs everywhere, on an 80×50 grid, and every docked screen re-flowed for it** | The layout mechanism (§6) and a layout per screen (§6.3). This is the ruling that sets the cost |
| 4 | ADR-001 §4 says an option with the faithful build green | **Replace 320×200 outright.** The C64 canvas stays internally, the executable presents only the screen | No runtime toggle; ADR-001, 002 and 005 amended (§2); the canvas becomes the verification view |

Where the precision comes from, element by element — because this is the table that says whether
the ruling on the space view can be honoured at all:

| Element | Drawn today by | Precision of its input today | What 2× gets, and from where |
|---|---|---|---|
| Ship lines | `LL9`: `PROJ` centre at scale 256, `LL28` vertex offsets (8-bit), `LL145` clips 16-bit to 8-bit, `LOIN` | One pixel; the divides truncate to it | A twin `PROJ` and `LL28` at scale 512 from the SAME rotated, scaled vertex the faithful stage produced; one extra bit, honest, no interpolation (§4.1) |
| Planet circle | `PROJ` centre, `DVID3B` radius, `CIRCLE2` steps, `BLINE` | One pixel for the centre, the radius and each arc point | Twin centre and radius at scale 512; arc points as a 16-bit multiply of the twin radius by the same `SNE` entry |
| Sun | `SUN`: a half-width per row from `LL5`'s square root | One pixel per row, 8-bit root | Twin: an integer root of the 2× radius per hi-res row — 288 rows, each its own |
| Stardust | `PIXEL2` from `SX`/`SY` | The high byte; the low byte `SXL`/`SYL` is thrown away | The twin reads the top bit of the low byte. The precision was always there |
| Explosion particles | `PIXEL` from 8-bit particle positions | One pixel | Nothing — a dot at the same place. Drawn at its screen size (2×2), so it does not shrink |
| Laser beams | `LASLI`: four lines, corner to an 8-bit convergence point | One pixel at the ends | Thin lines between the same ends. The point does not move |
| Tunnel rings | `HFS2` circles from 8-bit centre and radius | One pixel | Thin rings from the same numbers |
| Crosshairs, burst, Trumbles | Hardware sprites, 8-bit positions | One pixel | Pixel-doubled definitions; re-authored art is a later option (§5.4) |
| Dashboard bars | `DIL`: `value >> shifts`, sixteen steps of four fat pixels | Sixteen steps | Thirty-two steps of two hi-res pixels, from the same byte with one fewer shift |
| Scanner blips | `SCAN`: `x_hi`, `z_hi >> 2`, `y_hi >> 1` | A fat pixel; the fractions `x_lo`, `z_lo`, `y_lo` unread | The twin reads the fraction's top bits — four times finer across, twice down |
| Compass | `SPS2`: `2 * A / 20` on a seven-bit magnitude | A fat pixel over a radius of twenty | Twice the radius, the same seven bits: exactly twice the resolution |
| Text | `CHPR`: 8×8 glyphs at `XC`, `YC` | A cell | Nothing per glyph — the glyph IS the size. What changes is where it goes (§6) |

Two things the table says that the brief did not: the space view's gain is real but it is **one
bit**, because every projecting divide in the game truncates and the twin can only recover the bit
the truncation dropped; and the dashboard's gain is larger than the space view's, because its
inputs carry whole bytes the original never read.

---

## 2. The premise checked against the corpus

Ruling 4 is the one that touches decisions already taken, so each is read against it.

**ADR-001 §1 says "same lines on a 320×200 canvas."** That stays true, of the canvas: the faithful
routines draw into `Universe::canvas` on every frame exactly as they do today, and the oracle
compares it byte for byte. What §1 does not say is that the canvas is what a person sees, and that
is the clause this design adds. **ADR-001 §4 says phase-6 features are options, off by default, so
"the fidelity suites keep meaning what they say."** The suites keep meaning what they say here by a
different construction: nothing in them presents anything, every one of them reads the canvas, and
the canvas is unchanged. So the amendment is not "the option may be on" but "the verified view and
the presented view are two objects, and the tests own the first." Risk R7's mitigation ("every
slice's accept is a fidelity check") survives intact, because every slice below is accepted on the
replay digest and the canvas comparisons being unmoved (§10).

**ADR-002 §4 says the logical coordinate space is 256×144 and "higher internal resolution stays a
phase-6 item that would fork the drawing code rather than change it."** This is that fork, and it
forks exactly where ADR-002 said it would have to. The consequence at the foot of that ADR —
"a parallel path with the fidelity path still selectable" — is met with one word changed:
selectable by the *tests*, which construct a `Universe` and read its canvas, rather than by a
player. **ADR-002 §7** (why one colour index per pixel could not work) is the reason the screen's
space-view plane is bits and not indices (§3.2).

**ADR-005 §1** names the 320×200 `R8_UINT` texture, the integer scale and the letterbox. The
texture becomes 640×400 and the rest stands; the amendment is three numbers and one sentence (§9).

**ADR-006 §4.1's layers and ADR-007's ownership.** `Screen` is a `Kernel`-layer type beside
`Canvas`, owned by `Universe` beside it (§3.4), and reached by the same routines through the same
`Universe&`. It is **not folded into the state hash**, and §3.4 argues why that is right rather
than convenient.

**Modernize.md §5's rules** all hold. "The oracle decides": it decides the canvas, and the screen
is tested against the canvas (§8). "A byte's width and wraparound never change": no faithful byte
changes; the twins are NEW integer code with no 8-bit semantics to preserve (§4, rule T4). "A
mutant is re-anchored, never dropped": the twins get their own (§8.5). The ratchet: `Screen` is a
concrete class, so `effects-seams` does not move; the twins take `Screen&` beside `Canvas&`, which
`workspace-params` does not count; `outpost-elite-names` will rise by the handful of `Elite::Screen`
names the presenter uses and is lowered back in the same commit, journalled under R18.

**Modernize.md §4.10's fixtures.** A fixture records what the oracle answered about the canvas. The
screen is never in a fixture — after M6-b there is nothing to record it against, and before M6-b
there is nothing either. Its evidence is a different kind (§8): consistency with the canvas,
properties of the twin arithmetic, and golden hashes of its own.

**What this track needs from M6, and it is all already built.** Every prerequisite is in the
**M6-0 gate**, which closed on 2026-09-07, and four of its eight rows are load-bearing here:

- **M6-0-a** (the 6510 port register) is why `FlightLoopTests` can build `"a ship exploding"` as a
  whole frame on both machines. That scene is one of the three §8.1 resolves both surfaces over.
- **M6-0-d** (the two fixture faults) is why `"a sun close enough to draw"` exists at all, and why
  the flight-loop heaps sit inside the arena. §4.2's sun twin has no shadow test without it.
- **M6-0-b** (the replay reaching death and the escape pod) is two of the three digests §8.4 runs
  with the twins present and absent.
- **M6-0-g** (the mutant floor) is the mechanism §8.5 adds the four twin files to.

**Nothing from M6-a onward is a prerequisite**, and the reason is one sentence: this track never
asks the original anything. The shadow tests compare the picture against the canvas, the property
sweeps compare twin arithmetic against the PORT's `PROJ`, `LL28` and `SCAN`, and the goldens hash
the port's own output. So M6-b retiring the live oracle neither helps nor blocks it, and R19 — a
fixture pins only what was asked — does not reach it.

**Where the sequencing sits, and the choice is now made.** This design does not touch a faithful
routine's *behaviour*, but it adds a call site to most of the drawing routines, and M6-c renames
identifiers and M6-d rewrites comments in those same files. Doing both at once is two patterns in
one slice (Modernize.md rule 8). The build order (§10) therefore lands RS-0 to RS-4 either side of
that pair and not during it — and **RS-0 landing on 2026-09-07 settled it as BEFORE**. What that
costs is stated rather than discovered: M6-c gains the twins' names to rename, and M6-d gains
almost nothing, because a twin carries `/// 2x of:` instead of `// 6502:` and no assembly-shaped
comments — RS-0 held the `origin-markers` ratchet at its ceiling for exactly that reason.

---

## 3. The picture

### 3.1 Geometry

Every number is the canvas's doubled, and each is named beside the constant it doubles so that a
reader with `Canvas.h` open sees the pair.

| | `Canvas` | `Screen` | Where it comes from |
|---|---|---|---|
| Image | 320×200 | 640×400 | `WIDTH`, `HEIGHT` |
| Cells | 40×25 of 8×8 | 80×50 of 8×8 | `CELL_COLUMNS`, `CELL_ROWS`; a cell is a glyph and glyphs do not scale (ruling 3) |
| Space view | x 0..255, y 0..143 | x 0..511, y 0..287 | one x-unit is one pixel in both; the field of view is the same (ruling 2) |
| Space view centre | (128, 72) | (256, 144) | `SPACE_VIEW_CENTRE_X/Y` |
| Left margin | 4 cells (32 px) | 8 cells (64 px) | `SPACE_VIEW_MARGIN`; a margin, not view |
| Dashboard | cell rows 18..24, 320×56 | cell rows 36..49, 640×112 | `DASHBOARD_CELL_ROW` |
| Message row | cell row 16 | cell row 32 | `MESSAGE_ROW_SPACE_VIEW`, through the space view's layout (§6.2) |
| Sprite origin | (24, 50), 24×21 | (48, 100), 48×42 | `SPRITE_ORIGIN_X/Y`; definitions pixel-doubled |

### 3.2 Representation — why a bitmap plane and not an index buffer

The obvious modern shape is a 640×400 array of colour indices. It is wrong here for the reason
ADR-002 §7 measured on the canvas, and the reason survives doubling: **the game draws by
exclusive-or and erases by drawing again**, and every routine in the space view depends on it —
`LL9`, `WPLS2`, the stardust, the tunnel, the laser beam, the message that vanishes by being
printed a second time. An index buffer with an exclusive-or of indices would work for a single
colour and produce nonsense where two lines cross; a bit plane produces exactly what the C64
produces where two lines cross, which is the alternation a player of the original knows.

So the screen is three planes and one background:

| Plane | Covers | Holds | Written by |
|---|---|---|---|
| `m_bitmap` | all 400 rows, standard mode | one bit per pixel, cell-major exactly as the canvas's `RowOffset` lays it out at twice the width — `(y & 0xF8) * 80 + margin + cell * 8 + (y & 7)` | the twins, by exclusive-or; `CHPR`'s twin, by exclusive-or |
| `m_cells` | 80×50 | a `CellPalette` per cell: the colour of a set bit and of a clear one | the twin of every `celllook` write, `TT66`'s twin, the loader's |
| `m_dashboard` | rows 288..399 | one colour index per pixel, 640×112 | the dashboard picture, `DIL`'s twin by STORE, `SCAN`'s and `DOT`'s twins by exclusive-or of the index |
| ~~`m_background`~~ | — | — | **CORRECTED at RS-0: removed.** Every writer it could have had is a writer the canvas already has. `moonflower`, `welcome`, `DFLAG` and the sprite registers are game bytes the faithful code maintains, so `Resolve` reads all of them from the canvas and the surface holds no raster state at all — one fewer thing for a twin to keep in step |

The dashboard is an index plane because a scanner cell can hold a red blip, a yellow one and the
ellipse's own colour at once, which two colours a cell cannot express — the original solves this
with multicolour mode, whose four colours a cell at half the horizontal resolution is the whole
reason the dashboard is chunky. An index plane is the 2× answer to the same constraint. Its
exclusive-or is on the index, and two blips overlapping produce a third colour exactly as two
`PIXEL` masks overlapping do on the canvas (ADR-002 §7): the same artefact, at twice the resolution.

**The energy bomb** (`moonflower`, §6.155) reinterprets the canvas's bitmap bits as multicolour
pairs. The screen has no multicolour mode and needs none: its `Resolve` reads `Canvas::
SpaceViewMulticolour()` and, while it is set, decodes its own space-view bits two at a time through
the same four-colour rule `ResolveCell` uses, against `m_background`. Same scramble, twice the
resolution, no new state.

### 3.3 `Resolve`, the hash, and the sprites

`Screen::Resolve(std::span<std::uint8_t> _out, const Canvas& _canvas, const VideoState* _video)`
writes 640×400 indices: the bitmap plane through the cell palette for rows 0..287 (and for all fifty
rows while no dashboard is shown, exactly as `Canvas::Resolve` switches on `DashboardShown()`),
the dashboard plane for rows 288..399, then the eight sprites at twice their coordinates from
pixel-doubled definitions, sprite 7 first, with the raster split's two register sets selected by
`y >= 288` — a straight transliteration of `BlitSprite` with every constant doubled. It takes the
canvas because three of its inputs are the canvas's: the raster-mode bytes, the dashboard-shown
flag and the sprite pointers, none of which the screen should hold twice.

`Screen::Hash()` is FNV-1a over the resolved indices, as `Canvas::Hash()` is, for goldens (§8.3).

### 3.4 Where it lives, and why it is not in the state hash

`Universe::screen`, declared beside `canvas`. It has to be reachable from every routine that draws,
and every routine that draws takes `Universe&` or a reference the universe hands out; a second
parameter on forty signatures is the shape M2 spent a phase removing.

It is **not folded by `FoldIntoStateHash`**, which is the one place this design departs from
ADR-007's "every byte of game state" and says so. The replay digest exists to detect a change in
what the game *does*; the screen is a function of what the game did, computed by code this design
owns and will change — a thinner sun, a re-authored crosshair — and folding it would turn every
improvement to the rendering into a re-recording of five replay tables, which is R10's failure mode
at the scale of the whole game. What the hash needs to catch is the screen *leaking* into the game:
a twin that consumed a random number, moved a heap pointer, or wrote a canvas byte. That is caught
by construction — the twins take `Screen&` and const references to their inputs — and by the test
in §8.4, which drives the replay with the screen present and absent and requires the same digest.
`UniverseImage` names the field and marks it excluded, so `EveryCellTheImageNamesMovesTheHash` keeps
its meaning.

`Universe` still copies. The picture is **107,682 bytes measured at RS-0** — a 32,000-byte bitmap,
4,000 cell bytes and a 71,680-byte dashboard plane — which takes a `Universe` from 15 KB to 123 KB,
eight times. Nothing hot copies one; the oracle's image round trips put two on the stack beside a
64 KB interpreter, which is 310 KB of a Windows thread's megabyte. Noted, not mitigated, and the
dashboard plane is 71,680 of it and would halve at four bits a pixel if a later slice needs it back.

---

## 4. Twin drawing — the rule and its sites

**The rule.** Every call that puts pixels on the canvas is followed, at the same call site, by the
call that puts the corresponding pixels on the screen, computed from the same inputs by a *twin*
routine. Four constraints make it a rule rather than a habit:

- **T1 — The faithful routine decides; the twin computes.** Whether a ship is drawn, which faces
  are visible, whether a line is on the heap, whether a blip is on the scanner, what the RNG rolled:
  all decided once, by the ported code, and the twin is handed the result. A twin never reads the
  RNG, never writes anything but the screen and its own twin heap, and never returns a value the
  faithful code branches on. That is what keeps the replay digest still (§8.4).
- **T2 — Precision comes from upstream, never from interpolation.** A twin recovers the bit a
  faithful divide truncated by running the same divide at twice the scale on the same operands; it
  never smooths, averages or subdivides. The 2× coordinate halved is the faithful coordinate, and a
  sweep asserts it (§8.2). Where the input has no more precision — an 8-bit particle, a sprite
  register — the twin draws at the doubled coordinate and stops.
- **T3 — Every erase has a twin erase.** Where the canvas erases through a heap (`LineHeap`, the
  ball heap, the sun heap), the screen erases through a twin heap of 16-bit coordinates written at
  the same moment; where the canvas erases by recomputing (the stardust's old position, the compass
  dot, a message printed again), the twin recomputes the same way from the same bytes. The
  measurement that erase-by-redraw is exact (ADR-002 §7) is then a property of the screen too,
  because the twin draws the same pixels twice from the same stored numbers.
- **T4 — The twins are ordinary integer C++.** They keep no 8-bit semantics because there is
  nothing to be faithful to; `int` and `std::uint16_t` are the natural widths, wraparound is a bug,
  and the one rule of ADR-002 that binds them is *no float* (determinism across compilers, and
  `check_gamelogic.py` enforces it on `Screen.cpp` as on everything else). A `// 6502:` marker on a
  twin would be a lie: twins carry a `/// 2x of: <faithful routine>` line instead, which the check
  in §8.6 reads.

**The sites.** Read off the tree: every caller of `PlotPixel`, `PlotRelativePixel`, `PlotDash`,
`PlotBlock`, `DrawLine`, `DrawHorizontalLine`, `Canvas::Write` and `Canvas::ExclusiveOr` outside
`Lines.cpp` itself, grouped by the surface they draw on.

| File | Draws | Twin routine | Twin heap | Notes |
|---|---|---|---|---|
| `ShipDraw.cpp` | `LOIN` per heap line; `SHPPT` dots | `DrawShipLines2x`, `PlotDot2x` | `LineHeap2x` | §4.1 |
| `PlanetDraw.cpp` | `BLINE` segments, `SUN` rows, the erase of both, the tunnel rings | `DrawBallLine2x`, `DrawSunRow2x`, `EraseSun2x`, `EraseBall2x` | `ball2x`, `sun2x` in `PlanetSunState` | §4.2 |
| `Stardust.cpp` | `PIXEL2` per speck, old and new | `PlotSpeck2x` | none — recomputed from `SX`/`SXL` | §4.3 |
| `Explosion.cpp` | `PIXEL` per particle | `PlotDot2x` | none — the cloud is redrawn from its seed | §4.3 |
| `Lasers.cpp` | four `LOIN`s | `DrawLine2x` | none — `LASLI2` redraws from `LaserBurst` | §4.3 |
| `Scanner.cpp` | `CPIX2`/`CPIX4` blip, the stick, the compass dot | `PlotBlip2x`, `DrawStick2x`, `PlotCompass2x` | none — `SCAN` and `DOT` recompute | §5.2 |
| `Dashboard.cpp` | `DIL`, `DIL2`, `MSBAR`, the bulbs | `DrawBar2x`, `DrawIndicator2x`, `SetMissile2x`, `ToggleBulb2x` | none — STORE, redrawn every frame | §5.1 |
| `Charts.cpp` | `TT15` crosshairs, `PIXEL` per system, the rules, `SUN` discs and the fuel circle | `DrawCrosshairs2x`, `PlotDot2x`, the planet twins | the planet twins' | §6.3, the charts |
| `ViewChange.cpp` | `BOX` and the rules, the dashboard copy, the colour bands, `TT66simp`'s wipe | `DrawBorder2x`, the dashboard picture copy, `ClearTextArea2x` | none | §5.3, §6.1 |
| `TextPrint.cpp` | `CHPR`'s glyph and its `celllook` colour | `PrintGlyph2x` | none — a glyph erases by exclusive-or | §6.1 |
| `LoaderScreen.cpp` | the boot screen's border and cells | `SetUpLoaderScreen2x` | none | one call |

### 4.1 Ships — where the extra bit comes from

`LL9` is seven named stages since M4-b (`TestPresence`, `MeasureRange`, `ScaleShip`, `SelectFaces`,
`ProjectVertices`, `OpenHeapRun`, `PushEdges`), and the fork is one stage wide. `ScaleShip` and
`SelectFaces` are decisions and are not touched (T1). `ProjectVertices` is where each visible
vertex's rotated, scaled coordinates go through `LL28` (`R = 256 * A / Q`) and land in `XX3` as a
16-bit offset from the ship's centre, which `PROJ` put in `K3`/`K4` at scale 256. The twin is:

- `Project2x`: `PROJ` with the numerator shifted left once before `DVID3B` and the overflow test at
  2048 rather than 1024 — the same routine at scale 512. Its `Projection2x` is `(x, y)` as
  `std::int32_t` and is stored beside `Universe::projection`.
- `Divide512`: `512 * A / Q` as a 16-bit integer quotient. Not `LL28` and not `LL61`; a plain
  division, because the faithful routine's saturation at 255 is a decision `ProjectVertices` has
  already taken for this vertex (T1) and the twin only needs the number.
- `ClipLine2x`: Cohen–Sutherland on 32-bit ints against 0..511 × 0..287. Not a port of `LL145`: the
  faithful clipper's job is to decide what goes on the heap, the twin's is to cut a line it has been
  told to draw. A line the faithful clipper rejected is never handed to the twin; a line it accepted
  is cut to the 2× rectangle and, in the corner case where the 2× line ends half a pixel inside a
  boundary the faithful one ended on, drawn to there. That is the one place the two views can
  differ by a pixel, and the shadow test allows it (§8.1).
- `PushEdges` already writes each accepted line to the heap; its twin writes the 2× line to
  `LineHeap2x`, a parallel region of eight bytes per line at twice the same `HeapOffset` — so
  `KILLSHP`'s shuffle and `NWSHP`'s carve move both, because they are one offset.
- `EraseShip` (`EE51`) walks the heap drawing each line again; its twin walks `LineHeap2x` (T3).
- `SHPPT`'s dot: the distant ship is `PIXEL` at the projected centre; the twin plots a 2×2 mark at
  the 2× centre, so a far ship is the same size and twice as accurately placed.

`Bresenham2x` is the line drawer: integer, one hi-res pixel wide, exclusive-or into the bitmap
plane. It is deliberately not `LOIN`'s shape — `LOIN`'s two loops exist to plot one bit at a time
through a mask table so that a line alternates a cell's two colours, and a hi-res standard-mode
line does the same thing by being a bit plane, with no table.

### 4.2 Planet and sun

The planet's centre and radius come from `PROJ` and `DVID3B` and get the same twins; `CIRCLE2`'s
step walk and `BLINE`'s segment decisions stay (T1), and for each segment the twin computes the
endpoint as `radius2x * SNE[step] / 256` in 32-bit and pushes it to `ball2x`, sixteen bits an axis,
which `EraseBall2x` walks. The planet's detail — the crater ellipse and the meridians — is
`DrawEllipse` and `DrawHalfEllipse` over the same `BLINE`, so it comes with the circle.

The sun is a stack of rows. `SUN` computes one half-width per canvas row from `LL5`'s square root
and stores it in the 200-byte `LSO`; `WPLS` erases from the same bytes. The twin computes one
half-width per *hi-res* row — 288 of them, each `isqrt(radius2x² − dy²)` in 32-bit — and stores
them in `sun2x`, 288 sixteen-bit entries. The sun's *edge* wobble (`DORND` per row, T1) is a decision
the faithful routine takes per canvas row; the twin applies the same wobble to both hi-res rows of
that canvas row, so the flicker is the original's and not a smoother invention. `Yx2M1`, the
variable bottom row the short-range chart moves (§6.45), is read doubled.

### 4.3 Stardust, particles, beams, rings

Stardust is the case T2 was written for. Each speck's position is sixteen bits an axis and the
faithful `PIXEL2` plots the high byte; the twin plots `(hi << 1) | (lo >> 7)` — the bit that was
already there. The movers stage the OLD position before they update it (`Stardust.h`), and the twin
is called at both plots from the bytes staged for each, so erase matches draw (T3).

Explosion particles are eight-bit positions rolled from the cloud's seed inside the drawing loop;
the twin draws a 2×2 mark at the doubled position. The particle is one `PIXEL` mark on the canvas,
so it is the same size on the screen and gains nothing but thinness in its distance-graded shapes —
and T1 forbids anything else, because the loop consumes the RNG.

The laser beam is four `LOIN`s from the view's corners to an eight-bit convergence point; the twin
draws four `Bresenham2x` lines to the doubled point, and `LASLI2`'s erase draws them again from the
same `LaserBurst`. The launch and hyperspace tunnels are `HFS2` circles from eight-bit centres and
radii, presented one per ring through `Presenter::Present`; the twin draws each ring thin at the
doubled numbers and the `TT66` that ends the effect clears both surfaces.

---

## 5. The dashboard at twice its detail

### 5.1 Dials

`DIL` lights `value >> shifts` of sixteen fat pixels across four cells, storing the bytes rather
than exclusive-oring them. `DrawBar2x` lights `value >> (shifts - 1)` of thirty-two steps, each two
hi-res pixels wide and fourteen tall, into the dashboard plane at twice `SC`'s position, in the
colour `DialColours` chose (the danger flash is `PZW`'s decision, T1). The same entry-point shape —
the shift count is the argument, because `DILX`, `DILX+2`, `DIL-1` and `DIL` are four scales of one
routine — so the four callers change nothing but which twin they name.

`DIL2`'s roll and pitch indicators light one block of sixteen; the twin lights one of thirty-two,
two pixels wide. `MSBAR` writes a palette byte to a missile cell; the twin fills the missile's
16×16 block in the dashboard plane from the same `CellPalette`'s high nibble. The bulbs toggle a
palette in and out of two cells; the twins toggle the same two blocks between the bulb's colour and
the picture's. All four are STORE-semantics and are redrawn every frame by `DIALS`, so none needs a
heap.

### 5.2 The scanner and the compass

`SCAN` puts a blip at `(123 + x_hi, ...)` across, `z_hi >> 2` down the ellipse and `y_hi >> 1` up
the stick, discarding `x_lo`, `z_lo` and `y_lo`. `PlotBlip2x` takes the same block and reads
`(x_hi << 2) | (x_lo >> 6)` across — a fat pixel is four hi-res pixels, so two bits of the fraction
are exactly what fills them — and `(z_hi << 1 | z_lo >> 7) >> 2` and `(y_hi << 1 | y_lo >> 7) >> 1`
for the depth and the height. The blip is a 4×4 hi-res block in the ship type's scanner colour,
the stick is a two-pixel-wide vertical, both exclusive-ored into the dashboard plane so that
`WPSHPS`'s second call erases them (T3). `SCAN`'s decision whether a ship is on the scanner at all
is its own (T1).

`COMPAS` scales a unit vector of length 96 to a dot at `2 * A / 20` from the centre; `PlotCompass2x`
scales the same seven-bit magnitude to a radius of forty, which is twice the resolution with no
rounding the faithful routine did not also do. `DOT`'s block-or-dash by colour is a decision; the
twin draws a 4×4 block for ahead and a 4×2 dash for behind.

### 5.3 The picture

`DASHBOARD_IMAGE` is 2,241 bytes of two-bit pixels, 160×56, coloured per cell by
`DASHBOARD_SCREEN_COLOURS`; `wantdials` copies it into the bitmap. The 2× picture is a 640×112
sixteen-colour image, `DASHBOARD_IMAGE_2X`, 71,680 bytes in its own generated file, copied into the
dashboard plane where `ShowDashboard`'s twin runs.

**It needs a person with a paint program, and the design says so rather than pretending a tool can
draw it — the owner, by ruling (§11.1), on the bootstrap below, on no slice's schedule.** What a tool can do is start it: `tools/bitmaps.py` gains a fourth sheet and one command,
`bootstrap-2x`, which resolves the faithful dashboard through `Canvas::Resolve` (so the cell colours
are already applied), doubles every pixel, and writes it as an indexed BMP in the sixteen VIC-II
colours. That file imports as the first `DASHBOARD_IMAGE_2X` and the tree plays. The re-authoring —
a round ellipse instead of a stepped one, thin frame lines, legible labels — is then an edit of a
BMP and an import, the same round trip the tool already proves for the three existing sheets. Two
constraints for whoever draws it, checked by the import: only the sixteen palette colours, and the
positions the twins draw into (the bar troughs, the indicator tracks, the missile blocks, the
scanner ellipse's interior, the compass disc) are read from a small table of rectangles in
`Dashboard2x.h` that the picture must leave as its background colour — the check refuses a picture
that paints inside them.

### 5.4 Sprites

The crosshairs, the burst and the Trumbles composite at twice their coordinates from pixel-doubled
definitions (§3.3). Re-authored 48×42 definitions are a later, optional slice: a fourth generated
table, a fifth sheet in `bitmaps.py`, and one line in `Resolve`. Nothing in this design depends on
it, and the crosshairs pixel-doubled are the crosshairs the owner signed off in 2e.

---

## 6. Text on an 80×50 grid, and the re-flow

### 6.1 The glyph

`CHPR`'s twin, `PrintGlyph2x`, exclusive-ors the same eight `FONT_DATA` bytes into the screen's
bitmap plane at cell `(column2x, row2x)` and writes the same `CellPalette` into `m_cells` — the
`celllook` three-cell offset is the canvas's addressing and does not travel; the twin addresses the
cell it drew. Erase is exclusive-or, as on the canvas, so `MESS`'s "print it again to remove it"
and `me1` work unchanged. `TT66simp`'s wipe of rows 1..23 has a twin that wipes the wide rows the
layout maps them to; `BOX`'s border is drawn thin at 2× by `DrawBorder2x` round the same region.

### 6.2 The layout — how a faithful cursor becomes a wide one

**CORRECTED at RS-1: there is no second cursor.** What follows is the mechanism as designed, and
the paragraph after the table says what was built instead and why.

The faithful printer keeps `XC`/`YC` in `TextState` and every screen routine stores to them. The
screen keeps a second cursor, `TextState::wide`, and a **layout** decides where it goes whenever the
faithful one is *placed* (stored to); when the faithful cursor *advances* (a glyph, a space), the wide
one advances by one cell; when it *returns* (character 12 and 13, `RR4`'s newline), the wide one
returns to the layout's left edge for the next row. A layout is a small table per view:

```cpp
struct TextLayout
{
  std::uint8_t columnOffset; // added to a placed XC
  std::uint8_t rowOffset;    // added to a placed YC
  std::uint8_t rowStride;    // 1 or 2: whether faithful rows are spread over blank ones
  std::uint8_t wrapWidth;    // where the WIDE sink re-wraps justified text (§6.3, the data screen)
  std::span<const Anchor> anchors; // (fieldColumn, fieldRow) -> (wideColumn, wideRow) exceptions
};
```

**What RS-1 built instead, and the reason is that the design's version cannot be built.** A stored
wide cursor has to be updated whenever the faithful one is *placed*, and the faithful one is placed
by forty-odd plain field assignments (`text.column = 7;`) spread through the library. A plain field
assignment cannot be hooked in C++ without changing every site — which is a refactor of the faithful
code, for a cursor that turns out to be unnecessary. At the moment a glyph is drawn the faithful
cursor already holds the cell the printer is about to use, so **the wide cell is a pure function of
the faithful cell and the layout**, computed where it is needed and stored nowhere. `TextLayout::Map`
is that function and `TextState` is untouched. What the stored cursor was really for is RS-5's
re-wrapping, where the wide column stops being a function of the faithful one; that slice can store
what it needs when it needs it, and until then there is nothing to keep in step.

**And the offsets are measured in CANVAS CELLS, not in `XC`.** §6.2's original numbers mixed the two:
the space view's `columnOffset` is given as 24, which is right in `XC` — counted from the view's own
left edge — and 20 in canvas cells, which is what the clears count because that is what the game's
own loops count. Both layouts are +20 in canvas cells, and `24 + XC` is `(4 + XC) + 20`. A layout
measured in `XC` would have put every glyph sixteen cells right of the cell meant to clear it;
`TextPrint2x.h` carries the arithmetic and four `static_assert`s.

The default layout — what a view gets until §6.3 gives it its own — centres the faithful 40×25 grid
on the wide one: `columnOffset = 20`, `rowOffset = 12`, `rowStride = 1`, no anchors. That is the
"1× centred" arrangement ruling 3 rejected as the end state, and it is the right *start* state: it is
correct for every screen on the day the text layer lands, and each screen's own layout replaces it.
Anchors are how a re-flow is expressed without touching the screen routine: the market screen stores
`XC = 4` before the price column, and its layout's anchor `(4, *) → (36, *)` moves the column
wherever the wide design wants it. **The screen routines change nowhere for a re-flow**; only the
tables do, which is what keeps the faithful character stream — the thing seventeen fixtures compare
— untouched.

The space view's layout is not a re-flow: `columnOffset = 24`, `rowOffset = 0`, `rowStride = 2`, so
that a message at faithful row 16 lands on wide row 32 and a message centred over the 32-cell view
by `MESS`'s own arithmetic lands centred over the 64-cell one (the algebra: a message of length *n*
starts at `(32 - n) / 2` on the canvas and `8 + (64 - n) / 2 = 24 + (32 - n) / 2` on the screen).
`PrintCountdown`'s column 7 row 17 becomes column 31 row 34, just above the dashboard as before.

### 6.3 The re-flow, screen by screen

Ruling 3's cost. Each screen below gets a layout table and, where the table cannot express it, a
twin of one drawing routine. By ruling (§11.2) every RS-5 sub-slice OPENS with an ASCII sketch of
its screen for the owner to accept or redraw, and only then writes the table. The anchor table for a screen is sized off its placed-cursor stores
(`text.column = ...`, `text.row = ...`) when its slice opens — the market and equipment screens
have the most, the status and data screens one each; the sketches are proposals for the owner to
accept or redraw (§11) and are not normative until a slice lands them.

| Screen | Layout | What the table cannot do, and the twin that does it |
|---|---|---|
| Status (`STATUS`) | Two columns: commander, present system, hyperspace, condition, fuel, cash, rating, on the left 36 columns; the equipment list on the right 36 | Nothing; the equipment list is consecutive rows and an anchor moves its first |
| Buy / sell / inventory (`TT167`, `TT210`, `TT213`) | The table widened: item, unit, price and quantity at columns 4, 26, 38 and 52; `ListCargo` the same | Nothing |
| Equip ship (`EQSHP`) | Item at column 4, price at column 44; the laser view prompt on its own line | Nothing |
| Data on system (`TT25`) | The label/value pairs at columns 4 and 28; the description re-wrapped at `wrapWidth = 64` | **The wide sink re-wraps.** `TT27`'s justifier breaks lines at `JUSTIFIED_LINE_WIDTH` (30) and emits the break as a newline in the character stream. The wide sink treats a break the justifier made (the printer knows, because it made it) as soft and re-wraps at the layout's width; a break from a token is hard. The faithful stream is unchanged |
| Long-range chart (`TT22`) | The map at 512×256 from cell (8, 4): each system plotted at `(2x, y)`, which is the original's half vertical scale doubled; the title rule and legend above and below | `PlotDot2x` at `(2x, 2 * top + (y & ~1))` — the faithful `y >> 1` doubled is `y` less its low bit, kept so the map is the original's and not a redrawn one; `DrawCrosshairs2x` |
| Short-range chart (`TT23`) | The full 640×352 above a two-row legend; systems as the planet twin's discs; the fuel circle at twice its radius; **labels no longer collide**, because a name that overlapped its neighbour in 40 columns has room in 80 — the anchor for a label is computed from the 2× position exactly as `TT23` computes it from the 1× one | `DrawSun2x` and `DrawCircle2x` are the planet's (§4.2); the label placement (`TT23` derives a label's column from the disc's x by dividing by eight) has a twin that divides the 2× position by eight |
| Title (`TITLE`) | The ship centred in a 512×288 view as in flight; "COMMODORE 64 ELITE", the load prompt and the "press space" line at rows 4, 40 and 44 | Nothing beyond the ship, which is `LL9`'s twin |
| Name entry, save menu (`MT26`, `SVE`) | Prompt and input line centred | Nothing |
| Briefings and incoming messages (`BRIEF`, `BRIEF2`, `DEBRIEF`, `BRP`) | Text re-wrapped at 64 like the data screen; the Constrictor at the view's centre | The re-wrap, and `LL9`'s twin for the ship |
| Pause screen (`FREEZE`) | Default layout | Nothing — it prints nothing, it holds the frame |
| Death (`DEATH2`) | The wreckage in the 2× view, "GAME OVER" where it is | Nothing beyond the space view's twins |
| Flight views | §6.2 | Nothing |

Two sketches, so the shape is visible before any table is written. The market screen at 80 columns,
the frame omitted:

```
                            LAVE MARKET PRICES
  ────────────────────────────────────────────────────────────────────────
  PRODUCT               UNIT        PRICE          FOR SALE
  FOOD                  T           4.4            10
  TEXTILES              T           6.6            12
  RADIOACTIVES          T           23.4           3
  ...
```

And the status screen, two columns where the original had one:

```
                            COMMANDER JAMESON
  ────────────────────────────────────────────────────────────────────────
  PRESENT SYSTEM      : LAVE            EQUIPMENT:
  HYPERSPACE SYSTEM   : LAVE              FUEL SCOOPS
  CONDITION           : DOCKED            E.C.M. SYSTEM
  FUEL                : 7.0 LIGHT YEARS   FRONT PULSE LASER
  CASH                : 100.0 CR
  LEGAL STATUS        : CLEAN
  RATING              : HARMLESS
```

Nothing in either sketch is a new sentence, a new token or a new order: every string is the one the
faithful screen prints, in the order it prints it, and the layout only decides the cell. That is the
line this design will not cross, because the character stream is what the fixtures compare and a
re-flow that changed it would be a change to the game.

---

## 7. The presenter and the window

`CanvasPresenter` becomes `ScreenPresenter` in name and in three numbers: the texture is 640×400
`R8_UINT`, the upload footprint follows it, and `m_resolved` is `Picture::WIDTH * Picture::HEIGHT`.
`Present` takes the picture, the canvas and the video state and calls `Picture::Resolve`. The pixel
shader's literal `float2(320.0, 200.0)` becomes two more root constants beside the palette (eighteen
in all), so the shader never again knows the size. `FitCanvas` becomes `FitPicture` over the
picture's constants and `ShellTests` moves with it. **The three shader FILES keep their names**: the
blit is a texture, a quad and a palette lookup and knows nothing of either surface, and renaming
files with custom FXC build steps that no Linux leg compiles would buy a word and risk the one leg
that reads them. `Window::Create`'s client area is the screen
at `INITIAL_SCALE`, which drops from 3 to 2: **1280×800** (owner ruling, §11.3), which fits a 1080p
display with room for a title bar where 3× (1920×1200) would not. On a 1440p display the largest integer scale is 3×, on a
4K one 5×; the letterbox rule is unchanged. `GoldenCanvas.cpp`'s PNG writer and `golden_diff.py`
take a width and a height rather than the canvas's.

The presenter is the one place `Canvas::Resolve` stops being called in the executable, and it is not
deleted: every golden, `ViewChangeTests` and `TextPrintTests` call it, and the shadow tests of §8.1
call both.

---

## 8. Verification — what pins the picture, since the oracle cannot

The canvas keeps every test it has. The screen has no oracle and never will, and the design says
what stands in for one, in order of strength.

**8.1 Shadow tests — the screen against the canvas.** For every scene the goldens and the whole-frame
comparisons already build (a framed text screen, a cleared screen, `MA23`'s frames with a ship, a
sun, an explosion), resolve both surfaces and downsample the screen's space view by OR over each
2×2 block. The property: **every lit canvas pixel has a lit block at its position or an adjacent
one, and every lit block has a lit canvas pixel at its position or an adjacent one.** One pixel of
slop is the clipper's corner case (§4.1) and nothing else; a twin that dropped a line, doubled a
coordinate twice or drew from the wrong heap fails it by dozens. For the dashboard the property is
per instrument: a bar's 2× length halved is its 1× length, a blip's block position quartered is the
fat pixel, the missile block's colour is the cell's. For text: every glyph the faithful printer drew
is drawn exactly once on the screen, at the cell the view's layout names, and **no two glyphs land on
one wide cell unless they also landed on one canvas cell** — which is the test that catches a
re-flow table that collides (R24).

**8.2 Property tests on the twin arithmetic.** `Project2x` halved equals `PROJ` over the 65,536-case
sweeps `ShipDrawTests` already runs on `PROJ`; `Divide512 >> 1 == LL28` for every `(A, Q)` where
`LL28` does not saturate; `isqrt` against a reference over the sun's whole range; the scanner twin's
block quartered against `SCAN`'s fat pixel over the ship-position sweep `ScannerTests` runs. These
are the tests that pin the *extra* bit, which the shadow tests cannot see.

**8.3 Goldens of the screen.** `Screen::Hash()` for the same scenes the canvas goldens hash, with the
PNGs on failure and `golden_diff.py` at 640×400. Recorded when a slice's shadow test is green and
its hand-check is done, and never re-recorded without the diff attached (R10).

**8.4 The replay, twice.** `TheScriptedFlightIsAsRecorded`, `TheDeathIsAsRecorded` and
`TheEscapePodIsAsRecorded` run with the screen drawn and with the screen's twins compiled out
(`ELITE_SCREEN_SHADOW` off, a test-only define), and the digests must agree with the recorded tables
and with each other. This is T1's proof: the screen consumed nothing the game notices.

**8.5 Mutants.** `Screen.cpp`, `ShipDraw2x.cpp`, `PlanetDraw2x.cpp` and `Dashboard2x.cpp` join the
floor in `mutants.json`, each with a caught mutant and a `selftest`, and the shadow tests are what
catch them: a `<< 1` made `<< 2` moves every line by a screen width.

**8.6 A checker, because the rule in §4 is the kind that rots.** `tools/check_twins.py` reads every
`.cpp` in the site table of §4 and requires that each call to a canvas primitive is followed within
the same function by a call to a `*2x` routine, and that every `*2x` routine carries its `/// 2x of:`
line naming a faithful routine that exists. It joins `check_all.py` as the seventeenth check, and
`AGENTS.md` §6 gains its line. A faithful routine edited without its twin is the failure this design
is most likely to suffer, and a checker is cheaper than the shadow test that would otherwise be the
first to notice.

**8.7 The hand-check.** R5's mitigation extends to the screen: one screenshot per slice on the
owner's machine, recorded in the plan's journal, because the picture is the deliverable and no
property above says whether the dashboard *looks* right.

---

## 9. The amendments

Written here so the slice that makes them (RS-6) copies rather than composes.

- **ADR-001 §1**, after "same lines on a 320×200 canvas": *"— which is the verification view. What
  the executable presents is `Elite::Screen`, a 640×400 rendering of the same frame
  ([Design/Resolution.md](../Resolution.md)), drawn beside the canvas and never in its place."*
- **ADR-001 §4**: the sentence "Resolution ... none is designed in this corpus" loses "Resolution",
  and gains: *"The resolution is designed in Resolution.md and is not an option: the fidelity suites
  keep their meaning because they read the canvas, which the screen never replaces."*
- **ADR-002 §4**, the resolve bullet: *"`Canvas::Resolve()` produces the 320×200 indexed image the
  tests and the goldens read; `Screen::Resolve()` produces the 640×400 one ADR-005 §1 uploads."* The
  last sentence of the logical-coordinate bullet becomes: *"Higher internal resolution is built as
  the parallel path Resolution.md §4 describes; sub-pixel accuracy and anti-aliasing are not."*
- **ADR-005 §1**, the first bullet: 640×400 for 320×200, `Screen` for canvas, and *"the window opens
  at 2×"*. The placement bullet gains: *"On the screen the same measurements hold doubled: the view
  is x 0..511 in cells 8..71 of 80, and the dashboard starts at y 288."*
- **ADR-007**: one paragraph naming `Universe::screen` as the field the state hash excludes and §3.4
  as the reason.
- **Modernize.md §4.8** ("what the executable becomes"): `ScreenPresenter` for `CanvasPresenter`.
- **ADR-008 — the screen as built**, written at RS-6 in the shape of ADR-007: what the screen is,
  what pins it, what changing a twin means, and the layouts as accepted.
- **The plan**: a Phase 6 row for this design, with the sequencing note of §2.
- **`Design/README.md`**: the reading-order row this commit adds, and a "decisions at a glance" row
  for ADR-008 when it exists.

---

## 10. The build order

Every slice is accepted on the same three things first — **the replay digests unmoved, every canvas
comparison unmoved, `check_all.py` green** — and then on its own row. Sittings are the plan's unit
(§7 of the conversion plan) and carry the same caveat: the arithmetic is the cheap part, and the
findings — a heap the twin did not know about, a screen routine that places the cursor through a
path the layout did not see — are what the estimate cannot price.

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **RS-0 The picture and the presenter** ✅ **built 2026-09-07 (§13)** | `Picture` (§3) with `Resolve`, `Hash` and the energy-bomb decode; `Universe::picture` and the hash exclusion (§3.4); `ScreenPresenter`, `FitPicture`, the root constants, `INITIAL_SCALE = 2`; **the upscale**: `Picture::Resolve` fills every region it has no native content for from the canvas doubled, per region flag, so the tree plays at 1280×800 from this slice on; `GoldenCanvas.cpp` and `golden_diff.py` at both sizes | The game plays at 640×400 looking exactly as today; `ShellTests` moved; `UniverseImage` names the field excluded; §8.4 green with nothing to twin yet | 2 |
| **RS-1 The text layer** ✅ **built 2026-09-07 (§13)** | `PrintGlyph2x`, `EraseCell2x`, `ClearTextArea2x`, `TextLayout` and `LayoutForView` (§6.2), the centred default and the space view's layout; `check_twins.py` (§8.6). **NOT the borders or `CLYNS`** — see §13. The space-view REGION does not flip here — it flips when RS-3 completes it, so this slice's pixels are drawn and not shown | The text shadow test (§8.1) green over eight scenes; the glyph lands on the cell the layout names; the check in CI | 2–3 |
| **RS-2 Ship lines** | `Project2x`, `Divide512`, `ClipLine2x`, `Bresenham2x`, `LineHeap2x`, `PushEdges`' and `EraseShip`'s twins, `SHPPT`'s dot (§4.1) | The title ship and a flight with ships at 2×; the space-view shadow test green on `MA23`'s ship frames; the property sweeps of §8.2 for `Project2x` and `Divide512`; the first screen golden | 3–4 |
| **RS-3 Planet, sun, dust, beams, rings** | §4.2 and §4.3: `ball2x`, `sun2x`, `isqrt`, the stardust and particle twins, the laser and tunnel twins. **The space-view region flips here**, which is the first slice a person sees any of RS-1 to RS-3 | Shadow test green on the sun frame and the explosion frame; `isqrt` swept; the launch tunnel presents thin rings; a hand-check of the whole upper region | 2–3 |
| **RS-4 The dashboard** | §5: the dial, indicator, missile and bulb twins, the scanner and compass twins, `bitmaps.py`'s fourth sheet and `bootstrap-2x`, `DASHBOARD_IMAGE_2X` as bootstrapped, the rectangle table and its import check, sprites pixel-doubled in `Resolve` | The per-instrument shadow properties green; the flight view entirely native (no region upscaled) and a hand-check recorded; the scanner sweep of §8.2 | 3 |
| **RS-4-art The picture** | The owner redraws `DASHBOARD_IMAGE_2X` on the bootstrap (§5.3, ruling §11.1); no slice waits on it | Imports clean; a hand-check; a screen golden re-recorded with the diff attached | owner's |
| **RS-5 The re-flow** | One sub-slice per row of §6.3 in that order, each a layout table and, where named, one twin; the wide sink's re-wrap for the data screen and the briefings; the charts' twins | Per screen: the sketch accepted before the table is written (ruling §11.2); the text shadow test green including the no-collision clause; a hand-check | 1 each, 8–10 in all; the charts are two each |
| **RS-6 Close** | The upscale removed from `Resolve` and its region flags with it; the amendments of §9; ADR-008; `outpost-elite-names` re-ceilinged; `check_outpost.py` over `ScreenPresenter`; this document's status | `check_all.py` green with the upscale gone; every ADR named in §9 amended; the plan's Phase 6 row written | 1–2 |
| **Later, optional** | Re-authored 48×42 sprites (§5.4); an aspect-ratio option (ADR-005 §1, unchanged) | — | — |

**Total: roughly 25 sittings plus the artwork**, about a quarter of the port and a third of the
modernisation, and the docked re-flow is a third of it — which is ruling 3's price, stated.

**Order against Modernize.md.** RS-0 to RS-4 run BEFORE M6-c and M6-d, settled by RS-0 landing
(§2); M6-a and M6-b may go at any time, because this track asks the original nothing. RS-5 is layout
tables and touches almost no faithful routine, and may interleave with anything.

---

## 11. Rulings on what the shape left open — taken 2026-09-07

Four things needed the owner and not a slice, and all four were ruled the day the design was
written. They are recorded here as rulings rather than as open items, so nobody re-opens them.

1. **The dashboard picture is the owner's to draw, on the bootstrap.** RS-4 ships the pixel-doubled
   `DASHBOARD_IMAGE_2X` that `bitmaps.py bootstrap-2x` produces, so the tree plays; the owner edits
   the exported BMP and imports it whenever it is ready, and no slice waits on it. The alternatives
   — a procedural first pass drawn by a script, or keeping the doubled picture as final — were put
   and declined.
2. **Every re-flowed screen is sketched before it is built.** Each RS-5 sub-slice opens with an
   ASCII sketch of that screen at 80×50 for the owner to accept or redraw, and the layout table is
   written only after acceptance. §6.3's descriptions and its two sketches are proposals, not
   approvals.
3. **The window opens at 2×, 1280×800.** It fits every common display; the player resizes and the
   letterbox picks the largest integer scale that fits. 3× and a size measured from the monitor's
   work area were put and declined.
4. **The aspect ratio stays 8:5 with square pixels, and is out of this design's scope.** ADR-005 §1's
   "5:4 or 4:3 aspect option is phase 6" stands unchanged; adding a 4:3 option here, or making 4:3
   the only mode, were put and declined.

---

## 12. Risks this design adds to the register

| # | Risk | Mitigation |
|---|---|---|
| R23 | **A faithful routine is edited without its twin.** The canvas is right, the tests are green, and the screen drifts — a line drawn from a heap the twin no longer mirrors, a new call site with no twin | `check_twins.py` (§8.6) fails the push; the shadow tests (§8.1) fail the frame; both in CI |
| R24 | **A re-flow table collides or rots.** A layout anchor that puts two fields on one cell, or a screen routine that grows a placement the table does not know, prints garbage on the screen while the canvas is perfect | The no-collision clause of the text shadow test, run over every docked screen the session tests drive; the default layout catches an unknown placement by centring it, so the failure is visible rather than silent |
| R25 | **The extra bit is wrong and nothing sees it.** A twin that halves to the faithful value at every pixel can still be off by one hi-res pixel everywhere, and the shadow test allows one | The property sweeps of §8.2 on every twin divide and root; a screen golden per scene |
| R26 | **The screen leaks into the game.** A twin that reads the RNG, or a heap carve that moves the faithful pointer differently with the twin region present | §8.4's replay run both ways; T1 as a review rule; `LineHeap2x` addressed by the faithful `HeapOffset` so there is no second pointer to move |
| R27 | **The tree is half-native for weeks.** Between RS-0 and RS-6 the picture is part canvas-upscaled and part native, and a screenshot taken then is not the design | `Picture::NativeRegions` is a struct with a `Complete()` test that RS-6 asserts, and `ThePicture::TheRegionsSayWhichSlicesHaveLanded` fails the day every region is native and the fallback is still there; the journal names which regions are native at each slice. **CORRECTED at RS-0: two regions, not three.** The design named a third, "text", and text is not an AREA — it lands over the space view in flight and over the whole screen when docked. The regions are the two the raster split already makes, and the text layer belongs to the upper one, which flips when RS-3 completes it |

---

## 13. Journal

### RS-0 — the picture and the presenter, 2026-09-07

**Built and green.** `GameLogic/Picture.h` and `Picture.cpp` are the 640×400 surface; `Universe`
owns one beside the canvas; `Outpost::ScreenPresenter` uploads it at 1280×800. The suite is
<!--count:tests-->427 tests with the oracle present, all passing, and all
<!--count:checks-->18 repository checks pass. The canvas is untouched: every oracle comparison,
whole-bitmap comparison, golden and replay digest is unmoved, which is what the slice had to prove.

**What it can claim.** `ThePicture::WithNoRegionOfItsOwnItIsTheCanvasDoubled` asserts the equation
the whole slice exists for — `picture[y][x] == canvas[y / 2][x / 2]` — over a scene with bitmap bytes
across the whole plane, a different palette in every cell, colour RAM under it, docked and in flight.
`TheSpritesDoubleWithIt` asserts the same with four hardware sprites over it, one of them expanded
and two hanging off the edges, because a sprite's position, its expand flag and the output's scale
all multiply and that is where an off-by-one would live. `TheEnergyBombReinterpretsBothSurfacesTogether`
asserts it while the bomb is burning. So "looks exactly as today" is a test rather than a claim.

**Three things the building corrected, and each is in place above.**

1. **`Elite::Screen` could not be called that.** `Universe::screen` is `ScreenState` — the 6502
   raster bytes — and the compiler said so on the first build. Two near-identical type names in one
   namespace for unrelated things is the wrong answer whichever field gets renamed, so the surface
   is `Elite::Picture`, which is the word §3 had already reached for in prose.
2. **The surface holds no raster state of its own.** §3.2 gave it a background colour; every writer
   it could have had is one the canvas already has, so `Resolve` reads `moonflower`, `welcome`,
   `DFLAG`, colour RAM and the sprite pointers from the canvas and the field is gone. One fewer
   thing for a twin to keep in step, found by looking for the writer.
3. **Two regions, not three.** "Text" is not an area — it lands over the space view in flight and
   over the whole screen when docked — so it cannot be a region of an image. The regions are the two
   the raster split already makes, and the text layer belongs to the upper one. The consequence is
   scheduling and §10 now says it: RS-1 and RS-2 draw pixels nobody sees, verified by the shadow
   tests, and the upper region flips at RS-3.

**One decode in the tree, not two.** The upscale reads the canvas through `Canvas::ResolveCell`,
which is the decode `Canvas::Resolve` itself now runs — the per-cell body was lifted out of the
anonymous namespace for it. Likewise `CompositeSprites` is the sprite blit both surfaces call, with
the output's width, height, split row and scale as parameters. Duplicating either would have been
the defect ADR-002 §4 records, where the port decoded the bitmap in one mode and hashed it in
another and every glyph came out as stripes with the whole suite green.

**The mutant `raster/ra-blit-per-sprite` was re-anchored, not dropped** (Modernize.md rule 3): its
line moved when `BlitSprite` took the output's split row instead of the canvas's constant, and it
still pins the same decision.

**Two numbers worth having.** The picture is 107,682 bytes, which takes a `Universe` from 15 KB to
123 KB — eight times, measured rather than estimated; §3.4 carries what that costs and where it
would come back from. And the opening window drops from 3× to 2×, because 640×400 at 3× is 1920×1200
and misses a 1080p display, which is ruling 11.3 arriving in code.

**One test failed on the way and it was right to.** `TheHashIsStableAndNoticesOnePixel` plotted a
point into a cell nobody had coloured and the hash did not move — because an unpainted cell is
`CellPalette{}`, black over black, so a lit bit and a clear one are both colour 0 and the point is
invisible. That is exactly what `COL2` does on the canvas before `RES2` writes it. The test paints
the cell now and says why.

**What RS-0 does NOT do**, so nobody reads more into it: nothing is drawn natively. `Complete()` is
false, both regions are the canvas doubled, and the only thing a player would notice is that the
window opens at 1280×800 instead of 960×600 and every pixel is two. The twins start at RS-1.

**Not verified here: the Windows build.** `Outpost/` is Win32 and D3D12 and no hosted Linux runner
compiles it, so `ScreenPresenter.cpp`, the eighteen root constants and the shader's `gImageSize`
have been read by `check_outpost.py` and by nothing else. The Windows CI leg is the first compiler
to see them.

**And it broke there, which is the rest of this entry.** Five `C2065`s across three files: the
scripted rename of `Screen` to `Picture` had its rules in the wrong order, so the USES became
`_picture` and `m_picture` while the DECLARATIONS stayed `_screen` and `m_screen`. Every one of the
sixteen checks passed on that tree, the suite was green, and the defect was invisible on the leg
that can see `GameLogic/` because it was entirely inside the leg that cannot.

**So `check_outpost.py` grew a fourth half, and the shape of the argument is the file's own.** It
already reads `Elite::Name`, a call's arity, `name.member` and a constructor's initialiser list --
four answers to "what can a Linux runner know about code only MSVC compiles" -- and none of them can
see a BARE IDENTIFIER that resolves to nothing. `check_bare_identifiers` is the fourth: every
`m_member` and every `_parameter` the app mentions must be one it declares. It reports all three
files the compiler did, its self-test plants this exact rename, and it is deliberately conservative
about what it cannot know (a name declared in the wrong CLASS still compiles and is still the blind
spot only a compiler closes).

**One thing the failure proved for free.** `ScreenPresenter.cpp` reached line 384 before erroring,
so its `#include` of the FXC-generated shader headers had resolved -- which means the HLSL compiled,
and the `uint2 gImageSize` root constants and the pixel shader that reads them are sound. That was
the part of §7 with no evidence behind it at all.

### RS-1 — the text layer, 2026-09-07

**Built and green.** `GameLogic/TextPrint2x.h` and `.cpp` are the layer: `TextLayout` and its `Map`,
`LayoutForView`, `PrintGlyph2x`, `EraseCell2x`, `ClearCells2x`, `ClearTextArea2x` and
`ClearMessageRows2x`. `TextPrinter` gained `AttachPicture` and pairs its three canvas writes with
twins; `Game` attaches the picture and `QQ11`. The suite is <!--count:tests-->427 tests, green with
the oracle present, and all <!--count:checks-->eighteen repository checks pass — two of them new.

**What it can claim.** The shadow test resolves nothing: it reads the two surfaces' planes and
requires that every canvas cell with ink on it has the SAME eight bytes on the picture at the cell
the layout names, that no wide cell has ink the canvas cannot account for, and that no two canvas
cells map onto one wide cell — §8.1's three clauses, including the no-collision one that Risk R24 is
about. Because the glyph does not change size, that is an equality rather than a resemblance, so a
twin one cell out fails on the first character. It runs over four views, a message printed twice, a
delete, an explicit clear, a form feed through the printer, and a printer with nothing attached.

**Two things the design could not have known, both now corrected above.**

1. **There is no wide cursor, because there cannot be one.** §6.2 had `TextState::wide` updated
   whenever the faithful cursor is *placed* — and the faithful cursor is placed by forty-odd plain
   field assignments, which C++ cannot hook without changing every site. It does not need one: at
   the moment a glyph is drawn the faithful cursor holds the cell, so the wide cell is a pure
   function of it and the layout. `TextState` is untouched.
2. **The layout is measured in canvas cells, and §6.2's numbers were in `XC`.** Both are right about
   different origins — `XC` counts from the view's left edge and a canvas cell from the screen's —
   and the clears count canvas cells because the game's own loops do. Mixed, they would have put
   every glyph sixteen cells right of the cell meant to clear it. Both layouts are +20 in canvas
   cells; four `static_assert`s pin it.

**Two things moved OUT of the slice, and the reason is the same for both.** §6.1 grouped `BOX`'s
border and the loader's furniture with the text wipe, and building shows they need a different tool:
a border is LINES, and the 2× line drawer is `Bresenham2x`, which is RS-2's. Drawing a "thin border"
now would mean inventing a line primitive that RS-2 then builds properly. `ClearMessageRows` moved to
RS-5 for a different reason — its ten callers are spread over six files and most have no picture in
scope, so pairing it is the docked screens' slice rather than this one. Its twin exists and is
unused; `check_twins.py`'s `NEEDS_NO_TWIN` says so by name, with the reason, rather than staying
silent.

**`check_twins.py` is the check, and it has three rules rather than §8.6's two.** Every `*2x`
routine names what it twins with a `/// 2x of:` line and the named routine must exist — which is the
M6-c hazard, a twin named for an identifier that has been renamed away. Every drawing function in a
twinned file calls a twin, or is named in `PAIRED_BY_CALLER` or `NEEDS_NO_TWIN` with a reason. And
the third rule is what earns the second's exemption: **a routine paired by its caller is paired by
every caller**, checked across all of `GameLogic`. Without it, "the caller pairs them" is a promise
nobody checks — which is exactly how RS-0 broke the Windows build. Its self-test plants all three.
It reports the files not yet twinned as a COUNT rather than a failure (fifteen today), because a
check that went red from RS-1 to RS-4 would teach people to skip it.

**What RS-1 does NOT do.** Nothing is shown. The space-view region flips at RS-3, so every glyph
this slice draws goes onto a surface nobody presents, and the picture on screen is still the canvas
doubled. There is no screenshot to sign off and no hand-check to record; the shadow test is the whole
of the evidence, which is what §10 said this slice would be.
