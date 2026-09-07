# ADR-006 — Modernisation Architecture

**Status:** Accepted · 2026-09-06, written at Phase M2's opening from what M0 and M1 built and from
[Modernize.md](../Modernize.md) §4 as accepted; amended as each later phase lands. **§4 amended
2026-09-07 at M3's close**, where three of its claims did not survive the build; ADR-007 records the
ownership and the replay hash as they are. M5-d revises this document from what was built.
**Depends on:** ADR-001 (fidelity — unchanged: the game does not change), ADR-002 (numeric model —
unchanged: every byte keeps its width), ADR-003 (the oracle stays the judge until M6 records it),
ADR-004 (projects and layout), ADR-005 (presentation)
**Feeds:** every slice of Modernize.md M2 to M6; the ratchet in `tools/modernize_ratchet.json`;
ADR-007

## Context

The port reached the gate ADR-001 §4 set: every oracle suite, every whole-bitmap comparison and the
docked replay green on the faithful build. What it carried at that point was the 6502's calling
convention and memory map as its architecture — routines talking through zero-page scratch
structs, a ship that was thirty-seven bytes addressed by number, blueprints and line heaps
addressed as 6502 addresses, argument lists that were structs of twenty references, twenty-two
abstract "effects" seams, and the outer loops and the mode machine in the Windows executable where
no oracle and no Linux job could reach them. Modernize.md is the plan for replacing that with a C++
program **with no behavioural change**; this ADR records the architecture the plan builds toward
and the decisions it has already made on the way. The ADR wins on *what*, the plan on *when*.

Modernize.md §1 settled nine questions by owner ruling on 2026-09-06: C++20; the tests and the
executable's game logic are in scope; the blueprints are parsed and the heap arena is addressed by
offset; and the port is DETACHED from the original at the end (§7 below).

## Decision

### §1 Layers and the dependency rule

```
Ports      Presenter · Keyboard · SoundSink · SaveStore   -- four abstract classes; the executable and the tests' null port implement them
Game       Elite::Game -- the mode machine, the outer loops, the key dispatch, the exits
Systems    Flight, Render, Tactics, Spawn, the docked screens, Text, the sound players, Missions
Model      Elite::Universe -- every byte of game state, typed, value-copyable, hashable
Kernel     EliteTypes, Arith (value in, value out), Rng, the lookup tables, Canvas, Tokens
```

Edges point down only. Systems never include each other's private scratch; they communicate through
`Universe` and through typed stage results. `Game` is the only thing that knows the game has two
halves. Nothing below `Ports` includes a Windows header. Until M3 lands, `Outpost/FlightSession` and
`GameShell` hold the halves `Game` will absorb, and `tools/check_outpost.py` is the compile the
Linux leg cannot do (ADR-004 §5).

### §2 The data model: structs with codecs, bytes as the wire format

**Built by M1, 2026-09-06.** Game state is typed: `Ship` (`SignMag24` axes, `Vector16`
orientation, named tail bytes, `HeapOffset heap`), `Commander` (named fields, `Credits` and `Tally`
where a multi-byte order is the risk), `Blueprint` (the twenty header bytes as fields, the vertices,
edges and faces as spans over the region, placed where the header's offsets say in the 6502's
wrapping arithmetic), `ShipType`, `ShipStateBit`/`AiBit`/`NewbBit` over the three flag bytes, and
`HeapOffset` for the ship line heap. Each struct that the original holds as bytes has
`ToBytes`/`FromBytes` written once in terms of the original's offsets, and a `static_assert` round
trip over distinct bytes proves the order; the oracle compares the codec's bytes, so a struct's own
layout is free. Two rules hold for every field: **no width changes** — a byte stays eight bits with
its wraparound, and any widening happens inside a helper narrowed the way the original narrowed
(ADR-002 §3) — and **the default of a typed field is what the original's zero bytes mean**, which is
why `HeapOffset{}` is the zero pointer `ZINF` writes and not the arena's base.

Deferred, with the reason in the plan: strong types for the one-byte fields that are compared and
subtracted at many sites (`fuel`, the lasers, the equipment bytes) wait for M5, where a type earns
its operators; the flag bytes' owning types likewise.

### §3 Calling conventions: value in, value out

**Opened by M2-a, 2026-09-06.** Every kernel routine's inputs are parameters and its outputs a
returned struct. The zero-page scratch structs — `MathWorkspace`, `DrawWorkspace`,
`GeometryWorkspace`, `ClipState`, `Projection` and `K3Block` — carried forty-nine fields when M2-a
counted them and forty-two after M2-c's first commit (`NumberWorkspace` and the drawing bytes that
were parameters went), and `tools/channel_census.py` reads every routine and classifies each field per routine as
written, read before written from the caller, or read after a call; Modernize.md §4.3 holds the
census and a verdict for each field. The verdicts fall into four classes and only four:

- **Parameter** — the routine reads what its caller set (`Q`, `T1` in `DIL`, `CNT2` in the ellipse,
  `COL`, `ZZ`, the line the clipper takes).
- **Result** — a caller reads what the routine left (`P`'s low byte after a multiply, `R` after a
  divide, `K` after `DVID3B`, `SWAP` and `XX13` after the clipper, `XX3` after `LL9`).
- **Local** — every user writes before it reads (`U`, `T2`, `R2`, `LL9`'s counters).
- **State, deliberately** — a value that outlives the call because the original's behaviour depends
  on it: `Projection` (`K3`/`K4`) after `PROJ`, which `SHPPT` reads stale (ADR-001 §6), and `SC`,
  the dashboard cursor `DIALS` advances across seven calls. Each has its reason at the one place it
  matters and is not scratch.

The kernel's carry-in parameters (`AddWithCarry`, `Rng::Next`, the multipliers) are the numeric
model and stay; a carry on a routine boundary that merely happens to be where a 6502 flag was live
(`RunSpawning`, `AddDebris`, the sound seams) resolves to what each caller passes. `_a`/`_x`/`_y`
parameters are renamed for what they carry at the same time, and the `// 6502:` comment keeps the
register until M6.

### §4 Ownership: `Universe`, `Game`, four ports

**Built by M3, 2026-09-06/07, and ADR-007 records it — including the three places this paragraph
was wrong.** `Elite::Universe` is a plain aggregate owning every byte of game state and nothing
else, `Elite::Game` owns the dispatch and both outer loops, and `Outpost/Main.cpp` is 256 lines of
platform. The twenty-two effects seams became four ports — `Presenter`, `Keyboard`, `SoundSink` and
`CommanderStore` — plus five that turned out to be `GameLogic` reached through the executable, and
nine abstract classes remain rather than four: two are the text system's own polymorphism and two
are blocked on a comparison the emulator cannot yet make (ADR-007 §6, Modernize.md §4.5).

**Three claims above did not survive the build.** `Step(InputFrame)` is three `Step`s taking a key,
because `FRCE` chooses between three routines rather than three branches of one. `Frame()`,
`Sounds()`, `StateHash()` and `Mode` are not built, and `Mode` is M4-d's. And the executive decides
how many steps because it MUST: the count is floating point and the determinism guard forbids the
library one. ADR-007 §2 and §3 have the reasoning; the ownership and the replay hash are its §1 and
§4.

### §5 Control flow: pipelines with named stages

**Planned, M4.** The flight frame, `LL9`, `TACTICS` and `DOCKIT` become pipelines of functions with
typed results (`Contact`, `ScoopResult`, `DockingTest`, `ScaledOrientation` → `FaceVisibility` →
`ProjectedVertices` → `EdgeSelection` → `ClippedLines` → `HeapRun`, `Decision` and `Apply`), the same
control flow said once. Original bugs stay ported (ADR-001 §3, §6): a stage that could not express
`SHPPT`'s stale read would be wrong, and the ADR-001 row is the test that says so.

### §6 Verification through the change

- **The oracle decides**, per slice, until M6 records it. A slice ends with the suite green on the
  portable runner with the oracle present and, before merge, on the Windows job.
- **The universe image and the replay hash** (`Tests/GameLogicTests/UniverseImage`, built at M0-b and
  M0-c) are the layout-independent bridge: `Materialise` writes the same bytes before and after a
  slice for the same game, every oracle test compares through it, and the flight replay's sixteen
  recorded digests move only when the digest is deliberately widened or a defect is found and fixed.
- **The ratchet** (`tools/check_modernize.py`, `modernize_ratchet.json`) counts the patterns
  Modernize.md removes and only goes down; a ceiling that moves says which slice moved it. The one
  count that rises before M6 is the `6502:` marker count, because every typed field carries the
  label the offset carried (rule 4).
- **Mutants are re-anchored, never dropped**; the corpus is re-run at the end of every slice.
- **One pattern per slice; rename toward meaning as you go; every signature change reaches
  `Outpost/` in the same commit.**

### §7 The detachment

**Planned, M6, by owner ruling.** The tests keep their shape and what answers them changes: a
`RecordingOracle` wraps the live interpreter once and writes a fixture per suite, a
`RecordedOracle` serves it, and then the interpreter, the assembler, `Upstream/`, `MasterFile/`, the
label-named identifiers, the assembly quoted in comments, the `// 6502:` markers and the ledger go.
A fixture pins exactly the calls the tests made while the original was here (Risk R19), which is
why M6 is last and begins with a coverage review. The derived data tables and the recorded fixtures
stay: the tree carries the original's data and the port's own code, and nothing of its source.

### §8 Language level

C++20, by ruling: `std::span` with fixed extents for the codecs, `constexpr` codecs with
`static_assert` round trips, scoped enums, concepts where a helper must refuse the wrong bit type
(`ShipFlagBit`), `[[nodiscard]]` wherever a result carries a flag. C++23 is held; nothing in the plan
needs it.

## Consequences

- A reader can tell what a routine consumes and produces from its signature, and what a byte means
  from its type, without the original beside them — which is the property M6 needs before it can
  remove the original.
- The oracle compare is a byte compare of codec output for the whole of M1 to M5, so a slice's
  correctness is measured by the same instrument before and after; the cost is that every struct
  carries a codec and every bridge cell goes through one.
- The four "state, deliberately" fields and the ported bugs are visible exceptions with a reason each,
  rather than accidents of scratch; a future ADR under plan §6 Phase 6 that fixes one changes the
  game and says so.
- The executable shrinks to composition and pacing once M3 lands; until then it is the caller the
  Linux leg cannot compile, and rule 6 is the mitigation.

## Status by phase

| Phase | State | Recorded in |
|---|---|---|
| M0 The safety net | Built 2026-09-06 | Modernize.md §6 M0, §8 |
| M1 Typed data | Built 2026-09-06 | §2 above; Modernize.md §6 M1 |
| M2 Calling conventions | M2-a and M2-b built 2026-09-06 (the kernel takes values and returns structs; the frame's `Q` named, Modernize.md §8 and R22); M2-c's first of three commits built the same day (the line, the pixel, the blip, the compass, the dials and the number printer); M2-c-2, M2-c-3 and M2-d open | §3 above; Modernize.md §4.3 |
| M3 Ownership | Built 2026-09-06/07 (M3-0, M3-a, M3-b, M3-c, M3-d) | §4 above, amended from what was built; ADR-007 |
| M4 Control flow | Planned | §5 |
| M5 Polish and the ledger | Planned; amends this document | Modernize.md §6 M5 |
| M6 Detach | Planned | §7 |
