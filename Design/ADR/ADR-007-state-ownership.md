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
Elite::Ports        eleven references at M3's close: four the library's own, seven the platform's   -- GameLogic/Ports.h
                    (eight since M6-0-h, 2026-09-07: four and four -- see the amendment under §1)
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

**Amended 2026-09-07: `Ports` is eight references, and `ControlEffects` is gone.** The platform's
seven became four as their reasons expired — `SpawnChildEffects` at M4-a-1, `SidWriteLog` at M5-e-1
(the log is `Game`'s, drained through `Sounds()`), `ShipDrawEffects` at M6-0-a-3 once the
interpreter banked the I/O page, and `StartUpEffects` at M6-0-h-2 once `TITLE` ran on both machines
— so `Game(Presenter&, Keyboard&, CommanderStore&)` takes three and builds the library's four.
`ControlEffects` went at M6-0-h-3: `DOKEY` calls `DOCKIT` itself. `effects-seams` is five:
`Presenter`, `Keyboard`, `CommanderStore`, `TextSink`, `ValueTokens`.

### §2 Three `Step`s, not one, and the count of passes stays outside

§4.4 planned `void Step(const InputFrame&)`. What is built is `Step(key) -> bool`,
`StepDocked(key)` and `StepPaused(key)`, and the reason is the original's own shape.

**`FRCE` CHOOSES BETWEEN THREE ROUTINES, NOT THREE BRANCHES OF ONE.** `LDA QQ12 / BEQ P%+5 /
JMP MLOOP / JMP TT100` is a jump to one of two entry points, and `FREEZE` is a third that `DK4`
reaches and does not return from. Folding them into one method behind a mode byte would be
inventing a machine the original does not have; the caller's choice IS the game's, and three methods
say so.

**AMENDED BY M4-d (2026-09-07), IN TWO PLACES.** This paragraph said `Mode` was "exactly that
machine" and that M4-d would build it "when it retires `LoopOutcome`". Both halves were wrong.

`Game::Mode` IS built, and it is not the invented machine: `Flight` and `Docked` are `QQ12`'s own
two values and `Paused` is the port's freeze, which §3 already records as a byte the original does
not have. It does not replace the three `Step`s — it CHOOSES between them, which is what `FRCE`
does — so this paragraph's conclusion stands and only its prediction does not. What `Mode` removed
is an ordering rule the executable was keeping by hand: the pause test has to come above the `QQ12`
test, and that is now one value rather than two calls in a required sequence.

`LoopOutcome` is NOT retired and should not be. `Continued` is not a mode — it is "the frame
finished, go round again" — and `Docked`, `Died` and `Escaped` are TRANSITIONS rather than states.
It is the return value of `BeginFlightFrame`, `MoveEveryShip`, `EndFlightFrame` and `MainFlightLoop`,
which have to say which of `DOENTRY`, `DEATH` and `ESCAPE` they left by; a mode cannot carry that,
because by the time the mode has changed the routine has already returned. The two answer different
questions and both are needed.

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
hashed until a cell names it. **The cells were added on 2026-09-07 (M5-a-6) and §5's gap is closed**
— and closing it corrected the count above. The seven were never one gap: **five of them can have
cells and now do** (`safehouse`, `QQ8`, `JSTGY`, `JSTE`, `MUTOKOLD`, beside the `DNOIZ` cell that
already existed), **one was a duplicate** — `m_soundDisabled` was a SECOND `DNOIZ` beside
`Sound::soundOff`, which is the defect M5-a-5 found and deleted — and **one cannot be hashed at
all**: `m_crosshairStep` is what `TT17` leaves in X and Y, and REGISTERS have no address for `Where`
to look up, so there is nothing for a cell to name. A cell is a port field paired with a 6502
LABEL; state that lives in a register between two calls is outside what this instrument can measure
by construction, and §5 records it as such rather than as an open edit.

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

It moved a third time on 2026-09-07, when the replay stopped stepping its own transcription of the
flight pass and started stepping `Elite::Game` (§5). Every checkpoint moved and the flight did not:
1,170 steps ending `Docked`, before and after. Three defects came out of the fixture with it and the
journal names all three — which is the second case working exactly as written, on the one instrument
that could have hidden them.

### §5 What the hash does not cover, named rather than assumed

- **~~The seven bytes of §3~~ — five of them are cells since 2026-09-07 (M5-a-6), and the arithmetic
  is the finding.** This section had named seven bytes as one gap and they are three kinds.
  `safehouse`, `QQ8`, `JSTGY`, `JSTE` and `MUTOKOLD` have cells now, taken as the widening under
  rule 1's first case — the only re-take of the record so far under that case, and the record moved
  for every checkpoint while the flight did not (1,170 steps ending `Docked`, and no line of
  `GameLogic/` changed at all). `DNOIZ` needed nothing: it had a cell already, and the field §3
  listed beside it was a DUPLICATE of it — M5-a-5's finding, deleted. And **`crosshairStep` cannot
  have a cell and this is now the standing statement about it**: it is `TT17`'s X and Y, a pair of
  REGISTERS, and a cell is a port field paired with a 6502 label. There is no label. What the scan
  leaves for the dispatch a call later is pinned by `KeyboardTests` comparing `TT17` against the
  oracle, and by nothing in the digest — the one piece of flight state the replay is structurally
  blind to, named here rather than left to be rediscovered.
- **The docked half.** The replay is a FLIGHT replay. The docked side is `DockedSessionTests`'
  transcript — a character stream and a screen — which is a different instrument and not a hash.
- **`Ports` and the text objects.** They are not state and are not hashed. What they do reaches the
  universe or the canvas, and that is where it is compared. **Except for nine bytes, and M5-e-2
  (2026-09-07) found the digest reading the wrong copy of them.** `QQ17` and `DTW1`–`DTW8` live in
  the two printers, not in `Universe`, and the image reads them through the printers — which, for
  the replay, were the test wrapper's idle pair and not `Game`'s. The image takes them as an
  argument now (`Beside`) and the replay passes `Game`'s; the record moved on every checkpoint and
  the flight on none. That the bytes have labels and cells makes them game state by §6's rule, and
  M5-e-2b moved `DTW1`–`DTW8` into `Universe` the same day (`sentences`; the vtable reason is the
  printers', not their bytes'). `QQ17` was M5-e-2c's, the same day: it had been two bytes on the
  tree (`TokenPrinter::m_caseFlags` and `TextState::caseFlags`, stored in step at seven sites and
  separately at nine), and the token printer binds to `TextState` now with the struct's byte the
  only one.
- **~~The replay does not drive `Game`~~ — it does, since 2026-09-07, and this entry is what it
  found.** `FlightReplayTests` composed `FlightPort` and called the library's routines directly from
  M0-c until then, so `FlightPort::Step` was a second transcription of `M%`, `MLOOP`'s head, the
  spawner, part 5's tail and the keyboard scan sitting beside `Game::Step`'s, and the digest was a
  statement about the ROUTINES rather than about the object that dispatches them. It is one
  `game.Step(0u)` now, and the zero is a key — `TT102` dispatches every pass, which the
  transcription did not do. Three defects in the fixture came out with it: it built `Ports` with the
  value-token and control-code seams null where the app's are wired, it never ran `NA%` so the
  scripted flight was flown by a commander of all zeros, and it held a second `QQ12` beside
  `Universe::dockedFlag`. All three are rule 1's second case; the record is re-taken and every
  checkpoint moves. **The flight itself does not** — 1,170 steps ending `Docked`, before and after.
  This ADR said on 2026-09-07 that it became 2,589 ending `Died`; that measurement was taken before
  the duplicate `QQ12` was found and is wrong. §8 corrects it in full.

### §6 The rule that follows from all of it

**A byte of game state goes in `Universe`.** If it cannot, the reason is written down where it
lives and named here. There are three such reasons on the tree today and no others: it is not the
game's (`m_paused`, which no 6502 byte backs); it would cost `Universe` one of its three properties
(the text objects — and since M5-e-2b that reason covers the OBJECTS and not their bytes: `DTW1`–`DTW8`
are `Universe::sentences`, bound into `CharacterPrinter` by reference the way `TextPrinter` binds
`text`, and `QQ17` is `text.caseFlags` alone since M5-e-2c); or it is the platform's by the determinism guard (the clock,
the seconds, the files, the device). **And a byte in `Universe` gets a cell in `UniverseImage`, unless it has no
6502 label to pair with** — which on the tree today is `crosshairStep` and nothing else (§5).

## Consequences

- **`check_outpost.py`'s surface fell with the dispatch.** The executable reaches 71 distinct
  `Elite::` names where it reached 205 when M3 opened, so R15 — the app breaking on a type change
  no Linux leg can see — is a third of the risk it was. Seven Windows-only breaks during M3 each
  added a half to that check; the eighth has a much smaller file to hide in.
- **`Elite::Universe` is `Game`'s member since M5-e-2 (2026-09-07), as §4.4 planned; it was the composition
  root's from M3-c until then.** What held it there was not the seams this bullet once blamed but a
  construction-order cycle: both sessions bound the universe at construction and `Game` needs both at its
  own. Waiting for the seams to go would not have broken it — `GameShell` answers three ports `Game` cannot
  do without — so the sessions take the universe afterwards instead (`AttachUniverse`, the shape the four
  `Attach`es already had), and the composition root holds no game state at all.
- **`Sounds()` is built (M5-e-1, 2026-09-07) and `Frame()` is recorded rather than built** (`Mode` is, since M4-d — see §2).
  `Game` owns the SID log now and the executable drains it through `Sounds()`/`ClearSounds()`, where until then
  the executable owned the log and handed the game a reference to write into — the one place the app reached
  INTO library state. `Frame()` would be an alias of `State().canvas`, which the executable already reaches, so
  §4.4's line is served rather than built; `StateHash` wanted `UniverseImage`'s
  hash, which lives in the test tree and would have to move to ship — **built M5-e-3 as
  `Elite::HashState` instead**, a library-native fold over `Universe` recorded beside the label hash
  until M6-f, with `StateHashTests` holding the fold to the label table's cells; `Mode` was M4-d's,
  for §2's reason. Each was a named follow-on rather than a gap discovered later.
- **`Universe::picture` is the one field the state hash excludes, and by design** (2026-09-08, the
  resolution track). It is the 640×400 rendering of the same frame that `ScreenPresenter` uploads —
  a SECOND drawing of what the canvas already holds, produced by twins the resolution slices keep
  changing. Folding it would re-record every replay table on a thinner sun or a redrawn crosshair,
  none of which is a change to the game. What proves the exclusion is not a hole is §8.4's replay:
  it runs with the twins present and absent and requires the same digest, so a twin that leaked
  into the game would fail even though the hash cannot see the surface it drew on. The reason is
  written out in [Design/Resolution.md](../Resolution.md) §3.4 and beside the fold in
  `StateHash.cpp`. `Universe::screenLayout` is excluded with it, for the same reason: it decides
  where that surface puts text and nothing the game does.
- **Two seams survive M3 deliberately** — `TextSink` and `ValueTokens` are the text system's own
  polymorphism, not platform (Modernize.md §4.5) — **and two because a comparison is blocked**:
  `ShipDrawEffects` on the emulator's flat memory, `SpawnChildEffects` on M4-a's typed stage result.
  `effects-seams` therefore closes M3 at nine rather than at four, and the four numbers are not a
  shortfall against the same target. **Both blocked ones went when their blockers did** (M4-a-1,
  M6-0-a-3), `StartUpEffects` and `ControlEffects` in M6-0-h, and `SoundSink` became memory `Game`
  owns (M5-e-1): five remain, the two text seams and three platform ports (2026-09-07).

## Status

| Claim | State | Recorded in |
|---|---|---|
| `Universe` owns every byte of game state | Built; §3's seven moved in 2026-09-07 and are not yet hashed | Modernize.md §8, M3-a and the follow-on |
| Twenty-two seams to four ports | Built; five abstract classes remain since M6-0-h (2026-09-07): three platform ports and the text system's two | §8, M3-b; §1's amendment |
| `Game` owns the dispatch and both loops | Built | §8, M3-c |
| `Universe` owned by `Game` | Built 2026-09-07 (M5-e-2) — see Consequences | §8, M3-c, M5-e-2 |
| `Frame`, `Sounds`, `StateHash` | `Sounds` built (M5-e-1); `Frame` served by `State().canvas`; `StateHash` built library-native (M5-e-3) | §8, M3-c, M5-e |
| `Mode` | Built 2026-09-07 (M4-d); `LoopOutcome` deliberately kept beside it | §2 above; §8 |
| The replay digest survives M3 | Verified — unchanged across ten commits | §4 above |
| The replay drives `Game` | Built 2026-09-07; record re-taken for three fixture defects, 0 steps moved | §5 above; §8 |
