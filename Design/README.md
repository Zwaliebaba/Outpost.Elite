# Design/ — Outpost: Elite

**Status:** opened 2026-09-02. **Phases 0 to 5 are built as of 2026-09-05**: the kernel, the whole
docked game, flight with its 3D pipeline, the sound and music, the ship AI and the autopilot, the
explosions, the main game loop with hyperspace and the spawning rules, the pause screen with its
thirteen option toggles (removed again 2026-09-08 by owner ruling, below), and — closing phase 4 — the three missions and the Trumbles are all ported
and compared against the assembled original. The executable launches, flies, fights, docks, takes a
briefing and dies. **There is no recorded mutation debt left**: the ship AI's thirteen survivors are
closed and `python tools/mutate.py --unit tactics` is 16 of 16 — the last one was a defect, `TA7`'s
first `BCC` jumping to part SIX so that a ship above half energy never launches a missile (plan
§6.152, §6.153) and so are the hyperspace jump's last two, so **every recorded mutant in the corpus
is caught or a proved equivalent** (§6.156). **What is left needs a person, not a slice**: one
hand-checked screenshot for the presenter, which is the only part of the picture no test reaches,
and a written answer from the rights holders for 0e. Plan §1.2 has them. Two items left that list on
2026-09-06 — ADR-005 §1's raster effects, built as slice 4f, where the hyperspace tearing turned out
not to exist in this build at all (§6.155); and the missing goldens, three of which were already
covered by whole-bitmap oracle comparisons and the fourth of which is a sound hash now (§6.156).
**The modernisation of the PROGRAM is under way in [Modernize.md](Modernize.md), and its
phases M0 to M5 are built, 2026-09-06/07**: typed data, value-in value-out routines, `Universe` and
`Game` over the ports, the flight frame and `LL9` as pipelines of named stages, and the ledger and
the ADRs brought back into agreement with the tree. **The M6-0 gate closed 2026-09-07** — the eight
things the oracle could pin and nothing would pin after it is recorded, among them the interpreter
banking the I/O page so the start sequence runs on both machines, a whole frame with an explosion in
it, the replay reaching death and the escape pod, a coverage instrument CI reads against the
ledger's *Port* rows, and a mutant floor of fourteen files. **M6 CLOSED on 2026-09-08, and [ADR-009](ADR/ADR-009-detachment.md) is what it decided.** M6-a built the recorder and measured the corpus at 3.2 million calls; M6-b was scoped to commit a fixture from it. The owner asked "why bother?", read the corpus's recommendation to build one, and ruled the other way — so the oracle went with nothing put in its place. `Cpu6502`, `Oracle`, `OracleImage`, `Upstream/`, `MasterFile/`, `labels.py` and `inventory.py` are deleted, the `// 6502:` markers reached zero, and the suite went 469 tests to 131. What pins the port now is ADR-009 §2's four instruments, and a fresh clone needs a compiler and nothing else. Beside it, [Resolution.md](Resolution.md)'s RS-0 to RS-6 ran (2026-09-07/08) and the 640×400 picture is built. **Input and time, 2026-09-08**: six slices of
[InputTimer.md](Archive/InputTimer.md) are built beside M6-a — the pause screen removed and its thirteen
settings given a file, `TT217` ported as `Elite::ReadKey` so a held key is one press, the fire key
no longer selecting a joystick the port cannot read, the crowded end and the docked pass measured
while the oracle was still here, and the docked pass running `MLOOP` whole (its §9). **The suite needs nothing but the
repository** since M6-b-7 -- no assembler, no submodule, no assembled game. The suite is
**<!--count:tests-->146 tests** and
CI runs **<!--count:checks-->thirteen repository checks** beside it.

**2026-09-09.** Three things landed on top of M6. [Rendering.md](Archive/Rendering.md) opened, was ruled and
part-built: option A, a three-slice track, and RN-0's gate in the tree. Validating it against the
detached tree found that **ADR-008 §3 had been publishing an assertion as a proof for a month** --
the replay run with the twins present and absent, which no build configuration ever had -- so
[Resolution.md](Resolution.md) §8.4 is built at last, its R26 is closed, and §5.3's scanner sweep is
written; that document is down to its last open item, the dashboard artwork, which is the owner's.
[InputTimer.md](Archive/InputTimer.md)'s I-6 is built beside them, which unblocks I-2 by giving it the
predecessor its gate had been naming since the oracle deleted it.

**2026-09-09, later — one design for the platform.** The owner asked for the designs covering
rendering, input and time to be replaced by one, for what they held that is obsolete to move to
`Design/Archive/`, for the game to become more reactive without a change to its look or feel, and
for the duplicate code to go. [Platform.md](Platform.md) is the one design; `Rendering.md` and
`InputTimer.md` are in [Archive/](Archive/README.md) whole, with what was still live in each carried
forward in its §1. Nothing in it is built. Its §0 answers the three questions the request raised —
which parts of "more reactive" are free and which is a ruling; that the `2x` files are the 640×400
twins and not old copies, and that the duplication the owner is seeing is the canvas drawn every
frame and never shown; and what one design changes that two could not — and its §4 puts four
decisions to the owner, with a recommendation against each. **Ruled the same day, with four more on the
ADRs** (Platform.md §12): every ADR the rulings touch was amended on 2026-09-09, and the largest of
them — the canvas retired, the float ban scoped by role, a fixed-rate flight model as the first
phase-6 option with the faithful cadence selectable — are in ADR-001 §1 and §4, ADR-002 §1 and §4,
ADR-005 §2 to §4 and ADR-007 §4.

The task this corpus planned: take the annotated 6502 source of **Commodore 64 Elite** that sat
under `MasterFile/` until M6-f removed it, and produce a modern C++ port of the game inside the
`Outpost` solution, on the same engineering conventions as the sibling repositories
(Outpost.Frontier, Outpost.Warzone).

## The one finding to read first

`MasterFile/` held the **12 master files** of Mark Moxon's annotated C64 Elite source (5,577
lines), and it was deleted at M6-f on 2026-09-08 along with `Upstream/`. The numbers here are
history and no longer carry checked markers. Those masters were almost entirely `INCLUDE` lines:
they pulled in **710 distinct library files** plus the font binary, and the routine bodies, ship
blueprints and token tables all lived in those includes rather than in the masters. They were not
in this repository either.

**The count used to read "13 master files ... 5,615 lines" and that counted the FOLDER**, not the
source: upstream's own `README.md` sits beside the twelve `.asm` files and is 39 lines of
Markdown. `inventory.py` had been printing twelve since the day it was written. Both numbers were
marked and checked by `tools/check_counts.py` until M6-f deleted the folder they counted; the
thirteen-file figure remains the right one for the licence exposure, which is every file the
history still carries, and ADR-001 §5 says so there.

**Slice 0a fixed that**: the upstream tree sat at `Upstream/elite-source-code-library`, pinned
at commit `aa3f7ee`, as a **submodule** rather than a copy, and all 712 include paths resolved.
That is over: M6-f removed the submodule, the masters and the tools that read them, so a fresh
clone needs nothing but a compiler. See
[Elite-Conversion-Plan.md §1](Elite-Conversion-Plan.md#1-what-we-actually-have).

## Reading order

| | Read | For |
|---|---|---|
| 1 | this file | what exists and where the decisions are |
| 2 | [Elite-Conversion-Plan.md](Elite-Conversion-Plan.md) | the inventory of what we have, the target architecture, the phased build order with acceptance criteria, and the verification strategy. **The only document that sequences.** |
| 3 | the ADRs below | the decisions the plan rests on. **The ADR wins on *what*, the plan on *when*.** |
| 4 | [Risk-Register.md](Risk-Register.md) | what is most likely to go wrong, and where each risk is validated early |
| 5 | [Modernize.md](Modernize.md) | **the modernisation plan** (opened 2026-09-06; M0–M5 built 2026-09-06/07, the M6-0 gate closed 2026-09-07, M6-a ready and the resolution track running ahead of it): what the port carried from the 6502 as its architecture, measured and ratcheted; the target C++ shape; six phases of slices, each gated on the oracle; and the owner's rulings on its eight questions — including the one that ends it: Phase M6 detaches the port from the original, replacing the oracle with recorded fixtures and removing `MasterFile/`, `Upstream/`, the markers and the assembly from the tree. Reads after the plan, because it starts where the plan's build order ends. |
| 6 | [Resolution.md](Resolution.md) | **the 640×400 picture, BUILT 2026-09-07/08 in seven slices** (RS-0 to RS-6; the design proposed 2026-09-07 with eight owner rulings taken the same day, and the journal records a dozen places measurement moved it): the executable presents a second, 640×400 rendering of the same frame — the space view at twice the line resolution, the dashboard redrawn at twice its detail, 8×8 text on an 80×50 grid with every docked screen re-flowed — drawn beside the C64 canvas by twins of the drawing routines, while the canvas stays the view every oracle test, golden and fixture reads. **ADR-008 is what it decided**; read this for how it got there, including five improvements declined with the measurement that declined them. Reads after Modernize.md, because it obeys that plan's rules and sequences around its M6. |
| 7 | [Platform.md](Platform.md) | **rendering, input and time as one design** (opened 2026-09-09; nothing in it is built). It replaces `Rendering.md` and `InputTimer.md`, both moved whole to [Archive/](Archive/README.md), and its §1 says what was still live in each and where it went. One loop that presents from one place; one integer clock at the machine's rate with a simulated vertical blank, auto-pause and a time-clamped backlog; an `InputFrame` per step on a layered, scan-code map; a backdrop and a frame surface with the boundary at every present; the docked screens as coroutines; the SID on its own thread under the same blank. Every slice's gate is the picture the tree draws today, frame for frame. **Its four decisions and four more on the ADRs were RULED the day it opened (its §12)**: the C64 canvas — drawn every frame and never seen — is retired at its last slice; the blocking reads become coroutines; the sound interrupt is clocked by the simulated blank; the float ban is scoped by role; and the flight step's rate is a game-logic change — a fixed-rate flight model as its own track after this one, faithful cadence selectable — rather than a knob. Nine ADRs carry the amendments. |
| 8 | [Platform-Build.md](Platform-Build.md) | **the build plan for Platform.md, written for an agent** (opened 2026-09-09; nothing built): every slice of that design's §5 in a fixed order — T-1, T-3, RN-0, RN-1, RN-2, I-2, I-4, T-4, RN-6, P-0 — as files, signatures, tests, gate commands, the numbers to update and the entry to journal, under one protocol (§0) and one report shape (§4). Two things the plan settled that the design had worded otherwise are said in the slices that own them: RN-0's split composites the two surfaces by exclusive-or rather than copying one into the other, and RN-1's frame boundary is a library call the replay can make. |
| 9 | [Archive/](Archive/README.md) | **superseded designs, kept whole.** `Rendering.md` (the erase-by-exclusive-or analysis, option A ruled, RN-0's gate built) and `InputTimer.md` (input and time, seven of thirteen slices built) went here on 2026-09-09 when Platform.md replaced them. Their findings, journals and slice names are the record and are not maintained; code comments still cite them by their old names. |

## Decisions at a glance

| ADR | Question | Decision (one line) |
|---|---|---|
| [001](ADR/ADR-001-scope-and-fidelity.md) | Scope and fidelity | **Port the C64 game as it is, bit-faithful in logic, before changing anything.** GMA85 variant as configured in `elite-build-options.asm`; the assembled original running in an emulator is the reference. Modernisation is a later phase with its own decisions. |
| [002](ADR/ADR-002-numeric-model.md) | Numeric model | **8-bit integer semantics preserved exactly** — same widths, same wraparound, same lookup tables, same RNG — in the space view's 256×144 logical coordinates, on a canvas that holds the C64's own multicolour bitmap and cell-colour planes and resolves to 320×200 indices at the presenter seam (§4, amended 2026-09-03). No floats in game logic. |
| [003](ADR/ADR-003-verification.md) | Verification | **A 6502 oracle in the test project** ran the assembled original's routines and the C++ port on the same inputs; **golden canvases** for screens; **replay hashes** for whole-game determinism. §1, §2 and §4 are history since M6 deleted the oracle and both goldens — **ADR-009 is what replaces them**; §3, the replay hashes, is untouched and is now the centre of the method. |
| [004](ADR/ADR-004-projects-and-layout.md) | Projects and layout | **Our own codebase — nothing lifted from a sibling repository.** `GameLogic` (namespace `Elite`) holds the port, platform-free and deterministic; presentation lives in `Outpost.exe`; tests under `Tests/`. Flat folders, unique PascalCase names, generated data tables checked in; `MasterFile/` and `Upstream/` were reference only and left the tree at M6-f. |
| [005](ADR/ADR-005-presentation.md) | Presentation | **Packaged Win32, no XAML: MSIX stays, WinUI 3 goes.** Raw window, flip-model D3D12 swap chain blitting the indexed canvas at integer scale, XAudio2 with a small SID-style synthesiser. Amended 2026-09-08 (§3, §4): the pause screen removed by owner ruling, the blocking read ported as `TT217`, a joystick only when the platform has one, the crowded end and the docked pass measured. |
| [006](ADR/ADR-006-modernisation-architecture.md) | Modernisation architecture | **The port becomes a C++ program with no behavioural change**: typed structs with byte codecs, value-in value-out routines, `Universe` and `Game` over four ports, pipelines of named stages, the oracle as judge until M6 deleted it (ADR-009) — the architecture [Modernize.md](Modernize.md) builds toward, recorded at M2's opening and amended as phases land. |
| [007](ADR/ADR-007-state-ownership.md) | State ownership and the replay hash | **`Universe` owns every byte of game state; `Game` owns the dispatch; the executable owns the clock.** Written at M3's close from what was built, including the three places ADR-006 §4 was wrong: three `Step`s rather than one, the count of passes outside because it is floating point, and `Mode` deferred to M4-d. Records what the replay digest does and does not cover, and that it did not move across the phase. |
| [008](ADR/ADR-008-the-picture.md) | The 640×400 picture, as built | **The executable presents a 640×400 `Elite::Picture`; the C64 canvas stays as the verification view and is never replaced.** Written at the resolution track's close from what RS-0 to RS-6 built. Records the twin rule and its four clauses, the five kinds of evidence that stand in for an oracle that cannot judge this surface, the layouts as the owner accepted them, and five improvements declined with the measurement that declined each. |
| [009](ADR/ADR-009-detachment.md) | Detachment: what pins the port now | **The 6502 oracle, the assembled original and the tools that read it are deleted; 337 of 469 tests went with them.** Written at M6's close from what was built, on an owner ruling taken against this corpus's recommendation. Names the seven instruments that pin behaviour now, what a recorded digest is and what changing one means, and — at length, because a record of only what remains would be worse than none — exactly what was given up. |

## Two things this corpus is deliberately not

- **Not a feature design for phase 6.** Higher resolution, smoother motion, gamepad, new UI: all
  real, all later, all gated on the faithful port passing its tests — which until 2026-09-08 meant
  the oracle and the goldens (ADR-001 §4), and now means ADR-009 §2's four instruments. Building
  toward a nicer game before the original one runs is the failure mode the plan is shaped to avoid.
  **What the corpus does hold, since 2026-09-06, is a modernisation plan for the PROGRAM rather
  than the game**: [Modernize.md](Modernize.md) restructures the code with no behavioural change
  and its own ratchet (`tools/check_modernize.py`), and every one of its slices was gated on that
  same oracle while it lasted. The gate ADR-001 §4 set is met; that document's eight questions were
  ruled on 2026-09-06 and its last phase removed the original from the tree. **Two of the later
  documents are past that gate**: [Resolution.md](Resolution.md) is built, and
  [Rendering.md](Archive/Rendering.md) is ruled and unbuilt.
- **Not a licence.** The upstream source carries no licence (ADR-001 §5, Risk R1), and the
  owner intends to publish eventually, which makes this the project's largest exposure rather
  than a footnote. Slice **0e** seeks the rights holders' permission. **The repository is
  already public**, by owner ruling on 2026-09-03 that reversed a same-day ruling to make it
  private (Risk R1, realised and accepted rather than mitigated). **M6-f removed both from the tip
  on 2026-09-08**, which changes the exposure without ending it: `Upstream/` was a submodule, so
  none of its content was ever in this history, but `MasterFile/`'s 13 files are still in it, and
  the generated data tables in `GameLogic/` — the font, the tokens, the blueprints, the sine and
  arctangent tables — carry the same copyright at the tip today. ADR-009 §1 calls that the accepted
  residual and says why: the tables *are* the inheritance. What still closes 0e is a written answer
  from the rights holders.

## Conventions

The house rules are in [`AGENTS.md`](../AGENTS.md) at the repository root, written for this
repository in slice 0c from its own `.clang-format`, `.clang-tidy` and `.editorconfig`. In short: PascalCase types,
functions and files; `_param`, `m_member`, `g_global`; `UPPER_CASE` constants; Allman braces,
two-space indent, 140 columns; flat project folders; `/std:c++latest`, x64, v145.

**Until M6-e, every ported function carried the original label in a comment on its declaration
(`// 6502: LL9`), and `Source-Inventory.md` was the table that said where each label went.** Both
are gone: the markers reached zero at M6-e-4, the ledger and `tools/inventory.py` were deleted at
M6-e-1, and AGENTS.md §1 R7 is marked RETIRED. The original's *names* stay in the commentary by
owner ruling (Modernize.md §1 R-b), so "where did `TACTICS` part 4 end up" is still a grep — of the
prose rather than of a ledger. [ADR-009](ADR/ADR-009-detachment.md) is the record of what replaced
all of it.
