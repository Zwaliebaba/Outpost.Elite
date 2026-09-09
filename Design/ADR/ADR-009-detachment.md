# ADR-009 — Detachment: what pins the port now the original is gone

**Status:** **Accepted** · 2026-09-08, written at M6's close from what was actually built. It
records a DELETION, and the reasoning under it is an owner ruling taken against this corpus's
recommendation — which is why the argument the corpus made is written down here beside the ruling
rather than quietly dropped.
**Depends on:** ADR-001 (fidelity — unchanged; §5 restated at M6-f), ADR-002 (the numeric model,
which is what the port keeps of the original's arithmetic), ADR-007 (state ownership — `Universe`
is what the surviving digests fold), ADR-008 (the picture, which never had an oracle and shows what
verification without one looks like)
**Amends:** ADR-003 §1 and §4, which describe an oracle that no longer exists, and ADR-003 §2,
whose two golden canvases went with it. §3 is untouched and is now the centre of the method.
**Feeds:** every future change to `GameLogic/`, which is now judged by the port's own tests.

## Context

For six phases the port was written against the original: `Cpu6502` ran the assembled C64 game
beside the C++, and 337 tests compared them a routine at a time — the arithmetic exhaustively, the
drawing on whole bitmaps, the flight loop frame by frame. That is what ADR-003 §1 decided and what
made "faithful" a measurement rather than a claim.

Phase M6 was always going to end it, because the plan's own goal (§1 R-a) is a C++ program that
builds and reads on its own. The question was how. M6-a built a recorder and measured the corpus:
3.2 million calls, 98.5% of them with an input no earlier call had, 222 MB as a naive fixture and
about 25 MB once the sweeps folded their answers into digests (§1 R-f, R-g). M6-b was scoped to
build that fixture.

**The owner asked "I want to get rid of the oracle. Why bother?" on 2026-09-08.** The corpus
recommended building the fixture and said why: a fixture keeps the tests as regression tests, and
without one every behaviour the original was still pinning becomes pinned by nothing. The owner
ruled the other way after reading that argument, and the ruling stands. What follows is what the
ruling produced.

## Decision

### §1 The original is gone, at the tip, entirely

`Cpu6502`, `Oracle`, `OracleImage`, `OracleLabels.h`, `Upstream/`, `MasterFile/`, `.gitmodules`,
`labels.py`, `c64_source.py`, `extract_tables.py`, `golden_diff.py` and the BeebAsm step on both CI
legs are deleted (M6-b-5 to M6-f). **A fresh clone builds and runs the whole suite with a compiler
and nothing else.** No fixture was recorded: there is nothing between the port and its own tests.

What the repository still carries of the original is its DATA — the font, the token tables, the
sine and arctangent tables, the ship blueprints, the dashboard image — as generated `constexpr`
arrays in `GameLogic/`. The game cannot run without them and they cannot be regenerated here any
more. That is the accepted residual exposure (§1 R-c of the plan, Risk R1), and it is deliberate:
the tables ARE the inheritance.

### §2 What pins behaviour now

The suite went from 469 tests to 131. What is left is not a subset of the old method but a
different one, and it is worth naming instrument by instrument, because "131 tests" on its own
says nothing about what they can see.

| Instrument | What it pins | Where |
|---|---|---|
| **The replay digests** | COMPOSITION: three scripted flights — a launch-to-docking, a death, an escape pod — each hashed at every hundredth step and at every turn of the script, against a recorded table. A refactor that keeps every routine correct and changes how they compose fails here and nowhere else | `FlightReplayTests` |
| **The state hash and its cell walk** | That the digest above SEES the state: `Elite::HashState` folds `Universe` in declaration order, and a table of cells is walked one byte at a time with the requirement that each moves the hash -- 1,489 do and 7 are inert setters the table declares as such. A field the fold forgot is named, not counted | `StateHashTests`, `StateCells.cpp` |
| **The picture layer** | Fifty-three tests over the 640×400 surface, which never had an oracle and never could (ADR-008): every twinned routine draws on both surfaces, the glyphs match, the layouts land where their tables say | `Picture*Tests`, `check_twins.py` |
| **The port's own invariants** | The assertions that were never comparisons: the scaled divide really divides, `TIDY` reaches all three of its shapes, the economy gradient pushes price and quantity opposite ways, a station survives a launch, the raster split alternates | scattered through the trimmed suites, one or two per file |
| **The shell** | The viewport arithmetic, the palette, the step planner, the key map and the settings file — all of it the platform's rather than the game's | `ShellTests` |
| **The mutation corpus** | Whether a test would NOTICE. **Degraded, and §4 says how far** | `mutate.py`, `mutants.json` |
| **The determinism guard** | No clock, no randomness, no float, no file or registry access, no Win32 call in `GameLogic/` | `check_gamelogic.py` |
| **A person** | Every docked screen legible, the cadence right, the keys where a player expects. This half was always a human sign-off (ADR-003 §3) and it has not changed; what has changed is that it is now a larger fraction of the whole | the plan's 2e row |

### §3 What a recorded digest is, and what changing one means

There is no fixture in this tree. The nearest thing is the **replay record**: three tables of
`{step, state}` in `FlightReplayTests.cpp`, taken from a flight the port ran. Rule 1 of the plan
governs them and is unchanged by the detachment:

> A changed digest is a changed game. The slice that changed it has found a defect or introduced
> one, and the journal entry says which.

A record may be re-taken in exactly two cases, and the journal entry names it: the digest is
deliberately WIDENED (more cells in the fold), or a defect in the port is found and fixed and the
record follows the fix. Never for a refactor.

**What is different now is what a re-take costs.** While the original was here, a digest that moved
could be adjudicated: run the routine on both machines and see which was right. That is gone. A
moved digest is now judged by reading the code, which is a weaker instrument and an honest one to
name.

### §4 What was given up, stated rather than glossed

This is the section this ADR exists for. A document that recorded only what remains would be a
worse record than none.

- **337 tests are deleted.** The arithmetic sweeps (65,536-case exhaustive comparisons of every
  routine in the kernel), the drawing (ships, planets, the sun, the ball and line heaps, the
  explosion), the whole flight frame compared byte for byte at four entry points, the tactics, the
  missions, the spawns, the sound and music, the tokens and extended tokens, the Trumbles, the
  charts, the market, the scanner, the dashboard, the cycle model. Every one of them was a
  measurement against the machine that defines the answer. None of them can be re-run, here or
  anywhere, without re-obtaining the original.
- **Risk R19 is realised in full.** It said: "a recorded fixture pins only what the tests asked
  while the original was here; a behaviour no test reached before M6-b is unpinned for ever." No
  fixture was recorded, so the sentence is stronger than its own wording — a behaviour no
  SURVIVING test reaches is unpinned, which is most of them.
- **The mutation corpus lost most of its subjects, and 89 of its 97 mutants were then deleted**
  (M6-b-8, owner ruling 2026-09-09). M6-0-g built it for exactly this moment, on the argument that
  after detachment "a mutant is the only instrument that says whether a test would NOTICE" — and
  what it was measuring was the 337 tests. Eight of the thirteen units lost every `TEST_CLASS`
  their filters named; the other five were run, and 18 of their 26 mutants survived, `shipmove`
  and `flight` losing every one. Rule 3 says a mutant is re-anchored and never dropped, and for
  these there was nothing left to anchor to: the owner ruled they be deleted rather than recorded
  as expected survivors, so that what remains is a gate rather than a to-do list. **Eight mutants
  in four files remain, all caught; the floor M6-0-g set went from fourteen files to three, the
  first time it has shrunk and it was written never to.** What the corpus still speaks for is the
  raster split, the canvas blit, the explosion cloud's seeding and the `Game` object.
- **A selftest can rot without anything saying so, and four of five had.** Each unit carries one
  unmissable mutant whose survival means the harness itself is broken. `ra-selftest` zeroed a
  register that only the deleted comparison read; because a surviving selftest stops the run by
  design, the corpus could not be measured at all until the flags were lifted for one pass. **A
  selftest is only unmissable relative to the tests that exist**, and nothing checked that.
- **The two golden canvases went** with the routines that drew them into the interpreter, and
  `golden_diff.py` with them.
- **What did NOT change**: the port's behaviour. Not one line of `GameLogic/` was touched by any
  slice of M6-b or M6-f, and the replay digests are the same numbers they were before the
  deletion, to the bit, in all three records. That is the evidence that the game is the game it
  was, and it is the only evidence there is.

## Alternatives considered

- **Record the fixture (§1 R-f and R-g, the plan's own scope for M6-b).** ~25 MB of recorded
  answers, the 73 heaviest tests folding into digests, `RecordedOracle` serving the rest. It keeps
  all 337 tests as regression tests — they would no longer prove fidelity to the original, but
  they would still catch a port regression. **Rejected by the owner**, after the corpus recommended
  it, on the ground that the fixture is not worth carrying. M6-b-4 measured what it would have
  carried: 63.9% of the recorded bytes appear verbatim in the assembled image, in runs of up to two
  thousand contiguous bytes.
- **Freeze each test's expectation as a literal.** Cheaper than a fixture — one digest per test
  rather than per call — and it was not put to the owner, because the ruling was to delete the
  tests and not to re-express them. It remains available if the coverage loss proves unacceptable:
  nothing in this ADR prevents a future phase from writing port-native expectations for the
  behaviours §4 lists, and doing it from the port's own answers is a different and weaker claim
  than doing it from the original's, which is why it would need its own ruling.
- **Keep the oracle indefinitely.** This is what the plan's goal forbids: a C++ program that needs
  a 6502 interpreter, a submodule and an assembler to test itself is not detached, and every phase
  of M6 exists to end that.

## Consequences

- **The suite runs in half a minute** where it took a minute, on any machine with a C++20 compiler,
  with no submodule, no assembler and no assembled game. That is the whole of what was bought, and
  it is real: the loop is faster and the tree is simpler.
- **CI lost two jobs' worth of setup** — the BeebAsm build and cache, the assemble step, the
  submodule checkout — and one check, the source resolver.
- **New behaviour is now judged by the port's own tests, and the bar for adding one has risen.**
  Where a slice used to be able to write "compare it against the original", it must now say what
  the behaviour IS and assert that. §2's fourth row is the shape to copy: the assertions that
  survived the deletion are exactly the ones that were written that way in the first place.
- **A defect that was present before M6-b-5 and reached by none of the 131 surviving tests will
  not be found by this repository.** It would have been found by the 337, if a test reached it.
  That is the cost, and it is accepted rather than mitigated.
