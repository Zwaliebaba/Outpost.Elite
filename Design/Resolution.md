# Resolution — the game at 640×400

**Status:** Proposed · 2026-09-07 · **eight owner rulings taken the day it was opened** — four on
the shape (§1) and four on what the shape left open (§11). **RS-0 is built, 2026-09-07** (§13): the
surface, the presenter, the upscale and eleven tests, with the suite at
<!--count:tests-->469 green against the oracle and all <!--count:checks-->18 repository checks
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
| Ship lines | `LL9`: `PROJ` centre at scale 256, `LL28` vertex offsets (8-bit), `LL145` clips 16-bit to 8-bit, `LOIN` | **CORRECTED at RS-2: none.** `LL28` is a LOGARITHM-TABLE lookup, not a truncating divide — measured against exact division over all 32,640 pairs it is out by up to 3, by 0.70 on average, and by more than one in 13% of them | **Thinner lines, and nothing else.** There is no dropped bit to recover: a twin dividing exactly at 512 would put vertices up to three canvas pixels from where the game puts them, which is a different wireframe and what rule T2 forbids. The wide line is the faithful 16-bit line doubled, drawn one wide pixel thick instead of two, and cut to the wide view at the finer boundary (§4.1, §13) |
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
| `ShipDraw.cpp` | `LOIN` per heap line; `SHPPT` dots | `DrawShipLines2x` | none — it reads the faithful heap (RS-3) | §4.1 |
| `PlanetDraw.cpp` | `BLINE` segments, `SUN` rows, the erase of both, the tunnel rings | `DrawLine2x` and `DrawCanvasRow2x` at each site | none — both heaps are the faithful ones (RS-3) | §4.2 |
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

- **CORRECTED at RS-2: there is no `Project2x` and no `Divide512`, and there cannot usefully be
  one.** Both rested on `LL28` truncating an exact quotient. It does not — it is a logarithm-table
  lookup, and §1's table now carries the measurement. What replaced them is one line of arithmetic:
  the wide vertex is the faithful 16-bit vertex `XX3` already holds, DOUBLED (`Doubled` in
  `ShipDraw2x.h`). That is provably the same wireframe at twice the scale, which is what rule T2
  asks for, and it removes a whole class of divergence rather than managing it.
- `Project2x` itself moves to **RS-3**, where the planet's centre and radius need it and where
  `DVID3B` has to be twinned anyway; `SHPPT`'s dot doubles the faithful heap bytes until then.
- **`ClipLine2x` and the wide heap are GONE, deleted at RS-3.** The design had a Cohen–Sutherland
  cut against the 2× rectangle, a parallel `LineHeap2x` at twice the same `HeapOffset`, and a twin of
  `EraseShip` walking it. None of it is needed once the twin takes the faithful line: `PushEdges`
  writes the four bytes it always wrote, `EraseShip` walks the heap it always walked, and
  `DrawShipLines2x` reads those same bytes. Nothing can drift, because there is nothing to keep in
  step.
- `SHPPT`'s dot: the distant ship is `PIXEL` at the projected centre, and the two short lines it puts
  on the heap are drawn wide by the same heap walk as everything else.

`DrawLine2x` is the line drawer. **CORRECTED at RS-3, and the correction is the slice's finding.**
It was `Bresenham2x`: an exact straight line between two separately doubled and re-clipped endpoints,
on the reasoning that `LOIN`'s mask table exists only for multicolour alternation and a bit plane
does that for free. The table is indeed not needed. THE SLOPE IS — `LOIN` is a DDA whose step is
`LineSlope(dy, dx)`, a third logarithm-table lookup, so the line the game draws is not the straight
line between its ends. Measured over 18,432 lines, the exact drawer put 41,252 wide pixels two canvas
pixels away from the faithful one, 431 three away and one four away, and drew 510 pixels on each of
the 96 lines where `LOIN`'s count wraps to zero and it draws nothing. The twin now takes the
FAITHFUL line and runs `LOIN`'s own accumulator at twice the rate: all four figures go to zero, and
what is left is 0.03 percent of pixels one canvas pixel out, from the carry `LOIN` threads out of its
screen-pointer arithmetic — an address's carry, which a surface with its own geometry cannot have.

**`Line2x`, `LineHeap2x`, `ClipLine2x`, `Doubled` and `PushHeapLine2x` went with it.** Once the twin
takes the faithful line, the faithful heap is already the record rule T3 wants: `DrawShipLines2x`
reads the four bytes `LL155` reads. That deleted 13,824 bytes of state, a `Universe` field and a
state-hash exclusion, and it removed the possibility of the two surfaces disagreeing at all.

### 4.2 Planet and sun

**CORRECTED AT RS-3 IN BOTH HALVES, and the measurements are in §13.**

The planet's centre comes from `PROJ` and `DVID3B`, and `DVID3B` has an eight-bit mantissa — inside
`PLS6`'s own range it is out by 8 to 15 pixels in three percent of cases — so `Project2x` cannot
exist and the centre is the faithful one doubled. Each segment endpoint comes from `FMLTU2`, a
logarithm-table multiply out by up to 3 canvas pixels in six percent of the 16,384 (radius, angle)
pairs, so `radius2x * SNE[step] / 256` would be a DIFFERENT circle — one no longer concentric with
the crater and the meridians, which come through the same table. So `ball2x` does not exist either:
`CIRCLE2`'s step walk and `BLINE`'s segment decisions stay (T1) and the twin draws each accepted
segment doubled, out of the heap `BLINE` already fills. `EraseBall` walks the same heap for both
surfaces. The planet's detail is `DrawEllipse` and `DrawHalfEllipse` over the same `BLINE`, so it
comes with the circle.

The sun is a stack of rows. `SUN` computes one half-width per canvas row from `LL5`'s square root
and stores it in the 200-byte `LSO`; `WPLS` erases from the same bytes. `LL5` **is** exact — swept
over all 65,536 radicands — so a 288-row `sun2x` from a 32-bit `isqrt` would have been honest. It
would also have been invisible: `SUN` adds `DORND AND CNT` to every row, and `CNT` is 1 for a radius
of 16, 3 for 40 and 7 for 96, so any sun bigger than 32 pixels across already carries one to seven
pixels of deliberate noise on its edge — and the two hi-res rows of a canvas row must share one roll
of it (T1). Half a pixel of square root under seven pixels of RNG is not a gain a person can see, so
the sun's rows are the faithful rows doubled and the sliver arithmetic is written once. `Yx2M1`, the
variable bottom row the short-range chart moves (§6.45), is read doubled.

### 4.3 Stardust, particles, beams, rings

Stardust is the case T2 was written for. Each speck's position is sixteen bits an axis and the
faithful `PIXEL2` plots the high byte; the twin plots `(hi << 1) | (lo >> 7)` — the bit that was
already there. The movers stage the OLD position before they update it (`Stardust.h`), and the twin
is called at both plots from the bytes staged for each, so erase matches draw (T3).

Explosion particles are eight-bit positions rolled from the cloud's seed inside the drawing loop;
the twin draws the mark at the doubled position. A `PIXEL` mark is two canvas pixels wide and one or
two tall, and the twin draws the same shape in HI-RES pixels — half the size, centred on the point
rather than on `TWOS2`'s left-leaning anchor (§13) — so the cloud is a shower of points rather than
of smears, and the distance grading survives because the faithful routine still decides it.

The laser beam is four `LOIN`s from the view's corners to an eight-bit convergence point; the twin
draws the same four through `DrawLine2x`, and `LASLI2`'s erase draws them again from the same
`LaserBurst`. It is the beam that found RS-2's line drawer wrong: ninety pixels is long enough for a
log-table slope to diverge from a straight one, and a ship's edges are not (§13). The launch and hyperspace tunnels are `HFS2` circles from eight-bit centres and
radii, presented one per ring through `Presenter::Present`; the twin draws each ring thin at the
doubled numbers and the `TT66` that ends the effect clears both surfaces.

---

## 5. The dashboard at twice its detail

### 5.1 Dials

`DIL` lights `value >> shifts` of sixteen fat pixels across four cells, storing the bytes rather
than exclusive-oring them. `DrawBar2x` lights `value >> (shifts - 1)` of thirty-two steps, each two
hi-res pixels wide and **six tall** — corrected at RS-4 from "fourteen", which was an arithmetic
slip: `DIL` writes three canvas rows and three doubled is six. The bar keeps its height because
ruling 1 keeps the dashboard the same share of the screen; what it gains is the step.

**Corrected at RS-4: the four ENERGY bars are the exception, and they are named because the sweep
found them.** `DILX` is reached with no shift at all for those, and the value has already been dealt
out sixteen at a time by `DLL24`, so it is 0..16 with nothing under it: there the twin doubles, and
the bar is the same bar at half the step. Everything else — the speed at one shift, the fuel at two,
the shields and the two temperatures and the altitude at four — has a real bit and takes it.

The ink is `DIL`'s own, because the danger flash is `PZW`'s decision (T1). The same entry-point
shape — the shift count is the argument, because `DILX`, `DILX+2`, `DIL-1` and `DIL` are four scales
of one routine — so the four callers change nothing but which twin they name.

`DIL2`'s roll and pitch indicators light one block of sixteen; the twin lights one of thirty-two,
two pixels wide. **Corrected at RS-4: only the ROLL has a bit to spend on the extra slots.** `DIALS`
builds it from `alp1 >> 2`, two bits thrown away, so `alp1 >> 1` is one of them back — and the
centre doubles with it. The pitch's `beta` is `bet1` with its sign on, and `bet1` was shifted down
in the flight loop within four instructions of the joystick: the bit is gone before the dial sees
it, and recovering it would mean recomputing game state rather than rendering it. The pitch
indicator therefore doubles.

`MSBAR` writes a palette byte to a missile cell and the bulbs exclusive-or one into two cells.
**Corrected at RS-4: all three twins are the same one function**, and it is not a fill. What a
palette write does on the hardware is recolour whatever bits are already in the cell, so the honest
twin is to decode that cell again through `Canvas::ResolveCell` and double what comes out — which is
also the bootstrap below, and the one place the two meet. All are STORE-semantics and are redrawn
every frame by `DIALS`, so none needs a heap.

### 5.2 The scanner and the compass

`SCAN` puts a blip at `(123 + x_hi, ...)` across, `z_hi >> 2` down the ellipse and `y_hi >> 1` up
the stick, discarding `x_lo`, `z_lo` and `y_lo`. The twin takes the same three answers and adds the
top bit of `x_lo`, signed the way the magnitude is — which is four times finer across than the
canvas, because `CPIX2` indexes the ALIGNED mask table and the faithful dot snaps to a fat pixel,
losing `X1`'s bottom bit as well. The blip is a 4×4 hi-res block in the ship type's scanner colour,
the stick a two-pixel-wide vertical, both exclusive-ored into the plane so that `WPSHPS`'s second
call erases them (T3). `SCAN`'s decision whether a ship is on the scanner at all is its own (T1).

**Corrected at RS-4: the VERTICAL fractions are not taken, and that is a scoping call rather than an
oversight.** `z_lo` and `y_lo` are the same kind of byte and the same bit is there, but the row is a
clamped sum of two negated magnitudes — `~(z_hi >> 2 + 83)` plus a complemented `y_hi >> 1`, clamped
into 146..198 — and reproducing it at twice the scale is a second copy of `SCAN`'s arithmetic to
keep in step, in eight-bit wrapping that is load-bearing. What it would buy is one hi-res row on a
fifty-two row scale. It is written down here rather than left for somebody to rediscover, and it is
a small follow-up with a sweep behind it if the owner wants it.

`COMPAS` scales a unit vector of length 96 to a dot at `2 * A / 20` from the centre. **Corrected at
RS-4: the twin does NOT scale to a radius of forty, and this is the one place the building declined
a bit it had measured.** `DVID4`'s whole part is exact division — swept over all 65,280 pairs — so a
twin dividing at twice the radius would be the same function on a bigger domain and entirely
legitimate. It may not, because `COMPAS` erases last frame's dot by drawing it again out of `COMX`
and `COMY`, and those two bytes are the game's: a finer position would have to be remembered beside
them, unhashed, and kept in step by hand. That is the parallel-record shape RS-3 spent a slice
deleting, and what it would buy is half a dot's width on a disc twenty canvas pixels across. The
wide dot is `COMX` and `COMY` doubled.

`DOT`'s block-or-dash by colour is a decision; the twin draws a 4×4 block for ahead and a 4×2 dash
for behind, which is the canvas's shape halved — one fat pixel of width instead of two, as the blip
is.

### 5.3 The picture

`DASHBOARD_IMAGE` is 2,241 bytes of two-bit pixels, 160×56, coloured per cell by
`DASHBOARD_SCREEN_COLOURS`; `wantdials` copies it into the bitmap.

**CORRECTED AT RS-4: there is no `DASHBOARD_IMAGE_2X` and there is no `bootstrap-2x`.** The design
had a 71,680-byte generated table holding the faithful picture upscaled, and a fourth sheet in
`tools/bitmaps.py` to produce it. A generated table that is a pure function of a table already in
the tree is a COPY, and this repository has spent three slices removing copies that two people have
to keep in step. So the bootstrap is computed where it is needed instead — `CopyDashboardPicture2x`
decodes each dashboard cell through `Canvas::ResolveCell`, with the loader's cell colours and colour
RAM already applied, and doubles it into the plane. Same pixels, no second file, and it is checked
by an EQUALITY rather than by a golden: `TheBootstrapIsTheCanvasDoubledExactly` compares all 71,680
of them against the resolved canvas.

It still **needs a person with a paint program, and the design still says so rather than pretending
a tool can draw it — the owner, by ruling (§11.1), on no slice's schedule.** What it no longer needs
is a tool to start it: `Testing::WritePicturePng` already writes the resolved 640×400 picture as an
indexed PNG, which is the bootstrap image to paint over. RS-4-art replaces the call to
`CopyDashboardPicture2x` with an imported table and changes nothing else.

Two constraints for whoever draws it, which RS-4-art should check on import: only the sixteen
palette colours, and the positions the twins draw into (the bar troughs, the indicator tracks, the
missile blocks, the scanner ellipse's interior, the compass disc) left as the background colour, so
that a dial's unlit steps read as a trough rather than as artwork.

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

**CORRECTED at RS-5-0: an anchor is a RECTANGLE, and the table above's cell-to-cell version cannot
work.** `Map` is a pure function of the faithful cell — that is what the paragraph above settled —
so nothing remembers that the E of "EQUIPMENT:" was moved by the time the Q is printed. A cell-to-cell
table would have to name all ten cells of that heading, and the equipment list's eleven rows of up
to twenty-four characters would be some two hundred and fifty entries kept in step by hand. An
`Anchor` is therefore a rectangle of faithful cells relocated to a wide origin, with a row stride of
its own, and the status screen's whole re-flow is ONE of them:

```cpp
struct Anchor
{
  std::uint8_t firstColumn, lastColumn; // the faithful cells it covers, inclusive
  std::uint8_t firstRow, lastRow;
  std::uint8_t wideColumn, wideRow;     // where its top-left cell goes instead
  std::uint8_t rowStride;               // and how the block's own rows are spread once there
};
```

**`wrapWidth` IS DECLINED, and this is the measurement — RULED at RS-5-c.** §6.2 lists it and §6.3
asks the wide sink to re-wrap the data screen and the briefings at 64 columns, treating a break the
justifier made as soft. It cannot be done without giving up the property every twin in this track
rests on, and the reason is that `DA11` does not merely BREAK the line — it PADS it. Elite's
justification widens the gaps between words until the thirtieth character is a space, so the stream
the canvas receives already carries the padding for a thirty-column measure:

```
This planet  is  most  notable
for  Tibediedian  Arnu  Brandy
```

Those double spaces are `PadToWidth`'s, not the token's. Re-flowing that to 64 columns without
re-justifying carries thirty-column padding into a sixty-four-column line, which is ragged text with
holes in it; re-justifying means the wide surface computing its own `PadToWidth` at 64 and emitting a
DIFFERENT NUMBER OF SPACE CHARACTERS from the canvas. That breaks the invariant `TheGlyphsAgree`
tests and the whole of §8.1 rests on — every glyph the canvas has, the picture has at the mapped
cell, and nothing else — for the two screens where the text is longest and hardest to check by eye.
What it would buy is a wider paragraph measure. Declined, as RS-4 declined the compass bit, and for
the same kind of reason: a real improvement that costs the evidence.

**So the description gets a COLUMN instead of a WIDTH.** Thirty characters is a perfectly good
measure — narrower than a newspaper's — and 80 columns is enough to stand it beside the label/value
pairs rather than under them. That is a table, which is what every other screen gets.

**The cheap way to keep the no-collision property is OPPOSITE ROW PARITIES.** With `rowStride = 2`
every unanchored faithful row lands on a wide row of `rowOffset`'s parity; give every anchor the
other parity and an anchored block can never share a wide row with an unanchored one, which leaves
only anchor-against-anchor overlaps to think about — a handful of column ranges on one row. Found
while drafting the trade screens' tables, where the first attempt collided seventeen times on
exactly that. The status screen anchors odd and offsets even; the trade screens do the reverse.

**EVERY ANCHOR RANGE IS IN CANVAS CELLS, and a range that ends inside a word shears it.** An `XC`
of 1 is canvas cell 5 — the four cells of margin the game's own loops count — so a range copied off a
cursor-position dump is four cells left of the text it was meant to cover, and the words at its
edges come out split: "Radioacti   ve  s". The shadow test cannot see this. A sheared mapping is
still injective, still puts every canvas glyph on the picture, and still leaves nothing on the
picture the canvas lacks — all three clauses of §8.1 hold on a screen nobody could read. So §8.1
gains a fourth: **two canvas cells side by side, both with ink, must land side by side**
(`PictureTextTests::NothingIsSheared`, run on every screen test). It is the property a table is FOR.

**And a table anchored INTO the centred layout cannot be written at all**, which the collision sweep
found the first time it was run: a 40-column screen placed at column 20 already covers columns 20 to
59, so every anchor target inside the wide grid is a cell the offsets already reach. A re-flowed
screen moves its offsets as well as its blocks. `PictureTextTests::NoLayoutSendsTwoFaithfulCellsToOne
WideCell` sweeps the whole 40×25 grid of every layout in the tree and is Risk R24's tripwire.

**`QQ11` DOES NOT NAME A SCREEN, and the layout is therefore the caller's — RULED at RS-5-a.**
`LayoutForView` reads the view byte, and the status screen and the inventory screen are both view 8:
`STATUS` and `TT213` each call `TRADEMODE` with `#8`, so no function of that byte can tell them
apart. `SetUpScreen` and `SetUpTradeScreen` each gained a four-argument form taking the layout, and
each screen names its own at the one instruction that already says which screen is starting. The
layout is stored in `Universe::screenLayout` in the same breath as `QQ11` — one instruction, two
facts, nothing to drift — and `TextPrinter` reads it per glyph through the pointer that used to
point at the view. Two overloads rather than a default argument, because the default is a function
of `_view` and C++ cannot write one parameter's default in terms of another. The field is NOT folded
into the state hash, for `picture`'s reason: it decides nothing the game does.

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
| Status (`STATUS`) | **LANDED at RS-5-a**, sketch accepted 2026-09-08. `STATUS_LAYOUT{0, 4, 2}` with two anchors: the title to wide (20, 3), and canvas rows 12 to 23 — "EQUIPMENT:" and the eleven lines under it — to wide (42, 12) at stride 2 | Nothing; the equipment list is one anchored rectangle. **CORRECTED at RS-5-0: not "an anchor moves its first"** — an anchor covers a block, because a per-cell one would leave the rest of each string behind |
| Buy / market (`TT167`, `TT219`) | **LANDED at RS-5-b**, sketch accepted 2026-09-08. `BUY_LAYOUT{0, 1, 2}` with ten anchors: four fields whose canvas ranges are 0–17, 18–19, 20–24 and 25–39, and six that fold the game's TWO heading rows onto one wide row | Nothing |
| Inventory (`TT213`) | **LANDED at RS-5-b.** `INVENTORY_LAYOUT{0, 1, 2}`: title, the fuel and cash lines on the left, the hold anchored to wide (42, 4) so its first item is level with the fuel | Nothing |
| Sell cargo (`TT210` at view 4) | **BLOCKED, and not on this design.** `TT208`'s head — the `TRADEMODE` and the "SELL CARGO" title — is not in the port: `KeyAction::SellCargo` calls `ListCargo` directly, so the sell screen never sets its view, never clears, and has no instruction at which to name a layout. It inherits whatever the previous screen left. Porting `TT208`'s head is a change to the character stream and is not a re-flow's to make | — |
| Equip ship (`EQSHP`) | **LANDED at RS-5-b.** `EQUIP_LAYOUT{0, 1, 2}`: number and item over canvas columns 0–24, price over 25–39 right-aligned to wide 52, and a fourth anchor that brings `CLYNS`'s row 21 back to wide row 40 from the 43 the offsets would give it | Nothing |
| Data on system (`TT25`) | **LANDED at RS-5-d**, sketch accepted 2026-09-08: `DATA_LAYOUT{4, 8, 1}` — the only table with `rowStride = 1`, because `TT25` double-spaces itself. **CORRECTED at RS-5-c: a column, not a width, and not two columns of pairs.** The pairs stay whole on the left — their colons are at a different canvas column on every line, so no rectangle can split label from value — and the description moves to a column of its own beside them, at the thirty characters `DA11` justified it to | Nothing. **The wide sink does NOT re-wrap**, and §6.2 carries the measurement: the faithful stream already holds thirty-column PADDING, so re-wrapping means re-justifying, and re-justifying means the picture carrying different space characters from the canvas |
| Long-range chart (`TT22`) | **LANDED at RS-5-e.** `LONG_RANGE_LAYOUT{0, 19, 1}` with one anchor: the title to wide (20, 4), above the top rule. **CORRECTED: the map does not move.** `ToSpaceViewPoint` already supplies the space view's 64-pixel margin, so the doubled map is at 512×256 from cell (8, 4) — exactly what §6.3 asked a translation to produce. What was wrong was the TEXT, which the centred layout put at wide row 13, in the middle of the map | Nothing. `PlotDot2x` is `PlotPixel2x` and `DrawCrosshairs2x` is `DrawLine2x`; RS-3 built both |
| Short-range chart (`TT23`) | **LANDED at RS-5-e.** `SHORT_RANGE_LAYOUT{0, 0, 2}` — `rowStride = 2` so every name keeps its disc's height — with one anchor for the title, which must not scale | **`TextPrinter::SetLabelRun`**, and it is the one thing a layout cannot do. A label's ORIGIN must double while its letters stay eight pixels apart; a column stride scales the gaps too and prints "O r r e r e". `TT23` is the only thing that knows where a disc landed, so it says so, once, at the instruction that already places the cursor. **"Labels no longer collide" is DECLINED, measured**: 142 names of 2,668 systems in range across all 256 charts, 0.4 per chart |
| Title (`TITLE`) | The ship centred in a 512×288 view as in flight; "COMMODORE 64 ELITE", the load prompt and the "press space" line at rows 4, 40 and 44 | Nothing beyond the ship, which is `LL9`'s twin |
| Name entry, save menu (`MT26`, `SVE`) | Prompt and input line centred | Nothing |
| Briefings and incoming messages (`BRIEF`, `BRIEF2`, `DEBRIEF`, `BRP`) | **CORRECTED at RS-5-c**: not re-wrapped at 64, for the data screen's reason. The justified block is placed as it stands; the Constrictor at the view's centre | `LL9`'s twin for the ship, and nothing for the text |
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

And the status screen, two columns where the original had one. **ACCEPTED 2026-09-08 and LANDED at
RS-5-a**; this is the wide grid rendered by running `STATUS` through `STATUS_LAYOUT`, not a drawing,
with the wide row numbers on the left:

```
  3 |                               COMMANDER JAMESON                                |
 12 |     Present System      :Tibedied             EQUIPMENT:                       |
 14 |     Hyperspace System   :Reorte                    Escape Pod                  |
 16 |     Condition           :Docked                    Fuel Scoops                 |
 18 |     Fuel:7.0 Light Years                           E.C.M.System                |
 20 |     Cash:    100.0 Cr                              Energy Bomb                 |
 22 |     Legal Status: Fugitive                         Extra Energy Unit           |
 24 |     Rating: Above Average                          Docking Computers           |
 26 |                                                    Galactic Hyperspace         |
 28 |                                                    Front Pulse Laser           |
 30 |                                                    Rear Beam Laser             |
 32 |                                                    Left Military  Laser        |
 34 |                                                    Right Mining  Laser         |
```

The body ends on wide row 34 and the dashboard's footprint starts at 36, so the screen finishes
where the dashboard would begin rather than trailing off. The title is on wide row 3 and not 4
because `NLIN3`'s rule is drawn at canvas row 19 and its twin is a wide line at row 38, which is
inside the glyphs of wide row 4. Nothing draws that rule today — `NLIN3`'s is "the canvas's, so a
caller draws it" and only the two charts have a caller that does, which is a gap in the docked text
screens older than this track.

**CORRECTED at RS-5-0: the strings above are the printed ones and the first draft's were not.** That
draft had every label in capitals with the colons in one column, and the screen prints neither: only
the title and "EQUIPMENT:" are capitals, the first three labels are tabbed to column 21 and the last
four carry their colon inline, and the spacing inside "Cash:    100.0 Cr" is `PCASH`'s own. The
strings here were read off the port printing the screen rather than written down from memory, which
is the standard the paragraph below sets and the draft did not meet.

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

**8.2 Property tests on the twin arithmetic. CORRECTED at RS-2 and RS-3: the properties below do
not hold and could not, because every routine they name is a logarithm-table lookup rather than a
truncating divide.** What replaced them is an equality of a different shape — the wide drawing is the
faithful drawing at twice the scale, swept over the whole input space — plus a test per table that
pins its INEXACTNESS, so that a later slice cannot quietly reintroduce an exact twin. The original
text is kept below because the reasoning it encodes is the trap: `Project2x` halved equals `PROJ`
over the 65,536-case sweeps `ShipDrawTests` already runs on `PROJ`; `Divide512 >> 1 == LL28` for
every `(A, Q)` where
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
| **RS-2 Ship lines** ✅ **built 2026-09-07 (§13)** | `Bresenham2x`, `ClipLine2x`, `LineHeap2x`, `Doubled`, `PushEdges`' and `EraseShip`'s twins, `SHPPT`'s dot (§4.1). **NOT `Project2x` or `Divide512`** — the premise for them was measured false; they move to RS-3 where the planet needs `DVID3B` twinned anyway. The borders and the loader move there too, for a different reason (§13) | The space-view shadow test green over three ship distances; the wide vertex is the faithful one doubled over all 65,536 values; the half-open line drawer measured against `LOIN` | 3–4 |
| **RS-3 Planet, sun, dust, beams, rings** ✅ **built 2026-09-07 (§13)** | §4.2 and §4.3, **and everything else the upper region carries**, because a region is native for every screen that draws in it: the border and the rules, `TTX66K`'s wipe and the loader's, the cell palettes, `CLYNS`, and both charts at twice the scale (RS-5 re-flows them). **NOT `ball2x`, `sun2x` or `isqrt`** — all three premises measured false or invisible. **The space-view region flips here**, and this is the first slice a person sees | Shadow tests green on the planet, the sun over three frames of drift, the explosion cloud, the beam and the border; the stardust half-pixel swept over 262,144 cases; the wide line swept over 18,432 lines in both directions | 2–3 |
| **RS-4 The dashboard** ✅ **built 2026-09-07 (§13)** | §5: the dial, indicator, missile and bulb twins, the scanner and compass twins, and the bootstrap. **NOT `DASHBOARD_IMAGE_2X`, `bitmaps.py`'s fourth sheet or `bootstrap-2x`** — a generated table that is a pure function of one already in the tree is a copy, so the bootstrap is computed (§5.3). Sprites were already doubled in `Resolve` at RS-0. **The dashboard region flips here, so nothing on the screen is upscaled any more** | The bootstrap equal to the canvas doubled over all 71,680 pixels; the bar sweep over every value at every entry point; the scanner's fraction swept over both signs and every high byte; a whole `DIALS` frame shadow-tested; blips erase by redraw | 3 |
| **RS-4-art The picture** | The owner redraws the dashboard at 640×112 over the bootstrap PNG (§5.3, ruling §11.1); the import replaces `CopyDashboardPicture2x`'s call and nothing else; no slice waits on it | Imports clean; a hand-check; a screen golden re-recorded with the diff attached | owner's |
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
| R24 | **A re-flow table collides or rots.** A layout anchor that puts two fields on one cell, or a screen routine that grows a placement the table does not know, prints garbage on the screen while the canvas is perfect | `PictureTextTests::NoLayoutSendsTwoFaithfulCellsToOneWideCell` sweeps the whole 40×25 grid of every layout in the tree and fails on the first collision (RS-5-0, and it caught one the day it was written); the default layout catches an unknown placement by centring it, so the failure is visible rather than silent |
| R25 | **The extra bit is wrong and nothing sees it.** A twin that halves to the faithful value at every pixel can still be off by one hi-res pixel everywhere, and the shadow test allows one | The property sweeps of §8.2 on every twin divide and root; a screen golden per scene |
| R26 | **The screen leaks into the game.** A twin that reads the RNG, or a heap carve that moves the faithful pointer differently with the twin region present | §8.4's replay run both ways; T1 as a review rule; and since RS-3 there is no second heap at all — the twins read the faithful bytes, so there is no second pointer to move |
| R27 | **The tree is half-native for weeks.** Between RS-0 and RS-6 the picture is part canvas-upscaled and part native, and a screenshot taken then is not the design | `Picture::NativeRegions` is a struct with a `Complete()` test that RS-6 asserts, and `ThePicture::TheRegionsSayWhichSlicesHaveLanded` fails the day every region is native and the fallback is still there; the journal names which regions are native at each slice. **Both regions flipped by RS-4 and `Complete()` now holds — the tripwire has fired and RS-6 is the only slice left that owes anything to it.** **CORRECTED at RS-0: two regions, not three.** The design named a third, "text", and text is not an AREA — it lands over the space view in flight and over the whole screen when docked. The regions are the two the raster split already makes, and the text layer belongs to the upper one, which flips when RS-3 completes it |

---

## 13. Journal

### RS-0 — the picture and the presenter, 2026-09-07

**Built and green.** `GameLogic/Picture.h` and `Picture.cpp` are the 640×400 surface; `Universe`
owns one beside the canvas; `Outpost::ScreenPresenter` uploads it at 1280×800. The suite is
<!--count:tests-->469 tests with the oracle present, all passing, and all
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
twins; `Game` attaches the picture and `QQ11`. The suite is <!--count:tests-->469 tests, green with
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

### RS-2 — the ship lines, 2026-09-07

**Built and green.** `GameLogic/ShipDraw2x.h` and `.cpp` are the layer: `Line2x`, `LineHeap2x`,
`Doubled`, `ClipLine2x`, `Bresenham2x`, `PushHeapLine2x` and `DrawShipLines2x`. `Universe` owns the
wide heap beside the faithful one; `ShipRender` carries the surface; `PushEdges`, `EraseShip`,
`DrawShipLines` and `SHPPT`'s dot all pair. The suite is <!--count:tests-->469 tests, green with the
oracle present, and all <!--count:checks-->eighteen repository checks pass.

**THE SLICE'S REAL FINDING IS THAT ITS PREMISE WAS FALSE, and it took a measurement to see it.**
§1's table had the space view gaining a bit of precision because "the divides truncate to a pixel",
and §4.1 built `Project2x` and `Divide512` on that. `LL28` does not truncate a quotient: it is a
LOGARITHM-TABLE lookup, and swept against exact division over all 32,640 pairs it is out by up to 3,
by 0.70 on average, and by more than one in 13% of them. So there is no dropped bit to recover, and
a twin that divided exactly would place vertices up to three canvas pixels from where the game
places them — a different wireframe, which is precisely what rule T2 exists to forbid.

**What the ship actually gains is thinness.** The wide line is the faithful sixteen-bit line
doubled, drawn one wide pixel thick where the canvas draws two, and cut to the wide view at the
finer boundary. That is worth having — a wireframe in one-pixel lines at 640×400 reads far better
than one in two-pixel lines at 320×200 — and it is a smaller claim than the design made. Where a
real bit DOES exist it is a byte the faithful plot throws away rather than a table's resolution: the
stardust's `SXL`/`SYL` fractions (RS-3) and the scanner's `x_lo` (RS-4) still gain what §1 says.

**And the arithmetic collapsed to one line.** With the divide gone, the twin is `Doubled` — the
sixteen-bit vertex `XX3` already holds, times two — so there is no second projection to keep in step
and no second visibility decision anywhere. The test that replaced the two property sweeps is
stronger than either: the wide vertex equals the faithful one doubled for **all 65,536** sixteen-bit
values, and a second test pins the log divide's error so the claim above cannot rot.

**The line drawer is HALF-OPEN, and that was measured, not assumed.** `LOIN` lights
`max(|dx|, |dy|)` pixels: from (100, 50) to (104, 50) it lights four rather than five, and a line
whose ends coincide lights none. Bresenham as first written included both endpoints. The error is
nearly invisible — every extra endpoint is an exclusive-or, so at a vertex where an even number of
edges meet the two mistakes cancel — and the whole of it surfaced as ONE stray pixel at the view
centre, where a degenerate edge in the Cobra's blueprint meets an odd number of others. The shadow
test found it because it counts pixels in both directions; nothing else in the tree would have.

**One thing the diagnosis turned up that is not a defect.** At some distances a ship's coincident
edges exclusive-or each other away and the frame is blank — on both surfaces, identically. That is
the original's behaviour, and a test that read a blank frame as a broken twin would reach exactly
the wrong conclusion; `RedrawingAShipRemovesItFromBoth` now asserts the canvas has ink before it
asserts anything about the picture, and says why.

**Two things moved to RS-3.** `Project2x` goes with the planet, which needs `DVID3B` twinned anyway
and is the only caller that can justify it. The borders and the loader — which RS-1 moved here —
move again, and this is the last time: a border lives in the four-cell MARGIN, outside the view
coordinates every line primitive in this slice works in, so it needs a surface-coordinate primitive
nothing else wants and a visual judgement about its weight that only makes sense once the region
flips and somebody looks at it. Both are stated in `check_twins.py`'s `NEEDS_NO_TWIN` rather than
left silent.

**And the checker caught an imprecision in itself.** Its `DRAWS` pattern matched any `.Write(`,
which counts a `LineHeap` write as drawing — so it reported `StoreLineCountAndDraw`, which forwards
the surface and draws on both perfectly well. The pattern now checks the receiver, which is what it
always claimed to measure.

**What RS-2 does NOT do.** Nothing is shown yet. The space-view region flips at RS-3, so the ships
this slice draws go onto a surface nobody presents; the picture on screen is still the canvas
doubled. There is no screenshot to sign off, and the shadow test remains the whole of the evidence.

### RS-3 — the planet, the sun, the dust and the frame, 2026-09-07

**The upper region is native. This is the first slice a person can see.** Everything RS-1 and RS-2
drew onto a surface nobody presented is on the screen from here: the text at eighty columns, the
wireframes one pixel thick, and this slice's planet, sun, stardust, explosion particles, laser
beams, tunnel rings, charts and border. `NativeRegions::spaceView` defaults to `true` and there is
no setter in the library, because which regions are native is a fact about which slices have been
built rather than a runtime choice. Only the dashboard is still the canvas doubled, and RS-4 has it.

**THE SLICE WAS BIGGER THAN THE DESIGN SAID, AND FOR A REASON THE DESIGN CAN BE READ TWO WAYS.**
Section 10's row named §4.2 and §4.3 — the planet, the sun, the dust, the beams and the rings — and
said the region flips here. A region is native for EVERY screen that draws in it, not only for the
space view it is named after, so the flip forced everything else the upper rows carry: the border,
the rules and the colour bands (`BOX`, `BOXS`, `BOXS2`, `BLUEBANDS`); `TTX66K`'s wipe and the
loader's, without which nothing is ever erased; the cell palettes every one of those bits is
coloured through; `CLYNS`, which RS-1 had moved to RS-5; and both charts, whose twins RS-5's row
also claims. RS-5's charts row is about the RE-FLOW — the 512×256 map, the labels that stop
colliding — and that reading is the one this slice took: the charts are drawn at twice the scale
now and re-laid-out then.

**Three measurements, taken before anything was built, because RS-2 taught that lesson.**

- **`LL5` is EXACT.** The square root, swept over all 65,536 radicands, equals the exact integer
  square root every time. So a twin *may* legitimately extend it — `isqrt` at thirty-two bits is the
  same function on a bigger domain, which is what rule T2 asks for.
- **`FMLTU2` is not.** `CIRCLE2` builds every segment endpoint through the logarithm tables, and
  over all 16,384 (radius, angle) pairs it is out by up to 3 canvas pixels and by more than one in
  six percent of them. So §4.2's `radius2x * SNE[step] / 256` would draw a DIFFERENT circle — one no
  longer concentric with the crater and the meridians, which come through the same table. **The
  planet's circle is the faithful circle doubled**, and `ball2x` does not exist: the faithful heap
  already holds the endpoints, and `EraseBall` doubles them as it walks.
- **`DVID3B` is not either.** Eight significant bits of mantissa, 0.7 percent relative error on
  average, and inside `PLS6`'s own range it is out by 8 to 15 pixels in three percent of cases. So
  `Project2x` — carried from RS-2 to here — cannot exist: an exact `512x/z` would put the planet's
  centre several pixels from where the game puts the ships around it. It reduces to `Doubled`, which
  RS-2 already built.

**And the sun does not use `LL5`'s licence, which is the fourth measurement.** §4.2 had 288 hi-res
half-widths from a thirty-two-bit `isqrt`. They would be honest and they would be invisible: `SUN`
adds `DORND AND CNT` to every row's half-width, and `CNT` is 1 for a radius of 16, 3 for 40 and 7
for 96 — so any sun bigger than 32 pixels across already has one to seven pixels of deliberate noise
on its edge, and the two hi-res rows of a canvas row share one roll of that noise. Recovering half a
pixel of square root under seven pixels of RNG is not a gain a person can see. The sun's rows are
the faithful rows doubled, `sun2x` does not exist, and the sliver arithmetic — the hardest thing in
the routine — did not have to be written twice.

**THE LINE DRAWER WAS WRONG AND RS-2's SHADOW TEST COULD NOT SEE IT.** This is the slice's real
finding. `Bresenham2x` drew the exact straight line between two doubled endpoints. `LOIN` does not
draw that line: it is a DDA whose step is `LineSlope(dy, dx)`, **a third logarithm-table lookup**,
so the line the game draws wanders from the straight one over a long span. Swept over 18,432 lines,
the exact drawer put 41,252 wide pixels two canvas pixels away from the faithful line, 431 three
away and one four away — and on the 96 lines where `LOIN`'s pixel count wraps to zero and it draws
NOTHING AT ALL, it drew 510 pixels each. A ship's edges are twenty pixels long and the ship shadow
test was green throughout; a ninety-pixel laser beam is what finally showed it.

The fix is the twin running `LOIN`'s own accumulator: the same slope byte, the same seed at half a
row, the same swap, the same counts including the wrap — with the step taken per WIDE column so the
wide row is the doubled faithful row or the one beside it. All four figures above go to zero and
1,234 pixels (0.03 percent) remain two away. Those are the one thing a twin cannot have: `LOIN`
threads the carry out of its SCREEN POINTER arithmetic into the accumulator, so a line crossing a
character row advances one extra step, and a surface with its own geometry has no address to take
that carry from. It is named in `Lines2x.h` rather than left for somebody to rediscover.

**So RS-2's wide heap is gone, and with it a whole class of divergence.** Once `DrawLine2x` takes
the FAITHFUL line and doubles it itself, `Line2x`, `LineHeap2x`, `ClipLine2x`, `Doubled` and
`PushHeapLine2x` have nothing to hold: `DrawShipLines2x` reads the same four bytes `LL155` reads.
That is 13,824 bytes of state, a `Universe` field, a state-hash exclusion and one of the two things
`PushEdges` had to keep in step — deleted. **A twin that is a pure function of the faithful drawing
cannot disagree with it**, and that is worth more than any test.

**The one twin that recovers real precision is the stardust**, and it is the only one in the whole
track. A speck's position is sixteen bits an axis and `PIXEL2` throws the low byte away, so the wide
mark sits at `2 * faithful ± (fraction >> 7)` with the sign the one `PIXEL2` reads out of bit 7. The
sweep covers every `(across, down)` pair with both fraction bits both ways — 262,144 cases — and
asserts the mark is at that exact point, two pixels wide, and nowhere else.

**Two things measured about `PIXEL` that the design had not noticed.** `TWOS2`'s entry `i` lights
pixels `i - 1` and `i` (entry 0 lights 0 and 1), so the faithful mark sits to the LEFT of the point
it was given at seven x values in eight — an artefact of a table that is two bits wide because a
multicolour pixel is, on a screen that is not in that mode. The twin lights the two hi-res pixels
that ARE canvas pixel x, so a speck is centred at every x rather than at one in eight; both surfaces
always light canvas pixel x itself, which is what the shadow test compares. And `PIXEL`'s two
distance comparisons pick between one mark and one mark: only the second decides anything, so the
twin has one test where the faithful routine has two.

**`HLOIN` is pixel-exact and ADR-002 §7's "byte-granular" is about COLOUR.** Checked over all 65,536
end pairs: the masked ends land exactly on `x1` and `x2 - 1`. What the byte boundary costs is the
cell's palette changing under the line, not pixels the caller did not ask for — so the sun's rows
and the border's rules double exactly, with nothing to recover and nothing to lose.

**A rule the slice had to state because two answers were both defensible.** A MARK OR A LINE THINS:
half the size, twice the placement accuracy, which is what makes a distant star a point rather than
a smear. AN AREA DOUBLES: the border's edges, the rules and the colour bands are the frame around
the picture, and a two-pixel border at 640 across is a thread. Only what is drawn INSIDE the frame
gets finer.

**And the frame is drawn from ADDRESSES, which is why it waited two slices.** `BOXS2` exclusive-ors
a byte into eight rows of a cell, `BLUEBANDS` stores &FF over twenty-four bytes, `BOX` pokes one
byte into the bottom right corner and `TTX66K` zeroes whole pages: not one of them has an x and a y
to double. `WriteBitmapByte2x` doubles the BYTE — each bit into two pixels on each of the two wide
rows it covers — which is the only twin an address can honestly have, and it serves the border, the
bands, the wipes and the loader between them.

**The `near` trap sprang a second time and the check that catches it was pointed at one directory.**
RS-2 shipped `bool near = false` in `Tests/GameLogicTests/PictureLineTests.cpp`. `near` is a macro
in `<windows.h>`, so MSVC read the declaration as `bool = false` and the Windows job failed with
thirty errors while all eighteen checks called the tree clean — `check_gamelogic.py` has caught
exactly this since slice 3d-d-iii-b and reads `GameLogic/` alone. The rest of that scanner is about
the library being deterministic and platform-free; this rule is about the TOOLCHAIN, so it now runs
over every C++ file the Windows job compiles, with the exact declaration planted in its self-test.
AGENTS.md §6 says two CI legs rather than one.

**What RS-3 does NOT do.** The dashboard is still the canvas upscaled, so the scanner is chunky and
the dials are the original's blocks — RS-4. Nothing is re-flowed: the charts, the data screens and
the market are at their original coordinates on a screen with room for twice as much, which is RS-5
and is the first slice with a design question in it rather than a measurement.

### RS-4 — the dashboard, 2026-09-07

**Nothing on the screen is the canvas upscaled any more.** `NativeRegions::Complete()` holds from
this slice, which is the state Risk R27's tripwire was built to catch — it fired, as designed, and
the reminder it carried moves to §10's RS-6 row, which is the only slice that still owes anything to
it. `UpscaleCell` stays until RS-6 deletes it, because every test that compares the doubling asks
for it by name.

**THE LOWER REGION IS AN INDEX PLANE AND THAT CHANGES THE SHAPE OF THE EVIDENCE.** Above the raster
split a twin writes bits, and a shadow test can ask "is this pixel lit on both surfaces". Below it a
twin writes COLOURS, and a twin that put the right shape in the wrong colour would pass a shape test
and look wrong. So the slice's strongest test is an equality over the whole region: the bootstrap
resolves to exactly the canvas doubled, all 71,680 pixels of it. Everything else is drawn on top of
a picture already proved right.

**A twin has to know what colour a pattern is, and it is not the pattern's business.** `COL` holds
four two-bit codes and the codes select from the CELL — %00 the background register, %01 the high
nibble of screen RAM, %10 the low nibble, %11 colour RAM. So every twin here takes the canvas as
well as the picture and resolves the pattern through the same cell the faithful store lands in.
`Canvas::DashboardChoices` is the lift, the fourth in this track after `ResolveCell`,
`ToSpaceViewPoint` and `LineSlope`. `Striped` — the Thargoid's %01 %01 %10 %10 — is why the
resolution is per pixel rather than per pattern.

**Three of the four things ruling 1's table promised are here; the fourth was declined, measured.**

- **The BARS gain their bit.** `DIL` draws `value >> shifts` of sixteen and the twin draws
  `value >> (shifts - 1)` of thirty-two, from the same byte, swept over all 256 values at all three
  shifted entry points. The sweep also caught the exception the design had not named: `DILX` reached
  with NO shift is the four energy bars, whose value `DLL24` has already dealt out sixteen at a
  time, so there is no bit under it and the twin doubles. Both cases are asserted, not just the
  happy one.
- **The ROLL indicator gains one and the PITCH does not**, which is a distinction the design did not
  draw. `DIALS` builds the roll from `alp1 >> 2` — two bits thrown away — so `alp1 >> 1` is one of
  them back, with the centre doubled to match. The pitch's `beta` is `bet1` with its sign on, and
  `bet1` was shifted down four places in the flight loop within four instructions of the joystick:
  by the time the dial sees it the bit is game state that no longer exists.
- **The SCANNER gains two bits across.** `x_lo` is a byte the game maintains on every ship in the
  bubble and `SCAN` never reads, and `CPIX2` indexes the ALIGNED mask table so the faithful dot
  snaps to a fat pixel and loses `X1`'s bottom bit as well — four times finer, and the sweep says so
  by drawing the same ship twice with the bit both ways and requiring that the CANVAS blip did not
  move and the wide one did, over every high byte and both signs.
- **The COMPASS gains nothing, and this is the one place the building declined a bit it had
  measured.** `DVID4`'s whole part is exact division — swept over all 65,280 pairs, worst error
  zero — so a twin dividing at twice the radius would be the same function on a bigger domain and
  entirely legitimate. It may not, because `COMPAS` erases last frame's dot by drawing it again out
  of `COMX` and `COMY`, and those two bytes are the game's: a finer position would have to be
  remembered beside them, unhashed, and kept in step by hand. That is the parallel-record shape RS-3
  spent a slice deleting, and what it would buy is half a dot's width on a disc twenty canvas pixels
  across. Its FRACTION byte, incidentally, is `LL28`'s and out by up to 3 — so even the tempting
  cheap version was not available.

**And the scanner's VERTICAL fractions are named as not taken.** `z_lo` and `y_lo` are the same kind
of byte, but the blip's row is a clamped sum of two negated magnitudes in eight-bit wrapping that is
load-bearing, and reproducing it at twice the scale is a second copy of `SCAN`'s arithmetic to keep
in step for one hi-res row on a fifty-two row scale. Written down rather than left to be
rediscovered; a small follow-up with a sweep behind it if the owner wants it.

**`DASHBOARD_IMAGE_2X` does not exist and `bitmaps.py` gained nothing.** §5.3 asked for a
71,680-byte generated table holding the faithful picture upscaled, plus a tool command to produce
it. A generated table that is a pure function of a table already in the tree is a copy, and this
repository has spent three slices removing copies that two people have to keep in step. The
bootstrap is computed at the one call site instead, and — better than a golden — it is checked by an
equality against the resolved canvas. `WritePicturePng` already hands the owner the PNG to paint
over, so RS-4-art loses nothing and gains a file that has to exist rather than one that had to be
generated first.

**One twin serves the missile indicators, both bulbs and the bootstrap, and finding that out was the
slice's tidiest moment.** `MSBAR` writes a screen-RAM palette byte and the bulbs exclusive-or one:
what that does on the hardware is recolour whatever bits are already in the cell. So the honest twin
is not a fill — it is to decode that cell again and double what comes out, which is exactly
`ResolveDashboardCell2x`, which is exactly the bootstrap over one cell instead of two hundred and
eighty. The cost is named in the header: a blip drawn finely inside such a cell is flattened back to
the canvas's resolution until the next `SCAN`, which is one frame.

**A test that passed for the wrong reason, caught by reading its own failure.** The `DIALS` shadow
test reported twenty-seven orphan pixels at the right-hand end of every bar. They were not orphans:
a bar drawn at half the step ends one canvas pixel past the faithful one, and the check was growing
the window around the CANVAS pixel and then asking whether any wide pixel in it had ink — which
attributes a wide pixel to the wrong canvas column at the boundary. Walking the second direction
over WIDE pixels instead, as the space-view tests do, is the same slack asked the right way round.
The lesson is the shape of the question rather than the number.

**Two arithmetic slips in §5 corrected in passing.** A bar is three canvas rows tall and therefore
six hi-res ones; §5.1 said fourteen. And §5.2's `(x_hi << 2) | (x_lo >> 6)` reads two bits of the
fraction where the hi-res grid can only hold one — a fat pixel is four hi-res pixels wide, but a
canvas pixel is two, and `x_hi` moves in canvas pixels.

**What RS-4 does NOT do.** The artwork is still the original's, upscaled: a stepped ellipse, chunky
frame lines, the same labels. That is RS-4-art and it is the owner's, on no slice's schedule. And
nothing is re-flowed — RS-5 is the first slice with a design question in it rather than a
measurement, and its rulings ask for a sketch per screen before a table is written.

### RS-5-0 — the anchor, and two things the re-flow's mechanism could not do, 2026-09-08

**Built and green: `Anchor`, `TextLayout::anchors`, and the collision sweep.** Nothing moved on the
screen — no view has a table yet, `CENTRED_LAYOUT` and `SPACE_VIEW_LAYOUT` are what every view still
gets, and the suite is 454 green with all 18 checks passing. This slice is the mechanism §6.3's
sub-slices need and nothing else, because every one of those is gated on an accepted sketch
(ruling §11.2) and building a table before the sketch is building the wrong screen faster.

**AN ANCHOR IS A RECTANGLE, and the design's cell-to-cell version cannot be built.** §6.2 gives one
as `(fieldColumn, fieldRow) -> (wideColumn, wideRow)`, and a field is not a cell. RS-1 settled that
`Map` is a pure function of the faithful cell with nothing stored between glyphs, so by the time the
Q of "EQUIPMENT:" is printed, nothing remembers that its E was moved — a per-cell table has to name
all ten, and the eleven equipment rows of up to twenty-four characters come to some two hundred and
fifty entries kept in step by hand. A rectangle relocated to a wide origin, with a row stride of its
own, says the same thing in one table row, and the whole status re-flow is ONE anchor. The stride is
the anchor's rather than the layout's because a block that moves usually wants spreading: eleven
packed rows in the right-hand half of a fifty-row screen leave two thirds of it empty.

**A table anchored INTO the centred layout cannot exist, and the sweep said so on its first run.**
The collision test was written to protect Risk R24 and immediately failed on its own example:
`CENTRED_LAYOUT` puts a 40-column screen at column 20, so it already covers columns 20 to 59 and
every anchor target inside the wide grid is a cell the offsets reach. A re-flowed screen has to move
its offsets as well as its blocks — which is a constraint on every table §6.3 will write, found in
minutes by a property test rather than in a week by looking at a screen with two glyphs
exclusive-ored into one cell.

**`wrapWidth` was not added.** §6.2 lists it and only the data screen and the briefings need it, and
it cannot be written before the sink that re-wraps exists to read it: a field nothing reads is a
claim the code does not keep. That sub-slice adds it.

**OPEN, and it gates the first sub-slice: `QQ11` does not name a screen.** `LayoutForView` reads the
view byte, and `STATUS` and `TT213` both call `TRADEMODE` with `#8` — the status screen and the
inventory screen are one view. Two ways out, and both are the owner's: one shared table for the two
(they are the same shape — a heading, a few label lines, a list), or a layout handed to
`SetUpTradeScreen` beside the view it already takes, at the one instruction that already says which
screen is starting. The second adds a parameter to a faithful routine and no second copy of the
game's state, so there is nothing to drift; it is put with the status sketch.

**And the status screen was MEASURED before it was sketched.** §6.3's draft sketch had every label
in capitals with the colons in one column, and the screen prints neither — only the title and
"EQUIPMENT:" are capitals, the first three labels tab to column 21 and the last four carry their
colon inline. The strings in §6.3 are now the ones a fully-fitted commander's screen actually
prints, read off the port rather than remembered. The design's own line is that every string in a
sketch is the string the faithful screen prints; a sketch drawn from memory does not meet it.

### RS-5-a — the status screen, the first re-flow, 2026-09-08

**Sketch accepted by the owner and built to.** §6.3 carries the rendered grid rather than the draft
drawing: it is `STATUS` run through `STATUS_LAYOUT` and printed, so what the design shows and what
the screen does cannot differ. `STATUS_LAYOUT{4, 4, 2}` with two anchors — the title to wide (24, 3)
and canvas rows 12 to 23 to wide (46, 12) at stride 2 — is the whole re-flow. `StatusScreen.cpp`
gained one argument at one call and nothing else; the character stream the seventeen fixtures
compare is untouched.

**THE LAYOUT IS THE CALLER'S, because `QQ11` does not name a screen.** Ruled by the owner from the
two choices RS-5-0 put. `SetUpScreen` and `SetUpTradeScreen` each gained a four-argument form; the
layout is stored in `Universe::screenLayout` in the same breath as `QQ11`, and `TextPrinter` reads
it through the pointer that used to point at the view. `ClearMessageRows` takes a layout rather than
a view byte for the same reason. The field is not folded into the state hash, for `picture`'s
reason, and the replay digest is unchanged.

**Two overloads and not a default argument**, because the default is `LayoutForView(_view)` and C++
cannot write one parameter's default in terms of another. Every screen without a table of its own
still gets exactly what it got before, which is why 454 of the 469 tests did not move.

**The title is on wide row 3 and not 4, and a rule nobody draws is why.** `NLIN3`'s rule is at canvas
row 19, so its twin is a wide line at row 38 — inside the glyphs of wide row 4, which spans 32 to
39. Row 3 puts the title above it. Nothing draws that rule today: `NLIN3`'s is "the canvas's, so a
caller draws it" and only the two charts have a caller that does. That gap is older than this track
and is not a re-flow's to close, but a layout that ignored it would have to be redrawn the day it is.

**The shadow test needed a BASELINE, which is new.** `TheGlyphsAgree` assumed every inked canvas
cell was a glyph, which holds for a fixture that drives a printer and fails on cell (0, 0) of any
real screen: the border and the rules are inked on both surfaces by twins that work in wide
coordinates and not through a layout. So the test draws the frame first, keeps both surfaces, and
compares only what changed. Every screen re-flowed after this one wants the same shape.

### RS-5-b — the three trade screens, 2026-09-08

**Sketches accepted and built to: the market list, the inventory, the equipment shop.** Three tables,
seventeen anchors between them, no twin and no change to any screen routine beyond the argument each
now passes at the `TRADEMODE` call it already made. §6.3 carries each table's numbers.

**THE MARKET SCREEN'S HEADING IS TWO FAITHFUL ROWS ON ONE WIDE ROW, and that is the first thing in
this track that 80 columns buys outright.** The game splits its header because "UNIT PRICE" and
"QUANTITY FOR SALE" do not fit over their columns in forty: canvas row 1 carries "UNIT" and
"QUANTITY", canvas row 2 carries "PRODUCT UNIT PRICE FOR SALE". Six anchors interleave them on one
wide row so each phrase lands over the column it heads. Not a word is new — the tokens and their
order are the game's, and only the cells are the table's — which is exactly the line §6.3 draws.

**The equipment shop's fourth anchor is a new KIND of reason to move something.** `EQSHP` asks its
question on `CLYNS`'s row 21, which is "just under the text" on a 25-row screen and is nowhere near
anything on a 50-row one: the offsets put it on wide row 43, thirty rows below a list that ends at
34. Every anchor before this one moved a block because the screen is WIDER; this one moves it
because the screen is also TALLER, and a row number chosen for the bottom of a short screen means
nothing on a long one. Every screen with a `CLYNS` prompt will want the same anchor.

**THE SELL SCREEN IS BLOCKED, and the block is older than this track.** `TT208`'s head — the
`TRADEMODE` and the "SELL CARGO" title — is not in the port. `KeyAction::SellCargo` calls
`ListCargo` directly, so the sell screen never sets its view, never clears the screen and has no
instruction at which to name a layout; it inherits whatever the previous screen left, on both
surfaces, and always has. Porting that head is a change to the character stream seventeen fixtures
compare, which is not a re-flow's to make. Recorded in §6.3 as its own row rather than left inside
the "buy / sell / inventory" one, because the three are not one screen and only two of them could be
built.

**The screen tests share a `Docked` rig now**, which is what three more of them made worth having:
a universe with both surfaces wired as `Game` wires them, and a `FillTheHold` for the cargo lists.
The status screen's test moved onto it in the same change.

### RS-5-c — `wrapWidth` declined, measured, 2026-09-08

**The one thing §6.3 asked for that is not a table cannot be built, and the reason is padding rather
than breaking.** The design has the wide sink re-wrapping the data screen and the briefings at 64
columns, treating a break the justifier made as soft. But `DA11` does not merely break a justified
line — it WIDENS THE GAPS until the thirtieth character is a space, so the stream the canvas receives
already carries the padding for a thirty-column measure. Re-flowing it to 64 without re-justifying
carries that padding into a longer line and prints text with holes in it; re-justifying means the
picture emitting a different number of SPACE characters from the canvas, which breaks the invariant
`TheGlyphsAgree` and the whole of §8.1 rest on — and breaks it precisely on the two screens whose
text is longest and least checkable by eye.

**So the field is deleted from the design rather than deferred again.** RS-5-0 left it out because
nothing read it; this slice says nothing ever will. The description gets a COLUMN instead of a
WIDTH: thirty characters is a good measure, and eighty columns is enough to stand the paragraph
beside the label/value pairs rather than under them.

**And §6.3's "label/value pairs at columns 4 and 28" is not expressible either.** `TT25` prints
"Economy:Poor Industrial" and "Gross Productivity:11520 M CR" with the colon at canvas column 8 in
one and 19 in the other, so no rectangle splits label from value across the rows: an anchor moves a
column RANGE, and there is no range that is "the label" on every line. The pairs stay whole on the
left, which is what the sketch shows.

**Third time this track has declined something the design promised, and the shape is always the
same**: `Divide512` and the sun's twin at RS-3, the compass bit at RS-4, `wrapWidth` here. Each was a
real improvement, each was measured, and each cost more evidence than it bought. Written down so the
next one is recognised faster.

### RS-5-d — the data screen, and the shear that three tests could not see, 2026-09-08

**The data screen landed to its accepted sketch** — `DATA_LAYOUT{1, 8, 1}`, the pairs whole on the
left, the justified description in a column of its own beside them. It is the only table with
`rowStride = 1`, because `TT25` double-spaces itself and a stride of 2 would space it four wide rows
apart.

**AND RS-5-b's TABLES WERE WRONG ON THE SCREEN, which this slice found and fixed.** An anchor covers
a range of CANVAS columns, and the ranges had been read off a dump of the printer's `XC` — which is
four cells to the left of the canvas cell a glyph actually lands on. So the market screen's item
names and the equipment shop's item lines were SHEARED: "Radioacti   ve  s", "Large Cargo   Bay",
half of each word at the anchor's column and half at the offsets'.

**Three green tests said nothing, and that is the finding.** A sheared mapping is still injective;
it still puts every canvas glyph on the picture; it still leaves nothing on the picture the canvas
lacks. All three clauses of §8.1 hold on a screen that is unreadable, because every clause is about
cells and none is about WORDS. §8.1 gains a fourth: two canvas cells side by side, both with ink,
must land side by side. Planted against the original mistake it fails in one line and names the
cells.

**The sketch was supposed to be the check, and the renderer lied to it.** Ruling §11.2 puts an ASCII
sketch in front of the owner precisely so that a person sees the screen before the table is written
— and the sketch harness plotted `XC` where it should have plotted `XC + 4`, so the pictures shown
for RS-5-a and RS-5-b were four columns left of the truth and hid the shear. The harness reads the
canvas now. A gate is only as good as the instrument behind it, and this one was measuring the wrong
thing for two slices.

**Everything is re-rendered and re-accepted against the corrected instrument**: the status screen's
offsets go from 4 to 0 with its anchors from (24, 46) to (20, 42), the three trade tables likewise,
and every screen now lands exactly where its sketch showed.

### RS-5-e — the two charts, and the label a layout cannot place, 2026-09-08

**NEITHER MAP MOVES, and §6.3's translation was work that did not need doing.** The design has the
long-range map re-laid at 512×256 from cell (8, 4). It is already there: `ToSpaceViewPoint` supplies
the space view's 64-pixel margin to every 2× drawing, so the doubled map runs from wide cell 8 and
the rules with it. Nothing needed an offset, no twin needed a parameter, and the thing that was
actually wrong was invisible in the design — the chart's TEXT was still on the centred layout, which
put "GALACTIC CHART 1" at wide row 13, in the middle of its own map. A table fixes it.

**THE SHORT-RANGE CHART NEEDED A MECHANISM, AND `columnStride` WAS NOT IT.** `TT23` puts a name
beside its disc by dividing the disc's x by eight; the discs are at twice their coordinates, so the
name's origin must double. The obvious field — a column stride to mirror `rowStride` — was built,
and it prints `O r r e r e`: `Map` runs per GLYPH, so a stride of 2 doubles the gaps between letters
as well as the distance to the origin. **A label is a RUN whose start moves and whose letters do
not**, and no pure function of a cell can express that, which is exactly what §6.2 predicted when it
said the stored cursor was really for RS-5.

So `TextPrinter::SetLabelRun` is that cursor, in its smallest honest form: the row, the first canvas
column, and the wide column, set by `TT23` at the instruction that already places `XC`, and cleared
by the first control code — the newline after the name. It lives in the PRINTER and not the
universe, because a `Universe` is copied and hashed field by field and a cursor that lasts one name
belongs in neither.

**"Labels no longer collide" is DECLINED, and this one was measured before it was argued.** `TT23`
gives a name the row it wants, else the row below, else the row above, and drops it if all three are
taken. Over all 256 charts of galaxy one: 2,668 systems in range, **142 names lost — 5.3%, or 0.4
names on an average chart**, with the worst chart losing four; a fifty-row test would recover about
a hundred of them across all 256. Against that: the twin would be DECIDING which systems are named,
which is rule T1's line; the picture would carry ink the canvas has not, which §8.1's third clause
forbids; and `TT23` feeds "was it named" back into the disc's SIZE through the carry `cpl` leaves,
so a differently-named chart is a differently-DRAWN one. Half a name a chart is not worth any of it.

**Fourth decline of the track**, after `Divide512`, the compass bit and `wrapWidth` — and the first
where the measurement was of the GAME rather than of the arithmetic.

### RS-5-f — the frame, which made four green screens red, 2026-09-08

**`main` moved under this branch, and the merge was where the constraint appeared.** M6-a's work
gave `TTX66K` the picture, so a screen change now wipes the wide surface and draws the border on it
— which is right, and is rule T3 applied to a routine this track had not twinned. It also means the
wide surface has a FRAME on it for the first time, and four of the five re-flowed screens were
drawing text underneath it.

**The interior is 8 to 71, measured.** The border fills wide columns 0 to 6 and 73 to 79 with colour
band and puts its vertical rules on 7 and 72. Sixty-four columns — exactly twice the thirty-two the
canvas gives `CHPR`, which is the arithmetic that makes the two-column screens work at all: one
column of text at `columnOffset = 4` occupies 8 to 39 and leaves 40 to 71 for a second, which is why
every two-column table here anchors its right-hand block to wide column 36.

**Every table moved right by four and every right-hand block moved left by six**, and the design's
numbers in §6.3 with them. Nothing about the arrangement changed; the screens are the ones the owner
accepted, inside the frame instead of over it.

**§8.1 gains a fifth clause and the third loses eight columns.** The new one is that no table may put
a glyph outside 8..71, checked over every layout in the tree — the property that would have caught
this the moment the border landed rather than four tests later. The collision sweep now runs over the
32 cells `CHPR` can write rather than all 40, because the eight margin cells carry the frame and can
never hold a glyph: sweeping them turned two-column tables into an impossible packing — two
40-column images do not fit in a 64-column interior — for cells that cannot collide.

**The lesson is the same one RS-5-d taught, one level up.** That slice found a test that could not
see a sheared word; this one found a test that could not see a screen printed under its own border.
Both times the missing clause was about something the shadow test's cell-by-cell view has no word
for, and both times the fix was to say the property out loud.
