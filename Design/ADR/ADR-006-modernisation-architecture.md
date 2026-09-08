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

**M5-a's strong types, and the three that were examined and NOT built** (2026-09-07). `SoundEffect`
and `Colour` are built. The other three the plan named are refused, each for a reason the build
found rather than for want of time, and this is where they are recorded so the plan's row is not
read as unfinished work:

- **`Options`, the thirteen pause-screen toggles as one struct.** §4.4 deliberately splits the
  thirteen bytes across six owners, because each is state some part of the game already owns.
  Gathering them into one struct so `DKS3` can walk an array of member pointers would put them back
  where §4.4 took them from, and the array of member pointers needs exactly that one struct. The
  ordering `DKS3` depends on is pinned instead by the byte-checked `TGINT` table and the 3,328-case
  sweep that closed slice 4e — a stronger statement than a struct's field order, because it is the
  original's table rather than a C++ declaration order that anybody could reorder.
- **`View`.** `QQ11` is BOTH an index and an arithmetic value: the space views are 0..3 and the
  docked screens are bit values (2, 4, 8, 32) that the game tests with `AND` and combines. A scoped
  enum makes the indexing a cast at every site and forbids the combination outright, so it would
  cost more casts than it removes and misdescribe the byte besides.
- **`Message`.** The same shape: token numbers are indexed into tables and added to, and `MESS`
  reaches them by arithmetic on a base.

**And the three M1 deferred, ruled on 2026-09-07** (build `fuel` and the lasers; refuse the
equipment bytes with the reason):

- **`LightYearsTenths` is built (M5-a-10).** The deferral's arithmetic-operators-with-a-name worry
  was about the twenty-nine READS; the byte has three arithmetic RULES — `MA23`'s saturating scoop,
  the jump's floored burn with its carry, `TT111`'s range check — and the type is those three,
  written once each, with `tenths` for everything that reads a byte. It found the number seventy
  defined three times under three names.
- **`Laser` is built (M5-a-11).** The power byte is the laser's identity in the original, so the
  type is that byte with the three questions the game asks of it — fitted, beam (bit 7), power
  (the byte without bit 7) — and the five named values. It found the four powers defined in four
  files under four naming schemes, twelve constants for four bytes.
- **`Equipment` is refused (M5-a-12), for `Options`' reason: the bytes are not one kind of thing.**
  `ECM`, `BST`, `ESCP`, `GHYP` and `DKCMP` are `0`/`&FF` flags; `ENGY` is a count that multiplies
  the recharge; `BOMB` is a state machine — `&7F` fitted, shifted left on arming and again every
  flashing frame until it is gone, the countdown and the fitted flag in one byte; and `OUCH`
  indexes all of them as cargo-hold slots 17 to 20. A struct of five differently-encoded bytes has
  no operator to earn, and a flag or enum type would misdescribe two of them. The named bytes on
  `Commander` stay.

**AND `Colour` DID NOT SURVIVE THE PLAN'S OWN DESCRIPTION EITHER**, which is why it is worth
recording next to the three. The row asked for "scoped enums with the original values", and the
C64 build's colour constants are not colours: `RED`, `YELLOW`, `GREEN` and `WHITE` are four
multicolour PIXELS packed in a byte (with `BLUE`, `CYAN` and `MAG` all aliased to `YELLOW`), and
`RED2`, `GREEN2`, `YELLOW2`, `BLACK2`, `MAG2` and `BULBCOL` are screen RAM palette bytes holding TWO
colour indices each, two of them named for the wrong nibble. The thing that is one colour is the
VIC-II index, which the original never names at all. `Elite::Colour` is that index, and the two
constant families get their own slices rather than being forced into it (§8).

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

**Amended 2026-09-07, after M4-a and the M6-0 gate: five abstract classes remain, and the count is
final for M6.** The two that were blocked went when their blocker did — `SpawnChildEffects` with
M4-a's typed stage result, `ShipDrawEffects` when the interpreter learned to bank the I/O page
(M6-0-a-3) — and M6-0-h removed the two that had outlived their reason, `StartUpEffects` and
`ControlEffects`, by giving `ZEKTRAN`, `TITLE` and `DOCKIT` to the library. `SoundSink` stopped
being a port at M5-e-1: `Game` owns the SID write log and the executable drains it through
`Sounds()`. What is left is three platform ports — `Presenter`, `Keyboard`, `CommanderStore` — and
the text system's own two, `TextSink` and `ValueTokens`; `Ports` is eight references and `Game`'s
constructor takes the three (ADR-007, amended the same day).

**Three claims above did not survive the build.** `Step(InputFrame)` is three `Step`s taking a key,
because `FRCE` chooses between three routines rather than three branches of one. `Frame()`,
`Sounds()`, `StateHash()` and `Mode` were not built by M3-c: `Mode` is M4-d's, `Sounds()` M5-e-1's,
`StateHash()` M5-e-3's (library-native), and `Frame()` is served by `State().canvas`. And the executive decides
how many steps because it MUST: the count is floating point and the determinism guard forbids the
library one. ADR-007 §2 and §3 have the reasoning; the ownership and the replay hash are its §1 and
§4.

### §5 Control flow: pipelines with named stages

**Built by M4, 2026-09-07, and this section is amended from what was built rather than left to
predict it.** The flight frame, `LL9`, `TACTICS`, `DOCKIT` and `MLOOP`'s spawner are pipelines of
functions over a frame struct, each stage answering a typed result. Original bugs stayed ported
(ADR-001 §3, §6): no stage was allowed to be unable to express `SHPPT`'s stale read.

**Four things the paragraph above predicted did not survive the build, and the pattern in all four
is the same — the stage list was written from the routine's SHAPE and the routine's shape is not
what its 6502 control flow is.**

- **The frame's stages are `Contact`, `ScoopResult`, `DockingTest`, `Impact`, `Aim` and
  `KillOutcome`** (M4-a), and the finding that produced them was five booleans that were an
  unwritten state machine. Part 14's `BNE MA93` fall-through, which the port had been collapsing,
  is a branch again.
- **`LL9` is SEVEN stages, not five**, and they are the original's own part blocks rather than a
  graphics pipeline: `TestPresence`, `MeasureRange`, `ScaleShip`, `SelectFaces`, `ProjectVertices`,
  `OpenHeapRun`, `PushEdges`. `DrawShip` went 551 lines to 38 and the channel census can name a
  PART for the first time. The predicted `ScaledOrientation → FaceVisibility → ProjectedVertices →
  EdgeSelection → ClippedLines → HeapRun` reads like a renderer and `LL9` is not one.
- **`Decision` and `Apply` are not one pair but three different answers**, because the `bool`s they
  replaced were three different things. `TACTICS` answers a `Tactic` (`Steer`, `Done`, `Fatal`,
  `Docking`) because its `bool` was three outcomes in one costume; `DOCKIT`'s `bool` was a PHANTOM
  — no caller could act on it and the original reaches no `OOPS` and no `DEATH` — so it is `void`;
  and `MLOOP`'s spawner answers a `SpawnPass` over a `SpawnFrame` that carries §6.125's live carry
  across all four parts.
- **`LoopOutcome` is NOT retired, and `Game::Mode` was built anyway** (M4-d). ADR-007 §2 had said
  M4-d would retire it; the reason it cannot is recorded there rather than here, and the death
  sequence stays synchronous for the same reason.

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

**The M6-0 gate closed 2026-09-07** (Modernize.md §6 Phase M6, §8): the eight things the oracle
could pin and nothing would pin afterwards are done — the interpreter's port register and keyboard
matrix, a whole frame with an explosion in it, the replay reaching death and the escape pod, the
control codes compared through the dispatch, two fixture faults, a ruling on every routine that was
only ever trapped, a coverage instrument CI reads against the ledger, and a mutant floor. The
coverage review is that instrument's output, not a reading by eye, and its first run names four
gaps M6-a closes before anything is recorded.

### §8 Language level

C++20, by ruling: `std::span` with fixed extents for the codecs, `constexpr` codecs with
`static_assert` round trips, scoped enums, concepts where a helper must refuse the wrong bit type
(`ShipFlagBit`), `[[nodiscard]]` wherever a result carries a flag. C++23 is held; nothing in the plan
needs it.

**M5-b, 2026-09-07: every generated table is `constexpr` and its SHAPE is a `static_assert`.**
`tools/extract_tables.py` emitted `constexpr std::array` (the tool went with the original at
M6-f; the tables it wrote are checked in and are the port's own now) and `GameLogic/LookupTables.cpp` — a
translation unit that emits nothing — ties each table's length to the constant that INDEXES it
(`SHIP_TYPE_COUNT`, `SPRITE_DEFINITION_COUNT`, `Canvas::CELL_ROWS`, `SOUND_EFFECT_COUNT`,
`EQUIPMENT_ITEM_COUNT`), which is §6.8's rule for the whole ledger made mechanical. The assertions
read `std::tuple_size_v`, so they hold from the DECLARATION and the big tables stay in their own
`.cpp` rather than putting 160 KB of initialiser into every translation unit that wants a font. What
the bytes ARE is the assembler's; what shape they have to be is the port's, and that is the half a
compiler can hold.

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
| M2 Calling conventions | Built 2026-09-06 (M2-a, M2-b, M2-c-1/2/3, M2-d). The kernel takes values and returns structs; the frame's `Q` and `K2`'s bottom byte are the two channels that stay, each named (Modernize.md §8, R22 closed) | §3 above; Modernize.md §4.3 |
| M3 Ownership | Built 2026-09-06/07 (M3-0, M3-a, M3-b, M3-c, M3-d) | §4 above, amended from what was built; ADR-007 |
| M4 Control flow | Built 2026-09-07 (M4-a, M4-b, M4-c-1/2/3, M4-d) | §5 above, amended from what was built |
| M5 Polish and the ledger | Built 2026-09-07 (M5-a to its acceptance plus `SoundEffect` and `Colour`, M5-b, M5-c, M5-d). §2 carries the three strong types that were refused and why; §5 and §8 are amended from what was built | §2, §5, §8 above; Modernize.md §6 M5, §8 |
| M6 Detach | M6-0 gate closed 2026-09-07 (eight rows, §8 of Modernize.md); M6-a next, opening on the four gaps the coverage instrument named | §7 |
