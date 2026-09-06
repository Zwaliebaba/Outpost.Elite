# Modernize — from a faithful transcription to a modern C++ codebase

**Status:** **Accepted in scope · opened 2026-09-06, §1's questions ruled the same day** (all eight, and a
ninth the owner added: the port is DETACHED from the original at the end — the oracle, the assembler
source, the labels in the code and the assembly in the comments all go, §6 Phase M6). **The gate ADR-001
§4 set for phase 6 is met**: every oracle
suite, every whole-bitmap comparison and the docked replay are green on the faithful build
(<!--count:tests-->397 tests, oracle present), all <!--count:checks-->fourteen repository checks pass,
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
| **M6** | **Detach.** The oracle's answers are recorded as checked-in fixtures and the live oracle is retired; the identifiers named for 6502 labels, the assembly quoted in comments, the `// 6502:` markers and the ledger go; `MasterFile/`, `Upstream/`, the interpreter and the tools that read the original leave the tree. | A C++ program that builds, tests and reads on its own, with the original's data as its only inheritance (owner ruling, §1). |

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
| `EndFlightFrame` | `MA18` to `STARS` (parts 13–16) | Shield and bank recharge every eighth frame, the sixteen-step housekeeping cycle (energy warning, docking-computer reminder, cabin temperature, altitude, fuel scooping, the planet and sun through `PLANET`), then the stardust | status, bubble slots 0 and 1 | status, canvas, `FlightState::delt4` |

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

What nothing pins: the outer loops and the mode machine in `Main.cpp` (Windows only, no test); the
presenter (R5, by design); timing (ADR-005 §3, by design). M0 closes the first of those.

---

## 3. The legacy patterns, named and measured

Each pattern below is a claim about the tree and carries a checked number where the tree can be
counted. `tools/check_modernize.py` counts them and **fails the build if any count rises** — the
ratchet is what stops a slice reintroducing what another slice removed (§5, rule 5). The recorded
ceilings are in `tools/modernize_ratchet.json` and are lowered as slices land.

**P1 — The register-shaped calling convention.** <!--count:register-params-->16 parameters in
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
`LL9`'s four stage results. <!--count:workspace-params-->46 parameters in the headers are still one
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

**P5 — Reference aggregates as argument lists.** <!--count:aggregate-refs-->78 reference members
across the structs `FlightScreen` (twenty-three), `FlightLoop`, `TradeScreen`, `SaveScreen`,
`GameStart`, `MissionScreen`, `TitleScreen` and `ClipState`'s neighbours. `ViewChange.h` says it
plainly: "the struct is the argument list". They are built by `FlightSession`'s constructor, by
`Main.cpp`'s `StartOf`/`ChartOf`/`JumpOf`/`OptionsOf`, and by `FlightUniverse.h`'s `Screen()`, three
times over.

**P6 — Game state and the top of the program in the executable.** §2.6. `Outpost/Main.cpp` is
<!--count:main-lines-->1,219 lines, most of them the dispatch, the exits and the two loops. Plan
§2.1's `class Game { Reset(); Step(InputFrame); Frame(); Sounds(); StateHash(); }` was the seam
ADR-004 §1 drew "from day one" and it does not exist; `check_outpost.py` exists precisely because
the executable reaches <!--count:outpost-elite-names-->225 distinct `Elite::` names that
only a Windows compiler can type-check.

**P7 — Seams that outlived their reason.** <!--count:effects-seams-->22 abstract classes in
`GameLogic/*.h`. Some are platform (`TextSink`, `KeySource`, `DashboardEffects::PlaySound`,
`TunnelEffects::ShowFrame`, `SaveStore` through `SaveScreen`). Most are **phase order**:
`ShipEffects::RunTactics`, `ShipDrawEffects::DrawPlanetOrSun` and `DrawExplosion`,
`FlightLoopEffects::SpawnAhead` and `Anger`, `SpawnChildEffects::SpawnChild`, `ChartShapes`,
`ViewEffects::PlaySound`, `SightEffects`, `ExplosionEffects` — each declared when the routine on the
far side was "phase 4's" and kept after it landed, which §6.73 already names as a mistake made four
times. Three methods are declared on two interfaces each and one override satisfies both, which is
the language's rule and a smell. One seam carries a CPU flag across the platform boundary:
`PlaySound(std::uint8_t _effect, bool _carryIn)` returns a carry because `NOISE` does (§6.99), and
the *window* is asked to preserve it.

**P8 — Monolithic frame procedures.** `MoveEveryShip` is four hundred lines over nine annotated
parts; `BeginFlightFrame` and `EndFlightFrame` about the same between them; `DrawShip` 531 and
`RunTactics` 567; `RunDockingComputer` two hundred. Each is one routine in the original and one
function here, with local `bool`s standing in for the branch targets (`docking`, `scoopable`,
`collision`, `holdFull`, `crashed`) and results carried in the workspaces.

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
would touch eighty-seven call sites" (`Main.cpp`); <!--count:out-params-->18 parameters are
`std::uint8_t&` outputs (`_docked`, `_fuel`, `_crosshairX`).

**P11 — Carry-in parameters across non-kernel boundaries.** <!--count:carry-params-->32 `bool
_carryIn` parameters in headers. Inside the kernel (`AddWithCarry`, `Rng::Next`, the multipliers)
they are the numeric model and stay. The other twenty-four are a routine boundary that happens to
be where a 6502 flag was live, and this pattern's original entry said "every caller passes a
literal". **M2-d audited all twenty-four against the disassembly and that was wrong**: most are a
computed flag the port models, three were passed the wrong value, and the literals that remain are
each an inherited flag the port cannot see — the parameter is what makes the assumption visible at
the call site rather than buried in the routine. §4.7 is the table and §8 the three defects.

**P12 — The original as a build and test dependency.** <!--count:origin-markers-->3,927 `6502:`
references in `GameLogic/`'s comments; <!--count:oracle-test-files-->50 of the test translation
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
| `MathWorkspace.q` | `Q` | AddStep, DivideByShipZ, DrawExplosionCloud, DrawParticles, DrawShip, DrawSun, MeasureSlope, MovePlanetOrSun, MoveShipTail | EndFlightFrame | — | **The frame's Q**, and one of the two bytes left (M2-b, §8; risk R22). `MA23`'s altitude check takes whatever the frame last left in `Q` as its radicand's low byte, so `MoveShipTail`, `MovePlanetOrSun`, `DivideByShipZ`, `DrawShip`, `DrawSun`, `DOEXP`'s two routines and the clipper's `LL115` and `LL118` write it for that read alone, as the original's `STA Q`s do. R22 said `LOIN` was a tenth writer this port never modelled and it is not: this build's `LOIN` works in `P2`, `Q2`, `R2` and `S2` at 188-191 and never touches `Q` at 154. Closed 2026-09-06 by measurement -- `TheFramesOwnQReachesTheAltitude` runs the whole frame with the planet in range and compares `ALTIT`. |
| `MathWorkspace.k2Low` | `K2` | DrawPlanetDetail, DrawSun | MovePlanetOrSun | — | **One byte of state, deliberately** (M2-b, §8). `MV40` never writes `K2` and its `LDA K / CLC / ADC K2` reads this byte for the carry of its first addition, so what it gets is whatever the last planet or sun drawer left there a frame ago. `PL9`, `PL26` and `SUN` store to it where the original's `STA K2` is; the other three bytes of the block are the ellipse's axes and travel as an `EllipseAxes` value since M2-c-3. |
| `DrawWorkspace.sc` | `SC(1 0)` | DrawBar, DrawDials, DrawIndicator | DrawBar, DrawIndicator | — | **State, deliberately** (M2-c leaves it; M4 names it). `DIALS` sets the screen pointer once and `DIL`/`DIL2` advance it seven calls running (its own comment, slice 3d-b): a cursor the dashboard drawer owns, not scratch. |
| `GeometryWorkspace.xx16` | `XX16` | DrawEllipse, DrawPlanetDetail, LoadTwoAxes, ScaleOrientation | DotProducts, TransposeOrientation | — | **Stage result** (M2-c-3 leaves it in the frame; M4 makes it a pipeline). `LL15`/`LL21` fill it, `LL51` and the transpose read it, and the planet drawer uses the same six bytes for the ellipse's four signs -- two meanings, one block, as `RAT` and `RAT2` are. |
| `GeometryWorkspace.xx12` | `XX12` | BothEndsBeyondTheSameEdge, ClipLineKeepingSwap, DotProducts, DrawBallLine, DrawShip, MeasureSlope | FaceVisibility | DrawShip (after ?) | **Stage result** (M2-c-3 leaves it in the frame). `LL51` leaves three dot products that `LL9` parts 4 and 6 read, and `LL83`/`LL115` work in the same bytes while a line is being clipped -- the original's reuse, which nothing reads across. `DIALS` stopped borrowing them in M2-c-1. |
| `GeometryWorkspace.xx2` | `XX2` | DrawShip | EitherFaceVisible, RunDockingComputer | — | **Stage result, and one reader outside** (M2-c-3 leaves it). Face visibility, written by part 4 and read by parts 6 and 10; `DOCKIT` reads `XX2+10` as the memory it is (§6.112), which is why the frame is a struct and not four more locals. |
| `GeometryWorkspace.xx3` | `XX3` | DrawShip | DrawExplosionCloud | — | **Stage result, and one reader outside** (M2-c-3 leaves it). The projected vertices, filled by part 8 and read by parts 9 to 11; `DOEXP` copies them onto the heap for the burst, which is the frame's second outward reader. |
| `ClipState.dontclip` | `dontclip` | ResetShipAndBubble | ClipLineKeepingSwap | — | **State one screen writes and the clipper reads** (M2-c-2 leaves it). `TT23` sets it to 199 so the short-range chart can use the whole screen and `RES2` clears it again -- `Main.cpp` and `ResetShipAndBubble` in this port -- so it is not the clipper's scratch and did not become a `ClipResult` field with `XX13` and `SWAP`. `TT23` writes `Yx2M1` in the same two instructions and that byte is on `PlanetSunState`; whichever slice wires `TT23` puts this one beside it. |
| `Projection.x` | `K3` | DrawPlanetDetail, Project | CircleOffScreen, DrawBall, DrawEllipse, StorePoint | DrawPlanetDetail (after DrawHalfEllipse), DrawSun (after CircleOffScreen) | **State that outlives the call, deliberately** (§4.3's `PROJ` row; ADR-001 §6, `SHPPT`). `Project` writes it half at a time and `DrawShipAsPoint`, the planet drawer's `CircleOffScreen`, `DrawBall`, `DrawEllipse` and `DrawSun` read what the last `Project` left; `DrawPlanetDetail` rewrites it for the crater. Stays a parameter. |
| `Projection.x1` | `K3+1` | DrawPlanetDetail, Project | CircleOffScreen, DrawBall, DrawEllipse | DrawShipAsPoint (after Project), DrawSun (after CircleOffScreen) | **State, deliberately**, with `x`: the stale `K3+1` `SHPPT` reads is the ADR row. |
| `Projection.y` | `K4` | DrawPlanetDetail, Project | CircleOffScreen, DrawBallLine | DrawPlanetDetail (after DrawHalfEllipse), DrawShipAsPoint (after Project), DrawSun (after CircleOffScreen) | **State, deliberately**, with `x`. |
| `Projection.y1` | `K4+1` | DrawPlanetDetail, Project | CircleOffScreen, DrawBallLine | DrawSun (after CircleOffScreen) | **State, deliberately**, with `x`. |
| `K3Block.*` | `K3 to K3+9` | LoadPlanetAxis, LoadStationAxes, NormaliseAxes, OffsetAxis, RunTactics, SubtractShipAxis | BuildUnitVector, NormaliseAxes, OffsetAxis | RunDockingComputer (after SubtractStationAxes) | **Parameter and result** (M2-c). `SPS1` (`LoadPlanetAxis`, `NormaliseAxes`) leaves the vector `BuildUnitVector` and part 9 read; `TAS2`/`OffsetAxis` take and return it: `UnitVector`/`Vector24`, §4.3's `XX15 after SPS1` row. |
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
| `PlaySoundEffectPitched` (`NOISE2`) | `CPY #&E0` at the Trumble squeak: set for a burning cabin, clear otherwise | `false`, because `DashboardEffects::PlaySoundPitched` has no flag | **Dropped at the seam**, and unobservable: the only reader is `NOISE`'s sound-off return and every caller discards it. M3-b's seams are where it would go |

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
| `Presenter` | `TunnelEffects::ShowFrame`, `TradeScreenEffects::ClearToView` (the pixels half), `LineEntryEffects::WaitFrames`, `StartUpEffects::WaitFrames`, `ExplosionEffects`/`SightEffects`'s VIC pokes (they become `VideoState` writes the library makes itself) | `Present()`, `WaitFrames(n)` |
| `Keyboard` | `KeySource`, `ControlEffects::ScanKeyboard`, `StartUpEffects::ScanTitleKeys`, `FlushKeyboard`, `JumpState::controlHeld` | `Scan(KeyLogger&)`, `NextKey()`, `Flush()` |
| `SoundSink` | `DashboardEffects`, `ViewEffects::PlaySound`, `TextEffects::Beep`, `FlightLoopEffects::Start/StopDockingMusic` | `Write(SidRegister, value)` — the library runs `NOISE` and the music player itself and emits register writes, which ADR-003 §1 already says is the port's `SoundEvent` stream |
| `SaveStore` | the `SaveScreen` half that reads and writes files | `Load(name) -> std::optional<Image>`, `Save(name, Image)` |

Everything else in the twenty-two is either a call into a routine that now exists (`RunTactics`,
`DrawPlanetOrSun`, `SpawnAhead`, `Anger`, `SpawnChild`, `ChartShapes`, `DrawExplosion`,
`SeedExplosionCloud`, `ResetUniverse`, `ResetShip`, `ClearKeyLogger`, `StartTheme`, `StopTheme`,
`ShowTitleScreen`, `Run(controlCode)`) or a `VideoState` write. `PlaySound`'s carry stays inside
the library where `NOISE` lives. The null port for tests is one class with four interfaces and a
transcript, which is what `NullShell` and `LoopRecording` are today in two halves.

### 4.6 Pipelines with named stages

**The flight frame** keeps `MainFlightLoop` as its orchestrator and turns each annotated part into
a function with a typed result, so that the local `bool`s become one value:

```cpp
enum class Contact : std::uint8_t { None, Docking, Scoopable, Collision };   // part 7's four answers
struct ScoopResult { bool crashed; bool holdFull; std::optional<Cargo> item; };  // part 8
enum class DockingTest : std::uint8_t { Arrived, Bounced, Fatal };             // part 9
struct LaserHit { ... };                                                       // part 11, already a struct
```

and the slot loop is `while (auto slot = bubble.NextOccupied(slot))` with `KillShip` returning
whether the index must not advance — the same control flow, said once.

**`LL9`** becomes six stages over a `ShipRender` frame object: `Visible?` → `ScaledOrientation` →
`FaceVisibility` → `ProjectedVertices` → `EdgeSelection` → `ClippedLines` → `HeapRun`, each a
function of the previous stage's result; `DrawShipAsPoint` and `EraseShip` stay as they are (the
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
the escape pod. The data tables' `extract_tables.py --check` becomes a digest of the generated files
committed beside them; the tables themselves are already the port's own C++.

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
| **M3-a Universe** | `Elite::Universe` as a plain aggregate; `FlightScreen`/`FlightLoop`/`TradeScreen`/`SaveScreen`/`GameStart`/`MissionScreen`/`TitleScreen`/`JumpState` replaced by `Universe&` (plus the ports) on every routine; `FlightSession` and `Outpost::Game` lend their members to it. | Green on both legs; `aggregate-refs` at zero. **Windows job is the gate** — this slice cannot be compiled here. | 4 |
| **M3-b Ports** | The four port interfaces; the phase-order seams replaced by direct calls; the null port in tests replaces `NullShell`, `LoopRecording`, `RecordingSight`, `RecordingView`, `RecordingDashboard`. | Green; `effects-seams` at four. | 4 |
| **M3-c Game** | `Elite::Game` with `Reset`, `Step`, `Frame`, `Sounds`, `StateHash`; `Perform`, `Leave`, the docked pass, `Advance` and `AdvancePaused` moved from `Main.cpp`; `Mode` explicit. `Main.cpp` at its target shape. | `DockedSessionTests` and the M0-c replay drive `Game::Step` and reproduce their stored hashes; `main-lines` in the ratchet under 300. | 4–5 |
| **M3-d ADR-007** | State ownership and the replay hash, written from M3-a..c as built. | Accepted. | 1 |

### Phase M4 — Control flow

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M4-a Flight frame stages** | `MoveEveryShip`'s parts 7–12 as typed stages (`Contact`, `ScoopResult`, `DockingTest`, `LaserHit`, `KillOutcome`); `BeginFlightFrame` and `EndFlightFrame` split at their annotated parts with the sixteen-step cycle as a table. | `FlightLoopTests` green frame for frame; replay hashes unchanged. | 4 |
| **M4-b LL9 stages** | `DrawShip` as the six stages of §4.6 over a `ShipRender` frame. | `ShipDrawTests` green; the whole-bitmap comparisons unchanged. | 4 |
| **M4-c Decisions** | `TACTICS`, `DOCKIT` and `MLOOP` parts 1–4 return `Decision`s applied by one function; the sixteen `tactics` mutants re-anchored and re-run to zero survivors. | `TacticsTests` green; `mutate.py --unit tactics` at the recorded tally. | 5 |
| **M4-d Mode machine polish** | The mission sub-machine, the death sequence and the pause as explicit states; `LoopOutcome` retired. | Replay hashes unchanged. | 2 |

### Phase M5 — Polish and the ledger

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M5-a Strong types** | `View`, `SoundEffect`, `Message`, `Colour`, the option toggles as an `Options` struct (the thirteen become fields; `DKS3` walks a `constexpr` array of member pointers so the order stays the only definition). | Green; `out-params` at zero. | 3 |
| **M5-b constexpr data** | The generated tables as `constexpr std::array`; the codecs, the trig lookups and the token decoder evaluated at compile time where the tests can `static_assert` a known value. | `TableTests` green; one `static_assert` per table against the oracle-checked value. | 2 |
| **M5-c The ledger** | The <!--count:inventory-stale-files-->21 file names in `Source-Inventory.md` that name no file on disk corrected; `inventory.py` gains `--check-homes` so it cannot happen again. | In CI. | 1 |
| **M5-d ADR-006 and the tidy checks** | ADR-006 (modernisation architecture, written at M2's opening) amended from what was built; `.clang-tidy` widened one `modernize-` check per commit (Q8). | Accepted; `WarningsAsErrors` still `'*'`. | 2 |

### Phase M6 — Detach (owner ruling, §1 R-a to R-d)

| Slice | Scope | Acceptance | Sittings |
|---|---|---|---|
| **M6-a Coverage review and the recorder** | Every *Port* row of the ledger has a test that calls it (the review is the ledger's last job); the `Oracle` seam of §4.10; `RecordingOracle` writes `Tests/Fixtures/*.oracle`; the record-size threshold measured and written here. | The suite runs green through the recorder on both legs and the fixtures are committed; a second recording run produces identical files. | 3 |
| **M6-b Fixtures answer** | `RecordedOracle` serves the suite; `LiveOracle` and the BeebAsm steps leave CI; `OracleIsPresent` retired; `mutate.py`'s oracle check removed; `extract_tables.py --check` becomes a digest of the generated files. | Green on both legs with no assembler installed and the submodule uninitialised; the five mutation units at their M0-d tallies. | 2 |
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
