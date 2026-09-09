# ADR-008 — The 640×400 Picture, as Built

**Status:** Accepted · 2026-09-08, written at the resolution track's close from what RS-0 to RS-6
actually built rather than from what [Design/Resolution.md](../Resolution.md) planned — the two
differ in a dozen places and each is recorded in that document's journal with the measurement that
moved it.
**CORRECTED · 2026-09-09.** §3's closing sentence claimed an instrument that did not exist — the
replay run with the twins present and absent — and this ADR asserted it as *the* proof of T1. It had
never been built, from RS-0 onwards. **It is built now**, so the clause stands again with the
history of its falsehood beside it rather than tidied away; T1 is measured and Resolution.md's R26
closes with it. The search that found the gap is [Rendering.md](../Archive/Rendering.md) §11.3. **No
decision in this ADR moved**: the correction was always to a statement about evidence.
**Depends on:** ADR-001 (fidelity — the game does not change, and §1 now says which surface is the
verification view), ADR-002 §4 (the canvas and its resolve), ADR-003 (the oracle judges the canvas
and cannot judge this), ADR-005 §1 (which this amends: 640×400 for 320×200, `ScreenPresenter` for
`CanvasPresenter`, the window at 2×), ADR-007 (state ownership, which this adds one excluded field
to)
**Feeds:** every future drawing routine, which now has two sites instead of one

## Context

The C64 draws a 320×200 bitmap and the port reproduces it byte for byte, because that is what the
oracle can compare. Presented on a modern display that image is either small or soft: an integer
scale of a 320×200 canvas has no square-pixel identity, and the game's own lines are a quarter the
width a person now expects.

The owner ruled four things (Resolution.md §11) and they set the shape: the dashboard is redrawn at
twice its detail, the space view is drawn at twice the line resolution with the same field of view,
text stays 8×8 on an 80×50 grid with every docked screen re-flowed, and there is no runtime option —
the canvas stays as the verification view every oracle test, golden and fixture reads, and is never
presented.

That last clause is what makes the rest possible. **The canvas is not replaced; it is joined.** Every
byte the game writes still lands where it always did, so every comparison against the assembled
original still means what it meant.

## Decision

### §1 One surface beside the canvas, and it is VIC-II-shaped

`Elite::Picture` is 640×400: a bitmap plane of 32,000 bytes for the upper region, an 80×50 grid of
cell palettes beside it, and a 640×112 plane of colour INDICES for the dashboard. Since RS-4-art
that lower plane is filled from `DASHBOARD_PICTURE_2X`, the port's own sixteen-colour art, rather
than decoded from `DASHBOARD_IMAGE` — the picture below the split is no longer a function of
anything the C64 shipped (Resolution.md §5.3). It is
VIC-II-shaped rather than a framebuffer because the game *erases by drawing again* — every ship
line, every blip, every message is removed by exclusive-oring it a second time — so the upper
surface has to be a bit plane or erasure stops working. The dashboard is an index plane because
below the raster split the hardware is in multicolour mode, where two bits select from four
sources, and a twin that put the right shape in the wrong colour would pass a shape test.

`Universe` owns one, beside the canvas. It costs 107,682 bytes, which takes a `Universe` from 15 KB
to 123 KB — stated rather than mitigated, and the number a later slice would have to argue with.

### §2 The twin rule, which is the whole architecture

**Every canvas draw call has a twin at the same call site.** The faithful routine decides — what to
draw, when, in what colour, whether at all — and the twin only computes where, at twice the scale.
Four rules follow and each was earned:

- **T1 — the faithful routine decides and the twin computes.** A twin that branches on its own has
  become a second game. This is what declined the short-range chart's "labels no longer collide":
  a wide chart that named more systems would be *deciding*.
- **T2 — precision comes from upstream at twice the scale, never from interpolation.** A twin may
  use a byte the faithful routine discarded (the scanner's `x_lo`); it may not invent one.
- **T3 — every erase has a twin erase.** The one rule with no test that can catch its absence
  cheaply, because a missing erase looks like a smear three frames later.
- **T4 — a twin carries `/// 2x of:` and never a `// 6502:` marker.** It ports no routine, so a
  marker would claim a label it does not have. `tools/check_twins.py` enforces the pairing.

### §3 What pins it, since the oracle cannot

The assembled original has no 640×400 anything, so nothing outside the tree can say the picture is
right. Five kinds of evidence replace it, and three of the five clauses were added *because a green
test failed to see something*:

1. **Shadow tests.** Every glyph the canvas has, the picture has at the mapped cell, with the same
   bytes; and nothing on the picture the canvas does not account for.
2. **No two faithful cells map to one wide cell** — Risk R24's tripwire, which failed on its own
   example the first time it ran.
3. **No anchor boundary falls inside a word.** Added at RS-5-d, after two screens shipped sheared
   and three green tests said nothing: a sheared mapping is still injective, still carries every
   glyph, still adds nothing. Every clause was about *cells* and none about *words*.
4. **No table puts a glyph outside wide columns 8..71.** Added at RS-5-f, after `main` gave the
   picture a frame and four screens turned out to be printing underneath it.
5. **Property sweeps over whole input spaces** where a twin does arithmetic — the wide line over
   18,432 lines, the dashboard bars over every value at every entry point, the bootstrap equal to
   the canvas doubled over all 71,680 pixels.

And **the replay runs with the twins present and absent and requires the same digest**, which is
what proves T1 rather than asserting it.

**THIS CLAUSE WAS FALSE WHEN WRITTEN AND IS TRUE SINCE 2026-09-09.** From RS-0 until then the run
did not exist: `ELITE_SCREEN_SHADOW`, the test-only define Resolution.md §8.4 names, appeared
nowhere in `GameLogic/`, `Outpost/`, `Tests/`, either project file or the portable runner — its only
occurrence in the repository was inside the sentence describing it — and `FlightReplayTests.cpp` ran
its three digests once, twins present. RS-0's acceptance row reads "§8.4 green with nothing to twin
yet", which is how a clause can pass at the moment it is written and never be built afterwards. **So
this ADR published an assertion as a proof for a month**, and that is the part worth remembering:
§3 opens by saying three of its five clauses were added "because a green test failed to see
something", and this was a sixth thing no test saw — in the one clause that claimed to be the
measurement.

It is now `TheReplayIsTheSameWithNoTwins`, and all three digests are identical with the twins
switched off. The mechanism is a runtime switch (`Picture::SetDrawing`, and the one guard every twin
shares, `DrawingTwins`) rather than the compile-time define the design named, for the reason
Resolution.md §8.4 records. [Rendering.md](../Archive/Rendering.md) §11.3 has the search that found the gap.

### §4 The picture is not in the state hash

`Universe::picture` and `Universe::screenLayout` are the fields `Elite::HashState` excludes
(ADR-007, and the comment beside the fold in `StateHash.cpp`). The picture is a second rendering of
a frame the canvas already holds, produced by code the resolution slices kept changing; folding it
would re-record five replay tables on a thinner sun. The exclusion is narrow — the canvas beside it
is still folded — and §3's replay is what stops it being a hole.

### §5 Text is a mapping, and a re-flow is a table

The font stays 8×8, so what the resolution buys text is not sharper letters but twice as many of
them and room to put them somewhere. A `TextLayout` maps one faithful cell to one wide cell:
offsets, a row stride, and `Anchor` rectangles for the blocks that move. **The screen routines do
not change for a re-flow** — `STATUS` still stores `XC = 6` — so the character stream the fixtures
compare is untouched and only the tables decide the cells.

Two things a table cannot do, and both were found by building:

- **The layout is the CALLER's, because `QQ11` does not name a screen.** `STATUS` and `TT213` both
  call `TRADEMODE` with `#8`. `SetUpScreen` takes a layout and stores it beside the view, in the
  same breath, so there is nothing to drift.
- **A label is a RUN, not a cell.** `TT23` puts a system's name beside its disc; the discs are at
  twice their coordinates, so the name's origin must double while its letters stay adjacent. No
  function of a cell can say that. `TextPrinter::SetLabelRun` is the stored cursor Resolution.md
  §6.2 predicted, in its smallest form: set at the instruction that already places `XC`, cleared by
  the next control code, and living in the printer rather than the `Universe`.

### §6 The layouts as accepted

Each was put to the owner as the wide grid rendered by running the real screen through its table,
per ruling §11.2, and accepted before the table was written. Resolution.md §6.3 carries the numbers.

| Screen | Layout |
|---|---|
| Flight views, title, death | `SPACE_VIEW_LAYOUT` — rows spread so a line lands at the height the original put it, over a ship at twice the scale. No table of their own |
| Status | Two columns: the label lines left, the equipment list anchored right |
| Market / buy | Four fields spread, and the game's TWO heading rows folded onto ONE — the one thing 80 columns buys outright |
| Inventory | Fuel and cash left, the hold anchored right and level with them |
| Equip ship | Item and price, and an anchor that brings `CLYNS`'s prompt back under the list |
| Data on system | The pairs whole on the left, the justified description in a column of its own |
| Long-range chart | The map needs no moving; the title moves above the rule |
| Short-range chart | Rows doubled so labels keep their discs' heights, columns by `SetLabelRun` |
| Briefings | The space view's arrangement, given to a screen that is not the space view |

Two screens have no layout and cannot: **sell cargo** and the **save menu** set up no screen at all,
so they print on whatever is up and inherit its table. That is one defect with two faces and it
predates this track.

## Consequences

- **Every drawing routine has two sites.** A change to one without the other is a screen that drifts
  from a canvas that is still perfect. `check_twins.py` fails the push; the shadow tests fail the
  frame.
- **Five improvements were declined, each measured**: `Divide512` and the sun's twin (the faithful
  arithmetic is a log table, so a "more precise" twin would be a different function), the compass's
  extra bit (it would need a record beside `COMX`/`COMY`, unhashed), `wrapWidth` (`DA11` pads as
  well as breaks, so re-wrapping means the picture printing different spaces from the canvas), and
  the chart's label collisions (0.4 names a chart, against T1 and §3's first clause). **The shape is
  always the same: a real improvement that costs more evidence than it buys.**
- **The canvas fallback is gone.** Between RS-0 and RS-4 a region with no twins yet was drawn from
  the canvas doubled, which is what let the game be played at 640×400 from the first slice. RS-6
  took the scaffolding down; a picture nobody has drawn on now resolves blank, however busy the
  canvas beside it.

## Status

| Claim | State | Recorded in |
|---|---|---|
| The picture is VIC-II-shaped and 640×400 | Built | `GameLogic/Picture.h`, Resolution.md §3 |
| Every canvas draw has a twin | Built and checked | `tools/check_twins.py`, Resolution.md §4 |
| The canvas is never presented and never replaced | Built | ADR-001 §1, ADR-005 §1 |
| The picture is excluded from the state hash | Built | ADR-007, `GameLogic/StateHash.cpp` |
| Every docked screen is re-flowed | Built, two blocked | Resolution.md §6.3 |
| The upscale is gone | Built at RS-6 | `GameLogic/Picture.cpp` |
| The dashboard is redrawn at 640×112 | **Mechanism built; the art is the owner's, outstanding** | Resolution.md §5.3 and §11.1, `GameLogic/DashboardPicture2x.cpp`, `tools/bitmaps.py` |
| T1 — the twins consume nothing the game notices | **Built 2026-09-09**, a month after §3 said so: `TheReplayIsTheSameWithNoTwins` takes all three digests with the twins switched off and they do not move. Asserted, not measured, from RS-0 until then | §3 above, `FlightReplayTests.cpp`, Resolution.md §8.4, Rendering.md §11.3 |
| T3 — every erase has a twin erase | Built as a rule, never testable — and **due for deletion**: Rendering.md's RN-1 gives the picture a frame boundary, after which there are no erases to pair | §2 above, Rendering.md §6 |
