# ADR-007 — State Ownership and the Replay Hash

**Status:** Accepted · 2026-09-07, written at Phase M3's close from what M3-a, M3-b and M3-c
actually built rather than from what §4.4 planned — the two differ in three places and each is
recorded below with its reason.
**Depends on:** ADR-001 (fidelity — the game does not change), ADR-002 (numeric model — every byte
keeps its width), ADR-003 (the oracle is the judge until M6), ADR-004 §1 (which drew this seam
"from day one"), ADR-006 §4 (which planned it, and which this document amends)
**Feeds:** M4's stages, M5's polish, M6's detach; `tools/modernize_ratchet.json`

## Context

M3 opened on a port whose architecture was the 6502's memory map. Game state was thirteen members
of a struct in `Outpost/Main.cpp` and twenty-two of `Outpost/FlightSession`, split by which screen
had needed a byte first. Twenty-two abstract "effects" seams stood between the library and the
executable. Eight hundred lines of dispatch — `TT102`'s actions, `M%`'s outcomes, `DOENTRY`'s six
mission exits, `FREEZE`, the chart draw and both halves of the main loop — lived in the one file no
Linux runner compiles, which meant the only thing that could tell you they still worked was the
Windows job and a human looking at a screen.

M3 closed with `Elite::Universe` owning every byte of game state, eleven interfaces collapsed to
four ports and five direct calls, and `Elite::Game` owning the dispatch. `Outpost/Main.cpp` is 256
lines and every one of them is the platform. This ADR records the ownership as built, the three
places it departs from §4.4, and what the replay hash does and does not cover.

## Decision

### §1 Four owners, and the rule that decides between them

```
Outpost::App        the window, the swap chain, the audio device, the files, the clock   -- Outpost/Main.cpp, 256 lines
Elite::Game         the dispatch, the text chain, the ports, and eight bytes             -- GameLogic/Game.h
Elite::Universe     every byte of game state                                             -- GameLogic/Universe.h
Elite::Ports        eleven references: four the library's own, seven the platform's      -- GameLogic/Ports.h
```

**THE RULE IS THE DETERMINISM GUARD'S, and it is mechanised rather than agreed.** `GameLogic` may
not touch a clock, a float, a file or a Win32 call (AGENTS.md §5, `tools/check_gamelogic.py`), and
that is exactly the line M3-c's split fell on. Anything that needs one of those four is the
executable's; everything else is the library's. No judgement is required at the boundary and none
was exercised: the split was decided by running the check.

**`Universe` IS A PLAIN AGGREGATE AND THE THREE PROHIBITIONS ARE THE POINT.** No reference member,
no virtual, no printer. It copies, so a test can take a snapshot; it has no vtable, so its layout
is its fields; and `UniverseImage` can hash it without knowing what else is in the program. Those
three properties are what §4 below is built on, and a field that would cost one of them does not go
in — which is why the text machinery is not there. `TokenPrinter`, `CharacterPrinter`,
`TextPrinter`, `StateTokens` and `ExtendedTokenPrinter` are objects with virtual calls in them and
they live on `Game`.

**`Ports` IS ELEVEN REFERENCES AND THE SPLIT INSIDE IT IS NOT ARBITRARY.** Four are the library's
own — the token printer, the character printer, the sink and the extended printer, all built by
`Game` over the universe — and seven are the platform's: `ShipDrawEffects`, `SpawnChildEffects`,
`SidWriteLog`, `StartUpEffects`, `Presenter`, `Keyboard` and `CommanderStore`. `Game`'s constructor
takes the seven and builds the four, which is the whole of what "the executable supplies the
platform" means in this port. `ControlEffects` arrives beside them rather than inside `Ports`,
because `DOCKIT` is passed separately at every call site (§4.5) and adding it would push
`aggregate-refs` up, which rule 5 forbids.

### §2 Three `Step`s, not one, and the count of passes stays outside

§4.4 planned `void Step(const InputFrame&)`. What is built is `Step(key) -> bool`,
`StepDocked(key)` and `StepPaused(key)`, and the reason is the original's own shape.

**`FRCE` CHOOSES BETWEEN THREE ROUTINES, NOT THREE BRANCHES OF ONE.** `LDA QQ12 / BEQ P%+5 /
JMP MLOOP / JMP TT100` is a jump to one of two entry points, and `FREEZE` is a third that `DK4`
reaches and does not return from. Folding them into one method behind a mode byte would be
inventing a machine the original does not have — and `Mode` is exactly that machine, which M4-d
builds when it retires `LoopOutcome`. Until then the caller's choice IS the game's, and three
methods say so.

**AND THE COUNT OF PASSES CANNOT BE HERE.** §4.4 and Modernize.md's M3-c row both say `Advance`
moves into the library. It could not: it turns elapsed seconds into a count of passes over ADR-005
§3's accumulator, and that is floating point by construction. So it split — `Outpost::PlanSteps`
and the seconds stay in the executable, and one `Step` is one pass. That is a better division than
the row described: how many passes a wall-clock interval is worth is a property of the DISPLAY, and
the passes are the game's.

`Step` answers a `bool` because the loop it came from expressed "stop stepping" as a `return` out
of the whole batch — `M%` leaving the flight half, or the pause key freezing it. A caller that
ignored the answer would run a docked pass through the flight loop.

### §3 The eight bytes that were on `Game`, seven of which are in `Universe` now

`Game` held eight bytes that were loose members of `Main.cpp`'s composition struct:

| Field | 6502 | What it is |
|---|---|---|
| `m_crosshairStep` | `TT17`'s X and Y | what the scan leaves for the dispatch a call later |
| `m_jumpTarget` | `safehouse` | the seeds the countdown is running towards |
| `m_jumpDistance` | `QQ8` | the distance `hyp` measured and `TT18` spends |
| `m_joystickGeometry` | `JSTGY` | the joystick's y-inversion |
| `m_joystickEnabled` | `JSTE` | the joystick's enable |
| `m_musicSwitchWas` | `MUTOKOLD` | what `MUTOKCH` saw last |
| `m_soundDisabled` | `DNOIZ` | non-zero disables the sound |
| `m_paused` | — | the port's own; the original FREEZES in a loop instead |

**SEVEN OF THE EIGHT HAVE A 6502 NAME, WHICH MEANS THEY ARE GAME STATE, WHICH MEANS §4.4's OWN RULE
PUTS THEM IN `Universe`.** They were not there when this ADR was first written, and it recorded that
as a defect of the build rather than a decision: they were loose in the executable when M3-c moved
the dispatch, and carrying them across with it was the smallest change that compiled. **They are in
`Universe` as of 2026-09-07** (§8), which is the follow-on this section asked for.

**MOVING THEM DID NOT MOVE THE DIGEST, and that is the second half of the finding.** `Hash` walks
`UniverseImage`'s table of cells, not the struct's bytes, so a field added to `Universe` is not
hashed until a cell names it. Giving them cells is a deliberate widening under rule 1's first case
and is still open — so §5's gap is relocated rather than closed, and it is now one edit away from
closing instead of a refactor away.

`m_paused` is the exception and stays. The original does not have the byte — `FREEZE` is a loop
that reads the keyboard and does not return until CLR/HOME — and a windowed program cannot stop
pumping messages, so the freeze is a state the outer loop is in rather than a loop inside it. That
is the same trade `PlanSteps` makes for the frame rate (ADR-005 §3), and it is a port decision with
no 6502 byte behind it.

### §4 The replay hash: over the image, not over the structs

`Tests/GameLogicTests/UniverseImage` is one table of cells mapping a port field to a 6502 label,
and four walks over it:

```
Materialise   port -> 6502 memory        what `Mirror` was
Compare       6502 memory vs port        what `CompareState` was, as data rather than assertions
Absorb        6502 memory -> port        the M0-c replay reads a state back
Hash          FNV-1a over the image      the replay digest, layout-independent by construction
```

**THE HASH IS OVER THE IMAGE AND THAT IS THE WHOLE DESIGN.** A slice that turns thirty-seven bytes
into a typed `Ship` changes every struct in the program and no byte of the image, so the stored
digests survive the refactor and a changed digest means a changed game. It is the only instrument
in the project that can tell a refactor from a rewrite, because the per-routine oracle compares one
routine's bytes and cannot see an ordering change between two of them (risk R14).

**WHEN THE RECORD MAY MOVE, and it is two cases only** (Modernize.md §5 rule 1, §1 R-e): a
deliberately widened digest, named in the journal; or a defect in the port found and fixed, with
the record re-taken and the defect named. A record that moves with no defect named is a refactor
that changed the game, and that is the failure the record exists to produce.

**WHAT M3 DID TO IT IS THE EVIDENCE THIS ADR IS FOR.** The record moved twice, both in M3-b and
both for defects the slice found: `NOISE2` returning the sustain it went in with rather than the
flag byte it wrote (M3-b-2a), and `Music.cpp` missing the `SETL1` brackets around `stopat` and
`BDENTRY` (M3-b-2b). It has not moved since. Between M3-b-3a and M3-c inclusive — eleven commits,
eleven interfaces collapsed to four ports, five seams removed, eight hundred lines of dispatch
moved from the executable into the library, `Main.cpp` from 1,197 lines to 256 — **the digest did
not change**. That is what "no behavioural change" means when it is measured rather than asserted.

### §5 What the hash does not cover, named rather than assumed

- **The seven bytes of §3.** They are in `Universe` now and still not cells in the image, so a slice
  that broke `MUTOKOLD` or `QQ8` would not move the digest. Giving them cells is the widening under
  rule 1's first case, and it moves the record for every checkpoint — which is why it is a decision
  rather than a tidy-up, and why it is taken with the one below rather than separately.
- **The docked half.** The replay is a FLIGHT replay. The docked side is `DockedSessionTests`'
  transcript — a character stream and a screen — which is a different instrument and not a hash.
- **`Ports` and the text objects.** They are not state and are not hashed. What they do reaches the
  universe or the canvas, and that is where it is compared.
- **The replay does not drive `Game`, and pointing it at one changes the flight.** `FlightReplayTests`
  composes `FlightPort` and calls the library's routines directly, as it has since M0-c —
  `FlightPort::Step` is a second transcription of `M%`, `MLOOP`'s head, the spawner, part 5's tail
  and the keyboard scan, beside `Game::Step`'s. **It was measured on 2026-09-07 and the answer is
  not a tidy-up** (§8): a `FlightPort` built over an `Elite::Game` starts from `NA%` rather than a
  zeroed commander, and its text chain runs the value tokens and the control codes rather than
  deferring them. The scripted flight becomes a different flight — 1,170 steps ending `Docked`
  becomes 2,589 ending `Died` — so the record moves for a reason that is neither of rule 1's two
  cases as written. Until that is decided the digest is a statement about the ROUTINES and not about
  the object that dispatches them, and `GameTests` is what covers the object.

### §6 The rule that follows from all of it

**A byte of game state goes in `Universe`.** If it cannot, the reason is written down where it
lives and named here. There are three such reasons on the tree today and no others: it is not the
game's (the eight of §3 are, and are the defect); it would cost `Universe` one of its three
properties (the text objects); or it is the platform's by the determinism guard (the clock, the
seconds, the files, the device).

## Consequences

- **`check_outpost.py`'s surface fell with the dispatch.** The executable reaches 71 distinct
  `Elite::` names where it reached 205 when M3 opened, so R15 — the app breaking on a type change
  no Linux leg can see — is a third of the risk it was. Seven Windows-only breaks during M3 each
  added a half to that check; the eighth has a much smaller file to hide in.
- **`Elite::Universe` is still the composition root's member, not `Game`'s.** §4.4 planned
  `Universe m_universe` and what is built is `Universe&`. `Outpost::FlightSession` binds the
  universe at construction and answers three of the seams `Game`'s `Ports` needs, so whichever of
  the two is built first needs the other. Two of those three seams are already scheduled to go —
  `ShipDrawEffects` when the emulator models the banking §6.108 found, `SpawnChildEffects` in M4-a —
  and what is left of that class afterwards is `DOCKIT`. When it goes, the member moves across and
  the composition root stops holding any game state at all.
- **`Frame()`, `Sounds()`, `StateHash()` and `Mode` are not built.** The first two are the
  executable's draw and drain and have no library caller yet; `StateHash` wants `UniverseImage`'s
  hash, which lives in the test tree and would have to move to ship; `Mode` is M4-d's, for §2's
  reason. Each is a named follow-on rather than a gap discovered later.
- **Two seams survive M3 deliberately** — `TextSink` and `ValueTokens` are the text system's own
  polymorphism, not platform (Modernize.md §4.5) — **and two because a comparison is blocked**:
  `ShipDrawEffects` on the emulator's flat memory, `SpawnChildEffects` on M4-a's typed stage result.
  `effects-seams` therefore closes M3 at nine rather than at four, and the four numbers are not a
  shortfall against the same target.

## Status

| Claim | State | Recorded in |
|---|---|---|
| `Universe` owns every byte of game state | Built; §3's seven moved in 2026-09-07 and are not yet hashed | Modernize.md §8, M3-a and the follow-on |
| Twenty-two seams to four ports | Built; nine abstract classes remain, four of them named above | §8, M3-b |
| `Game` owns the dispatch and both loops | Built | §8, M3-c |
| `Universe` owned by `Game` | Not built — see Consequences | §8, M3-c |
| `Frame`, `Sounds`, `StateHash`, `Mode` | Not built | §8, M3-c; M4-d for `Mode` |
| The replay digest survives M3 | Verified — unchanged across ten commits | §4 above |
