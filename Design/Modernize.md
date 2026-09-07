# Modernize — from a faithful transcription to a modern C++ codebase

**Status:** **Accepted in scope · opened 2026-09-06, §1's questions ruled the same day** (all eight, and a
ninth the owner added: the port is DETACHED from the original at the end — the oracle, the assembler
source, the labels in the code and the assembly in the comments all go, §6 Phase M6). **The gate ADR-001
§4 set for phase 6 is met**: every oracle
suite, every whole-bitmap comparison and the docked replay are green on the faithful build
(<!--count:tests-->395 tests, oracle present), all <!--count:checks-->sixteen repository checks pass,
and every recorded mutant is caught or a proved equivalent (plan §6.156). Plan §4.2 and §4.3 said
the original's data model would be kept "until the oracle is green, then and only then tidy"; this
document is the tidy, planned.
**Depends on:** ADR-001 (fidelity — unchanged), ADR-002 (numeric model — unchanged), ADR-003 (the
oracle stays the judge), ADR-004 (projects and layout — §1's seam is what M3 finally builds).
**Feeds:** ADR-006 (the modernisation architecture, written 2026-09-06 at M2's opening and amended at M5-d) and ADR-007 (to be written when M3 closes — §6), the risk register (§7).

**What this is.** A plan to restructure `GameLogic/` and the executable so that the code reads as a
C++ program rather than as annotated 6502, **with no behavioural change**. Same universe, same
prices, same AI decisions on the same RNG state, same bytes on the canvas, same SID writes. The
oracle suites are the definition of "same" and every slice below ends with them green.

**What this is not.** It is not phase 6's *feature* list (resolution, gamepad, save UI, bug-fix
toggles — plan §6 Phase 6). Those change the game and each waits for its own ADR. This plan changes
the *program* and leaves the game alone, which is why it can go first and why it must: every phase-6
feature would otherwise be built on top of `FlightScreen`'s twenty-three references and a `Game`
struct that lives in `Main.cpp`.

---

## 0. Summary

The port is already further from "decompiled C" than the brief assumed: it has no `goto`, no
globals, no raw-pointer arithmetic, no `union` punning, `std::span` in thirty places and a comment
culture that explains every carry. What it carries instead is the **6502's calling convention and
memory map as its architecture** — routines that talk through zero-page scratch structs, a ship
that is thirty-seven bytes addressed by number, blueprints and line heaps addressed as 6502
addresses, argument lists that are structs of twenty references, twenty-two abstract "effects"
seams most of which exist because a routine had not been ported yet, and the whole top of the
program — the outer loops, the key dispatch, the mode changes — written in the Windows executable
where no oracle and no Linux job can reach it.

Six moves, in order, each a phase with slices and a fidelity gate:

| Phase | Move | What it buys |
|---|---|---|
| **M0** | **The safety net.** A ratchet on the patterns this plan removes; a layout-independent image of the game state for the oracle to compare and the replay to hash; the mutants re-runnable on Linux (they already are). | Every later slice is measured by the same instruments the port was, plus one that sees composition rather than routines. |
| **M1** | **Typed data.** `Ship`, `Commander`, `Blueprint`, `ShipType`, ship-state flags — each with a byte codec that reproduces the original layout exactly, so the oracle compare stays a byte compare. | Three hundred and twenty-nine numeric byte indices become names; the bytes stop being the model and become the wire format. |
| **M2** | **Explicit calling conventions.** Value in, value out; the zero-page scratch structs shrink to the handful of channels that genuinely cross routine boundaries, each named. | A reader can see what a routine consumes and produces without knowing what `P` held three files away. |
| **M3** | **Ownership.** `Elite::Universe` owns every byte of game state; `Elite::Game` owns the outer loops, the dispatch and the mode machine; the twenty-two seams collapse to four platform ports; `Outpost.exe` becomes a presenter. | The whole program is deterministic, hashable and driven from a test — which is what ADR-003 §3 and ADR-004 §1 said in September and never got. |
| **M4** | **Control flow.** The flight frame, the ship renderer, the AI and the docking computer become pipelines of named stages with typed intermediate results; implicit state machines become explicit ones. | The three routines over five hundred lines each become readable in one sitting. |
| **M5** | **Polish and the ledger.** Strong types for the remaining bytes, `constexpr` where the data allows, the twenty-one stale file names in `Source-Inventory.md`, and the ADRs that record the decisions. | The corpus describes the tree again. |
| **M6** | **Detach — behind the M6-0 gate.** Eight things the oracle can pin today and nothing will afterwards are closed first (§6 Phase M6); then the oracle's answers are recorded as checked-in fixtures and the live oracle is retired; the identifiers named for 6502 labels, the assembly quoted in comments, the `// 6502:` markers and the ledger go; `MasterFile/`, `Upstream/`, the interpreter and the tools that read the original leave the tree. | A C++ program that builds, tests and reads on its own, with the original's data as its only inheritance (owner ruling, §1). |

Four rules hold across all of it and are restated in §5: **the oracle decides, until M6 records
it**; **a byte's width and wraparound never change**; **a mutant is re-anchored, never dropped**;
**`// 6502:` and the ledger survive every rename, until M6 removes them together**.

---

## 1. Clarification — decisions this plan needs, and what it assumes meanwhile

The brief asks for questions before rewriting. These are the ones whose answers change the work;
each carries the default this plan proceeds on until the owner rules, so that M0 — which needs no
answer — is not blocked on them.

| # | Question | Why it matters | Assumed until ruled |
|---|---|---|---|
| Q1 | **C++20 or C++23?** The brief says C++20/23. The projects pin `stdcpp20` and the portable runner compiles with `-std=c++20` (`generate_runner.py`); MSVC v145 and g++ 13 both carry most of C++23 behind a flag. | `std::expected` would replace the `{value, carry}` result structs' error half; deducing `this` would simplify the typed views; `std::to_underlying` the enum plumbing. Each is a toolchain flag in three places and one CI matrix. | **C++20.** C++23 features are listed in §4.9 as candidates; nothing in the plan needs them. |
| Q2 | **Are the tests inside the "structural freedom"?** ADR-003's suites index the port's structs by name (`math.k[3]`, `work[31]`) at hundreds of sites. Changing the data model changes them. | If tests are frozen the data model is frozen with them. If they may change, M0-b builds the bridge that makes them layout-independent once, so later slices touch them rarely. | **Yes**, provided each changed test still compares the same bytes against the oracle — the bridge (§4.7) is how that is proved. |
| Q3 | **May game logic move out of `Outpost/`?** `Main.cpp` holds `Game`, `Perform`, `Leave`, `Advance`, `Run` and thirteen option bytes — the top of the 6502 program. ADR-004 §1 says the executable is "composition root and presentation". | It is the largest single item (M3) and the one this environment cannot compile: `Outpost/` is Win32 and D3D12 and builds only on the Windows job. Every M3 slice therefore lands through CI rather than a local build. | **Yes.** It is what ADR-004 already says; the plan treats the current state as the debt, not the decision. |
| Q4 | **Do `// 6502:` markers and the ledger stay mandatory after modernisation?** A typed `Ship` has no natural place for `INWK+31`. | Traceability is what lets "where did `TA7` go" be a grep. Dropping it would also break `inventory.py --strict`, which CI runs. | **Yes.** The marker moves to the field or the function that replaces the offset; the ledger's *Home* column follows the file. |
| Q5 | **Blueprints: parsed or addressed?** `ShipBlueprint.h` argues that a struct "would have to decide what a blueprint IS", because `LL9` walks the vertex bytes through a pointer it advances. | A parsed `Blueprint` with `std::span`s over vertices, edges and faces removes `ShipByte(address)` and the `XX21` self-modification model; the three blueprints whose header disagrees with their extent (`ShipDataTests`) are the risk. | **Parsed**, with the extent taken from the header exactly as `ShipBlueprintExtent` takes it today and the three disagreeing ships carried as they are — the port reads what the header says, so the parse reads the same. |
| Q6 | **The line heap: an arena with 6502 addresses, or per-ship spans?** `KILLSHP` shuffles every run above a dead ship down; `NWSHP` refuses a ship when the heap would run into the block it is about to write, by comparing two addresses through a carry-dependent subtraction. | Per-ship spans are the clean model and cannot express the refusal without reproducing the same arithmetic. | **Arena kept, addressing changed**: a `HeapOffset` strong type from the top of the arena replaces the raw `std::uint16_t`; the refusal keeps its exact byte chain inside one function (§4.2). |
| Q7 | **Should the oracle bridge be allowed to grow the label map?** `Where` in `FlightUniverse.h` looks up sixty labels by hand. A generic bridge wants the map generated. | `tools/labels.py` already writes `Labels.txt`; a `--fields` mode could emit the C++ table. Generated code is checked in (ADR-004 §3) and needs a `--check`. | **Yes**, as a generated header with a check, like the data tables. |
| Q8 | **May `clang-tidy`'s `modernize-*` set be widened?** `.clang-tidy` enables two `modernize-` checks. | The rest (`modernize-use-using`, `-loop-convert`, `-avoid-c-arrays`, `-use-designated-initializers`) would mechanise part of M5 and stop regressions. `WarningsAsErrors: '*'` means each one is a decision. | **Widened one check at a time**, each in its own commit with the fixes it demands, never as a batch. |

**Ruled 2026-09-06, owner.** Q1 **C++20**. Q2 **yes**, the tests may be rewritten through the bridge.
Q3 **yes**, the loops and the dispatch move into `GameLogic`. Q5 **parsed** blueprints. Q6 the **offset
arena**. Q7 and Q8 fall under the ruling below. Q4 was answered with a decision the table had not
offered, and it is the one that changes the shape of the plan:

> Remove the link with the assembler code. Remove the dependency on the oracle and on all of the
> legacy assembler, so that it can be removed.

Clarified the same day into four rulings:

| # | Ruling | What it means for the plan |
|---|---|---|
| R-a | **Detach at the end, not first.** The oracle stays the judge through M1–M5; a final phase records its answers as checked-in fixtures, replaces every oracle test with a fixture test, and only then deletes the interpreter, the upstream submodule and the masters. | Phase **M6** (§6). Every earlier slice is still measured against the assembled original, which is the only instrument that can say a refactor changed nothing. |
| R-b | **Every trace of the original goes**: the `// 6502:` markers and `Source-Inventory.md`; the identifiers that are 6502 labels (`p`, `q`, `xx15`, `k3`, `INWK`-style names); the assembly transcribed in comments (`LDA` / `STA` / `BCC` sequences); `MasterFile/` and `Upstream/` from the tree. | M6-c, M6-d and M6-e. M2 and M4 rename as they go so that M6-c is a sweep of what is left, not a second pass over everything. AGENTS.md R7 and §7 are amended when M6-e lands, not before. |
| R-c | **The derived data stays**: the generated tables the game cannot run without, and the recorded fixtures the tests cannot run without. That is the accepted residual exposure (Risk R1, restated at M6-f). | Q7 is moot — the label table the bridge needs exists only while the oracle does, and is generated into the test tree for M0-b to M6-a and deleted with it. Q8 stands: `modernize-*` widened one check per commit. |
| R-e | **"The port was wrong, the record is not needed."** (Ruled 2026-09-06, on the replay.) When a slice finds a defect in the port and fixes it, the replay record follows the fix: it is re-taken with the journal entry naming the defect, and no ADR-001 §6 row is needed for the record to move. What stays forbidden is a record that moves with no defect named — that is a refactor that changed the game. | Rule 1 and M0-c's "when the record may change", below. The journal entry is the audit trail; the oracle suites, which do not move for a fix of this kind unless the defect was theirs too, are the check that the fix is a fix. |
| R-d | **History is not rewritten by this plan.** Removing the files at the tip is M6-f; whether the history that carried them is rewritten is a separate owner decision and is not scheduled here. | Recorded in R21 (§7) so that it cannot be mistaken for something M6 did. |

Anything not in this table is a routine judgement call this plan makes itself and records in §6.

---

## 2. What the port is today — the system decoded

This section is the reading a person needs before touching any of it: how execution and data flow
through the whole program, in the port's own names. The 6502 labels are given once each so that the
`Source-Inventory.md` row can be found.

### 2.1 Two outer loops, and where they live

The original has one main loop with two halves chosen by `QQ12` (docked): `FRCE` reads a key,
`MLOOP` dispatches it through `TT102`, and `TT100` runs a flight frame (`M%`) first and then falls
into `MLOOP`. Every docked screen ends by blocking in `TT217` for a key, so the docked half costs one
keypress per pass; the flight half runs as fast as the processor allows and the game slows down as
the bubble fills (plan §6.17, §6.114).

**In the port both halves are in `Outpost/Main.cpp`**, in the anonymous namespace, over a struct
named `Game` that is the composition root's and not `GameLogic`'s:

```
Run()                                  6502: BR1 / TT170 / FRCE
 ├─ SetUpLoaderScreen, SaveCommander   the loader's palette, NA%
 ├─ ResetAndStartGame -> ForcedKey     TITLE, RESET, BAY -- the start sequence, in GameLogic
 └─ while (shell.Turn())               one present per turn; the window pumps and blocks on vsync
      ├─ paused?  AdvancePaused        6502: FREEZE, turned inside out (one pass per key event)
      ├─ docked?  N passes of          6502: MLOOP -- paced by PlanSteps on the empty-bubble frame cost
      │            CoolTheGuns / ScanFlightControls / TakeKey / PressKey -> Perform(KeyAction)
      └─ flying:  Advance              6502: TT100
                   for each planned step:
                     MainFlightLoop -> LoopOutcome     6502: M% parts 1..16 (GameLogic)
                     Leave(outcome)                    6502: JMP DOENTRY / DEATH / ESCAPE -- the exits
                     RunLoopHead -> maybe RunSpawning  6502: MLOOP parts 1..4 (one pass in 256)
                     RunLoopTail                       6502: MLOOP part 5 (laser cooling, dials, Trumbles)
                     ScanFlightControls, TakeKey, PressKey   6502: TT17 then TT102
```

`Perform` is the eighteen-way `switch` on `KeyAction` that `TT102` decides (`DockedKeys.h` decides,
`Main.cpp` performs). `Leave` is the three non-returning jumps out of `M%`, turned into a return
value and a `switch`: docking runs `DockAtStation` and one of seven mission briefings; dying runs
`Die` and the start sequence again; the escape pod runs `AbandonShip` and then the docking.

The consequence for verification: **`DockedSessionTests.cpp` rebuilds this graph by hand** over a
null presenter to drive the docked half, and there is no flight equivalent — `MainFlightLoop` is
oracle-compared frame by frame in `FlightLoopTests.cpp`, but nothing drives `Advance`/`Leave`
outside a Windows build. The mode machine that connects title, docked, flight, paused, dying and
docking is written twice (once in `Main.cpp`, once in the test) and tested once.

### 2.2 The flight frame — `M%` as a pipeline

`MainFlightLoop(FlightLoop&)` runs three stages a frame; every stage reads and writes the same
aggregates, which is the shape M4 changes and M3 makes possible.

| Stage | 6502 | What it does | Reads | Writes |
|---|---|---|---|---|
| `BeginFlightFrame` | `M%` to `MA3` (parts 1–3) | Seeds `RAND` from the planet's x, moves the Trumbles, reads the key logger into roll/pitch/speed, fires lasers, missiles, the E.C.M., the energy bomb, the escape pod, the docking computer | keys, controls, commander, status | `FlightState` (alpha/beta/delta and their sign/magnitude copies), `RAND`, sounds |
| `MoveEveryShip` | `MA3` to `MAL1` (parts 4–12) and `KS1` | Per occupied slot: copy the block into `INWK`, look up the blueprint, let the bomb kill it, `MVEIT` (motion, tactics one pass in eight, scanner blip), copy back, contact test, scooping, docking, collision damage, `LL9`, laser hit, kill or write-back | bubble, `INWK`, blueprint, status, commander | the same, plus the canvas, the line heap, the scanner, sounds |
| `EndFlightFrame` | `MA18` to `STARS` (parts 13–16) | Shield and bank recharge every eighth frame, the thirty-two-step housekeeping cycle (energy warning, docking-computer reminder, cabin temperature, altitude, fuel scooping, the planet and sun through `PLANET`), then the stardust | status, bubble slots 0 and 1 | status, canvas, `FlightState::delt4` |

The loop over slots is a `for (;;)` with a hand-advanced index because `KILLSHP` shuffles the slots
down and the slot that took the dead one's place is processed next. Three things leave the frame
without finishing it — docking, death, the escape pod — and come back as `LoopOutcome`.

**The data a ship moves through in one frame** is the trap for any refactor and is worth stating
exactly: `Bubble::blocks[slot]` (the resting copy) → `ShipBlock work` (`INWK`, the working copy) →
`MoveShip` → `blocks[slot] = work` → contact tests read `work` → `DrawShip` reads `work` and writes
the ship's run of the `LineHeap` through the address in `work[33..34]` → kill or write back bytes 31
and 32 only. `FlightState::blueprint` (`XX0`) is loop state and **is not reset per ship** — a planet
inherits the previous ship's blueprint pointer and `MVEIT` reads byte 15 of it (§6.90). The typed
`Ship` of M1 keeps that: the blueprint reference lives on the loop, not the ship.

### 2.3 Drawing — `LL9`, the clipper, and erase-by-redraw

`DrawShip` (`LL9`, 531 lines in `ShipDraw.cpp`) is one routine with twelve annotated parts:
distance and visibility tests → scale the orientation matrix → transpose it → face visibility by dot
product → project every vertex (`PROJ`, one division per axis) → decide each edge from its two faces
→ clip each edge (`LL145`, four parts, `ClipState` for `XX12`/`XX13`) → write the surviving lines
into the ship's run of the line heap → draw them by XOR. Erasing a ship is drawing its previous run
again (`EraseShip`), which is why the heap is game state and not a rendering cache: lose it and
nothing can be rubbed out. The sun and the planet keep their own heaps (`PlanetSunState`) and the
space station borrows the sun's (§6.112).

Intermediate values flow through `GeometryWorkspace` (`XX16`, `XX12`, `XX2`, `XX3`, `XX4`, `XX17`,
`XX18`, `XX20`, `V`), `DrawWorkspace` (`X1`, `Y1`, `X2`, `Y2`, `XX15`...) and `MathWorkspace`. None of
those is ship state; all of them are a single frame's scratch, which M4 turns into stage results.

### 2.4 The docked half — screens over a token language

`TT102` maps a key to one of eighteen actions; each action is a screen (`StatusScreen`, `BuyScreen`,
the charts, `EquipShipScreen`, `DiskAccessMenu`...) that prints through the token printers
(`TokenPrinter` → `CharacterPrinter` → `TextPrinter` → `Canvas`) and reads keys through `KeySource`
and `LineEntryEffects`. The token system is a small language with persistent capitalisation state
(`QQ17`), a recursion into `ExtendedTokenPrinter` for the descriptions and the missions, and a cycle
(`ValueTokens` needs the printer that needs it) that the composition root breaks with
`SetValueTokens`. This half is already close to the target shape: screens are functions over a small
aggregate (`TradeScreen`, seven references), and the seams they use are genuinely platform
(a key, a wait, a beep). It changes least.

### 2.5 The AI and the living universe

`RunTactics` (`TACTICS`, 567 lines) decides one ship's behaviour: missiles steer at their target
and die against the station; the station launches Vipers or Transporters; an Anaconda calls
escorts; everything else measures its energy, its distance and the dot product of its nose against
the player, rolls the RNG and picks between fleeing, attacking, firing and doing nothing. It reaches
`OOPS` and therefore `DEATH` on three paths, which is why it returns "is the player still alive"
(§6.122). `RunDockingComputer` (`DOCKIT`) is the same shape for the autopilot. `RunSpawning`
(`MLOOP` parts 1–4) puts traders, asteroids, police, bounty hunters, Thargoids and pirate packs into
the bubble one pass in 256, driven entirely by `DORND`.

All three are decision procedures written as one fall-through routine each, with the decision
carried in the ship's bytes (`INWK+32`'s AI flags, `INWK+31`'s state bits, `NEWB`) and the result
being side effects on the ship (roll, pitch, speed, a missile launched, a laser fired). M4 gives
each a decision type.

### 2.6 Who owns which byte today

The single most important table for M3. Every 6502 label that is game state has a home; the homes
are in four places and the boundaries between them are the port's history rather than a design.

| Owner | Where | Holds |
|---|---|---|
| `Outpost::Game` (anonymous namespace, `Main.cpp`) | the executable | `TP`..`CHK` (`CommanderBlock`), `NA%` image and name, `QQ15`, `QQ2` and `tek`/`gov`/`QQ28` (`CurrentSystem`), the market (`AVL`, `QQ26`), `FlightStatus` (`ENERGY`, `FSH`, `ASH`, `QQ22`, `GNTMP`...), `MessageState`, `QQ9`/`QQ10`, `EV`, `QQ12`, `safehouse`, `QQ8`, the pause state, `JSTGY`, `JSTE`, `MUTOKOLD`, `DNOIZ`, the RNG, the whole text system, the sound buffer and the music player |
| `Outpost::FlightSession` | the executable | `INWK`, `K%`/`FRIN`/`MANY`/`JUNK`/`MSTG`/`SLSP` (`Bubble`), the stardust, the planet and sun heaps, the ship line heap, `FlightState` (`ALPHA`..`DELT4`, `MCNT`, `XSAV`, `TYPE`, `XX0`), `ControlState`, `ControlOptions` (`DAMP`, `DJD`, `JSTK`, `PATG`), `KLO`, the compass, the Trumble sprites, `VideoState`, `ScreenState`, every workspace, `VIEW`, `L1M` |
| `Outpost::GameShell` | the executable | `QQ11` (by reference into `Game`), `GCNT` (a pointer into the commander), the briefing ship slot, the title-screen clock |
| `GameLogic` aggregates | the library | none of it — `FlightScreen` and `FlightLoop` are structs **of references** into the three above, and `TradeScreen`, `SaveScreen`, `GameStart`, `MissionScreen`, `TitleScreen` and `JumpState` are the same for the docked half |

`GameLogic` owns no game state at all. It owns behaviour over state the caller lends it, one
aggregate of references per routine family, and the two callers that lend it are the Windows
executable and a test fixture that copies the executable's wiring. ADR-003 §3's `StateHash` was
never built because there is no object to hash.

### 2.7 What pins behaviour, and what does not

Three instruments are in place and every slice below leans on them:

- **The oracle**, per routine: a test sets the same bytes on both sides, runs both, compares the
  bytes the routine's commentary lists plus the flags its callers read. For the flight universe this
  is `FlightUniverse.h`'s `Mirror` (port → 6502 memory, by label) and `CompareState` (6502 memory →
  port), which already know where sixty labels live. **That pair is the seed of the bridge in
  §4.7**: it is written once per fixture and against the port's *current* struct layout, which is
  why a layout change today would touch it.
- **The whole-bitmap comparisons**: `TITLE`, `TT110`, the dashboard, the planet and the stardust
  suites compare the whole `SCBASE` region byte for byte. They see composition where the per-routine
  tests see routines.
- **The mutants**: <!--count:mutants-->72 recorded edits in nine files, each anchored to a line of
  source that must match exactly once, each expected to be caught. `mutate.py --check` runs in CI;
  the run itself works through the portable runner on Linux (`--runner portable`).

What nothing pins: the presenter (R5, by design); timing (ADR-005 §3, by design); and the things
M6-0 names — a whole frame with an explosion in it, the escape pod and the death sequence in
composition, eight control codes, and seven routines that are only ever trapped. The outer loops
and the mode machine were on this list until M3-c and M4-d moved them into `Game`, which the
replay drives; the rest is why M6-0 exists.

---

## 3. The legacy patterns, named and measured

Each pattern below is a claim about the tree and carries a checked number where the tree can be
counted. `tools/check_modernize.py` counts them and **fails the build if any count rises** — the
ratchet is what stops a slice reintroducing what another slice removed (§5, rule 5). The recorded
ceilings are in `tools/modernize_ratchet.json` and are lowered as slices land.

**P1 — The register-shaped calling convention.** <!--count:register-params-->13 parameters in
`GameLogic/*.h` are named `_a`, `_x` or `_y` and typed `std::uint8_t`: the routine takes what the
6502 routine took in that register, and its meaning is in the comment. Twelve result structs carry
a field named `a` or `carry` for the same reason (`ProjectResult::a`, `ScreenOffset::a`). Example:
`LargestAxisFrom(const Bubble&, std::uint8_t _slot, std::uint8_t _a)` is `MAS2` and `_a` is the
value the caller ORs the high bytes into.

**P2 — Zero-page scratch as an implicit channel.** `MathWorkspace` held `P`, `P+1`, `P+2`, `Q`, `R`,
`S`, `T`, `T1`, `U`, `CNT`, `TGT`, `CNT2`, `XX`, `YY`, `K`, `K2` and `widget`, and was passed to
hundreds of functions so that a routine could "leave the low byte in P" for a caller three files
away (`Arith.h`'s own words). M2-b took the kernel's off it and M2-c the drawing's; two bytes are
left, and both outlive their writer on purpose (§4.3). `DrawWorkspace`, `GeometryWorkspace`, `ClipState`, `K3Block`,
`Projection` and `K3Block` are the same pattern for other zero-page runs. M2-c emptied most of
them: `NumberWorkspace` went with M2-c-1, `DrawWorkspace` is the dashboard's screen cursor since
M2-c-2, `MathWorkspace` is `Q` and `K2`'s bottom byte since M2-c-3, and `GeometryWorkspace` is
`LL9`'s four stage results. <!--count:workspace-params-->45 parameters in the headers are still one
of them by reference. The pattern is faithful and it is also the reason no signature says what a function
consumes or produces.

**P3 — Flat byte blobs addressed by number.** `ShipBlock` is `std::array<std::uint8_t, 37>` with
`operator[]`; when this plan opened, 329 sites in `GameLogic/*.cpp` indexed it with a numeric
literal (`work[31]`, `work[29]`, `_work[8]`) and 107 with a named offset constant — **M1-a took
both to <!--count:ship-literal-sites-->0 and <!--count:ship-offset-sites-->0, and M1-c made the
block a `Ship` of fields with the bytes as its codec** — and two of the
constant families named the same byte (`SHIP_STATE` and `SHIP_STATE_OFFSET`,
`SHIP_FLAGS` and `SHIP_FLAGS_OFFSET`, `SHIP_ENERGY` and `SHIP_ENERGY_OFFSET`) — the §6.34 trap set
twice. `CommanderBlock` is seventy-seven bytes behind a `Field` enum and `At()`, which is the better
half of the pattern and still hands back a byte for `Cash` (four bytes) and `Kills` (two). A second
naming family duplicates the ship types (`SHIP_COBRA_MK3` and `SHIP_TYPE_COBRA_MK3`; `SHIP_ADDER`
beside `SHIP_TYPE_*`).

**P4 — 6502 addresses as a data model.** `SHIP_BLOCK_BASE = 0xF900`, `SHIP_HEAP_TOP = 0xFFC0`,
`SlotAddress(slot)`, `ShipHeapAddress(ship)`, `BlueprintFor(bubble, type) -> std::uint16_t`,
`ShipByte(address)`, `LineHeap::Read(address)` with a "borrowed" window at `&0580` for the sun's heap,
and `Bubble::stationBlueprint` modelling the one self-modifying store into `XX21`. Each is right and
each is a 6502 address doing the job of a reference or an index. `NWSHP`'s refusal — the one place
the arithmetic is load-bearing — is a carry-dependent subtraction of two addresses that the port
reproduces exactly and must keep reproducing (§4.2).

**P5 — Reference aggregates as argument lists.** <!--count:aggregate-refs-->10 reference members,
and they were seventy-eight before M3-a. All seven argument-list structs are gone — `FlightScreen`,
`FlightLoop`, `MissionScreen` and `TitleScreen` in M3-a-2, `TradeScreen`, `SaveScreen`, `GameStart`
and `MissionBay` in M3-a-3 — and every routine takes `(Universe&, Ports&)`. **The eleven that
remain ARE `Ports`**, which is the one struct §4.5 exists to collapse: M3-b replaces its interfaces
with four ports without touching a signature again, and three of the four have landed. `ViewChange.h` said it plainly while
the rest existed: "the struct is the argument list".

**P6 — Game state and the top of the program in the executable.** §2.6, **closed by M3-c**.
`Outpost/Main.cpp` is <!--count:main-lines-->253 lines and every one of them is the platform: the
window, the swap chain, the audio device, the files, the two outer loops and the accumulator that
paces them. §2.1's `class Game` exists (`GameLogic/Game.h`) with `Reset`, three `Step`s and the
state behind them, and `check_outpost.py`'s surface fell with it — the executable reaches
<!--count:outpost-elite-names-->69 distinct `Elite::` names where it reached 205 when M3 opened.

What §2.1 asked for and this did not have until M5-e is `Frame()`, `Sounds()` and `StateHash()` —
`Sounds()` is built (M5-e-1) and `Frame()` is `State().canvas`, which the executable already reaches —
and one thing it did not ask for: `Elite::Universe` is still the composition root's, because
`Outpost::FlightSession` binds it at construction and answers three of the seams `Game`'s `Ports`
needs. Two of the three are already scheduled to go, and the member moves across when they do
(§8, 2026-09-06).

**P7 — Seams that outlived their reason.** <!--count:effects-seams-->8 abstract classes in
`GameLogic/*.h`. Some are platform (`Keyboard`, `Presenter`, `CommanderStore`); `TextSink` and
`ValueTokens` are the text system's own and are argued about in §8 rather than assumed away. Most are **phase order**:
`ShipDrawEffects::DrawPlanetOrSun` and `DrawExplosion`, `SpawnChildEffects::SpawnChild`,
`ViewEffects::PlaySound`, `SightEffects`, `ExplosionEffects` — each declared when the routine on the
far side was "phase 4's" and kept after it landed, which §6.73 already names as a mistake made four
times. `SpawnEffects`, `ChartShapes`, `ShipEffects::RunTactics`, `FlightLoopEffects`'s `SpawnAhead`
and `Anger`, and `StartUpEffects`'s `ResetUniverse`, `ResetShip` and `ResetMissileIndicators` are
gone (M3-b-1a to M3-b-1e). Three methods
are declared on two interfaces each and one override satisfies both, which is
the language's rule and a smell. One seam carries a CPU flag across the platform boundary:
`PlaySound(std::uint8_t _effect, bool _carryIn)` returns a carry because `NOISE` does (§6.99), and
the *window* is asked to preserve it.

**P8 — Monolithic frame procedures.** `MoveEveryShip` was four hundred lines over nine annotated
parts; `BeginFlightFrame` and `EndFlightFrame` about the same between them; `DrawShip` 531 and
`RunTactics` 567; `RunDockingComputer` two hundred. Each is one routine in the original and one
function here, with local `bool`s standing in for the branch targets (`docking`, `scoopable`,
`collision`, `holdFull`, `crashed`) and results carried in the workspaces.

**M4-a HAS DONE THE THREE IN THE FLIGHT LOOP** (§8, 2026-09-07). `MoveEveryShip` is 143 lines and
its parts answer `Contact`, `ScoopResult`, `DockingTest`, `Impact`, `Aim`, `LaserHit` and
`KillOutcome`; `BeginFlightFrame` is 14 over `StirTheFrame`, `TurnTheShip`, `RunFlightKeys` and
`FireTheGuns`; `EndFlightFrame` is 43 over `BurnEnergyBomb`, `RechargeBanks`, `MaybeSpawnStation`
and `RunCycleStep`. Every one of the five `bool`s is a value some stage returns. `DrawShip` is
M4-b's and `RunTactics` and `RunDockingComputer` are M4-c's.

**P9 — Implicit state machines.** The game's mode is `dockedFlag` (a `std::uint8_t&` written by
`RESET`, `DOENTRY` and `TT110`), `paused` (a `bool` in `Main.cpp`), `LoopOutcome` plus `Leave`, and
`ForcedKey::loop`. A ship's mode is bits 5, 6 and 7 of byte 31 (`SHIP_STATE_EXPLODING`, `FIRING`
*or* `CLOUD_DRAWN` depending on bit 5, `KILLED`) and bits of byte 36 (`NEWB`: hostile, remove,
cop, trader...). The screen's mode is `QQ11`'s value (`BUY_CARGO_VIEW = 2`, `SELL_CARGO_VIEW = 4`,
`INVENTORY_VIEW = 8`, `EQUIP_SHIP_VIEW = 32`, charts, space views 0..3 through `VIEW`).

**P10 — Untyped bytes.** Ship types, views, laser kinds, sound effects, message tokens and colours
are `std::uint8_t` constants — **M1-b took the ship types and the three flag bytes of a ship to
`ShipType`, `ShipStateBit`, `AiBit` and `NewbBit`** — booleans are `0`/`0xFF` (`BST`, `ECM`, `DISK`); the thirteen pause-
screen options are an `OptionBlock` of thirteen `std::uint8_t*` because "making them contiguous
would touch eighty-seven call sites" (`Main.cpp`); <!--count:out-params-->0 parameters are
`std::uint8_t&` outputs (`_docked`, `_fuel`, `_crosshairX`).

**P11 — Carry-in parameters across non-kernel boundaries.** <!--count:carry-params-->30 `bool
_carryIn` parameters in headers. Inside the kernel (`AddWithCarry`, `Rng::Next`, the multipliers)
they are the numeric model and stay. The other twenty-four are a routine boundary that happens to
be where a 6502 flag was live, and this pattern's original entry said "every caller passes a
literal". **M2-d audited all twenty-four against the disassembly and that was wrong**: most are a
computed flag the port models, three were passed the wrong value, and the literals that remain are
each an inherited flag the port cannot see — the parameter is what makes the assumption visible at
the call site rather than buried in the routine. §4.7 is the table and §8 the three defects.

**P12 — The original as a build and test dependency.** <!--count:origin-markers-->4,125 `6502:`
references in `GameLogic/`'s comments; <!--count:oracle-test-files-->48 of the test translation
units load the assembled original through `OracleImage` and cannot run without BeebAsm, the
submodule and the label map; <!--count:origin-tools-->7 of the tools read `Upstream/` or
`MasterFile/`; CI builds an assembler on every push. This was the port's method, not a defect in
it, and it is the one pattern that the owner's ruling (§1, R-a to R-d) makes a target: the end state
builds, tests and reads with none of it present. Until M6 it is also what every other slice is
measured by, which is why it is counted here and removed last.

**What is not a legacy pattern, and stays.** The 8-bit arithmetic with explicit carries, the
sign-magnitude coordinates, the extracted tables, the RNG's carry chain, the XOR canvas with the
C64's byte layout, the line heaps, the ported bugs of ADR-001 §6, the deliberate divergences
recorded at their call sites (the stardust count of zero, out-of-arena reads as zero), the
`// 6502:` markers and the commentary — both until M6, which removes the markers and the transcribed
assembly but never the reason a byte is handled the way it is (Risk R20). ADR-002 is unchanged by
this plan in every clause.

---

## 4. Target architecture

### 4.1 Layers and the dependency rule

```
Ports          Presenter · Keyboard · SoundSink · SaveStore        (4 abstract classes; the executable and the null port implement them)
   ▲
Game           Elite::Game -- the mode machine, the outer loops, the key dispatch, the exits      6502: BR1, FRCE, MLOOP, TT100, DOENTRY, DEATH2, FREEZE
   ▲
Systems        Flight (M%), Render (LL9, PLANET, SUN, STARS), Tactics, Spawn, Docked screens, Text, Sound players, Missions
   ▲
Model          Universe { Commander, Bubble{Ship[10]}, LineHeap, Stardust, FlightState, FlightStatus, Screen, Options, Rng ... }   -- owns every byte
   ▲
Kernel         EliteTypes (Byte, carry, SignMag24), Arith (value in / value out), Rng, LookupTables, Canvas, Tokens
```

Edges point down only. `Systems` never include each other's private workspaces; they communicate
through `Universe` and through typed stage results. `Game` is the only thing that knows there are two
halves. Nothing below `Ports` includes a Windows header, as now.

### 4.2 The data model

**`Ship` (6502: `INWK`, one entry of `K%`).** A struct with the fields the thirty-seven bytes are —
`SignMag24 x, y, z`; `Orientation` of three `Vector16` (`INWK+9..26`, each component sixteen-bit
sign-magnitude, `nosev`/`roofv`/`sidev`); `rollCounter`, `pitchCounter` (`INWK+29`, `30`); `speed`
(`27`); `acceleration` (`28`); `ShipState state` (`31`); `AiFlags ai` (`32`); `HeapOffset heap`
(`33..34`); `energy` (`35`); `NewbFlags newb` (`36`) — and two functions:

```cpp
[[nodiscard]] constexpr std::array<std::uint8_t, SHIP_BLOCK_SIZE> ToBytes() const noexcept;   // 6502: the K% layout
[[nodiscard]] static constexpr Ship FromBytes(std::span<const std::uint8_t, SHIP_BLOCK_SIZE>) noexcept;
static_assert(Ship::FromBytes(Ship{}.ToBytes()) == Ship{});
```

The bytes are the **wire format**: what `NWSHP` copies, what the save game will never contain, and
what the oracle compares. The routines that walk the block by index — `MVT1`'s `INWK,X` with X an
axis offset, `MVS4`'s Y stepping through the orientation in sixes, `NWSHP`'s wholesale copy — get
`Ship::Axis(Axis::X)` returning `SignMag24&` and `Orientation::Component(vector, axis)`; the wholesale
copy is `ToBytes()` into the slot. Where a routine genuinely reads one byte as two things (`INWK+31`
is missiles-left in bits 0–2 and state in bits 3–7), `ShipState` is a flags type with both accessors
and one storage byte.

**Why bytes-plus-codec and not a view over the bytes.** A view (`ShipView` over `std::array`) is the
smaller step and is M1-a below, because it lets the 329 literal sites be renamed before any layout
changes and gives the oracle bridge nothing to do. It is also where the port has to stop if Q2 is
answered no. The struct with a codec is M1-c, and it is the one that removes `operator[]`.

**`Commander` (6502: `TP` to `CHK`).** The same: typed fields (`Credits cash` as a strong type over
four bytes big-endian in tenths, `LightYearsTenths fuel`, `std::array<Laser, 4>`, `CargoHold`,
`Equipment` flags with their `0`/`0xFF` encoding preserved in the codec, `Tally kills`) and a
seventy-seven-byte `ToBytes`/`FromBytes` that `SaveCommander`, `LoadCommander` and both checksums use
unchanged. The disk format is untouched by construction; `SaveGameTests` proves the round trip.

**`ShipType`, `View`, `Laser`, `SoundEffect`, `Message`, `Colour`.** Scoped enums with the original
values. `ShipType` gets `IsBody()` (bit 7), `IsJunk()` (the `JL`..`JH` range), `IsWreckage()`
(`PLT`..`SPL`) as `constexpr` functions beside it, and the second naming family goes.

**`Blueprint` (6502: `XX21` and the region under it).** Parsed once from the generated data into
`std::array<Blueprint, SHIP_TYPE_COUNT + 1>`: the twenty header bytes as named fields, then
`std::span<const std::uint8_t>` over the vertices (six bytes each), edges (four) and faces (four),
sized by the header exactly as `ShipBlueprintExtent` sizes them today. `LL9`'s advancing pointer
becomes an index into the span. The station's mutable entry becomes `Bubble::stationType`
(`Coriolis` or `Dodo`) resolved to a blueprint at the two places `NWSPS` and part 14 read it.
`Blueprint` is `const`, which is what the region was until `NWSPS` — so the self-modification is
modelled as the one bit of state it is, and nowhere else.

**`Bubble` and the line heap.** `slots`, `counts`, `junk`, `missileTarget` stay. `SlotAddress` and
the two base constants move *inside* `Bubble::TryReserveHeap(slot, bytesNeeded)`, which reproduces
`NWSHP`'s chain — `LDA SLSP / SEC / SBC bytes / STA INWK+33 / LDA SLSP+1 / SBC #0 / STA INWK+34 /
LDA INWK+33 / SBC INF / LDA INWK+34 / SBC INF+1 / BCC refuse` — byte for byte, with the constants
as the only 6502 addresses left in the model and a comment saying why. The heap is addressed by
`HeapOffset` (bytes down from `LS%`), a strong type with no implicit conversion, and `LineHeap`
loses the sun window: the station's heap is `PlanetSunState::sun` lent as a `std::span`, and a
`HeapRun` is `{HeapOffset start, std::uint8_t count}` whether it is in the arena or the sun's.

### 4.3 Explicit calling conventions

Every kernel routine gets a signature in which the inputs are parameters and the outputs are a
returned struct. The workspace fields that are *genuinely* live across a call — the ones a caller
reads after the callee returns without having set them — are enumerated in M2-a and each becomes a
named field of the callee's result. From the port's own findings, the list is short and known:

| Channel | Producer → consumer | Becomes |
|---|---|---|
| `P` low byte after `MULTU`/`MU11` | the multipliers → `MVEIT`, the stardust | `Product{ high, low, carry }` (the carry is already `WideResult::carry`, §6.33) |
| `K`, `K2` in `MV40` | `MULT3` → `MV40`'s add | locals in `MovePlanetOrSun` |
| `XX15` after `SPS1` | `LoadPlanetAxes` → part 9's alignment test | `UnitVector` returned |
| `K3`/`K4` after `PROJ`, and `K3+1` surviving to the next ship (ADR-001 §6, `SHPPT`) | `Project` → `DrawShipAsPoint` | `Projection` stays a parameter that outlives the call, **deliberately**, with the ADR row on it |
| `CNT`, `TGT`, `CNT2` | fourteen users, each initialising before reading (§6.49) | locals |
| `T1` after `TIDY`, `Q` after `DVID`, `R` after `LL28` | within one file each | locals or a result field |

**The census, M2-a (2026-09-06), re-taken as each slice lands.** The table above is what the
port's own findings named before anyone counted. The table below is the count: `tools/channel_census.py`
reads every routine in `GameLogic/*.cpp`, finds each workspace it takes, reaches through
`FlightScreen`, or binds to a local reference, and records per field which routines write it, which
read it before writing it with no call in between (it came from the caller) and which read it first
after passing the workspace to a callee (it came, most likely, from that callee). The verdict column
is the human reading, held in the tool beside the mechanics, and `channel_census.py --check` fails
when this table and the tree disagree or a field has no verdict. When M2-a took it, five fields
outlived a call on purpose (`Projection`'s four and `SC`), one was a constant in this port
(`dontclip`), nine were locals and the rest parameters or results. M2-b took the kernel's to values
and found two more that outlive a call by design — `K2`'s bottom byte, which `MV40` reads and never
writes, and `Q` as the frame leaves it for the altitude check (§8, R22) — so `MathWorkspace` keeps
those two bytes and the drawing scratch M2-c owns, `DrawWorkspace` shrinks to the dashboard's
cursor, and `GeometryWorkspace` to `LL9`'s frame. **M2-c finished it**: thirteen fields are left
across all six structs — `Q` and `K2`'s bottom byte, `SC`, `dontclip`, `LL9`'s four stage results,
`Projection`'s four and `K3` — where there were forty-two when M2-a took the first count.
`Flags` stays for the routines whose callers read
`C` or `V` (`TwistSeeds`'s carry, `PrintSystemName`'s carry, `Rng::Next`'s `C` and `V`), returned,
never global.

<!--census:start-->
| Field | 6502 | Written by | Read before written, from the caller | Read after a call | Verdict |
|---|---|---|---|---|---|
| `MathWorkspace.q` | `Q` | AddStep, DivideByShipZ, DrawExplosionCloud, DrawParticles, DrawSun, MeasureSlope, MovePlanetOrSun, MoveShipTail, ProjectVertices | RunCycleStep | — | **The frame's Q**, and one of the two bytes left (M2-b, §8; risk R22). `MA23`'s altitude check takes whatever the frame last left in `Q` as its radicand's low byte, so `MoveShipTail`, `MovePlanetOrSun`, `DivideByShipZ`, `DrawShip`, `DrawSun`, `DOEXP`'s two routines and the clipper's `LL115` and `LL118` write it for that read alone, as the original's `STA Q`s do. R22 said `LOIN` was a tenth writer this port never modelled and it is not: this build's `LOIN` works in `P2`, `Q2`, `R2` and `S2` at 188-191 and never touches `Q` at 154. Closed 2026-09-06 by measurement -- `TheFramesOwnQReachesTheAltitude` runs the whole frame with the planet in range and compares `ALTIT`. |
| `MathWorkspace.k2Low` | `K2` | DrawPlanetDetail, DrawSun | MovePlanetOrSun | — | **One byte of state, deliberately** (M2-b, §8). `MV40` never writes `K2` and its `LDA K / CLC / ADC K2` reads this byte for the carry of its first addition, so what it gets is whatever the last planet or sun drawer left there a frame ago. `PL9`, `PL26` and `SUN` store to it where the original's `STA K2` is; the other three bytes of the block are the ellipse's axes and travel as an `EllipseAxes` value since M2-c-3. |
| `DrawWorkspace.sc` | `SC(1 0)` | DrawBar, DrawDials, DrawIndicator | DrawBar, DrawIndicator | — | **State, deliberately** (M2-c leaves it; M4 names it). `DIALS` sets the screen pointer once and `DIL`/`DIL2` advance it seven calls running (its own comment, slice 3d-b): a cursor the dashboard drawer owns, not scratch. |
| `GeometryWorkspace.xx16` | `XX16` | DrawEllipse, DrawPlanetDetail, LoadTwoAxes, ScaleOrientation | DotProducts, TransposeOrientation | — | **Stage result** (M2-c-3 leaves it in the frame; M4 makes it a pipeline). `LL15`/`LL21` fill it, `LL51` and the transpose read it, and the planet drawer uses the same six bytes for the ellipse's four signs -- two meanings, one block, as `RAT` and `RAT2` are. |
| `GeometryWorkspace.xx12` | `XX12` | BothEndsBeyondTheSameEdge, ClipLineKeepingSwap, DotProducts, DrawBallLine, MeasureSlope, OpenHeapRun, PushEdges, SelectFaces | FaceVisibility | ProjectVertices (after ?), SelectFaces (after ?) | **Stage result** (M2-c-3 leaves it in the frame). `LL51` leaves three dot products that `LL9` parts 4 and 6 read, and `LL83`/`LL115` work in the same bytes while a line is being clipped -- the original's reuse, which nothing reads across. `DIALS` stopped borrowing them in M2-c-1. |
| `GeometryWorkspace.xx2` | `XX2` | ScaleShip, SelectFaces | EitherFaceVisible, RunDockingComputer | — | **Stage result, and one reader outside** (M2-c-3 leaves it). Face visibility, written by part 4 and read by parts 6 and 10; `DOCKIT` reads `XX2+10` as the memory it is (§6.112), which is why the frame is a struct and not four more locals. |
| `GeometryWorkspace.xx3` | `XX3` | MeasureRange, ProjectVertices | DrawExplosionCloud, OpenHeapRun | PushEdges (after EitherFaceVisible) | **Stage result, and one reader outside** (M2-c-3 leaves it). The projected vertices, filled by part 8 and read by parts 9 to 11; `DOEXP` copies them onto the heap for the burst, which is the frame's second outward reader. |
| `ClipState.dontclip` | `dontclip` | DrawShortRangeChart, Game::DrawChart, ResetShipAndBubble | ClipLineKeepingSwap | — | **State one screen writes and the clipper reads** (M2-c-2 leaves it). `TT23` sets it to 199 so the short-range chart can use the whole screen and `RES2` clears it again -- `Main.cpp` and `ResetShipAndBubble` in this port -- so it is not the clipper's scratch and did not become a `ClipResult` field with `XX13` and `SWAP`. `TT23` writes `Yx2M1` in the same two instructions and that byte is on `PlanetSunState`; whichever slice wires `TT23` puts this one beside it. |
| `Projection.x` | `K3` | DrawPlanetDetail, Project | CircleOffScreen, DrawBall, DrawEllipse, StorePoint | DrawPlanetDetail (after DrawHalfEllipse), DrawSun (after CircleOffScreen) | **State that outlives the call, deliberately** (§4.3's `PROJ` row; ADR-001 §6, `SHPPT`). `Project` writes it half at a time and `DrawShipAsPoint`, the planet drawer's `CircleOffScreen`, `DrawBall`, `DrawEllipse` and `DrawSun` read what the last `Project` left; `DrawPlanetDetail` rewrites it for the crater. Stays a parameter. |
| `Projection.x1` | `K3+1` | DrawPlanetDetail, Project | CircleOffScreen, DrawBall, DrawEllipse | DrawShipAsPoint (after Project), DrawSun (after CircleOffScreen) | **State, deliberately**, with `x`: the stale `K3+1` `SHPPT` reads is the ADR row. |
| `Projection.y` | `K4` | DrawPlanetDetail, Project | CircleOffScreen, DrawBallLine | DrawPlanetDetail (after DrawHalfEllipse), DrawShipAsPoint (after Project), DrawSun (after CircleOffScreen) | **State, deliberately**, with `x`. |
| `Projection.y1` | `K4+1` | DrawPlanetDetail, Project | CircleOffScreen, DrawBallLine | DrawSun (after CircleOffScreen) | **State, deliberately**, with `x`. |
| `K3Block.*` | `K3 to K3+9` | DecideDisposition, DecideMissile, LoadPlanetAxis, LoadStationAxes, NormaliseAxes, OffsetAxis, SubtractShipAxis | BuildUnitVector, NormaliseAxes, OffsetAxis | RunDockingComputer (after SubtractStationAxes) | **Parameter and result** (M2-c). `SPS1` (`LoadPlanetAxis`, `NormaliseAxes`) leaves the vector `BuildUnitVector` and part 9 read; `TAS2`/`OffsetAxis` take and return it: `UnitVector`/`Vector24`, §4.3's `XX15 after SPS1` row. |
<!--census:end-->

The `_a`/`_x`/`_y` parameters are renamed for what they carry (`_seed`, `_axisOffset`, `_highBits`)
at the same time; the `// 6502:` comment keeps the register.

### 4.3.1 The boundary carries, audited (M2-d, 2026-09-06)

Twenty-four `bool _carryIn` parameters cross a boundary that is not the arithmetic kernel's. Each
one is a place where the original had a 6502 flag live across a `JSR`, and the audit below walks
back from every call site in `Design/Reference/compile-source.txt` to the instruction that decided
the flag. Three classes come out, and three of the twenty-four were being passed the wrong value.

| Routine (6502) | What the original leaves at each call site | What the port passes | Verdict |
|---|---|---|---|
| `SpawnDebris` (`SPIN`), `SpawnItems` (`SPIN2`) | `.nosp` is reached from `CMP #AST`, from `CMP #Mlas` or by falling out of `SPIN2`; the second `JSR SPIN` runs on the first one's exit | `false`, `false` | **Wrong, fixed.** Both return their exit carry now (`DORND`'s, or the last `SFS1`'s, which is `NWSHP`'s "was it made"), and `MA47` threads it |
| the splinter roll (`JSR DORND` at `MA47`) | `CMP #Mlas` was EQUAL, so the carry is SET | `false` | **Wrong, fixed** |
| `StartEcm` (`ECBLB2`) from `M32` | `LSR A / BCS` -- taken, so SET | `false` | **Wrong, fixed.** Unobservable (`NOISE` only returns it when the sound is off, and `ECBLB2` discards it), and wrong all the same |
| the missile-lock `Beep` at `MA47` | `HITCH`'s `SEC`, which `LDA MSAR` and `BEQ` do not touch | `false`, beside a local already set to `true` for the same flag | **Wrong, fixed.** Observable on a silent build: `NOISE` hands it back and `LL9` seeds a cloud on it (§6.157) |
| `TakeDamage` (`OOPS`), `DamageEquipment`, `FireLaser` (`LASLI`), `SeedExplosionCloud`, `EraseShip`, `DrawShip` (`LL9`), `DrawBall`/`DrawBallLine`, `SeedDebris` (`Ze`), `SpawnThargoidPair` (`GTHG`), `AddDebris` (`fq1`), `SeedStardustAndClearShips` | a computed flag: a compare, a shift, or a callee's answer | the same flag, computed | **Live and modelled.** The parameter is the routine's operand and stays |
| `RunSpawning` (`MTT1`), `RunLoopTail` (part 5), `BuildSystem` (`SOLAR`), `StartEcm` from the flight loop's E.C.M. key | inherited from before anything the port models: `M%`'s exit for the spawner, `RES2`'s (and `ZERO`'s) for `SOLAR`, and possibly `WARP`'s for the key | `false` at every caller | **Honest assumption.** The parameter is what puts it at the call site instead of inside the routine; `false` is what the port can supply. Collapsing it would hide the assumption, which is why M2-d does not |
| `PlaySoundEffectPitched` (`NOISE2`) | `CPY #&E0` at the Trumble squeak: set for a burning cabin, clear otherwise | the compare, since M3-b-2a | **Restored when the seam went.** `DashboardEffects::PlaySoundPitched` had no flag to carry; the routine takes one now, and `EXNO`/`EXNO2` were audited with it — four `ASL A` on an X between 11 and 15 cannot carry out, so both reach `NOISE2` clear |

**Why the count went up rather than down.** The M2-d row promised `carry-params` "at the kernel's
floor". The audit is the reason it is not: twenty of the twenty-four are the routine's operand, and
the four literals left are assumptions that belong at a call site. `SpawnItems` GAINED one, because
`SPIN2` really does hand its caller's carry back when the count is zero.

### 4.4 `Universe`, `Game`, and the mode machine

```cpp
namespace Elite
{
  struct Universe            // every byte of game state, and nothing else. Plain aggregate, value-copyable, hashable.
  {
    Commander commander;  CurrentSystem current;  SystemSeeds selected;  Market market;
    Bubble bubble;        LineHeap heap;          PlanetSunState heaps;  Stardust dust;
    Ship work;            FlightState flight;     FlightStatus status;   Compass compass;
    Screen screen;        Text text;              Messages messages;     Options options;
    KeyLogger keys;       ControlState control;   TrumbleSprites trumbles; VideoState video;
    Rng rng;              Canvas canvas;          SoundBuffer sound;     MusicPlayer music;
    Mode mode;            JumpState jump;         std::uint8_t explosions;  // EV
  };

  class Game
  {
  public:
    void Reset();                                     // 6502: BR1 / RESET / BAY -- the cold start
    void Step(const InputFrame& _input);              // one pass of FRCE: a docked pass, a flight frame, or a paused pass
    [[nodiscard]] const Canvas& Frame() const noexcept;
    [[nodiscard]] std::span<const SoundEvent> Sounds() const noexcept;
    [[nodiscard]] std::uint64_t StateHash() const noexcept;  // over UniverseImage (§4.7), so it survives refactors
    [[nodiscard]] const Universe& State() const noexcept;
  private:
    Universe m_universe;  Ports& m_ports;
  };
}
```

`Mode` is the explicit machine `Main.cpp` and `DockedSessionTests.cpp` each spell out by hand today:

```
        ┌──────────┐  key      ┌──────────┐  TT110 / TT18 arrival    ┌──────────┐
  Reset─►  Title   ├──────────►│  Docked  ├─────────────────────────►│  Flight  │
        └──────────┘           └────▲─────┘                          └──┬───┬───┘
                                    │ DOENTRY (+ one of seven briefings)│   │ DEATH
                                    └───────────────────────────────────┘   ▼
             CLR/HOME ◄──── Paused ◄──── pause key (either mode)        Dying ──► Title (DEATH2 → BR1)
                              │ Q                                      ESCAPE → Docked
                              └──────────────────────────────────────► Dying
```

`Step` is a `switch` on `mode`, and each arm is what `Advance`, the docked pass and `AdvancePaused`
do now, moved into the library with the executable's pacing (`PlanSteps`, `FlightFrameSeconds`)
left where it is: the executable decides *how many* steps and the library takes them. `Leave`'s
three arms become transitions with the mission briefings as a sub-machine (`DockingOutcome` already
is one). The docked dispatch (`Perform`) moves whole, its `switch` on `KeyAction` intact, because it
is `TT102`'s second half and belongs beside the first.

`InputFrame` is plan §2.1's `{ held, pressed }` over the C64 matrix positions `KeyMap` already
produces, plus the one blocking read the docked screens need (`KeySource::NextKey`), which stays a
port because a windowed program cannot block and the plan does not change that (§2.1 of the plan,
`GameShell::NextKey`'s comment).

### 4.5 Four ports

| Port | Replaces | Methods |
|---|---|---|
| `Presenter` ✅ **M3-b-3b/3c** | `TunnelEffects::ShowFrame`, `TradeScreenEffects::ClearToView` (the pixels half), `LineEntryEffects::WaitFrames`, `StartUpEffects::WaitFrames`, `ExplosionEffects`/`SightEffects`'s VIC pokes (they become `VideoState` writes the library makes itself) | `WaitFrames(n)`, `Present()`, `HoldFlightFrame(ships)`, `HoldTitleFrame(distance)` — four, because one `ShowFrame` was carrying three pacing policies and the two holds are two different cost curves |
| `Keyboard` ✅ **M3-b-3d** | `KeySource`, `ControlEffects::ScanKeyboard`, `StartUpEffects::ScanTitleKeys`, `LineEntryEffects::FlushKeyboard` (`JumpState::controlHeld` is a chart's own byte and stays) | `Held(key)`, `NextKey()`, `Flush()` — **not** `Scan(KeyLogger&)`: `RDKEY` is `Elite::ScanKeyboard` in the library and only the row read is the platform's (§8, 2026-09-06) |
| `SoundSink` ✅ **M3-b-2b** | `DashboardEffects`, `ViewEffects::PlaySound`, `TextEffects::Beep`, `FlightLoopEffects::Start/StopDockingMusic` | `Write(SidRegister, value)` — the library runs `NOISE` and the music player itself and emits register writes, which ADR-003 §1 already says is the port's `SoundEvent` stream |
| `CommanderStore` — **not renamed** | the `SaveScreen` half that reads and writes files, which it became in M3-a-3 | `Write(name, file)`, `Read(name, outFile)` — the row said `SaveStore`, and `Outpost::SaveStore` has been the executable's implementation of it since slice 2d, so the rename would give `class SaveStore : public Elite::SaveStore`. `CommanderStore` says what is stored and `Outpost::SaveStore` says where it goes (§8, 2026-09-06) |

**`Elite::Ports` is the intermediate this table collapses.** M3-a-3 left one struct of fourteen
references — three printers and eleven interfaces — where seven argument-list structs had held
seventy-eight, and that is the whole surface M3-b works on: the four rows above replace the eleven,
and no signature changes again. Eleven references after M3-b-3d, with `SaveStore` the row still to
land — and each port has arrived in the same commit as at least one removal, because a ceiling with
zero slack does not let a struct grow first and shrink afterwards (§8, 2026-09-06).

Everything else in the twenty-two is either a call into a routine that now exists (`RunTactics`,
`DrawPlanetOrSun`, `SpawnAhead`, `Anger`, `SpawnChild`, `ChartShapes`, `DrawExplosion`,
`SeedExplosionCloud`, `ResetUniverse`, `ResetShip`, `ClearKeyLogger`, `StartTheme`, `StopTheme`,
`ShowTitleScreen`, `Run(controlCode)`, `ClearScreen`) or a `VideoState` write.

**`TextSink` AND `ValueTokens` ARE NOT IN THE TWENTY-TWO AND DO NOT GO.** This section used to group
them with `ControlCodes` as "the text system's own polymorphism", and then concluded they should go
because `CHPR` and the value tokens exist — which does not follow. `TextSink` is not a seam in front
of `CHPR`; it is the interface `CHPR` IMPLEMENTS. Both production implementations are inside
`GameLogic` and form a chain — `TokenPrinter` → `CharacterPrinter` → `TextPrinter` → `Canvas` — and
seventeen fixtures use it to compare token expansion as a CHARACTER STREAM against the shipped
routine, which is a more precise instrument than the pixel comparison that would replace it. §6.73's
corollary points the other way here, and this is the first seam in M3-b where it does (§8,
2026-09-06). `ValueTokens` is the same shape: `StateTokens` is its only production implementation
and it exists to break a construction cycle inside the library. `PlaySound`'s carry stays inside
the library where `NOISE` lives. The null port for tests is `NullSeams`, one class over six interfaces
(M3-b-4c). It carries no transcript and must not grow one: a fixture that wants to know a seam was
reached passes something that counts, and the two things it DOES decide -- whether the store failed
and whether the bubble had room -- are booleans on it rather than second classes.

### 4.6 Pipelines with named stages

**The flight frame** keeps `MainFlightLoop` as its orchestrator and turns each annotated part into
a function with a typed result, so that the local `bool`s become one value:

```cpp
enum class Contact : std::uint8_t { Clear, Docking, Scoopable, Collision };  // part 7's four answers
enum class ScoopResult : std::uint8_t { Stowed, HoldFull, Crashed };         // part 8
enum class DockingTest : std::uint8_t { Arrived, TooFast, Bumped };          // part 9
enum class Impact : std::uint8_t { None, Bounced, Bumped, Crashed };         // part 10's three entries
struct Aim { bool draws; bool carry; };                                      // part 11's answer to `LL9`
struct LaserHit { bool stores; std::uint8_t energy; };                       // part 11's, already a struct
enum class KillOutcome : std::uint8_t { Kept, Removed };                     // part 12
```

**AMENDED FROM WHAT WAS BUILT (M4-a-2, §8).** The sketch above had `ScoopResult` as a struct with an
`optional<Cargo>`; it is an enum, because part 8's item never leaves the routine — it goes straight
into `QQ20` and the caller only needs to know which of `MA59`, `MA58` and "stowed" it took. Part 10's
three entries needed a name of their own (`Impact`) and part 11 needed `Aim` for the pair `LL9` is
reached with, so seven types where the sketch drew four. `Contact::None` is `Clear` and
`DockingTest::Bounced`/`Fatal` are `Bumped`/`TooFast`, each named for the 6502 label rather than for
the consequence.

`KillShip` returning "the index must not advance" is what `KillOutcome` says, and the slot loop is
still `for(;;)` with the index advanced by hand rather than `while (auto slot =
bubble.NextOccupied(slot))`: `KILLSHP` shuffles every slot above the dead one down, so the loop must
go round on the SAME index, and a `while` over an iterator would have to un-advance it. The
original's shape is the honest one and the comment on it is the whole explanation.

**`LL9`** becomes SEVEN stages over a `ShipRender` frame object: `Visible?` → `ScaledOrientation` →
`FaceVisibility` → `ProjectedVertices` → `EdgeSelection` → `ClippedLines` → `HeapRun`, each a
function of the previous stage's result. (This row said "six" and then listed seven, which is also
the number of annotated part blocks `DrawShip` carries — parts 1, 2, 3, 4–5, 6–8, 9 and 10–11.
Counted from the tree rather than from the sentence, M4-a-3.) `DrawShipAsPoint` and `EraseShip` stay as they are (the
ADR-001 §6 row on `SHPPT` is about `Projection` outliving a call, and the stage form makes that
visible rather than hiding it).

**`TACTICS` and `DOCKIT`** return a `Decision` (`enum class Manoeuvre { Flee, Attack, Fire, Launch,
Steer, Halt, ... }` plus the parameters each needs) that a single `Apply(Ship&, Decision)` performs.
The seven parts become `DecideMissile`, `DecideStation`, `DecideEscorts`, `DecideCombat`, in the
order the fall-through runs them, and `OOPS`'s three paths become `Decision::Fatal`.

### 4.7 The oracle bridge and the replay hash

`Tests/GameLogicTests/UniverseImage.h`: one function pair over `Universe` and the label map —

```cpp
void Materialise(const Elite::Universe&, Cpu6502&, const Labels&);      // port -> 6502 memory, every label the game has
void Absorb(const Cpu6502&, Elite::Universe&, const Labels&);           // and back
[[nodiscard]] std::uint64_t Hash(const Elite::Universe&);               // FNV-1a over Materialise's bytes
```

— which is `FlightUniverse.h`'s `Mirror`/`CompareState` promoted to one table of cells. **Amended at M0-b:**
there is no generated `Labels.h`. The table maps port fields to label NAMES and sizes, which
`Labels.txt` cannot supply, and the addresses are looked up at runtime through `OracleImage::Label`
exactly as `Where` does; Q7 closes as "not needed".
The invariant every M1–M4 slice is measured by: **`Materialise` produces the same bytes before and
after the slice for the same game**, and every oracle test compares through it rather than through
`math.k[3]`. `Game::StateHash` is `Hash`, so the replay suite ADR-003 §3 asked for — the docked
transcript and a flight script through the null port, hashed at every step, stored — is
layout-independent by construction and is the one instrument that sees composition.

### 4.8 What the executable becomes

`Outpost/` keeps `Window`, `CanvasPresenter`, `SidSynth`, `SoundOutput`, `KeyMap`, `SaveStore` and
`Presentation.h`'s pacing, and gains one `Platform` class implementing the four ports. `Main.cpp`
becomes: create the window and the device, build `Game` over `Platform`, and loop `PlanSteps` →
`game.Step(input)` → present, with `Guarded` around it — the two hundred lines ADR-004 §1
described. `FlightSession` and `GameShell` are absorbed: the universe half into `Universe`, the eight-
interface halves into `Platform`. `check_outpost.py` keeps running and has almost nothing to check.

### 4.9 C++20 used, C++23 held

Used from C++20: `std::span` (fixed-extent for the codecs), `constexpr` codecs with
`static_assert` round trips, scoped enums with `using enum` in the dispatch, designated initialisers
for the stage results, `<bit>` (`std::rotl`, `std::bit_cast` for nothing wider than a byte),
`[[nodiscard]]` everywhere a result carries a flag, concepts for the two ports that are templated in
tests. Held for Q1: `std::expected` (the `{value, carry}` structs are not errors, so the case is
weak), deducing `this` for the typed views, `std::to_underlying`, `std::print` in tools. Nothing
in the plan depends on any of them.

### 4.10 The detachment: recorded fixtures replace the live oracle

Every oracle test has the same shape: build a 6502 state, `Call(label, state)`, read memory back,
compare with the port. The interpreter is reached through one object, `OracleImage`, and that is the
seam the detachment uses — **the tests do not change shape; what answers them does.**

```cpp
class Oracle                     // Tests/GameLogicTests/Oracle.h
{
public:
  virtual State Call(std::string_view _label, const State& _in) = 0;      // the one method every test uses
  virtual std::span<const std::uint8_t> Memory() const = 0;
};
class LiveOracle final : public Oracle { Cpu6502 ... };                    // today's OracleImage, until M6-b
class RecordingOracle final : public Oracle                                 // M6-a: wraps a LiveOracle, writes the fixture
{ /* per call: FNV-1a of (label, _in, the bytes the test reads back) folded into the test's digest;
     below a size threshold the full (input -> output diff) is written too, for diagnosis */ };
class RecordedOracle final : public Oracle                                  // M6-b: serves the fixture; no interpreter
{ /* a call whose input hash the fixture does not hold fails the test loudly: the test asked the
     original something it was never asked while the original was here */ };
```

The fixture per test file is `Tests/Fixtures/<Suite>.oracle`: one digest per `TEST_METHOD`, and the
full input-to-output records for tests whose record is under the threshold M6-a measures (the
exhaustive 65,536-case sweeps are digests only; a sweep that fails after M6 is re-run against the
port's previous commit to find the case, which is the diagnosis path the plan accepts in exchange for
a tree with no interpreter in it). The whole-bitmap comparisons record the bitmap bytes they read, so
a drawing test after M6 still says which byte differs. The mutation harness needs no change: it runs
the suite, and the suite no longer needs an oracle to be present — `check_oracle_present` goes with
it, and with it the one deliberate failure `OracleIsPresent` (R9 closes by construction).

**What a fixture can and cannot pin.** A fixture pins exactly the calls the tests made while the
original was here. A behaviour no test reached before M6-b is unpinned for ever afterwards, because
nothing can ask the original again. That is Risk R19, and it is why M6 is last and why M6-a begins
with a coverage review: every routine the ledger marks *Port* must have a test that calls it before
its answers are recorded, and the M0-c replay must cover launch, flight, combat, docking, death and
the escape pod. The data tables need nothing here: their oracle comparison (`extract_tables.py
--check` and `TableTests`) was retired on 2026-09-07, and the tables are already the port's own C++.

**What M6 removes, in order**: the label names from identifiers (M6-c), the assembly from the
comments (M6-d), the markers and the ledger with `inventory.py` and AGENTS.md R7 (M6-e), and then
`Upstream/`, `MasterFile/`, `Cpu6502`, `OracleImage`, `labels.py`, `c64_source.py`,
`extract_tables.py`'s assembler half, the BeebAsm steps in both CI jobs, and the count markers that
described the masters (M6-f). ADR-001 §5 and Risk R1 are restated at M6-f to what is then true: the
tree carries the original's data and the port's own code, and nothing of its source.

---

## 5. Rules every slice obeys

1. **The oracle decides, until M6 records it.** A slice ends with the suite green on the portable
   runner with the oracle present and, before merge, on the Windows job. "Builds, not run" is not
   a state a slice is left in. The replay record (M0-c) moves in exactly two cases: the digest is
   deliberately widened, or a defect in the port is found and fixed — "the port was wrong, the
   record is not needed" (§1 R-e) — and the journal entry names which. After M6-b the recorded
   fixtures decide, and a fixture is never re-recorded — nothing remains to record it from — so a
   changed fixture is a changed game, by definition, and needs its own ADR under plan §6 Phase 6.
2. **No width changes.** Every `std::uint8_t` that becomes a typed field keeps eight bits and its
   wraparound; every widening happens inside a helper narrowed the way the original narrowed
   (ADR-002 §3). A codec's `static_assert` round trip is the proof for a struct; the oracle byte
   compare is the proof for a routine.
3. **A mutant is re-anchored, never dropped.** A slice that touches a line a mutant's `find` names
   updates `tools/mutants.json` in the same commit and re-runs that unit (`mutate.py --unit X
   --runner portable`); the tally goes in the slice's journal entry. `mutate.py --check` in CI is
   the backstop, not the process.
4. **`// 6502:` and the ledger survive until M6-e, and go together.** Until then a renamed function
   keeps its label on the declaration, a field that replaces an offset carries the label the offset
   carried, and `Source-Inventory.md`'s *Home* column changes in the same commit as the file,
   inside the notes column and never as a new cell (`check_docs.py`). They are the map from the
   port back to the original, and the map is needed exactly as long as the original is.
5. **The ratchet only goes down.** `tools/check_modernize.py` fails when any P1–P11 count exceeds
   its recorded ceiling; a slice that lowers a count lowers the ceiling in the same commit. It
   also fails when a ceiling is above the count by more than the slack it records, so the file
   cannot quietly stop describing the tree (§6.154's rule, mechanised).
6. **Every signature change reaches `Outpost/` in the same commit**, and `check_outpost.py` runs —
   which since M3-0 checks the members the app names as well as the names and the arities.
   Until M3 lands the executable is the caller this environment cannot compile, and a Windows job
   red on a type change is the failure mode; keeping the diff small per slice is the mitigation.
7. **Rename toward meaning as you go.** A slice in M2 or M4 that touches `_math.q` or `xx15` names
   it for what it holds in that routine (`divisor`, `unitVector`), so that M6-c is a sweep of what
   nobody touched rather than a second pass over everything; the `// 6502:` comment keeps the old
   name beside it until M6-e.
8. **One pattern per slice.** A slice removes one pattern from one unit. A slice that "while it is
   in there" renames a second thing is two slices and is split before review.
9. **Original bugs stay ported** (ADR-001 §3, §6). A refactor that would fix one — a typed
   `Contact` that cannot express `SHPPT`'s reading of a stale `K3+1` — is wrong, and the ADR row is
   the test that says so.
10. **New files go in both project files** (`.vcxproj` and `.filters`) and are unique repo-wide;
   `check_projects.py` runs.
11. **The journal is written as the slice lands** (§8), numbers unmarked, in the plan's convention.

---

## 6. The build order

Each slice: what it does, the files, the acceptance criterion, and the risk it carries. Sizing is in
sittings of a few hours, as the conversion plan's §7 counts them, and is a guess to be corrected in
the journal.

### Phase M0 — The safety net (no product code changes)

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M0-a Ratchet** | `tools/check_modernize.py` counting P1–P11 with `tools/modernize_ratchet.json` as the ceilings; wired into `check_all.py`, the workflow and `check_counts.py`'s names so this document's numbers are checked. | In CI; a deliberately raised count fails `--self-test`. **Built 2026-09-06 with this document** (§8). | 1 |
| **M0-b UniverseImage** | `Materialise`/`Absorb`/`Compare`/`Hash` over the fixture's `Universe`, as one table of cells with the labels resolved at runtime (the slice plan below says why there is no generated table). `FlightUniverse.h`'s `Mirror`/`CompareState` become calls into it. | Every existing flight-universe test passes unchanged through the bridge; `Hash` is stable across two runs; four tests of the bridge itself. **Built 2026-09-06** (§8). | 2–3 |
| **M0-c Flight replay** | A scripted flight through a test-side port that answers every seam with the routine the executable calls (launch from Lave, coast, accelerate, fight a hostile Viper, dock on the autopilot), digested at every hundredth step and every turn of the script, the record stored in the suite. | Green; the same script twice gives the same digests; a one-byte change in four places each changes the record (the harness's own selftest). **Built 2026-09-06** (§8). | 2 |
| **M0-d Mutation baseline on Linux** | `mutate.py --runner portable` for all five units, tallies journaled, so the re-anchoring in later slices has a number to match. | Five units, zero survivors beyond the recorded equivalents. **Built 2026-09-06**, and re-run after M0-b and M0-c so that the bridge is shown to catch what the fixture caught (§8). | 1 |

**Phase M0 is complete, 2026-09-06.** Four slices built and green on both legs; the Windows job
compiled the executable and ran the suite in Release against the same replay record the portable
runner took, which is the cross-compiler half of ADR-003 §3's determinism check done by CI rather
than by hand.

#### M0-b slice plan (written 2026-09-06, before the build; §8 records what the build found)

**Name.** The aggregate is `Universe`, by owner ruling of 2026-09-06 ("we are in space"): the fixture
`FlightWorld.h` becomes `FlightUniverse.h`, `struct World` becomes `Universe`, `LoopWorld` becomes
`LoopUniverse`, the bridge is `UniverseImage`, and M3-a's `Elite::World` is `Elite::Universe`. Two
consequences. `GameLogic/Universe.h` today holds the seed twisting (`TT20`, `TT54`, `TT24`, `TT111`,
`cpl`, `PDESC`) and file names are unique repo-wide, so **M3-a renames that pair to `Galaxy.h/.cpp`**
— it is the galaxy generator, and the ledger's *Home* column follows it. And the word "world" where
it means a PLANET (`Market.h`'s "an agricultural world", `Spawn.cpp`'s meridians, `Universe.cpp`'s
"two worlds a light year apart", `TheWorldTurningMatchesMV`) is left alone: that is a different noun.

**Decision 1 — where the label table comes from.** Nowhere: it is not a table of addresses. What the
bridge needs is the map from a port field to a label NAME and a byte count, and that is C++ that
`Labels.txt` cannot generate. The addresses are resolved at runtime through `OracleImage::Label`, as
`Where` already resolves sixty of them, and `Where` stays as that cache because thirteen suites read
its fields directly. §4.7 is amended; Q7 closes.

**Decision 2 — what "the whole state" is before M3-a.** The fixture's `Universe` plus what the suites
already mirror into it. When M3-a lands, `ImageCells` is re-pointed at `Elite::Universe` in one
commit; the cell names and addresses do not change, so the stored replay hashes of M0-c survive it —
which is the property the whole design exists for.

**Decision 3 — the call sites.** `Mirror` and `CompareState` keep their signatures and become the
bridge's `Materialise` and `Compare`; the forty-nine calls in seven suites are untouched. A cell
carries its scope — `Seeded`, `Image` or `Compared` — so that the asymmetry the two functions had
(`Mirror` wrote a superset of what `CompareState` checked) is preserved exactly, and widening the
compared set is a later, deliberate change with a finding behind it.

**Decision 4 — what the hash covers.** Every cell the image writes except the seeded constants
(`spasto`). The VIC-II sprite coordinates only when the fixture claims them, because they alias
`XX21` in the flat image (§6.108). The sound buffer and the rest of `VideoState` are not in the image
at all: the oracle holds them in hardware registers rather than memory, so they enter the hash at
M3-a as port-side bytes when `Elite::Universe` owns them.

**Rename, done ahead of M3-a.** `GameLogic/Universe.h/.cpp` is `Galaxy.h/.cpp` since 2026-09-06 (owner
ruling with M0-c), `UniverseTests.cpp` is `GalaxyTests.cpp`, and the ledger's *Home* column follows.

**Acceptance, as built:** the suite unchanged and green through the bridge on the portable runner;
four new tests in `UniverseImageTests.cpp` (every cell resolves to an address; a round trip through
memory loses nothing, the nine-bit sprite x included; the hash is stable across two seeds and notices
one byte; what is written is what is compared, and one changed byte is one named difference);
`check_all` green.

#### M0-c slice plan (written 2026-09-06 with the build; §8 records what the flight found)

**The port.** `Tests/GameLogicTests/FlightPort.h` is `Outpost::FlightSession` with the platform half
replaced by data the script owns: the keys held this frame are an array, the sound goes into the
game's own `SoundBuffer` and `SidWriteLog`, the raster mode is remembered. Every game-half answer —
`RunTactics`, `DrawPlanetOrSun`, `SpawnAhead`, `Anger`, `SpawnChild`, `DrawExplosion`, the chart
shapes, the docking computer, `CLYNS` for the message countdown — is the routine the executable
calls with the same arguments, so that a flight through the port is the flight the app would run.
`Step()` is one pass of `TT100` as `Main.cpp`'s `Advance` performs it, without the pacing and the
window's key queue. It is the second copy of `FlightSession`'s wiring and says so; M3-b replaces both
with direct calls, and this port is what makes M3-b measurable before it lands.

**The digest.** The universe image (`Hash(const Universe&)`, oracle-free: a `Where` with every address
zero, because the hash reads cells in table order and never their addresses) widened with what the
image does not carry and a flight changes — the pixels, the ship line heap, the flight controls —
because the image leaves those to the oracle's other comparisons and a replay has no oracle. Every
part is a byte layout ADR-002 fixes. Not in the digest, and said so: the sound buffer, the SID log,
`VideoState` beyond the sprites, and `ScreenState`'s raster half, which the executable ticks at the
display's rate rather than the frame's. They enter at M3-a with `Elite::Universe`, and that is the one
re-recording the plan allows for.

**The script.** Launch from Lave (the default commander, a docking computer bought for the last
phase, `RESET`, `TT110`); forty frames coasting; sixty at full speed with a roll for the first
twenty; a hostile Viper straight ahead (`FRS1`, then `ANGRY`) and two hundred and forty frames of a
pulsing laser and a pitch; the docking computer engaged; and the autopilot until the frame that
docks or a cap of four thousand. The spawner runs on the way — `MCNT` wraps four times in the
record — so traders and pirates the script never named are in the digest too. Digests every
hundredth step and at every turn of the script, stored as constants in the suite; an empty record
fails, so the tree can never carry an unpinned replay; the failure message prints the whole new
record in paste-able form.

**When the record may change.** Never for a refactor. In two cases, each with a journal entry that
says which: the digest is deliberately widened, or a defect in the port is found and fixed and the
record follows the fix (§1 R-e, ruled 2026-09-06). There is no re-record for "the numbers moved" —
a moved record with no defect named is a refactor that changed the game.

### Phase M1 — Typed data

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M1-a ShipView** | Named accessors on `ShipBlock` (`X()`, `Y()`, `Z()`, `Nose()`, `Roof()`, `Side()`, `PositionAt()`, `VectorAt()`, `ComponentAt()`, `Speed()` … `Newb()`), the layout named once and `static_assert`ed; every literal and named-offset site migrated; the duplicate offset and type constants removed. Bytes unchanged. | Oracle suite green; `ship-literal-sites` and `ship-offset-sites` at zero in the ratchet; mutants in `Tactics.cpp` and `Missions.cpp` re-anchored and re-run. **Built 2026-09-06** (slice plan and §8 below). | 3–4 |
| **M1-b ShipType and the flag types** | `enum class ShipType`, `ShipStateBit`, `AiBit`, `NewbBit` with the original bit values, the byte-valued helpers beside them; the second naming families deleted. | Green; `check_outpost.py` green after the app's constants follow. **Built 2026-09-06** (slice plan and §8 below). | 2 |
| **M1-c Ship with a codec** | `Ship` struct replaces `ShipBlock`; `ToBytes`/`FromBytes`; `Bubble::blocks` becomes `std::array<Ship, 10>`; `NWSHP`'s copy and `MAL2`/`MAL3` become struct copies and the partial copies codec calls. Tests migrate to the bridge. | Green through `UniverseImage`; `Materialise` bytes identical to M1-b's for the replay scripts (M0-c's stored hashes do not change). **Built 2026-09-06** (slice plan and §8 below). | 4–5 |
| **M1-d Commander** | Typed `Commander` with the seventy-seven-byte codec; `Credits` and `Tally` strong types where the bytes' order is the risk, plain named bytes elsewhere; `SaveCommander`/`LoadCommander`/checksums over the codec. | `SaveGameTests` and `CommanderTests` green; a commander file from R12's fixture still loads. **Built 2026-09-06** (slice plan and §8 below). | 3 |
| **M1-e Blueprint** | Parsed `Blueprint` table; `ShipByte`/`BlueprintAddress`/`ShipBlueprintExtent` removed, `BlueprintFor` returns the parsed entry; `XX0` a pointer; `Bubble::stationType`. | `ShipDataTests` green with the three disagreeing ships called out as before; `LL9` suite green. **Built 2026-09-06** (slice plan and §8 below). | 3 |
| **M1-f HeapOffset** | The line heap addressed by offset; `TryReserveHeap` with the exact chain; the sun's heap lent as a span. | `NWSHP`'s refusal sweep green (`ShipSlotTests`); the station-into-sun-heap case (§6.112) green. **Built 2026-09-06** (slice plan and §8 below); M1 complete. | 2–3 |

#### M1-a slice plan (written with the build, 2026-09-06; §8 records what the build found)

**The view is on `ShipBlock` itself, and the bytes stay.** `ShipBlock` keeps `bytes` and
`operator[]`, and gains accessors that are references into the array: `X()`, `Y()`, `Z()` return an
`AxisBytes` of `lo`, `hi` and `sgn`; `Nose()`, `Roof()`, `Side()` return a `VectorBytes` of six; the
tail is one accessor per byte, `Speed()` to `Newb()`. Nothing is copied, every write lands where
`STA INWK+n` landed, and the oracle compares the same bytes it compared before. A struct with a
codec is M1-c's step; this one changes no layout and so needs no bridge.

**The offsets are the layout, named once.** `SHIP_X_OFFSET` to `SHIP_FLAGS_OFFSET` live in
`ShipSlot.h` with `static_assert`s between them, and the accessors are built on them. The three
axis offsets came home from `ShipDraw.h`; the second families — `FlightLoop.cpp`'s ten
`std::size_t`s, `Combat.cpp`'s two, `Spawn.cpp`'s `SHIP_AI_OFFSET`, `StartUp.h`'s `SHIP_COBRA_MK3`
and `SHIP_ADDER` — are gone, and the executable's one use follows.

**The routines entered with an offset in a register keep the offset.** `MVT1` with X = 0, 3 or 6,
`MVS4` with Y = 9, 15 or 21, `MVS5` with two component offsets: their parameters are unchanged (M2
renames them) and their bodies address the block through `PositionAt`, `VectorAt` and
`ComponentAt`, taken once at the top as a named view. `TIDY`'s helpers, which indexed the high
bytes at `10 + n` and `16 + n`, say `SHIP_NOSE_OFFSET` and `SHIP_ROOF_OFFSET`.

**What stays as `operator[]`.** Five wholesale copies — `NWSHP`'s and `MAL2`'s byte loops and the
two into `K3` — which are the thing M1-c's codec replaces. They index by a loop variable, not a
number, and the ratchet does not count them.

**The migration was mechanical and the mutants followed it.** One regular expression over the
receivers a block is reached through (`work`, `_ship`, `block`, `_slot`, `station`, `victim`, the
`blocks[...]` of a bubble, and the rest) rewrote the fixed sites in the library and the eighteen
`find`/`replace` fields in `tools/mutants.json` that named one, in the same pass, so that
`mutate.py --check` was green before the first build.

#### M1-b slice plan (written with the build, 2026-09-06; §8 records what the build found)

**The type is an enumeration and the byte stays the wire format.** `enum class ShipType :
std::uint8_t` in its own header (`ShipType.h`, because `ShipBlueprint.h` and `ShipSlot.h` both need
it and one includes the other) carries the original's numbers — 1 to 33 in `XX21`'s order, 128 and
129 for the planet and the sun — and `Byte`/`TypeOf` cross to and from the byte. `FRIN`, `MANY` and
the image keep holding bytes (`Bubble::slots`, `Bubble::counts`); the API speaks the type
(`Bubble::Count(ShipType)`, `FlightState::type`, every `_shipType` and `_parentType` parameter, the
`ShowTitleScreen`, `SpawnAhead`, `Anger` and `SpawnChild` seams); and the predicates the source has
as ranges — `IsBody` (bit 7), `IsJunk` (`JL`..`JH` and `HER`), `IsWreckage` (`PLT`..`SPL`) — are
`constexpr` beside it. Where a routine does arithmetic on a type (`ADC #SHU-1`, `E%-1,X`, the
docking computer's `STA TYPE` of &E0, `NWSPS`'s type from the tech level), the site says `Byte(...)`
or `TypeOf(...)` and the arithmetic stays visible rather than hidden in an operator.

**The flags are bits with names, and the helpers are values.** `ShipFlags.h` holds `ShipStateBit`
(bit 6 named twice, `Firing` and `CloudDrawn`, told apart by bit 5 as the source tells them),
`AiBit` (bit 6 named twice too: `Hostile` for a ship's aggression field, `AimedAtPlayer` for a
missile), `NewbBit` in the order `TA1`'s `LSR` walk reads them, and `MissilesOf`, `MissileTargetOf`
and `MissileAiFor` for the two fields that share those bytes with the flags. `Has`, `HasAny`,
`With`, `Without` and `Mask` are the `AND` and `ORA` immediates, as values. There is no
by-reference `Set` or `Clear`: the first build had them, the ratchet read the three `std::uint8_t&`
parameters as P10 coming back, and it was right — a helper that takes a byte by reference is an
untyped byte with a nicer name, and the type that owns the byte is M1-c's. So a site is
`work.State() = With(work.State(), ShipStateBit::Killed)`, which is the load-modify-store of
`LDA INWK+31 / ORA #%10000000 / STA INWK+31` and no shorter than it.

**The second naming families are gone.** `SHIP_TYPE_x` and `SHIP_STATE_x` from `ShipSlot.h`,
`JUNK_TYPE_FIRST`/`JUNK_TYPE_LIMIT`, `FlightLoop.cpp`'s `SHIP_KILLED`, `SHIP_EXPLODING`,
`SHIP_DRAWN_OR_EXPLODING`, `NEWB_REMOVE`, `NEWB_INNOCENT` and `NEWB_HOSTILE`, `Tactics.h`'s
`NEWB_STATION_ALLY` and `NEWB_HOSTILE`, and `Spawn.h`'s and `GameLoop.h`'s copies of the types. A
whole-byte value with a name of its own stays (`STATION_LAUNCH_AI`, `SPAWN_CHILD_AI`,
`HERMIT_PIRATE_NEWB` — the last now written as the `Mask` it is).

**What stays as bytes, deliberately.** `TA1`'s `LSR` walk over `NEWB` (`flags >> 1`, `>> 2`) is the
source's control flow and M4's to restructure; `NWSHP`'s caller rotating the AI's carry into
`INWK+31`; `INWK+32` used as a loop counter when the escape pod launches; `DEC INWK+31` spending a
missile. The tests keep their byte fixtures and byte sweeps and cross with `TypeOf`/`Byte` at the
call, so a sweep over all 256 types still runs through the same code.

**The mutants followed.** Four re-anchored (`ta-240`, `msl-82`, `msl-bit5`, `kill-rotate`) and none
dropped; `msl-82`'s replacement now sets the ECM bit on the station's missile address rather than
writing `0x83`, which is the same byte with its meaning visible.

#### M1-c slice plan (written with the build, 2026-09-06; §8 records what the build found)

**The struct is the model and the bytes are its wire format.** `Ship.h` holds `Ship` as fields in
the bytes' order — `SignMag24 x, y, z` (`EliteTypes.h`'s type, which already existed for exactly
this and gained `operator==`), `Vector16 nose, roof, side` of three `SignMag16` components, then
`speed`, `acceleration`, `rollCounter`, `pitchCounter`, `state`, `ai`, `heapLow`, `heapHigh`,
`energy`, `newb` — and `ToBytes`/`FromBytes`, the one place the K% layout is written down, with a
`static_assert` round trip over thirty-seven distinct bytes so the ORDER is proved and not just the
count. `state`, `ai` and `newb` stay bytes under `ShipFlags.h`'s names and the heap pointer stays two
bytes: the owning types are M1-f's and the flags' own slice's, one pattern at a time.

**The accessors went with the bytes.** M1-a's `X()`, `Nose()`, `Speed()` … `Newb()` were views of
references because there was nothing else to return; a field needs no accessor, so the 491 sites
say `work.x.hi`, `work.nose.z.hi`, `work.state` — the same regex pass as M1-a, over the same receiver
list, with the eighteen mutant anchors rewritten in it. `ShipBlock` is `Ship` at its 217 mentions,
`ClearShipBlock` is `ClearShip` and is `_work = Ship{}`.

**The offset-parametric routines keep their offsets and lose their arithmetic.** `PositionAt(0|3|6)`,
`VectorAt(9|15|21)` and `ComponentAt(9..25 odd)` return the field a register offset names, and M2
replaces the offset with an enumeration. The routines that indexed through arithmetic on the offset
are now typed: `PUS1` swaps `vector.x` and `vector.z`; `TIDY`'s eight low-byte clears are eight
named pointers with `side.z.lo` visibly absent (the off-by-one the oracle insists on); the rear
view's eight sign flips likewise; `PLS1` reads a `ComponentAt`, because its X is 9, 11, 21 or 23 —
a VECTOR component with the sign in bit 7 of its high byte, which the first build read as a
position axis and the planet sweep caught at once. A local that took a view by `auto` now copies a
field: the four writers (`MVT1`, `MVS4`, `MVS5`, `NWSPS`'s nose) are `auto&`, and the oracle found
every one of them before the second suite run — twenty tests red, all on `INWK+0..26`.

**Whole copies are struct copies; partial copies go through the codec, where the count shows.**
`NWSHP`'s `NWL3`, `MAL2`, `MAL3` are `=`. `WSL2`'s thirty-two bytes and `MAL4`'s twenty-nine are
`ToBytes`, `std::copy_n`, `FromBytes`, with what is left out named in the comment; `SPL1`'s nine
bytes into `K3`, `LL9`'s eighteen into `XX16` and nine into `XX18` read `ToBytes()`. Eleven codec
calls in the library, and none of them in a routine that reads one field.

**The bridge and the tests speak bytes through the codec.** `UniverseImage`'s K% and INWK cells
are `ShipCells`: get is `ToBytes()[byte]`, set is `FromBytes` of the edited array, so the image is
the K% layout whatever the struct's is — the replay's sixteen recorded digests did not move, which is
the acceptance. In the tests, 161 numeric indexes became fields by the same offset table, 42 byte
loops fill an array and `FromBytes` it, 45 loop reads are `ToBytes()[byte]`, and the 29 fixtures
that are written in a register offset because the routine under test is entered with one use
`PokeShip`/`PeekShip` (`ShipBytes.h`, tests only) — a fixture keeps its `INWK,X`, and the library
never sees the helpers.

**What the regex got wrong, and how it showed.** Two receivers on the list are not ships in every
file: `sun` is also the sun's line heap and a label, `seeded` and `afterClear` are also RNGs, and a
test's `block` is sometimes a byte array. Each misfire was a compile error, none a wrong test; the
lesson for M1-d's pass over `CommanderBlock` is to derive the receiver list per file from the
declarations rather than from the library's habits.

#### M1-d slice plan (written with the build, 2026-09-06; §8 records what the build found)

**The same shape as M1-c, on the block that has a file format.** `Commander` is the fields the
seventy-seven bytes are, in their order, with the two bytes no label names kept as fields (`spare`
after `ESCP`; `lasers` has six entries because `LASER` is six bytes of which four are mounts) so
that `ToBytes`/`FromBytes` is a plain walk written in terms of the `Field` offsets. `Field` STAYS:
it is the wire format's table, the codec is written in it, and the tests address the oracle's `TP`
through it. The `static_assert` round trip over seventy-seven distinct bytes proves the order;
`CommanderTests`' 221 compared blocks and `SaveGameTests`' round trips prove the file.

**Two strong types, where a byte's order is the thing a port gets wrong.** `Credits` holds `CASH`
as tenths and writes the four bytes most-significant first in `Byte`/`SetByte`, which the two
routines that read byte 2 (`EN6`'s Trumbles offer, the competition number) and the printer that
reads all four say by name. `Tally` is `TALLY` and `TRIBBLE`: two bytes, low first, which the game
steps and rotates a byte at a time (`INC TALLY+1`, `ROR TRIBBLE+1 / ROR TRIBBLE`), so the halves
are fields and `Value` is for the readers of the pair. The plan's `LightYearsTenths`, `Laser` and
`Equipment` types are NOT here: `fuel` is compared and subtracted at twenty-nine sites and a type
would be arithmetic operators with a name, the lasers are read as bytes (`LASER,Y` with Y the
view) and written as powers, and the equipment bytes carry `0`, `&FF` and the bomb's `&7F` as the
original carries them. Named bytes are the honest step; a type that earns its operators is M5's.

**The accessors and the second family went.** `At(Field::x)`, `Cash()`/`SetCash()`,
`Kills()`, `GalaxySeeds()`/`SetGalaxySeeds()` and `bytes[static_cast<std::size_t>(Field::x) + n]`
became the field — 319, 40 and 72 sites by regex, twelve that had cached an offset in a local by
hand (`hold + item` is `cargoHold[item]`, `tribble + 1u` is `tribbles.hi`). `Equipment`'s
fittings table, which held a `Field` per item, holds a pointer to member; `STATUS`'s walk from
`BOMB` over five equipment bytes reads `ToBytes()`, because it IS a byte walk (`LDA BOMB,X`) and
saying so is truer than five names. `CommanderBlock` is `Commander` at its 131 mentions.

**The file and the checksums are the codec's first customers.** `CHECK` and `CHECK2` walk
`ToBytes()`; `DefaultCommander` is `FromBytes` of the table; `SaveCommander` writes `ToBytes()`
after the name; `LoadCommander` is `FromBytes` of the file's block, and the byte the original's
`QUL1` never loads — the block's own checksum — is kept by copying the struct and putting the
caller's `checksum` back, which says in two lines what a seventy-six-iteration loop said. The
bridge's `TP` cells go through the same `CodecCells` the ship's do, so the image is the layout
whatever the struct is.

**What the build found.** Nothing in the library: 392 of 392 on the first run after the compile,
which is what a codec proved by a `static_assert` and a file format proved by the oracle's own
`SVE` buys. One mutant anchor (`mi-tally`) named the cached `tally` local and was re-anchored to
`kills.hi`; the executable lost `Elite::Field` and gained `Elite::Commander`.

#### M1-e slice plan (written with the build, 2026-09-06; §8 records what the build found)

**A blueprint is its header, named, and three spans placed where the header says.** `Blueprint`
holds the twenty header bytes as fields (`heapBytes`, `laserVertex`, `vertexBytes`, `edgeCount`,
`bounty`, `faceBytes`, `visibility`, `maxEnergy`, `maxSpeed`, `normalShifts`, `weapons`, and the
rest) and `vertices`, `edges` and `faces` as `std::span<const std::uint8_t>` over the region,
sized by the header's counts and starting at `XX0` plus the header's offset in SIXTEEN-BIT
ARITHMETIC. The table is parsed once from `SHIP_DATA`, per type, into `BlueprintOf(ShipType)`;
`SHIP_HEADER_x` are the parser's layout constants, the wire format's table, and `address` is kept
on every entry for the bridge and the tests, which address the oracle's copy, and read by no
routine. `NO_BLUEPRINT` is twenty zero bytes with address 0 — what `XX0` holds before anything has
set it, and what the old `ShipByte` returned for an address outside the region.

**`XX0` is a pointer.** `FlightState::blueprint` is `const Blueprint*`, never null; the routines
that read it take `const Blueprint&` (`MVEIT`, `LL9`, `HITCH`, `SFS1`'s debris) and the ones
that WRITE it — `NWSHP` and everything that falls into it — take `const Blueprint*&`, which is
the same out-parameter it was in a different type, and M2's to remove. `E%`, `KWL%` and `KWH%` are
functions of a `ShipType` (`DefaultNewbFor`, `KillWorthFor`), not fields of a `Blueprint`, because
the game indexes them by the type in the slot and the station is type 2 under either of its two
blueprints; `DefaultNewbFor` still reads past the table for the negative types, as `NW8` does.

**The station's self-modified entry is a type.** `Bubble::stationType` is `Station` or `Dodo`,
`NWSPS` sets it from the tech level, and `BlueprintFor(bubble, Station)` resolves it. The bridge's
two address pairs — `XX0` and `XX21+2*SST-2` — are `AddressPair` cells that read the pointee's
address and, on a write, reassemble the address from both bytes before looking it up, so a
half-written pair changes nothing. `ShipDraw`'s `V` pointer is an index into the span it walks.

**The seam that carried an address carries the byte.** `SeedExplosionCloud` took `XX0` and read
nothing from it; it takes `explosionCount`, which is `(XX0),7`, the one byte `EE55` reads. That is
also what kept `outpost-elite-names` at 225: a `Blueprint` in the seam's signature would have been
one more `Elite::` name the executable reaches, and the executable does not need the type.

**What the build found.** One test red on the first run, `LL9` for type 8, and the reason is the
finding below. Then 392 of 392, `ShipDataTests` still counting three disagreements and the same two
"overruns" — for a truer reason.

#### M1-f slice plan (written with the build, 2026-09-06; §8 records what the build found)

**A place in the heap is an offset, and the two arena addresses live in one header.** `HeapOffset`
(`HeapOffset.h`) is bytes up from `K%` — ruling R-c's offset arena — with `Byte(Y)` for `(XX19),Y`,
`Back(n)` for a run moved down, and `FromAddress`/`Address` for the wire format and nothing else.
Its DEFAULT is the zero pointer `ZINF` writes, which is outside the arena as the original's is, so
`Ship{}` still codes to thirty-seven zero bytes; `Top()` is `LS%`, where `SLSP` starts; the
station's pointer, `FromAddress(SUN_HEAP_ADDRESS)`, is outside the arena too and `LineHeap`
resolves it against the window it has been lent. `SHIP_BLOCK_BASE`, `SHIP_HEAP_TOP` and
`SUN_HEAP_ADDRESS` moved into the header as the only 6502 addresses left in the model.

**`NWSHP`'s chain is `Bubble::TryReserveHeap`.** The eleven-instruction comparison of the new heap
bottom against the slot's block address is one function, byte for byte as it was in `AddShip`,
with `SlotAddress` (`GINF`'s arithmetic) private to it. It returns the pointer `INWK+33/34` receive
WHETHER OR NOT the ship is admitted — the original writes them before it decides, and the refusal
sweep compares them — and moves `heapBottom` only when the ship fits.

**`Ship::heap` is a `HeapOffset`, `Bubble::heapBottom` too, and `LineHeap` reads and writes by
offset.** `ShipHeapAddress` is gone: `LL9`, `EE31`, `DOEXP` and `KILLSHP` say `work.heap` and
`run.Byte(y)`; `KILLSHP`'s two-byte `ADC` for the top of the dead run is `heap.Byte(size)` and its
descent `top.Back(size)`. The sun's heap is lent as a span with no base argument — the window's
address is the constant. The `SeedExplosionCloud` seam keeps its `std::uint16_t` address, which is
`heap.Address()`, for the same reason as M1-e's: the executable does not need the type.

**The tests speak addresses where the oracle does.** Thirty-eight `FromAddress` sites: a fixture's
`HEAP_AT`, the arena sweeps against `LS%`, `SLSP` mirrored into `cpu.memory` and back. Nothing in
the tests moved to offsets, because the oracle's copy is bytes at addresses.

**What the build found.** Nothing: 392 of 392 on the first run, the refusal sweep and §6.112's
station-into-sun-heap case among them — the chain moved as a block and the offset type's default
was chosen before the first compile for the reason above.

#### M2-a slice plan (written with the build, 2026-09-06; §8 records what the census found)

**The census is a tool, and the table is its output.** `tools/channel_census.py` parses every
routine in `GameLogic/*.cpp` by its Allman brace at namespace indentation, finds the workspaces it
takes (`MathWorkspace& _math`, `_work`, `_draw`, `_geometry`, `_clip`, `_centre`, `_axes`) and the
ones it reaches through `FlightScreen` and `FlightLoop` (`screen.math.q`, `_loop.clip.xx13`), and
walks the body top to bottom classifying every access as a write (`=`, `+=`, `++`, ...), a read, or
a pass of the whole workspace to a callee. A field is "from the caller" for a routine when its
first access is a read that no pass precedes, and "after a call" when a pass precedes it. It does
not follow control flow, and says so: a read in one branch after a write in another counts as a
read-first, and the verdicts are where the reading corrects the count.

**Every field has a verdict, in the tool.** `VERDICTS` holds one per field, in four classes —
parameter, result, local, and state-by-design — with the routine or the finding that decides it;
the table in §4.3 is generated from the mechanics and the verdicts together and `--check` fails
when the two drift or a field is missing. That makes the census a thirteenth repository check
rather than a page that was true once.

**What the slice does not do.** It changes no signature. M2-b takes the kernel's verdicts (`P`, `Q`,
`R`, `S`, `K`, the `Wide24` triple) to `Product`, `Quotient` and `SignedSum` results; M2-c takes the
drawing scratch (the line the clipper carries, `LL9`'s frame, the ellipse's `TGT`/`CNT2`/`K2`, the
sun's `XX`/`YY`) to stage results and locals and leaves `Projection` and `SC` as the two
deliberate exceptions; M2-d resolves the boundary carries.

#### M2-b slice plan (written with the build, 2026-09-06; §8 records what the build found)

**Every routine in `Arith.h` takes its operands as values and answers with a struct.** `Product{high,
low, carry}` from the shift-and-add multipliers (`MU11`, `MULTU`, `MLU2`, `MULT1`, `SQUA`, `SQUA2`,
`MULTS`), `Product24` from `MLTU2`, the kept `AddSignedResult` from `ADD` and `MAD` — both of which
take their `(A P)` and `(S R)` as `SignMag16`, moved to `EliteTypes.h` for the purpose — `Quotient`,
`Quotient16` and `WideQuotient` from `LL28`, `LL61` and `DVIDT`, `LogProduct` from `FMLTU` and
`FMLTU2`, `SignedSum{value, sign, carry}` from `LL38` (the sign is the `S` callers used to read
back), `ScaledDivision{whole, fraction, carry}` from `DVID4`, `Root` from `LL5`, and `KBlock` from
`MULT3` and `DVID3B`, whose twenty-four bit operands are `SignMag24`s. The names follow rule 7 where
the old one named a register: `MultiplyUnguarded`, `MultiplyMagnitude`, `DivideSigned`,
`DivideByLog`, `DivideWideByLog`, `MultiplyBySine`, `MultiplySigned24`, `DivideSigned24`. `MULT12`,
`MU6`, `MLS2`, `MUT1`, `MUT2` and `MULTS-2` were nothing but a store in front of a routine the kernel
already had; they dissolve into their call sites and the ledger says where.

**Callers pass what they staged and store what they read.** A scratch audit over the census's
parser listed, for each of the hundred-odd kernel call sites, where each input had been written in
the caller and where each output was read after it; the conversions follow that list line by line,
and where an input was INHERITED — `EndFlightFrame`'s `Q`, `MV40`'s `K2` — the slice stopped and
looked (§8). Routines whose whole job was staging lose their workspace parameter: the stardust's
wrappers, `TAS3`, `TIS3`, `MVS4`, `MVS5`, `TIDY`, `MAS1`, `TAS1`, `SFS2`, `SPS2`, `LL51`, `LL15`,
`LL89`. `MVEIT` and `MV40` keep it for two bytes, both state by design, both documented at the line.

**Tests through the bridge.** The arithmetic sweeps compare returned fields against the oracle's
zero page and lose nothing: the sixteen-bit products, the carries, the quotients and the roots are
all still compared, and `MULT1`'s exit carry and `MULTS`'s are compared for the first time.
Assertions on the kernel's scratch — `T`, `T1`, `U`, `widget`, and `P`, `R`, `S` where only the
kernel wrote them — go, each with a comment saying why; assertions on what the kernel's callers
leave (`DIALS`'s `T1`, `PLANET`'s `K`) go the same way where the byte was the kernel's.

**What the slice does not do.** The drawing scratch — `K`, `K2`, `P+1`/`P+2`, `CNT`, `TGT`, `CNT2`,
`XX`, `YY`, the clipper's `(S R)` and `T` — stays in `MathWorkspace` for M2-c, and so does
`NumberWorkspace`, which the census had pencilled in for M2-b but which is `PrintNumber`'s, not the
kernel's; the boundary carries are M2-d's; `LOIN`'s `Q` (R22) is nobody's until the owner rules.
(It turned out to be nobody's at all: `LOIN` never writes `Q` in this build — R22, closed below.)

#### M2-c slice plan (written before the build, 2026-09-06; three commits; §8 records what each found)

**The drawing scratch becomes the values the routines pass, and the bytes that outlive a call
stay named.** Where M2-b made the kernel take values, M2-c does the same for what the census left:
`DrawWorkspace`'s six bytes of `XX15`, `GeometryWorkspace`, `ClipState`, `K3Block`, `NumberWorkspace`,
and the planet, sun, stardust, dashboard and explosion scratch still in `MathWorkspace`. Three
commits, each green on the suite, the replay and `check_all`, each re-taking the census and the
ratchet.

**M2-c-1 — the line, the pixel and their callers.** `Line{x1, y1, x2, y2}` in `Canvas.h` is what
`LOIN` draws: `DrawLine(Canvas&, Line)` returns `SWAP` as a `bool` (the byte is 0 or 255 and its one
reader is `WPLS2`'s `BNE`), `DrawHorizontalLine` takes its two ends and its row, `PlotPixel` and
`PlotRelativePixel` take the point and the distance `ZZ`, `PlotDash` and `PlotBlock` the point and
the colour `COL`; `T2` and `R2` are locals. The stardust's `XX(1 0)` and `YY(1 0)` are `SignMag16`
locals of the three movers and `MultiplyByHeight` returns what it staged; the scanner's blip and the
compass dot take values; `TAS2`, `SPS1`, `SPS4` and `TA2` return `UnitVector{x, y, z}` — the three
bytes of `XX15` after `NORM` — with the length `NORM` left, `TAS4` and the docking computer's negation
take and return one, and `K3+9` is `TAS2`'s local; the part 9 docking check holds the `K3` block it
normalises twice and `FlightLoop` loses its reference to it; `DIL` takes its threshold and its two
colours (`T1`, `K`, `K+1`) and keeps `SC`, the cursor M4 names; `DIALS`'s four energy bars are its own
array and not `XX12`; the charts, the lasers, the border and the screen rule take a `Line` or a byte;
`BPRNT` takes its value and its digit count and `NumberWorkspace` goes. The clipper's six bytes and
`SWAP` stay on `DrawWorkspace` until the next commit, because `LL145` still reads and writes them.

**M2-c-2 — the clipper.** `Line16` — three sixteen-bit coordinates and `XX12(1 0)` for the fourth
— in, `ClipResult{Line line; bool rejected; std::uint8_t swap; std::uint8_t ends}` out, with `ends`
the `XX13` `BLINE` reads; `MeasureSlope` returns a `Slope{gradient, direction, steep}` that
`PrepareSlope`, `MultiplySlope`, `DivideSlope` and `MovePointOnScreen` take, the working `(S R)` a
local. `LL147`'s `_a` stays, because `LL9` part 10 reaches it with `XX15+5` in the accumulator and
`LL145` reaches it with the caller's; the swap byte it accumulates onto is its second parameter.
`ClipState` keeps `dontclip` alone. `DrawWorkspace` is `SC` alone after this commit.

Two things the plan had wrong and the tree corrected, both journalled below: `swap` is a **byte**,
because `LL147` decrements it and `LL9` part 10 walks it across edges; and `Q` is **not** a local
of the slope helpers. `LL129` writes it and `LL122` shifts it, so the byte a clipped line leaves
behind differs from the byte an unclipped one leaves — and that byte is the frame's `Q`, which the
altitude check reads (§8, R22). `SlopeStep` carries it out and `MovePointOnScreen` publishes it,
which is the second reason the clipper still takes `MathWorkspace&` after `LL115`'s divisor.

**M2-c-3 — `LL9`'s frame, the planet and the sun.** `GeometryWorkspace` is `LL9`'s:
`ScaleOrientation` → `DotProducts` → `FaceVisibility` and the vertex loop read and write it as the
stage results they are (M4 makes them a pipeline), and it stays a `FlightScreen` member for its two
readers outside the routine — `DOEXP`'s copy of `XX3` and `DOCKIT`'s stale `XX2+10` (§6.125), both
reads of memory the frame keeps. The planet and the sun take theirs as values: `DVID3B2`'s `K` is
the `KBlock` `DivideByShipZ` returns and `CHKON`, `CIRCLE2` and `SUN` take the radius; `CHKON`
returns the vertical extent (`P+1`, `P+2`) `SUN` reads; the ellipse's four axes and six signs are one
`EllipseAxes` that `PLS5`, `PL9` and `PL26` fill and `PLS22` takes with its `CNT2` start and `TGT`
end; `BLINE`'s `T` and `CNT` are `CIRCLE2`'s and `PLS22`'s locals handed in and back; `EDGES` takes
the centre as `SignMag16` and the half-width and returns the row's two ends; `SUN`'s `K2` and `CNT`
are locals; `DOEXP`'s `U`, `CNT` and `TGT` are `PTCLS`'s locals. After this commit `MathWorkspace`
holds `q` (the frame's Q, R22) and `k2`'s bottom byte (`MV40`, §8) and nothing else — what M2-a's
verdicts said, with the tree agreeing.

Two corrections the tree made to this paragraph, journalled below. `DOEXP`'s **`Q` is not** a
parameter: it is the frame's `Q` and `DOEXP` runs inside `LL9` part 9, so the same argument that
kept the clipper's applies. And `CNT2` **is not** a parameter of the steering: `TACTICS` and
`DOCKIT` write `RAT`, `RAT2` and `CNT2` in the same three instructions and `TA6` reads all three, so
the third byte joins the first two on `FlightState` rather than being threaded through `TA151`,
`GOPL` and `TA152`. The ellipse walk's `CNT2` is a different meaning in the same byte and is a
parameter, as planned.

**Tests** are rewritten through the bridge as M2-b's were: each sweep stages the same bytes into
the value it now passes and compares the returned struct against the oracle's zero page, and an
assertion on what a routine's own scratch held goes with a comment. **Mutants.** `ta-selftest`
names `NegateVector`'s `_draw.y1`, which becomes the `UnitVector`'s `y`; it is re-anchored in the
same commit and the tactics unit re-run (rule 3). **What the slice does not do.** `Projection` and
`SC` stay as the census says; the frame's `Q` and `MV40`'s byte stay; the boundary carries are
M2-d's; `LL9`'s stages stay one routine until M4.

#### M3-a slice plan (written before the build, 2026-09-06; two commits; §8 records what each found)

**`Elite::Universe` owns every byte of game state, and nothing else.** The seven argument-list
structs — `FlightScreen`, `FlightLoop`, `TradeScreen`, `SaveScreen`, `GameStart`, `MissionScreen`,
`TitleScreen` — hold seventy-eight references between them, and §4.4's `Universe` is what they are
all views of. Two commits, each green on the suite and `check_all`.

**M3-a-1 — the type exists and owns the bytes.** `GameLogic/Universe.h` holds `struct Universe`: a
plain aggregate of the state the flight half names, in the order §4.4 lists it, with no reference
member, no virtual and no printer — so it copies, and `UniverseImage` can hash it without knowing
what else is in the program. `FlightScreen` and `FlightLoop` stay, and are BUILT from it: the app's
`FlightSession` and the fixture's `FlightUniverse.h` replace their twenty-odd members with one
`Universe` and hand out the two aggregates as before. No routine signature changes, so the whole
suite is the check, and `check_outpost.py`'s member half (M3-0) is what watches the app.

**M3-a-2 — the routines take it.** Every routine that took `FlightScreen&` or `FlightLoop&` takes
`(Universe&, Ports&)` and the two structs go. `Ports` is what is left when the state comes out: the
text machinery bound to the universe's own bytes (`TokenPrinter`, `CharacterPrinter`, the `TextSink`
they print through) and the five flight seams (`SightEffects`, `ViewEffects`, `ShipEffects`,
`ShipDrawEffects`, `FlightLoopEffects`). It is a struct of references for one slice: M3-b is what
collapses the seam half to §4.5's four ports, and doing it here would be two patterns in one slice
(rule 8).

**The acceptance the row promised, corrected before the build.** "`aggregate-refs` at zero" is not
what M3-a can reach: thirty-nine of the seventy-eight are the docked half's four structs, which the
row does not name and which M3-c's `Game` is the natural place for, and eight are `Ports`. The
number M3-a can honestly deliver is **78 → 47** — the flight half's thirty-nine replaced by eight —
and the rest is M3-b's and M3-c's. Recording it here rather than discovering it at the ratchet.

**What the slice does not do.** The seams stay twenty-two (M3-b). `Main.cpp` keeps its own state
for the docked screens (M3-c). The printers stay where the app builds them, because two of them
need a seam — `TextPrinter` takes the bell and `ExtendedTokenPrinter` the control codes — and a
`Universe` that held them would not be a plain aggregate, which is the one property M3-c's
`StateHash` needs.

#### M3-b slice plan (written before the build, 2026-09-06; four commits; §8 records what each found)

**Twenty-two seams become four, and most of them are not ports.** §4.5's table is the destination:
`Presenter`, `Keyboard`, `SoundSink`, `SaveStore`. Everything else in the twenty-two is either a
call into a routine that now exists — the phase order that scoped it has since been built — or a
`VideoState` write, or a piece of the text system's own internal polymorphism.

**M3-b-1 — the phase-order seams go, ONE COMMIT EACH.** `SpawnEffects`, `SpawnChildEffects`,
`ShipEffects`, `ShipDrawEffects`, `ChartShapes` and the spawn and reset halves of
`FlightLoopEffects` and `StartUpEffects`: every method of them is a routine `GameLogic` now
contains, reached through an interface because it did not when the seam was written (§6.73's rule,
which this slice is the last application of). The routines that reached through them take
`(Universe&, Ports&)` and call. No new abstraction.

**One commit each, and the reason is the tests.** A seam is what a suite COUNTS: the oracle traps
the routine, the port records the call, and the two tallies are compared. Take the seam away and
both sides run the routine, so the trap has to come off and the comparison becomes the pixels or
the state the routine produced — which is a stronger statement and a per-suite piece of work. §8
records what each one found.

**`SpawnChildEffects` CANNOT GO IN THIS SLICE, and the reason is worth writing down before the
build rather than after.** §4.5 lists `SpawnChild` among the calls into routines that now exist, and
it is one — but its callers are `SpawnItems` and `SpawnDebris`, whose entire contract IS the call
sequence: `SPIN2` is a loop that calls `SFS1` `CNT` times and hands back the LAST one's carry, which
is `NWSHP`'s "was there room". `TheWreckageMatchesSPIN` compares that sequence argument by argument
with `SFS1` trapped to `SEC` on the oracle and the seam answering `true` on the port — both sides
told "always room". Let the routine spawn for real and the bubble fills at ten slots, the carry
flips, and the comparison is no longer of the same thing.

The honest fix is M4-a's pattern, not M3-b's: `SpawnItems` ANSWERS which ships to spawn and the
caller spawns them, so the sequence is a typed stage result and the test compares it without a seam
at all. Doing that here would be two patterns in one slice (rule 8). So the seam stays until M4-a,
and **`effects-seams` reaches five in M3-b rather than four** — the M3-b row's acceptance is wrong by
one, and this is where that is recorded rather than discovered at the ratchet.

**`ShipDrawEffects` CANNOT GO IN THIS SLICE EITHER, and the reason is the oracle rather than the
port.** It was built and reverted, and it takes no slice letter because a reverted slice should not
own one — `M3-b-1d` is the spawn half of `FlightLoopEffects`, which went in its place. This is what
the attempt found, recorded so the next one starts from evidence rather than from the plan's
optimism.

`DrawPlanetOrSun` is `LL25`'s `JMP PLANET` and `DrawExplosion` is `LL14`'s `JMP DOEXP`, and both
are routines this library has had since slices 3c and 4b-b. The removal itself is small: `DrawShip`
takes `(Universe&, Ports&, Ship& _slot, bool _carryIn)` — thirteen arguments become four, because
every caller passed the same nine members of the universe — and `Ports` trades `ShipDrawEffects&
drawing` for `ExplosionEffects& explosion`, which is a port in §4.5's sense and stays. That part
worked; `ShipDrawTests`, `TacticsTests` and `LaunchTests` went green.

**THE FLIGHT LOOP DID NOT, AND THE OBSTACLE IS THAT `XX21` IS THE VIC-II.** §6.108: the oracle's
memory is FLAT, so the sprite registers at `&D000` and the blueprint pointer table at `XX21` are the
same bytes — the harness even names the field `Where::vic = Label("XX21")`. `SETL1` maps the I/O
page in on real hardware and is trapped here, so `PTCLS2`'s `STA VIC+&17`, `+&1D`, `+&2`, `+&3`,
`+&10` and `+&15` land on the pointers for ship types 2, 3, 9, 11, 12 and 15. While `DOEXP` was a
seam the oracle never ran it and nothing was hurt. Run it on both sides and the first exploding ship
in a frame corrupts the blueprints of the ships drawn after it: `M% whole (MCNT 2, distance 0,
shape 2): XX0+1 -- game has 218, port has 216`. That is the ORACLE reading its own damaged table,
which no change to `GameLogic/` can fix.

**Two fixture faults came out with it and are worth keeping.** The flight-loop fixtures give every
ship a line heap at `&0C00`, outside `LineHeap`'s `K%`-to-`LS%` window — §8 already noted in passing
that "with the fixture's heap pointers outside the arena the seeds it writes are compared nowhere",
and with `DOEXP` running the cloud is DRAWN from those bytes, so the port put every particle in one
place and the game read whatever `&0C00` held (28 screen bytes apart on `MAL1 (in the sights,
already dead)`). And `Where` has no `SUNX` and no `LSY2`, so a fixture cannot yet put a DRAWN body
into both machines: `MA23 whole frame (a sun close enough to draw, planet at 97)` differs at screen
offset 8033. Both are one-line fixes; neither helps while the VIC aliasing stands.

**What unblocks it is a harness slice, not a library one**: `Cpu6502` has to model the 6510 port
register that `SETL1` writes, so that a store to `&D000`-`&DFFF` with the I/O page mapped in goes to
a VIC register file instead of to RAM. That is the same change that would let `MVTRIBS` and a drawn
ship coexist in one frame, which `Universe::spriteRegistersAreOurs` exists to work around today. So
`ShipDrawEffects` stays until then, and **`effects-seams` reaches six in M3-b rather than five** —
`SpawnChildEffects` for rule 8's reason and this one for the oracle's.

**M3-b-2 — `SoundSink`.** `DashboardEffects`, `ViewEffects::PlaySound`, `TextEffects::Beep` and
`FlightLoopEffects`'s music pair collapse into one port that takes a SID REGISTER WRITE. The library
already owns `SoundBuffer`, the music player and the tables; what it lacks is somewhere to put them,
so `Universe` gains the two objects and the port is what `SidWriteLog` already is.

**AND THE SLICE HAS TO PAY FOR THE PORT IT LANDS, which this plan did not say and M3-b-2b found.**
Every M3-b slice before it only REMOVED from `Ports`, so the struct sits on `aggregate-refs`'s
ceiling of thirteen with no slack; a slice that adds `SoundSink` and removes nothing puts the count
at fourteen, and rule 5 fails a count above its ceiling with no exception — rightly, since a ratchet
that can be argued past is not one. The four ports of §4.5 all have to arrive before the eleven
seams they replace can go, so **each of M3-b-2b, M3-b-3 and M3-b-4 lands its port in the same commit
as at least one removal**, and the count leaves the phase at four without ever going up. 2b pays with
`ViewEffects`, which the §4.5 table already names in the `SoundSink` row (§8, 2026-09-06).

**M3-b-3 — `Presenter` and `Keyboard`.** `TunnelEffects::ShowFrame`, `LineEntryEffects::WaitFrames`,
`StartUpEffects::WaitFrames` and `TradeScreenEffects::ClearToView`'s pixels become `Presenter`;
`KeySource`, `ControlEffects::ScanKeyboard`, `StartUpEffects::ScanTitleKeys` and `FlushKeyboard`
become `Keyboard`. `SightEffects` and `ExplosionEffects` become `VideoState` writes the library
makes itself, which ADR-005 §1 already decided.

**M3-b-3a did that last part first, and it is what pays for the two ports.** Both seams go and
nothing replaces them, so `aggregate-refs` reaches twelve with the credit standing. What held them
back was `SetRasterMode` — `SETL1`, which this plan's own row called "self-modifying code inside a
raster interrupt handler" and which is nothing of the kind (§8, `MemoryMap.h`).

**M3-b-3b spent it on `Presenter`, and the row above is wrong about what that port carries.**
`TradeScreenEffects::ClearToView` is not the presenter's: it is `TT66`, which the library has. Nor
are `SetUpTradeScreen`, `ClearBottomRows` or `BeepAndPause`'s beep. What `Presenter` carries is
`DELAY`, because `WSCAN` waits for a raster line and nothing in `GameLogic` knows what one is —
which is also why the oracle traps it and a suite that untrapped it would hang (§8).
`TunnelEffects::ShowFrame` became `Present()` in M3-b-3c — and it was one method carrying three
answers, one of which the executable was getting wrong (§8).

**M3-b-3d landed `Keyboard`, and the row above is wrong about that one too.** `ControlEffects::
ScanKeyboard` does not become a method of it: `RDKEY` is `Elite::ScanKeyboard` in the library and
the port answers `Held(key)`, because the walk is the only part of the routine that reads hardware
and everything around it was already the library's (§8). `StartUpEffects::ScanTitleKeys` goes with
it, its reason answered by `Presenter::HoldTitleFrame`. **M3-b-3 is complete**; three of §4.5's four
ports have landed and `SaveStore` is M3-b-4's.

**M3-b-4 — the text system's seams, and the null port.** The row said `CommanderStore` would be
renamed to §4.5's name and that `TextSink`, `ValueTokens` and `ControlCodes` would all go; two of
those three claims are wrong and §4.5 above now says so. What the slice actually is:

  - **4a ✅ `TextEffects`**, which the row did not mention at all. `clss` is `TT66simp`, the library
    has had it since slice 2a, and the executable was answering the seam with the whole of `TT66`
    (§8).
  - **4b ✅ `ControlCodes`**. `DT3`'s dispatch is `Elite::RunControlCode` over `(Universe&, Ports&)`;
    the last thing the shell still owned was `CLYNS`, which is `Elite::ClearMessageRows`.
  - **4c ✅ the null port**, and three of the five classes the row named had already gone with their
    seams. `LoopRecording` was a second null port for one boolean and is folded in; `NullShell` is
    a RECORDER and stays one, which is the distinction `NullSeams.h` has always insisted on.
  - **`CommanderStore` keeps its name** and `TextSink` and `ValueTokens` stay, for the reasons §4.5
    now records. `effects-seams` stops at the number that leaves them standing rather than the
    ratchet quietly failing to reach zero.

**What this slice does not do.** `Elite::Game` is M3-c's. The seams that survive are four, and the
count is what says so.

### Phase M2 — Explicit calling conventions

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M2-a The channel census** ✅ | For every workspace field: who writes it, who reads it, and whether any reader reads without writing first — from the tests' `Mirror` lists and a read of each routine. Written into this document as §4.3's table, completed. | The table names every field; no field is "unknown". **Built 2026-09-06** (slice plan and §8 below; `channel_census.py --check` holds it). | 2 |
| **M2-b Kernel** ✅ | `Arith` routines take values and return `Product`/`Quotient`/`SignedSum` structs; `MathWorkspace` parameters removed one routine family at a time (multipliers, dividers, `LL28`, `NORM`, `TIDY`). | Exhaustive oracle sweeps unchanged; `register-params` and the `MathWorkspace` parameter count in the ratchet fall to their floors. **Built 2026-09-06** (slice plan and §8 below; register-params 64 → 22, workspace-params 207 → 151). | 4–5 |
| **M2-c Geometry and drawing scratch** ✅ **built 2026-09-06 in three commits (§8)** | `GeometryWorkspace`, `DrawWorkspace`, `ClipState`, `K3Block`, `Projection` and `NumberWorkspace` become stage results or locals, with what M2-b left in `MathWorkspace`; `Projection` outliving `Project` stays and is documented at the one place it matters. The line and its callers, then the clipper, then `LL9`'s frame with the planet and the sun. | `LL9`, planet, sun, stardust and clipper suites green; `workspace-params` 151 → 46 and `MathWorkspace` down to `Q` and `K2`'s bottom byte, which is the floor the census names. | 4 |
| **M2-d Boundary carries** ✅ **built 2026-09-06 (§4.3.1, §8)** | Every non-kernel `bool _carryIn` walked back to the instruction that decides it, at every call site in the disassembly. Three were passed the wrong value and are fixed; the rest are the routine's operand or an inherited flag the port cannot see, and the parameter is what keeps that assumption at the call site. | Green, with a fixture that reaches `MA47`'s kill and compares `RAND` across it; `carry-params` 31 → 32 with the audit as the reason. | 1 |

**Phase M2 is complete, 2026-09-06.** Five commits over four slices, each green on the suite, the
M0-c replay and `check_all`. What a routine consumes and produces is now in its signature: the
arithmetic kernel takes values and answers with structs (M2-b), the drawing takes and returns the
line, the slope, the circle's radius and the ellipse's axes (M2-c), and every carry that crosses a
boundary has been walked back to the instruction that sets it (M2-d). Six workspace structs held
forty-two fields when M2-a counted them and hold thirteen now, each with a verdict the thirteenth
repository check holds the tree to: two bytes that outlive their writer on purpose (`Q` and `K2`'s
bottom byte, §8), the dashboard's screen cursor, the short-range chart's `dontclip`, `LL9`'s four
stage results and `Projection`'s four. The ratchet moved `register-params` 64 → 16 and
`workspace-params` 207 → 46 across the phase. Three port defects were found on the way and fixed:
`HLOIN` and `BOX2` writing `T2` where the game writes `T` (M2-c-1), and `MA47`'s three carries into
`SPIN` and the beep (M2-d).

### Phase M3 — Ownership

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M3-0 The app's member check** ✅ **built 2026-09-06 (§8)** | `check_outpost.py` gains a third half: every member the app names on an `Elite::`-typed variable, against that type's members as `GameLogic/*.h` declares them, bases closed over. A `--self-test` plants one that cannot resolve. | In CI as the fourteenth check; 111 accesses resolved on the tree as it stands. | 1 |
| **M3-a Universe** ✅ **built 2026-09-06 (§8)** | `Elite::Universe` as a plain aggregate; `FlightScreen`/`FlightLoop`/`TradeScreen`/`SaveScreen`/`GameStart`/`MissionScreen`/`TitleScreen`/`MissionBay` replaced by `(Universe&, Ports&)` on every routine; `FlightSession` and `Outpost::Game` own the universe and the ports between them. | Green on both legs; `aggregate-refs` 78 → 14, and the fourteen ARE `Ports` — "at zero" is M3-b's, which collapses that one struct. `JumpState` is values rather than references and goes with M3-c's `Game`. **The Windows job was the gate and caught two defects** (§8). | 4 |
| **M3-b Ports** ✅ **built 2026-09-06 (§8)** | Three of the four port interfaces (`CommanderStore` keeps its name — §4.5); **five seams that turned out to be `GameLogic` reached through the executable**: `TradeScreenEffects`, `TunnelEffects`, `ControlEffects::ScanKeyboard`, `TextEffects` and `ControlCodes`; the null port folded to one class. | Green; `effects-seams` at **nine**, not six. Two more than the row expected and both are named rather than counted away: `TextSink` and `ValueTokens` are the text system's own polymorphism and stay (§4.5). `SpawnChildEffects` needs M4-a's typed stage result and `ShipDrawEffects` needs the emulator to model the 6510 port register, as the row said. `aggregate-refs` 14 → 11, `outpost-elite-names` 205 → 157. | 4 |
| **M3-c Game** ✅ **built 2026-09-06 (§8)** | `Elite::Game` with `Reset` and three `Step`s; `Perform`, `Leave`, `MissionOf`, the chart draw, the docked pass and the pause pass moved from `Main.cpp`, with the eight bytes of game state that were in no struct and the five text objects. `Advance` SPLIT rather than moved — the count of passes is a `double` and the determinism guard forbids one here. | `main-lines` 1,219 → 256, under the 300 the row asked for, and `outpost-elite-names` 205 → 71. (Both plain rather than marked: an acceptance records what the slice ACHIEVED and a `count:` marker asserts what the tree holds TODAY, so a marker here is dragged wrong by the next slice that moves the number — M4-a-1 is the one that did. The live counts are §3's.) `GameTests` drives the object as `Run` drives it. `Frame`, `Sounds` and `StateHash` are not built and `DockedSessionTests` still owns its own composition (§8). | 4–5 |
| **M3-d ADR-007** ✅ **built 2026-09-07 (§8)** | State ownership and the replay hash, written from M3-a..c as built — with the three places ADR-006 §4 was wrong, the eight bytes that should be in `Universe` and are not, and what the digest does not cover. ADR-006 §4 amended in place. | Accepted; `Design/ADR/ADR-007-state-ownership.md`. | 1 |

### Phase M4 — Control flow

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M4-a Flight frame stages** ✅ **built 2026-09-07 (§8)** | `MoveEveryShip`'s parts 7–12 as typed stages (`Contact`, `ScoopResult`, `DockingTest`, `LaserHit`, `KillOutcome`); `BeginFlightFrame` and `EndFlightFrame` split at their annotated parts with the housekeeping cycle as a table. **M4-a-1 built 2026-09-07 (§8):** `SPIN` and `SPIN2` answer an `Elite::Drop` and `PerformDrop` spawns, which is what `SpawnChildEffects` was waiting on — the seam goes, and the frame fixtures untrap `SFS1` on both machines. **M4-a-2 built 2026-09-07 (§8):** parts 7 to 12 as `Contact`, `ScoopResult`, `DockingTest`, `Impact`, `Aim` and `KillOutcome` beside the existing `LaserHit`; `MoveEveryShip` 426 → 143 lines, and three dead stores at the end of part 9 that only a type could show were dead. **M4-a-3 built 2026-09-07 (§8):** the head and the tail split at their annotated parts — 324 → 14 and 254 → 43 — with the cycle as a table, the thirty-two-step count corrected in three places, and part 14's fall-through into part 15 restored. **M4-a is complete.** | `FlightLoopTests` green frame for frame; replay hashes unchanged. M4-a-1: both, plus `effects-seams` 9 → 8 and `aggregate-refs` 11 → 10. | 4 |
| **M4-b LL9 stages** ✅ **built 2026-09-07 (§8)** | `DrawShip` as the SEVEN stages of §4.6 over a `ShipRender` frame — `TestPresence`, `MeasureRange`, `ScaleShip`, `SelectFaces`, `ProjectVertices`, `OpenHeapRun`, `PushEdges`; 551 → 38 lines of pipeline. | `ShipDrawTests` green; the whole-bitmap comparisons unchanged; the replay digest unchanged. The channel census names a PART rather than `DrawShip` for `xx2`, `xx3`, `xx12` and `q`, and raises three inherited inputs one function had hidden. | 4 |
| **M4-c Decisions** ✅ **built 2026-09-07 (§8)** | `TACTICS` answers a `Tactic` over five parts (M4-c-1); `DOCKIT`'s `bool` was a PHANTOM — the original reaches no `OOPS` and no `DEATH`, so it is `void` (M4-c-2); `MLOOP` parts 1–4 answer a `SpawnPass` over a `SpawnFrame`, because §6.125's carry is live across all four (M4-c-3). | `TacticsTests` green at 7,326 cases; SEVEN `tactics` mutants re-anchored, none dropped, `mutate.py --check` at 72 of 72. The row said sixteen mutants and the unit has seven that name a line in `RunTactics`. | 5 |
| **M4-d Mode machine polish** ✅ **built 2026-09-07 (§8)** | `Game::Mode` built — `Flight`, `Docked`, `Paused` — which takes the pause-before-`QQ12` ordering out of `Main.cpp` (`main-lines` 256 → 253). The mission sub-machine was already `MissionOf` (M3-c). **`LoopOutcome` is NOT retired and the row was wrong to ask**: `Continued` is not a mode and the other three are transitions, so a mode cannot carry what four routines have to return. **The death sequence stays synchronous**, because making it a state would change the pacing this row's own acceptance forbids from moving. | Replay hashes unchanged. | 2 |

### Phase M5 — Polish and the ledger

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M5-a Strong types** | `View`, `SoundEffect`, `Message`, `Colour`, the option toggles as an `Options` struct (the thirteen become fields; `DKS3` walks a `constexpr` array of member pointers so the order stays the only definition). **The `out-params` half is built 2026-09-07 (§8)** in three slices: four routines were handed a field of the `Universe` they already took, six more took it, and the two that were not state returned instead. `SoundEffect` is built; two defects came out of the state moves (a second `DNOIZ`, and the digest gap ADR-007 §5 named) and both are closed. | Green; `out-params` at <!--count:out-params-->0. `Colour` is built and found a defect (the background register was never latched); `Options`, `View` and `Message` were examined and refused, with the evidence in §8 and ADR-006 §2. The original's two colour-constant families are both built: `PixelPattern` (M5-a-9) and `CellPalette` (M5-a-8), 2026-09-07 — and the second found two constants defined twice. | 3 |
| **M5-b constexpr data** ✅ | All <!--count:generated-tables-->55 generated tables as `constexpr std::array`, emitted that way by `tools/extract_tables.py`; `GameLogic/LookupTables.cpp` asserts their SHAPES against the constants that index them. **Built 2026-09-07** (§8). **The row's second clause is answered rather than built, and the acceptance is rewritten because it named a suite that no longer exists** — `TableTests` was deleted on `main` when the oracle comparison of the generated tables was retired, and the codecs already `static_assert` their round trip (ADR-006 §2, M1). | Green; the shape assertions fail the build when a table's length stops matching what indexes it, shown by planting one. | 2 |
| **M5-c The ledger** ✅ | The twenty file names in `Source-Inventory.md`'s HOME cells that named no file on disk corrected; `inventory.py` gains `--check-homes` so it cannot happen again. **Built 2026-09-07** (§8), and the count of ten that were left over is the finding: they are in the NOTES, which are history, and two of them name a missing file deliberately. | In CI, with a self-test that plants both traps; <!--count:inventory-stale-files-->0 stale homes. | 1 |
| **M5-d ADR-006 and the tidy checks** ✅ | ADR-006 amended from what was built — §2 (the strong types that were refused), §5 (M4's stages, four of which the plan predicted wrongly), §8 (the `constexpr` tables) and the status table. `.clang-tidy` **rewritten for this repository**: every word of its status block and three of its four exclusions were about the sibling tree it was adopted from, and **nothing here had ever run it** (§8). `modernize-` goes from two checks to all but three, and two inherited exclusions are removed rather than widened around. **Built 2026-09-07**; `-modernize-avoid-c-arrays` came off the same day (M5-d-2), so all but two. | `tools/check_tidy.py` sweeps `GameLogic/` on the Linux leg of every push and comes back clean; `WarningsAsErrors` still `'*'`, and now with a gate behind it. | 2 |

### Phase M6 — Detach (owner ruling, §1 R-a to R-d)

**M6-0 IS A GATE, AND IT EXISTS BECAUSE OF WHAT M6-b MAKES PERMANENT.** A recorded fixture pins
exactly the calls the tests made while the original was here (R19); a behaviour no test reached
before M6-b can never again be asked of the original. So the question to settle before M6-a records
anything is not "is every row built" — every M0–M5 row is — but "is there anything the oracle can
pin today that nothing will pin afterwards." The assessment of 2026-09-07 (§8) found eight such
things, and they are the rows below, **ordered by irreversibility**: the ones at the top are the
ones that can only be done while the interpreter is in the tree. Three were known and parked as
tasks since M3-b; two are preconditions §4.10 already states and nothing enforces; three are
instruments M6-a's own acceptance needs and does not have. None of them is a refactor: `GameLogic/`
changes only where a defect is found, and the replay record moves only under rule 1.

What is NOT in the gate, deliberately: `Frame()`, `Sounds()`, `StateHash()` and `Game` owning its
`Universe` (task #13, structure, no oracle involved); M1's deferred `LightYearsTenths`, `Laser` and
`Equipment` types, which ADR-006 §2 parked "for M5, where a type earns its operators" and M5 did not
build — that is a decision to take, not a gap to close, and it needs no original; M5-a-8 and
M5-a-9 (both built the same day); `modernize-avoid-c-arrays`' sites (M5-d-2, built). All of them
were safe after M6-f, and none of them waited.

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M6-0-a The 6510 port register** | `Cpu6502` models the port register `SETL1` writes, so that with the I/O page mapped in a store to `&D000`–`&DFFF` reaches a VIC register file rather than RAM. Known since M3-b (§8, `ShipDrawEffects`): the oracle's memory is flat, the VIC registers alias `XX21`, and an explosion drawn on the oracle side corrupts the blueprints of the ships drawn after it. Then `ShipDrawEffects` goes the way of every other seam — `DrawPlanetOrSun` and `DrawExplosion` become library calls — and `MVTRIBS` and a drawn ship can share one oracle frame. | **The first whole-frame comparison with an explosion in it**, green; `effects-seams` 8 → 7; the trap on `DOEXP` gone from every composition test; `RDKEY` compared with the banked writes rather than around them. This is the one row that cannot be done after M6-b at any price. | 3 |
| **M6-0-b The replay reaches death and the escape pod** | §4.10 says the replay "must cover launch, flight, combat, docking, death and the escape pod" and it covers four of the six: the script has a launch, a coast, a Viper and the docking computer. Two more scripted phases — a flight that ends in `DEATH`'s wreckage, and one that ends in the escape pod — each digested at its turns; both are rule 1's first case and the journal names them. Death and the pod are compared per routine today and nowhere in composition, and composition is what the replay is for (R14). | Sixteen-plus checkpoints re-recorded with the two endings named; every other test unmoved; the digests taken while the original can still say whether a moved one is a defect. | 2 |
| **M6-0-c The eleven control codes** | Task #12. Five (9, 21, 25, 27, 28) are comparable now given a `Ports` and a canvas comparison; three (22, 24, 26) need a scripted keyboard on both sides first; three (11, 30, 31) have nothing behind them on either side and fall to `default`. The eight get compared; the three get the sentence that says why not, next to the `DEFERRED` array. | `ExtendedTokenTests` defers three and says which; the eight compared against the original. | 2 |
| **M6-0-d Two fixture faults from M3-b** | `Where` has no `SUNX` and no `LSY2`, so a fixture cannot put a DRAWN sun into both machines and `MA23 whole frame (a sun close enough to draw)` has differed at screen offset 8033 since it was first run; the flight-loop fixtures give every ship a line heap at `&0C00`, outside `LineHeap`'s window, so "the seeds it writes are compared nowhere." Both were called one-line fixes at the time and neither was made. | The sun frame compared on the whole bitmap; the heaps inside the arena and compared; no case excluded by name that this row could include. | 1 |
| **M6-0-e Seven routines only ever trapped** | `TRADEMODE`, `NLIN`, `TT67` and `DK4` are ported and have no direct comparison against the original anywhere — every test that reaches them traps them; `WSCAN` is the platform's (ADR-005 §3), `REDU` is proven unreachable, `GTNMEW` is the load path's name entry. A trapped routine's fixture records the TRAP's answer. Each of the seven gets a ruling: compared before M6-a, or a sentence beside its trap saying it never will be and why. | No `AddTrap` on a label that has neither a direct comparison nor a recorded reason. | 1 |
| **M6-0-f A coverage instrument** | M6-a's acceptance is "every *Port* row has a test that calls it" and nothing can answer that: the ledger's ✅ is per label and inconsistent (twelve of thirty-three Port rows carry none, the flight loop's sixteen parts among them), and a marker-to-test name match is noise. `OracleImage` gains a `--coverage` mode that records which labels each test calls, and `inventory.py` reads it against the ledger's Port rows. R19 says the review is a gate, and a gate needs a reading. | The review is a tool's output, not a person's; every Port row's labels appear in some test's call list or the row says which do not and why. | 2 |
| **M6-0-g Mutants to a stated floor** | Eight of fifty-two hand-written `.cpp` files carry a mutant. After M6-b a fixture says what the tests ASKED and a mutant is the only instrument that says whether a test would NOTICE — and `Rng.cpp`, `Arith.cpp`, `ShipMove.cpp`, `PlanetDraw.cpp`, `Spawn.cpp` and `Flight.cpp` have none. A floor is chosen and written here; M6-b's "five mutation units" is a count from before the corpus reached nine files and is replaced by it. | Every file the floor names has a caught mutant; `mutants.json`'s note per unit says what the mutant would have hidden. | 3 |
| **M6-0-h The empty seams** | `StartUpEffects` is an abstract class with nothing but a virtual destructor, and `ControlEffects` holds only `RunDockingComputer`, which M4-c-2 made a library routine. Both are still counted. Cheap, and worth doing before M6-a so the seam count M6 inherits is the real one. | `effects-seams` at the number §4.5 can explain: the four ports, the text system's two, and whatever M6-0-a leaves. | 1 |
| **M6-a Coverage review and the recorder** | **Blocked on M6-0.** Every *Port* row of the ledger has a test that calls it, read off M6-0-f's instrument rather than reviewed by eye; the `Oracle` seam of §4.10; `RecordingOracle` writes `Tests/Fixtures/*.oracle`; the record-size threshold measured and written here. | M6-0's eight rows green first. Then the suite runs green through the recorder on both legs and the fixtures are committed; a second recording run produces identical files. | 3 |
| **M6-b Fixtures answer** | `RecordedOracle` serves the suite; `LiveOracle` and the BeebAsm steps leave CI; `OracleIsPresent` retired; `mutate.py`'s oracle check removed (the tables' own oracle comparison went on 2026-09-07). | Green on both legs with no assembler installed and the submodule uninitialised; the mutant corpus at M6-0-g's floor with every tally unchanged. | 2 |
| **M6-c Identifiers** | Every identifier that is a 6502 label — the workspace fields, `xx*`/`k*`/`qq*` names, `INWK`-style parameters — renamed for what it holds, in the code and the tests; a ratchet counter (`origin-identifiers`) at zero. | Green; replay hashes unchanged; ratchet at zero. | 4 |
| **M6-d Comments** | The assembly transcribed in comments rewritten as prose about the behaviour, keeping the REASON every time (Risk R20); the plan's own journal is history and is left alone. | A ratchet counter over opcode-shaped comment lines at zero; per-file review that no "why" was lost. | 8–10 |
| **M6-e Markers and the ledger** | `// 6502:` markers removed; `Source-Inventory.md` and `inventory.py` deleted; AGENTS.md R7 and §7 amended; ADR-004 §4 amended. | `check_all.py` green with `inventory.py` gone; `origin-markers` at zero. | 1 |
| **M6-f The tree** | `Upstream/` (the submodule entry and `.gitmodules`), `MasterFile/`, `Cpu6502`, `OracleImage`, `labels.py`, `c64_source.py` and the master-count markers removed; ADR-001 §5 and Risk R1 restated; `.gitignore`'s upstream rules dropped. | A fresh clone builds and runs the whole suite with nothing but the repository; `origin-tools` at zero. | 1 |
| **M6-g ADR-008** | The detachment as built: what pins behaviour now, what a fixture is, what changing one means. | Accepted. | 1 |

**Total: roughly 90 sittings**, which is the same order as the port itself took (plan §7), and the
plan expects the estimate to be wrong in the same direction the port's was: the dense units
(`LL9`, `MVEIT`, `TACTICS`) cost more and the rest cost less — and M6-d, which is writing rather
than code, is the least certain number in the table.

### Appendix — the four-step method on one module, as the template

The brief's process (analyse → decode → strategise → implement) applied to `ShipSlot.h`, which is
M1-a's first file and the worked example every later slice copies.

1. **Clarification.** `ShipBlock` is addressed three ways (§2.2): by literal offset, by named
   constant, and by dynamic offset from an axis register. The ambiguity is whether the dynamic
   form (`_work[_x + 2u]` with `_x` ∈ {0, 3, 6}) is an axis or a byte index; reading `MVT1`, `MVT3`
   and `MVS5` settles it as an axis in every case, and `MVS4`'s Y-in-sixes as an orientation vector.
   No global-state dependency: the block is copied in and out by its callers.
2. **Flow.** Slot block → `INWK` copy → motion → drawing → two-byte write-back or full copy (§2.2).
   The heap address in bytes 33–34 is read by the drawing and written by `NWSHP`; the state byte is
   written by `LL9`, `DOEXP`, the bomb and the kill path; byte 36 by `NWSHP`, `ANGRY` and the scoop.
3. **Strategy.** A view first (`ShipView`), a struct with a codec second (`Ship`), never a struct
   without one — the bytes are what the oracle and `NWSHP` see. The two constant families collapse
   to the accessor names. The dynamic-offset routines get `Axis` and `Orientation::Component`.
4. **Implementation.** M1-a lands the view and the 436 migrated sites with the oracle suite green
   and the `Tactics.cpp`, `Missions.cpp` and `Spawn.cpp` mutants re-anchored; M1-c lands the codec.
   The summary that goes in the journal names what became safer (no numeric offsets, no duplicate
   names), what became clearer (a reader sees `ship.State().IsExploding()` where there was
   `(work[31] & 0xA0u)`), and what did not change (every byte).

---

## 7. Risks this plan adds to the register

| # | Risk | Where it is validated | Mitigation |
|---|---|---|---|
| **R14** | A refactor that keeps the oracle green changes composition the per-routine tests cannot see (an ordering of side effects between two routines). | M0-c's replay hashes, from the first slice of M1 on. | The replay is layout-independent and covers a whole flight; a changed hash is a stop. |
| **R15** | `Outpost/` breaks on a type change that `check_outpost.py` cannot see, on the Windows job only, after the Linux leg is green (§6.116, §6.123 again). | Every M1–M3 slice. | Small slices; the app edited in the same commit; M3 removes most of the surface. |
| **R16** | A typed field silently widens a byte (an `int` promotion in a codec, a `bool` that was `0xFF`). | The codec `static_assert`s and the byte compare. | Rule 2; the ratchet counts `int` arithmetic on model fields as a pattern from M1-c. |
| **R17** | The mutant corpus degrades under renaming — a `find` that matches once by accident on a different line. | `mutate.py --check` per commit, `--unit` per slice. | Rule 3; the selftest mutant per unit is the harness's `OracleIsPresent`. |
| **R18** | The ratchet's ceilings are lowered to match the tree rather than the tree lowered to match the plan (a number with no decision behind it). | `check_modernize.py`'s slack check. | Rule 5; a ceiling change needs a journal entry naming the slice. |
| **R19** | A recorded fixture pins only what the tests asked while the original was here; a behaviour no test reached before M6-b is unpinned for ever. | M6-a's coverage review; the M0-c replay's breadth. | M6 is last; the review is a gate, not a report; a fixture is never re-recorded (rule 1). |
| **R20** | Rewriting the comments loses the reasons — the commentary records WHY a carry matters, and prose that says only WHAT is worth less than the assembly it replaced. | M6-d, per file. | The rule for M6-d is "keep the reason, drop the transcription"; a comment that cannot be rewritten without losing its reason keeps the instruction sequence as a quotation. |
| **R21** | Deleting `MasterFile/` and `Upstream/` at the tip leaves them in every commit before M6-f; a reader of the history still finds them. | Not validated by this plan. | Owner decision, out of this plan's scope (§1 R-d); recorded so that M6-f is not mistaken for having done it. |
| **R22** ✅ **closed 2026-09-06** | The altitude's radicand low byte is a stale scratch byte: `MA23`'s `LL5` takes `(R Q)` with `Q` whatever the frame last left. The risk as written also said `LOIN` writes `Q` on every line and the port keeps it local — **and that half was false**: this build's `LOIN` works in `P2`, `Q2`, `R2`, `S2` at 188–191 and never touches `Q` at 154. The claim came from the BBC commentary, which is where M2-c-1's `T`/`T2` defect came from too. | `TheAltitudeMatchesMA23` seeds `Q` on both sides over eight values and eight distances; `TheFramesOwnQReachesTheAltitude` runs the whole of `M%` with the planet in range over six bubble shapes and lets each side decide `Q` for itself. `ALTIT` is in the compared image. | **Closed by measurement, not by ruling.** Neither fix was needed: `LOIN` had nothing to publish, and the frame's `Q` agrees with the game's on every shape the sweep covers. The fixture found a different defect on the way — `MA23` reaches `SBC #36` with the carry CLEAR, so the planet's radius costs 37 — which is fixed and the replay re-taken (§8). |

---

## 8. Journal

**2026-09-06 — Opened, with the baseline measured rather than asserted.** Suite green on the
portable runner with the oracle assembled from the submodule (385 of 385); all eleven repository
checks passed before this document added a twelfth. Pattern counts taken by `check_modernize.py`
on the same tree and recorded as the ratchet's first ceilings: 64 register-shaped parameters, 207
workspace parameters, 329 numeric ship-byte indices and 107 named ones, 78 reference members in
the argument-list structs, 22 abstract seams, 28 carry-in parameters, 18 byte out-parameters,
1,162 lines in `Main.cpp` reaching 227 `Elite::` names, 21 stale ledger file names. Three of those
are not the numbers a first grep gave (56, 66 and 26): the greps read comments and missed
`std::uint8_t&`, which is the argument for a counter with a self-test over a hand count.

**2026-09-06 — M0-d: the mutation baseline on Linux.** All five units through
`mutate.py --runner portable` against a green 385-test baseline: 65 mutants, 61 caught, 4 survived,
and every one of the four is a recorded equivalent — the run's own verdict was that every mutant did
what `tools/mutants.json` says. That is the number every re-anchoring slice has to match.

**2026-09-06 — The questions ruled, and the plan reshaped.** All eight of §1's questions answered on
their recommended defaults, and a ninth ruling given that none of them had asked for: detach the
port from the original at the end. Phase M6 is added with seven slices; rule 1, rule 4 and a new
rule 7 say what changes on the way; P12 counts the dependency so that the ratchet can watch it go
(3,549 `6502:` references, 49 oracle test files, 7 tools); R19 to R21 are added. ADR-001 §4, ADR-003
and AGENTS.md R7 carry a one-line pointer each so that nothing in `Design/` disagrees with this
before M6 amends them for real. The total grows from about 70 sittings to about 90, and the least
certain of the new numbers is the comment rewrite, which is prose and not code.

**2026-09-06 — M0-b built, and `World` is `Universe`.** `Tests/GameLogicTests/UniverseImage.h/.cpp`
hold the table of cells and its four walks; `Mirror` and `CompareState` are the bridge's wrappers and
no suite changed a line for it. The rename touched fifteen test files, the two project files, the
executable's comments where "world" meant the aggregate, and this document; where the word means a
planet it stays. What the build found: the table is about a thousand cells, of which the ship blocks
are 370 and the sun's heap 200, and `CompareState`'s first-failure message now says how many cells
differ as well as which was first — the one visible change, and an improvement.
The ratchet's `oracle-test-files` ceiling goes UP by one, from 49 to 50, and this entry is the
record rule 5 asks for: `UniverseImageTests.cpp` loads the oracle because it tests the bridge to
it, which is the instrument M6-b retires — the file goes with the interpreter, not before.

**2026-09-06 — `Universe.h` is `Galaxy.h`, and M0-c is built.** The rename first, by owner ruling and
ahead of M3-a: eleven headers and a test file changed their include, the projects and the ledger
follow, and the file's own comment now says it is the galaxy generator. Then the replay. The
scripted flight through `FlightPort` launches, fights and DOCKS — the autopilot brings the ship into
the Coriolis's slot at step 1,170, which the plan had not promised — and the record is sixteen
digests. The three tests it needed all pass on the first run that had a record to compare against:
the same script twice agrees digest for digest, and each of four one-byte changes after the launch
(the planet's x, a generator byte, the fuel, a speck of stardust) changes the record. What the
first run found: a checkpoint taken twice at step 100, once as a hundredth step and once as the end
of a phase, which the script now takes once. Nothing in `GameLogic/` changed for any of this.

**2026-09-06 — M0-d re-run through the bridge, and M0 closed.** The mutation corpus again on the
tree with the universe image and the replay in it: baseline 392 of 392, then 65 mutants, 61 caught,
4 survived, the four the recorded equivalents — the same tally as before M0-b, which is the bridge
shown to catch what the two hand-written functions caught. CI on the same commit: all three jobs
green, the Windows job's Release suite agreeing with the record the portable runner took. A note on
method rather than on the tree: a shell loop that waited for the run by `pgrep`-ing its command line
matched itself and never ended, which is the kind of thing that looks like a hung run and is not.
M0 is complete; M1-a is next.

**2026-09-06 — M1-a built.** 460 sites, not the 436 the ratchet counted: the regular expression also
reached the headers (`LineHeap.h`'s heap address) and the `.bytes[...]` spelling in `ShipDraw.cpp`,
which the counter's receiver list had not. Suite 392 of 392 after the fixed sites, and again after
the index-parametric routines. **Two of the deleted constants were misnamed, and the bytes they
named were right**: `FlightLoop.cpp` called byte 14 `SHIP_PITCH_COUNTER` and byte 16
`SHIP_ROLL_COUNTER`, and part 9's docking test read them as "the pitch counter" in its comment — but
14 is the nose vector's z high byte and 16 the roof vector's x high byte, which is what `DOENTRY`'s
`CMP #&D6` and `CMP #&24` test (is the ship pointing down the slot, and is it rolled to it). The
oracle never noticed because a name is not a behaviour; the view names them `Nose().zHi` and
`Roof().xHi` and the comment is corrected. The ratchet: `ship-literal-sites` and `ship-offset-sites`
at zero, the second after its counter stopped counting `counts[SHIP_TYPE_x]`, which indexes the
per-type tally and never was a byte of a block; `origin-markers` UP from 3,549 to 3,581 because
every accessor carries the `INWK+n` label the offset it replaces carried (rule 4), and that is the
one direction rule 5 allows a marker count to move before M6. And one counter learned something:
`aggregate-refs` read the views' `Byte&` members as argument-list references and rose from 78 to 89,
which is P5 being miscounted rather than P5 coming back — a templated struct of references is a
view over bytes, generic over constness because it is one — so the counter now cuts those bodies
before it counts, with a sample in its self-test. The first push of this slice went out with
`check_counts.py` red on the three markers above, because the command that ran the checks piped
their exit status away; the fix followed in the next commit, and the lesson is the same one
`check_all.py` was written for.

**2026-09-06 — M1-a closed on the mutants.** The corpus re-run on the migrated tree, through the
portable runner against a 392-test baseline: 65 mutants, 61 caught, 4 survived, the four the recorded
equivalents — the tally of M0-d exactly, with eighteen anchors now naming `work.RollCounter()`,
`work.Energy()`, `work.Ai()` and `screen.work.Z().hi` where they named a number. Rule 3 is met, and
M1-b — `ShipType`, `ShipState`, `AiFlags` and `NewbFlags` as types — is next. M0-a is built with this entry; nothing in `GameLogic/` changed.

**2026-09-06 — M1-b built.** 112 `SHIP_TYPE_x` sites in the library and the executable and 65 in
the tests became `ShipType::x`, 28 parameters in the headers were retyped, and 29 raw hex masks on
`INWK+31`, `INWK+32` and `NEWB` became named bits; the suite was 392 of 392 at each of the three
stops (the type compiling, the flags, the helpers as values). **Three findings, none of which
changed a byte.** First, `FlightLoop.cpp`'s `NEWB_INNOCENT` (0x40) is the COP bit: `KS1`'s
`AND #%01000000 / ORA FIST` folds "you shot a policeman" into the legal status, and the innocent
bystander is bit 5, which the port had as `NEWB_STATION_ALLY`; the two names were each right about
what the bit DOES and wrong about what the source calls it, and `NewbBit` names both as the source
does. Second, `SHIP_DRAWN_OR_EXPLODING` (0xA0) is killed-or-exploding — the main loop's
`AND #%10100000 / JSR MAS4` before `MA65` reads bits 5 and 7, and bit 3 is the drawn bit; the seed
`MAS4` is handed was right and the name was not. Third, `INWK+32`'s bit 6 is two things: the top of a ship's aggression field, which `ORA
#%11000000` sets on every hostile spawn, and a missile's "aimed at us", which `TACTICS` reads with
`ASL A / BMI` — so `AiBit` names it twice and the target-slot field gets its own packing pair.
**And one lesson from the ratchet**: the first build's `Set`/`Clear`/`Toggle` took the byte by
reference, `out-params` rose from 18 to 21, and the counter was reading the code correctly (the
slice plan says why); the helpers are values now and the count is 18 again. The other movements:
`outpost-elite-names` 227 → 226, because `Elite::SHIP_TYPE_COBRA_MK3` and `Elite::SHIP_TYPE_STATION`
became one `Elite::ShipType`; `origin-markers` up again, 3,583 → 3,615, because every enumerator
carries its label (rule 4), and `modernize_ratchet.json`'s `slice` fields now name the slice that
last moved each ceiling — M1-a's three had been left saying `M0-a`. Four mutants re-anchored; the
corpus rerun follows.

**2026-09-06 — M1-b closed on the mutants.** The corpus on the typed tree, through the portable
runner against the 392-test baseline: 65 mutants, 61 caught, 4 survived, the four the recorded
equivalents — M0-d's tally for the third time. `msl-bit5` now reads `ShipStateBit::Exploding`
against `ShipStateBit::OnScanner` and is caught by the same victim it always was, which is the
point of naming the bits: the mutant says what it breaks. Rule 3 is met; M1-c — `Ship` with a
codec, `Bubble::blocks` as ships, `operator[]` retired, the tests through the bridge — is next.

**2026-09-06 — M1-c built.** `Ship` is a struct of fields with `ToBytes`/`FromBytes` as its wire
format; `operator[]`, `bytes` and the M1-a views are gone; `Bubble::blocks` holds ships. 491
accessor sites became fields, 217 `ShipBlock`s became `Ship`, the library reads the codec eleven
times and the tests through `ShipCells`, forty-two array loops and twenty-nine `PokeShip`/`PeekShip`
fixtures. **The oracle earned its keep twice.** The first suite run had twenty tests red, every one
on `INWK+0..26`: four routines had taken M1-a's view by `auto`, which copied a struct of references
harmlessly and now copied a field silently — `MVT1`, `MVS4`, `MVS5` and `NWSPS`'s nose write went
into a copy and the block never changed. `auto&` closed nineteen. The twentieth was `PLS1`, which
the build had read as a POSITION axis because its parameter is called an axis and the routine
"divides an axis by z"; its X is 9, 11, 21 or 23, an orientation component whose sign is bit 7 of
its high byte, and the planet's meridian sweep is the one test that reaches it. Neither would have
been found by reading. The replay's sixteen digests are unchanged, which is M1-c's acceptance:
`Materialise` writes the same bytes from a struct as it wrote from an array. The ratchet moved once,
`origin-markers` 3,615 → 3,623 for `Ship.h`'s field labels (rule 4), and `aggregate-refs` stayed at
78 — the counter had already learned to cut the views it no longer needs to cut. Mutants: the
anchors moved with the regex and `mutate.py --check` was green before the first build; the corpus
rerun follows.

**2026-09-06 — M1-c closed on the mutants.** 65 mutants on the struct tree: 61 caught, 4 survived,
the four the recorded equivalents — M0-d's tally for the fourth time, with the anchors reading
`work.rollCounter`, `work.energy`, `work.ai` and `screen.work.z.hi`. Rule 3 is met; M1-d — the
typed `Commander` with the seventy-seven-byte codec, the checksums and the save file over it — is
next.

**2026-09-06 — M1-d built.** `Commander` is a struct of named fields with `Credits` and `Tally`
for the two multi-byte orders that matter and `ToBytes`/`FromBytes` as the seventy-seven-byte wire
format; `Field` stays as that format's table; 431 sites by regex and a dozen by hand; the
checksums, the default commander, `SaveCommander`, `LoadCommander` and the bridge's `TP` cells are
codec calls. **First suite run green**, 392 of 392 — the first slice of M1 where the oracle found
nothing, and the reason is the order of the previous three: the ship's slice had already made the
bridge and the tests speak bytes through a codec, so this one changed one struct and the pattern
held. The plan's `LightYearsTenths`, `Laser` and `Equipment` types were not built, and the slice
plan says why: a byte that is compared and subtracted at twenty-nine sites needs operators before
it needs a name, and the equipment bytes carry three different "fitted" values the original
chose. Ratchet: `outpost-elite-names` 226 → 225 (`Elite::Field` left the executable,
`Elite::CommanderBlock` became `Elite::Commander`), `origin-markers` 3,623 → 3,654 for the field
labels (rule 4). One mutant re-anchored; the corpus rerun follows.

**2026-09-06 — M1-d closed on the mutants.** 65 mutants on the typed-commander tree: 61 caught, 4
survived, the four the recorded equivalents — the fifth run of M0-d's tally, with `mi-tally`
reading `kills.hi` against `kills.lo` and caught by the mission's `TALLY+1` compare as before.
Rule 3 is met; M1-e — the parsed `Blueprint` table, `XX0` as a pointer to one, the station's
self-modified entry as `Bubble::stationType` — is next.

**2026-09-06 — M1-e built.** The region is a parsed table: `Blueprint` with the header as fields and
the three tables as spans, `BlueprintOf(type)`, `BlueprintAt(address)` for the bridge, `XX0` a
pointer, the station's entry a `ShipType`; `ShipByte`, `BlueprintAddress` and
`ShipBlueprintExtent` are gone and 61 header reads in the library say what byte they read.
**The famous disagreement is not what the port thought it was.** `ShipDataTests` has said since
slice 3a that three headers "overrun" the gap to the next blueprint, and the first build placed
each span at the blueprint's start plus the header's offset, as the words say — and `LL9` drew the
Splinter wrong. The Splinter's edges offset is &FD78 and the Thargon's &E7E6: added to `XX0` in the
6502's sixteen-bit arithmetic they WRAP, and the tables they name sit before the blueprint — the
Splinter draws with the Escape Pod's six edges and the Thargon with the Canister's fifteen, two
ships sharing another's data by an offset nobody would write by hand. The Splinter's faces, at +68,
do run into the Shuttle's header eight bytes on, so that one table is read out of the next
blueprint, which is the part the old test measured. The parser now places every span at the
wrapped address and the comment in `ShipBlueprint.cpp` says which ship's table each of the two
borrows. A port that had cut the region into thirty-three arrays would have found this on day one
and had to decide; a port that kept the region as bytes never had to know. `SeedExplosionCloud`
narrowed from `XX0` to the one byte `EE55` reads (the app's implementation is a documented no-op
either way, §6.91). Ratchet: `origin-markers` 3,654 → 3,702 for the header's twenty labels and the
parser's (rule 4); everything else unmoved, `outpost-elite-names` included, by the seam's
narrowing. Mutants: every anchor still applies; the corpus rerun follows.

**2026-09-06 — M1-e closed on the mutants.** 65 mutants on the parsed-blueprint tree: 61 caught, 4
survived, the four the recorded equivalents — M0-d's tally for the sixth time, with no anchor
moved, because no mutant ever named a blueprint byte. Rule 3 is met; M1-f — the line heap
addressed by offset, `NWSHP`'s chain in `TryReserveHeap`, the sun's heap lent as a span — is
next, and it closes M1.

**2026-09-06 — M1-f built, and M1 is complete.** `HeapOffset` addresses the arena, `TryReserveHeap`
holds `NWSHP`'s chain, `Ship::heap` and `Bubble::heapBottom` are offsets, `LineHeap` takes the
sun's heap as a span; thirty-one offset sites in the library and thirty-eight `FromAddress` in the
tests, `ShipHeapAddress` and the public `SlotAddress` gone. First suite run green. **The one
decision worth recording**: the offset's default. `ZINF` zeroes `INWK+33/34`, so a ship the game
has not yet given a heap points at address 0 — outside the arena — and a `HeapOffset` that
defaulted to `K%` would have made `Ship{}` code to bytes `00 F9` where the oracle has `00 00`.
The default is the zero pointer, `Top()` is `LS%`, and the `static_assert` in the header says so.
Ratchet: `outpost-elite-names` 225 → 224 (`Elite::SUN_HEAP_ADDRESS` left the executable with the
base argument), `origin-markers` 3,702 → 3,705. And the M1-a mistake, repeated once: the push went
out with `check_counts.py` red on one marker in the risk register because the command ran the
checks through a `grep` that succeeded on the word FAIL; the follow-up commit fixed the marker, and
the habit is now to run `check_all.py` on its own and read its exit status. **M1 as a whole**: six slices, every one green on
the oracle and the corpus, four findings the bytes never showed (two misnamed offsets, two misnamed
flag bits, a misnamed state mask, and the Splinter's borrowed edges), and the data model is now
`Ship`, `Commander`, `Blueprint`, `ShipType`, the flag bits and `HeapOffset`, with the bytes as
their codecs. M2 — explicit calling conventions — is next; `Design/ADR-006` is due when it opens
(§0).

**2026-09-06 — M1-f closed on the mutants, and M1 with it.** 65 mutants on the offset-addressed
tree: 61 caught, 4 survived, the four the recorded equivalents — M0-d's tally for the seventh time
and the last of M1, with no anchor moved. Rule 3 is met for every slice of the phase.

**2026-09-06 — ADR-006 written, and M2-a built.** `Design/ADR/ADR-006-modernisation-architecture.md`
records the architecture Modernize.md §4 builds toward and what M0 and M1 settled on the way — the
layering, structs with codecs and the zero-byte default rule, the four classes a workspace field
can fall into, the four ports, the pipelines, the verification rules and the detachment — with a
status table by phase; M5-d amends it from what was built. Then the census. `tools/channel_census.py`
found 451 routines and 49 workspace fields, and the mechanical columns confirmed every row §4.3
already had and added the ones it did not: `T1` is a parameter `DIALS` hands `DIL`; `CNT` is a
local everywhere except `CIRCLE2` → `BLINE`; `TGT` and `CNT2` are the ellipse's parameters; `XX`
and `YY` are two callers' coordinates and not a shared pair; `K2` is `MV40`'s local and the
ellipse's parameter; `SWAP` and `XX13` are the clipper's results; `XX3` is `LL9`'s stage result
that the explosion reads. **Two findings.** `dontclip` is a constant in this port: only `ZERO`
writes it, to zero, and only `LL145` reads it — the routine that sets bit 7 in the original is on no
path this build takes, so the clipper's "return the line unclipped" branch has never run here and
no test reaches it. And `SC` joins `Projection` as state that outlives a call on purpose: `DIALS`
sets the screen pointer once and `DIL`/`DIL2` advance it seven calls running, which slice 3d-b
documented and the census now lists. The tool is the thirteenth repository check
(`channel_census.py --check`: the table in §4.3 matches the tree and no field lacks a verdict);
nothing in `GameLogic/` changed.

**2026-09-07 — M5-e-1: `Game::Sounds()`, and the one place the executable reached INTO the library.**

Task #13's first item, and the owner's ruling on the three §4.4 sketched: `Sounds()` real, `Frame()`
recorded, `StateHash()` library-native (M5-e-3). `Game` owns the SID log now — `SidWriteLog m_sid`,
declared before the `Ports` that binds `sid` to it — and answers it through `Sounds()`, with
`ClearSounds()` for the executable to call once it has applied what it read. Until this slice the
executable owned the log: `SoundOutput::Direct()` handed the game a reference into the app's own
buffer, `Pump` applied that buffer and cleared it, and the library wrote into memory it did not own.
It was the only place the app reached into library state rather than being handed a value, and
ADR-007 §1's rule — a byte of game state goes in the library — had not been applied to it because
`Ports.h` argued, correctly, that the log is not STATE. It is not; but it is the library's output,
and the executable reads outputs, it does not lend buffers for them.

**WHAT MOVED IN THE EXECUTABLE.** `SoundOutput::Pump` takes the log as an argument and `Direct()`
and `m_direct` are gone; `GameShell::AttachSound` takes the `Game` whose log it drains, and `Turn`
reads `Sounds()` before the present and clears it after — the same point in the frame the old
buffer was applied at, so the writes reach the chip in the same order relative to the interrupt's.
`Main.cpp`'s construction loses `audio.Direct()`; `check_outpost.py` sees the arity change and
agrees with the header.

**`Sounds()` answers the log, not §4.4's span.** The sketch said `std::span<const SoundEvent>`; the
log carries a `dropped` count beside its writes and `Apply` takes a log, so a span would lose the
count and gain a conversion. `Frame()` is not built because `State().canvas` is what it would
return and the executable already reaches it; §2.1 and ADR-007 say so rather than leave a line
unbuilt.

Two test fixtures had been handing `Game` a log of their own — `FlightPort` and `GameTests`' `Bare` —
and neither read it back. Both members go. 395 of 395; all 16 repository checks; the replay digest
unmoved; `outpost-elite-names` unmoved at 69, because `Game` was already a name the executable
reached and its members are not counted; `origin-markers` 4,123 → 4,125.

**2026-09-07 — M5-d-2: the hundred C arrays were thirteen, and the thirteen are `std::array`.**

M5-d left `-modernize-avoid-c-arrays` excluded with the note "100 real findings, its own slice". The
hundred was a phantom: clang-tidy reports a diagnostic in a HEADER once for every translation unit
that includes it, and 96 of the hundred were three lines of `VideoState.h` and two of `Canvas.h`
counted over the files that include them. Unique sites: thirteen, in nine files. That is worth
recording as a lesson about reading the tool — a count from a sweep is a count of REPORTS, and the
assessment that wrote "100" into M6-0's prose had not de-duplicated it.

Eleven of the thirteen were indexed and are `std::array` by substitution. The two that were not
are the pair `Canvas` hands `BlitSprite` — `santana` and `lotus` as the raster split leaves them,
one entry per half — which `SpriteRegisters` held as raw pointers because a C array decays to one.
They are `std::span<const T, 2>` now: the extent is in the type, the aggregate initialiser is
unchanged, and `[half]` reads the same. The exclusion is gone and `check_tidy.py` is clean with the
check on; `modernize-*` runs less two — trailing return types and `auto`, both style decisions this
tree has made and neither of them a site count.

395 of 395; all 16 repository checks; no ratchet count moved.

**2026-09-07 — M5-a-8: a screen RAM byte is two colours, and the type makes the original's names
tell the truth.**

The second family. `RED2`, `GREEN2`, `YELLOW2`, `BLACK2`, `MAG2` and `BULBCOL` are screen RAM bytes
in multicolour bitmap mode — the high nibble is what a `%01` pixel draws in and the low nibble what
`%10` draws in — and the port had them as `std::uint8_t` constants whose names were the original's.
`CellPalette` is the pair, built from two `Colour`s, and writing the constants as pairs is the
finding made permanent: `MISSILE_ARMED` is `{Orange, Yellow}` and the original calls it `YELLOW2`;
`MISSILE_NONE` is `{DarkGrey, Yellow}` and the original calls it `BLACK2`. A `static_assert` in
`Colours.h` pins both. The default is `{Black, Black}`, because that is `COL2`'s shipped value and
a printer that runs before `RES2` must print invisibly, as the original's does.

**TWO CONSTANTS WERE DEFINED TWICE, and the type is how they were found.** `Market.cpp` and
`NameEntry.cpp` each carried a private `TEXT_COLOUR_TYPING = 0x40` and `TEXT_COLOUR_NORMAL = 0x10`
in an anonymous namespace — the same bytes `TextPrint.h` exports as `TEXT_COLOUR_PURPLE` and
`TEXT_COLOUR_WHITE`, two files away, under other names. And `Dashboard.h`'s `MISSILE_GREEN` was
`MISSILE_READY` under a second name: both `GREEN2`, both `&57`, one for `msblob`'s indicator and one
for the byte `KILLSHP` hands `ABORT`, which are the same green for the same reason. Four constants
collapsed to two headers' worth; nothing in the game changed.

`Canvas::Write` and `ExclusiveOr` take a `CellPalette` beside the byte, because the bulbs toggle a
palette and the text printer stores one; `TextState::cellColour` is `palette`, named for what it
holds (rule 7); the missile API takes the type; the loader's border pair joins the family;
`UniverseImage` gains a `Palette` cell and `COL2` uses it. The two sweeps that hand the routines every
byte — `MSBAR` over 256, `CHPR` over `COL2` — go through `CellPalette::Of`, because every byte IS a
palette and the sweep is the point.

The one thing that is NOT a pair and stays a byte: `COLOUR_RAM_YELLOW`, colour RAM's single nibble,
which is a `Colour` in memory the oracle compares — M5-a-7's line, unchanged.

395 of 395; all 16 repository checks; the replay digest unmoved; `origin-markers` 4,122 → 4,123.

**2026-09-07 — M5-a-9: `COL` is a pixel pattern, and the compass and the dials had been calling one
a colour since slice 3d.**

The first of the two families M5-a-7 named. `RED`, `YELLOW`, `GREEN` and `WHITE` are four
multicolour pixels packed in a byte — `%01010101`, `%10101010`, `%11111111`, `%01011010` — and the
port had them as `std::uint8_t` constants called `DIAL_DANGER`, `DIAL_NORMAL`, `COMPASS_AHEAD` and
`COMPASS_BEHIND`, a `Compass::colour` field, a `DangerColours` pair and a `DialColours` pair. Every
consumer ANDs the byte with a mask — `CPIX2` with `CTWOS2`, `DIL` with the block it has just
shifted, `DIL2` with `CTWOS` — and a byte whose only operation is AND-with-a-mask is a pattern.
`PixelPattern` is the type: `Red`, `Yellow`, `Green`, `Striped` for the Thargoid's `%01 %01 %10 %10`,
and `Blank` for the `%00` four times that `DIL` falls through to when `K+1` is zero — which the
port had been spelling as a literal `0` with no name. `BLUE`, `CYAN` and `MAG` get no enumerator,
because the original defines all three as `YELLOW`.

**THE ASSESSMENT THAT SCOPED THIS SLICE HAD FILED TWO OF ITS SITES UNDER THE WRONG FAMILY.**
M5-a-8's task description listed `DangerColours` and `DialColours` among the palette pairs. They are
`PZW`'s `RED`/`YELLOW` and `DIALS`' `K`/`K+1` — patterns, this slice's — and reading the AND sites
is what said so. Which is the point of building the types: a name can be filed wrongly and an
operation cannot.

`PlotDash` and `PlotBlock` take a `PixelPattern`; `SCAN`'s `scacol` read goes through `PatternOf`,
because every byte is a pattern and the table is the assembler's; the compass field is `pattern`,
named for what it holds (rule 7). `UniverseImage` gains an `Enumerated` cell for a byte the port
holds as a scoped enum, and `COMC` is the first to use it. Every site was a rename or a cast at a
boundary; the replay digest did not move.

395 of 395; all 16 repository checks; `origin-markers` 4,121 → 4,122, rule 4's reason.

**2026-09-07 — M6-0: every M0–M5 row is built, and M6 is not safe to start.**

The owner asked whether anything was still open beside M6, on the ground that M6 removes the
oracle and so must find no gap. The answer is that "no rows open" and "no gaps" are different
questions, and the difference is R19: a fixture pins what the tests asked while the original was
here, so anything the oracle can pin today and no test asks is unpinned for ever after M6-b. Sorted
by that lens, the tree has eight such things, and they are now the M6-0 rows in §6 — a gate M6-a
is blocked on, ordered by irreversibility.

**THREE WERE KNOWN AND PARKED.** The 6510 port register (task #11) has been named since M3-b as
"what unblocks it is a harness slice", and it is the largest single gap in the tree: the oracle's
flat memory aliases the VIC registers onto `XX21`, so `ShipDrawEffects` is still a seam, `DOEXP`
is trapped in every composition test, and **no whole-frame comparison with an explosion in it has
ever been made against the original**. The eleven control codes (task #12) split three ways and
eight of them are comparable now. And two fixture faults M3-b called one-line fixes — `Where`
without `SUNX`/`LSY2`, the heaps at `&0C00` — were never made.

**TWO ARE PRECONDITIONS §4.10 ALREADY STATES AND NOTHING ENFORCES.** "The M0-c replay must cover
launch, flight, combat, docking, death and the escape pod": it covers four of the six. And seven
routines are only ever trapped — `TRADEMODE`, `NLIN`, `TT67`, `DK4` ported and never compared on
their own; `WSCAN`, `REDU`, `GTNMEW` not ported — and a trapped routine's fixture records the trap.

**THREE ARE INSTRUMENTS M6-a's OWN ACCEPTANCE NEEDS.** "Every *Port* row has a test that calls it"
cannot be read off anything: the ledger's ✅ is per label and inconsistent (twelve of thirty-three
Port rows have none, including the flight loop's sixteen parts, the most-tested code in the tree),
and a marker-to-test name match over 3,117 markers is noise — the attempt in this assessment
returned 433 "uncovered" labels, nearly all of them opcodes and sub-labels. Mutants cover eight of
fifty-two hand-written files, and after M6-b they are the only instrument that says whether a test
would NOTICE rather than merely what it asked; M6-b's acceptance still counts "five units" from
before the corpus reached nine files. And `StartUpEffects` is an abstract class with nothing in it
but a destructor, still counted.

**AND TWO SENTENCES OF THIS PLAN WERE NO LONGER TRUE.** §2.7's "what nothing pins: the outer loops
and the mode machine in `Main.cpp`" — they are `Game`'s since M3-c and M4-d and the replay drives
them; the sentence now names M6-0's list instead. And §4.10's coverage requirement, the strongest
sentence in the document, had no row enforcing it; M6-0-b is that row.

Nothing in `GameLogic/` changed. The rows are the deliverable, and the order is the point.

**2026-09-07 — M5-d: `.clang-tidy` calls itself this repository's single source of truth, and
nothing in this repository had ever run it.**

The row asked for ADR-006 amended from what was built and `.clang-tidy` widened one `modernize-`
check per commit. The ADR half is four sections and is below. The tidy half turned out not to be a
widening at all until something ran the file.

**IT IS ANOTHER REPOSITORY'S FILE, AND NOT ONLY IN ITS VALUES.** `.clang-tidy` was adopted whole
from the sibling tree Outpost.Warzone so that engine code could move between the two without a
rename pass, which is a good reason and is still true of the option values. What came with them was
the STATUS and the REASONS. The file claimed "CI gates this over GameLogic and NeuronServer"; there
is no `NeuronServer` here. It named `Build/CheckProjectFiles.py` and
`Design/Archive/MmoScalabilityPlan.md`; neither exists here. It justified three of its four
exclusions by wire records, mesh vertices, GPU instance structs and "metres to eighth-metre lattice
steps, radians to turns16, i64 sectors to the wire's i32" — this port's numeric model is eight-bit
arithmetic with explicit carries and there is no wire at all. And it said GameLogic "has been swept
clean", on a tree where **no job, script or check has ever invoked clang-tidy**. `WarningsAsErrors:
'*'` was a setting with nothing behind it.

**THE FIRST SWEEP FOUND THIRTY-SIX DIAGNOSTICS, AND WIDENING `modernize-*` TOOK IT TO FORTY-FOUR;
sixteen of them were code, and two were undefined behaviour.** `ReadFlightControls`
held five locals still wearing the `_` that AGENTS.md gives a PARAMETER — aliases left behind when
M3-a and M5-a turned those parameters into fields of the `Universe` the routine already takes.
Three dead stores were transcriptions of a 6502 flag nothing reads: two `LSR` carries in the
divider's scaling tail, where only the third is read, and the initialiser of a loop-local carry in
`AddDebris`. **And the fourth is the fourth vestigial assignment this port has carried**: part 4's
`LDA SSPR / BNE MTT1` had been transcribed as `toPart3 = true`, four hundred lines after the only
thing that reads `toPart3` has run — the branch is the `if`, and falling out of the block IS
reaching part 3. **AND `DecideMissile` AND `DecideStation` COULD FALL OFF THEIR OWN ENDS.** Both open with
`if (_frame.type == ShipType::Missile)` — or `::Station` — around their whole body and have no
return after it, so a call with any other type runs off the end of a function that returns a
`Tactic`, which is undefined behaviour. It is unreachable only because `RunTactics` dispatches on
the same type first, which the compiler cannot see: `CPX #MSL / BEQ TA18` is ONE test in the
original and was two here. The guards are gone and the dispatch is the test.

The rest were a lambda named as if it were a type twice, a local dodging a keyword with a trailing
underscore, a `const` C array of offsets shouting in capitals, two index loops that were walks, and
two more dead carries. All sixteen are fixed; 395 of 395 and the replay digest unmoved, which is
what says the dead stores were dead rather than merely unread.

**THE REST ARE EXCLUDED WITH THIS TREE'S OWN REASONS, and measuring each one changed the answer
twice.** `-bugprone-narrowing-conversions` and `-clang-analyzer-optin.performance.Padding` were
inherited exclusions that fire NOTHING here once one struct's fields are ordered — the padding
finding was a five-entry local table in `Equipment.cpp` with no layout obligation at all — so both
are **removed** rather than carried, which is a strengthening the row did not ask for. Three
exclusions are new and each is about the port's method: `-bugprone-branch-clone`, because two 6502
branch instructions to one label are two branches and folding them into a `||` would stop saying
what the original does; `-bugprone-implicit-widening-of-multiplication-result`, because the screen
arithmetic is `int` on purpose and its largest term is 64,000; and
`-clang-analyzer-optin.core.EnumCastOutOfRange`, because the analyser is simply wrong about a
scoped enum with a fixed underlying type — `TypeOf` exists so a sweep over all 256 bytes can go
through it, and C++ says every one of them is a value of `ShipType`.

**AND THE WIDENING IS THREE CHECKS SHORT OF `modernize-*`, each one a decision.**
`-modernize-use-trailing-return-type` is 3,835 findings and a style this tree does not use;
`-modernize-use-auto` is 174 and would hide the WIDTH, which in a port whose numeric model is the
byte is the one thing a reader needs; `-modernize-avoid-c-arrays` is 100 real findings and is its
own slice rather than a rider on this one. `-performance-enum-size` stays, and its reason here is
the opposite of the inherited one: the 78 enums it wants smaller are the port's own OUTCOME types
(`JumpOutcome`, `KeyAction`, `DockingOutcome`, `DigitResult`) which live between two functions and
never in memory — the enums that ARE bytes the game stores already say `: std::uint8_t`, and
blurring the two would lose a distinction M4-c and M5-a spent slices establishing.

`tools/check_tidy.py` is the gate, and it runs where the compiler cannot: `GameLogic/pch.h` reaches
`<WinSock2.h>` through `NeuronCore.h`, so the sweep goes through `Tests/PortableRunner/Shim` — the
stand-in that already lets g++ compile these sources. `Outpost/` is deliberately not swept; its
DirectX headers are the Windows job's, and a linter that has never seen them would bury this
library's findings under theirs.

**ADR-006 IS AMENDED IN FOUR PLACES, and §5 is the one worth reading.** It had predicted M4's
pipelines as `ScaledOrientation → FaceVisibility → ProjectedVertices → EdgeSelection → ClippedLines
→ HeapRun` plus a `Decision`/`Apply` pair, and four of those predictions did not survive: `LL9` is
SEVEN stages and they are the original's own part blocks rather than a renderer's; the flight
frame's are `Contact`, `ScoopResult`, `DockingTest`, `Impact`, `Aim` and `KillOutcome`; the one
`Decision` type is three different answers because the `bool`s it replaced were three different
things; and `LoopOutcome` is not retired. §2 carries the strong types that were refused, §8 the
`constexpr` tables, and the status table says M2, M4 and M5 are built rather than planned.

`check_all.py` is sixteen checks and the sweep comes back **clean over all sixty-eight files** under LLVM 18 -- the first time that sentence has been measured in this repository rather than
inherited. It runs a process per file over a pool, because serially it is half an hour and
`check_all.py` is meant to be run whole before every commit.

395 of 395; the replay digest unmoved.

**2026-09-07 — M5-b: the tables are `constexpr`, and the row's acceptance named a suite that had
been deleted eight hours earlier.**

**THE ACCEPTANCE HAD TO BE REWRITTEN BEFORE ANY OF IT COULD BE BUILT.** The row read "`TableTests`
green; one `static_assert` per table against the oracle-checked value", and `TableTests.cpp` is not
in the tree: `main` retired the oracle comparison of the generated tables this morning, on the
ground that the tables are the port's own data now and the three pictures among them are EDITED
through `tools/bitmaps.py` rather than regenerated. So the row asked for a suite that no longer
exists to stay green, and for assertions "against the oracle-checked value" when nothing checks them
against the oracle any more. That is §6.73's pattern for the fourth time — a criterion scoped before
the thing behind it changed — and the criterion is rewritten here rather than quietly met.

**WHAT THE TABLES BECOMING `constexpr` IS ACTUALLY WORTH, stated plainly because it is less than it
sounds.** `const std::array<std::uint8_t, N>` with a brace initialiser is already
constant-initialised into read-only data; `constexpr` changes no byte of the program. What it does
is make that a GUARANTEE the compiler checks rather than an optimisation it happens to perform, and
it is the precondition for anything reading a table in a constant expression. One line of
`extract_tables.py` emits it, thirteen files regenerate byte-identical apart from the keyword, and
the three pictures — which that tool deliberately does not overwrite — take it by hand. `bitmaps.py`
finds an array by `identifier = {`, so it is unaffected, and the no-op import still changes nothing.

**AND THE ASSERTIONS ARE ABOUT SHAPE, WHICH IS THE HALF A COMPILER CAN HOLD.** The row imagined
"a known value" per table. A hand-typed `SINE_TABLE[8] == 0x59` would be a worse instrument than the
byte-for-byte oracle comparison that was just retired, and it would pin nothing anybody could get
wrong in a generated file. What IS worth pinning is what `LookupTables.h` already claims in prose
and nothing checks: the font is "96 characters of eight rows"; the sprite sheet is
`SPRITE_DEFINITION_COUNT * SPRITE_BYTES`; the scanner colours are "sized by what indexes it, which
is a ship TYPE", so `SHIP_TYPE_COUNT + 1`; `celllook` has one entry per `Canvas::CELL_ROWS`; the
dashboard's two palettes cover exactly the cells below the raster split; `CTWOS2` is the aligned
masks PLUS TWO, because the extra pair are the wrapped cases and not padding; the eight per-effect
sound tables are eight columns of one table with `SOUND_EFFECT_COUNT` rows; `TGINT` is the thirteen
`DKS3` walks. Every one of those is §6.8's own rule — size a table from what can INDEX it — and
every one was previously checked by a person reading two files at once.

They live in `GameLogic/LookupTables.cpp`, a translation unit that emits nothing. It is not in the
header because the constants belong to the code that INDEXES each table, and `LookupTables.h` is
included by nearly everything: reaching `SHIP_TYPE_COUNT` or `Canvas::CELL_ROWS` from there would
make the tables depend on the port rather than the other way round. And the assertions read
`std::tuple_size_v` — the DECLARATION, never the bytes — which is why they hold with the big tables
still in their own `.cpp` rather than putting 160 KB of initialiser into every translation unit that
wants a font.

Shortening `SCANNER_COLOUR_TABLE`'s declaration by one entry gives
`static assertion failed: the scanner has a colour for every ship type and for none`, which is the
check that they measure something.

`origin-markers` 4,107 → 4,121, rule 4's reason: each assertion names the 6502 label it is about.
395 of 395; all 15 repository checks; the replay digest unmoved.

**2026-09-07 — M5-c: twenty ledger homes named files the port never built, and ten more names are
history that must NOT be corrected.**

`Source-Inventory.md` is the coverage ledger: one row per family of 6502 labels, with a HOME cell
saying which port file holds them. Twenty of those cells named files that are not on disk, and the
ratchet had been counting them since M3-c without anybody reading what they were. Reading them is
the slice, and the twenty split cleanly:

  - **Most are the port filing a routine by what it TOUCHES rather than by what it is near**, which
    the journal has recorded happening nine separate times. `main_game_loop` went to `GameLoop.cpp`
    and not `Spawner.cpp`; `circle`/`circle2`/`bline` to `PlanetDraw.cpp` and not `Circles.cpp`;
    `TITLE` to `Flight.cpp`, because it creates a ship, moves it and draws it. `MAS1`–`MAS3` are in
    `FlightLoop.cpp` and `TAS1`–`TAS6` in `Tactics.cpp`, where the plan had one `Orientation.cpp`
    for both.
  - **Some are a plan file that was never built at all**, and the workspace row is the one worth
    naming. `ZeroPage.h` and `Workspace.h` do not exist because §4.4 files a byte with its OWNER, so
    the zero page split across `MathWorkspace` (`Arith.h`), `GeometryWorkspace`, `Projection` and
    `ClipState` (`ShipDraw.h`), `Universe` and `ShipSlot.h`. The row now says that rather than
    pointing at two files nobody wrote.
  - **And one row already knew**: §6.129's raster row says in as many words that "this row is
    misnamed, and its home does not exist". It is `ScreenTables.cpp` and `Raster.cpp` now.

**THE TEN THAT REMAIN ARE THE FINDING, AND CORRECTING THEM WOULD BE VANDALISM.** With every home
fixed the counter still read ten, because it counted every backticked file name in the file and the
NOTES are history: "built 2026-09-05 in `Spawner.cpp` as one function" was true the day it was
written. The plan's own rule for numbers says the same thing — a journal number is history and is
never touched, only a marked one describes the tree. Two of the ten are stronger than that: §6.129's
note and the workspace note above name a missing file precisely IN ORDER TO SAY the tree does not
have it, so a counter over the whole file demands that a finding be deleted to reach zero.

So the counter reads the HOME cell and nothing else, and that is a narrowing of what it measures
rather than a ceiling lowered to meet the tree — R18's failure mode is the other direction, and the
ceiling went 20 → **0**, not up. `inventory.py --check-homes` applies the same rule and is the
repository check behind it; `inventory.py --self-test` plants BOTH traps, a stale home that must
fail and a stale note that must not, because a check that could not tell them apart is the one that
would have made this slice destroy history to go green.

`check_all.py` is fifteen checks (`--check-homes` and `--self-test`), the workflow runs both,
`inventory-stale-files` 20 → 0. 395 of 395; nothing in `GameLogic/` or `Outpost/` changed.

**2026-09-07 — M5-a-7: `Colour`, and the original has no colour constants at all.**

The last of M5-a's strong types the owner asked for, and building it found that the plan's own
description of it could not be met. The row says "scoped enums with the ORIGINAL VALUES". The C64
build defines eleven constants with colour names and **not one of them is a colour**:

  - `RED` (%01010101), `YELLOW` (%10101010), `GREEN` (%11111111) and `WHITE` (%01011010) are FOUR
    MULTICOLOUR PIXELS packed in a byte. They are what `COL` holds and what `CPIX2` ANDs with a
    mask out of `CTWOS2` — pixel patterns, not colours. `BLUE`, `CYAN` and `MAG` are all defined as
    `YELLOW`, because `scacol` carries scanner colours from the BBC build that this machine cannot
    honour, so three of the seven names describe nothing at all on a C64.
  - `RED2` (&27), `GREEN2` (&57), `YELLOW2` (&87), `BLACK2` (&B7), `MAG2` (&40) and `BULBCOL` (&E0)
    are SCREEN RAM palette bytes, each holding TWO indices: the high nibble is what %01 draws in and
    the low nibble what %10 draws in. Their names describe the high nibble and two describe it
    **wrongly** — `YELLOW2` is orange (8) over yellow (7), and `BLACK2` is dark grey (&B) over
    yellow. `MISSILE_NONE` was zero in this port until slice 3d-d-iii-b for exactly that reason: a
    name that sounds like an index, over a byte that is a pair.

The thing that IS one colour is the VIC-II index 0 to 15, and the original never names it. So
`Elite::Colour` is that index with the sixteen VIC-II names, and the two constant families above are
not forced into it — they get their own slices (M5-a-8 for the palette pairs, M5-a-9 for the pixel
patterns), which is rule 8 rather than three patterns in one commit.

**AND THE LINE THE TYPE DRAWS IS BETWEEN MEMORY AND A REGISTER, which is what found the defect.**
Colour RAM and screen RAM stay bytes: the oracle compares them address by address and the loader
dumps tables straight into them. A REGISTER is where the chip takes four bits and a byte becomes a
colour, so `Canvas`'s three colour registers take the byte that was stored and answer a `Colour`,
and `VideoState::colour` — which is already documented as the registers rather than the stores —
holds `Colour` outright.

**THE PORT HAD NEVER LATCHED VIC+&21, and the energy bomb is what makes that matter.** `COMIRQ1`
does `BIT BOMB / BPL nobombef / INC welcome` and then stores `welcome` whole into the background
register, so eight frames of bomb carry the byte past 15 and it keeps counting to 255 and wraps. The
VIC-II keeps four bits; this port resolved its canvas into COLOUR INDICES that the presenter looks
up in a sixteen-entry palette, and handed it 39, or 200, or whatever the counter had reached.
**Nothing could have caught it.** The oracle holds the same unlatched byte the port did —
`TheRasterInterruptMatchesCOMIRQ1` already compares `welcome` at &9C and &FF and agrees, because the
STORE was always right — and the defect is one step further on, in what the port then does with the
register, where no comparison looks. It is the `DNOIZ` shape again: a comparison cannot see a
difference that both sides do not have.

`TheBombFlashCountsPastEveryColourTheChipHas` is the instrument: forty passes of bomb, the byte
past 15, the canvas latching it to seven, and no resolved pixel indexing past the palette. Removing
the mask makes it read `expected 7 actual 39`, which is the check that it measures the fix.

`Outpost::Colour` is `Outpost::Rgb` now. It is the presenter's RGB triple and `Elite::Colour` is the
index — one name each, and the header's own comment already said which of the two the game means.

The three strong types M5-a named and did not get — `Options`, `View` and `Message` — are refused
with their evidence in ADR-006 §2 rather than left looking unfinished: `Options` would undo §4.4's
split and `DKS3`'s ordering is pinned better by the byte-checked `TGINT` table anyway, and `QQ11`
and the message tokens are both indexed AND combined arithmetically, so an enum would cost more
casts than it removes.

403 of 403 (one new test); all 14 repository checks; the replay digest unmoved. `origin-markers`
4,106 → 4,107, which rule 5 permits for rule 4's reason: `Colours.h` is new code and the marker ties
`ColourOf` to what the chip does.

**2026-09-07 — M5-a-6: the digest gap ADR-007 §5 named is closed, and the seven bytes it named were
three kinds.**

The owner's ruling was "close it now", and closing it is the FIRST time this project has re-taken
the replay record under rule 1's first case — a deliberate widening — in five re-takes. The other
four were all the second case, a defect found and fixed. **No line of `GameLogic/` changed.** What
changed is what the digest looks at: `Where` gained five label lookups and `UniverseImage` five
cells, and every one of the sixteen checkpoints moved while the flight did not — 1,170 steps ending
`Docked`, before and after, which is what a widening should look like and what a behavioural change
would not.

**THE ARITHMETIC IS THE FINDING.** ADR-007 §5 listed seven bytes as one gap. They are three kinds
and the section is amended to say so:

  - **Five can have cells and now do** — `safehouse`, `QQ8`, `JSTGY`, `JSTE`, `MUTOKOLD`. `QQ8` is
    two bytes the port keeps as one `std::uint16_t`, so it goes through the same `AddressPair`
    helper `XX0` uses; `safehouse` is six bytes and goes through `Run`.
  - **One was a duplicate**: `soundDisabled`, a second `DNOIZ` beside the one the sound system
    reads, which M5-a-5 found and deleted. It never needed a cell; `DNOIZ` has had one since
    M3-b-2a and it was bound to the working half all along.
  - **One cannot have a cell at all, and this is the durable part.** `crosshairStep` is what `TT17`
    leaves in X and Y. A cell is a port field paired with a 6502 LABEL, and REGISTERS have no
    address for `Where` to look up — so there is nothing to name, and no amount of widening reaches
    it. It is pinned by `KeyboardTests` comparing `TT17` against the oracle and by nothing in the
    digest. ADR-007 §5 and §6 now say that outright rather than leaving it filed as an open edit,
    which is how it would have been rediscovered.

**AND THE OTHER 401 TESTS PASSING IS THE EVIDENCE THE WIDENING IS SOUND.** The five new cells are
`CellScope::Compared`, so `CompareState` now checks them against the oracle everywhere it already
ran; the first run after adding them was 401 passed, 1 failed, and the one failure was the record
itself. Had any of the five disagreed with the original the suite would have said which byte and
where, before the record was touched. Re-taking a record on a suite that is otherwise green is the
only safe order to do it in, and it is the order the failure message is designed for.

402 of 402; all 14 repository checks; no ratchet count moved (`origin-markers` counts `GameLogic/`
and the change is entirely in `Tests/`).

**2026-09-07 — M5-a-5: there were TWO `DNOIZ` bytes, and the pause screen's sound-off key has never
worked in this port.**

Found while preparing the widening the owner has now authorised, by asking which bytes the replay
digest was not watching. `Universe::soundDisabled` and `SoundBuffer::soundOff` are both `DNOIZ`.
**The original has one**: `DK4` writes it — `CPX #&02 / BNE DK6 / STX DNOIZ`, the key code itself —
and `NOISE` reads it, `LDA DNOIZ / BNE SOUR1`. This port had the pause screen write one and the
sound system read the other, so `Universe::soundDisabled` was **written twice and read nowhere**,
and pressing "2" on the pause screen left every sound playing.

**IT IS THE DUPLICATE `QQ12` AGAIN, and it was found the same way.** The replay slice found
`FlightPort` holding a second `dockedFlag`; this is a second `DNOIZ`, and both surfaced from the
same question — which bytes does the digest see? `UniverseImage` has had a `DNOIZ` cell since
M3-b-2a and it is bound to `sound.soundOff`, the byte the sound system reads, so the digest was
watching the half that worked. A byte written and never read cannot be caught by any comparison; it
can only be caught by looking.

`Universe::soundDisabled` is gone, `PressPauseKey` writes `_universe.sound.soundOff`, and
`PauseScreenTests` compares the byte `NOISE` reads against the oracle's `DNOIZ` as it always did —
which now means something. The seven bytes M3's follow-on moved into `Universe` are **six**.

`origin-markers` 4,107 → 4,106, one marker gone with the duplicate. 402 of 402, the replay digest
unchanged — the scripted flight presses no pause key, which is exactly why nothing caught this.
All 14 repository checks.

**2026-09-07 — M5-a-4: `SoundEffect`, and the sixteen sounds were declared in eight different
headers.**

M5-a asks for `SoundEffect` as a strong type, and building it found why it was worth asking. The
sixteen ids were `inline constexpr std::uint8_t` declarations spread over EIGHT headers — three in
`Combat.h`, five in `FlightLoop.h`, two in `Tactics.h`, one each in `Dashboard.h`, `Flight.h`,
`ViewChange.h` and `SoundEffects.h` — each sitting beside whichever routine first played it, while
`SOUND_EFFECT_COUNT = 16` sat on its own. **They are one table**: `sfxatk`, `sfxcnt`, `sfxvch`,
`sfxpr`, `sfxsus` and `sfxfrq` are six arrays indexed 0 to 15 by the same number. §6.121 said this
for the ship types — a number is a property of the table, not of the routine that first happened to
want one — and it is the same finding a second time.

**SLOT 8 IS NAMED FOR THE FIRST TIME.** The port had fifteen constants for sixteen entries and
nothing said which was missing. The original calls it `sfxeng` and its own comment says "This sound
is not used".

**TWO OF THE `SOUND_*` NAMES WERE NOT SOUNDS.** `SOUND_OFF_KEY = 0x02` and `SOUND_ON_KEY = 0x33`
are C64 key codes that the pause screen tests for; they are `KEY_SOUND_OFF` and `KEY_SOUND_ON` now.
A prefix that means two different things is the kind of thing a strong type makes impossible to keep.

**AND THE `+ 128` IS AN ENUMERATOR RATHER THAN A CAST.** `HYPNOISE` plays `sfxhyp1` pitched, then
`sfxwhosh`, then `LDY #sfxhyp1+128` — the one place in the game that sets bit 7. It is not a
seventeenth sound: it is sound 7 with an index that falls PAST the end of `SFXPR`, so the priority
byte reads as zero and the routine looks for a voice already playing it rather than taking a new
one. It was `static_cast<std::uint8_t>(SOUND_HYPERSPACE + 128u)` at the call site and is
`SoundEffect::HyperspaceAgain = 135` in the enum, because the trick is the point.

`origin-markers` 4,105 → 4,107 for the two slots the port had never named. 402 of 402, the replay
digest unchanged, all 14 repository checks.

**2026-09-07 — M5-a-3: `out-params` at ZERO, which is M5-a's acceptance, and the last four split two
ways.**

**Two were state and took the universe.** `RunSpawning`'s `_explosionCount` and `ArriveAtSystem`'s
are both `EV`, and both are `Universe::explosions` at every call site — so the universe comes in and
the reference goes, along with the commander, the current system and the generator, which are the
same fields at every site too. `RunSpawning` goes from nine parameters to two.

**Two were NOT state, and returned instead.** `CharacterPrinter::PadToWidth`'s `_rotor` is `SC+1`,
the rotating bit that carries from one gap to the next within a line, and `TypeDigit`'s `_value` is
`R`, the digits accumulating inside one prompt. Neither is a byte anybody keeps, so neither belongs
in `Universe`; they go back as `PadResult` and `TypedDigit`, which is M2-b's rule for the arithmetic
kernel applied to the two places P10 had left. **That distinction is the whole of why P10 is a
pattern worth counting**: a `std::uint8_t&` says nothing about whether the byte is the game's or the
call's, and every one of the sixteen turned out to be one or the other.

**Two fixtures converged and one of them caught a real difference.** `GameLoopTests` built nine
separate objects for the spawner and `HyperspaceTests` four for the arrival; both now seed one
`Elite::Universe` and name the pieces they set. The spawner's blueprint pointer is the one worth
recording: it was `const Blueprint*& _blueprint`, which the routine WRITES, and taking it as a
snapshot rather than a reference made the comparison read the pointer that went in — `XX0: expected
58635 actual 55491` on the first run. The fixture holds a reference into the universe now, which is
what the app has.

**P10 is closed.** 402 of 402, the replay digest unchanged, all 14 repository checks.

**2026-09-07 — M5-a-2: the six stragglers take `Universe&`, which is M3-a's pattern finishing three
phases late.**

M3-a gave every routine `(Universe&, Ports&)`. Six were missed, and the tell is that each was handed
a `std::uint8_t&` to a universe field by EVERY caller while not taking the universe:
`CrosshairsToCurrentSystem` (`QQ9`, `QQ10`), `EnterDockingBay` (`QQ12`), `SetMissileTarget` and
`AbortMissileLock` (`MSAR`, and `MSTG` and the canvas beside it), `NoteMusicSwitch` and
`PressPauseKey` (`MUTOKOLD`, `DNOIZ`). **`out-params` 12 → 4.**

**ONLY THE OUT-PARAMETERS FOLDED, and the value inputs stayed.** `EnterDockingBay` still takes
`_view` and `_countdown` by value, because `DockAtStation` passes its own `_view` at one site and
`0` at another; `SetMissileTarget` still takes `_missiles`, `_target` and `_colour`, because
`DashboardTests` sweeps them. Folding an input that varies by caller would have changed which byte
the routine reads, which is a behaviour change dressed as a tidy-up — the discipline is that a
parameter goes only when it is provably the same field at every call site.

**Four fixtures converged again, and one of them was drawing into the wrong canvas.**
`DashboardTests` built an `Elite::Canvas`, an `Elite::Bubble` and a loose `seeking` byte;
`PauseScreenTests` two loose bytes; `StartUpTests` a `Commander` with two crosshairs, and a
`portDocked`. All five now seed an `Elite::Universe`, which is what the app hands the routine. The
canvas is the one worth naming: the fixture filled ITS canvas from the oracle and compared ITS
canvas back, and once the routine drew into the universe's the comparison was looking at a screen
nothing had written — `ABORT2(target 0, colour 183): screen differs at offset 10185`. The suite said
so on the first run, which is the third time in two days that a fixture holding its own copy of a
byte the app keeps in the universe has been found by a failure rather than by reading.

402 of 402, the replay digest unchanged, all 14 repository checks.

**2026-09-07 — M5-a-1: four out-parameters were a field of the `Universe` the routine already
took, and one of them was bound to a copy.**

M5-a's acceptance is `out-params` at zero, and the first four are the easiest kind: `ResetGame`,
`Launch`, `AbandonShip` and `DockAtStation` all take `(Universe&, Ports&)` since M3-a and were ALSO
handed a `std::uint8_t&` to one of that universe's own bytes — `dockedFlag` three times and
`commander.fuel` once. That is §4.4's rule broken in the signature: the byte is game state, the
universe holds it, and a second name for it is the duplicate-`QQ12` defect the replay slice found in
`FlightPort`, one level up. **`out-params` 16 → 12.**

**AND ONE OF THE FOUR WAS BOUND TO A COPY.** `Ghy` calls `Launch(_universe, _ports, _jump.docked,
…)`, and `JumpState::docked` is a `std::uint8_t` VALUE that `JumpOf()` fills from the universe — so
`Launch`'s `_docked = 0u` wrote a temporary the caller discarded. Checked rather than assumed: it is
not a defect, because `RequestHyperspace` returns `JumpOutcome::Docked` before it can return
`Galactic`, so that path is only ever reached with the flag already clear and the write was a no-op
whether or not it landed. **It is still the exact failure mode P10 exists to remove** — a reference
parameter silently bound to something that is not the state — demonstrated rather than argued, and
the suite and the digest both say nothing moved.

**Four fixtures were carrying their own `QQ12` and one its own `QQ14`**, and all five are the
universe's now: `LaunchTests` had a `docked`/`flag`/`inFlight` local per case and two helpers taking
it as a parameter, `FlightLoopTests` had one, `DockingTests` two, and `HyperspaceTests` a `fuel`
local that meant `AbandonShip` wrote the local while `CompareState` compared an untouched
`commander.fuel`. Each is the same convergence the replay slice made, and each was found by the
suite failing rather than by reading.

402 of 402, the replay digest unchanged, all 14 repository checks.

**2026-09-07 — M4-d: `Game::Mode` is built, and `LoopOutcome` is NOT retired — the row asked for
three things and the tree already had two of them.**

**`Mode` IS REAL AND IT IS NOT INVENTED.** `FRCE` is `LDA QQ12 / BEQ P%+5 / JMP MLOOP / JMP TT100`,
a two-way dispatch on a byte the game keeps, so two of `Mode`'s three values are the game's own; the
third is `Paused`, which the port adds because `FREEZE` is a loop that does not return and a
windowed program cannot stop pumping messages (ADR-007 §3 already records that byte as the port's).
What it buys is an ORDERING: a frozen game is frozen in both halves, so the pause test has to come
above the `QQ12` test, and `Main.cpp` was keeping that rule by hand with a paragraph explaining it.
One value cannot be got wrong. `main-lines` 256 → **253**, which is the rule leaving the executable.

**`LoopOutcome` STAYS, and the row's premise does not survive contact.** The plan says M4-d retires
it into the mode machine. It cannot: `LoopOutcome::Continued` is not a mode — it is "the frame
finished, go round again" — and the other three are TRANSITIONS rather than states. It is the return
value of `BeginFlightFrame`, `MoveEveryShip`, `EndFlightFrame` and `MainFlightLoop`, which have to
say "I left early and by which of `DOENTRY`, `DEATH` and `ESCAPE`"; a mode cannot carry that,
because by the time the mode has changed the routine has already returned. `Mode` and `LoopOutcome`
answer different questions and both are needed. ADR-007 §2 is amended, since it is where the claim
was written down.

**The other two items were already built or are deliberately not done.** The mission sub-machine is
`Game::MissionOf(DockingOutcome) -> ForcedKey`, built in M3-c: `DOENTRY`'s six exits are the
briefings and the seventh is `BAY`. The death sequence as an explicit state is NOT built, and the
reason is this row's own acceptance: `DEATH` runs sixty-four iterations of the flight loop to fly
the wreckage past, and making that a state the outer loop pumps would change the pacing — the
acceptance says "replay hashes unchanged", and a pacing change is exactly what would move them. It
stays a synchronous sequence, which is what the original does.

**M4 IS COMPLETE.** `origin-markers` 4,098 → 4,101 for `Mode`'s three values. 402 of 402, the replay
digest unchanged, all 14 repository checks.

**2026-09-07 — M4-c-3: `MLOOP`'s spawner in four parts, and the carry is why it needed a frame.**

`RunSpawning` was 430 lines over `MLOOP`'s parts 1 to 4 with FOURTEEN `return;` statements, every
one of them the same `JMP MLOOPS` — "this pass of the spawner is over". `SpawnPass` has two values
and the parts are `SpawnTraderOrLoner` (1 and 2, which are one function because the same roll
chooses between them and `.whips` is the tail both reach), `SpawnPolice` (3) and `SpawnEncounter`
(4), over a `SpawnFrame`. `RunSpawning` is 24 lines.

**THE CARRY IS WHY THERE IS A FRAME AT ALL, and it is §6.125 running the length of the routine.**
Every `CMP` overwrites the generator's own flag and the next `DORND` rotates in what the compare
left, so ONE boolean is live across all four parts and thirty-odd statements. Passing it between
four functions would be four more `bool _carryIn` parameters — P11's pattern, which M2-d spent a
slice removing from the headers and whose ratchet sits at thirty — so it travels in the frame as
one named field instead. That is the same trade `TacticFrame` makes for `CNT` and `ShipRender` for
`XX4`, and it is the third time the answer has been "a frame, because a value is live across a
boundary the original has and the port did not".

**And `Ze` is called twice.** The port had one `RngResult ze` for both calls because it was one
function; part 3's roll is dead by the time part 4's runs — every read of it is behind the `fothg`
branch, which returns — so part 4 declares its own. One variable standing for two different rolls
is the kind of thing only a split can show.

**M4-c is complete.** `origin-markers` 4,088 → 4,098, the sixth and last of M4's rises: ten labels
where the parts split and on `SpawnPass`'s two values. 402 of 402 on the first run, the replay
digest unchanged, all 14 repository checks.

**2026-09-07 — M4-c-2: `DOCKIT`'s answer was a phantom, and the mechanical edit that removed it
broke the game in a way the oracle caught on the first run.**

`RunDockingComputer` returned "did the player survive" and the answer was ALWAYS yes. Checked
against the original rather than inferred from the port: `DOCKIT`'s every exit is an `RTS`,
`JMP GOPL` or `JMP TA151`, and it reaches no `OOPS` and no `DEATH` — so there is no path on which it
could say no. §4.6's "`OOPS`'s three paths become `Decision::Fatal`" belongs to `TACTICS`, where
`OOPS` genuinely is reached, and not here. Every caller discarded the byte with a `(void)` or a
`static_cast<void>` except one line in `TacticsTests`, which asserted `IsTrue(...)` — **a tautology
dressed as a check**, and it is a plain call now with the reason beside it. The routine is `void`.

**AND THE FIRST ATTEMPT AT IT WAS WRONG, which is worth recording rather than quietly fixing.** The
edit blanked every `return true;` in the function. Eight of the nine were EARLY EXITS and only the
last was the tail, so the change turned eight guards into fall-throughs. The run said so
immediately and said it three ways: `DOCKIT: straight in front of the slot (theirs) no faces:
INWK+29 — expected 131 actual 0`, a `TACTICS` case one byte out, and the M0-c replay diverging from
step 342. Three instruments, one cause, no ambiguity. The lesson is the one M2-c learned about
scripted deletions and `check_outpost.py` grew two halves for: **a mechanical edit over a control
statement has to distinguish the tail from the guards**, and here the suite is what distinguished
them.

Nothing else moved: 402 of 402, the replay digest unchanged, all 14 repository checks, and no count
touched.

**2026-09-07 — M4-c-1: `TACTICS` answers a `Tactic`, and the `bool` it returned was three things
wearing one costume.**

`RunTactics` was 583 lines over seven annotated parts and returned "did the player survive". That
boolean conflated THREE outcomes: a ship finished with for this frame (`TA22`'s `RTS`), the player
killed by `OOPS` reaching `DEATH`, and `TN2`'s `JMP DOCKIT` handing the ship to a different routine
altogether — the third hidden as a tail call whose boolean was passed straight through. `Tactic` has
four values (`Steer`, `Done`, `Fatal`, `Docking`), the parts are `DecideMissile`, `DecideStation`,
`DecideDisposition`, `DecideCombat` and `SteerTowardsTarget` over a `TacticFrame`, and `RunTactics`
is 48 lines that performs what they answer. **`JMP DOCKIT` is a call at the top level now**, which
is the one place `TACTICS` hands a ship to another routine and it was the least visible line in the
function.

**PARTS 4, 5 AND 6 ARE ONE FUNCTION, and the reason is the same one that kept the roll and the pitch
together in M4-a-3.** `fightsOn` is `TA7`'s first `BCC TA3` jumping clean over part 5 and
`fellFromFleeTest` is the carry `CMP #230` leaves for the `DORND` inside it; both are live from part
4 into part 5, so a split between them would need two parameters to say what two locals already say.
§6.85's rule, applied a second time and named as such.

**§4.6's four names are five, and one of them does not map.** The row asks for `DecideMissile`,
`DecideStation`, `DecideEscorts` and `DecideCombat`. `DecideEscorts` has nowhere to go — part 2 IS
the station launching escorts, so it is `DecideStation` — and parts 3 and 7 had no name at all.
Recorded rather than forced.

**Seven mutants re-anchored, none dropped (rule 3).** `ta-240`, `ta-half`, `ta7-three`, `ta3-ecm`,
`msl-82`, `msl-kill` and `kill-rotate` all name lines whose `_universe`/`work` became
`_frame.universe`/`_frame.work`; each was re-anchored to the same expression in its new stage and
`mutate.py --check` is back at 72 of 72. The tally is the next run's to report, because `mutate.py`
builds HEAD.

**Counts.** `origin-markers` 4,072 → 4,088, the fifth rise: sixteen labels where the parts split and
on `Tactic`'s four values, each of which is a 6502 label — `TA22`, `DEATH`, `DOCKIT`, `TA4`. The
census names `DecideDisposition` and `DecideMissile` as the writers of `K3` where it said
`RunTactics`. 402 of 402 on the first run, TACTICS still 7,326 cases with 22 fatal, all 14 checks.

**2026-09-07 — M4-b: `LL9` as its seven part blocks, and the census can name a part for the first
time.**

`DrawShip` was 551 lines over seven annotated part blocks, taking thirteen arguments and carrying
four locals across them under comment rules. The rules are function boundaries: `TestPresence`,
`MeasureRange`, `ScaleShip`, `SelectFaces`, `ProjectVertices`, `OpenHeapRun` and `PushEdges`, over a
`ShipRender` frame, with `DrawShip` itself 38 lines of pipeline. §4.6 said "six stages" and listed
seven; seven is what the tree has and M4-a-3 corrected the row.

**THE FRAME IS FILE-LOCAL AND THAT IS THE POINT, not a way round P5.** `aggregate-refs` counts
reference members of the argument-list structs in the HEADERS, because those are signatures threaded
through the whole library — the thing M3-a spent a phase removing. `ShipRender` is the opposite: it
exists so that seven stages in ONE translation unit stop passing thirteen arguments each, and no
caller outside this file can see it. Said here rather than left to look like ratchet-dodging.

**AND THE FOUR ZERO-PAGE BYTES ARE NOT IN IT.** `XX2`, `XX3`, `XX12` and `XX16` stay in
`GeometryWorkspace`, because two of them have a reader OUTSIDE the routine — `DOEXP` copies the
projected vertices off `XX3` to build the cloud (§4.3) — so they are the universe's state and not a
frame's. What the frame holds is only what the parts carry between themselves: `XX4`, `XX18`, the
heap run and `U`.

**THE CENSUS CAN NAME A PART NOW, and that is the finding worth keeping.** `channel_census.py`
listed `DrawShip` as the writer of `xx2`, `xx3` and `xx12` and as a reader of `q`, because one
function was the whole answer. It now says `ScaleShip` and `SelectFaces` write `xx2`;
`MeasureRange` and `ProjectVertices` write `xx3` and `OpenHeapRun` reads it; `ProjectVertices` is
the `q` writer the altitude check inherits from. **It also raises three inherited inputs that were
invisible while everything was one function**: `PushEdges` reads `xx3` after `EitherFaceVisible`,
and both `ProjectVertices` and `SelectFaces` read `xx12` after a writer the tool cannot name. Each
is real — they are the stage results parts 4 to 11 hand each other — and each is now a row rather
than a property of a 551-line function nobody could see inside.

**Two vestigial locals came out.** `V(1 0)` was assigned zero in parts 4–5 and again in parts 6–8
and read in neither: the blueprint carries the faces and the vertices as spans, so the pointer
set-up IS the loop index. Both assignments are gone and the comment says why, which is the same
correction M1-e made to `V` in parts 10–11 and did not finish.

**Counts.** `origin-markers` 4,053 → 4,072, the fourth rise: nineteen labels where the code splits.
Nothing else moved. 402 of 402 on the first run, the replay digest unchanged, all 14 repository
checks.

**2026-09-07 — M4-a-3: the frame's head and tail split at their annotated parts, and the port had
been collapsing a branch the original does not.**

`BeginFlightFrame` was 324 lines and `EndFlightFrame` 254, each with the original's part numbers
written into the comments as rules across the page. The rules are function boundaries now:
`StirTheFrame`, `TurnTheShip`, `RunFlightKeys` and `FireTheGuns` for parts 1, 2, 3 and 3's tail;
`BurnEnergyBomb`, `RechargeBanks`, `MaybeSpawnStation` and `RunCycleStep` for parts 13's two halves,
14 and 15. The two entry points are 14 and 43 lines. **The roll and the pitch stay in ONE function
on purpose** — the roll's exit carry is the pitch's `ADC #4` input (§6.85), a flag live across what
looks like a boundary, and splitting there would need a carry parameter to say what a local already
says.

**AND THE SPLIT FOUND A BRANCH THE PORT WAS NOT TAKING.** Part 14 ends `LDA MCNT / AND #31 /
BNE MA93`, and `MA93` is part 15's first compare — so on a frame where `MCNT` is 0 mod 8 but not 0
mod 32, the original runs part 13's tail and then falls into part 15's three tests. The port
returned the frame tail on BOTH sides of that branch. It is observationally identical, and the
argument is arithmetic: a step that is 0 mod 8 is never 10, 15 or 20 mod 32, so the three jobs could
not have fired. **Nothing had written that argument down**, and the comment above part 15 asserted
the opposite — "`MA93` is entered from part 14 as well ... whichever way in it came" — which is
false of this port and true of the original. The fall-through is restored, so the shape is the
original's again and the argument is not needed.

**THE CYCLE IS THIRTY-TWO STEPS AND NOT SIXTEEN.** The M4-a row asked for "the sixteen-step cycle as
a table", §2's decode table said "the sixteen-step housekeeping cycle", and part 15's own heading
said "one job every sixteen frames". All three are wrong: `MCNT` is masked with 31, the three jobs
fire at steps 10, 15 and 20 of a thirty-two step block, and the shields and banks fire once per
eight. Sixteen appears nowhere in the original. `RunCycleStep` carries the table, all three places
are corrected, and `MCNT`'s DECREMENT is recorded beside it — the residues are visited backwards and
all thirty-two are still visited once per block.

**Counts.** `origin-markers` 4,042 → 4,053, the third rise and the last of M4-a's: eleven labels on
the new function boundaries, which is `MA18`, `MA77`, `MA22`, `MA93`, `MA23S`, `MA68` and the rest
written where the code splits rather than inside a comment rule. The channel census moves one
reader with the code — `MathWorkspace.q`'s is `RunCycleStep` rather than `EndFlightFrame`, which is
the same read in a smaller function — and P8's paragraph in §3 records what M4-a did to the three
frame procedures it names. 402 of 402, all 14 repository checks, the replay digest unchanged.

**2026-09-07 — M4-a-2: parts 7 to 12 as typed stages, and five booleans that were a state machine
nobody had written down.**

`MoveEveryShip` was four hundred and twenty-six lines with parts 5 and 7 to 12 spelled out inside
its `for(;;)`, and the parts talked to each other through FIVE BOOLEANS declared above the block
that set them and read two parts later: `docking`, `scoopable`, `collision`, `holdFull`, `drawIt`.
Each part is a function now and each answers a named type — `Contact`, `ScoopResult`,
`DockingTest`, `Impact`, `Aim`, `KillOutcome`, beside the `LaserHit` part 11 already had — and the
loop body is a pipeline of seven calls in ninety lines.

**AND THE TYPES SAID SOMETHING THE BOOLEANS COULD NOT.** Part 7 sets exactly one of `docking`,
`scoopable` and `collision` — a station takes the first branch and nothing else can reach the other
two — so the three lines at the end of part 9 that cleared `collision`, `scoopable` and `holdFull`
were DEAD STORES on every path that reached them. They read as defensive and they were unreachable,
and only an enumeration can say that: `Contact` has four values because the original has four
answers, and there is no state in which two of them hold. Nothing else changed; the suite is 402 of
402 on the first run and the replay digest did not move, which is the acceptance the M4-a row asks
for.

**THE STAGES ARE FILE-LOCAL, and that is the M4 pattern rather than a shortcut.** `ApplyLaserHit`
has been file-local since it was extracted and `LaserHit` with it; what M4-a buys is the SHAPE of
the routine, not a wider header. Exposing seven more names would put them through rule 6 into
`Outpost/` for nothing — the app calls `MainFlightLoop` and always has.

**Counts.** `origin-markers` 4,026 → 4,042, and this is the second rise in a row: sixteen
enumerators and struct fields, every one of them a 6502 label the code previously expressed only in
its shape — `MA65`, `ISDK`, `MA58`, `MA59`, `MA67`, `GOIN`, `KS1`, `MA27`, `MA15`. **M4's pattern
moves P12 UP and that is expected**, which is worth saying plainly rather than discovering at the
ratchet a third time: naming a label in a type is still naming a label, rule 4 wants it labelled
until M6-e, and M6-e strips the lot in one pass. Nothing else moved.

**2026-09-07 — M4-a-1: `SpawnChildEffects` goes, and the seam that outlived a phase was being
kept alive by a TRAP rather than by the code.**

M3-b-1's rule was §6.73's: a seam scoped before the routine behind it existed goes when the routine
exists. `SFS1` has been `Elite::SpawnChildShip` in `Spawn.cpp` since slice 4a-b, and both
implementations of `SpawnChild` — the app's and the replay port's — were ONE LINE calling it over a
universe the object already held. It should have gone with the other seven. The M3-b row says why it
did not and the reason is in the suite: `TheWreckageMatchesSPINAndSPIN2` traps `SFS1` on the oracle
to `SEC` and let the port's seam answer `true`, so both machines were told the bubble always had
room. Call the routine for real and the bubble fills at ten slots, the carry flips, and the two are
no longer comparing the same thing.

**SO THE SEAM WAS A SEAM BECAUSE OF WHAT A SUITE COUNTED, which is §6.73's corollary read from the
other end.** The fix is M4-a's pattern rather than M3-b's: `PlanItems` and `PlanDebris` ANSWER an
`Elite::Drop` — a type, a count, an AI byte, and the carry an empty drop hands back — and
`PerformDrop` runs the loop. `SPIN2` decides a count and nothing else, which is the finding rather
than a shape imposed on it: every subtlety in that routine's twelve-line comment is about a LOOP,
and the loop is now the caller's.

**AND THE TRAP CAME OFF WHERE A BUBBLE EXISTS.** The SPIN sweep keeps its trap and compares the
ANSWER against the oracle's call sequence, which needs no fixture bubble at all. The FRAME fixtures
untrap `SFS1` entirely: both machines really spawn, and what is compared is the slot list, the ship
blocks, the whole line heap, `SLSP` and `RAND` — `CompareState` and the walks that were already
there. That is strictly more than the seam could say. `NWSHP`'s allocation was compared NOWHERE on
this path before, because the recorder answered `childSucceeds` and the trap answered `SEC`: a frame
that filled the bubble was indistinguishable from one that did not.

**Three fixtures were carrying the seam without using it**, which is what a `Ports` member costs
even when nobody reaches it: `TacticsTests::CountingEffects` collected a `spawned` list nothing
asserted on (the AI reaches `SFS1` through `SpawnEscapePod` and `SpawnShipAhead`, which are calls
and not seams since 4a-b); `LaunchTests::RecordingLaunch` was a whole class answering "there was
room" for a launch that spawns no child; and `NullSeams::spawnRoom` was a boolean two fixtures set
to true so that a kill could drop debris — the answer comes from the slot list now.

**Counts.** `effects-seams` 9 → 8, `aggregate-refs` 11 → 10, `outpost-elite-names` 71 → 69.
`origin-markers` 4,022 → 4,026, and it ROSE on purpose: `Drop`'s four fields are X, `CNT`, A and
`oh`'s carry, four 6502 things the seam's one method named in a sentence, and rule 4 wants each of
them labelled. **The replay digest did not move** — the port's `SpawnChild` and `PerformDrop` make
the same call in the same order, which is what a refactor is supposed to look like from the outside.
402 of 402 on the first run, all 14 repository checks.

**One documentation defect found on the way out.** The M3-c acceptance row carried `<!--count:-->`
markers on `main-lines` and `outpost-elite-names`. An acceptance records what a slice ACHIEVED and a
`count:` marker asserts what the tree holds TODAY, so the marker drags the history wrong at the next
slice that moves the number — and this was that slice. Both are plain numbers now; §3's are the live
ones. The M3-b row above it had it right already.

**2026-09-07 — the replay drives `Elite::Game`, the record is re-taken, and yesterday's
measurement of it was wrong.**

**THE CORRECTION FIRST.** The entry below says pointing `FlightPort` at an `Elite::Game` turns
"1,170 steps ending `Docked`" into "2,589 ending `Died`" and attributes it to the two causes that
bisection separated. **It does neither.** The flight is 1,170 steps and it ends `Docked`, exactly as
recorded; every digest moves and not one step does. There was a THIRD cause the bisection did not
separate out because I did not know the byte existed, and with it fixed the length and the outcome
come back. The two causes below are real and the record moves for them; the length and the death
were the third, and putting a wrong number in front of the owner as the reason to park a decision is
the mistake worth recording, not the byte.

**THE THIRD CAUSE IS A DUPLICATE `QQ12`.** `FlightPort` held `std::uint8_t docked = 0xFF` beside
`Universe::dockedFlag`, and both are the same 6502 byte. Nothing noticed while the port stepped its
own transcription: `Launch` took the port's by reference and cleared it, the transcription never
wrote either, and `dockedFlag` sat at whatever `Universe` starts with. Step it through `Game` and
the two diverge on the first pass — `Game::Leave` writes the universe's byte and `Game::Docked`
reads it, while the replay's own loop tests the port's — so the flight neither stopped when it
docked nor knew it had. It is a reference now (`std::uint8_t& docked = universe.dockedFlag`) rather
than a rename, because `RESET` and `LAUN` take it by reference and the image names it.

**AND THE OTHER TWO ARE THE FIXTURE BECOMING WHAT THE APP IS.** `FlightPort` built `Ports` itself
out of `universe.printer`, `universe.characters` and `universe.extendedPrinter` with the value-token
and control-code seams left null, so the scripted flight deferred eleven control codes the app runs;
and it never ran `NA%`, so the flight has been flown by a commander of all zeros — no fuel, no
laser, a galaxy seed of zero. `Elite::Game`'s constructor does both, because the app's does. Both
are rule 1's second case, the record follows the fix, and this entry names them.

**WHAT THE RECORD MEASURES NOW IS THE LOOP AND NOT A TRANSCRIPTION OF IT.** `FlightPort::Step` was
`M%`, `MLOOP`'s head, the spawner, part 5's tail and the keyboard scan spelled out a second time —
M3-c moved the executable's copy into `Elite::Game::Step` and left this one, and ADR-007 §5 recorded
the gap. It is `game.Step(0u)` now, one call, and the zero is a key: `TT102` dispatches every pass,
which is how `TT107`'s countdown ticks when nothing is pressed (§6.159), and the old transcription
did not dispatch it. `Game::LastOutcome` is added for it — the replay compares outcomes digest for
digest and wants `M%`'s answer rather than the boolean `Step` takes from it (`origin-markers`
4,021 → 4,022, rule 4). `FlightPort::Ports()` hands back `game.PortsOf()`, so `RESET`, `LAUN` and
`DOCKIT` reach the struct the game steps through instead of a second one built beside it.

**WHAT IS STILL NOT MEASURED.** The seven bytes that moved into `Universe` yesterday have no cells
in `UniverseImage`, so the digest still does not see them, and `Game::m_paused` is outside the
universe by design. Closing the first is rule 1's FIRST case — a deliberate widening — and it would
move all sixteen checkpoints a second time; it is one edit and it is not this slice's. ADR-007 §5
says so.

402 of 402; all 14 repository checks; the record re-taken, 16 of 16 checkpoints moved, 0 steps.

**2026-09-07 — the ADR-007 §3 follow-on: seven bytes go where they belong, and the replay slice
that was to come first turns out to be a decision rather than a tidy-up.**

**THE SEVEN BYTES ARE IN `Universe`.** `crosshairStep`, `jumpTarget`, `jumpDistance`,
`joystickGeometry`, `joystickEnabled`, `musicSwitchWas` and `soundDisabled` — `TT17`'s X and Y,
`safehouse`, `QQ8`, `JSTGY`, `JSTE`, `MUTOKOLD` and `DNOIZ`. Every one has a 6502 name, which makes
it game state, which §4.4's rule puts in the universe; they were on `Game` because M3-c carried them
across from `Main.cpp`'s composition struct and not because anything decided they belonged.
`Game::m_paused` stays: the original has no such byte, `FREEZE` is a loop that does not return, and
the state a windowed program is in instead is the port's own.

**AND MOVING THEM DID NOT MOVE THE DIGEST, which is the half of the finding worth keeping.** `Hash`
walks `UniverseImage`'s table of cells rather than the struct's bytes, so a field added to `Universe`
is not hashed until a cell names it. The gap ADR-007 §5 records is relocated rather than closed — but
it is one edit away from closing now instead of a refactor away, and that edit is a deliberate
widening under rule 1's first case which moves every checkpoint in the record.

**THE REPLAY SLICE WAS BUILT, MEASURED AND PARKED.** `FlightPort::Step` is a second transcription of
`M%`, `MLOOP`'s head, the spawner, part 5's tail and the keyboard scan, sitting beside
`Elite::Game::Step`'s — M3-c deleted the executable's copy and left this one, so the replay digest
measures a TRANSCRIPTION of the loop rather than the loop. Pointing `FlightPort` at an `Elite::Game`
compiles and runs. It also changes the flight:

  - ~~**1,170 steps ending `Docked` becomes 2,589 ending `Died`**, and every checkpoint differs.~~
    **THIS MEASUREMENT WAS WRONG. The next entry corrects it: the flight keeps its length and its
    ending, and only the digests move.** The 2,589-step death was a third cause this bisection did
    not separate out — `FlightPort` holding a second `QQ12` byte beside `universe.dockedFlag` — and
    it was read as an effect of the two below.
  - Bisecting the composition says why, in two independent parts. **Without `SetValueTokens` and
    `SetGame` the flight is 1,170 steps again** and only the digests differ — so the value tokens
    and the control codes running rather than deferring is what changes the LENGTH of the flight.
    **With `Game` present but its ports unused the record first differs at step 400** — so
    `Game`'s constructor writing `NA%` into the commander is a second, separate cause: the replay
    has been flying a commander of all zeros, with no fuel, no laser and a galaxy seed of zero.
    Both parts of the bisection stand; only the length and the outcome attributed to them do not.

Both are the fixture becoming what the app is, and both are defensible as rule 1's second case — a
defect in the port found and fixed. But the two together replace the project's primary regression
instrument with a different flight, and "the record moved and the flight is now a death" is not a
change to make on a slice's own judgement. **It is recorded here and put to the owner rather than
taken.** What is committed is the diagnosis, the `FlightPort::Ports()` accessor the change pivots on,
and this entry; `FlightPort` still steps its own transcription.

402 of 402, all 14 repository checks, the digest unchanged.

**2026-09-07 — M3-d: ADR-007, and writing it from what was built found three things the plan had
wrong and one the build has wrong.** The row asks for state ownership and the replay hash "written
from M3-a..c as built", and the value of that phrasing is that it forces a comparison. ADR-006 §4
planned `Elite::Game` with `Universe m_universe; Ports& m_ports;` and a `Step(const InputFrame&)`.
What exists is the ownership INVERTED — `Universe& m_universe; Ports m_ports;` — three `Step`s
rather than one, and no `Frame`, `Sounds`, `StateHash` or `Mode`. Each has a reason and ADR-007
records it; ADR-006 §4 is amended in place rather than left to disagree with the tree.

**THE THREE `Step`s ARE THE ORIGINAL'S SHAPE AND NOT A COMPROMISE.** `FRCE` is
`LDA QQ12 / BEQ P%+5 / JMP MLOOP / JMP TT100` — a jump to one of two entry points — and `FREEZE` is
a third that `DK4` reaches and does not return from. One method behind a mode byte would be
inventing the machine `Mode` is, which is M4-d's when it retires `LoopOutcome`. Until then the
caller's choice IS the game's.

**AND THE BUILD HAS EIGHT BYTES IN THE WRONG PLACE, which is the finding this ADR exists to
produce.** `Game` holds `crosshairStep`, `jumpTarget`, `jumpDistance`, `joystickGeometry`,
`joystickEnabled`, `musicSwitchWas`, `soundDisabled` and `paused`. **Seven of the eight have a 6502
name** — `TT17`'s X and Y, `safehouse`, `QQ8`, `JSTGY`, `JSTE`, `MUTOKOLD`, `DNOIZ` — which by §4.4's
own rule makes them game state, which puts them in `Universe`. They are not there because they were
loose in `Main.cpp`'s composition struct when M3-c moved the dispatch and carrying them across was
the smallest change that compiled. The consequence is exact and is now written down: they are not
cells in `UniverseImage`, so a slice that broke `MUTOKOLD` or `QQ8` would not move the replay
digest. Moving them in and giving them cells is a deliberate widening under rule 1's first case.
`paused` is the one that stays: the original has no such byte, it FREEZES in a loop, and a windowed
program cannot.

**THE DIGEST DID NOT MOVE ACROSS THE PHASE, and that is the sentence the whole apparatus exists to
be able to write.** It moved twice, both in M3-b, both for defects the slice found — `NOISE2`
answering the sustain it went in with rather than the flag byte it wrote (2a), and `Music.cpp`
missing the `SETL1` brackets around `stopat` and `BDENTRY` (2b) — each with the defect named, which
is rule 1's second case. From M3-b-3a to M3-c inclusive it has not moved: eleven commits, eleven
interfaces collapsed to four ports and five direct calls, eight hundred lines of dispatch out of the
executable, `Main.cpp` from 1,197 lines to 256. That is "no behavioural change" measured rather than
asserted, and it is the one thing the per-routine oracle cannot say (risk R14).

**WHAT THE DIGEST DOES NOT COVER IS NAMED RATHER THAN ASSUMED**, because a hash everybody trusts and
nobody has bounded is worse than no hash: the eight bytes above; the docked half, which is
`DockedSessionTests`' transcript and a different instrument; `Ports` and the text objects, which are
not state; and — the one that matters most — **the replay does not drive `Game`**. `FlightReplayTests`
composes `FlightPort` and calls the library's routines directly, as it has since M0-c, so the digest
is a statement about the ROUTINES and not about the object that dispatches them. M3-c's row asked
for the replay and `DockedSessionTests` to drive `Game::Step` and neither does; `GameTests` is what
covers the object until that changes.

**M3 IS COMPLETE.** Five slices over two days: M3-0 (the app's member check, which grew to seven
halves under seven Windows-only breaks), M3-a (`Universe`), M3-b (the ports), M3-c (`Game`) and this.
`aggregate-refs` 78 → 11, `effects-seams` 22 → 9, `outpost-elite-names` 205 → 71, `main-lines`
1,219 → 256. The M3-b row expected `effects-seams` at six and it closes at nine, which is two seams
the row did not know were the text system's own and is recorded as a correction to §4.5 rather than
as a shortfall. Next is M4-a, which is also what unblocks `SpawnChildEffects`.

**2026-09-06 — M3-c: the top of the program moves into the library, and `Main.cpp` goes from 1,160
lines to 256.** `Outpost/Main.cpp` held the universe, the text system, the ports, eight bytes of
game state that are in no struct, and every dispatch the main loop makes — `TT102`'s twenty-odd
actions, `M%`'s outcomes, `DOENTRY`'s six mission exits, `FREEZE`, the chart draw, the docked pass
and the flight pass. Eight hundred lines of 6502 in the ONE FILE NO LINUX RUNNER COMPILES, which is
the whole of R15's surface and the reason `check_outpost.py` has grown seven halves this week.
`Elite::Game` is where all of it lives now. `main-lines` 1,160 → 256, `outpost-elite-names` 157 →
71 (205 when M3 opened).

**THE LINE THE SPLIT FALLS ON IS THE DETERMINISM GUARD'S, and that is the finding.** The plan's row
says `Advance` and `AdvancePaused` move here. `AdvancePaused` did. `Advance` COULD NOT: it turns
elapsed seconds into a count of passes, over ADR-005 §3's accumulator, and `GameLogic` may not
touch a float (AGENTS.md §5, `check_gamelogic.py`). So it SPLIT — `PlanSteps` and the seconds stay
in the executable and `Game::Step` takes one key and runs one pass. That is §2.1's
`Step(InputFrame)` arrived at from the other direction, and it is a better shape than the row
described: the count of passes is a property of the display and the passes are the game's.

**`Step` ANSWERS A BOOLEAN, which is the `return` the old loop used.** `Advance`'s body returned out
of the whole batch when `M%` left the flight half or the pause key froze it; a `Step` that could not
say so would let the caller run a docked pass through the flight loop. It is the one piece of
control flow that had to become a value.

**EVERY PLATFORM REACH IN THE MOVED CODE HAD AN EXACT PORT EQUIVALENT, and that is M3-b's dividend
rather than a coincidence.** `_game.shell.ClearToView(v)` is `SetUpScreen(universe, ports, v)`;
`_game.shell.Flush()` is `ports.keyboard.Flush()`; `_game.shell.View()` is `universe.view`;
`_game.audio.Direct()` is `ports.sid`; and `_game.window.Held(KEY_CONTROL)` — the galactic drive's
modifier, read live — is `ports.keyboard.Held`, which is the port M3-b-3d landed four commits ago.
Nine reaches, nine one-for-one replacements, no new seam.

**WHAT §2.1 ASKED FOR AND THIS DOES NOT HAVE, said plainly.** `Frame()`, `Sounds()` and
`StateHash()` are not built: the first two are the executable's draw and drain, which have no caller
in the library yet, and the third wants `UniverseImage`'s hash, which lives in the test tree.
`DockedSessionTests` still owns its own composition rather than driving `Game` — it asserts on a
CHARACTER STREAM and `Game` builds its own text chain down to the canvas, so pointing it at `Game`
would cost the transcript that suite is made of. `GameTests` is the new suite instead: four methods
that drive the object the way `Run` drives it — the cold start ends docked with a rolled market, the
pause key freezes and the resume key thaws, a keyless docked pass still ticks `TT107`'s countdown,
and sixty-four docked passes followed by flight passes end in `M%` answering `Docked` and `Leave`
performing the arrival. That last one is the real one: it runs `DOENTRY`, the missions and the
status screen, so a `Game` whose dispatch were unwired would still be flying at the end of it.

**AND `Elite::Universe` IS STILL THE COMPOSITION ROOT'S, which §2.1 does not ask for.**
`Outpost::FlightSession` binds the universe at construction and answers three of the seams `Game`'s
`Ports` needs, so whichever of the two is built first needs the other, and the only ways out are a
deferred binding on the app side or the reference this took. Two of those three seams are already
scheduled to go — `ShipDrawEffects` when the emulator models the banking §6.108 found,
`SpawnChildEffects` in M4-a — and what is left of that class afterwards is `DOCKIT`. When it goes,
`m_universe` becomes a member and the composition root stops holding any game state at all. Named
here so the next person does not have to re-derive why it is a reference.

Two smaller things went with the slice: `GameShell::AttachExtended` and `m_extendedPrinter`, which
existed for a null check that never dereferenced the pointer; and `Main.cpp`'s `Game` struct, which
is `App` now and holds seven members where it held twenty.

402 of 402, all 14 repository checks, `origin-markers` 3,925 → 4,021 — UP by ninety-six, the
direction rule 5 allows before M6, because that is the dispatch's own markers arriving with it
(rule 4).

**2026-09-06 — M3-b-4c: the null port was already one class, and the second one was a boolean.**
The plan's row asks for `NullShell`, `LoopRecording`, `RecordingSight`, `RecordingView` and
`RecordingDashboard` to become one null port. THREE OF THE FIVE HAD ALREADY GONE with the seams they
answered — `SightEffects` in M3-b-3a, `ViewEffects` and `DashboardEffects` in M3-b-2 — and of the two
left, one is not a null port and the other was not two things.

`LoopRecording` was named for a recording it had stopped making: two empty draw methods and a
`SpawnChild` that returned true where `NullSeams` returns false. ONE BOOLEAN, and it is `spawnRoom`
on the null port now — so §4.5's "one class" is reached by deleting the other rather than by merging
them. `LoopUniverse` loses a member and two suites lose a name.

`NullShell` is a RECORDER and stays one. `NullSeams.h` has said since M3-a-3 that it is not a
recorder and must not become one, and folding a transcript into it would be exactly that: a fixture
that wants to know a seam was reached passes something that counts, and this exists so the seams a
routine cannot reach cost a fixture nothing to declare. The distinction is the whole value of the
class and the plan's row would have destroyed it.

**M3-b IS COMPLETE.** Three of §4.5's four ports landed — `SoundSink` (2b), `Presenter` (3b, 3c) and
`Keyboard` (3d) — and the fourth keeps the name it has, for the reason §4.5 now records. Eleven
interfaces became four ports and five seams that turned out to be `GameLogic` reached through the
executable: `TradeScreenEffects`, `TunnelEffects`, `ControlEffects::ScanKeyboard`, `TextEffects` and
`ControlCodes`. `effects-seams` 22 → 9 across the phase, `aggregate-refs` 39 → 11,
`outpost-elite-names` 205 → 157, `main-lines` 1,219 → 1,160.

Two seams survive the phase deliberately and are named so nobody has to re-derive it: `TextSink` and
`ValueTokens` are the text system's own polymorphism and are argued about in §4.5. Two survive
because a comparison is blocked rather than because they are right: `ShipDrawEffects` needs the
banking §6.108 found, and `SpawnChildEffects` is M4-a's typed stage result. `StartUpEffects` is down
to `ClearKeyLogger` and `ShowTitleScreen`, and `ControlEffects` to `RunDockingComputer`.

**2026-09-06 — M3-b-4b: `ControlCodes` goes, and what the seam stood in front of was `GameLogic`
reaching `GameLogic` through the executable.** `DT3` and its `JMTB` table are the control codes that
leave the text system, and `GameShell::Run` answered three of them before forwarding the rest to
`Elite::MissionCodes`. All three were the library's: codes 8 and 9 are stores into `Universe::text`
plus `SetUpScreen`, and code 21 is `CLYNS`, which is `Elite::ClearMessageRows` and has been since
slice 2a. So `MissionCodes` becomes `Elite::RunControlCode(Universe&, Ports&, code)`, absorbs 21,
and the extended printer reaches it directly. §6.73 for the twelfth time.

**THE GALAXY WAS A PARAMETER THAT WAS A UNIVERSE BYTE.** `MissionCodes` took `const std::uint8_t&
_galaxy` and every caller bound `commander.galaxyNumber` to it — the executable through
`AttachGalaxy`, the suites through a local. It reads the commander now, which is what M3-a removed
nine of, and `out-params` fell with it.

**THE SEAM WAS HOLDING FIVE OF THE SHELL'S POINTERS UP.** `GameShell` carried the token printer, the
cursor, the sentence flags, the message counters and `GCNT` for one method, and `Attach` and
`AttachGalaxy` existed to set them. `RunControlCode` reaches all five through `(Universe&, Ports&)`,
so both methods and all five members go and `Main.cpp` loses two lines of wiring.
`outpost-elite-names` 165 → 157: eight names the app reached ONLY to answer codes.

**`SetGame` IS A SETTER AND HAS TO BE.** `ExtendedTokenPrinter` is a member of `Ports` and
`RunControlCode` takes a `Ports&`, so neither can be the other's constructor argument.
`TokenPrinter::SetValueTokens` unties the same knot for the same reason and `Main.cpp`'s composition
already lends the struct back to two of the objects inside it. A printer with no game ignores the
codes that leave, which is exactly what a null `ControlCodes*` meant — so the token suites are
unaffected by construction rather than by luck.

**AND A COUNTER REPLACES THE RECORDER, WHICH IS THE ONE THING §6.73's COROLLARY COULD NOT COVER.**
Three suites needed to know that a token had reached a code the port defers: `CompareToken` must
SKIP such a token, because it cannot be compared against a game that runs it, and no state
comparison can tell "deferred" from "ran and did nothing". So `ExtendedTokenPrinter` counts the codes
that leave — a `std::uint32_t`, not a virtual — and `CodesThatLeft()` is what the suites ask.
`GalaxyTests` still asserts that none of 2,048 generated descriptions reaches one; `ExtendedTokenTests`
still asserts exactly which fourteen of the thirty-one leave.

**WHAT THIS DOES NOT DO, named rather than left to be discovered.** The eleven codes
`ExtendedTokenTests` defers are still deferred. Five of them — 9, 21, 25, 27 and 28 — could be
compared there against a real universe and are not, because doing it means giving that sweep a
`Ports` and a canvas comparison, which is a second pattern and belongs in its own slice. Three more
(22, 24, 26) need a scripted keyboard on both sides first. Three (11, 30, 31) have nothing behind
them on either side and fall to `default` in `RunControlCode` exactly as they fell to the shell's.

**Two suites now run the codes for real.** `MissionTests` reaches them through
`Universe::RunCodesThrough`, and `DockedSessionTests` — the fixture that most nearly IS the
executable — calls `SetGame` in its constructor. `LaunchTests` lost its `codes.ran.empty()`
assertion and gained the attachment instead: the oracle has always RUN `TITLE`'s codes, so what that
assertion really claimed is that the two screens agree, which `CompareScreens` says directly and for
the right reason. Falsified before it was believed: stubbing `SetGame` out fails four methods across
`PAUSE`, `BRIS`, `MT9` and `MT27`.

`effects-seams` 10 → 9, `outpost-elite-names` 165 → 157, `main-lines` 1,161 → 1,160, `out-params`
17 → 16, and `origin-markers` 3,923 → 3,925 — UP, the direction rule 5 allows before M6, because
the shell's markers moved into `GameLogic` with the dispatch (rule 4). `mi-captain` and
`mi-mt9-view` re-anchored on the free function.

**AND `ExtendedTokenPrinter::RunControlCode` HAD TO BE RENAMED, which is worth one line.** The
printer already had a private member of that name — the text system's own half of `DT3` — and a
member hides a namespace-scope function of the same name at unqualified lookup, so the first build
of this slice reported four calls to a member "expecting 1 argument, 3 provided". It is `RunTextCode`
now, and the pair of names says which half is which.

**2026-09-06 — M3-b-4a: `TextEffects` goes, and the seam was answering with the wrong routine.**
`clss` is what `CHPR` does with a character printed below the last row, and this port had it as a
one-method seam whose header said it was "the one thing CHPR does that the library still cannot do
for itself" — because `TT66` reaches the dashboard, the sprites, the border and the colour bands.
**That is the wrong routine.** `clss` is `JSR TT66simp / LDA K3 / JMP RRafter`, and `TT66simp` is a
bitmap wipe of character rows 1 to 23 followed by `INY / STY XC / STY YC`: the cursor home to
(1, 1), with row 0 and the dashboard left alone. `Elite::ClearTextArea` has been that routine, ported
and compared against the shipped one, since slice 2a. So the seam was in front of a routine the
library already had — §6.73 for the eleventh time — and the executable was answering it with
`GameShell::ClearScreen`, which is `ClearToView`, which is the whole of `TT66`: a palette fill, the
dashboard, the sprites, the border and a `QQ11` write, none of which `clss` performs.

**AND THE NULLABILITY WAS HIDING A SECOND DEFECT, which is §6.149's shape for the third time in this
milestone.** `TextPrinter` took the seam as a pointer, and a printer built without one did not clear
AND did not print the character — the overrunning glyph was simply dropped. That was every
`TextPrinter` in the suite. `ClearTextArea` needs only the canvas and the `TextState` the printer
already holds, so there is no pointer to be null: the branch is unconditional now.

**NOTHING IN THE SUITE REACHED THE BRANCH, and that is why neither defect had been found.**
`PrintableCharactersMatchTheShippedRoutine` sweeps rows 0, 1, 11 and 23 — the row after the last one
is exactly the row it stops before. `TheOffTheBottomPathMatchesClss` is the new method: rows 24, 25
and 30, three columns, three characters, both screens SEEDED WITH INK first, compared on the whole
screen plus `XC`, `YC` and the returned character. The seeding is the point — over two blank screens
the comparison would pass whether the routine cleared everything, nothing, or exactly the right
band, and those are the three answers being told apart. Falsified twice before it was believed:
dropping the character reports `screen differs at offset 352 -- game has 0, port has 163`, and
clearing row 0 as well reports `offset 32 -- game has 227, port has 0`.

**Two findings about what is left of M3-b-4, because the plan's row is wrong about both.**

  - **`SaveStore` IS ALREADY TAKEN.** §4.5 asks for `CommanderStore` to be renamed to `SaveStore`,
    and `Outpost::SaveStore` has been the executable's implementation of it since slice 2d — so the
    rename produces `class SaveStore : public Elite::SaveStore`. The name has to move on one side or
    the other, and `CommanderStore` says what is stored where `SaveStore` says only that saving
    happens. Recorded here rather than decided unilaterally.
  - **`TextSink` IS NOT A PLATFORM SEAM AND ITS REASON HAS NOT EXPIRED.** The plan groups it with
    `ValueTokens` and `ControlCodes` as "the text system's own polymorphism", which is right, and
    then concludes it should go "since `CHPR` … exist[s]", which does not follow: `TextSink` is not
    in front of `CHPR`, it is the interface `CHPR` implements. Both production implementations are
    in `GameLogic` and form a chain — `TokenPrinter` → `CharacterPrinter` → `TextPrinter` → `Canvas`
    — and seventeen fixtures use it to compare token expansion as a CHARACTER STREAM against the
    shipped routine, which is a strictly more precise instrument than the pixel comparison that
    would replace it. §6.73's corollary cuts the other way here for the first time.

`effects-seams` 11 → 10, `outpost-elite-names` 166 → 165, `origin-markers` 3,924 → 3,923, tests
397 → 398.

**2026-09-06 — M3-b-3d's fix: a member reached through an expression, and a seventh half for
`check_outpost.py`.** `Main.cpp` called `_game.shell.FlushKeyboard()` in the market-price case, and
the method is `Keyboard::Flush` since the slice that renamed it. Six halves of the check passed the
file: `MarketPrice` reads through TWO hops and `APP_ACCESS` sees `_game.shell` and
`shell.FlushKeyboard` as unrelated pairs, neither of which resolves — `shell` is not a variable that
file declares. `Main.cpp(401,19): error C2039` from the Windows job and from nothing else, which is
R15 for the sixth time.

**So the check walks the chain now, and the walk is the point rather than the pattern.** `_game` is
declared `Game& _game`; `Game::shell` is a `GameShell`; `GameShell` is a type whose members are
known. Three lookups, each of which may fail — and a failure ends the walk rather than reporting
one, so `_game.flight.Loop().options` is checked as far as `flight` and no further. Two things had
to change to make it reach anything: the member map gains each member's own TYPE, and it reads
`Outpost/*.cpp` as well as the headers, because `Main.cpp`'s composition struct is declared in an
anonymous namespace in the file that walks it. 292 chains resolved on the tree as it stands, where
the flat member pass resolves 227 single hops.

**The self-test's plant is a chain whose FIRST hop is good.** A check that reported the whole
expression whenever any part of it failed to resolve would report every call in `Main.cpp`; one
that stopped at the first hop would never reach the bad one. So the plant is `_it.held.kept` beside
`_it.held.gone` and the test requires that exactly the second be named. `CanvasPresenter.cpp`'s two
`view`s are declined here as they are by the flat pass — one scope per file cannot tell an
`Outpost::Viewport` from a `D3D12_SHADER_RESOURCE_VIEW_DESC`.

**Six Windows-only breaks, six halves, and the pattern in them is worth stating.** `check_members`
after M3-a-2, `check_initialisers` after M3-a-3, `check_braces` after M3-b-2a, the app-type pass
after M3-b-3a, `check_switch_scopes` after M3-b-3c and this after M3-b-3d. Every one was found by
MSVC and every one was cheap to catch once it had been seen; none of them was predicted. That is
what R15's mitigation actually looks like — not a check written in advance, but a check written the
same afternoon as the break, which is only affordable because the slices are small.

Mutants re-run on the committed tree: 72, 68 caught, 4 survived, the four recorded equivalents.
Rule 3 met for M3-b-3d.

**2026-09-06 — M3-b-3d: `Keyboard` lands, and `RDKEY` turns out to be one line of platform under
fifty of game.** The third of §4.5's four ports replaces `KeySource`, `LineEntryEffects` and
`StartUpEffects::ScanTitleKeys`, and it does NOT have the `Scan(KeyLogger&)` the table asked for.
The routine walks `&DC00`/`&DC01` eight columns at a time, and everything around that walk had
already stopped being the platform's: the `SETL1` bracket is `MemoryMap` since M3-b-3a, the sprite
mask is a `VideoState` write, `ZEKTRAN` is sixty-five bytes of `Universe`, the countdown that leaves
`thiskey` holding the LOWEST-numbered held key is arithmetic, and the `QQ11` tail is the piece this
port's own comment has called "the one piece of `RDKEY` that is game logic" since slice 3d. So the
port answers `Held(key)` and `Elite::ScanKeyboard` is the rest — compared, ratcheted and mutated
like anything else in the library.

**IT WAS TRANSCRIBED THREE TIMES AND IS NOW WRITTEN ONCE.** `FlightSession::ScanMatrix` in the app,
`FlightPort::ScanKeyboard` in the tests and `GameShell`'s title-screen path were three copies of the
same fifty lines, each with its own `NON_STEERING_KEYS` array and its own `RDKEY_SPRITE_MASK`, and
two of them also carried ADR-005 §4's chart-view steering rule — which is the port's own and not
`RDKEY`'s, and so exactly the kind of rule that drifts when it lives in three places. Two copies are
deleted and the third is the library's.

**AND THE SECOND SEAM FOR THE SAME ROUTINE GOES WITH IT.** `StartUpEffects::ScanTitleKeys` existed
because the title screen must PRESENT between two drawn frames where a flight loop must not, and the
argument recorded beside it was that the difference is in the platform AROUND the scan rather than
in the scan. That was true and is answered by `Presenter::HoldTitleFrame`, which M3-b-3c's shape
made available: the hold happens at the call site and the scan is one routine again. `TITLE`'s loop,
`PAS1` and `PAUSE2` all now read `HoldTitleFrame` then `ScanKeyboard`, which is the order the 6510
had for free.

**`ReadNumber` AND `ReadLine` LOST A PARAMETER EACH, AND `ReadFlightControls` LOST FOUR.** The line
editor took a `LineEntryEffects&` for one method; `DOKEY` took the five bytes it reads and a seam,
and takes `(Universe&, Ports&, ControlEffects&)` — which is M3-a's shape, arrived at here because
`ScanKeyboard` needs `video`, `memoryMap`, `keys` and `QQ11` and they are all in the universe.
`ControlEffects` is down to `RunDockingComputer`.

**WHAT THE SUITE STILL DOES NOT COMPARE, said plainly.** `Elite::ScanKeyboard` has no oracle test.
The shipped `RDKEY` selects a matrix column by writing `&DC00` and reads the answer from `&DC01`,
and `Cpu6502`'s memory is FLAT — one byte at `&DC01` whatever was written — so an oracle running the
real routine would read the same column eight times and compare nothing. Comparing it needs CIA
1 modelled in the emulator, which is the same shape of blocker `ShipDrawEffects` waits on (§6.108's
banking) and belongs in the same slice. Until then both sides of `DOKEY` are stubbed at `RDKEY` and
the port's stub is a `Keyboard` that holds down the four steering keys the case names, so the logger
`ScanKeyboard` rebuilds is the one the test seeded. That is a comparison of `DOKEY`, which is what
it always was, and not of the walk.

**Three fixtures learned that a seam is what a suite COUNTS (§6.73's corollary, for the tenth
time).** `MissionTests` scripted `TitleKey{pressed, key}` and now scripts which key is DOWN, with
the walk turning that into the carry and `thiskey`; `LaunchTests` the same, `KY7` included so the
title loop still takes `BMI TL3`; `NameEntryTests` compared the ORDER `DELAY` and `FLKB` are reached
in, so the keyboard writes into the presenter's log rather than keeping one of its own. `PortsWith`
gained a fifth argument for the fixtures that reach the keyboard, and the four-argument form fills
it with nothing — which is what made `TheBriefingShipMatchesPAS1` fail loudly and the two `PAUSE`
tests HANG rather than fail, the first sign of a fixture whose keyboard was answering the wrong
object.

397 of 397 green. `aggregate-refs` 12 → 11, `effects-seams` 12 → 11, `outpost-elite-names` 174 →
166, and `origin-markers` 3,910 → 3,924 -- UP, which is the one direction rule 5 allows before M6:
`Outpost/FlightSession.cpp` lost nine markers and `GameLogic/Controls.cpp` gained them, because they
came with the routine (rule 4). `mi-pause2-loop` is re-anchored on the call it now names.

**2026-09-06 — M3-b-3c's fix: the braces a scripted deletion took were the ones holding a `case`
label off a declaration, and a sixth half of `check_outpost.py` is what says so now.** Removing
`DeathPacing` took the block that had enclosed it, and the `switch` in `Perform` was left with
`const Elite::ForcedKey begun = Elite::StartGame(...)` bare in its body. A `switch` body is ONE
scope, so the two labels below that line jump past an initialisation and the file does not compile:
`Main.cpp(794,5): error C2360: initialization of 'begun' is skipped by 'case' label`, twice, from
the Windows job and nothing else. The scope is restored — the case is braced, as every other case
in that switch that declares anything already was.

**IT IS THE FIFTH TIME A SCRIPTED DELETION IN `Outpost/` HAS BROKEN ONLY THE WINDOWS LEG, AND THE
FIRST THAT `check_braces` SHOULD HAVE CAUGHT AND COULD NOT.** The braces balanced. That check
counts delimiters and by construction cannot tell one nesting from another, so a deletion that
removes a matched PAIR is exactly the shape it is blind to — which makes this the second lesson of
the same afternoon: a cheap half of a parse catches the mistakes it was written for and no others.

So `check_switch_scopes` reads each `switch (...) { ... }` body, tracks depth over both kinds of
bracket, and fails a declaration-with-initialiser at depth zero when a `case` or `default` label
follows it there. It is deliberately narrow: a braced case may declare what it likes, and so may
the last case of a switch, because neither is a shape a compiler objects to. The self-test plants
all three — a legal braced declaration, the illegal bare one, and a legal bare one below the final
label — and requires that only the middle be reported, so a check that simply hated declarations in
switches would fail its own test. Reverting the braces on the tree reproduces the Windows error
here, in a tenth of a second, which is the point.

`main-lines` stays at 1,161 and the ratchet is why the entry is worth reading: bracing the case is
two lines, and paying for them meant rewrapping the comment above it to the width the rest of the
file already uses. A ceiling with zero slack makes every fix name its own cost.

**2026-09-06 — M3-b-3c: one seam was carrying three answers, and the executable was choosing between
them.** `TunnelEffects::ShowFrame` is `Presenter`'s now, and it did not survive the move as one
method. The interface had exactly one, threaded as a NULLABLE POINTER through eleven routines, and
what the pointer selected was not an implementation but a POLICY:

  - `GameShell::ShowFrame` was `DELAY` with a count of one -- the vertical sync the launch and
    hyperspace tunnels ask for, because the original spells `JSR DELAY` inside `LL164` and `HFL2`.
  - `Main.cpp`'s own `DeathPacing::ShowFrame` was a frame held for as long as the NEXT took to
    compute, which is what `DEATH`'s `.D2 JSR M% / DEC LASCT / BNE D2` gets from a machine that
    waits for nothing. Paced by vertical sync instead, sixty-four frames go past in a second and
    read as a glitch -- §6.149's bug, found once and prevented here by a second signature.
  - A null pointer was NO present at all, which `PlanetDraw.h` documented as the oracle's: the 6502
    has none either, and the two sides must agree on pixels rather than on time.

So `Presenter` gains `Present()` and `HoldFlightFrame(ships)`, the third is a presenter that does
nothing, and `Main.cpp`'s pacing struct goes. `Die` walks `FRIN` for the ship count itself, which is
where that walk belonged: the cost of a frame depends on how full the bubble is and the wreckage
empties it, so the rate rises through the sequence exactly as the original's did.
`effects-seams` 13 → 12, `main-lines` 1,197 → 1,161, `outpost-elite-names` 175 → 174.

**AND A NULL WAS BEING PASSED WHERE THE GAME HAS AN INSTRUCTION.** `Main.cpp` handed `PerformJump`
and the `Launch` after it a null pacing on the IN-FLIGHT hyperspace jump -- the one the countdown
expires into -- where the docked chart's jump passed the shell. `TT18` is one routine whichever door
it is reached through, and the `JSR DELAY` is inside `LL164`, not at the call site; so the in-flight
jump drew its tunnel with no frame shown and cut straight to the arrival. It is a defect of the same
shape as §6.149 and the seam's NULLABILITY is what allowed it: a port that must name an object at
every call site can drop a present, and one that gets it from `Ports` cannot. Nothing in the suite
covers it -- it is the app's, and R15's -- so the evidence is the shipped source rather than a
comparison, and this note is the record.

**`TheLaunchPacing` was written to catch exactly this and now cannot fail.** The test exists because
someone replaced one of `TT110`'s two pacing arguments with `nullptr`, changed no pixel, failed no
assertion, and put half of §6.109 back. Its assertion survives -- sixty-eight circles across the two
tunnels -- but what it guards is now guarded by the type: there is no argument to get wrong.

**2026-09-06 — M3-b-3a's fix: `Main.cpp` read an accessor the slice had deleted, and the fifth half
of `check_outpost.py` is the one that would have said so.** `Outpost::FlightSession::Video()` handed
the presenter the sprite registers to composite from; it sat inside the block of `SightEffects` and
`ExplosionEffects` overrides, and the script that cut that block took it too. MSVC said
`Main.cpp(109): error C2039: 'Video': is not a member of 'Outpost::FlightSession'` and nothing else.
The accessor is not restored: `Universe::video` is where the registers live since ADR-005 §1, so the
composition root hands the presenter that, and the flight session has one fewer thing to own.

**IT IS THE FOURTH TIME A SCRIPTED DELETION IN `Outpost/` HAS BROKEN ONLY THE WINDOWS LEG**, and the
first three each added a half to this check -- `check_members` after M3-a-2, `check_initialisers`
after M3-a-3, `check_braces` after M3-b-2a. `check_members` resolved `name.member` against the type
of `name` for `Elite::`-typed variables ONLY, and `flight` is an `Outpost::FlightSession`. So the
check ran, resolved 124 accesses, and could not see the one that mattered. It runs twice now, over
both namespaces, and the self-test's second plant is that access restored.

**Two things the app-type pass has to decline, and it declines both.** `CanvasPresenter.cpp`
declares two different `view`s -- an `Outpost::Viewport` and a `D3D12_SHADER_RESOURCE_VIEW_DESC` --
and this check keeps one scope per file, so a name also declared with a type it cannot parse is not
checked at all. The Direct3D headers are not read and never will be; what the check knows is what
`Outpost/*.h` declares, and everything else is somebody else's compiler's business.

**2026-09-06 — M3-b-3b: `Presenter` arrives, and four of the five seams it was supposed to collapse
turned out to be forwarding calls.** §4.5 lists `TunnelEffects::ShowFrame`,
`LineEntryEffects::WaitFrames`, `StartUpEffects::WaitFrames` and `TradeScreenEffects::ClearToView` as
`Presenter`'s. Reading the executable's implementations first is what changed the slice: three of
`TradeScreenEffects`' four methods called library routines and nothing else -- `ClearToView` was
`Elite::SetUpScreen`, `ClearBottomRows` was `Elite::ClearMessageRows`, `SetUpTradeScreen` was those
two calls one after the other -- and `BeepAndPause` was `Elite::Beep` plus the one thing that is not
the library's. So `TradeScreenEffects` and `ChartEffects` GO, and what `Presenter` carries out of
this slice is `DELAY` alone. `effects-seams` 14 → 13, `outpost-elite-names` 177 → 175,
`aggregate-refs` twelve before and twelve after.

**WHY `DELAY` IS A PORT WHEN THE OTHER FOUR WERE NOT, and a test that would hang says so.** `DELAY`
is `LDY #n / JSR WSCAN / DEY / BNE`, and `WSCAN` waits for the raster to reach the bottom of the
screen. There is no way to wait for a vertical sync that does not know what a screen is -- and the
oracle cannot run it either: a flat-memory interpreter never reaches that raster line, so every
suite that drives a docked screen traps `DELAY` and would spin for ever without the trap. That is
the sharpest statement of the difference between a seam and a call that this phase has produced.

**`ClearBottomRows` WAS DECLARED TWICE, WHICH IS WHAT §6.59's MISTAKE LOOKS LIKE FROM INSIDE.**
`TradeScreenEffects` and `ChartEffects` each had it, because two slices needed `CLYNS` and neither
could call it. `Elite::ClearMessageRows` has been that routine since slice 1d.

**Five suites stopped counting seams and started comparing screens.** `MarketScreenTests` compared
the port's list of `TRADEMODE`/`CLYNS`/`TT66`/`dn2` calls against the oracle's trap hits, four
sweeps of it; `SystemScreenTests` asserted the one `TRADEMODE` and its view number; `ChartTests`
counted `CLYNS`; `GameLoopTests` folded a `CLYNS` tally into its coverage key; `DockedSessionTests`
answered all four from its null shell. `TRADEMODE`, `CLYNS` and `TT66` are trapped nowhere now, so
what those sweeps compare is the character stream and the text state the routines produce. `dn2`
splits: `JSR BEEP` runs on both machines and `JMP DELAY` cannot, so the oracle keeps that one trap
and the port's `Presenter::WaitFrames` is counted against it.

**One assertion was replaced by a weaker one and it is worth naming.** `SystemScreenTests` asserted
that the data screen reached exactly one seam and that its argument was the view number. What it
asserts now is `QQ11` -- the byte `TRADEMODE`'s `STA QQ11` leaves -- which cannot tell one call from
two. The character stream is what carries the rest, and it is compared in full.

**2026-09-06 — M3-b-3a: `SETL1` is not what six slices of this port believed it was.** `SightEffects`
and `ExplosionEffects` go, and with them the last write-only seams over the VIC-II. Five of their six
methods were already one line into `ApplySightColour`, `ApplySpritesEnabled`, `ApplyMaskSprites`,
`ApplySpriteExpansion` and `ApplyExplosionSprite` -- ADR-005 §1 decided that in slice 3d and the
implementations have been forwarding ever since. `effects-seams` 16 → 14, `aggregate-refs` 13 → 12,
`outpost-elite-names` 180 → 177, `main-lines` 1,198 → 1,197, `register-params` 14 → 13.

**THE SIXTH IS WHY THE OTHER FIVE WAITED, AND THE REASON WAS A MISREADING.** The conversion plan's
3d row says in as many words: "`SETL1` is NOT one of them: it is self-modifying code inside a raster
interrupt handler and belongs behind a seam like the sound." `Controls.h` repeated it, `VideoState.h`
gave it as the reason the byte could never live there, and `TrumbleTests` explained a trap with it.
The routine is eight instructions -- `SEI / STA L1M / LDA l1 / AND #%11111000 / ORA L1M / STA l1 /
CLI / RTS` -- and none of them writes code. `l1` is &0001, the 6510's own port register; the
`SEI`/`CLI` is there so an interrupt cannot be taken with the memory map half-written, and that
bracket is the whole of what the "interrupt handler" reading was built on. `Trumbles.h` had it right
("memory banking rather than a register") and was overruled by the other three.

**So it is two bytes, and `MemoryMap.h` is where they and the reading now live.** The same argument
as `SoundBuffer` in M3-b-2a and `MusicPlayer` in M3-b-2b: the game writes them and what a port does
about the banking is the port's business. It is not part of `VideoState`, and the shipped source is
what says so -- the fourteen callers bracket the sprite registers, the SID (`startbd`, `stopat`), the
keyboard's CIA (`RDKEY`, `DKSANYKEY`) and the KERNAL's disk routines (`SVE`, `LOD`). One register,
four chips. The executable had been keeping it as `FlightSession::m_rasterMode`, a byte nothing read.

**A PORT DEFECT CAME OUT OF IT, AND THE SEAM IS WHY IT WAS INVISIBLE.** `april16` brackets `BDENTRY`
with `SETL1` and `stopat` brackets its twenty-five SID writes with it, because the chip has to be
banked in before it can be written. `Music.cpp` had said so in its comments since slice 5b and had
never made the calls -- there was nowhere to make them to. Both are there now, and `SoundTests`
compares `l1` across `startbd` and `stopbd` over all thirty-two flag combinations.

**FOUR SUITES COMPARE THE BRACKET AND THE OTHERS CANNOT, WHICH IS §6.108 AGAIN.** `SETL1` is trapped
nowhere now; `ControlsTests` (`SIGHT`), `TrumbleTests` (`MVTRIBS`), `ExplosionTests` (`PTCLS2`) and
`SoundTests` run it on both machines and compare the byte. ONE BYTE STILL TELLS THE CASES APART,
which was the doubt worth checking before the assertions were rewritten: the bracket's two modes
DIFFER, so no call leaves the seed, one leaves %101 and both leave %100 -- and the fixtures seed the
top five bits non-zero, so a port that had lost the `AND #%11111000` would not agree by accident.
What a final byte cannot see is a doubled bracket, and the oracle's own store log pins the shipped
routines to exactly two. The whole-universe image MIRRORS `l1` and `L1M` and does not compare them,
because every fixture that changes a screen still traps `NOSPRITES` for §6.108's reason -- in flat
memory its `STA VIC+&15` lands on the blueprint pointer table -- so the game does not reach the
`SETL1` inside it and the port does.

**Two counts had to be earned back rather than excused.** `main-lines` and `origin-markers` both rose
on the first pass, and the way down was to stop saying the same thing three times: there is one
`SETL1` in the game and there are now one pair of constants for it, `MEMORY_MAP_IO` and
`MEMORY_MAP_RAM`, where `Trumbles.h`, `FlightLoop.cpp` and `FlightSession.cpp` each had their own.

**What a recorder could count and a byte cannot.** `ExplosionTests` counted the port's calls to
`ShowExplosionSprite` per cloud -- six vertices, some refused -- and only the LAST placement survives
in the registers, on the hardware as much as in `VideoState`. So the seam let the suite count
something no screen could show. The coverage counters read the oracle's own stores to VIC+&2 now, and
the two machines are compared on the state they end in.

**2026-09-06 — M3-b-2b: the music comes inside, and one of the two seams it removed was in front of
an `RTS`.** `FlightLoopEffects::Start/StopDockingMusic` were `JSR startbd` and `JSR stopbd`,
`StartUpEffects::Start/StopTheme` were `JSR startat` and `JSR stopat`, and `TextEffects::Beep` was
`R5`'s `JSR BEEP` -- five seams in front of routines `Music.cpp` and `SoundEffects.cpp` have had
since slice 5. `Universe` gains `MusicPlayer music` for the same reason it gained `SoundBuffer` in
2a: the player is memory the game writes and the interrupt reads. `Ports` gains `SidWriteLog& sid`,
which is §4.5's `SoundSink` and the first of the four to arrive -- the handful of writes the GAME
side makes between interrupts, which the executable applies ahead of the next tick. `effects-seams`
18 → 16, `outpost-elite-names` 184 → 180, `origin-markers` 3,915 → 3,911, `main-lines` 1,198 → 1,197,
and `aggregate-refs` thirteen before and thirteen after.

**THE PLAN PUT THIS SLICE BEFORE THE ONES THAT PAY FOR IT, AND THE RATCHET SAID SO.** Every M3-b
slice up to here only removed from `Ports`, so it sits on `aggregate-refs`'s ceiling of thirteen with
zero slack; adding the sink and removing nothing is fourteen, which rule 5 fails outright. Three ways
out were weighed and two rejected. Threading the sink as a parameter is M3-a's pattern run backwards.
Putting `SidWriteLog` in `Universe` is worse than it looks: the log is not state -- nothing in the
library reads it back, it is drained every frame, and the M0-c replay HASHES the universe, so a
growing write log would be in the digest. What is left is to make the slice pay, and §4.5's own
`SoundSink` row names the payer.

**`ViewEffects` WAS THAT PAYER, AND ITS LAST METHOD WAS IN FRONT OF NOTHING AT ALL.** After 2a took
`PlaySound` it held only `SetPalette`, which is `DOVDU19` -- and the C64 build assembles that label
and an `RTS` and nothing between them: `setvdu19-dovdu19.asm` guards the `STA VNT3+1` with
`IF _6502SP_VERSION OR _MASTER_VERSION`, and the call sites say "this doesn't actually do anything in
this version of Elite" in as many words. The port's header claimed it "writes a VIC-II colour
register" (the Master's reading); `Outpost`'s implementation was an empty function with the right
comment on it. So the seam was a claim that something is outside the library which was never true --
§6.73 arriving from the other side, where what was scoped too early was the READING and not the
routine. The two `JSR`s are comments now.

**FOUR ORACLE SUITES STOPPED COUNTING AND STARTED COMPARING.** `StartUpTests` trapped `ZEKTRAN`,
`startat`, `stopat` and `stopbd` on the 6502 and matched the four against the port's list; it traps
`ZEKTRAN` alone now, logs every store to `SID`..`SID+&18`, and compares the write sequence against
`Ports::sid` write for write -- order and all, because a new tune zeroes the chip and then sets four
registers, which a real SID hears as a gate falling and rising. `FlightLoopTests` counted `startbd`
and `stopbd` trap hits against two integers on its recorder; `LaunchTests` counted `RES2`'s single
`stopbd`; `DockedSessionTests` and `DockingTests` noted "music on"/"music off" in a transcript. All
of them are `MUPLA` and `MULIE` in `ImageCells` now, mirrored in and compared out with every other
byte, so the whole oracle suite carries the music rather than five suites carrying a tally of calls.
`LaunchTests`'s case is the sharpest: `RES2`'s `stopbd` writes NOTHING on either machine, because no
tune is playing and `stopat` returns on its first test -- an assertion about silence, which a count
of calls could never have made.

**The M0-c replay record moves, and it is rule 1's first case rather than its second.** `MUPLA` and
`MULIE` are two new cells in the universe image, so the digest is two bytes wider at step 0 --
before a frame has run. No comparison in the suite changed answer, the flight is still 1,170 steps
to `Docked`, and every oracle sweep is green.

**`TextPrinter` holds a `SoundBuffer*` beside its `TextEffects*` and that is a wart worth naming.**
`CHPR`'s bell needs the buffer and its screen clear still needs the seam, so the printer takes both;
the pointer is null in the tests that build a printer with no universe. It goes when M3-b-3 turns
`ClearScreen` into `Presenter` and the printer takes one object again.

**2026-09-06 — M3-b-2a's fix: one brace, sixteen errors, and a check that would have caught it.**
The script that deleted `FlightSession`'s three sound methods began its cut at the `/*` above them,
which belonged to `SyncVideoRegisters`'s BODY, and took the body and its closing brace with it. The
Linux leg cannot see that -- `Outpost/` compiles on the Windows job alone (R15) -- and the file
still parsed far enough for `check_outpost.py` to read its names, its arities, its members and its
initialisers and pass all four. MSVC reported sixteen `local function definitions are illegal` and
one `C1075: '{': no matching token found`, which is one missing brace wearing seventeen costumes.

**`outpost-elite-names` was 180 and the tree's real number is 184.** The four names the deleted body
used went with it, so the count the slice recorded was measured on a broken tree. The ceiling is
corrected upward to 184 rather than left at a figure no working tree can reach -- nothing that was
removed came back, a miscount is being undone -- and M3-b-2a's true saving is three names, not seven.

**`check_outpost.py` counts delimiters now.** It is the cheapest half of a parse and it is the
second time a scripted deletion has unbalanced an app file that only CI could see; `check_members`
and `check_initialisers` were each added after a Windows failure for the same reason. Strings,
character literals and comments come out first, because a brace inside any of them is not a brace --
and the self-test plants exactly that, a `"}"` and a `'{'` around a function whose closing brace is
missing, so a check that counted them would pass the planted file and fail the real ones.

**2026-09-06 — M3-b-2a: `DashboardEffects` goes, and the sound system was deciding a ship's energy
byte behind it.** `PlaySound` was `NOISE`, `PlaySoundPitched` was `NOISE2` and `StopSound` was
`NOISEOFF` -- three routines `SoundEffects.cpp` has had since slice 5a. `ViewEffects::PlaySound` was
the SAME routine declared a second time and went with them. `Universe` gains `SoundBuffer sound`,
which is where the seam's reason went: the SID is written from a raster interrupt and not from the
game, `NOISE` fills a buffer and `SOINT` drains it, and the port had nowhere to keep the buffer
between them. It is memory, not a port. `effects-seams` 19 → 18, `outpost-elite-names` 187 → 184,
`carry-params` 32 → 30.

**`.MA14 STA INWK+35` STORES WHAT `NOISE2` LEFT IN A, AND THAT IS NOT THE SUSTAIN.** The dead
ship's energy byte comes out of the sound system -- the port's own comment said so -- but a seam
method returns one thing and `DashboardEffects::PlaySoundPitched` returned the carry, so `RecordKill`
answered the sustain it went IN with. `NOISE`'s successful exit is `INY / TYA / ORA #128 /
STA SOFLG,X / CLI / SEC`: A is the flag byte it just wrote. The oracle said `game has 132, port has
243` on three ships at once the first time `NOISE2` ran on both sides. `PlaySoundEffect` and its
pitched twin answer a `NoiseResult` now -- the carry AND the accumulator -- and the two refusal
paths carry their own: the priority byte the failed `CMP` was made on, and `DNOIZ` with the sound
switched off.

**And `EXNO3`'s carry is `OOPS`'s, which is §6.87 a second time.** A missile that arrives runs
`JSR EXNO3 / LDA #250 / JMP OOPS`, and `LDA` touches no flag -- so `OOPS` subtracts on whatever
`NOISE` returned. The port passed false while the seam's answer was discarded. `FSH` was one point
out on a missile that touched us, which is what the oracle reported.

**Two carries came back that the plan had written off.** §5's table recorded `NOISE2`'s Trumble
squeak carry as "dropped at the seam ... M3-b's seams are where it would go", and this is where:
`LDY CABTMP / CPY #&E0 / BCC burnthebastards` is the flag, so a burning cabin squeaks with it set.
Auditing the other two `NOISE2` callers with it found both are always CLEAR and said why -- four
`ASL A` on an X the volume ladder leaves between 11 and 15 cannot carry out.

**The suites stopped counting `NOISE` and started comparing `sound_variables`.** `Where` gains the
ten runs plus `PULSEW` and `DNOIZ`, so `CompareState` carries the buffer for every fixture that uses
it and a new `CompareSound` does the same for the ones that mirror in and check a handful of things
out. It subsumes §6.118's documented gap rather than losing it: the old comparison could check the
carry going INTO four hand-picked effects and never the one coming out; `NOISE`'s answer now comes
from two buffers that agree byte for byte, and its consequence -- `LASLI` and `OUCH` open a `DORND`
on it -- lands in `RAND`, which was already compared. Nothing is excluded by name any more.

**The M0-c replay record moves, under rule 1's second case.** Two port defects found and fixed, the
same 1,170 steps and the same `Docked` outcome, every digest different.

**2026-09-06 — M3-b-1e: the reset half of `StartUpEffects` goes, and untrapping `RES2` found a
routine hidden behind a trap's address.** `ResetUniverse` was `RESET`, `ResetShip` was `RES2` and
`ResetMissileIndicators` was `msblob` — three routines `Flight.cpp` and `Dashboard.cpp` have had
since the stardust, the heaps and the dashboard were built. `msblob` was declared on TWO interfaces,
`StartUpEffects` and `TradeScreenEffects`, and both copies went; `DockAtStation` lost its
`StartUpEffects&` parameter with them and reaches `WaitFrames` through `Ports` like everything else.
`outpost-elite-names` 189 → 187.

**`RES2` OPENS `JSR stopbd`, AND `stopbd` FALLS INTO `stopat`.** `StartUpTests` traps `stopat` for
the title theme and the port records `StopTheme` against it. Untrap `RESET` and `RES2` and the
oracle's sequence grows two `stopat` hits the port cannot match — not because the port is missing a
stop, but because a trap catches an address rather than a routine, and `stopbd` is ten bytes in
front of `stopat` with no `RTS` between them. The fix says what is really there: `stopbd` gets its
own trap, and the port's `StopDockingMusic` — which `ResetShipAndBubble` has always called — is
recorded into the same list. A cold start now shows `stopbd` twice at the head of the sequence,
which is `TT170`'s fall-through into `DEATH2` reaching `RES2` a second time (§6.25) made visible.

**Three suites replaced a count with an observation.** `DockingTests` asserted the list `{RES2,
DELAY}`; `RES2` empties the bubble and stops the ship, so that is what it asserts now — the same
shape §6.109 gave `LAUN` when it stopped being a seam. `MarketScreenTests` and `StartUpTests`
counted `msblob`; it is the ONLY thing in either screen that touches the canvas (the text goes into
a recording sink and `CLYNS`, `TT66`, `dn2` and `TITLE` are still seams), so ink on the canvas is
the routine having run. Both were checked by deleting the call and watching the assertion come back.

**2026-09-06 — M3-b-1d: the spawn half of `FlightLoopEffects` goes, and "the bubble is full" stops
being a trap's answer.** `SpawnAhead` was `JSR FRS1` and `Anger` was `JSR ANGRY`; slice 4a-b built
both, in `Spawn.cpp` and `Tactics.cpp`, and the flight loop calls them. `outpost-elite-names`
191 → 189.

**`Anger` had already stopped being a seam without anybody removing it.** §6.157 made every
implementation forward to `Elite::Anger`, because a trap's exit carry is the caller's and part 11
falls from `ANGRY` into `JSR LL9` where that flag seeds an explosion cloud. So the interface was a
dispatch to one function from four places that all wrote the same line. Taking it out changed no
behaviour and removed the last place where an implementation could get it wrong.

**`SpawnAhead` was still real, and the test that counted it now fills the bubble instead.**
`FRS1` was trapped on the oracle with `TrapExit::SetCarry` or `ClearCarry` chosen by the case, and
answered on the port by a seam returning the same `bool` — both sides told what to say. With the
seam gone `NWSHP` decides, and the only way to make it refuse is to leave it no slot: `M% (fire with
a lock, bubble full)` now fills every one of the ten. That is a stronger case in two directions —
the refusal is the routine's rather than the fixture's, and the frame that follows runs a full
bubble.

**And the jam is OBSERVED rather than counted.** The old case could not stop being true; the new one
can, because a fixture that quietly stopped filling the bubble would spawn the missile and the loop
would still tick over. `FR1` gives up before `DEC NOMSL`, so the count still standing is what says
the rail kept it — checked by removing the fill and watching `expected 3 actual 2` come back.

**2026-09-06 — M3-b-1c: `ShipEffects` goes, and `MVEIT` becomes a routine that reaches all of
`Universe`.** One method — `RunTactics` — and it is the seam that stood longest for the plainest
reason: `MVEIT` was slice 3a's and `TACTICS` was phase 4's, so the AI had to be an interface for a
year. Slice 4a-c built it. `MoveShip` now takes `(Universe&, Ports&)` and calls `RunTactics`
directly; the six arguments it took — canvas, work block, `MathWorkspace`, `FlightState`,
blueprint and `QQ11` — were the same six members of the universe at every one of its five call
sites, so they went with the seam. `effects-seams` 20 → 19, `workspace-params` 46 → 45,
`aggregate-refs` 14 → 13 (`Ports` is thirteen references), `outpost-elite-names` 193 → 191.

**THE BOOL SURVIVES THE INTERFACE, and that is the part worth saying out loud.** §6.122 gave
`RunTactics` a `bool` because three of the AI's paths reach `OOPS`, `OOPS` ends `JMP DEATH`, and on
the 6502 that abandons the stack — `TACTICS`, `MVEIT` and the frame simply stop. It reads like a
seam's concession and it is not: the answer is threaded out through `MoveShip` to `MoveEveryShip`,
which turns it into `LoopOutcome::Died`, and every one of those hops is port code. The interface
went; the return type is unchanged.

**Four suites stopped counting and started comparing.** `FlightLoopTests`, `MissionTests`,
`LaunchTests` and `ShipMoveTests` each held a recorder whose `RunTactics` did nothing and answered
`true`, against an oracle with `TACTICS` trapped. All four traps are off. In `FlightLoopTests` that
is nearly free — `Seed` gives its fleet a random `INWK+32`, so half the bubble was already asking
for an AI that neither side ran, and the frame comparison covers what it does. `ShipMoveTests` is
where the work was.

**`ShipMoveTests` had no universe to run the AI in, and that is the seam's real cost.** It built an
`Elite::Ship`, a `MathWorkspace`, a `FlightState` and a `Canvas` — four objects, enough for the
arithmetic and nothing more — and asserted `item.tactics`, a per-case count of how often `MV26` was
reached: three times in twenty for a hostile ship, twenty for a missile. The AI reads the bubble,
the commander, the ECM countdown, the message line and the generator, and writes ships, sounds and
screen bytes, none of which four loose objects can put into the oracle. So the suite takes
`FlightUniverse.h` now — the shared fixture `ViewChangeTests` and `FlightLoopTests` already use —
and `Mirror`/`CompareState` replace the count with the whole universe, compared on every one of the
twenty iterations. That says how often the AI ran AND what it did each time.

**And it was checked rather than assumed.** A suite that compares more can still be comparing
nothing, so `RunTactics` was made to return early for exactly this fixture's shape and the run
repeated: `MVEIT: a HOSTILE ship, so tactics run: iteration 3, INWK+29 -- expected 2 actual 6`. The
roll counter the AI writes is the byte that says so. Reverted, and 397 pass.

**2026-09-06 — M3-b-1b: `ChartShapes` goes, and the first run without its traps found the port
drawing nothing at all.** `CIRCLE2` is `DrawBall` and `SUN` is `DrawSun`, both ported since slice
3c; the seam survived because "the charts are compared against the shipped game through it"
(§6.115), which is the argument that kept it and the reason nothing noticed what follows.
`effects-seams` 21 → 20, `register-params` 16 → 14 (the seam's `DrawSystemDisc` took an x and a y),
`outpost-elite-names` 199 → 193.

**THE PORT'S SHORT-RANGE CHART DREW NO DISCS, and no test could see it.** `TT23` opens
`LDA #199 / STA Yx2M1 / STA dontclip` and closes by putting both back — it lifts the clipper's
limits because the system discs go below the space view's floor. §6.45 moved those two stores OUT
of `DrawShortRangeChart` and into the caller, on the sound-sounding argument that both bytes live
with the drawing rather than with the chart; `Main.cpp` did them and the app was right. The suite
could not be, because `ChartShapes` meant the chart asked for discs and drew none, so the clipper
never came into it. Take the seam away and the port's canvas comes back empty against a game that
drew 114 bytes of ink on the first chart. The stores are inside the routine now, where the original
has them.

**What the tests compare changed shape, and got stronger.** `TT14` and `TT22` compared `K3`, `K4`,
`K` and `STP` at a `CIRCLE2` trap; `TT23` compared the SEQUENCE of `SUN`'s arguments through a
watched trap. All three now draw on both sides and compare the whole screen, which subsumes every
one of those: a circle at a different centre draws different pixels. `TT23`'s "how many discs"
became "how much ink", because the count was the trap's and the ink is the port's own.

**One thing the sweep needed that the seam had hidden**: `SUN` takes a `DORND` for the streak down
the disc, so the port's generator has to start where the game's does. `DrawBall` takes none, which
is why the other two sweeps needed nothing.

**2026-09-06 — M3-b-1a: `SpawnEffects` goes, and three traps come off with it.** The seam's four
methods — `ABORT`, `MESS`, `SPBLB` and `msblob` — are four routines this library contains, reached
through an interface because it did not when `KILLSHP` and `SOLAR` were ported. `KillShip`,
`AddPlanetOrSun`, `AddStation` and `BuildSystem` take `(Universe&, Ports&)` and call them;
`LoopSpawnEffects`, the adapter that answered the seam out of a universe and its ports, went with
it, and `MISSILE_GREEN` moved to `Dashboard.h` beside the routine that takes it. `effects-seams`
22 → 21.

**The tests are what made this a commit rather than a rename.** A seam is what a suite COUNTS: the
oracle traps the routine, the port records the call, and the two tallies are compared. With the
seam gone both sides run the routine, so the three traps in `SpawnTests` came off and the
comparison became the WHOLE SCREEN — the missile indicator, the message row and the station light
are all pixels, and one comparison covers them where three tallies did. `NWSPS` gained a screen
comparison it never had, and it asserts the indicator was actually drawn rather than that a call
was made. That is what §6.73 has been arguing a seam owes the port every time it comes up, and it
is why the remaining phase-order seams are a commit each rather than one sweep.

**2026-09-06 — the Windows job on M3-a-3, and a fourth half for `check_outpost.py`.** `Main.cpp`
would not compile: `Game`'s constructor still initialised `trade` and `save` and still named
`name`, `selectedSeeds` and `numberWidth` bare, all five in the MEMBER-INITIALISER LIST, which is
the one place none of the tool's three checks could see. That is the second consecutive slice to
fail this way — M3-a-2's was `recursive.SetCursor(&text)` in the same constructor — and twice is a
pattern rather than a slip.

The tool has a fourth check now: **every name a constructor's initialiser list initialises must be
a member of its own type, a base of it, or the type itself.** That is exactly MSVC's C2614, and it
is the half a regex can settle soundly; the C2065s that come with it — a bare identifier used as an
argument inside one of those initialisers — need a real parser, and both times they arrived
together, so catching the one catches the commit.

**Its own first draft would have passed the tree it was written to fail**, which is why the
self-test now plants a brace initialiser before the bad one. `ports{a, b}` opens a brace at depth
zero exactly as the constructor's body does, so the list-finder stopped at the first
brace-initialised member and read the rest of the list as empty — and `Game`'s two bad entries are
brace-initialised. The difference is what precedes the brace: an initialiser's follows its name,
the body's follows whitespace. Fourteen initialisers are read on the tree as it stands.

**2026-09-06 — M3-a-3 built, and M3-a is done: all seven argument-list structs are gone and what
is left IS `Ports`.** `TradeScreen`, `SaveScreen`, `GameStart` and `MissionBay` went the way the
flight half's four did: every docked screen, the disk menu, the start sequence and the six mission
exits take `(Universe&, Ports&)`. `aggregate-refs` **39 → 14**, and the fourteen are `Ports` itself
— the M3-a row's "at zero" is reached exactly when M3-b collapses that one struct to §4.5's four
ports, which is the slice it was always describing.

**`GameStart` was not a separate commit in the end, and could not be.** It held a `SaveScreen&`,
so it could not outlive it — the same knot `TitleScreen` and `MissionScreen` had with `FlightLoop`
one commit earlier, and the same answer: it goes with what it held. Rule 8 is satisfied because it
is one pattern, not two; recording it because the slice plan had it as M3-a-4.

**Nine parameters went with the structs, and every one of them was a second name for a byte the
universe owns**: the commander, `QQ28`, `tek`, `QQ9`, `QQ10`, `QQ15`, the market, `NA%`/`NAME`, and
`U`. `StatusScreen`'s `SystemSeeds& _outSelected` was the last of the out-parameters this phase can
reach (`out-params` 18 → 17): `TT111` writes `QQ15` and the screen reads it back, which is a field
and not a result. `Universe` gained `QQ9`, `QQ10`, `QQ15`, the market, `NAME`, `NA%`, the line
editor's buffer, `DISK` and `U` — nine byte ranges that were the composition root's, and are memory
in the original.

**`MissionBay` bought a small fidelity gain on the way out.** It carried `QQ11` and `QQ22+1` as
VALUES snapshotted at entry, and `BAY`'s fall-through reads them live in the original — which
matters, because a briefing's `{9}` runs `TT66` and moves `QQ11` while the token is still printing.
The port reads the universe now. Two mission tests asserted on their own `dockedFlag` local and had
to be pointed at the universe's; nothing else moved.

**`Ports` grew to fourteen and `Outpost::Game` took it over.** Eight of the fourteen are
`GameShell`'s and `SaveStore`'s rather than the flight session's, so a session that built the struct
would be composing the docked half; the root owns it and lends it back to the two objects whose own
seams are calls that need it (`FlightSession::AttachPorts` for `TACTICS` and `DOCKIT`,
`GameShell::AttachPorts` for `TT66`, `RESET` and the mission codes). `main-lines` 1,219 → 1,199,
`outpost-elite-names` 205 → 199.

**The test tree gained one header and lost nothing.** `NullSeams.h` answers all ten interfaces with
nothing, so a suite says which seams it cares about by passing its own recorder for those; the
flight fixture's `UnusedSeams` is an alias for it. The docked suites name the universe's fields
through local references rather than holding their own bytes — the same mistake in a fixture that
M3-a-2's Windows build found in the app, and the reason the aliases are written as aliases.

397 of 397 pass, all fourteen checks pass, and three more of the seventy-two mutants were
re-anchored with none dropped.

**2026-09-06 — M3-a-2 built: the routines take `(Universe&, Ports&)`, and four argument-list
structs are gone.** `FlightScreen`, `FlightLoop`, `MissionScreen` and `TitleScreen` held forty-nine
reference members between them and every ported routine took one of them; each now takes the
universe and the ports beside it. `Ports` is ten references — the three text objects, which cannot
live in a universe that has to copy because two of them take a seam, and the seven seams the
platform answers — and it is a struct of references for exactly one slice: M3-b collapses the seam
half to §4.5's four ports, and doing it here would be two patterns in one (rule 8). 397 of 397
still pass and the M0-c replay digests are untouched, which is the property the slice is for.

**The acceptance the slice plan corrected was itself wrong, in the port's favour.** It predicted
`aggregate-refs` 78 → 47 on a split of thirty-nine flight references and thirty-nine docked ones.
The real split is forty-nine and twenty-nine, so the number is **78 → 39**: the flight half's
forty-nine replaced by `Ports`' ten, and `TradeScreen` (nine), `SaveScreen` (nine) and `GameStart`
(eleven) left for M3-a's docked half and M3-c. Recording the miss rather than the outcome, because
the plan's number was arithmetic on a count nobody had run.

**M3-a-1 had quietly made two bytes out of one, and this is where it showed.** `Universe` took a
`std::uint8_t techLevel` while `CurrentSystem` stayed in the composition root — but `tek` reached
the flight half as a REFERENCE into that struct before M3-a, so the copy was a second byte with the
same name and no writer keeping them equal. `CurrentSystem` is a member of `Universe` now and
`PerformJump` and `GalacticJump` lost the parameter, which is the shape §4.4 wanted and the one the
original has. The same argument moved `INF`: `MissionScreen::shipSlot` carried the briefing ship's
slot out through `GameShell::SetBriefingShip` and back in through control code 22, because `PAUSE`
runs inside the token `BRIEF` is printing; it is `Universe::shipSlot` now and the shell's copy and
its two setter calls are gone.

**What the app lost.** `FlightSession` held twenty-two members of game state and holds none:
`Outpost::Game` owns the one `Elite::Universe` and the session takes a reference to it, which took
`outpost-elite-names` 225 → 205. `main-lines` did not move: `_game.universe.` is longer than
`_game.` and clang-format rewrapped what overflowed, and the aliases and doc blocks the slice made
redundant came out again.

**And the Windows job earned its keep on the first push, twice over** (R15). `Main.cpp` would not
compile: `recursive.SetCursor(&text)` named a member that had moved, which `check_outpost.py`
cannot see because a bare identifier in a constructor body is not a `name.member`. Behind it was
the one that mattered — `Game` still declared `Elite::Commander commander = Elite::DefaultCommander()`
beside the universe's own, and every reader had been redirected to `universe.commander`, which is
DEFAULT-CONSTRUCTED. Moving the byte without the initialisation starts the game with no credits, no
laser and no home system, and no test on either leg would have caught it: the fixtures assign
`DefaultCommander()` themselves. The dead member is gone and the cold start stores it in `Game`'s
constructor. The session still builds
`Ports`, because eight of that struct's ten references are to itself. `origin-markers` fell
3,954 → 3,937 and every one of the seventeen was a `///< 6502:` on a reference member naming a byte
`Universe.h` names in the same words — rule 4 is about markers on code, and no code lost one.

**Twenty-two of the seventy-two mutants were re-anchored, none dropped** (rule 3): their `find`
text named `screen.`, `_loop.` or `_mission`, and nine of them had to be rewritten by hand rather
than by substitution because the call they mutate changed shape as well as spelling. The count
stays at 72.

**The Windows job is still the gate.** `check_outpost.py` caught nineteen arity and member errors
in `Main.cpp` and `Shell.cpp` on this slice — the whole point of M3-0, one commit earlier — but it
reads names and arity and never types, so R15 is unchanged: no Linux runner compiles `Outpost/`.

**2026-09-06 — M3-a-1: `Elite::Universe` exists, and the whole suite ran against it unchanged.**
`GameLogic/Universe.h` holds the state the flight half's two argument-list structs name, in §4.4's
order, with **no reference member, no virtual and no printer** — so it copies, its layout is its
fields, and `UniverseImage` can hash it without knowing what else is in the program, which is what
`Game::StateHash` and the M0-c replay are built on. `ScreenState` moved into it from `ViewChange.h`,
because it is state and that is where the state lives now.

**The fixture inherits it rather than holding it**, which is what made the commit small: every
field the library owns is the base's, so the thousand `universe.canvas` in the suite kept working
and 397 of 397 passed with no test edited. Slicing a fixture now gives the state and nothing else.
What stays in the derived struct is the recording ports, the printers and two bytes that are the
fixture's own.

**Two fields did not go in, and both for the same reason.** `ExtendedTextState` is a member of
`CharacterPrinter`, and the printers cannot live in a universe that has to copy: `TextPrinter`
takes the bell as a `TextEffects*` and `ExtendedTokenPrinter` the control codes as a
`ControlCodes*`, so a universe that owned them would own a pointer to the platform. Giving the
printer a reference to state the universe owns is M3-b's question.

**The app is untouched**, deliberately: `FlightSession` still holds its own members and builds
`FlightScreen`/`FlightLoop` from them. It moves in M3-a-2, when the routines change and it has to
be edited anyway — one edit to the half no Linux runner compiles instead of two.

**2026-09-06 — M3-0: the app's member names are checked, because M3 is about to rename a hundred
of them in files no Linux runner compiles.** `check_outpost.py` has read every `Elite::Name` the
executable mentions since slice 3d-b and every call's arity since 3d-c, and its own docstring named
what was left: "anything reached through a member call rather than a qualified `Elite::` one". M3-a
moves every byte of game state into one `Elite::Universe` and rewrites `Main.cpp` and
`FlightSession` around it, which is exactly that — a rename of member names, in the half of the
tree the portable runner cannot build. The Windows job would find it several minutes after a push,
which is how `DockedShip` and `ClearMessageRows` were found, twice in one afternoon.

The check reads `struct X { ... }` and `class X { ... }` out of `GameLogic/*.h` with their base
lists, closes each type's members over its bases, finds every `Elite::Type name` the app declares,
and checks each `name.member` against that set. Three choices are deliberately conservative: an
identifier it cannot resolve is skipped, a type it cannot parse is skipped, and an identifier two
declarations disagree about takes the UNION of their members — a false positive in a check that
gates the build costs more than a miss. 111 accesses resolve on the tree as it stands, 84 of them
in `Main.cpp`, and none is wrong. `--self-test` plants an access that cannot resolve and fails if
it is not reported, because the arity half went a whole slice before anyone had watched it fail.

What it still cannot see is a parameter TYPE that keeps its arity, a member reached through an
expression rather than a named variable (`_game.flight.Loop().options`), and templates. Compiling
the app is the only thing that would.

**2026-09-06 — R22 ruled on, and it was not a ruling: half of it was written from the wrong
machine's commentary, and measuring the other half found a different defect.** The risk said the
altitude's radicand takes its low byte from whatever the frame last left in `Q`, and that `LOIN`
writes `Q` on every line it draws while this port keeps it local — so the owner was to choose
between `LOIN` publishing its last height and an ADR-001 §6 row accepting the divergence. **Neither
was needed.** A search of the whole disassembly for writes to `Q` — zero page 154 — finds
eighty-eight instructions and not one of them is inside `LOIN`: this build's line drawer works in
`P2`, `Q2`, `R2` and `S2` at 188 to 191, which are a second set of scratch bytes, and touches `Q`,
`R`, `S` and `T` nowhere. The claim came from the upstream commentary, which is the BBC's — the
same source that had this port writing `T2` where the game writes `T` until M2-c-1 (§8). There was
nothing for `LOIN` to publish.

**And the half that was real was never measured.** The radicand's low byte genuinely is inherited:
`MA23` does `SBC #36 / STA R / JSR LL5` and writes `R` alone, so `Q` is the frame's. Every frame
sweep in the suite puts the planet at a high byte of 0x20, which makes `MAS2` answer non-zero, so
`ALTIT` stays 255 and the square root has never run in a test. Two sweeps now do it:
`TheAltitudeMatchesMA23` seeds `Q` on both sides over eight values and eight distances — the
distances chosen so `SBC #36` leaves a small difference, because that is what makes `Q` the whole
radicand — and `TheFramesOwnQReachesTheAltitude` runs the whole of `M%` with the planet in range
over six bubble shapes (nothing else there, ships too far to draw, wireframes, dots, an explosion,
a sun) and lets each side decide `Q` for itself. `ALTIT` is in the compared image, so a port whose
frame left a different byte says so. It does not. **R22 is closed by measurement.**

**What the fixture found instead.** The first case failed with the ORACLE dead and the port alive.
`MA23` reaches `SBC #36` with the carry **clear**: `MAS3` returns the flag its last `ADC` left, and
the only path that sets it is the saturation the `BCS MA23` two instructions above has already sent
away. So the planet's radius costs thirty-seven, and the port had been subtracting thirty-six — an
altitude one unit too generous everywhere it is computed, and a death radius one unit too small.
Four slices of drawing work and a full-flight replay never saw it because no fixture put a planet
close enough. Fixed, and the M0-c replay **re-taken under rule 1's second case**: seven of the
sixteen digests moved and not one step did — the same 1,170 steps to the same dock.

**A hook on the frame comparison.** `CompareFrames` takes an optional seeder that runs after the
mirror and before the call, for a byte `UniverseImage` does not carry. Since M2-c that is
`MathWorkspace`'s two, and this is the first fixture that needed one.

**2026-09-06 — M2-d built: the boundary carries walked back to the instructions that set them,
and three of them were wrong.** Twenty-four `bool _carryIn` parameters cross a non-kernel boundary,
and the slice's row said they were "a routine boundary that happens to be where a 6502 flag was
live, and every caller passes a literal". Walking each call site back through the disassembly to
the nearest instruction that writes the carry says otherwise: twenty are the routine's operand, the
four literals left are flags inherited from before anything this port models, and three sites were
being passed a value the original does not have. §4.3.1 is the table.

**`MA47`'s two `JSR SPIN`s and the splinter roll.** `BURN` reaches the kill with the carry CLEAR --
`ASL INWK+31 / SEC / ROR INWK+31` shifts the zero the `ASL` put in bit 0 straight back out -- and
then `CMP #AST` and `CMP #Mlas` overwrite it, so `.nosp` is reached with "was the type at least an
asteroid" or "was the laser at least a mining one", and the splinter roll (reached only when both
compares were EQUAL) with the carry SET. The second `JSR SPIN` runs on what the first one left,
which is `DORND`'s when the roll dropped nothing and `SFS1`'s -- that is `NWSHP`'s "was it made" --
when it did. The port passed `false` to all four. `SPIN` and `SPIN2` return their exit carry now
and `MA47` threads it.

**Why four slices missed it.** No fixture reached the kill: `PopulateBubble` gives every ship sixty
units of energy and the sweep's strongest laser takes fifty, so `BURN`'s subtraction never borrowed
and `SPIN` was never called from a frame. Three cases were added -- a ship shot to bits in the
sights, and an asteroid shot with the right laser and the wrong one -- and the first of them
failed on `RAND` before the fix, which is the whole point of adding them. Both `SPIN` sweeps now
compare the exit carry as well, with the `SFS1` trap ending `SEC` the way `NWSHP` does.

**`M32`'s E.C.M. and `MA47`'s beep.** `M32` reaches `ECBLB2` through `LSR A / BCS`, so the carry is
SET; the port passed `false`. It is unobservable -- `NOISE` only hands the flag back when the sound
is switched off, and `ECBLB2` discards it -- and wrong all the same. The beep is not unobservable:
`MA47` reaches it with `HITCH`'s `SEC`, the port had a local already set to `true` for exactly that
flag two lines above, and passed `false` to the sound anyway. On a silent build `NOISE` returns
what it was given and `LL9` seeds an explosion cloud on it (§6.157), so the wrong value moved the
generator.

**What the slice deliberately does not do.** Four literals stay: `MTT1`'s (the spawner's first
`DORND` rotates in whatever `M%` left), part 5's, `SOLAR`'s (`RES2` runs into `ZINF` and returns
the carry its own caller had), and the flight loop's E.C.M. key (possibly `WARP`'s, three
instructions earlier, which this port calls through a seam). Each is an assumption, and the
parameter is what keeps it at the call site instead of buried in the routine -- so collapsing them
to constants, which the row's "carry-params at the kernel's floor" asked for, would have made the
tree read as if the question were settled. `PlaySoundEffectPitched`'s is dropped at a seam that has
no flag; the squeak's `CPY #&E0` is what the original passes, nothing reads it, and M3-b's ports
are where it would go.

**Green.** 395 of 395 with the oracle present and the M0-c replay record UNCHANGED -- the replay
never shoots a ship to pieces, which is the other half of why the defect survived. All thirteen
repository checks. The ratchet: `carry-params` 31 → **32**, because `SPIN2` really does hand its
caller's carry back when the count is zero and the port now says so; `origin-markers` 3,924 →
3,927. **Mutants** (rule 3): the corpus is re-run against the committed slice below.

**2026-09-06 — M2-c-3 built: `MathWorkspace` is two bytes, and both of them are there on purpose.**
`LL9`'s five scratch bytes -- `XX4`, `XX17`, `XX18`, `XX20` and `V` -- are locals of the one
function the eleven parts became, and so are `CNT`, `T`, `T1` and `U` where it used them; what is
left in the frame is the four stage results parts 3 to 11 hand each other, two of which have a
reader outside (`DOEXP`'s copy of `XX3`, `DOCKIT`'s stale `XX2+10`). The planet and the sun take
theirs as values: `DVID3B2` returns the `KBlock`, `CHKON` takes the radius and returns the circle's
bottom edge, `CIRCLE`/`CIRCLE2`/`SUN` take the radius, `BLINE` takes `(T X)` and `CNT` and returns
the `CNT` it advanced, `PLS22` takes an `EllipseAxes` with its `CNT2` start and `TGT` end, `PLS4`
returns the angle, `PLS5` the second axis pair, `EDGES` takes `YY(1 0)`, and `SUN`, `PL26`, `HFL5`,
`PLS3` and `PL9` hold the rest as locals. Outside the drawers, `HITCH`'s `(S R)`, `FRS1`'s `T1`,
`SPIN2`'s `CNT`, `SIGHT`'s `T`, `PTCLS`'s `U`/`CNT`/`TGT`, `DOEXP`'s `T`, `TACTICS`'s `CNT` and
`TT207`'s `P` went the same way.

**Two bytes are left and neither is scratch.** `Q` is the frame's, which `MA23`'s altitude check
reads (R22); `K2`'s bottom byte is the one `MV40` reads for the carry of its first addition without
ever writing it (M2-b, §8). `PL9`, `PL26` and `SUN` store to `K2` where the original's `STA K2` is,
for that read alone, exactly as the eight `Q` writers do -- the third time this slice has met the
pattern, and the reason the plan's "`DOEXP`'s `Q` is a parameter" line was wrong: `DOEXP` runs
inside `LL9` part 9, so what it leaves in `Q` is what the altitude check gets.

**And `CNT2` is not a parameter either.** `TACTICS` and `DOCKIT` write `RAT`, `RAT2` and `CNT2` in
three instructions and `TA6` reads all three; `RAT` and `RAT2` have been `FlightState` members since
slice 3a, so the third byte joins them as `steerCone` rather than threading through `TA151`, `GOPL`
and `TA152`. The ellipse walk's `CNT2` is a different meaning in the same byte and is `PLS22`'s
parameter, as planned.

**Tests.** Every comparison that was about an answer is kept -- `CHKON`'s `(P+2 P+1)` (now compared
only on the on-screen paths, which are the ones that write it), `DVID3B2`'s `K`, `BLINE`'s returned
`CNT`, `DOEXP`'s `Q`, the sun's and the planet's `K2` bottom byte. The ones that were about a
routine's own scratch go with a comment naming what pins the byte instead: `LL9`'s `XX18` (the
`ovflw` retry is visible in `XX2`, the heap and the screen, over thirty-three blueprints and five
placements), `CIRCLE2`'s and `SUN`'s `CNT` and `TGT` (the segments and the row widths), `PTCLS`'s
`U`, `CNT` and `TGT` (the sprite seam records every particle), `SPIN2`'s `CNT` (the spawn seam),
`SIGHT`'s `T` (the sprite-enable byte), `BLINE`'s `CNT` (returned) and the `PLANET` sweep's `CNT`,
`CNT2` and `TGT` -- whose coverage counters now read the ORACLE's `TGT`, because it is the sweep's
reach they measure and not the port's answer. One dead helper removed from `UniverseImage.cpp`
(`Pair` lost its last caller when M2-c-1 dropped the `T2` cell).

**Green.** 395 of 395 with the oracle present, the M0-c replay record unchanged, all thirteen
repository checks. The ratchet: `register-params` 17 → 16, `workspace-params` 58 → 46,
`origin-markers` 3,887 → 3,924 (rule 4). **Mutants** (rule 3): no recorded mutant anchors a line
this slice moved; the corpus is re-run against the committed slice below.

**2026-09-06 — M2-c-2 built: the clipper takes a line and answers with one, and the frame's `Q`
turned out not to be the helpers' to keep.** `LL145` and `LL147` take a `Line16` — two sixteen-bit
points, the second's y from `XX12(1 0)` because six bytes of `XX15` cannot hold eight — and return
a `ClipResult{Line line; bool rejected; std::uint8_t swap; std::uint8_t ends}`. `LL115` returns a
`Slope{gradient, direction, steep}`; `LL129` returns a `PreparedSlope`; `LL120`, `LL123` and
`LL118` take the slope and the distance and give back a `SlopeStep`; `LL83`, `LL109` and `LL146`
work on the value. Below and around them the planet and the sun lost their `DrawWorkspace` too —
`EDGES` returns a `SunRow{x1, x2, offScreen}`, `WPLS`, `WPLS2`, `HLOIN2`, `PL2`, `CIRCLE`,
`CIRCLE2`, `BLINE`, `SUN` and `PLANET` take what they used to stage — and so did `LL51`, which
takes the `Vector16` in `XX15`'s six bytes, and `LL9`'s erase, line walk and dot. `DrawWorkspace`
is `SC` alone and `ClipState` is `dontclip` alone, as the slice plan said.

**Two things the plan had wrong.** `SWAP` cannot be a `bool`: `LL145` zeroes it and answers 0 or
255, but `LL147` does not, and `LL9` part 10 clips edge after edge through it, so the byte walks
255, 254, … It is a `std::uint8_t` on `ClipResult` and the sweep compares the byte. And `Q` is not
a local of the slope helpers. `LL129`'s `STA Q` and `LL122`'s `ASL Q` mean a clamped line leaves a
different `Q` behind than an unclamped one — zero after the multiply, the gradient after the
divide — and that byte is the frame's `Q`, the one `MA23`'s altitude check reads (§8, R22). The
port has modelled it faithfully since slice 3b and the value form dropped it, which is a fidelity
regression rule 1 does not allow the replay record to absorb. `SlopeStep` carries the leftover
divisor out, `MovePointOnScreen` publishes it, and the clipper keeps `MathWorkspace&` for that as
well as for `LL115`'s divisor. `R` and `S` genuinely are the helpers' own and are gone.

**Tests.** The six sweeps (`LL51`, `LL129`/`LL120`/`LL123`, `LL118`, `LL145`/`LL147`, `EDGES`,
`WPLS`/`WPLS2`/`PL2`/`CIRCLE`/`BLINE`) stage the same bytes into the values they now pass. Every
comparison that was about an answer is kept and three are narrowed, each with a comment: `XX15+4`
and `XX15+5` are working bytes `LL109` steps on and nothing reads, so the four coordinates
`LL146` writes are what is compared; `XX12(1 0)` is the second end's y going in and one of the
four bytes `LLX117` exchanges, and `LL9` parts 9–11 and `BLINE` write both before every call, so
the sweep compares `XX12+2` up; and the four coordinates are compared only for a line that was
ACCEPTED, because `LL109` returns with the carry set and no answer. `R`, `S` and `T` after the
slope helpers go the way `M2-c-1`'s scratch comparisons went. One test bug fixed on the way: the
`PL2` sweep seeded the oracle's `X1`/`Y1` with 100 and 50 while `WPLS2` starts its walk from them
— the game reaches it from a break, so both sides now start from zero, as the `WPLS2` sweep
already did.

**A defect in the census tool.** M2-c-1 pasted eight verdicts into `channel_census.py`'s `LABELS`
dict instead of its `VERDICTS` dict, so §4.3's "6502" column has been carrying prose where it
should say `P`, `TGT`, `YY`, `XX15+4`, `XX16`, `XX13`, `X1` and `K3 to K3+9`. Restored, the fields
this commit removed are out of the census, and `ClipState.dontclip`'s verdict is corrected: it said
"only `ZERO` writes it", and `TT23` — `Main.cpp`, since slice 2 — writes 199 to it for the
short-range chart. That is why the byte stayed a struct rather than joining `ClipResult`.

**Green.** 395 of 395 on the portable runner with the oracle present, the M0-c replay record
unchanged (which is the proof the `Q` restoration was needed and sufficient), and all thirteen
repository checks. The ratchet: `register-params` 20 → 17, `workspace-params` 86 → 58,
`origin-markers` 3,854 → 3,887 (rule 4). **Mutants** (rule 3): no recorded mutant anchors a line
this slice moved, so none is re-anchored; the corpus is re-run against the committed slice below.

**2026-09-06 — M2-c-1 built: the line and everything that draws one take values, and `HLOIN`
turned out to be writing the wrong byte.** `LOIN` takes a `Line` and answers with a `DrawnLine`
(the four ends as it leaves them and `SWAP` as a `bool`); `PIXEL`, `PIXEL2`, `CPIX2` and `CPIX4`
take the point, the distance and the colour; `HLOIN` takes its two ends and its row. Above them,
`SCAN`'s blip, the compass's dot, `SPS1`/`SPS4`/`TAS2`/`TA2` (which answer with a `UnitVector` and
the length `NORM` left), `TAS3`/`TAS4`/`TAS6`, `DIL`/`DIL2`/`DIALS`, the charts' crosshairs, rules
and fuel circle, the lasers, the border, the stardust's three movers and `nWq`, `WPSHPS`, `SOLAR`
and `MVEIT`'s blip all take what they used to stage. `BPRNT` takes its value and its digit count
and returns the `U` it leaves, so `NumberWorkspace` goes and `SaveScreen` keeps the one byte `SV1`
reads (the competition number's width, which is the last `BPRNT`'s). `MVEIT` and `LL9`'s erase,
line walk and dot lose their `DrawWorkspace` with the blip.

**The port was writing `T2` and `R2` where the game writes `T` and `R`** (rule 1's second case).
The new `HLOIN` sweep compares what the routine leaves in `T2` and it disagreed on 2,112 of 15,360
lines -- because the C64's `HLOIN` stores `T` (`.HL1 TXA / AND #&F8 / STA T`, and `HL2` then
overwrites it with the right-hand mask) and `BOX2` stores `T` as well (`LDX #18 / STX T`), while
the port had followed the upstream commentary's BBC naming into two bytes this build never uses
there. Nothing read either byte on either side, so the screens agreed and four slices missed it.
What it moved was the M0-c digest: `UniverseImage` hashed a `T2` cell holding the port's invention.
Both are locals now, the cell is gone, and **the replay record is re-taken** -- same 1,170 steps,
same dock, every digest one byte narrower.

**Tests.** The sweeps compare what each routine now returns and keep every comparison that was
about an answer: `LOIN`'s four ends and `SWAP` are compared for the first time, `TAS2`'s length
(`Q`) likewise, and `BPRNT`'s exit `U`. The comparisons that were about a routine's own scratch --
`SCAN`'s three bytes, `CPIX4`'s `Y1`, `DIL`'s `COL` and `Q`, `DIALS`'s `K`, `K+1`, `T1` and `XX12`,
`BOX2`'s `T2`, `MLU1`'s `Y1`, `PTCLS`'s `ZZ` and `Y1` -- go, each with a comment saying which sweep
pins the byte instead. `K3+9` is `TAS2`'s shift counter and the block compares stop at nine bytes.
**Mutants** (rule 3): `ta-selftest` and `ta20-eor` name lines this slice moved and are re-anchored
to `NegateVector`'s value form and `SteerMissileTowardsTarget`'s call, and the tactics unit is
re-run: 16 of 16 caught, and the whole corpus with it -- 72 mutants, 68 caught and the four
recorded survivors surviving, as recorded. The ratchet: `register-params` 22 → 20, `workspace-params`
151 → 86, `main-lines` 1,220 → 1,219, `outpost-elite-names` 226 → 225 (`NumberWorkspace` leaves the
app), `origin-markers` 3,807 → 3,854 (rule 4: every byte a value replaced carries its label).

**2026-09-06 — M2-b built: the kernel takes values and answers with structs.** Every routine in
`Arith.h` lost its `MathWorkspace&` (the slice plan above lists the types and the names), and every
caller — twelve files, a hundred-odd call sites — passes what it used to stage and stores what it
used to read back; the staging wrappers and helpers that had no other job (`MLS2`, `MUT1`, `MUT2`,
`MULT12`, `MU6`, `MULTS-2`, and the workspace parameter of `TAS3`, `TIS3`, `MVS4`, `MVS5`, `TIDY`,
`MAS1`, `TAS1`, `SFS2`, `SPS2`) went with them. The suite is green (`Tests/PortableRunner`, oracle
present) and the M0-c replay record is unchanged, which took one detour worth the whole entry.
**The frame's Q.** The first green suite failed the replay at step 100. The audit had flagged one
inherited kernel input — `EndFlightFrame`'s `LL5` reads `Q` without anyone in the routine writing it
— and tracing every `Q` store in the pre-M2-b tree over the replay showed what the byte was: `MVS4`'s
BETA in 134 of the 184 altitude checks (the last ship moved and not drawn), `LL9`'s last vertex
distance in 45, the clipper's in 4, `DVID3B`'s scaled divisor in 1. The original does the same —
`MA23`'s radicand low byte is whatever the frame last left in `Q` — so those writers are kept, as
five explicit `_math.q =` stores that say what they are for, rather than as forty staging stores;
the replay proves the value is the one the port always read. What the trace also showed is that
the port has never modelled the biggest writer of all: `LOIN` stores `Q` on every line it draws and
has kept it local since slice 1d, so on a frame whose last ship drew lines the original's byte is
the last line's height and this port's is not. That is R22, older than M2, and the owner's to rule
on: `LOIN` publishing its last height, or an ADR-001 §6 row accepting the divergence. **`MV40`'s
`K2`.** The same audit found `MV40`'s `LDA K / CLC / ADC K2` reading a byte it never writes:
`K2`'s bottom byte is whatever the last planet or sun drawer left, and the carry of the first
addition depends on it. It stays in the workspace, read once and documented, and `MV40` holds the
rest of both blocks as `KBlock` locals — the plan's "K and K2 in MV40 → locals" row, with one byte
of state by design beside it. **`TIDY`'s `Q`.** The cross product set `Q` once and ran two more
`MULT12`s on whatever the `TIS1` before each left, which the port reproduced by calling the routines
in the original's order on the same workspace; with values, each `MULT12` is written with the
multiplier it actually gets — `TIS1`'s X — and the `TIS1` sweep now asserts that `Q` is X on the way
out. **`MULT1`'s exit carry** is modelled for the first time (the opening `LSR A`'s on `mu10`, the
last `ROR P`'s otherwise) and pinned by the exhaustive sweep; no caller reads it. **The census
missed three receivers**: `TACTICS` binds `MathWorkspace& math = screen.math;` and the tool saw only
parameters and `FlightScreen` members; it reads local references now, and the table is re-taken.
**Tests.** The arithmetic sweeps compare returned fields and keep every comparison that was about an
answer; the comparisons that were about the kernel's scratch (`T`, `T1`, `U`, `widget`, `S` after
`LL61`, the six bytes after `DVID3B`, `DIALS`'s `T1`, `PLANET`'s `K`) are gone, each with a comment.
The ratchet: register-params 64 → 22, workspace-params 207 → 151, origin-markers 3,721 → 3,798
(the frame's `Q` at its five writers and the kernel's scratch where it became locals, labelled;
rule 4). Merged the owner's death-sequence fix (`EE51`'s carry, `SeedExplosionCloud`, seven new
mutants) from the branch mid-slice; no conflicts. **Mutants** (rule 3): `python tools/mutate.py
--runner portable` against the committed slice, 72 of 72 as recorded — 68 caught and the four
recorded survivors surviving. The first run said 67 and 5: the owner's `cs-ll9-carry-hit`
survived, and it survived on the owner's own head too (their entry above says the tally was the
next run's to report). Two things had hidden it. The frame comparison never mirrored or compared
`RAND`, and the fixture's "on top of us" ships are behind the player by the time `HITCH` looks —
`MVEIT` takes the speed off z first — so no case in the per-ship sweep ever reached `LL9` with the
carry set; by the same token none of its "at close range" laser cases reaches `ApplyLaserHit`,
which this entry names and does not close. The comparison now carries `RAND` both ways and a case
two units ahead gives `HITCH` a ship to say yes to, and the mutant is caught by the generator's
state. The harness also found its own abort on the way: the owner's death-screen test threw from
inside a `noexcept` frame callback, which ended the runner instead of failing the test, so the
cloud-seed self-test aborted every portable run; `Watching` records the failure and asserts after
`Die`, and the self-test fails the test as it must.

**2026-09-06 — `main` merged in, mid M1-f.** The owner's death-sequence pacing (`HoldFlightFrame`,
a `TunnelEffects*` on `Die`) came in from `main` with one conflict — `leaving.world.dashboard`,
which this branch had already renamed to `universe` — and the suite was 392 of 392 on the merged
tree before the M1-f work was put back. It moved one count: `main-lines` 1,162 → 1,199, the
thirty-seven lines the pacing adds to `Main.cpp`. That is the product moving, not a pattern
coming back, so the ceiling follows it and says so in `slice`; M3 is the slice that takes it down.

**2026-09-06 — The death sequence fix (plan §6.157) moved two counts, and the replay record.**
`carry-params` 28 → 31: `EraseShip`, `SeedExplosionCloud` and `DrawShip` carry the flag `LL9` is
reached with, because a newly killed ship's cloud is seeded on it -- a 6502 flag that is live
across a routine boundary, which is what P11 counts, and which M2's explicit-convention pass will
fold into a result the way §4.3 folds the others. `origin-markers` 3,705 → 3,718: the `EE55` block
is `LL9`'s code now and labelled, as rule 4 requires until M6-e. `effects-seams` did not move:
`SeedExplosionCloud` left `ShipDrawEffects`, but the class still has two pure virtuals. The replay
record moved from step 200 under rule 1's second case -- the port was wrong: no cloud had ever been
seeded, and the scripted flight's first kill seeds one now -- and the journal entry names it.
`mutants` 65 → 72: the `cloud-seed` unit, a selftest and six flags, one per decision the fix made
(rule 3); the tally is the next run's to report, because `mutate.py` builds HEAD and the fix is
uncommitted as this is written.

**2026-09-06 — M1-d left an indexed load running past the typed hold (plan §6.158).** `OUCH`'s
`QQ20,X` reaches the five fittings after the seventeen goods by design, and `cargoHold[slot]` on a
`std::array<std::uint8_t, 17>` is an out-of-range subscript for all five: Debug asserts, Release
reads the right bytes by the accident of layout, and CI runs Release. `Commander::HoldOrFitting`
is the typed form of the same load. Rule 2's "every widening happens inside a helper" has a
sibling: every access that ran past a byte array's end on purpose needs a named helper too, and
the Debug configuration is the check that finds the ones a slice missed. `origin-markers`
3,718 → 3,720 for the helper's label and the load's.

**2026-09-06 — The hyperspace key wired (plan §6.159) moved three counts.** `main-lines`
1,199 → 1,220 for the read of the matrix in `PressKey`, the every-pass dispatch in `Advance` and the second countdown byte; `outpost-elite-names`
224 → 225 for `KEY_HYPERSPACE`; `origin-markers` 3,720 → 3,723 for its label and the two on the second countdown byte; `SetUpLoaderVideo` (plan §6.160) adds one more executable name and its labels. The product moving,
as with the pacing; M3 takes the lines down and this executable-name count with them.
