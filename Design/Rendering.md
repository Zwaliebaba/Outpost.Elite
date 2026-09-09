# Rendering — erase-by-exclusive-or, and what it would take to render frames instead

**Status:** analysis, opened 2026-09-08, from an owner question: *"today some of the rendering takes
place through exclusive-or with the old and the new model; I would like to move to a normal
rendering setup with multiple backbuffers and regular rendering — what is needed?"* **The first of
its four rulings was taken the same day: §4's option A — the picture becomes a rendered frame, and
the canvas and the game are not touched.** §5's option B is declined, and it is
rewritten to decline on the sequencing finding that arrived *with* the ruling rather than on the one
this document opened with, because the two point opposite ways and the first one was wrong.
**All four of its rulings are taken (§9)** and the track is three slices, RN-0 to RN-2: the frame
boundary and nothing past it, delivering a picture pixel-identical to today's. **RN-0 is part-built
as of 2026-09-09 — its GATE is in and its SPLIT is not** (§12), and RN-1 and RN-2 are unstarted.
Three sites need an owner ruling before the split can finish, and §12.3 names them.

**REVALIDATED against `a827b97` on 2026-09-09, after M6 closed and the original left the tree**, and
the revalidation moved three things: R-1 (the clear now has one caller, and it is a test),
**R-4 (the twins-absent replay this document leaned on IS NOT BUILT)**, and the whole of §5.2, whose
argument was about a fixture that was never recorded. §11 is the log of that pass and says which
claims held. Every number in §1 held unchanged.

It reads after [Resolution.md](Resolution.md) and [ADR-008](ADR/ADR-008-the-picture.md), because the
answer turns entirely on a clause ADR-008 already wrote (§4: the picture is not in the state hash),
and after [ADR-009](ADR/ADR-009-detachment.md), which is now what says what pins the port at all.

**What was read.** `Outpost/ScreenPresenter.cpp` and `.h`, `Presentation.cpp`, `CanvasPixelShader.hlsl`;
`GameLogic/Canvas.h`, `Canvas.cpp`, `Picture.h`, `Picture.cpp`, `LineHeap.h`, `HeapOffset.h`,
`Lines.cpp`, `Lines2x.h`, `ShipDraw.h`, `ShipDraw2x.h`, `Dashboard.cpp`, `Dashboard2x.h`,
`TextPrint.h`, `TextPrint2x.h`, `Scanner.h`, `Stardust.h`, `PlanetDraw.h`, `Explosion.cpp`,
`Lasers.h`, `Messages.h`, `Charts.h`, `Universe.h`, `StateHash.cpp`, `FlightLoop.cpp`,
`GameLoop.cpp`, `ShipSlot.h`, `ShipSlot.cpp`; `Tests/GameLogicTests/FlightReplayTests.cpp`,
`Picture*Tests.cpp`; `tools/check_twins.py`, `tools/mutants.json`; ADR-002 §4 and §7, ADR-005 §1,
ADR-007, ADR-008 whole, **ADR-009 whole**, Resolution.md §0, §1, §3, §4 and §8.

**On the tree it was read against.** The document opened against `fa862ea`, when the 6502 oracle,
`MasterFile/`, `Upstream/` and 4,103 `// 6502:` markers were still here; it was revalidated against
`a827b97`, where none of them are (ADR-009). **Where it quotes an original label — `LOIN`, `NWSHP`,
`EE51` — that is now prose and not a marker**: M6-e took the markers to zero and the owner's ruling
R-b kept the names in the commentary. Every count is named with the command that took it, so a
reader can disagree with the number rather than with the prose.

**Depends on:** ADR-002 §4 and §7 (why both surfaces are bit planes), ADR-005 §1 (the presenter),
ADR-007 (state ownership), ADR-008 §2 and §4 (the twin rule, and the exclusion that makes this
possible at all), **ADR-009 §2 (what pins behaviour now — which is what §4.1 turns on since the
revalidation, ADR-003 having been amended out of that role)**.
**Feeds:** ADR-008 (amended at RN-2 — T3 goes, and §3's fifth clause needs the correction §11
found), the risk register, the plan's Phase 6 row.

---

## 0. The question, answered first

Three answers, and the order matters because the first two change what the third one is asking.

**The multiple backbuffers already exist, and they are not where the exclusive-or is.**
`Outpost/ScreenPresenter.cpp` is a conventional Direct3D 12 presenter: a `FLIP_DISCARD` swap chain
with `FRAME_COUNT = 2` back buffers (`ScreenPresenter.h:105`, used at `ScreenPresenter.cpp:131`),
per-frame command allocators, upload heaps and fence values (`ScreenPresenter.h:119`–`123`), a
`ClearRenderTargetView` every frame, and one `Present(1, 0)` that is the only place the program
waits (`ScreenPresenter.cpp:475`). Going to three buffers is one constant. **Nothing about the
presentation layer is blocking anything**, and a slice that only changed it would change nothing a
player could see.

**The exclusive-or is not in the renderer. It is game state, and some of it is load-bearing on the
random number generator.** The game has no display list and no frame: a thing comes off the screen
by being drawn a second time, and the bytes that say what to draw a second time are ordinary game
bytes that the replay digest folds. §1.4 lists them. The sharpest instance is not a pixel at all:
`SeedExplosionCloud` rolls its first `DORND` on the carry that `EraseShip` returned, so **erase
order reaches the generator** — though R-8 narrows how, and narrows it in a way that matters.

**So the question is not whether to render normally, but which of the two surfaces moves — and only
one of them can.** The canvas is the surface the port's whole method was built to keep honest, and
since M6 it is folded into the replay digests that are now the port's primary instrument (ADR-009
§2); the picture is the surface ADR-008 §4 deliberately **excludes** from the state hash, drawn by
twins that hold const references to everything they read. **That exclusion is the whole of the
freedom this document has to spend**, and it was written for a different reason. §4 is the option it
buys; §5 is the option it does not.

**Revalidation note.** This paragraph originally claimed a second proof beside the exclusion — the
replay run with the twins present and absent for the same digest, which ADR-008 §3 asserts. **That
run does not exist** (§11, R-4). The freedom is real and rests on the exclusion plus the twins'
const-ness; it does not rest on a measurement, and RN-1 is where that is fixed.

---

## 1. What the tree does today

### 1.1 There is no frame boundary anywhere in the program (finding R-1)

`Canvas::Clear()` is declared at `Canvas.h:113` and defined at `Canvas.cpp:111`. `Picture::Clear()`
is declared at `Picture.h:203` and defined at `Picture.cpp:41`. **Neither is called by the
program.** `grep -rn "Clear()"` over `GameLogic/`, `Outpost/` and `Tests/GameLogicTests/` finds the
two definitions, the two declarations, `SoundEffects.h:65`'s unrelated one — and, at `a827b97`,
**one caller: `Tests/GameLogicTests/VideoStateTests.cpp:54`**, a test setting up a blank canvas.
`Picture::Clear` still has none at all.

**CORRECTED 2026-09-09.** The finding opened as "neither has a single caller", measured at `fa862ea`
where that was exactly true. It is now one caller, in a test, and the distinction is worth keeping
rather than smoothing: *nothing in the shipped program clears either surface* is the claim the rest
of this document rests on, and a test that clears one before using it does not touch it. The
original wording would have been falsified by a passing test, which is the wrong way for a finding
to die.

Both surfaces are therefore permanently persistent, from the first frame the program draws to the
last. What wipes a screen is never a surface clear: it is the game's own regional routines writing
zeros — `TT66simp` (`TextPrint.h:122`), `CLYNS` (`TextPrint.h:164`), `TT66`/`TTX66`
(`ViewChange.h:226`) — and everything else comes off by being drawn again.

This is the finding the rest of the document hangs on. **"Regular rendering" is, first and before
any representation changes, the introduction of a frame boundary that does not currently exist**,
and every difficulty in §4 is a consequence of adding one to a program written without it.

### 1.2 The exclusive-or sites, counted

`grep -rn "ExclusiveOr" GameLogic/*.cpp` is **20 call sites**, and they are narrower than the
architecture around them suggests:

| File | Sites | What |
|---|---|---|
| `Lines.cpp` | 11 | `LOIN`, `HLOIN`, `PIXEL`, `PIXEL2`, `CPIX2`, `CPIX4` — the whole pixel and line primitive layer |
| `Dashboard.cpp` | 4 | the bulbs, which toggle a *palette* in and out of screen RAM rather than a bitmap byte |
| `Scanner.cpp` | 2 | the blip and the compass dot |
| `TextPrint.cpp` | 1 | the glyph, which is how a message printed twice erases itself |
| `Dashboard2x.cpp`, `TextPrint2x.cpp` | 1 each | the twins of the two above |

The dials themselves are **not** in this list, and `Dashboard.cpp:92` says why: a bar is a store and
not an exclusive-or, "which is why the dashboard needs no erase and the space view does". That
matters for §4.5 — most of the dashboard is already written absolutely and is already, in effect,
rendered rather than toggled.

### 1.3 The twin layer, counted

`grep -rn "Picture\* _picture\|Picture& _picture" GameLogic/*.h` is **90 entry points across 16
headers** (`Charts`, `Dashboard`, `Dashboard2x`, `Explosion`, `Lasers`, `Lines2x`, `LoaderScreen`,
`Messages`, `PlanetDraw`, `Scanner`, `ShipDraw`, `ShipDraw2x`, `Stardust`, `TextPrint`,
`TextPrint2x`, `ViewChange`). Behind them are **12 `/// 2x of:` twins** in four files, and
`tools/check_twins.py` names **16 twinned translation units** from RS-1 to RS-4.

Ninety is the number a track would have to touch. Twelve is the number that would actually have to
be *rewritten*, because the other seventy-eight are call sites that pass the surface through.

### 1.4 The state that exists only so that things can be erased

| State | Where | Bytes | What it is for |
|---|---|---|---|
| `LineHeap` | `LineHeap.h`, `Universe::heap` | 1,728 (`0xFFC0 - 0xF900`) | last frame's lines for every ship, with `SLSP`, `XX19`, and `NWSHP`/`KILLSHP` carving and shuffling runs |
| the lent sun heap | `LineHeap::AttachSunHeap`, window at `&0580` | 200 | the station borrowing `LSO` because `NWSPS` evicted the sun first |
| `PlanetSunState` | `Universe::heaps` | — | `LSO`, `LSX2`, `LSY2`, `LSP` — the planet's and sun's own line records |
| `ShipFlags::Drawn` (bit 3) | `ShipFlags.h` | 1 bit/ship | "the lines on the heap are currently on the screen" — the whole of `EE51`'s state |
| `ShipFlags::OnScanner` (bit 4) | `ShipFlags.h` | 1 bit/ship | the same, for the blip |
| `COMX` / `COMY` | `Universe::compass` | 2 | last frame's compass dot, so `COMPAS` can draw it again first (`Scanner.h:65`, `:210`) |
| `MCH` / `DLY` | `Universe::message` | 2 | the token on screen, so `me1` can re-print it to rub it out (`TextPrint.h:57`, `Messages.h:41`) |
| `viewLaser` / `laserCount` | `Universe::status` | 2 | the beam on screen and its countdown (`FlightLoop.cpp:1374`–`1383`) |
| the stardust's prior positions | `Universe::dust` | — | shared with the movement, but the erase is what forces the ordering (`Stardust.h:97`, `:133`) |

**And the coupling that is not a byte.** `SeedExplosionCloud` builds the explosion cloud *inside*
the ship's line heap and rolls its first `DORND` on `EraseShip`'s exit carry (`ShipDraw.h`, §6.157).
That seam was empty until 2026-09-06 and the defect it caused — two hundred and fifty bytes of
`XX3` written over every heap above the dying ship — is recorded there. It is the concrete reason §5
is not a rendering change.

### 1.5 What redraws every frame, and what does not (finding R-2)

This is the difficulty §4.5 has to solve, and it is worth stating before the options.

- **The space view redraws every frame.** Ships (`FlightLoop.cpp:1341`, "the same call that erases
  the last frame's ship"), the stardust (`:1419`), the planet and sun (`:1513`), the laser beam
  (`:1382`), the explosions.
- **The dials redraw every frame** — but only on the space view. `GameLoop.cpp:118`–`122`: "`DIALS`
  on every pass of the space view, which is what makes the speed, roll and pitch indicators move at
  all." A docked pass does not call it.
- **The dashboard's artwork does not redraw at all.** It is copied in once when the view changes
  (`ViewChange.cpp:216`, `CopyPagesDown` / `CopyDashboardPicture2x`) and then persists.
- **Every docked screen does not redraw at all.** `STATUS`, the market, the charts, a briefing: the
  screen routine runs once on the view change, prints its glyphs, and the picture stays until
  something wipes it. There is no per-frame re-emission of a docked screen anywhere in the tree, and
  no routine that could produce one — the screens are written as straight-line printing, not as
  functions of state that could be called again.

A naive "clear and redraw each frame" would therefore blank every docked screen and the dashboard
artwork on the first frame after it landed. **This, and not the display list, is the hard part.**

---

## 2. Findings

| # | Finding | Where |
|---|---|---|
| **R-1** | There is no frame boundary: `Canvas::Clear` and `Picture::Clear` have no callers at all | §1.1 |
| **R-2** | Docked screens and the dashboard artwork are drawn once per view change and never re-emitted; only the space view and the dials are per-frame | §1.5 |
| **R-3** | Erase order feeds the RNG through `SeedExplosionCloud`'s first `DORND`, so the faithful erase is behaviour and not presentation | §1.4, `ShipDraw.h` §6.157 |
| **R-4** | The picture is excluded from the state hash — verified at `StateHash.cpp:397`, where the comment beside the skipped fold is the record of it. **CORRECTED 2026-09-09, then CLOSED the same day.** ADR-008 §3 and Resolution.md §8.4 both claimed the replay runs "with the twins present and absent" for the same digest, and no such run existed: `ELITE_SCREEN_SHADOW` appeared nowhere outside Resolution.md's own §8.4 and `FlightReplayTests.cpp` ran its three digests once. **T1 was asserted, not proved, from RS-0 for a month.** It is proved now — `TheReplayIsTheSameWithNoTwins`, all three digests unmoved with the twins switched off — so §4's licence rests on a measurement as well as on the exclusion, which is what this document assumed on the day it opened | `GameLogic/StateHash.cpp:397`; `FlightReplayTests.cpp`; ADR-008 §3, Resolution.md §8.4 |
| **R-5** | ADR-008's own T3 ("every erase has a twin erase") is recorded there as the one rule with no cheap test — a rendered picture deletes the rule rather than testing it | ADR-008 §2 |
| **R-6** | The picture's evidence is byte-shaped: ADR-008 §3 clause 1's shadow tests assert the canvas's glyph bytes appear at the mapped wide cell. A picture that is no longer a bit plane cannot satisfy that clause as written | ADR-008 §3, 2,741 lines across five `Picture*Tests.cpp` |
| **R-7** | Most of the dashboard is already written absolutely rather than exclusive-ored, so the dashboard is the cheapest region to move and the space view the dearest | `Dashboard.cpp:92`, §1.2 |

Three more arrived on 2026-09-08 with the owner's question about option B, and they are recorded
here rather than in §5 because they are facts about the tree and outlive the option that prompted
them. **R-8 corrects R-3**, which was written from a header comment rather than from the body.

| # | Finding | Where |
|---|---|---|
| **R-8** | **The RNG coupling is metadata, not drawing.** The carry `EraseShip` returns is `_heap.Read(_ship.heap) >= 4u` and nothing else — the line replay contributes no part of it. So the erase *draw* can be removed with the generator's stream bit-identical, provided the count byte and the `OnScreen` bit stay | `ShipDraw.cpp:213`–`218`, and it narrows R-3 |
| **R-9** | **The line heap is a spawn limiter, so its allocation is gameplay.** `NWSHP` refuses a ship when the reservation would run down into the block it is about to write — "a bubble full of Anacondas runs out of heap before it runs out of slots, so this is reachable rather than defensive". Delete the arena and the bubble accepts ships it used to refuse | `ShipSlot.h` on `AddShip`, `ShipSlot.cpp`'s `TryReserveHeap` |
| **R-10** | **Two of the "exclusive-or" routines are not.** `SUN` is a *differential* renderer — it holds last frame's half-width per row and draws only the two pieces that differ, which is what `LSO` and `SUNX` are for — and the hyperspace rings erase by rewinding `LSP` to 1 so the next circle overwrites the same run. Removing erasure does not simplify either; it replaces them | `PlanetDraw.h:437`–`448`, `:307` |

---

## 3. What "regular rendering" means here, stated before it is planned

Three separable changes travel under the one phrase, and they have very different costs. A ruling
that says "yes" to the sentence without separating them buys the dearest one by accident.

1. **A frame boundary** — begin frame, draw everything that should be visible, end frame. Removes
   the need for an erase at all. This is R-1 and it is the whole of the benefit.
2. **A display list** — draw calls captured as data (line, run, dot, glyph, blip) and replayed,
   instead of writing bits at the call site. This is what buys per-primitive colour, line width and
   any future antialiasing, and it is what costs R-6.
3. **More than one surface** — a background buffer holding what persists and a frame buffer built
   from it each frame. This is what the owner's phrase "multiple backbuffers" names, and §4.5 argues
   it is the *answer to* R-2 rather than a separate wish.

**They can be taken in that order and each is independently useful.** 1 without 2 is already a
program with frames. 3 without 2 is already a program that can clear and redraw without losing the
docked screens.

---

## 4. Option A — the picture becomes a rendered frame, the canvas does not

### 4.1 Why the picture can move and the canvas cannot

Because ADR-008 §4 already said so, for its own reasons. `Universe::picture` and
`Universe::screenLayout` are the two fields `Elite::HashState` deliberately walks past, on the
ground that the picture is "a second rendering of a frame the canvas already holds, produced by code
the resolution slices kept changing" — and `StateHash.cpp:397` carries that record beside the fold
it skips, where the revalidation of 2026-09-09 confirmed it still does.

**So nothing downstream of the game depends on how the picture is drawn**, and after M6 that
sentence has a shorter proof than it did: ADR-009 §2 lists what pins the port, and the picture
appears there only as its own layer — 53 tests and `check_twins.py`, judging the picture against
itself. It reaches the replay digests, the state-hash cell walk and the mutant floor nowhere,
because the fold skips it. The canvas has the opposite property by design, and since M6 it is the
replay digests rather than an oracle that hold it.

**What this argument turned out not to have, and now has again** (§11.3): a *measurement* that the
twins consume nothing the game notices. ADR-008 §3 claimed one; it was not built, from RS-0 for a
month. It was built on 2026-09-09 — `TheReplayIsTheSameWithNoTwins` — and the three replay digests
are identical with the twins switched off. So the argument stands on both legs: the construction (a
twin takes the surface by pointer and everything else by const reference) and the measurement that
was always supposed to check the construction rather than restate it.

So: the canvas keeps its twenty exclusive-or sites, its line heap, its `Drawn` bit and its
explosion carry, unchanged and still folded into the replay. The picture stops being a
VIC-II-shaped bit plane and becomes a frame.

### 4.2 The frame boundary

Two call sites, and they are the smallest part of the work: one in the flight loop's tail and one in
the docked loop, both in `GameLoop.cpp` where `RunLoopTail` already distinguishes the space view
from a docked pass. `Picture::Clear()` finally acquires the caller it has never had.

### 4.3 The display list

The primitive set is small, which is the encouraging part of the estimate. Read off the twelve
`/// 2x of:` twins, it is: a line; a horizontal run; a distance-graded dot (`PIXEL`'s three sizes);
an 8×8 glyph at a wide cell; a filled cell; a dashboard bar; an indicator block; a scanner blip with
its stick; the compass mark. Nine kinds, plus a colour.

The twins stop calling `Picture::PlotPoint` and friends and start appending to a list; a rasterizer
replays it. **T1 is unaffected** — the faithful routine still decides what, when, in what colour and
whether at all, and the twin still only computes where. T2 is unaffected. **T3 is deleted**, which
is R-5: the rule ADR-008 admits it cannot cheaply test stops needing to be true.

### 4.4 The erases that become drops

Every one of these becomes a no-op on the picture side and stays exactly as it is on the canvas
side: the picture arm of `EraseShip`, `EraseSun`, `EraseBall`, `ErasePlanetOrSun`; the stardust's
erase (`Stardust.h:97`); `COMPAS`'s first `DOT` (`Scanner.h:210`); `me1`'s re-print
(`Messages.h:41`); `TT103`'s first crosshair (`Charts.cpp:209`); the laser beam's second
`DrawLaserLines` (`FlightLoop.cpp:1382`).

`tools/check_twins.py` rule 2 currently requires a canvas-drawing function in a twinned file to call
a twin. An erase whose twin is deliberately absent is a new category and the script needs a fourth
data table for it — `ERASE_NEEDS_NO_TWIN`, beside the existing `NEEDS_NO_TWIN`, kept as data with a
reason per entry in the style that file already uses.

### 4.5 The persistent regions, which are the actual problem — and the owner's own answer

R-2 says a clear-and-redraw blanks every docked screen and the dashboard artwork, because nothing
in the tree can re-emit them.

**The answer is the second buffer the question asked for.** Keep two pictures:

- a **background** picture, written the way the tree writes today — once, when the view changes, by
  the docked screen routines and the dashboard copy — and never cleared per frame;
- a **frame** picture, refreshed from the background at the top of every frame, drawn on by
  everything per-frame, and presented.

"Refreshed from" is a `memcpy` of the bitmap plane and the cell palettes, which is **36,000 bytes**
(32,000 + 80×50) — not the whole 107,682, because the dashboard's index plane is 71,680 of it and is
almost entirely static; only the bars, blips and compass move, and those can be re-emitted onto the
static plane cheaply or given a small dirty-rectangle list. On any machine that runs this at all,
36 KB a frame at 60 Hz is 2.1 MB/s and is not worth measuring twice.

The cost is one more `Picture` in `Universe`, which ADR-008 §1 already stated as 107,682 bytes and
which this would raise. **That number is the one a ruling has to accept**, and §9.3 puts it. If it
is refused, the alternative is making every docked screen routine re-entrant so it can be called
each frame — which is a rewrite of a dozen straight-line printers, and which risks exactly the
"deciding" that T1 forbids.

### 4.6 What the presenter does then

Two honest choices, and the cheap one is genuinely cheap:

- **Keep the upload path.** The rasterizer runs on the CPU into the same 640×400 plane of colour
  indices `Picture::Resolve` produces today, and `ScreenPresenter` does not change by a line — same
  `R8_UINT` texture, same sixteen-entry palette in root constants, same triangle. Everything in §4.3
  is then a `GameLogic` change, testable under the PortableRunner with no GPU. **This is the right
  first target** and it keeps the change inside the project that has the tests.
- **Draw geometry.** The list goes to the GPU as vertices, which buys resolution independence and
  antialiasing and costs a real renderer in `Outpost/` with none of the determinism the library has.
  Worth wanting; not worth coupling to this.

### 4.7 What it costs in evidence

R-6 is the bill. ADR-008 §3 lists five kinds of evidence and clause 1 — every glyph the canvas has,
the picture has at the mapped cell *with the same bytes* — is stated in bytes because the picture is
a bit plane. Under a display list it becomes "the frame's list carries a glyph of that character at
that cell", which is a better test of the same thing but is not the same test: five files and 2,741
lines of `Picture*Tests.cpp` are rewritten, not adjusted. Clauses 2 to 5 survive unchanged; they are
about mappings and arithmetic, not representation.

Set against it: **T3 stops needing evidence at all**, the shadow tests get easier to write against a
list than against a plane, and the replay digest is untouched throughout because of R-4.

---

## 5. Option B — take the exclusive-or out of the game itself

**DECLINED by owner ruling, 2026-09-08**, in favour of §4. Kept whole rather than cut, because the
three findings that came out of examining it (R-8, R-9, R-10) are the sharpest description this
corpus has of what erase-by-redraw actually is, and because the reason it is declined **changed
between the question and the ruling** — the version below is the correct one and §5's first version
was wrong in the direction that matters.

### 5.1 It is three changes, and only one of them is rendering

The phrase bundles three things that have nothing to do with each other, and the bundling is what
made the option look monolithic:

| | Change | What it really is |
|---|---|---|
| 1 | The erase **drawing** — the `DrawShipLines` call inside `EraseShip`, and its equals in `EraseSun`, `EraseBall`, the stardust, `COMPAS`'s first `DOT` | **Presentation.** By R-8, removable with the RNG stream bit-identical |
| 2 | The heap **bytes** — the four-per-line records that say what to redraw | Presentation, *except* that the explosion cloud uses the same run as storage and `DOEXP` ages a counter in byte 1 |
| 3 | The heap **allocation** — `SLSP`, `TryReserveHeap`, `NWSHP`'s refusal, `KILLSHP`'s shuffle | **Gameplay.** By R-9 the arena's size is a spawn limiter, and removing it changes which fights can happen |

So "remove the exclusive-or" is not one decision. **Change 1 alone is behaviour-preserving and would
have been affordable; change 3 is a change to the universe and would not.**

### 5.2 Why the oracle going does not unlock it — CORRECTED

**This document opened by saying option B "belongs after M6 has replaced the live oracle with
recorded fixtures". That is backwards, and the correction is the reason the ruling went the way it
did. CORRECTED AGAIN 2026-09-09**, and the second correction is sharper than the first, because M6
closed on 2026-09-08 in a way neither version anticipated.

**There is no fixture.** M6-b was scoped to record one; the owner asked "why bother?", the corpus
recommended building it, and the owner ruled the other way (ADR-009's Context, which records the
argument it went against). ADR-009 §1: "**No fixture was recorded: there is nothing between the port
and its own tests.**" So the reasoning this section had — a content-addressed record that answers
with a *miss* rather than a mismatch — describes an artefact that was never built.

The correct statement is simpler and cuts harder. **The evidence option B would have had to move is
not stale; it is deleted.**

- **The whole-bitmap comparisons are gone.** `TITLE`, `TT110`, the dashboard, the planet and
  stardust suites compared the whole `SCBASE` region byte for byte, and they went with the oracle at
  M6-b-5 — 469 tests to 131. ADR-003 §2's two golden canvases went with them, which ADR-009 records
  as an amendment rather than a loss. There is now no test anywhere that asserts what the canvas
  *looks* like against anything outside the port.
- **The mutant floor survives, and it is the instrument option B would still have to answer to.**
  M6-b-8 cut the corpus from 97 mutants in seventeen files to **8 in four**, and the floor with it —
  from fourteen files to **three: `Canvas.cpp`, `Raster.cpp` and `ShipDraw.cpp`**. That cut makes
  the point *harder*, not softer: **every file left on the floor is drawing**, and two of the three
  are what changes 1 and 2 rewrite. The floor "grows when a file joins that list, never shrinks",
  so option B would have to put a caught mutant back into code it had just rewritten, with no
  oracle to say what the mutant should have broken.
- **The replay digests are now the primary instrument** (ADR-009 §2) and they fold the heap bytes
  and the state bits, so option B moves them. Re-recordable — but re-recording a digest is only
  honest when something can say the move was intended, and that is exactly what no longer exists.

**So the window for option B closed on 2026-09-08 and this document was written on the wrong side of
it.** Not because the oracle was an obstacle to be waited out — that was the first version's error —
but because it was the only arbiter the change would ever have had, and it is gone. Option B is not
merely unverified now; it is unverifiable in this tree, permanently, and no slice can reopen that.

**This does not change the ruling and it is not an argument that the ruling was lucky.** Option A
was chosen on its merits before any of this was known. But the margin turned out to be narrower than
§5 first described, and a reader deciding something similar later should see the real shape:
verification you delete is not verification you can get back.

### 5.3 The measurement that would have settled it, and why it is not needed now

One experiment decides change 1 outright and costs an afternoon: **stub the `DrawShipLines` call
inside `EraseShip`, keep the `OnScreen` flip and the returned carry, and require the replay digest
to be unchanged.** A held digest proves erase-drawing is pure presentation; a moved one names the
coupling R-8 missed. The same stub applies to the sun and the stardust separately.

It is **not carried into §6**, and the reason is worth stating so nobody adds it back as diligence:
under option A the canvas keeps erasing exactly as it does today, so there is nothing for the
experiment to measure. The picture's own erases can be dropped without any equivalent measurement,
because R-4 already proves — by construction and by the twins-absent replay — that nothing the
picture does can reach the game.

### 5.4 What was given up

Honestly, and so the ruling is not read as costless: option B is the only one of the two that would
have removed the 1,728-byte arena, `KILLSHP`'s shuffle and `EE51` from the port. Option A leaves all
of it in place and running every frame, drawing a canvas nobody ever sees. **That is a real price,
and since 2026-09-09 it is charged for a different reason than this section first gave.** The canvas
was kept as the oracle's view; the oracle is gone, and what keeps it now is that the replay digests
fold it (ADR-009 §2) — it is the port's own regression surface rather than a bridge to the original.
The faithful surface is maintained solely as evidence, and the evidence is now internal.

If a later phase ever retires the canvas itself, this section is where that conversation starts, and
R-8, R-9 and R-10 are what it starts from — **but §5.2 is the warning it starts under**: the
arbiter that could have priced such a change is no longer in the tree.

---

## 6. The track — three slices, ruled 2026-09-08

**The track is RN-0, RN-1 and RN-2, and it stops there** (owner ruling, §9.2). What it delivers is a
program with real frames and no erase logic on the surface a person sees, **drawing a picture
pixel-identical to today's**. Nothing a player can see changes, no test is rewritten, and every
slice's gate is an equality against the picture the tree already draws.

The three slices below the rule are **out of scope and kept rather than deleted**, because they are
what a later track would start from and because RN-1's gate only means something if the reader can
see what was deliberately not built beside it.

Shaped so that the tree plays at every step, which is Resolution.md §10's rule and it earned it.

| Slice | What | Gate |
|---|---|---|
| **RN-0** ◐ **part-built 2026-09-09 (§12)** | The second `Picture` and the background/frame split (§4.5), with the plane still a bit plane and the twins still exclusive-oring. Frame picture cloned from background each frame; everything else unchanged. **The GATE is built and the SPLIT is not**, and §12 says why that order and what is left | ✅ `ThePictureIsAsRecorded` — sixteen checkpoints of the scripted flight, recorded and green. ◻ The docked screens are not covered and want a `PictureTextTests`-shaped fixture (§12.2). ◻ Three of the seventy-four sites need a ruling, not a classification (§12.3) |
| **RN-1** | The frame boundary (§4.2). `Picture::Clear` on the frame surface each frame; the per-frame twins re-emit; the erase twins become drops (§4.4) and `check_twins.py` grows its fourth table | The picture must still be identical, because a correct erase and a correct clear produce the same frame. **This is the slice that proves R-2 is solved.** It also runs under `TheReplayIsTheSameWithNoTwins`, which is the guard that matters here — dropping every erase twin is precisely the change T1 exists to catch, and that test was built on 2026-09-09 ahead of the track rather than inside it (§11.3) |
| **RN-2** | ADR-008 amended, and it is amended by DELETION: **T3 goes** — "every erase has a twin erase" is not a rule the picture can break once it has a frame — and §1's byte count is corrected for the second surface. §3's five clauses all stand, because the picture is still a bit plane and still byte-comparable to the canvas | `tools/check_docs.py`, and the ADR's Status table |

**Out of scope by the ruling of 2026-09-08**, and kept as the record of what was weighed:

| Not built | What it would have been | Why it is not in the track |
|---|---|---|
| ~~RN-3~~ | The display list behind the nine primitives (§4.3), rasterized into the same index plane | Buys nothing on its own — representation changes, output does not. It is only worth its cost as the thing colour is built on |
| ~~RN-4~~ | Colour per primitive; the cell-colour clash goes | The one slice a player would see, and the one that costs ADR-008 §3 clause 1 and 2,741 lines of `Picture*Tests.cpp` (R-6). Declined for now, not refused |
| ~~RN-5~~ | Dirty rectangles for the dashboard's static index plane | A 36 KB copy at 60 Hz is 2.1 MB/s; there is nothing here worth optimising until something measures it |

**What stopping at RN-2 means, said plainly.** The picture keeps its per-cell palette, so two colours
in one character cell is still two colours in one character cell — the C64's constraint, faithfully
reproduced on a surface that no longer needs it. That is the visible thing this track does not fix,
and §9.4 is the ruling that would reopen it.

---

## 7. What none of this changes

Same universe, same RNG consumption, same AI, same bytes on the C64 canvas, same replay digests,
same state-hash cell walk, same mutant floor — every instrument ADR-009 §2 lists, reading exactly
what it reads today. The canvas is not touched by any slice above.

*(This paragraph named the oracle, the goldens and the recorded fixtures until 2026-09-09. All three
are gone from the tree and the claim is unchanged in substance: what the track does not disturb is
whatever pins the port, and the list of those has simply got shorter.)*

The window, the integer scale, the letterbox and the palette are also untouched; §4.6's first
choice means `ScreenPresenter` does not change at all.

---

## 8. Risks

| # | Risk | Mitigation |
|---|---|---|
| RN-a | A per-frame region that nobody noticed was persistent — a message, a bulb, a mission marker — blanks after RN-1 and no test looks at it | RN-1's gate is frame-for-frame identity against today's picture on a recorded flight, which sees every region a replay visits. What it cannot see is a screen the replay never reaches |
| RN-b | RN-3's colour change makes the shadow tests' rewrite the moment the picture stops being checkable against the canvas at all | Keep clauses 2 to 5 whole; they are the ones that do not depend on representation. Restate clause 1 before RN-3, not after |
| RN-c | The two-surface split invites a twin to write the background from a per-frame path, which is a smear that survives a clear | The background surface is `const` to every per-frame twin; only the view-change path holds a mutable reference |
| RN-d | `Universe` grows again — ADR-008 §1 already recorded 15 KB → 123 KB, and §4.5 adds 36 KB | Owner ruling §9.3, stated rather than mitigated, as ADR-008 stated its own |
| ~~RN-e~~ | **CLOSED 2026-09-09, before the track started.** T1 had never been measured (R-4): ADR-008 §3 said the replay ran with the twins present and absent, and it did not, from RS-0 onwards. `TheReplayIsTheSameWithNoTwins` now does, and Resolution.md's R26 closes with it | The risk predated the track; closing it was the precondition rather than the product |

---

## 9. The rulings — all four taken, 2026-09-08

The document opened asking four questions and all four were answered the day it opened. They are
kept as questions, with the answer against each, because the alternative that was rejected is part
of the record.

1. **Which surface moves?** **Option A.** The picture becomes a rendered frame; the canvas and the
   game are not touched. §5 is declined and rewritten twice to say why — once for the sequencing
   correction, and again on 2026-09-09 when M6 closed and the premise changed under it (§5.2).
2. **How far?** **RN-0 to RN-2 and no further.** Frames, no erase logic, and a picture that stays
   pixel-identical to today's. The alternative — through the display list to per-primitive colour —
   is not refused, only not now; §6's second table holds it whole.
3. **Does `Universe` take another 36 KB** for the background surface (§4.5)? **Yes.** Taken as the
   obvious default rather than deliberated: 36 KB inside a struct ADR-008 already recorded at 123 KB
   is not worth a rewrite of a dozen straight-line screen printers, and the re-entrant alternative
   is the one that risks a twin *deciding* against T1. ADR-008 §1's number is corrected at RN-2.
4. **Is per-primitive colour wanted at all?** **Not in this track**, which is ruling 2 seen from the
   other end. The consequence is §6's closing paragraph and it is the one thing here a player would
   have noticed.

---

## 10. Journal

**2026-09-08 — opened.** Written from the owner's question. Two things were expected and turned out
false, and both changed the shape: the presenter was assumed to be the obstacle and is already
conventional (§0), and the surfaces were assumed to be cleared somewhere and are never cleared at
all (R-1). The second is why §3 separates the frame boundary from the display list — the boundary
is the benefit, and it was invisible from the outside because the program has no frame to look at.

The finding that made §4 possible was not in the rendering code: ADR-008 §4 had already excluded
the picture from the state hash, for reasons about re-recording replay tables that had nothing to
do with this question, and Resolution.md §8.4 had already built the twins-absent replay that proves
the exclusion is not a hole. **The licence for a rendering change was written a day earlier by
somebody solving a different problem**, which is worth recording because it is the second time in
this corpus that a decision taken for verification's sake turned out to be the thing that made a
change affordable.

**2026-09-08 — option B examined on the owner's word that the oracle is going, and option A ruled.**
The question was reasonable and the answer was the opposite of the one the question expected, which
is why §5.2 is marked CORRECTED rather than edited quietly. Three findings came out of it (R-8 to
R-10) and one of them corrects a finding this document had already published: **R-3 said the erase
feeds the generator, and the body says the carry is `_heap.Read(_ship.heap) >= 4u`** — heap
metadata, with the line replay contributing nothing. R-3 was written from the header comment on
`EraseShip` and not from `EraseShip`, and the comment is about where the flag comes from in the
*original*, which is a different question from what the port's expression depends on. **A marker
that explains the 6502 is not a specification of the C++ beside it**, and this is the second time in
this corpus that reading one as the other produced a wrong claim with every test green.

R-9 is the finding that would have mattered most and was not visible from the drawing code at all:
the line heap is a spawn limiter, so a change filed under "rendering" reaches which fights the game
can stage. R-10 is smaller and the same shape — two routines filed under erase-by-redraw that erase
by neither.

The ruling took option A the same day. What it buys is in §4; what it gives up is in §5.4, and that
section exists because a ruling recorded only with its benefits is not a record.

**2026-09-08 — the remaining three rulings, and the track cut to three slices.** Scope ruled at
RN-0 to RN-2: the frame boundary and nothing past it. **The track therefore delivers no visible
change at all**, and that is the point rather than a disappointment — every gate is an equality
against the picture the tree already draws, so the whole of it is verifiable by tests that already
exist. What it removes is a category of defect: T3, the rule ADR-008 recorded as the one with no
cheap test, stops being a rule that can be broken. The 36 KB was taken as a default rather than
deliberated, and §9.3 says so in those words, because a ruling that was never really in doubt should
not be dressed as one that was.

The document opened with four questions and closes the same day with four answers, which is unusual
enough in this corpus to be worth marking. It is also **shorter than it looks**: §5 and §6's second
table are both records of roads not taken, and a reader who only needs the plan needs §4, §6's first
table and §9.

---

## 11. Revalidation against `a827b97`, 2026-09-09

The tree moved 23 commits under this document overnight: **M6 closed** (ADR-009). The oracle,
`Cpu6502`, `OracleImage`, `Upstream/`, `MasterFile/`, `labels.py`, `inventory.py` and the ledger are
deleted; 4,103 `// 6502:` markers reached zero; the suite went 469 tests to 131 and the repository
checks 19 to 13. Every claim in this document was re-taken against the new tip. **Three moved and
one of those was never true.**

### 11.1 What held, unchanged

Every number in §1, exactly: **20** exclusive-or sites with the same per-file split, **90** `Picture`
entry points across **16** headers, **12** `/// 2x of:` twins, the line heap at **1,728** bytes
(`0xFFC0 - 0xF900`), **2,741** lines across five `Picture*Tests.cpp`. The presenter is untouched —
`FLIP_DISCARD`, `FRAME_COUNT = 2`, `Present(1, 0)`. R-8's expression is still `ShipDraw.cpp:218` and
still `_heap.Read(_ship.heap) >= 4u`; R-9's refusal and R-10's differential sun read as quoted. The
picture is still excluded from the state hash (`StateHash.cpp:397`). `check_twins.py` is green, and
`mutate.py --check` is green.

**RE-TAKEN AGAIN against `eb99415` on 2026-09-09**, after `origin/main` was force-pushed with a
rewritten history (M6-b-11, which purges the masters from the past that M6-f removed from the tip).
The rewrite moved no content this document depends on — every count above held a second time — but
two upstream commits landed with it and one of them moved a number quoted here: **M6-b-8 cut the
mutant corpus from 97 mutants in seventeen files to 8 in four, and the floor from fourteen files to
three.** §5.2's bullet is corrected to match, and the correction strengthens it. The other,
M6-b-9, added `mutate.py --check-selftests` after four of five selftests were found rotted — they
zeroed registers only the oracle comparisons read, so when those went the selftests survived while
`--check` stayed green. **That is this document's own R-4 in a different file**: an instrument that
passes because it is no longer testing anything, going unnoticed because the thing that would have
noticed was deleted.

**That a rendering analysis survived the removal of the entire verification apparatus with every
count intact is itself the finding**: none of §1 was ever about the original. It was about the port.

### 11.2 What moved

| Claim | Was | Is |
|---|---|---|
| **R-1** | "Neither `Clear` has a single caller" | `Canvas::Clear` has one, in `VideoStateTests.cpp:54`. The load-bearing half — *nothing in the program clears either surface* — is untouched |
| **§5.2** | Option B fails because the recorded fixture answers with a miss | **No fixture was ever recorded.** The owner ruled against building one and ADR-009 records the argument it overrode. The whole-bitmap comparisons are deleted outright, so option B's evidence is not stale but absent |
| **Depends on** | ADR-003 (the oracle) | ADR-009 §2. ADR-003 §1, §2 and §4 were amended out of the role this document gave them |
| **§1.3** | 15 twinned units | 16 — and that was a miscount on 2026-09-08, not a change in the tree |

### 11.3 The one that was never true — R-4

This document leaned twice on ADR-008 §3's fifth kind of evidence: *"the replay runs with the twins
present and absent and requires the same digest, which is what proves T1 rather than asserting it."*
Resolution.md §8.4 describes it in detail, names the three tests and names the flag,
`ELITE_SCREEN_SHADOW`, "a test-only define".

**`ELITE_SCREEN_SHADOW` does not exist and never did.** It appears nowhere in `GameLogic/`,
`Outpost/`, `Tests/`, either project file, the portable runner's generator or its shell script — the
only occurrence in the repository is inside the sentence in Resolution.md that describes it.
`FlightReplayTests.cpp` runs `TheScriptedFlightIsAsRecorded`, `TheDeathIsAsRecorded` and
`TheEscapePodIsAsRecorded` once each, with the twins present. RS-0's acceptance row says "§8.4 green
with nothing to twin yet", which is true and is how a clause can pass at the moment it is written
and never be built afterwards.

So **T1 — the rule the entire twin architecture rests on — is asserted and not measured**, and has
been since RS-0. It is a documentation defect in ADR-008 §3 and Resolution.md §8.4 rather than a
defect in the code: the twins do take const references, and the construction argument is sound. But
ADR-008 §3 opens by saying "three of the five clauses were added *because a green test failed to see
something*", and this is a sixth thing no test sees.

**It was not this document's to fix in prose, and it is fixed in code.** On the owner's instruction
of 2026-09-09 the run was built the same day, ahead of the track rather than inside it:

- `Picture` gains `Drawing()`/`SetDrawing()` and the free `DrawingTwins(_picture)`, which replaced a
  bare `_picture != nullptr` at **49 guard sites** across eleven files. The flag lives in `Picture`
  because that surface is already the one field `HashState` skips and `StateCells` gives no cells,
  so it inherits that treatment and opens no new hole.
- Three twins take the surface by REFERENCE and could not use the shared guard — `PlotPixel2x` in
  the long-range chart and the two `WriteBitmapByte2x` in `Flight.cpp`. They read `Drawing()`
  directly and the line says why.
- `TheReplayIsTheSameWithNoTwins` runs all three scripted flights with the twins off. **The digests
  do not move.** Two further assertions stop it passing vacuously: the bitmap plane must be blank
  with the twins off and written with them on — without the second, a `SetDrawing` that did nothing
  would leave every digest where it was and the suite would be green for the wrong reason.

**A runtime switch, not the compile-time `ELITE_SCREEN_SHADOW` the design named**, because a define
needs a second compilation of `GameLogic` and a second test binary for the same answer. What the
define would additionally have caught is a twin reading something *before* its guard, so guard
placement is now a review rule; all forty-nine wrap the whole of their twin's work.

ADR-008 §3's clause stands again with the history of its falsehood beside it, Resolution.md §8.4 is
BUILT and R26 is CLOSED. The finding was reported to the owner on the day it was found rather than
folded silently into a slice, and repaired on the next instruction.

---

## 12. RN-0 begun, 2026-09-09 — the gate first

**Built: the gate. Not yet built: the split.** RN-0's acceptance is "the picture identical to
today's, frame for frame, on a recorded flight", and on 2026-09-09 that instrument did not exist —
so it was built before the change rather than after it, which is the order risk RN-a demands.

### 12.1 `ThePictureIsAsRecorded`

`Trace` gains a `pictures` vector and the replay's checkpoint lambda fills it with
`Picture::Hash(canvas)` — what the surface RESOLVES to, so it is what a person would be shown and
not an internal representation. Sixteen checkpoints over the scripted flight, recorded as
`RECORDED_PICTURE` on the tree as RN-0 found it.

**This is the first whole-frame regression test the 640×400 surface has ever had.** Everything else
about it is per routine: a glyph at a cell, a bar at a value, a line swept over its inputs
(ADR-008 §3). Nothing asserted a whole frame, let alone a hundred frames of a real flight — and the
replay tables could not, because `HashState` walks past the picture on purpose (ADR-008 §4). That
exclusion is right and it left precisely the hole RN-0 and RN-1 would fall into: they do not change
*what* is drawn, only *when*, and a state digest cannot see that.

### 12.2 The docked screens are NOT covered, and the reason is a fixture

An attempt to gate them the same way failed and is recorded rather than retried:
**`DockedSessionTests`' `Session` cannot see the picture at all.** It prints through a
`TranscriptSink` and never calls `TextPrinter::AttachPicture` — only `Game` and `PictureTextTests`
do — so all seven docked screens digest to the same value, which is whatever the start sequence
left. A gate there needs a fixture of the `PictureTextTests` kind, and that is RN-1's to build.

What covers them meanwhile is ADR-008 §3 clause 1: the shadow tests assert every glyph the canvas
has appears at the mapped wide cell, which would catch a blanked docked screen. That is real cover,
and it is per cell rather than per frame.

### 12.3 The sites, classified — what RN-0 has left to do

Seventy-four sites reach a picture through the universe. They divide into two kinds and the division
is the whole of the remaining work, because §4.5's two surfaces only help if each site is on the
right one.

**Transient — redrawn every flight pass, and therefore the FRAME's:** `DrawShip`,
`DrawShipAsPoint`, `DrawExplosionCloud` and `DrawPlanetOrSun` (`ShipDraw.cpp` ×4); `MoveShipTail`
(`ShipMove.cpp` ×3); `MoveStardust`, `EraseSun`, `DrawScannerBlip`, the laser's two
(`FlightLoop.cpp` ×5); `DrawDials` (`GameLoop.cpp`).

**Persistent — drawn once and expected to survive, and therefore the BACKDROP's:** every
`ShowMessage` (×8, which live for a `DLY` countdown of about twenty frames); `ClearMessageRows`
(×5); the indicators — `SetMissileIndicator`, `ToggleStationIndicator`, `StartEcm`, `StopEcm`,
`ResetMissileIndicators` (×9); all of `Charts.cpp` (×11); `SetUpScreenPixels`, `DrawFullBorder`,
`SetUpLoaderScreen`, the crosshairs and `Launch` (×9).

**And three that need a ruling rather than a classification**, which is why RN-0 is not finished
here:

- **`SeedStardustField` / `SeedStardustAndClearShips` / `FlipStardust`.** The field is transient and
  `MoveStardust` re-emits every particle each pass, so the seed looks like the frame's — but it also
  runs at a launch and a hyperspace, where there is no next pass yet.
- **`DrawHyperspaceRings`.** It presents inside its own loop, so it is an animation that already
  owns a frame boundary of its own; whether it becomes eight frames or stays one is a design
  decision this document has not taken.
- **`ClearAllShips`.** Under RN-1 the restore removes the ships and the erase becomes a drop — but
  it is also called from a view change, where there may be no restore before the next present.

**What this means for the estimate.** §6 called RN-0 the plumbing and RN-1 the boundary. Building
the gate showed the split is the dear half and the boundary the cheap one: the two surfaces are a
`memcpy` and two call sites, and deciding which of seventy-four writes goes where is the slice.
That is not a reason to stop — it is the reason the gate went in first.
