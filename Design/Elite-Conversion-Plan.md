# Elite Conversion Plan — 6502 to modern C++

**Status:** Accepted · 2026-09-02 · **revised in place** (this document is the sequence of
record; the ADRs decide *what*, this decides *when*).
**Owner decisions:** all five taken on 2026-09-02, and four more on 2026-09-03. All recorded in §8.
**Phase 2's computational content is complete and verified; what remains of it is the input
layer and the shell.** 2a and 2b are done. 2c has its price model, its trade arithmetic, its
market screen and `gnum`; its remaining screens are loops around the keyboard. 2d has the
commander block, both checksums and the save format; its file I/O and name entry read the
keyboard. **2e's decisions are settled and it still needs a Windows machine** — Risk R3 is
answered (count cycles, and the counter is built; §6.17 explains why the question was not the one
being asked) and its verification is split between a CI replay-hash leg and a human sign-off, but
its criterion is that a person can play it and nothing here can launch a window.

**Phase 1 is closed.** 1c-c-b built 2026-09-03: twenty of the thirty-one extended control codes
are ported and compared against the shipped dispatch, the justification line buffer with them,
and the three that reach the canvas stay a counted seam. §6.11 records what the drawing slices
cost and §6.12 what the last one was hiding.
**Phase 0 is closed**, bar slice 0e — which is owner action, is overdue, and turned out to have
been breached before it was written: the repository is public and has tracked `MasterFile/` since
`92a3c7f`. Ruled 2026-09-03: make it private. **Nothing in this project can do that; the owner
must.** The rest: 0a (upstream referenced, §6.9), 0b-a (the
assembler and label map), 0c (skeleton, the 6502 interpreter), 0f (the determinism guard) are
built; 0b-b is cancelled and 0d deferred, both by owner ruling. **Phase 1 is under way**: 1b-a
and 1b-b have ported eighteen arithmetic routines. **The oracle is live**, which is the gate
phases 1 to 4 were waiting on.

---

## 0. Summary

The goal is a C++ port of Commodore 64 Elite, running as `Outpost.exe` inside this solution,
that plays exactly like the original before it plays any better. The plan is built on three
ideas:

1. **The original binary is the oracle.** The assembled 6502 game, executed by a small 6502
   interpreter inside the test project, tells us what every arithmetic, text, universe and
   drawing routine is *supposed* to return. Each ported routine is tested against it on the same
   inputs. This turns "does it feel right" into "does it match", routine by routine (ADR-003).
2. **Game logic is a platform-free, deterministic library.** Everything that was 6502 code
   becomes `GameLogic` (namespace `Elite`): no window, no GPU, no clock, no file system. It draws
   into an in-memory 320×200 indexed canvas and emits sound-register writes. The executable
   presents that canvas and plays those sounds (ADR-002, ADR-004, ADR-005).
3. **Port in the order the game can be played.** Data and arithmetic first (testable with no
   screen), then the docked game (charts, market, equipment, saves — a playable build with no
   flight), then flight and drawing, then combat and the wider universe, then sound. Each slice
   ends in something that runs.

The work is sized in [§6](#6-the-build-order); the coverage ledger is
[Source-Inventory.md](Source-Inventory.md).

---

## 1. What we actually have

### 1.1 `MasterFile/` — 13 master files, 5,615 lines, and 710 files that are not here

| File | Lines | What it is | Port disposition |
|---|---|---|---|
| `elite-source.asm` | 1,498 | The main game. Configuration constants (ship type numbers, key codes, colour bytes, sound numbers, memory map) then **627 `INCLUDE` lines** that pull in every routine, split into the eleven code blocks ELTA–ELTK | **Port** (via its includes) |
| `elite-data.asm` | 283 | Recursive token table, sine/arctan tables, extended token tables, ship blueprints (36 ships) — **58 `INCLUDE`s** plus an `INCBIN` of the font `C.FONT.bin` | **Port as generated data** |
| `elite-loader.asm` | 325 | The game loader: zero page setup, the dashboard bitmap (`dials.asm`), the colour maps (`sdump`, `cdump`), sprite definitions, date string, plus seven loader code parts | **Extract assets; code not ported** |
| `elite-sprites.asm` | 84 | Sprite definitions (`spritp.asm` via macros) | **Extract asset** |
| `elite-gma1.asm` | 1,550 | Disk fast loader | Not ported |
| `elite-gma2.asm` | 86 | Empty loader stage | Not ported |
| `elite-gma3.asm` | 300 | Disk protection (disabled) | Not ported |
| `elite-firebird.asm` | 274 | BASIC autoboot | Not ported |
| `elite-send.asm` | 701 | PDS transfer utility | Not ported |
| `elite-checksum.asm` | 325 | Reference for `elite-checksum.py` | Not ported (checksum *logic* the game applies to saves is — see `CHK` in the inventory) |
| `elite-readme.asm` | 146 | Disk README generator | Not ported |
| `elite-build-options.asm` | 5 | `_VERSION=8` (C64), `_VARIANT=1` (GMA85 NTSC), `_MATCH_ORIGINAL_BINARIES=TRUE`, `_MAX_COMMANDER=FALSE` | Fixes which variant we port (ADR-001) |
| `README.md` | 38 | Moxon's index of the above | — |

**The include tree was absent, and is now referenced.** The `INCLUDE` paths are of the form
`library/<lineage>/main/<kind>/<name>.asm` and
`versions/c64/1-source-files/main-sources/elite-build-options.asm`, which is the layout of the
upstream repository **`markmoxon/elite-source-code-library`**. Slice 0a put it at
`Upstream/elite-source-code-library` as a **submodule** pinned at commit **`aa3f7ee`**
(2026-09-01) — see §6.9 for why that word matters and what it cost to discover — and proved the
acceptance criterion mechanically:

| Check | Result |
|---|---|
| Distinct `INCLUDE`/`INCBIN` paths in the masters | 712 (710 under `library/`, plus the build options and the font) |
| Resolved against the vendored tree | **712 / 712** |
| The 13 masters compared byte-for-byte with their upstream copies | **identical** |

`tools/inventory.py --check-includes` is that check, and it runs in a second.

Breakdown of the 710 missing files by lineage — this matters because it says how much of the
game is shared with the BBC versions (where bbcelite.com's deep-dive articles apply verbatim)
and how much is C64-specific:

| Lineage | Subroutines | Variables / tables | Macros | Workspaces | Total | Meaning |
|---|---|---|---|---|---|---|
| `common` | 363 | 33 | 8 | 4 | **408** | Identical across every Elite version — the core game |
| `enhanced` | 55 | 36 | 6 | 1 | **98** | Shared by the disc/6502SP/Master/C64 "enhanced" versions: extended tokens, missions, extra ships |
| `advanced` | 12 | 13 | — | 1 | **26** | Shared by 6502SP/Master/C64 |
| `master` | 8 | 8 | — | — | **16** | Shared with the BBC Master version (the C64's direct ancestor) |
| `6502sp` | 6 | 1 | — | — | **7** | Borrowed from the second-processor version |
| `c64` | 91 | 55 | 4 (sprites) | 4 | **155** | C64-only: VIC-II drawing, SID sound and music, sprites, keyboard, Kernal disk I/O, Trumbles |

The C64 game is the BBC Master lineage with a C64 platform layer of ~155 files. About 78% of
the routine bodies are the same code every Elite has, which is why an oracle-driven port is
tractable: most of it is well-documented integer arithmetic and state machines with no
hardware in them.

### 1.2 What the solution contains today

- `Outpost.slnx` with two projects and three platforms (x64, x86, ARM64).
- `NeuronCore/` — static library, C++20, v145. Two headers: `NeuronCore.h` (the shared PCH
  content: STL, Win32, Winsock, C++/WinRT base) and `Debug.h` (`DebugTrace`, `Fatal`,
  `ASSERT` family). Nothing else yet.
- `Outpost/` — a **WinUI 3 / Windows App SDK 2.4 desktop app template**, MSIX single-project
  packaged, C++/WinRT 3.0, with `packages.config` pulling WebView2, AI/ML, Widgets, Search and
  DWrite packages. It has `pch.h/.cpp` and assets only: no `Main.cpp`, no XAML page. It
  references `NeuronCore`. ADR-005 recommends replacing this shell (owner decision, §8).
- `.clang-format`, `.clang-tidy`, `.editorconfig` — copied from Outpost.Frontier. They cite an
  `AGENTS.md` and a `Build/` folder that do not exist here yet.

### 1.3 What the sibling repositories give us

**Owner ruling, 2026-09-02: none of it is used.** The codebase is this project's own
(ADR-004 §1). The sibling trees are named here only so that a later reader knows they were
considered and deliberately not drawn on:

- **Outpost.Frontier** and **Outpost.Warzone** — same family, same conventions, and a
  `NeuronClient` project containing a window, a D3D12 device and an XAudio2 device that an
  earlier draft proposed lifting. Not lifted. The presentation layer is written here.
- **`~/source/repos/Elite`** — a *previous* port by the owner of the **1987 IBM PC DOS**
  Elite (`ELITEL.EXE`, reverse-engineered x86, 25k lines) to C++/D3D11 with CMake, 8.2k lines,
  eleven phases marked complete in its `migration.md`. It is a different code base (Andy
  Onions' x86 rewrite, 31 ship types, CGA 320×200×4) and does not follow the house style, so
  **none of it is copied**. Three of its decisions are validated here and adopted: CPU-side
  framebuffer uploaded to a GPU texture and blitted at integer scale; integer arithmetic
  preserved rather than floated; golden regression tests over the RNG and all 2,048 systems.
  Its universe output is a useful *second* cross-check for galaxy generation, since both games
  derive from the same seeds.

---

## 2. Target architecture

```
NeuronCore.lib                 foundation (exists): the shared precompiled-header content and Debug.h.
├── GameLogic.lib              THE PORT.  namespace Elite.  Deterministic, platform-free.
│                              Input: InputFrame.  Output: Canvas (320x200 indexed) + SoundEvents.
└── Outpost.exe                composition root AND presentation, all written here: Main, AppConfig,
                               Window (raw Win32), GpuDevice + swap chain (D3D12), CanvasPresenter,
                               SidSynth (sound events → XAudio2), KeyMap (VK → Elite keys),
                               SaveStore (commander files).  Packaged MSIX, no XAML (ADR-005 §5).
Tests/GameLogicTests.dll       CppUnitTest.  Cpu6502 oracle, arithmetic/token/universe suites,
                               golden canvases, replay hashes.
tools/                         inventory.py (coverage ledger), extract_tables.py (asm → C++ data),
                               labels.py (BeebAsm listing → label addresses), golden_diff.py, check_gamelogic.py
MasterFile/                    the 13 masters.  Reference only.  Never compiled, never included.
Upstream/                      the upstream tree as a SUBMODULE, pinned at aa3f7ee.  Never edited, never compiled.
                               A fresh clone needs "git submodule update --init" before anything builds.
```

### 2.1 The seam: what `GameLogic` looks like from outside

```cpp
namespace Elite
{
struct InputFrame            // one iteration's worth of keys, already logical (KeyMap did the VK work)
{
  std::uint64_t held;        // bit per Elite key (KYTB order); the port keeps the original polling model
  std::uint64_t pressed;     // edges, for the screens that read a single key
};

class Game                   // 6502: the whole of ELTA..ELTK
{
public:
  void Reset();                                   // 6502: RESET / BEGIN / BR1 flow
  void Step(const InputFrame& _input);            // one main-loop iteration (flight or docked)
  [[nodiscard]] const Canvas& Frame() const;      // 320x200, one byte per pixel, C64 colour index
  [[nodiscard]] std::span<const SoundEvent> Sounds() const;   // register-level SID writes this step
  [[nodiscard]] std::uint64_t StateHash() const;  // for replay determinism (ADR-003 §3)
  ...
};
}
```

`Step` is one iteration of the original's main loop. The original's loop has no fixed period;
its rate depended on how much was on screen. §5.3 says how the executable paces it.

### 2.2 Module map (files in `GameLogic/`, flat, PascalCase — ADR-004)

Only the shape; the full label-by-label mapping is in
[Source-Inventory.md](Source-Inventory.md).

| Area | Files | Original |
|---|---|---|
| Kernel | `EliteTypes.h`, `Arith.h/.cpp`, `Rng.h/.cpp`, `LookupTables.h`, `SineTable.cpp`, `ArctanTable.cpp`, `LogTables.cpp` | zero-page workspace, `MULT*`, `MULTU`, `FMLTU`, `DVID*`, `LL28`, `ARCTAN`, `SQUA`, `DORND`, `SNE`, `ACT`, `LOG`/`ANTILOG` |
| Text | `Tokens.h/.cpp`, `ExtendedTokens.h/.cpp`, `TokenTables.cpp`, `TextPrint.h/.cpp`, `Font.cpp` | `QQ18`, `TT27`, `TKN1`, `DETOK`, `MT1`–`MT29`, `TT26`/`CHPR`, `DTW*`, `BPRNT`, `C.FONT.bin` |
| Canvas | `Canvas.h/.cpp`, `Lines.cpp`, `Circles.cpp` | `LOIN` 1–7, `HLOIN`, `PIXEL`, `CPIX*`, `CIRCLE`, `CIRCLE2`, `BLINE`, `TT66`/`BOX`, `CLYNS` |
| Universe | `Universe.h/.cpp`, `SystemData.h/.cpp`, `Charts.h/.cpp` | `TT20`, `TT54`, `TT24`, `TT25`, `cpl`, `TT111`, `TT22`, `TT23`, `TT18` |
| Commander | `Commander.h/.cpp`, `Market.h/.cpp`, `Equipment.h/.cpp`, `SaveBlock.cpp` | `NA%`, `QQ23`, `TT151`, `var`, `TT219`, `TT210`, `EQSHP`, `prx`, `qv`, `CHK`/`CHK2`/`CHK3` |
| Screens | `Screens.h/.cpp`, `StatusScreen.cpp`, `MarketScreen.cpp`, `ChartScreens.cpp`, `TitleScreen.cpp` | `STATUS`, `TT167`, `TT213`, `TT22`/`TT23`, `TITLE`, `TT167`… |
| Ships | `ShipBlueprints.h`, `ShipBlueprintData.cpp`, `ShipSlot.h`, `Bubble.h/.cpp` | `XX21`, `VERTEX`/`EDGE`/`FACE`, `INWK`/`K%`, `FRIN`, `MANY`, `UNIV`, `NWSHP`, `KILLSHP`, `ZINF` |
| Motion | `ShipMove.h/.cpp`, `Orientation.cpp` | `MVEIT` 1–9, `MVS4`, `MVS5`, `MVT*`, `TIDY`, `MAS*`, `NORM` |
| Drawing | `ShipDraw.h/.cpp`, `LineHeap.h`, `Planet.cpp`, `Sun.cpp`, `Stardust.cpp`, `Explosion.cpp` | `LL9` 1–12, `LL145` 1–4, `SHPPT`, `PLANET`, `PL9`, `PLS*`, `SUN` 1–4, `STARS*`, `DOEXP` |
| Flight | `FlightLoop.h/.cpp`, `Dashboard.h/.cpp`, `Scanner.cpp`, `Lasers.cpp`, `Docking.cpp` | main flight loop 1–16, `DIALS` 1–4, `DILX`, `COMPAS`, `SCAN`, `MSBAR`, `LASLI`, `HITCH`, `LAUN`, `DOCKIT` |
| AI & combat | `Tactics.h/.cpp`, `Missiles.cpp`, `Ecm.cpp` | `TACTICS` 1–7, `ANGRY`, `FRMIS`, `SFRMIS`, `ECMOF`, `OOPS`, `DEATH` |
| Game loop | `GameLoop.h/.cpp`, `Hyperspace.cpp`, `Spawn.cpp`, `Missions.cpp`, `Trumbles.cpp` | main game loop 1–6, `hyp`, `MJP`, `ghy`, `BRIEF*`, `DEBRIEF*`, `TBRIEF`, `MVTRIBS`, `TRIBTA` |
| Input | `EliteKeys.h`, `KeyPoll.cpp` | `KYTB`, `KEYLOOK`, `RDKEY`, `DOKEY`, `CTRL`, `DKS*` |
| Sound | `SoundEvents.h`, `SoundEffects.cpp`, `Music.cpp`, `TuneData.cpp` | `NOISE`, `BEEP`, `EXNO*`, `sfx*` tables, the `BD*` music player, the tune blocks |
| Top | `Game.h/.cpp` | `BR1`, `BAY`, `RESET`, `RES2`, `DEATH2`, `TT170` |

Sizing rule of thumb from the inventory: ~520 routine-ish files become ~45 C++ files. A typical
6502 routine of 30–80 lines becomes 10–40 lines of C++; the port will land around **25–35k
lines** of `GameLogic` including comments carried over, plus ~8k of generated data.

---

## 3. Verification strategy (summary of ADR-003)

Three instruments, each cheap to keep running:

| Instrument | What it proves | Lands in |
|---|---|---|
| **6502 oracle** — a `Cpu6502` interpreter in `GameLogicTests` loads the assembled ELTA–ELTK, WORDS, IANTOK and SHIPS binaries at their original addresses, sets zero page / registers, `JSR`s a labelled routine, and returns the resulting memory and registers. Each ported pure routine is compared against it over exhaustive or sampled inputs. | Arithmetic, RNG, tokens, universe generation, market, checksums, ship transforms, line clipping — the ~78% that is hardware-free | Slice 0c (the interpreter), then every slice |
| **Golden canvases** — the port's 320×200 canvas after a scripted sequence, hashed and stored; first goldens taken by comparing against emulator screenshots by eye, then frozen | Screens, dashboard, planet/sun/ship rendering | Slice 1d onward |
| **Replay hash** — a scripted `InputFrame` sequence run twice (and across Debug/Release) must yield identical `StateHash` per step | Whole-game determinism, the precondition for everything above staying meaningful | Slice 2e onward |

The reference binaries are built with BeebAsm from the upstream tree (`make encrypt=no
match=no`), which is how label addresses are obtained too. They are **not committed** (ADR-001
§5); the tests read their location from `Tests/GameLogicTests/Oracle.json` and report
*skipped, oracle absent* — loudly — when it is missing.

---

## 4. Principles for the porting work itself

1. **One routine, one function, one comment.** Every function names its 6502 label. Where a
   6502 routine has multiple entry points (`TT26`/`CHPR`, `hy6`/`docked`), each entry point is
   a function, and the shared tail is a private helper.
2. **Keep the original's data model until the oracle is green, then and only then tidy.** The
   37-byte `INWK` ship block becomes a `struct ShipSlot` whose fields are the 6502 names
   (`x_lo`, `x_hi`, `x_sgn` → `x` as a signed 24-bit sign-magnitude helper type), not a
   redesigned entity.
3. **Zero page becomes a struct, not globals.** `ZeroPage` and `Workspace` structs owned by
   `Game`. It is fine for them to be large and ugly; they are the original's memory map with
   names. A later slice can move fields to where they belong once tests pin behaviour.
4. **Self-modifying code and stack tricks are documented at the call site.** The original
   patches operands (e.g. the line-drawing routines) and pops return addresses in places. The
   port replaces each with a named parameter or an explicit state variable, and says so.
5. **Hardware is emulated by effect, not by mechanism.** VIC-II hardware sprites (scanner
   blips, the laser sights, Trumbles), the raster interrupt that recolours the dashboard, and
   the multicolour bitmap's per-cell colour constraints all become ordinary canvas drawing
   that produces the same pixels. The SID becomes a sequence of register writes consumed by a
   synthesiser in the executable.
6. **Erase-by-redraw stays where logic depends on it.** The original erases lines by drawing
   them again (EOR). `Canvas` keeps XOR pixel semantics; ship and sun line heaps are ported
   because `LL9` and `SUN` use them to decide *what* to erase, and golden tests compare frames
   at the end of a full iteration, where both approaches agree.
7. **Known original bugs are ported, then listed.** E.g. `NRU% = 0` (Data on System can crash
   for a few systems) ships as-is behind a documented constant, and the fix is a modernisation
   option, not a silent change (ADR-001 §3).
8. **No third-party code.** A 6502 interpreter is ~600 lines and is written here (test-only);
   the SID synthesiser is a small voice model, not reSID (which is GPL). Frontier's AGENTS.md
   §5 rule applies.

---

## 5. Presentation (summary of ADR-005)

### 5.1 Screen

The C64 game draws into a 320×200 multicolour bitmap: a 256×144 space view at the top (the
constants `X = 128`, `Y = 72` are its centre) and a dashboard from character row 18 down, with
its own colour map. The port's `Canvas` is 320×200 bytes of C64 colour index. `CanvasPresenter`
uploads it each presented frame into an `R8_UINT` texture, and a full-screen pixel shader maps
index → palette and samples with nearest filtering at the largest integer scale that fits the
window (4× = 1280×800 on a 1080p display), letterboxed. The exact horizontal placement of the
256-wide view inside the 320 bitmap is confirmed against the emulator in slice 0b.

### 5.2 Sound

Sixteen sound effects (`sfxplas` … `sfxelas2`) are described by tables of SID parameters and
driven by an interrupt-time player; the music player (`BD*`) is a separate interrupt routine
with nine tune data blocks. `GameLogic` ports both players *as state machines that emit
register writes per tick*; `SidSynth` in the executable renders three voices (waveform,
frequency, pulse width, ADSR; filter optional) into an XAudio2 source voice at 44.1 kHz. If
fidelity is not reached, the fallback is recorded samples — a decision for slice 5, not now.

### 5.3 Timing

The original iterates its main loop as fast as the machine allows, so speed and turn rates were
tuned to *that* cadence. Slice 0b measures it in VICE (iterations per second in an empty view,
with three ships, with eight ships). The executable then runs `Step` at a **fixed rate chosen
from that measurement** (expected 10–20 Hz), presenting every step, with vsync. A "variable
rate like the original" mode is a modernisation option. This is the one place where fidelity
cannot be defined by the oracle, and it is called out as Risk R3.

### 5.4 Input

`Window` (from Frontier) produces a key-state table; `KeyMap` in the executable turns virtual
keys into `Elite::InputFrame` bits using the original C64 key assignments as the default map
(`f0`–`f9`, `DINT`, `FINT`, `HINT`, `OINT`, `YINT` and the flight keys in `KYTB`). Remapping is a
modernisation item.

---

## 6. The build order

Slices are small enough to land in one sitting each and every one ends with something that
runs or a test that is green. "Accept" is what has to be true to close the slice. The order
within a phase is the recommended one; slices marked ∥ can run in parallel.

### Phase 0 — Foundations (nothing ported yet)

| Slice | Scope | Accept |
|---|---|---|
| **0a Source acquisition** ✅ **Built 2026-09-02 · repaired 2026-09-03** | `markmoxon/elite-source-code-library` at `Upstream/elite-source-code-library`, pinned at `aa3f7ee` (2026-09-01), per the owner's ruling to hold it here rather than reference a sibling checkout (ADR-001 §5). | **Met, and now met on a clean clone too.** 712/712 include paths and the font binary resolve; all 13 masters byte-for-byte identical to upstream; `tools/inventory.py --check-includes` is the standing check. It reported **0/712 on a fresh clone until 2026-09-03**, because the gitlink had no `.gitmodules` — see §6.9. |
| **0b-a Label map** ✅ **Built 2026-09-03** | Build BeebAsm from source, assemble the C64 variant, and normalise its label dump and load addresses into something the oracle can read (`tools/labels.py`). | **Met.** BeebAsm 1.11 built with the VS 18 toolchain; the variant assembles to all eleven blocks; **1,782 labels** and the 11-block load map exported. The oracle now calls the shipped game by name. |
| **0b-b Emulator measurements** ❌ **Cancelled 2026-09-03 by owner ruling** | *"You can ignore the use of VICE, no need to do a reference run."* No emulator is installed and none will be. | **Dropped, not deferred.** It was gating two things and each now has a different answer, below. |
| **0c Repository skeleton** ✅ **Built 2026-09-02** | `AGENTS.md` written for this repository from its own `.clang-tidy`/`.clang-format`, not adapted from a sibling; `GameLogic` (static lib) and `Tests/GameLogicTests` (CppUnitTest) added to `Outpost.slnx`; `tools/inventory.py` built; `Cpu6502` written and proved. | **Met.** Debug x64 builds clean; 16 tests pass, of which 11 pin the interpreter against published 6502 behaviour and 5 are the first oracle suite. See §6.1. |
| **0d Application shell** ⏸ **deferred to phase 2 by owner ruling, 2026-09-03** | *"Do not strip WinUI, ignore it and proceed."* The `Outpost` project is left exactly as it is, packages and all. Nothing here is done now. | Re-enters the plan at **2e**, which is the first slice that needs a window because it is the first playable build. Until then the port is game logic proved by tests, and the shell is not on the critical path. |
| **0e Permission** 🔴 **Owner action, and overdue** | Approach the rights holders about the intent to publish (ADR-001 §5). ~~Until it closes, nothing is pushed to a public remote.~~ **That clause was already false when it was written**: `Zwaliebaba/Outpost.Elite` is public — confirmed against the GitHub API rather than assumed — and `MasterFile/` has been tracked since `92a3c7f`. Those 13 masters are 5,577 lines carrying "copyright D. Braben and I. Bell 1985" and Moxon's commentary copyright in their own headers. The structural mitigation did hold: `Upstream/` is a **submodule**, so the ~3,000 library files are a gitlink and not content, and no assembled binary is tracked. The exposure is the 13 masters and nothing else. | **Ruled 2026-09-03: make the repository private.** Cheapest complete stop — the oracle build is untouched, no history rewrite, no network dependency in CI — and the publication intent stays intact for this slice to resolve properly. It does not undo what is already public. **This is the one thing in the project no tool available here can do**: repository visibility is changed by the owner at github.com/Zwaliebaba/Outpost.Elite → Settings → General → Danger Zone → Change visibility. Until that is done the slice stays open and Risk R1 stays realised. The written answer from the rights holders is still what actually closes it. |
| **0f Determinism guard** ✅ **Built 2026-09-03** | `tools/check_gamelogic.py`: fails if `GameLogic` names a clock, operating-system randomness, `float`, `double`, file or registry access, or a Win32 call. Comments and string literals are stripped first, so a comment explaining the float ban does not trip the float rule. | **Met.** Passes on the tree. A planted `<chrono>` include and a `double` were both caught, and the tree was clean again afterwards. `--self-test` proves the scanner still detects six violation kinds and correctly ignores four look-alikes, so the checker itself is checked. |

### 6.10 The canvas was designed from an assertion, and the assertion was wrong

**Slice 1d was one slice with a big scope and a quiet assumption underneath it.** ADR-002 §4
said the canvas is "320×200 logical pixels, one byte per pixel holding a C64 colour index".
Nothing in the corpus derived that; it reads like a description of the output, which it is, and
was taken for a description of the storage, which it is not. A half day of measurement before
`Canvas.cpp` rather than after it is what this entry is arguing for.

The C64 screen is a **multicolour** bitmap: 160 double-width pixels of two bits each, with the
colours for `%01` and `%10` in a per-8×8-cell byte in screen RAM and `%11` in colour RAM. The
drawing routines EOR whole **bytes** into it. The decisive measurement is that the C64 build of
`PIXEL` indexes `TWOS2`, whose masks slide by one bit, so three of the eight x offsets set the
low bit of one multicolour pixel and the high bit of the next. **There is no colour index to
store for that write.** Plot at x = 66 and then x = 68 and the byte reads `%01111000` — three
lit pixels in three colours, the middle one a colour neither call asked for. ADR-002 §7 carries
the measurements; ADR-002 §4 now carries the four-plane design that replaces the clause.

Three things worth carrying forward:

- **The spike paid for itself twice over before any code was written.** It also answered
  ADR-005 §1's dependency on the cancelled emulator run — the space view's placement is `0x20`
  in `ylookup`, a four-cell left margin, and the dashboard split is `ylookup[144]` landing on
  character row 18 — which §6.5 had missed when it counted that slice's dependents.
- **Deriving by hand got it wrong; measuring got it right.** `celllook` starts three cells into
  screen RAM while the bitmap's margin is four, which reads as an off-by-one. It is not: `CHPR`
  writes the colour *after* advancing the cursor, so both land on cell `4 + XC`. The hand
  derivation in this project's own notes said otherwise for an hour.
- **1d is now four slices, and 1c-c has an order.** The original 1d bundled the canvas, the
  text printer, the golden harness and the deferred control codes into one sitting, in the one
  area where the oracle mechanism itself was new. Each of 1d-a to 1d-c ends with a byte compare
  against the shipped bitmap.

### 6.11 What phase 1's drawing slices cost, and what the oracle earned

Three defects in 1d-a, none of them visible by inspection, all three found by the same thing:
**comparing the whole screen rather than the pixel the test was about.**

- `Canvas` guarded its accesses with `& (SCREEN_SIZE - 1)`. `SCREEN_SIZE` is `0x2800`, which is
  not a power of two, so the mask silently dropped bit 11 of every address that had it and put
  pixels eight character rows from where they belonged. A test that checked only "the expected
  byte changed" would have passed.
- `PIXEL2` collapsed a carry chain into a constant. There is no `CLC` before the `ADC` that
  negates a downward offset — unlike the x half, which has one — so the carry comes from the
  range check that just failed and the carry that `ADC` produces is what the following `SBC`
  borrows against. The visible consequence is that y1 = 128 and y1 = 129 plot the same row.
- `LOIN`'s steep row step ends with `SBC` and no `CLC`, unlike both shallow paths, so its borrow
  survives into the accumulator on every iteration that crosses a character row. Dropping it
  left the port right for 71 pixels of a 72-pixel line, with one pixel one bit out of place.

All three are Risk R4's shape exactly — 6502 flag semantics leaking — and the third is the
second time in the project that an uncleared carry has been the bug (§6.4 was the first). That is
now a pattern worth naming: **when a routine's structure looks symmetrical and one branch is
missing a `CLC`, the asymmetry is the behaviour.**

Two other things worth carrying forward:

- **`DVID4` has no `RTS`.** It falls into a second, unlabelled copy of `LL28`'s body, and both
  of its callers get it. Checking whether a routine actually returns, before porting it as a
  function, is now part of reading one.
- **The corpus's own scope was wrong twice, and in the same direction.** `1b`'s "same shape as
  the four above, now unblocked" covered two routines that read workspaces phase 1 does not
  define, and 1d-c's `CIRCLE` family reaches the sun line heap and `LL145`'s clipping. Both are
  moved to 3a and 3c rather than forced. A ledger row that names a routine is not evidence that
  its dependencies were checked.

### 6.12 Phase 2 has a dependency chain nobody drew, and it ends at the line buffer

Slice 2a's description looked like the easiest thing in the phase. `PDESC`'s generated path is
four instructions: seed the RNG from the system's own seed bytes, print extended token 5. The
port of it is right and it is two lines long.

It cannot be compared against the game, and the reason is a chain:

**`PDESC` → the extended control codes → the justification line buffer.**

Token 5 reaches **nine** control codes. Five are two-line pokes at the `DTW` state. Two route to
printers that already exist. `MT18` builds a random pronounceable word and needs only the RNG.
And `MT17` prints the system's name as an ADJECTIVE — which it does by writing the name into the
justification line buffer, reading back the last character, dropping it if it is a vowel, and
appending "IAN". `TIBEDIED` becomes `TIBEDIEDIAN`.

That needs `DTW3`, `DTW5` and `BUF` — the justification machinery that slice 1c-c listed under
"`dtw1`–`dtw6`, `dtw8`, `feed`, `dtw*` justification state" and did not build. So the honest
shape is one slice, **1c-c-b**, containing the control codes AND the line buffer, and `PDESC`
lands when it does.

**Built 2026-09-03, and the chain was longer than that.** Two of the nine codes 1c-c-b was
"waiting on phase 2 for" needed nothing of the sort. `MT17` and `MT18` are pure text; what they
needed was the buffer, which was in the same slice. Code 16's self-modified operand is a *value*,
not a dependency — the routines that patch it are the disk catalogue's, and MT16 works without
them. Only three codes genuinely reach outside: 9, 11 and 21, for `TT66`, `NLIN4` and `CLYNS`.

So the scoping note of 2026-09-03 that put nine codes out of reach was wrong on six of them, and
wrong in a way worth naming: it counted a routine as blocked because *something* it touched had
not been built, without asking whether that something was in the slice already. The ledger's
failure mode in §6.12 has a mirror image — a dependency graph read too pessimistically stalls
work as surely as one read too optimistically breaks it.

**This is the fourth time in two phases that a slice's stated scope has not survived contact with
what its routines actually read** — after `DVID3B2`/`LL51`, `CIRCLE`/`BLINE`, and `LL5` going the
other way. The pattern is now clear enough to name: `Source-Inventory.md` is an excellent
coverage ledger and an unreliable dependency graph, because its rows were written from what
routines are *about* rather than from what they *touch*. Before phases 3 and 4 are planned as
sittings, one pass over the ledger asking only "what does this read?" would be worth more than
any amount of re-sequencing.

### 6.18 Two ways a test can be green and blind, both found on the same day

Building the callers of an already-verified routine turned up a defect in the verification rather
than in the code, and running the mutation pass that would have caught it turned up a defect in
how the mutation pass was being run. Neither is about Elite; both are about this project's method,
which is why they are here rather than in a commit message.

**A comparison that could not see what the caller reads.** `gnum` has one exit for the
number-building path, `OUT`, and it begins `LDA #&10 / STA COL2 / LDA R / RTS`. Neither `LDA` nor
`STA` touches the carry, so the carry a caller gets is whichever comparison branched there — set
for the two "too big" tests, clear for everything else. The buy screen's `JSR gnum / BCS TQ4`
branches on exactly that bit.

The port's test swept 393,216 keystrokes and matched on every one. It broke out of the run when
the program counter reached `OUT`'s address and mapped every arrival to one outcome, so **the
carry was never read**. A port with that bit backwards would have passed the largest sweep in the
suite and then silently accepted purchases the original refuses.

The lesson is narrower than "test more": the sweep compared everything the routine *computes* and
nothing about *how it returns*. Exit state — the carry, the flags, where control goes — is part
of a 6502 routine's contract as much as its output is, and a comparison that stops at the exit
address is measuring the wrong boundary. Slice 2b's `hyp` test hit the same shape from a different
direction, where a message printed on the wrong row was invisible to a whole-screen compare
because the cursor was not asserted.

**A mutation harness that sometimes tested nothing.** The portable runner builds with `make`,
which compares modification times at one-second granularity. A mutate-build-run-restore cycle that
completes inside one second can rebuild nothing and report the **unmutated** binary as passing —
a false negative in the direction that matters, because it says a mutation survived when it was
never compiled. One did: the twelve-key limit in `gnum`'s loop.

Mutations now go through a harness that deletes the object file first. The two survivors reported
earlier the same day were re-checked that way and both claims stand — the branch-page mutation is
genuinely caught, and the dead page-crossing flag genuinely survives. The general point is that a
mutation reported as SURVIVING deserves the same suspicion as a test reported as passing: it is a
claim about a build, and the build has to have happened.

### 6.17 The C64 main loop has no frame cap, so the frame rate is not a setting

Risk R3 has said since the plan opened that "the original's main loop ran as fast as the machine
allowed". That was inherited from the BBC-oriented commentary and never checked against the C64
build, and checking it changed what the risk was asking.

**The check.** `WSCAN` is the routine that waits for vertical sync. Rather than read the version
gates — which is what got this wrong the first time — the assembled C64 binary was scanned for
the byte pattern of a `JSR` or `JMP` to `WSCAN`'s address, and each hit resolved to the nearest
preceding label. There are exactly three:

| Caller | What it is |
|---|---|
| `DELAY` | the pause routine, which loops `WSCAN` N times — so its argument is a count of **frames** |
| `TT16+7` | moving the chart crosshairs, which waits for sync to avoid tearing |
| `FREEZE` | the pause-the-game routine |

**`main_flight_loop_part_13_of_16.asm` has a `JSR WSCAN`, and the C64 is not in its version
gate.** The gate reads `_CASSETTE_VERSION OR _DISC_FLIGHT OR _6502SP_VERSION`, and the BBC needs
it for a palette trick the C64 does not do. So the premise is confirmed and sharpened: **the
main loop is uncapped**, and that is why the real game visibly slows down when the screen fills
with ships.

**What that means for the port.** "What step rate should we use" has no answer in the source,
because the original does not have a step rate — it has a loop and a processor. The rate is a
*consequence*. So the timing model is two things, not one:

- **The main loop is cycle-budgeted and free-running.** Measure what an iteration costs, divide
  by the clock, and let it slow under load exactly as the original does. A fixed rate would be a
  behaviour the game never had — and one that makes combat *easier* than it shipped, because the
  slowdown is part of the difficulty.
- **`DELAY`, `TT16` and `FREEZE` are frame-quantised** and need a vertical-sync tick. 50 Hz is
  the default: this is a Firebird UK release, and `wscan.asm`'s own comment says the screen
  refreshes "50 times a second (50Hz) on PAL systems, or 60 times a second (60Hz) on NTSC" —
  the game is agnostic and the *machine* decides, so the port exposes it and defaults to PAL.

The cycle counter this needed is built and lives in `Cpu6502` (Risk R3). It is validated against
two of the game's own routines, hand-counted from the source: `TT54` at 66 cycles over 23
instructions and `DORND` at 36 over 13, both exact. `DORND2` — one byte earlier, at the `CLC`
that makes the entry carry-independent — costs 38, and the two-cycle difference is asserted
rather than the totals, which is what pins the model to a known instruction.

**The residual, stated rather than buried.** The counter does not price a trapped call, so any
measurement of a routine that prints or plots is a lower bound; and it does not model the cycles
the VIC-II steals from the processor for character and sprite fetches, which on a real C64 is a
further 5-10%. The port will therefore run slightly fast unless that is corrected once a loop
exists to measure. Both omissions are documented on the `cycles` field itself, where anyone
reading a number will see them.

### 6.16 One side effect, surfacing for the third time

**Slice 2c, 2026-09-03.** `var` computes the economy's contribution to a price and, on its way
out, writes zero to `AVL+16` — the availability of Alien Items. It is two instructions with
nothing to do with the arithmetic around them, and it is how the game enforces that Alien Items
can never be bought.

This is the third slice to trip over it.

- `GenerateMarket` (2c, the price model) had to reproduce it, and the port does it in the
  generator rather than in `EconomyAdjustment` — deliberately, because the adjustment is
  arithmetic and has no market to reach into.
- `EconomyAdjustment` therefore does NOT do it, which is recorded in its own header.
- And `TT167`, the market screen, works out every price through `var` — so printing the screen
  makes Alien Items unavailable, and the seventeenth line is always a dash however much stock the
  market was generated with. The whole-screen comparison caught it on the last character of the
  first case: 404 of 405 matched.

The lesson is not about Alien Items. It is that a side effect the port moved for good reasons has
to be re-created at every call site the original reaches it from, and the only thing that finds
those sites is comparing the whole output rather than the value under test. `PrintMarketScreen`
takes its market by reference for this and nothing else.

### 6.15 Two off-by-ones that cancel, and why neither may be fixed

**Slice 2c, 2026-09-03.** `tnpr` decides whether a purchase fits in the hold, and it counts the
hold **one tonne too high**. The `CPX QQ29` that chose the tonne path leaves the carry set, and
the first `ADC` of the counting loop consumes it, so the total is always one more than the cargo
aboard.

That is not a defect, and the original's own comment says why: `CRGO` holds **two more** than the
capacity it describes — 22 for a twenty-tonne hold — "to make the maths in tnpr slightly more
efficient". The two off-by-ones are a matched pair. Correct either one alone and every hold in the
game is off by a tonne, in opposite directions depending on which you touched.

It is worth recording because the two constants are in different files, ported in different
slices, a day apart, and each looks like a bug on its own. §6.14 records the `CRGO` half from the
commander's side; this is the other end of it.

A third thing the sweep found, by mutation rather than by reading: 256 tribbles weigh a tonne, so
`tnpr` adds the HIGH byte of the tribble count — and that addition takes the carry the counting
loop left. The carry that reaches it is the one the LAST addition produced, item 0's, not the
running total's. So it can only be set when a single good holds most of the cargo, and a test that
spread the tonnage across the hold never reached it: dropping the carry passed a sweep of all 256
purchase sizes. The test now sweeps two layouts.

### 6.14 What the commander block turned out to be

**Slice 2d, 2026-09-03.** Three things, none of which a reading of the source gives you.

- **`CRGO` is two greater than the cargo capacity it describes.** A standard hold reads 22 and
  carries 20 tonnes; a large one reads 37 and carries 35. The original says why in a comment: it
  makes the arithmetic in `tnpr`, which decides whether a purchase fits, "slightly more
  efficient". A port that read the byte as the capacity would let the player carry two tonnes too
  many, and nothing about the value looks wrong.

- **`CASH` is stored most significant byte FIRST**, and it is the only multi-byte value in the
  game that is. `TALLY`, sixty bytes further down the same block, is low byte first like
  everything else. Both are in the save file, so a port with one convention gives the player
  either fourteen pence or several million credits.

- **`DFAULT` does not reject a bad save file, it HANGS.** `JSR CHECK / CMP CHK / BNE doitagain`
  branches backwards, and the second checksum's failure branches to the same place — so a
  tampered commander leaves the game spinning on a black screen. That is copy protection rather
  than a defect. It is the one behaviour in this slice the port refuses to reproduce, because a
  hang is not something a caller can handle; `LoadCommander` returns false instead, and a test
  establishes that the two agree on exactly WHICH files are acceptable by running the shipped
  routine under an instruction budget and reading "did not return" as "rejected".

  The routine also does something between its two checks that is easy to miss: it sets bit 6 of
  the competition flags always, and bit 7 when the first checksum EORed with &A9 disagrees with
  the second stored one. So a tampered file is *remembered* rather than refused, and the
  competition code reads the flag later.

### 6.13 A shipped bug the port had to keep: the first system of a galaxy cannot be found by name

**Found 2026-09-03, building slice 2b.** Elite's short-range chart has an `F` key that asks for a
planet name and moves the crosshairs to it. `HME2` implements it by turning the justification
buffer into a scratch pad: control code 14 starts buffering, `cpl` prints each system's name into
`BUF` instead of onto the screen, and the typed name is compared against it backwards.

The comparison is `LDA INWK+5,X / ORA #%00100000 / CMP BUF,X`. The typed character has bit 5
FORCED ON and the buffer's is taken as it is, so a match needs `cpl` to have printed in lower
case — which depends on `QQ17`, which `HME2` never sets.

`HME2` opens by printing extended token 14, and that token ends at `CLYNS`, which sets
`QQ17 = %10000000`: sentence case with the "a letter has been seen" bit CLEAR. So the first name
`cpl` prints comes out as `Tibedied` rather than `tibedied`, fails on its first character, and
**the first system of every galaxy cannot be found by typing its name**. Printing it sets the bit,
and every system after it matches.

Measured rather than reasoned: the search finds 1,022 of 1,024 across two galaxies, and the two it
misses are the two first systems. Both halves were checked separately — that `DETOK` of the prompt
leaves `QQ17` at `%10000000`, and that `cpl` at that value prints `Tibedied` while at `%11000000`
it prints `tibedied`.

The port reproduces it, under ADR-003: match before improving. The test asserts the miss count is
exactly two, so a later change that "fixes" the search fails rather than silently diverging from
the game.

### 6.9 Slice 0a was not finished, and the way it failed is the interesting part

**Found 2026-09-03, on a clean clone.** Slice 0a was accepted on a mechanical criterion —
712/712 include paths resolve — and the criterion was sound. What nobody checked is that it
still held on a machine other than the one it was written on. It did not. On a fresh clone the
same command reported **0 of 712**, and every slice from 1a onward was unbuildable.

The cause is one missing file. `Upstream/elite-source-code-library` is recorded in the index as
a **gitlink**: mode `160000`, the commit hash `aa3f7ee`, and nothing else. That is a submodule
entry, and a submodule entry without a `.gitmodules` to name its URL is inert — `git submodule
update --init` answers `no submodule mapping found` and there is no other way to recover the
content, because the repository never held it. So a clone got an empty directory where 710
routine bodies, the font and the assembled blocks were supposed to be.

`.gitmodules` now exists and names the path and the upstream URL. One `git submodule update
--init` restores the tree, and `--check-includes` is green again.

Three things worth carrying forward:

- **The corpus said "vendored ... not a submodule" and the tree said otherwise.** ADR-001 §5 is
  corrected rather than reinterpreted. This is the failure mode AGENTS.md §7 names — code and
  `Design/` disagreeing — arriving in the one place nobody re-reads, which is the description of
  something already accepted.
- **An acceptance criterion that only ever ran on the author's machine has not been tested, it
  has been demonstrated.** Everything phase 0 built is exercised on each test run and would have
  survived; slice 0a's output is the one thing that is only touched at clone time, which is why
  it is the one thing that broke. Anything whose accept depends on the state of the working tree
  should be re-run from a clean clone before its slice is closed.
- **The accident landed on the safer side of R1.** Because the content was referenced rather
  than copied, none of the ~3,000 unlicensed files has ever entered this history. What ADR-001
  described as reversible in one command turns out never to have needed reversing — and the
  files that *are* committed, and would be published, are the 13 in `MasterFile/`, which §5 did
  not mention. That is now recorded there as an owner decision, not settled by the corpus.

### 6.1 What slice 0c actually built

| Piece | Detail |
|---|---|
| `Tests/GameLogicTests/Cpu6502.h/.cpp` | An NMOS 6502 interpreter, ~470 lines, every documented opcode, decimal mode, and the indirect-jump page bug. `CallSubroutine` pushes an unreachable return address and stops on it, and *also* stops when the stack unwinds past its entry point — because the game discards its own return address in places, and a run that does that has finished too. |
| `Tests/GameLogicTests/Cpu6502Tests.cpp` | 11 tests pinning the interpreter against published behaviour before it is trusted as an oracle: carry versus borrow in subtraction, signed overflow separate from carry, zero-page index wrapping, the indirect-jump page bug, stack balance across call and return, signed backward branches, decimal addition, and the illegal-opcode stop. |
| `GameLogic/EliteTypes.h` | The numeric vocabulary of ADR-002: `AddWithCarry` returning value, carry and overflow; `RotateLeft`/`RotateRight`; `SignMag24` for the three-byte sign-magnitude coordinate. |
| `GameLogic/Rng.h/.cpp` | The first ported routine (`DORND` and its repeatable entry point), carrying its `// 6502:` marker per AGENTS.md R7. |
| `Tests/GameLogicTests/RngTests.cpp` | The first oracle suite, and it needs no assembled game: the routine is 20 bytes, so the test assembles it and runs both. 60,000 iterations across three starting states, plus 8,192 first-call comparisons, all agreeing on the returned byte, the previous byte, both flags and all four state bytes. |
| `tools/inventory.py` | `--check-includes` (712/712) and the coverage report: 707 distinct library stems, **603 named in the ledger, 104 not yet**. That 104 is the ledger's real backlog and the number slice 1a starts chipping at. |

The order here was deliberate: the interpreter is tested against the processor's published
behaviour *before* anything trusts it, because an oracle nobody checked is just a second
opinion from the same author.

### 6.2 What slice 0b-a built, and why it changes the shape of everything after it

BeebAsm turned out not to be a dependency to wait on. It is open source, so it was cloned and
built from source with the VS 18 toolchain (its own CMake file is GCC-flavoured, so the sources
were compiled directly). It lives in `Tools-ext/`, which is **gitignored** — it is a tool we
run, never a library we link, and vendoring a GPL project into a tree intended for publication
is a licence question nobody needs.

| Piece | Detail |
|---|---|
| Assembly | The C64 variant assembles to all eleven blocks, `ELTA.bin`…`ELTK.bin`, loading contiguously at `$1D00`–`$3ED2` and `$6A00`–`$CCD7`. |
| `tools/labels.py` | Runs the assembler and normalises two awkward formats into plain tables: BeebAsm's label dump is one line of Python-2 dict syntax, and the load addresses exist only as `PRINT` lines in the verbose log. Output is `Design/Reference/Labels.txt` (**1,782 labels**) and `Binaries.txt`. |
| `Tests/GameLogicTests/OracleImage.*` | Loads all eleven blocks into a 64 KB image and resolves labels by name, so a test says `oracle.Label("MULTU")` rather than an address. Each call gets a fresh copy, so tests cannot leak state into one another. |
| `OracleTests.cpp` | The generator compared against the routine **as it actually ships**, over 20,000 iterations. Plus the presence tests that make an absent oracle loud. |

**Why this matters more than the slice count suggests.** Until now the oracle could only reach
routines short enough to hand-assemble into a test — which is essentially none of the game. It
can now call any of 1,782 labels in the shipped binary with chosen inputs and read back the
memory. That is the instrument the whole plan is built on, and phases 1 through 4 are now
mechanical in a way they were not this morning.

### Phase 1 — Kernel (no screen needed; oracle-driven throughout)

| Slice | Scope | Accept |
|---|---|---|
| **1a Data extraction** 🟡 **Trigonometry and logarithms built 2026-09-03; the rest pending** | `tools/extract_tables.py` emits `*.cpp` data files, **extracted from the assembled binaries by label and length rather than by parsing the assembler** — see §6.6 for why that is the cheaper and truer route. Built so far: `log`, `logL`, `antilog`, `antilogODD`, `SNE`, `ACT`. Still to extract: `QQ18`, `TKN1`/`RUPLA`/`RUGAL`/`RUTOK`, `QQ23`, `NA%`, `XX21` and the blueprints, `E%`, `KWL%`/`KWH%`, `scacol`, `TWOS`/`CTWOS`/`TWFL`/`TWFR`, the sound and tune tables, `sdump`/`cdump`, `dials`, `spritp`, the font. | **Met for what is built.** Each array is compared byte for byte against the same address range in the oracle's image, so a stale or hand-edited table fails a test. `--check` does the same from the command line. Adding a table is one row in the tool. |
| **1b-a Multiply and add** ✅ **Built 2026-09-03** | `MULTU`, `MU11`, `MU1`, `MULT1`, `MULT12`, `SQUA`, `SQUA2`, `MLU2`, `ADD` (with `MU8`/`MU9`) — the shift-and-add core and sign-magnitude addition. | **Met.** Every multiply compared against the shipped routine over **all 65,536 input pairs**; the addition over a 200,000-case deterministic sweep plus eleven sign-magnitude edge cases. Green in Debug and Release. See §6.3. |
| **1b-b Multiply-accumulate and divide** ✅ **Built 2026-09-03** | `MAD`, `MU5`, `MU6`, `MULTS`, `MLTU2`, `TIS1` (with `DVID96`), `TIS2`, `DVIDT` — everything in the division and accumulate family that needs no table and no game state. | **Met.** Exhaustive where the input space is 16 bits, 150,000-case sweeps above. Green in Debug and Release. §6.4 records the one defect it caught. |
| **1b-c State-dependent helpers** | `MULT3`, `MLS1`/`MLS2` setup, `MLU1`, `MUT1`–`MUT3`, `TAS3`, `TIS3`, `DV41`, `DV42`, `CNTR`, `BUMP2`, `REDU2`, `NORM`, `LL5`. Each reads ship slots, rotation angles, stardust arrays or the damping flag. | Land with the workspace they belong to, in slice 3a, rather than being forced into the kernel early. |
| **1b-d Logarithm-table routines** ✅ **Built 2026-09-03** | `FMLTU`, `LL28`, `LL38`, `ARCTAN` — multiply and divide by adding logarithms, and the angle of a ratio. `DVID4`, `DVID3B2`, `FMLTU2` and `LL51` remain and are the same shape. | **Met.** All four compared against the shipped routines; the multiply, the divide and the angle **exhaustively over all 65,536 input pairs each**, the combine over a 200,000-case sweep. The divide's returned carry is compared too, because its callers branch on it. |
| **1c-a Recursive tokens** ✅ **Built 2026-09-03** | `TT27` and everything it falls through into: `TT41`, `TT42`, `TT43`, `TT44`, `TT45`, `TT46`, `TT47`, `TT74`, `qw`, `ex` with its walk, and the case-flag state machine. Plus the `QQ18` and `QQ16` tables. | **Met.** **243 of 250 tokens compared character for character against the shipped printer, in all five capitalisation states.** The remaining 7 embed value tokens that read commander and system state; the test detects and counts those rather than skipping them silently. §6.7 explains the trap that made output comparable at all. |
| **1c-b Extended tokens** ✅ **Built 2026-09-03** | `DETOK`, `DETOK2`, `DETOK3` and the case state they carry, plus the letter-pair, nested-token and randomised-variant paths. `TKN1`, `TKN2`, `RUTOK` and `MTIN` extracted. | **Met.** 199 of 255 tokens compared character for character in three case states, and 21 of the per-system overrides. The 56 deferred reach control codes; the tests count them. §6.8 records the two table-sizing lessons this slice taught. |
| **1c-c-a Numbers** ✅ **Built 2026-09-03** | `BPRNT` with `TT11`, `pr2`, `pr5`, `pr6`, and the `TENS` constant. | **Met.** 308 numbers compared **character for character** through a trap on `DASC` — every digit width against both settings of the carry that decides whether a decimal point appears — plus `TT11` swept over the sixteen-bit range and `pr2` over every byte. One defect: the `BCC` at the top skips its two decrements when the carry is *clear*, so they apply to a number that IS getting a point; reading it the other way round shifts the padding two characters and puts the point where a digit belongs. |
| **1c-c-b Control codes** ✅ **Built 2026-09-03** | `MT1`–`MT19` behind the control-code seam, `vowel`, `whitetext`, `feed`, `DASC` and the `DTW1`–`DTW8` justification behaviour with its line buffer. | **Met, and wider than scoped.** Twenty of the codes are ported and 8, 21, 23 and 29 are *split* so the flags they set land here and only the cursor or screen half is deferred. **Corrected 2026-09-03 while scoping 2b:** `JMTB` has thirty-one reachable entries, not the twenty-one this slice first read out of it, and all ten of codes 22 to 31 are used by tokens the game prints. They are still deferred — they wait for keys, spin the title ship, or print a token under `GCNT` or `DISK` — but the note that called them meaningless was wrong, and the dispatch test now drives all thirty-one. Every code compared against the shipped dispatch in two case states — output, all seven `DTW` bytes, `QQ17` and 128 bytes of `BUF`. `DASC` compared character by character over all 256 byte values and eight justified paragraphs, plus the `DTW4` bit 6 branch that only the in-flight message printer sets and no token can reach. And **all 2,048 system descriptions now compare character for character** against `PDESC`, which is the test slice 2a could not write. Three findings: the `LSR SC+1` at `DA5` is dead, so the justification does not depend on the screen pointer it borrows; `BUF`'s ninety bytes are not enough for the game's own text, which overruns into the ship position tables; and `CHPR` returns with the carry clear, which the `SBC #30` four instructions later borrows on — so the oracle's trap had to be taught to clear it too, or the game's own text came out a character wider than the game produces. |
| **1d-0 Canvas representation spike** ✅ **Built 2026-09-03** | Measure what the drawing code actually writes, before deciding what `Canvas` holds. `Tests/GameLogicTests/CanvasSpikeTests.cpp`. | **Met.** ADR-002 §4 amended from measurement (its new §7 carries the evidence), ADR-005 §1's orphaned screenshot dependency answered, and the `celllook` puzzle resolved. See §6.10. |
| **1d-a Canvas and the pixel primitives** ✅ **Built 2026-09-03** | `Canvas`: the four planes of ADR-002 §4, the original's addressing (`ylookup`, `celllook`), byte-wise EOR, and `Resolve()` to 320×200 indices. Routines: `PIXEL`, `PIXEL2`, `PIX1`, `DOT`, `CPIX2`/`CPIX4`, `HLOIN`/`HLOIN2`, `LOIN` all seven variants with the `LIJT*` jump tables as a `switch`. Tables `TWOS`, `TWOS2`, `CTWOS`, `CTWOS2`, `TWFL`, `TWFR`, `DTWOS`, `ylookup`, `celllook` extracted (the rest of 1a for this area). | **Met, and the compare is the whole 0x2800 screen rather than the bitmap alone.** `PIXEL` over every x at eight distances, `PIXEL2` over all 65,536 coordinate pairs, `CPIX2`/`CPIX4` over every x in five colours, `HLOIN` over both ends across three cells, and **`LOIN` over 3,528 lines**. Three defects caught: a non-power-of-two bitmask corrupting addresses, a collapsed carry chain in `PIXEL2`, and an uncleared borrow in `LOIN`'s steep row step. See §6.11. |
| **1d-b Text and the font** ✅ **Built 2026-09-03** | `TextPrint`: `TT26`/`CHPR` with the font, `setxc`/`setyc`/`doxc`/`doyc`, `INCYC`, `CLYNS`, the `MAG2` input colour and the cell-colour writes; `Font.cpp` extracted from `C.FONT.bin`. | **Met.** 5,376 printable characters at seven columns and four rows in two cell colours, the 31 control codes at nine cursor positions, and the `QQ17 = 255` suppression over every code — glyph, cursor and colour cell each compared. `TT66simp` ported too, which closes one of the slice's two seams; the bell is the other and belongs to phase 5. |
| **1d-c The golden harness** ✅ **Built 2026-09-03** | `TT66simp`; the harness: `Canvas::Resolve()` → indexed PNG, `Canvas::Hash()`, `tools/golden_diff.py`. **`CIRCLE`, `CIRCLE2` and `BLINE` moved to 3c** — they reach `CHKON`, the sun line heap (`LSX2`/`LSY2`/`LSP`) and `LL145`'s clipping, none of which phase 1 defines. | **Met.** Two goldens, each checked twice: the shipped routines draw the scene into the oracle, its memory is decoded into a `Canvas`, and the two resolved images are compared **pixel for pixel** — then against a committed hash, which is what a self-comparison cannot catch. On a mismatch both images are written as PNGs and the failure names the `golden_diff.py` command (Risk R10). |

### 6.3 What slice 1b-a built

Nine routines: the unsigned shift-and-add multiply and its register-argument entry point, the
sign-magnitude multiply and its two-scratch-byte variant, both squaring entry points, the
magnitude-times-Q helper, and sign-magnitude addition with its subtract-and-negate branch.

| Routine group | Coverage |
|---|---|
| `MULTU`, `MU11`, `MULT1`, `MULT12`, `MLU2` | **All 65,536 input pairs each**, comparing the returned high byte and the low byte left in scratch |
| `SQUA`, `SQUA2` | All 256 inputs, both entry points |
| `ADD` | 200,000-case deterministic sweep over four bytes of input, plus eleven hand-picked sign-magnitude edges |

Three things worth carrying forward:

- **The multiplier is decremented before the loop and the addition is done with carry set**, so
  the two cancel out. It reads like an off-by-one and is not, which is why `Arith.cpp` says so
  at the top rather than leaving the next reader to work it out.
- **`MU11` must not be called with a zero multiplier.** It decrements first, so zero would
  become 255 and the routine would multiply by that. The game's callers check for zero and jump
  to a different tail; the test does the same, and the port keeps the check in `MultiplyUnsigned`
  where the original has it.
- **The unsigned multiply is also checked against actual multiplication**, not just against the
  oracle. An oracle comparison alone proves the port agrees with the original, which is the goal
  — but for a routine whose whole job is `a * b` it costs one line to also confirm both of them
  are right, and that line would have caught a shared misreading of the calling convention.

### 6.8 Two lessons about sizing an extracted table

Slice 1c-b spent most of its debugging on one wrong number, and the mistake generalises.

**A table's size is what can index it, not the gap to the next label.** The extended letter-pair
table was extracted at the 26 bytes its neighbouring label implied. It failed on the second
token. The routine that reads it accepts every byte from 215 to 255, so it is reachable to
offset 81 — and Elite genuinely lets it overlap the table that follows, sharing address space
where the ranges allow. Deriving a length from the next label is a guess dressed as a
measurement. Derive it from the index range instead, and let the byte-comparison test confirm it.

**The game's table walkers are not bounded lookups.** They count terminators with nothing to
stop them. Ask for a token past the last entry and the original keeps scanning into whatever
follows, while the port bounds-checks and returns. Past the end the two are not comparable:
one has stopped and the other is still running. The test therefore compares the entries that
are bounded on both sides and says why it stops where it does.

Neither of these is a defect in the port. Both are the kind of thing that only surfaces when a
comparison is exact, which is the argument for making it exact.

### 6.7 Comparing output, not just arithmetic

Everything before slice 1c compared a routine's *return value*. Text does not have one: the
printer's whole job is the sequence of characters it hands to something else. So the oracle
gained a mechanism it will need for the rest of the port.

**Call traps.** The interpreter can be told that certain addresses are to be recorded and
returned from rather than executed. Trapping the game's character routine turns "what did this
token print" into a list, with none of the screen code running underneath. The same mechanism
will serve the drawing slices, where trapping the pixel routine turns "what did this draw" into
a list of plotted points.

It works for the game's control flow as it actually is, which is the part worth noting: the
printer reaches its character routine by a jump rather than a call in several paths, so the
trap's simulated return lands where that routine's own return would have. Nothing had to be
special-cased.

**The phase boundary is measured rather than assumed.** Seven of the 250 tokens expand into
text that embeds a value token, and those print cash or fuel from commander state that phase 2
owns. Comparing them would be comparing against uninitialised memory. The test therefore runs
the port with a provider that records when game state is reached, skips those tokens, and
**counts them** — so a change that quietly made everything skip would show up as a count of 250
instead of passing green.

### 6.6 Extracting data from the binary rather than from the source

The plan originally had the extractor parse the assembler for each table. Building it that way
turned out to be the wrong instinct, and the reason generalises.

**The source computes these tables; it does not contain them.** They are built with
assembly-time loops and macros, so a parser would have had to evaluate that — which is to say,
be a second assembler. Meanwhile the bytes that actually matter are the ones the game ships
with, and those are sitting in the assembled blocks the oracle already loads. Extracting by
label and length is a dozen lines instead of a subsystem, and it reads the same artefact the
port is being tested against.

It is also self-checking, which matters more than the simplicity. A one-way copy with no check
goes stale. `TableTests.cpp` compares every generated array against the same address range in
the oracle's image, so an edited file, a regeneration against a different build, or a length
changed in the extractor but not in the declaration all fail a test rather than drifting.

**This slice also closed a gap in the oracle itself.** The tables live in the *data* build, not
the code blocks, so the image had never loaded them: the angle routine could not have been
tested at all. `tools/labels.py` now assembles both files and merges their labels, taking the
count from 1,782 to 1,927 and the loaded blocks from eleven to thirteen. Reading the saved
filename out of the assembler's own log rather than guessing it from the printed name is what
makes that reliable, because the two do not agree.

### 6.5 What cancelling the reference run costs, and what replaces it

The emulator run was carrying two things. Losing it is not free, and pretending otherwise is how
a corpus starts lying.

**1. The first golden canvases (slice 1d) lose their acceptance method — and gain a better one.**
The plan said a person would accept each first golden by eye against an emulator screenshot.
That is no longer available, and the replacement is stronger rather than weaker: **the oracle can
draw the screen itself.** The game's drawing routines write into a bitmap in the 64 KB image,
so a test can call them, decode those bytes into canvas form, and compare pixel for pixel
against the port. That is an exact comparison rather than a human judgement, it needs no
emulator, and it extends the oracle to the drawing code instead of stopping at arithmetic.
Goldens stay useful for whole screens assembled over many calls, but they stop being the
*authority* for anything the oracle can reach. **ADR-003 §2 is amended accordingly.**

**2. The step cadence (Risk R3) has no measurement, and now no planned source for one.** This
is the real cost. The original's loop ran as fast as the machine allowed, and the feel of flight
follows from that rate. Nothing in the oracle answers "how many iterations per second did a real
machine manage", because the interpreter has no notion of time.

Two honest options were put to the owner:

- **Count cycles.** Give the interpreter a cycle count per instruction and run a main-loop
  iteration in a representative scene. Divide by the processor clock and the answer falls out,
  with no emulator and no guesswork. It is perhaps a day of work and it is the option that
  actually measures something.
- **Pick a number and make it a setting.** Ship a default in the region of 10 to 20 steps per
  second, expose it in configuration, and tune by feel. Cheap, and honest so long as nobody
  later describes it as measured.

**Ruled 2026-09-03: count cycles. Built the same day, and the question turned out to be a
different one — see §6.17.**

### 6.4 What slice 1b-b built, and the defect it caught

Eight more routines: the multiply-accumulate the geometry runs on, two block-fill helpers, the
scaled multiply, the sixteen-step wide multiply, and three divisions.

**Seven matched first time. The sixteen-step long division did not, and that is the useful
part of this entry.** The port restarted the carry flag at zero on every step of the loop. In
the original the carry threads straight through: what falls out of the top of the quotient on
one step is shifted into the remainder on the next. The two versions agree for a great many
inputs and part company on the very first case the sweep tried, returning 128 where the game
returns 165.

Three things that is worth noting for the slices ahead:

- **It is invisible by inspection.** Both versions are a rotate, a compare, a conditional
  subtract and two more rotates. Nothing about the wrong one looks wrong, which is precisely
  the failure mode the whole oracle approach exists to catch (Risk R4).
- **It cost about a minute to find and fix**, because the failure named the routine, the
  iteration and both numbers. That is the argument for exhaustive-or-swept comparison over
  spot checks.
- **The comment now says why the carry threads**, so the next person to read the loop does not
  quietly simplify it back.

### Phase 2 — The docked game (playable without flight)

| Slice | Scope | Accept |
|---|---|---|
| **2a Universe** 🟡 **Generator and descriptions built 2026-09-03** | Seeds and galaxy (`TT20`, `TT54`, `TT111`, `TT18`, `TT146`), system data (`TT24`, `TT25`, `cpl`, `cmn`, `ypl`, `tal`, `fwl`, `pdesc`), the Data on System screen, `jmp`/`ee3` distances. | **Met for everything the screen does not need.** All 2,048 systems in all eight galaxies compared on economy, government, technology, population and productivity; all 2,048 names character for character; and — once 1c-c-b landed the control codes and the line buffer — **all 2,048 descriptions character for character** as well, which is the strongest single test in the port so far: token expansion, the randomised alternatives, the case machinery, the word wrap, the padding, `MT17`'s reach into the buffer and `MT18`'s random words all have to agree for one description to come out right. `TT111` over 864 searches; `LL5` over all 65,536 radicands. Galaxy 1 system 0 is TIBEDIED. What remains is the Data on System *screen* and `TT25`'s siblings, which are cursor and canvas work. The DOS-port cross-check needs a machine this work did not run on. |
| **2b Charts** ✅ **Built 2026-09-03** | `TT22` long-range, `TT23` short-range, crosshairs (`TT15`, `TT14`, `TT16`, `TT103`, `TT105`, `TT123`), `NLIN`/`NLIN2`/`NLIN3`/`NLIN4`, `HME2` find planet by name, `hyp`/`hy6` target selection with `hm`, `ee3` and `TT147`. **The scope line named `TT16a` as "find planet by name" and it is not** — it prints "g", for grams, on the market screen; `HME2` is the search, and it is built. | **Met, and stronger than the stated criterion.** Not goldens but whole-screen comparisons against the shipped routines: both charts for all eight galaxies (the short-range one at four home positions), 1,200 crosshairs, 1,250 crosshair moves, 112 fuel circles, and `TT123` exhaustively over all 65,536 value-and-step pairs. `HME2` compared over 1,024 searches. **The stated criterion cannot be met and should not be**: the fuel circle is `CIRCLE2` and every system's blob on the short-range chart is `SUN`, both of which keep a line heap that belongs to 3c — so a golden of either chart would be a golden of a chart with its circles missing. They are seams whose ARGUMENTS are compared instead, which pins them before the drawing exists. `hyp` was scoped out on first reading as needing commander state, and that was too pessimistic in the way §6.12 describes: the state is an INPUT, not something this slice has to own, so it arrives as a value the way the market's does and the logic is ported. All 1,024 combinations of docked, countdown, CTRL, view, fuel and crosshair position compared — every one of the six branches reached — on the text, the cursor, the countdown, the seeds saved for the jump and the screen. What genuinely remains outside is `Ghy` (the galactic hyperdrive reads the equipment you are carrying), `hyp1`/`TT18` (arrival: the market roll and the tunnel), and `MT26`'s keyboard half of the `F` flow. |
| **2c Trade and equipment** 🟡 **Price model and trade arithmetic built 2026-09-03** | `TT151`/`var`/`GVL` prices, `TT167` market, `TT219` buy, `TT210` sell, `gnum`, `TT213` inventory, `STATUS`, `EQSHP` with `prx`, `qv`, `refund`, `hm`, cash (`LCASH`, `MCASH`, `GCASH`), `TT162`/`TT160`/`TT161` units. | **Met for everything that is arithmetic.** Every price the game can quote — seventeen goods, eight economies, every value of the market's random byte, 34,816 in all — plus 512 generated markets. And with 2d's commander block in place, the four routines the buying and selling are built on: `LCASH`, `MCASH`, `GCASH` over every price, and `tnpr` over 86,016 capacity checks. §6.15 records what `tnpr` turned out to be. **The market screen and `gnum` are built too**: `TT167` with `TT151`, `TT152` and the unit printers, compared **character for character with the cursor stamped on every character** across all eight economies and six market randomisers — 405 characters a screen, 48 screens. Stamping the cursor is what makes it a real comparison; the characters alone would pass a port that printed every line one cell left. `gnum`'s body — one keystroke of a typed number — is compared over **393,216 keystrokes**, every value against every key at six availabilities, by stepping the shipped routine to whichever of its five exits it reaches. **`gnum`'s loop and the six state control codes landed 2026-09-03 as well.** `ReadNumber` is the loop, ported against a `KeySource` seam and compared over thirteen scripted key sequences; §6.18 records the testability defect that building it exposed. `StateTokens` closes the `ValueTokens` seam slice 1c-a opened — `csh`, `tal`, `ypl`, `cpl`, `cmn` and `fwl`, compared character for character over 72 cases — which is what every docked screen needs to print a cash or a fuel line at all. **All three market screens are built** — `TT219` (buy) over seven scripted key sequences, `TT210`/`TT213` (sell and inventory, one routine told apart by QQ11) over thirteen — each compared character for character with the cursor stamped, plus the cash, the hold, the market, the seam ordering and the random state. That is the proof the deferral was wrong: what these screens wait on is a KEY, and a key is a seam. **`STATUS` is built too** — twenty situations compared character for character, plus the case flags and the system `TT111` settled on. **What is left of 2c is `EQSHP`**, the equipment shop, which is not blocked on 2e either. |
| **2d Commander and saves** 🟡 **Block, checksums and save format built 2026-09-03** | `NA%` default commander, `JAMESON`, `CHK`/`CHK2`/`CHK3`, `sve`/`lod` replaced by `SaveBlock` (the exact original byte layout so an original C64 save imports) + `SaveStore` in the exe writing to LocalAppData; `trnme`/`gtnme` name entry; `DFAULT`/`qu5`; the `Y/N` prompts. | **Met for the format; the file I/O and the name entry are not built.** The seventy-seven-byte block with every field named from the assembled build, both checksums, and the save and load layout — compared against `CHECK`, `CHECK2`, `SVE`'s copy and `DFAULT` over 221 blocks (the shipped default, all-zeros, all-255, a single bit walked through all 77 bytes, and 64 pseudo-random fills). The block is held as BYTES with named offsets rather than as a struct: the save file is those bytes, so making them the storage removes the serialiser a `.d64` import could drift from. Three findings recorded in §6.14. `SaveStore` (the file itself, in LocalAppData), `trnme`/`gtnme` name entry and the `Y/N` prompts need the keyboard and land with 2e. |
| **2e First playable** 🟠 **Unblocked on decisions, still blocked on a Windows machine** | `Game` top-level state (`BR1`, `BAY`, `TT170`, `DOENTRY`), key dispatch for the docked screens (`DOKEY`, `RDKEY`, `TT217`), `CanvasPresenter`, `KeyMap`, frame pacing; the title screen **without** the rotating ship (that needs LL9 — a placeholder box until 3b). | **Both owner decisions are now taken, and one of them was answered wrongly in this row until 2026-09-03.** (1) **Risk R3 is settled: count cycles, and the counter is built** — but the row used to say the choice was between measuring a rate and picking one, and §6.17 records why that was the wrong framing. The C64 main loop has no frame cap at all, so there is no rate to pick: the loop is cycle-budgeted and free-running, and a separate 50 Hz PAL vertical-sync tick serves `DELAY`, `TT16` and `FREEZE`. (2) **Verification is split, ruled 2026-09-03**: the replay-hash half runs in CI through a null presenter — no window, no GPU, so it goes on the Ubuntu leg — and "is every docked screen legible, does the cadence feel right" is a human sign-off recorded here. That is ADR-003 §3 extended rather than a new mechanism. **What is still needed is a Windows machine or runner that can build and launch the shell.** `Outpost.vcxproj` is the untouched WinUI 3 template ADR-005 §5 defers, and CI builds only the test project. Everything 2e needs from `GameLogic` exists, and the first thing it should do is measure a main-loop iteration and write the number down. |

### Phase 3 — Flight and the 3D pipeline

| Slice | Scope | Accept |
|---|---|---|
| **3a Ship slots and motion** | `ShipSlot`, `Bubble` (`FRIN`, `MANY`, `UNIV`, `NWSHP`, `NWS1`, `KILLSHP`, `KS1`–`KS4`, `ZINF`, `RESET`/`RES2`, `ZES1`/`ZES2`, `GINF`), `MVEIT` 1–9, `MVT1`, `MVT3`, `MVT6`, `MVS4`, `MVS5`, `MV40`, `TIDY`, `MAS1`–`MAS4`, `TAS1`–`TAS6`, `DCS1`, `ABORT`, `sightcol`. | Oracle: run `MVEIT` on a slot with sampled orientations/speeds/roll/pitch for N iterations; byte-identical `INWK`. |
| **3b Ship drawing** | `LL9` 1–12, `LL61`, `LL62`, `LL118`, `LL120`, `LL123`, `LL129`, `LL145` 1–4 clipping, `SHPPT`, `LL5`, the ship line heap (`LSX2`/`LSY2`), `PROJ`, `PL2`. Title screen rotating ship. | Oracle: for sampled ship types and orientations the list of clipped line segments matches; golden of the title screen Cobra at frames 1, 30, 60. |
| **3c Planet, sun, stardust** ∥ | `PLANET`, `PL9` 1–3, `PLS1`–`PLS6`, `PLS22`, `WPLS`/`WPLS2`, `WP1`, `EDGES`, `CHKON`, `PL21`, `SUN` 1–4 with its heap, `CIRCLE` uses, `STARS`, `STARS1`, `STARS2`, `STARS6`, `NWSTARS`, `FLIP`, `WPSHPS`, `FLFLLS`, `SOLAR`, `NWQ`. | Goldens of the launch view at Lave (planet + sun + stardust) at several iterations; oracle for `PLS`/`CHKON` arithmetic. |
| **3d Flight loop and dashboard** | Main flight loop 1–16, `DIALS` 1–4, `DILX`/`DIL2`, `COMPAS`/`SP1`/`SP2`/`SPS*`, `SCAN` (sprite blips as canvas draws), `MSBAR`, `ECBLB`/`SPBLB`, `PZW`, `MESS`/`me1`/`mes9`, `LASLI`, `LAUN`/`LL164` hyperspace tunnel, `DEATH` (the "GAME OVER" fly-by), `WARP` (J), `CTRL`, `DOKEY` flight half, `SPIN`, `cargo` canisters, docking check (`ISDK` path in loop part 10–11). | Launch from Lave, fly, dock manually, hyperspace to Diso, dock. Goldens of the dashboard; replay hashes for the whole trip. |

### Phase 4 — Combat and a living universe

| Slice | Scope | Accept |
|---|---|---|
| **4a Tactics** | `TACTICS` 1–7, `DOCKIT`, `ANGRY`, `FR1`, `FRS1`, `FRMIS`, `SFRMIS`, `SFS1`/`SFS2` spawning from ships, `HITCH`, `OOPS`, `EXNO*`, `ECMOF`, `SESCP`, `bomboff` / energy bomb. | Oracle for `TACTICS` decisions on sampled states (they consume `DORND`, so seed-locked); a replay: launch, get attacked, win. |
| **4b Explosions and death** ∥ | `DOEXP`, `EXLOOK`, `PTCLS2`, `SOS1`, `DEATH2`, the escape pod, `BAD`/`FAROF`/`FAROF2`, `SHD`/`DENGY` shields and energy. | Golden of an explosion sequence; energy/shield oracle. |
| **4c Main game loop** | Main game loop 1–6 (spawning rules: traders, pirates, police, asteroids, Thargoids, rock hermits, cougar), `MJP` witchspace, `ghy` galactic hyperspace, `hyp1`, `GTHG`, `TT18`, `NWSPS` station placement, `TT102`, the Dodo station switch by tech level. | Long replay (≥10,000 steps) hash-stable; spawn statistics over seeds match the oracle's for the same seeds. |
| **4d Missions and Trumbles** | `BRIEF`, `BRIEF2`, `BRIEF3`, `BRP`, `BRIS`, `DEBRIEF`, `DEBRIEF2`, `TBRIEF`, `PAUSE`/`PAUSE2`, `MT23`/`MT29`, the Constrictor and Thargoid-plans state (`TP`), `MVTRIBS`, `TRIBTA`, `TRIBMA`, `tribdir`, the Trumble sprites and sounds. | Scripted replays reach each briefing; Trumble multiplication matches oracle over N steps. |

### Phase 5 — Sound and music

| Slice | Scope | Accept |
|---|---|---|
| **5a Effects** | `NOISE`, `NOISE2`, `BEEP`, `EXNO`, `EXNO2`, `EXNO3`, `SOFLUSH`, `NOISEOFF`, `HYPNOISE`, the `sfx*` tables, the interrupt-time effect player (`soint`, `comirq1`'s SID half) as a per-step state machine emitting `SoundEvent`s; `SidSynth` in the exe. | The register-write log for each of the 16 effects matches the oracle's over the effect's duration; audible check against VICE. |
| **5b Music** | The `BD*` player (`bdirqhere`, `bdro*`, `bdlab*`, `bdentry`, jump tables), `comudat`, `music_variables`, the nine tune blocks, `startat`/`startbd`/`stopbd`, docking music trigger, the `M` mute keys. | Register-write log for the first 2,000 ticks of each tune matches the oracle. |

### Phase 6 — Modernisation (each item its own ADR when it comes)

Not planned in detail on purpose (Risk R7). Candidates, in the order they are likely to be
wanted: window/fullscreen and scale options; key remapping and gamepad; a "fixed bugs" toggle
(`NRU%`, others found on the way); PAL/NTSC timing option; save-slot UI; then the things that
change the game (higher internal resolution for lines, smoother iteration rate). Each is gated
on the fidelity suites staying green with the option *off*.

---

## 7. Effort and shape of the curve

Rough, in sittings of a few hours each, assuming the oracle is in place from 0c:

| Phase | Slices | Sittings | Notes |
|---|---|---|---|
| 0 | 6 | 4–6 | 0a and 0c are done; 0b needs BeebAsm installed |
| 1 | 4 | 6–9 | mostly mechanical once the oracle harness pattern exists |
| 2 | 5 | 8–12 | the text screens are many but shallow |
| 3 | 4 | 10–15 | `LL9` and `MVEIT` are the densest code in the game |
| 4 | 4 | 8–12 | tactics is long but well documented |
| 5 | 2 | 4–7 | the synthesiser is the unknown |
| **Total** | **23** | **40–60** | before modernisation |

The curve front-loads risk: by the end of phase 1 the oracle has either proven itself or shown
where the 6502-semantics approach leaks (Risk R4), and by 2e there is a playable build.

---

## 8. Owner decisions — taken 2026-09-02

| # | Question | Ruling | Where it landed |
|---|---|---|---|
| 1 | Licence posture | **Intend to publish eventually.** Not a permanently private port. | ADR-001 §5; new slice **0e Permission**; Risk R1 rewritten |
| 2 | Application shell | **Keep MSIX packaging, drop WinUI 3.** A packaged Win32 desktop app: raw window, flip-model swap chain, no XAML. | ADR-005 §5; slice 0d |
| 3 | Where the upstream tree lives | **Vendored** at `Upstream/elite-source-code-library`, pinned at `aa3f7ee`. | ADR-001 §5; ADR-004 §3; slice 0a, built |
| 4 | Presentation code | **Ignore the sibling repositories; write our own.** No `NeuronClient`; presentation lives in `Outpost.exe`. | ADR-004 §1; §2 of this document |
| 5 | Platforms | **Left as they are** (x64, x86, ARM64 in the solution), which is not what the plan recommended. Removing platforms was not put to the owner and is not a change to make unilaterally, and keeping MSIX makes ARM64 more plausible rather than less. x64 is the platform that is built and tested; the others are unmaintained. | ADR-004 §1 |

### Owner decisions — taken 2026-09-03

| # | Question | Ruling | Where it landed |
|---|---|---|---|
| 6 | Risk R3: how is the step rate determined? | **Count cycles**, rather than pick a number and tune by feel. | Built the same day: `Cpu6502` counts cycles, `CycleTests` validates it against two hand-counted routines. R3 rewritten; **§6.17 records that the question was mis-framed** — the C64 loop has no frame cap, so the rate is a consequence, not a setting, and the model splits into a cycle-budgeted main loop plus a 50 Hz PAL vertical-sync tick. |
| 7 | How is slice 2e verified, given it needs a window? | **Split the criterion.** The replay-hash half runs in CI through a null presenter; the legibility and feel half is a human sign-off. | ADR-003 §3; the 2e row |
| 8 | Slice 0e: the repository is public and tracks `MasterFile/`. | **Make the repository private.** | The 0e row; Risk R1, now marked realised. **Owner action, not yet taken** — no tool available here can change repository visibility. |
| 9 | The portable test runner that existed only in a working directory. | **Commit it and gate CI on it.** MSVC stays the authority. | ADR-004 §6; `Tests/PortableRunner/`; the `linux-tests` job |

**Decision 8 is the one that was not a design question.** It records a policy this document had
already stated and the repository had never met, and the fix is one setting in GitHub. The
engineering half — `Upstream/` as a submodule, no assembled binaries committed, nothing lifted
into `GameLogic/` — did hold, which is why the exposure is 13 files rather than 3,000.

**Decisions 1 and 3 pull against each other and the corpus says so.** Vendoring places roughly
3,000 unlicensed files in the tree, and publishing that tree is the largest legal exposure in
the project. The structural mitigation is that everything not ours lives under `Upstream/` and
nowhere else, and the assembled binaries are never committed — so the decision stays reversible
by removing one directory rather than by unpicking a history. Slice 0e is what resolves it, and
nothing is pushed to a public remote before it closes. See ADR-001 §5 and Risk R1.

---

## 9. Revision log

| Date | Change |
|---|---|
| 2026-09-03 | **The status screen built, and a cursor move that had been missing since slice 1c-a.** Control code 9 is `LDA #21 / JSR DOXC / JMP TT73` — tab to column 21, then a colon — and the port printed the colon while leaving a comment saying the cursor move would land with the canvas. It never did, and `STATUS` is the first routine to notice: it prints four headings through that code and all of them came out at whatever column the previous line ended in. The token printer now takes an optional cursor. The rating turns out not to be a table but a shift count, so its bands double in width. |
| 2026-09-03 | **All three market screens built, and the 2e deferral shown to be wrong.** `TT219`, `TT210` and `TT213` ported against a `KeySource` seam and compared character for character with the cursor stamped — twenty scripted scenarios between them, plus the cash, the hold, the market, the seam ordering and the random state. Findings: `dn` falls through into `dn2`, so the beep on buying comes from the cash printer; a quantity of zero skips the room check entirely; the purchase is committed before the cash is checked; the sell screen calls the line printer with printing switched OFF purely for its arithmetic, which makes **selling anything zero Alien Items' availability** — the fourth appearance of §6.16's side effect; and `TT69` falls into `TT67`, so every `JSR TT69` prints a newline as well. |
| 2026-09-03 | **`gnum`'s loop and the six state control codes built, and two blind spots in the method found (§6.18).** `ReadNumber` against a `KeySource` seam, thirteen scripted key sequences. `StateTokens` closes the `ValueTokens` seam open since slice 1c-a: `csh`, `tal`, `ypl`, `cpl`, `cmn`, `fwl`, compared character for character over 72 cases. Two findings in the game — `ypl` swaps the seeds, prints, and swaps back, but printing twists them, so what returns is not what left; and `cmn` bypasses the case flags, which is invisible unless the test sweeps both case states. Two findings in the method — a 393,216-case sweep that could not see the carry its caller branches on, and a mutation harness that could report the unmutated binary as passing. |
| 2026-09-03 | **Four owner decisions taken (§8), and one of them found a mis-framed risk.** *Count cycles* for R3 — built the same day, and scanning the assembled binary for `WSCAN` calls showed the C64 main loop has **no frame cap at all** (§6.17), so the rate is a consequence rather than a setting and the timing model splits in two. The counter is validated against `TT54` and `DORND` hand-counted from the source, both exact, and a mutation pass found one real gap in the branch timing. *Split 2e's criterion* between a CI replay-hash leg and a human sign-off. *Make the repository private* — recording that slice 0e's "nothing is pushed to a public remote" clause **was already false when written**, and that the fix is owner action no tool here can take. *Commit the portable test runner and gate CI on it* — ADR-004 §6, a third CI job, and the fast feedback loop stops living in one working directory. |
| 2026-09-03 | **Phase 2's computational core built: 2a's generator and 2c's price model.** All 2,048 systems compared on every field and every name; `TT111`, `LL5`, and the whole market — 34,816 prices and 512 generated markets. `var` turned out to zero Alien Items' availability as a side effect of computing an economy adjustment. **2a's description is blocked** on a chain nobody had drawn: §6.12 records it, and it ends at the justification line buffer. **2e is not startable** without the two owner decisions ADR-005 §5 and Risk R3 defer to exactly this point. |
| 2026-09-03 | **`gnum` built, and phase 2's boundary drawn.** One keystroke of a typed number, compared over 393,216 keystrokes by stepping the shipped routine to each of its five exits. Both carries in its multiply by ten are dead and provably so — the cap at 26 keeps every intermediate under 256 — which is the second dead carry the port has kept rather than simplified. Phase 2's computational content is now complete; what remains is the input layer, and **2e is not startable in this environment**: its criterion is that a person can play it. Its row records the two things the owner has to supply. |
| 2026-09-03 | **Slice 2c's market screen built.** `TT167`, `TT151` and the unit printers, compared character for character with the cursor stamped on each one, over 48 screens. §6.16 records `var`'s AVL+16 store surfacing for the third time — printing the screen makes Alien Items unavailable, and the whole-screen comparison caught it on the last character of the first case. |
| 2026-09-03 | **Slice 2c's trade arithmetic built.** `LCASH`, `MCASH`, `GCASH` and `tnpr`, the four routines buying and selling are built on, compared over 86,016 capacity checks and every price. §6.15 records the two off-by-ones that cancel — `tnpr` counts a tonne too many because a `CPX` left the carry set, and `CRGO` holds two more than the capacity it names. What is left of 2c is screens and keyboard input. |
| 2026-09-03 | **Slice 2d's format built.** The commander block, both checksums and the save layout, compared over 221 blocks against `CHECK`, `CHECK2`, `SVE` and `DFAULT`. §6.14 records what the block turned out to be: `CRGO` is two greater than the capacity it names, `CASH` is the only big-endian value in the game, and a bad save file makes the original hang rather than complain. |
| 2026-09-03 | **Slice 2b finished.** `hyp`'s target selection with `hm`, `ee3` and `TT147`, compared over all 1,024 combinations of its six inputs with every branch reached. Two findings: `hyp`'s range check is two tests, not one — 256 tenths or more fails on the high byte before the fuel is consulted, so a system 25.6 light years away is out of range with a full tank and says the same thing as an empty one; and `hy6` turns out to BE `dockEd`, the same routine under the cassette version's name. The scope line's `TT16a` was never the search: it prints "g" for grams. |
| 2026-09-03 | **Slice 2b's geometry and search built.** Both charts compared whole-screen against the shipped routines across all eight galaxies, `TT123` exhaustively, and `HME2` over 1,024 searches. Three findings: the two charts map the same galaxy with additions that differ only by a `CLC` the short-range one does not have; a system's disc is sized by a carry left over from `cpl` or from its own screen row, so `AND #1 / ADC #2` yields two, three or four rather than the two-or-three it reads as; and §6.13 records a shipped bug the port keeps. `CIRCLE2` and `SUN` are seams whose arguments are compared, so the stated "golden of both charts" criterion is replaced by something stronger and honest about what 3c still owes. |
| 2026-09-03 | **Slice 1c-c-b built, and phase 1 is closed.** Twenty of the thirty-one extended control codes, `DASC`, and the justification line buffer. `PDESC`'s 2,048 descriptions now compare character for character, which unblocks the last of slice 2a. Six of the nine codes the scoping note called blocked were not: §6.12 records the mirror image of the ledger problem — a dependency graph read too pessimistically. |
| 2026-09-03 | **Slice 1c-c split.** The number printers (`BPRNT`, `TT11`, `pr2`, `pr5`, `pr6`) are built and compared character for character over 308 numbers. The control codes are scoped from `JMTB` read out of the binary: eleven are pure state, nine wait on `TT66`, `NLIN4` or phase-2 mission state, and building only the eleven would turn a seam that counts what it cannot do into one that ignores it. |
| 2026-09-03 | **Slices 1a (screen half), 1b-d (completed), 1d-a, 1d-b and 1d-c built.** The screen tables and the font extracted; `FMLTU2` and `DVID4` ported; `Canvas` and the pixel primitives, `LOIN` over 3,528 lines, `CHPR` over 5,376 characters, `TT66simp`, and the golden harness with two oracle-derived goldens. Three defects caught by whole-screen comparison and recorded in §6.11. `DVID3B2`/`LL51` moved to 3a and `CIRCLE`/`CIRCLE2`/`BLINE` to 3c, both because they read workspaces phase 1 does not define. **1c-c is the only slice of phase 1 still open.** |
| 2026-09-03 | **Slice 1d-0 built, and 1d split into 1d-a/1d-b/1d-c.** The canvas representation spike measured the shipped drawing code instead of trusting ADR-002 §4's assertion, and the assertion did not survive: the C64 build of `PIXEL` writes masks that straddle multicolour pixel boundaries, which one colour index per pixel cannot express. ADR-002 §4 is amended to the four-plane design and gains a §7 of evidence; ADR-005 §1's screenshot dependency is answered from `ylookup`; 1c-c is ordered after 1d-b. §6.10 records it. |
| 2026-09-03 | **Slice 0a repaired.** `Upstream/elite-source-code-library` was a gitlink with no `.gitmodules`, so a fresh clone got an empty directory and `--check-includes` reported 0/712 — every slice from 1a onward unbuildable off the author's machine. `.gitmodules` added; the tree is a **submodule**, which is what it always was. ADR-001 §5's "vendored ... not a submodule" corrected, along with the `.gitignore` block that claimed the tree was committed. §6.9 records the finding, including that §5's "everything not ours lives under `Upstream/`" omits the 13 committed files in `MasterFile/`. |
| 2026-09-02 | Opened. Inventory of `MasterFile/`, target architecture, phases 0–6, owner decisions. |
| 2026-09-03 | **Slice 1c-b built** — the extended token system: the walkers, the case state, letter pairs, nested tokens and randomised variants, with `TKN1`, `TKN2`, `RUTOK` and `MTIN` extracted. Control codes are a declared seam and land with the canvas as slice 1c-c. §6.8 records why a table's size comes from its index range rather than from the next label. |
| 2026-09-03 | **Slice 1c-a built** — the recursive token printer, 243 of 250 tokens compared character for character in all five capitalisation states. The oracle gained **call traps** (§6.7), which is how output becomes comparable and is what the drawing slices will use too. `QQ18` and `QQ16` extracted; `EliteConfig.h` added for the build constants. |
| 2026-09-03 | **Slices 1a (in part) and 1b-d built.** The extractor pulls tables from the assembled binaries rather than the assembler source (§6.6), and the oracle now loads the data build too — thirteen blocks, 1,927 labels — which is what made the angle routine testable at all. Trigonometry and logarithm tables extracted and self-verifying; four logarithm routines ported and exhaustively compared. |
| 2026-09-03 | Owner ruling: *"You can ignore the use of VICE, no need to do a reference run."* **Slice 0b-b cancelled.** Its two dependents are re-answered in §6.5: goldens move from screenshot comparison to exact oracle comparison (ADR-003 §2 amended, R5 revised), and the step cadence **loses its mitigation outright** (R3). **Slice 0f built** — the determinism guard, proved against a planted violation. **Phase 0 is closed** but for slice 0e, which is owner action. |
| 2026-09-03 | **Slice 1b-b built** — the multiply-accumulate and division family, eight routines (§6.4). One real defect caught by the oracle, in the long division's carry chain. 1b re-split into 1b-c (state-dependent helpers, deferred to 3a) and 1b-d (the table routines, still waiting on 1a). |
| 2026-09-03 | **Slice 1b-a built** — the multiply and add core, nine routines, exhaustively verified against the shipped game (§6.3). Slice 1b split into 1b-a (built), 1b-b (divide, arctan and the rest) and 1b-c (the logarithm-table routines, which wait on 1a). |
| 2026-09-03 | Owner ruling: *"Do not strip WinUI, ignore it and proceed."* Slice 0d deferred to phase 2 (ADR-005 §5); the `Outpost` project is left untouched. **Slice 0b-a built** — BeebAsm built from source, the variant assembled, 1,782 labels exported, and the oracle now calls the shipped game by name (§6.2). `Oracle.json` dropped in favour of repository-root discovery (ADR-003 §1 amended). R9 exercised. |
| 2026-09-02 | All five decisions taken (§8). Upstream vendored, so §1.1 changes from "absent" to a resolved check. Architecture loses `NeuronClient`: presentation is our own and lives in the executable. Shell is packaged Win32, not WinUI 3. Slices 0e (permission) and 0f (determinism guard) added; 0b marked blocked on BeebAsm. **Slices 0a and 0c built** — §6.1 records what landed. |
