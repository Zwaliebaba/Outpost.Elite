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
**Phase 0 is closed**, bar slice 0e — which turned out to have been breached before it was
written: the repository is public and has tracked `MasterFile/` since `92a3c7f`. Ruled 2026-09-03,
after a reversal the same day: **it stays public, knowingly.** That makes the exposure an accepted
decision rather than an unexamined default; it does not close the slice, which needs a written
answer from the rights holders. The rest: 0a (upstream referenced, §6.9), 0b-a (the
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
| **0e Permission** 🟠 **Exposure accepted; the permission itself is still open** | Approach the rights holders about the intent to publish (ADR-001 §5). ~~Until it closes, nothing is pushed to a public remote.~~ **That clause was already false when it was written** and is now gone: `Zwaliebaba/Outpost.Elite` is public — confirmed against the GitHub API rather than assumed — and `MasterFile/` has been tracked since `92a3c7f`. Those 13 files are 5,615 lines carrying "copyright D. Braben and I. Bell 1985" and Moxon's commentary copyright in their own headers. The structural mitigation did hold: `Upstream/` is a **submodule**, so the ~3,000 library files are a gitlink and not content, and no assembled binary is tracked. The exposure is the 13 masters and nothing else. | **Ruled 2026-09-03, reversing an earlier ruling the same day: the repository stays public.** The first ruling was to make it private; this one accepts the exposure knowingly instead. The value of that is that it is now a decision rather than a default — but it is an ACCEPTANCE, not a mitigation, and the slice does not close on it. **What closes 0e is a written answer from the rights holders.** Until then the position is "published knowingly, pending permission", and Risk R1 stays realised to say so. The submodule rule, the no-binaries rule and the nothing-copied-into-`GameLogic` rule all stay, because they are what keeps the decision cheap to revisit. |
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

### 6.37 The §6.12 pass on `LL9`, and four workspace sizes the layout settles exactly

Run before `LL9` is written, as §6.12 asks. `LL9` is twelve parts, and two of them are already
built — part 1's `EE51` and the whole of part 12 — so what is left is parts 2 to 11 with `LL51`,
`LL61`, `LL62`, `LL145`'s four parts, `LL118`, `LL120`, `LL123` and `LL129`.

**What it reaches outside itself, completely.** Twelve labels: `DORND`, `FMLTU`, `LL28`, `LL30`
and `LL38` (all ported in phase 1 or 1d), `SHPPT` and `LL30` (built this sitting), `LL145` and
`LL147`, `LL61` and `LL62` (this slice), and `PLANET` and `DOEXP` — two tail jumps into 3c and
the explosion, which the ledger already files elsewhere. So the hardest routine in Elite has
exactly two dependencies that are neither its own parts nor already built, and both are in its own
slice. §6.34 said this from a smaller sample and it holds up.

**A caller list built from `JSR` and `JMP` alone is wrong.** The first version of that analysis
reported `LL62` as having no callers anywhere in the C64 build, which would have made it dead code
and a candidate for a *Drop* row. It is reached by `BMI LL62` from part 8. A 6502 routine's callers
are its branches as much as its jumps, and a two-byte relative branch into another routine is
normal here rather than exotic — the same source has `BCS PL2-1`, `BMI DV9` and `BEQ LL41`. Any
scoping grep in this repository has to include the eight branch mnemonics or it will under-scope.

**`LL51` is not arithmetic, and not movement.** The ledger has it beside `dvid3b2` in row 98,
deferred to 3a because it reads `XX15` and `XX16`. Those exist now, and they exist *as part of
`LL9`* — `LL51` is called from parts 5 and 6 and from nowhere else in the game. It goes to
`ShipDraw.cpp` with them. That is the same correction §6.35 made to `DVID3B2` one row earlier, and
it is the second time one row's deferral reason ("it reads a workspace that does not exist yet")
survived into a home that the reason never justified.

**`LL147` is `LL145`'s other entry point**, in the same source file, and `LL9` calls one from part 9
and the other from part 10. The row covers the file, so coverage is fine and the *behaviour* is
two routines; and `LL145` has a second caller outside this slice entirely — `BLINE`, the planet's
ball line, which is 3c. 3b builds it, 3c uses it, and 3c's row should not schedule it again.

**Four workspace sizes, each confirmed three ways.** §6.8 says size a table from what indexes it.
Here the zero-page layout agrees exactly, which is worth recording because it is the first time
all three measurements have lined up without argument:

| Workspace | Indexed by | Bytes | Next label |
|---|---|---|---|
| `XX2` (face visibility) | a NIBBLE, and the largest face count of the 33 blueprints is 15 | 16 | `XX16` at 69, and `XX2` is at 53 |
| `XX16` (scaled orientation) | three vectors of six | 18 | `XX0` at 87, and `XX16` is at 69 |
| `XX15` (the geometry vector) | six, as three sign-magnitude pairs | 6 | `XX12` at 113, and `XX15` is at 107 |
| `XX12` (the dot products) | three results of two bytes | 6 | `K` at 119, and `XX12` is at 113 |

`XX3`, the projected-vertex heap, is the one that does not resolve so neatly: it is at **256**,
which is the stack page, and it is indexed by a whole byte out of the edge data with offsets up to
`+3`. So 259 bytes are reachable and the top of what it can address is where the 6502's own stack
is living. The port has no stack there and the game never fills more than 148 bytes of it (37
vertices, the Anaconda), but the size is a decision to take when it is written rather than one the
layout hands over.

**And two names that really are the same bytes.** `XX2` spans 53 to 68; `K3` is 53 and 54, `K4` is
67 and 68. The face visibility flags sit exactly on top of the projected screen position. They are
never live together — `SHPPT` is a `JMP` out of part 2, long before `EE30` fills `XX2` — but this
is §6.34 inverted: there, two names that looked like one thing were two, and here two names that
look like two things are one. A port that has separated them (this one has) must not then assume
they are independent; where the original relies on the overlap, the port has to make the copy
explicit and say why.

**One decision left open, deliberately.** `XX15` is six bytes to the geometry and four to the line
drawing, and they are the same six: `X1`, `Y1`, `X2`, `Y2` are `XX15` to `XX15+3`. `LL9` uses it
both ways within one call — part 8 stores a screen coordinate into `XX15(1 0)`, part 10 loads a
vertex pair into all six and `LL147` clips them back down to four. So `DrawWorkspace` and the
geometry vector are one workspace, and the port currently has only the first four bytes of it. The
choice is between adding two bytes and letting the geometry address `DrawWorkspace` as six, or
keeping two structures and copying between them at every vertex. The first is what the original
does; the second is what the port's existing field names make easy, and it puts a copy in the
hottest loop in the game where a divergence could hide. The recommendation is the first, and the
cost is that `x1`/`y1`/`x2`/`y2` stop being plain fields — which is a rename across `Lines.cpp`,
`Charts.cpp` and their suites, and belongs in its own commit before `LL9` rather than inside it.

### 6.36 A test that agreed with the game on every case and exercised one branch

The first `SHPPT` suite ran twelve ship positions through the shipped routine and through the
port, comparing the whole 10,240-byte canvas, the whole line heap, the state byte and both screen
coordinates after each one. Every byte matched. Every one of the twelve was rejected as off-screen
and nothing was ever drawn, so what the run actually proved was that the port agrees with the game
about doing nothing.

The comparison did not catch it. `Assert::IsTrue(drawn > 0)` did.

The cause is a scale. `DVID3B` returns **256 times the ratio**, not the ratio: the eight-bit
division in the middle of it computes `256 * A / Q`, and the shifts at the end put back the
difference between the two scaling loops and nothing else. Its upstream summary line says
`K(3 2 1 0) = (A P+1 P) / (z_sign z_hi z_lo)` and `PROJ`'s says `K3(1 0) = #X + x / z`, and both
are the ratio with the scale left off — informally true, and wrong by a factor of 256 for anyone
choosing test inputs. The test used z = 1 throughout, which is the closest a ship can be; at that
distance the `ORA #1` on the numerator alone puts the answer at 256, `K+1` is 1, and every ship is
off the screen. Both headers now state the scale, because the next routine that divides by z will
be read alongside the same summary line.

The rule this makes explicit, which had been practice without being stated: **a swept comparison
asserts that each named outcome occurred at least once.** `PLS6`'s four exits, `DVID3B`'s three
tails, `PROJ`'s three outcomes and `SHPPT`'s drawn / refused / half-written all carry a counter and
an assertion. Nine slices in, this is the first time a coverage assertion was the only thing that
failed — and it failed on the one test in the phase whose inputs are hand-chosen rather than swept,
which is not a coincidence. A sweep over all 65,536 pairs cannot miss the interesting half; twelve
positions written by hand can miss all of it.

One more equivalent mutation, and the same shape as §6.35's: rewriting `LL155`'s heap walk from a
do-while to a while passes everything, because the `length < 4` guard above it has already decided
the first test. Twelve of the thirteen mutations on this unit were caught; that one is the
thirteenth, and it is recorded rather than fixed.

### 6.35 Six places where a ledger row can name a routine no branch ever enters

The §6.12 pass on the projection chain — `DVID3B` → `DVID3B2` → `PLS6` → `PROJ`, which is every
pixel the space view draws — moved three routines between slices. The first two are the usual
finding. The third is a mechanism, and it is enumerable.

**`DVID3B2` is not movement code.** The ledger sends it to `Arith.cpp` and `ShipMove.cpp`, and it
was deferred to 3a because it reads `INWK+6..8`. Reading the ship block is not the same as
belonging beside the code that writes it: its two callers are `PLS6` here and `PLANET`/`PLS1` in
3c, and nothing in `MVEIT`'s tree touches it. It is the two instructions that turn a general
divide into "divide by this ship's distance", so it is built with the projection.

**`PLS6` is not planet code.** The ledger groups `pls3`–`pls6` with the planet and sun drawing,
which is 3c. `PLS3`, `PLS4` and `PLS5` really are that, and none of them falls through into
`PLS6`; `PLS6`'s only callers in the whole game are `PROJ`'s two `JSR PLS6`, and `PROJ` is 3b. The
row was scoped by a RANGE OF LABEL NAMES, and four consecutive names turn out not to be four
related routines. That is a cheaper mistake to make than §6.34's and a cheaper one to find —
`grep "JSR PLS6"` answers it — but it is a third distinct way for a row to be wrong, after "what
it is about rather than what it touches" and "two structures with confusable names".

**`PL2` is in slice 3b because of one byte, and the byte is not `PL2`'s.** `PROJ` returns an
overflow with `BCS PL2-1`, and `PL2-1` is `PROJ`'s own `RTS` — the last byte before the next
routine starts. `PL2` itself is `LDA TYPE / LSR A / JMP WPLS2 / JMP WPLS`, the planet and sun line
heap eraser, which belongs with `wpls`/`wpls2` in 3c. Nothing in 3b calls it. The row named the
routine the address arithmetic points NEAR rather than the code the branch reaches.

That one generalises, and it is worth measuring rather than worrying about. The C64 build has 43
branch or jump targets written as a label minus an offset, 33 of them distinct. Twenty-seven are
an alternative entry point into the same source file — `MVT1-2` is one, two bytes earlier in
`mvt1.asm` so that the caller can skip an `AND #%10000000`, and slice 3a ported both entries from
the one row correctly. **Six are not**, and land in the file BEFORE the one that defines the label:

    LASLI-1   LL10-1   PL2-1   SFS1-2   WPLS-1   ypl-1

Every one of those is a row that can be placed by a routine no branch ever enters. `PL2-1` is now
resolved. The other five are worth checking when their slices open rather than now — `LL10-1` is
slice 3b's own and will be met on the way through `LL9`.

**And one mutation the sweep does not catch, because there is nothing to catch.** `DVID3B`'s
denominator loop puts its `DEY` at the top and its test at the bottom, and the first draft of the
header comment called that load-bearing: a do-while, so the first shift always happens. Rewriting
it as a while-loop passes all four sweeps. It has to: `A` is `S AND %01111111` on entry, so bit 7
is clear and a while-loop enters too — which is also why the `BMI DV9` commented out above it in
the upstream source could never have branched. The comment was a §6.29 in miniature, a plausible
reading with a justification attached, and the mutation is what said so. Seventeen other mutations across the four
routines were caught; this one is recorded as EQUIVALENT rather than as a gap, and the comment
now says which it is. An eighteenth is neither: taking the `ORA #1` out of `DVID3B2` makes the
port HANG rather than answer wrongly, which is what the original does too and what the header
says it does.

### 6.34 The same name for two different things, and a slice scoped around the wrong one

The §6.12 pass on slice 3b, run before any of it was written, as §6.12 asks. Three corrections,
and the middle one is a shape the pattern has not taken before.

**`LL5` is already ported.** The 3b row lists it as scope; it is the square root, phase 1 built it
for `TT111`, and `Arith.cpp` has carried it since. The row over-scopes rather than under-scopes,
which is the milder failure but still means the slice looks bigger than it is. `LL28` and `LL38`
are in the same position — named in the pipeline, ported in phase 1.

**The row names the wrong line heap.** It says *"the ship line heap (`LSX2`/`LSY2`)"*, and those
are two different things:

- The SHIP line heap is per ship, lives in the region below `LS%`, is allocated by `NWSHP` from
  `SLSP` and is pointed at by `INWK+33/34`. Slice 3a modelled it, because `NWSHP` refuses a ship
  when it runs out.
- `LSX2` and `LSY2` are the PLANET AND SUN line heap, with `LSP` as their pointer, written by
  `WPLS2` and the sun's own drawing. The plan puts those in **3c**, not 3b.

So the row names 3c's heap while describing 3a's, and does not name the one 3b actually uses.
Nothing was broken by this — it was found by looking — but a slice planned from it would have
budgeted for the wrong data structure.

This is worth separating from §6.12's usual finding. That one is *"the row was written from what
a routine is about rather than what it touches"*. This one is **two structures with confusable
names, and the row picked the wrong one** — which no amount of asking "what does this read?"
catches, because the answer is a name that exists and is real. What catches it is asking who
WRITES the thing, which is a different question and took one grep.

**`LL9` jumps to `DOEXP`**, the explosion, which the ledger files under `Explosion.cpp` and the 3b
row does not mention. A seam rather than a surprise, and now named before it is reached.

And one finding in the other direction, worth recording because it makes the slice cheaper than
it looks: **`LL9` is almost self-contained.** Its only calls outside itself are `DORND` and
`FMLTU`, both ported, plus that one jump. The dozen `LL` labels the row lists are its own parts.
A routine with the reputation of being the hardest thing in Elite turns out to have two external
dependencies, and both already exist.

### 6.33 Two defects one call could not have found

`MVEIT` is slice 3a's acceptance criterion and the plan wrote it as *"run `MVEIT` on a slot with
sampled orientations/speeds/roll/pitch for N iterations; byte-identical `INWK`"*. The N is the
part that earned its place.

Eight routines were ported ahead of it — the blueprints, `NORM`, `MULT3`, the bubble, `NWSHP`,
`MVT1`/`MVT3`/`MVT6`, `MVS4`/`MVS5`, `TIDY` — and every one matched the shipped game on its first
run. `MVEIT` did not, and the two things wrong with it are both invisible to a single call:

- **`BPL MV43` branches to the SUBTRACTION.** Every other sign test in the file reads "signs
  agree, so add"; this one reads "signs agree, so subtract", because the branch target is the
  subtracting path and the fall-through is the adding one. Written the natural way round it is
  wrong for half its inputs, and on a still ship with nobody turning it is wrong by exactly one in
  the low byte of y.
- **The `ADC` after `JSR MLTU2` has no `CLC`.** It runs on the carry the multiplier exits with —
  `MLTU2` ends `DEX / BNE / RTS`, neither of which touches the flag, so what survives is the carry
  from its final `ROR P`. The port's `MultiplyWide` returned only the byte and discarded it.

Neither is exotic and both are the same family as §6.29's: arithmetic whose meaning depends on a
flag that no instruction in sight sets. What made them findable was iterating. **A single call
compares one step of arithmetic; twenty calls compare the FEEDBACK** — the damping in the tail,
the sixteenth-pass `TIDY`, the acceleration cleared each iteration — and a routine that is out by
one in a byte nothing immediately reads agrees for one pass and separates over twenty.

That is worth generalising, because it is cheap to act on: **where a routine's output is fed back
into its own input, compare a RUN and not a call.** The cost is one loop in the test; the return
is every error that compounds rather than showing.

The seam counts turned out to be worth asserting for the same reason. `SCAN` and `TACTICS` are
stubs here, but how OFTEN each is reached is behaviour: an ordinary ship is scanned twice a pass,
an exploding one once, and the sun never — and a missile runs its tactics every pass where a Krait
runs them one pass in eight. All of that is `MVEIT`'s control flow rather than its arithmetic, and
`INWK` alone would not have shown any of it.

### 6.32 The ship blueprints cannot be thirty-three arrays, and the data says so three ways

Phase 3 opened with §6.12's own instruction — one pass over the ledger asking only "what does
this read?" — and it paid immediately. The 3a row lists the motion and slot routines and does not
mention the ship blueprints; the ledger files them under 3b, because that is what they are ABOUT.
But `MVEIT` reads byte 15 of the blueprint on every iteration to clamp acceleration against the
ship's maximum speed, and `NWSHP` reads bytes 5, 14 and 19 before the ship exists at all. 3a
touches them, so 3a cannot be compared against the shipped game without them. That is §6.12's
pattern for the seventh time, and the first time it was caught by looking rather than by failing.

Extracting them then turned up why they cannot be thirty-three arrays, and the reasons are
measured rather than argued:

- **Two blueprints overrun their neighbour.** A blueprint's length is its own header: twenty
  bytes, then `(XX0),8` bytes of vertices, four times `(XX0),9` of edges and `(XX0),12` of faces.
  For thirty of the thirty-three that is exactly the distance to the next blueprint. The splinter
  claims 24 bytes more than it has room for and the Thargon 60, so their tails are read out of the
  ship that follows. Slicing per ship truncates them.
- **The labels cannot arbitrate**, because those same two are the only blueprints in the build
  with no `SHIP_x_EDGES` label at all. A third, the Asp Mk II, has four bytes of slack, so the
  labels are not a tight partition either.
- **The game has no concept of a blueprint's end.** `NWSHP` puts an address in `XX0` and every
  read is `LDA (XX0),Y`. Extent is something a port would be inventing.

So the region is extracted whole — `XX21`, `E%` and all 33 blueprints, 8,073 bytes from `&D000` —
and indexed by address, which is what the original does and what makes the pointer table usable
without translating anything.

**Then the same mistake happened again, one level up.** The extraction sized the region from the
`SHIP_` labels, immediately after concluding the labels are unreliable, and the first run of
`ShipDataTests` failed on a blueprint `XX21` names that has no label. Chasing it found three
entries pointing at 1, 24865 and 41120 — zero page and the middle of two code blocks. They are not
blueprints: the pointer table is 33 entries, not the 39 the walk assumed, and past the end it
returns `E%`'s default-flag bytes as addresses. The upstream source says so in one line,
`NTY=33:D%=&D000:E%=D%+2*NTY`, which settles the table's length, the region's base and where `E%`
begins, all three.

The lesson is the sharper form of §6.8's rule. "Size a table from what indexes it" is not just
about the table's END: **the INDEX has an extent too, and a walk that runs past it does not
fault.** It returns whatever is next, and here what is next was plausible enough to send the
search after a missing blueprint instead of a wrong bound.

### 6.31 A shader compiled at run time is a shader nobody has read

The presenter's first draft built its HLSL with `D3DCompile` from a string literal, which is the
normal thing to do and was, here, the one remaining hole in the argument this slice is built on.
Everything else in the shell went onto the Windows CI leg precisely so that code written on a
machine that cannot run it would at least be READ by a compiler. Twenty-five lines of HLSL sat
outside that: nothing in the build touched them, and the first check they would ever get was a
person launching the game and seeing a black window with an eight-digit HRESULT in a message box.

So the shaders became `.hlsl` files that FXC compiles into C arrays in `$(IntDir)`, which the
presenter includes. A typo in them is now a build error like any other. Two things fall out of it
that were not the reason for doing it: `d3dcompiler.lib` and its `d3dcompiler_47.dll` are gone,
which a packaged application would otherwise have had to carry; and `tools/check_projects.py`
gained `FxCompile` and `None` to its item types, because an `.hlsl` on disk that no project names
is not compiled by ANYTHING -- a worse version of the `.cpp` case that script already existed to
catch.

The general form is the one this slice keeps running into: **verification you cannot run is not
verification, and the question is always what would have to be true for a machine to check this.**
For the C++ it was a CI leg that builds the executable. For the shader it was moving it from a
string into the build. Neither needed a display, and both found or foreclosed something.

### 6.30 Two static libraries, one C++/WinRT, and thirty-one link errors nobody could have seen

Building `Outpost.exe` for the first time produced no compile errors at all -- about twelve
hundred lines of Win32 and Direct3D written on a machine that cannot run either, and the compiler
took all of it. It failed at the LINK step, thirty-one times over:

```
GameLogic.lib(TextPrint.obj) : error LNK2038: mismatch detected for 'C++/WinRT version':
    value '2.0.250303.5' doesn't match value '3.0.260818.1' in pch.obj
```

`Outpost` carries the `Microsoft.Windows.CppWinRT` NuGet package and therefore compiles against
3.0; every other project takes the 2.0 that ships inside the Windows SDK. C++/WinRT emits a
`detect_mismatch` pragma so that the linker refuses to mix them, and it is right to -- they are
different headers with different inline definitions of the same names.

**Nothing could have caught this earlier, and that is the point.** The defect has been latent
since the projects were created: `GameLogicTests.dll` links `GameLogic.lib` and both are 2.0, so
the suite has always been consistent with itself. The mismatch needs a binary that links the
library built one way against objects built the other, and until this slice no such binary was
ever produced. A CI leg that builds the executable is not a formality on top of a green suite; it
is a different question, and the first time it was asked it found something.

The fix is not to align the versions. It is that `GameLogic` and `NeuronCore` were never using
C++/WinRT in the first place -- they inherited it from the shared precompiled header, which
includes `winrt/Windows.Foundation.h` and does `using namespace winrt;` for the benefit of the one
project that needs it. ADR-004 makes `GameLogic` deterministic and platform-free, and AGENTS.md §5
calls C++/WinRT "a COM-helper sanction only", for the presentation layer's `com_ptr` and
`check_hresult`. A static library with no COM in it emitting a version directive is a constraint
on everything that links it, bought for nothing. So `NeuronCore.h` gained a `NEURON_NO_CPPWINRT`
guard, and `Outpost/pch.h` is now the single place that does not define it.

The general shape is worth keeping: **a dependency acquired by inheritance rather than by need
costs nothing until something else needs a different version of it.** The shared header is a
convenience, and the price of a convenience is paid by whoever links it.

### 6.29 The routine did not end where the line did

`TT66` sets the text state every docked screen is entered with, so the shell had to answer it and
`GameLogic` had to have it. The upstream source is a BBC BASIC assembler listing with several
instructions per numbered line, and line 9400 is the whole of `TT66` up to its last `JSR`:

```
.TT66 STAQQ11:.TTX66 JSRMT2: ... :LDA#128:STAQQ17:STADTW2: ... :LDA#1:STAXC:STAYC:JSRTTX66K
```

Read that and stop, and the answer is obvious: `QQ17 = 128`, sentence case. It is also wrong. The
routine continues across lines 9410 and 9420, and its last five bytes are `LDX #1 / STX XC / STX
YC / DEX / STX QQ17` -- so `QQ17` goes back to **zero**, ALL CAPS, and only `DTW2` keeps the 128.

What makes this worth writing down is not the mistake but how close it came to being committed
with a confident comment attached. The draft had a paragraph explaining that an existing comment
in `MarketScreen.h` -- which said all caps, and was right -- had quoted the *cassette* build and
that this build differed. It was a plausible story, it fitted the evidence available, and it was
entirely invented. §6.20 already says a hand-built oracle can share the port's misreading; this is
the same failure without the oracle: **a misreading plus a justification is harder to spot than a
misreading alone**, because the justification is what a reviewer reads instead of the source.

The rule that caught it is §6.20's, applied without exception: where a routine can be run, run it.
`TT66` can -- `TTX66K` and `FLFLLS` are the only things in it that need a VIC-II, and neither
touches a byte of text state -- so `TheScreenSeamsMatchTheShippedRoutines` traps those two, calls
the shipped routine, and compares `XC`, `YC`, `QQ17`, `DTW1`, `DTW2` and `DTW6` against the port.
It cost about what reading the routine a second time would have cost, and unlike reading it a
second time it could not have agreed with the first reading.

There is a smaller finding underneath it, which is why the seam was worth testing at all. The
per-screen oracle tests set this state up **on both sides**, so a seam that got it wrong would
agree with the game on every screen in the suite and still print every one of them in the wrong
case. The session harness of §6.27 set `XC`, `YC` and `QQ17` from a comment and did not set `DTW1`,
`DTW2` or `DTW6` at all. Neither was a test failure waiting to happen; both were tests that could
not fail.

### 6.28 One 6502 byte, two C++ variables

`QQ17` is the capitalisation state, and the port keeps it in two places: `TokenPrinter::m_caseFlags`,
which is the live one every printing path reads and writes, and `TextState::caseFlags`, which
`CHPR` reads for the single value 255 ("print nothing at all"). They are one byte in the game.

Nothing has gone wrong yet, because until slice 2e every caller that assigned `QQ17` was inside
the token printer and only touched its own copy. `SetUpTextScreen` is the first routine outside it
that holds both, and it has to assign both -- which is a rule a reader has to know rather than one
the types enforce, and exactly the kind of rule that is obeyed until it is not.

The fix is to give `TokenPrinter` a reference to the `TextState` it already has a pointer to and
delete the duplicate, which is a change to the token printer, the character printer, every screen
that constructs one and about a dozen tests. That is a refactor with no behavioural content, and
doing it inside a slice whose subject is the window would mean a diff where the risky part is
invisible among the mechanical part. It is written down here instead, with the note that the
duplication is REAL and not a false alarm: two variables holding one byte, kept in step by
convention.

### 6.27 What a null presenter can and cannot verify

Slice 2e's acceptance criterion was split on 2026-09-03: a replay through a null presenter, which
needs no window and no GPU and therefore runs on the Ubuntu leg, plus a human sign-off on
legibility and cadence. The first half is now built, and building it turned up the boundary
between the two.

**What it does verify** is that the pieces compose. Every routine in a docked session is compared
against the shipped game somewhere else in the suite, one at a time; what no per-routine test can
say is whether the market screen reads the economy the start sequence cached, whether the
equipment shop is handed the tech level the chart selected, or whether two tonnes of food bought
on the buy screen are two tonnes in the hold when the inventory prints it. The session test asserts
exactly that last one: the hold goes from five tonnes to seven and the cash falls by twice the
price the market quoted -- three routines, three tests of their own, and nothing until now that
checked they were talking about the same tonne of food.

It also does something a hash would not: it keeps a TRANSCRIPT. A hash catches drift and tells you
nothing about what drifted; a line of each screen's output is diffable, and reading it is how you
notice that a screen has gone blank rather than merely different.

**What it cannot verify is LAYOUT**, and the reason is structural rather than an oversight. `CHPR`
advances the cursor, and `CHPR` is the presenter -- so with a null one the cursor only moves where
a routine moves it deliberately, through `INCYC` or `DOXC`. Every character in the transcript is
stamped wherever that left it. The per-screen oracle tests DO compare the cursor, and can, because
there the shipped CHPR is trapped on both sides and neither advances it; but a session driven
through a null presenter has no cursor to compare.

So layout is verified per screen and not across a session, and "is every docked screen legible" --
the human half of the criterion -- is the only thing that covers a session's layout at all. That is
not a gap to be closed by writing more tests here. It is what the split was for, and it is worth
being explicit that the CI half being green is not the criterion being met.

The other thing the harness is, incidentally, is a specification of the composition root. One
object satisfies five separate seam interfaces -- `TradeScreenEffects`, `ChartEffects`,
`LineEntryEffects`, `StartUpEffects` and `ControlCodes` -- which is what ADR-004 says the
executable does, and this is the cheapest available check that those five declarations are
mutually consistent. `Game.cpp` in the executable has to hold what `Session` holds.

### 6.26 Being richer is not being more eligible, and DOENTRY is not the entry point

Two findings, and the smaller one first because it is the reason the larger one was found at all.

**`DOENTRY` is not the program's entry point.** The ledger had it beside `COLD`, `BRKBK` and
`startup` in a row marked "Replace — the C64's NMI handling and memory banking". It is the DOCKING
routine: "do entry" means entering the station. What it actually does is reset six flight
variables, show the docking tunnel, pause for forty-four vertical syncs and then decide which of
six mission briefings the player has just earned. All of the deciding is arithmetic on the
commander block, and every one of the three things it reaches outside the slice was already a
seam or a phase-3 routine.

That is the **sixth** stale scope line this port has found, and the first that was wrong about
what a routine IS rather than about what it needs. The previous five all had the same shape — a
row written from what a routine is *about* — and this one is worse: it was written from the
routine's NAME.

**And the Trumbles mission compares one byte of four.** `EN4` is `LDA CASH+2 / CMP #&C4 / BCC
EN6`. `CASH` is four bytes, most significant first, holding tenths of a credit — and this reads
the third of them and ignores the two above it. So the condition is not "at least 5017.6
credits". It is

    (tenths >> 8) & 255 >= 196

which is a band 1,536 credits wide that recurs every 6,553.6. A commander with 5,017.6 credits is
offered the mission. One with 10,000 is not. One with 11,571.2 is again. Getting richer moves you
in and out of eligibility, over and over, for ever.

The upstream source's own comments on those three instructions say "the cash amount is less than
&C400 (5017.6 credits)" and, four lines later, "we have at least 6553.6 credits". They disagree
with each other, and neither describes what the code does — which is a fair reflection of how hard
this is to see. The port has a test that walks nine cash amounts across two bands for exactly that
reason: the obvious improvement, comparing the whole four-byte value, passes every other test in
the file and fails that one.

It is worth being explicit that this is kept rather than fixed. It is reachable, it changes
whether a mission is offered, and ADR-001 puts fidelity first — the same ruling as the disk menu's
stack frame in §6.21. What the port owes is not a correction but a comment, and a test that will
fail if someone later supplies one.

### 6.25 The commander's position is the current system, and the oracle had to say so

Building `BR1` meant giving the port somewhere to keep "where the ship is", and the obvious answer
— a `CurrentSystem` holding x, y, seeds, economy, tech level and government — is wrong. The test
failed on the commander's byte 1, and the reason is a memory map: `QQ0` is `TP+1` and `QQ1` is
`TP+2`. The ship's galactic coordinates are two bytes of the COMMANDER BLOCK. `QQ2`, `QQ28`, `tek`
and `gov` are eighty-five bytes further on and are not.

So `ping` and `jmp` — the two-line routines that move the crosshairs to the ship and back — read
and write the commander itself, and a hyperspace jump is a change to the saved game rather than to
a variable beside it. That is why your position survives a save, and it is invisible from either
routine: both are four instructions with no hint of what they are addressing.

A port that kept a separate copy would work until something wrote one and read the other. The
oracle found it on the first run of the first script, which is the argument for comparing MEMORY
rather than return values: the port and the game agreed on every number and disagreed about where
one of them lived.

Two more things `BR1` turned out to be, both of them fall-throughs the source does not mark:

**The cold start resets twice.** `TT170` is `LDX #&FF / TXS / JSR RESET`, and `RESET` has no RTS —
it runs off its end into `RES2`. Then `TT170` itself falls into `DEATH2`, which is the same three
instructions with `JSR RES2`. So a cold start runs `RES2` twice, and it is not idempotent: it
toggles the energy bomb off and stops the bulletin board. Collapsing the two calls would be a
different game, and nothing at any call site says there are two.

**And `BR1` does not return.** It runs off its end into `BAY`, which sets the docked flag to `&FF`
and presses "8" on the player's behalf. Starting a game and arriving at the docking bay are one
instruction stream — which is why the test knows the routine is finished by watching for `BAY`
rather than for an RTS, and why proving it got there is the fall-through proved.

### 6.24 A path relative to the wrong thing, and the check that should have existed

The Windows leg went red on `error C1083: Cannot open source file: '..\Outpost\SaveStore.cpp'`.
The project is at `Tests/GameLogicTests/`, and MSBuild reads an `Include` relative to the PROJECT
rather than to the repository — so reaching `Outpost/` needs two `..`, not one. A second to make,
four minutes of Windows CI to report, and invisible on the Linux leg, which globs its sources and
never reads the project file at all.

`tools/check_projects.py` now runs on the Ubuntu leg and resolves every `ClCompile` and
`ClInclude` path the way MSBuild does. Writing it turned up three problems that were already in
the tree and had nothing to do with the change that prompted it:

- `GoldenCanvas.h` was in the test project's `.filters` and not in the project.
- `SaveStore.cpp` and `SaveStore.h` were in `Outpost.vcxproj` and in neither its filters.
- and the check for the OTHER direction — a source on disk that no project names — matters more
  than the resolution check does. The portable runner GLOBS `GameLogic/*.cpp`. A file added to the
  tree and forgotten in the project compiles, runs and passes on Linux, and is simply absent from
  the Windows build: a green suite testing less than it says, with nothing anywhere to say so.

The general shape is worth naming because this repository has two builds of the same sources and
they disagree about how they find them. One globs and one enumerates. Wherever two mechanisms
answer the same question differently, the cheap one has to check the expensive one — otherwise the
expensive one is the only place a drift shows, and it is the one that runs least often.

### 6.23 "No oracle" is not "no test", and the difference was a defect

`Outpost/SaveStore.cpp` shipped with a header saying, in as many words, that nothing in it was
covered by the suite and that this was the whole reason the seam was drawn where it is. The first
half was true and the second was an argument that does not follow.

There is no ORACLE for the file: the C64 handed the Kernal a filename and an address range, and
this writes eighty-five bytes to a path under LocalAppData, so there is no shipped routine to
compare it against. But the format, the checksums, the competition number and every failure path
are already compared against shipped routines in `GameLogicTests`, and what is left on this side
of the seam — a path and two streams — has properties worth asserting even without a routine to
disagree with.

It had gone uncompiled for a day as well, because the only machine that could build it ran Windows
and CI does not build `Outpost.vcxproj` (it would restore the whole WinUI 3 package set to compile
two files). The file makes exactly one Win32 call. Ten lines of `GetEnvironmentVariableW` in the
portable runner's shim, one source added to its makefile, and one constructor taking an explicit
root instead of reading the environment, and it compiles and runs on both legs.

**The first test written against it found a defect.** `PathFor` accepted any name of letters and
digits and refused everything else, on the argument that the name reaches it from a FILE as well
as from the keyboard and is therefore untrusted. That argument was right and the filter was not
sufficient: every legacy Windows device name — `CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`,
`LPT1` to `LPT9` — is made of letters and digits, is seven characters or fewer, and is typeable at
the commander name prompt. Win32 resolves such a stem to the DEVICE whatever directory precedes it
and whatever extension follows, so a player calling themselves CON would have had their commander
written to the console, with the write reporting success, and read back from it.

The near misses matter as much as the hits and the test walks them: `COM0` and `LPT0` are not
devices, and neither is anything with a character appended, so a rule matching a prefix would
break four ordinary names to fix twenty-two broken ones.

The rule this argues for is narrower than "test everything": **when the reason something is
untested is that it cannot be built here, price the shim before accepting the reason.** It was ten
lines.

### 6.22 One number, two words, and a jump into the middle of the second

`TT25` prints eight lines and three of them are worth writing down, because all three would pass a
test that compared only the values it is built from.

**The economy is two words out of one three-bit number, and the branch that picks them is a
comparison against a shifted value.** `(QQ3 + 1) >> 1` comes out 0, 1, 1, 2, 2, 3, 3, 4 across the
eight economies. Where it is 2 -- economies 3 and 4 -- the routine prints "Mainly" and jumps to
`TT72`, which is in the MIDDLE of the code for the second word, past the `ADC` the first word
needed. Otherwise `BCC` asks whether that shifted value is below 2, and where it is not, `SBC #5`
folds economies 5, 6 and 7 back onto 0, 1 and 2. So "Rich", "Average" and "Poor" are three tokens
serving six economies, and the fold is invisible unless you notice that the `BCC` reads the `CMP`'s
carry and not the `LSR`'s -- the `LDA QQ3` between them leaves the flags alone. A port that took
the shift's carry there gets four of the eight economies wrong, and only ever on the second word.

**The inhabitants are four words, three of them optional, and the third one feeds the fourth.**
Size, colour and appearance print only when their own index falls below the length of their table
(3, 6 and 6); the noun always prints. But the appearance's index is STORED in `QQ19`, and the
noun's index is `(QQ15+5 & 3) + QQ19` — so a system that loses its appearance word because the
index came out too high still has that index folded into what its inhabitants are called. The four
lines are one calculation with three early exits, not four independent lookups.

**And the radius is assembled from two seed bytes with a constant in the middle.** The low byte is
`QQ15+3` as it stands; the high byte is the bottom nibble of `QQ15+5` plus eleven. So every planet
in Elite has a radius between 2,816 and 6,911 km — no gas giants, no asteroids, and the eleven is
the only reason. It is a `CLC / ADC #11` in the middle of a line that otherwise looks like masking.

The screen was in 2a's row as "cursor and canvas work", which is **the fifth stale scope line this
port has found** (§6.12, and then the trading screens, `STATUS`, `EQSHP` and 2d's name entry). The
pattern has not changed: the row was written from what the routine is *about* rather than from what
it *touches*, and what it touches is a token printer, a number printer and one seam that already
existed.

Building it also produced `tools/c64_source.py`, which is overdue. The upstream library serves ten
versions of Elite from one tree, and the scratch filter used up to now silently dropped
`IF NOT(...)` blocks — which is how `SV1`'s `LSR SVC` and half of `DFAULT` came out missing when
they were read by eye. The new one evaluates the conditionals against the master build's own symbol
values and errors on a symbol it does not know rather than guessing FALSE. AGENTS.md now says to
read routines through it.

### 6.21 The disk access menu, and the stack frame nobody pops

`SVE` is five options around routines slice 2d had already built one at a time, which made it look
like assembly work. Running it whole against the port found four things, and the first is a bug in
the shipped game that a player can hit in ordinary use.

**A failed load poisons every later exit from the menu.** `LOD`'s two error paths -- `tapeerror`
when the device refuses and `ELT2F` when the file is not a commander -- print a message, wait for
a key, and leave by `JMP SVE`. Not by an RTS. So the menu is re-entered while `loading`'s own
`JSR LOD` return address is still on the stack, and whatever the player does next returns *into*
`loading`, at the three instructions that were waiting there: `JSR TRNME / SEC / RTS`.

Press "5" to leave after a failed load and the game stores the name you typed over the last-saved
commander's, then tells `TT102` that a new commander was loaded -- so `TT102` jumps to `QU5`,
whose `DFAULT` makes that rename stick, and restarts the game instead of returning to the docking
bay. Nothing was loaded. Save instead of leaving and the same thing happens on top of a real save.
And the frame is pushed again on *every* failed load, so a player who fails a hundred and
twenty-eight loads without leaving the menu runs the stack into the zero page.

The port reproduces it with one flag, because `TRNME` is idempotent and N pending frames therefore
do what one does. It is worth being explicit about why: this is not a bug the port is free to fix.
The behaviour is reachable, it changes which screen the player ends up on, and ADR-001 puts
fidelity first -- so it is reproduced, named in the header, and pinned by four of the twenty-one
scripts.

**Option 4 returns with the carry set, and nothing in `SVE` sets it.** The default-commander
option ends `JSR JAMESON / JMP DFAULT`, a tail call, so what the caller sees is whatever `DFAULT`
left -- and `DFAULT` ends on `CMP CHK3 / BNE doitagain / RTS`, taking the RTS only when the two
agree, which is exactly when `CMP` sets the carry. The flag that tells `TT102` to restart the game
is a side effect of a checksum test three routines away. A port that read `SVE` alone would return
`CLC` here and be wrong.

**`BPRNT`'s field width is whatever the last caller left.** `SV1` prints the competition number
with `CLC / JSR BPRNT` and never sets `U`, which is the width. `U` is a scratch byte in zero page
that `ZERO` does not clear. The upstream source says so in as many words, and the consequence is
small -- the number always has ten digits, so all that varies is a leading space -- but it is
still caller state, so the port takes a `NumberWorkspace` by reference rather than choosing a
width. Two of the scripts vary it, and a mutation that gave `BPRNT` a fresh workspace was caught.

**And bit 6 of the competition flags is not what slice 2d's comments said it was.** They read it
as "this commander was loaded from a file"; the C64 source says `ORA #%01000000 \ Set bit 6 of A
to denote that this is the Commodore 64 version`, in a chain where the cassette build sets bit 1
and the Master bit 3. It is the platform stamp, and `DFAULT` sets it for the default commander
too. Corrected in `Commander.cpp`, `SaveGame.h` and the test that repeated it.

There is a fifth, smaller one that the two-commander split made visible. `GTNME`'s "you typed
nothing, keep the name you had" path is `TR1`, and `TR1` reads `NA%` -- the LAST SAVED commander's
name -- not `NAME`, the one being played. The two agree after any load or save and can differ
before one. A port that collapsed the save image into the live commander (which is tempting, since
every caller of `SVE` runs `DFAULT` immediately) gets this wrong *and* prints the menu's own
"2. SAVE COMMANDER <name>" line under the wrong name after a failed save. The image is a
parameter for that reason.

### 6.20 A hand-built oracle can share the port's misreading

Slice 2d compared `SaveCommander` against the shipped save over 221 blocks and it passed. It was
wrong, and the test could not have found it, because the test and the code were built from the
same reading of the same routine.

**What was missing.** `SVE`'s save path writes THREE checksums into the file: `CHK3` from CHECK2,
`CHK` from CHECK, and then -- four instructions past the competition number, sixteen further down
-- `CHK2`, which is `CHK EOR &A9`. The port wrote the first two and stopped. A file it saved would
load into the original with bit 7 of `COK` set: flagged as tampered by the game's own copy
protection.

**Why nothing caught it.** Three things had to line up, and they did.

- `LoadCommander` only READS `CHK2`, to decide whether to set that bit. It does not fail on it, so
  a save-then-load round trip through a port that got it wrong on both sides agreed with itself.
- The round-trip test skipped byte 74 as "written by the save", which was true and unexamined.
- And the comparison against the shipped routine **reproduced `SVE`'s arithmetic by hand** rather
  than running it. Its own comment explains the choice: stepping the copy loop and the two
  checksums directly was "less misleading than trapping six routines and hoping the remainder is
  the same shape". That reasoning is sound, and it has a cost -- a hand-built model is written
  from the same reading as the code, so it shares the same blind spots. Both stopped at the two
  CHECK calls.

**What found it.** Building the caller. `SaveGameTests` runs the real `SV1` up to `KERNALSETUP` --
the first instruction that would touch hardware -- and compares the whole eighty-five-byte file.
The first commander disagreed at byte 82.

The rule this argues for: **where a routine can be run, run it.** Trapping six routines and
comparing what is left is more work than stepping a model by hand, and it is the version that
cannot agree with a mistake. The hand-built test is kept for its breadth -- 221 blocks against the
new one's 67 -- and corrected, with a note pointing here.

### 6.19 One print, two meanings: how MT26 answers a key it will not take

The line editor is nineteen instructions and three of them are not what they look like. It is
worth writing down because the same three shapes recur, and because a port that read any of them
straight would be wrong in a way no test of the *characters* would catch.

**There is one `JSR CHPR`, and what reaches it decides whether the key appeared or beeped.** The
accepted path ends `STA INWK+5,Y / INY / EQUB &2C`, and `&2C` is `BIT absolute` — a three-byte
opcode that swallows the two bytes after it, which are `LDA #7`. So an accepted character arrives
at the print with the key still in A, and a rejected one arrives having just loaded the bell. The
routine therefore ALWAYS prints, and a port that simply ignored a bad key would be silently
different: the player would get no feedback where the game gives a beep.

**`BCC OSW0L` after that print is a jump.** CHPR returns with the carry clear — the same exit
slice 1c-c-b found the justification depends on — so the branch is always taken. Reading it as a
conditional suggests a way out of the loop that does not exist, and the only ways out are RETURN
and ESCAPE.

**RETURN is stored, not just detected.** `OSW03` writes the carriage return into the buffer at
the current length before it leaves, which is why a commander's name is eight bytes ending in 13
rather than a length and seven characters. The save format depends on it (§6.14).

Two smaller things. The accepted range is `'!'` to `'z'`, because `CMP RLINE+4 / BCS` refuses
`RLINE+4` itself and that byte is `'{'`. And `TRNME` falls into `TR1`, so storing a typed name
copies it straight back over the buffer it was read from — undetectable, provably, and kept for
the reason the source gives.

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
| **1a Data extraction** 🟡 **Twenty-six tables built 2026-09-03; the ship and sound data pending** | `tools/extract_tables.py` emits `*.cpp` data files, **extracted from the assembled binaries by label and length rather than by parsing the assembler** — see §6.6 for why that is the cheaper and truer route. **Built, twenty-six tables** (corrected 2026-09-03 — this row had gone stale and still listed half of them as pending, which `tools/inventory.py` cannot catch because it checks the ledger rather than this prose): the arithmetic tables `log`, `logL`, `antilog`, `antilogODD`, `SNE`, `ACT`, `TENS`; the text tables `QQ18`, `QQ16`, `TKN1`, `TKN2`, `RUTOK`, `MTIN` and the font; the drawing tables `TWOS`, `TWOS2`, `CTWOS2`, `DTWOS`, `TWFL`, `TWFR` and the four screen-address lookups `ylookupl`/`ylookuph`/`celllookl`/`celllookh`; and the game data `QQ23` and `NA2%`. **Still to extract**, and all of it belongs to phases 3 to 5 rather than being overdue: `RUPLA`/`RUGAL`, `XX21` and the ship blueprints, `E%`, `KWL%`/`KWH%`, `scacol`, the sound and tune tables, `sdump`/`cdump`, `dials`, `spritp`. | **Met for what is built.** Each array is compared byte for byte against the same address range in the oracle's image, so a stale or hand-edited table fails a test. `--check` does the same from the command line. Adding a table is one row in the tool. |
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
| **2a Universe** ✅ **Built 2026-09-03** | Seeds and galaxy (`TT20`, `TT54`, `TT111`, `TT18`, `TT146`), system data (`TT24`, `TT25`, `cpl`, `cmn`, `ypl`, `tal`, `fwl`, `pdesc`), the Data on System screen, `jmp`/`ee3` distances. | **Met for everything the screen does not need.** All 2,048 systems in all eight galaxies compared on economy, government, technology, population and productivity; all 2,048 names character for character; and — once 1c-c-b landed the control codes and the line buffer — **all 2,048 descriptions character for character** as well, which is the strongest single test in the port so far: token expansion, the randomised alternatives, the case machinery, the word wrap, the padding, `MT17`'s reach into the buffer and `MT18`'s random words all have to agree for one description to come out right. `TT111` over 864 searches; `LL5` over all 65,536 radicands. Galaxy 1 system 0 is TIBEDIED. **And the Data on System screen is built, 2026-09-03, which closes 2a.** The row above called `TT25` "cursor and canvas work" and it is neither: every line of it is a token, a number or a seed bit, and the only thing it reaches outside `GameLogic` for is `TRADEMODE` — the seam slice 2c built and four screens already use. **All 2,048 systems in all eight galaxies compared character for character with the cursor stamped, PDESC included**, so a description one random alternative adrift or a line wrapped a column early fails here. Seventeen mutations, fifteen caught and two provably equivalent. §6.22 records what the screen turned out to be. The DOS-port cross-check needs a machine this work did not run on. |
| **2b Charts** ✅ **Built 2026-09-03** | `TT22` long-range, `TT23` short-range, crosshairs (`TT15`, `TT14`, `TT16`, `TT103`, `TT105`, `TT123`), `NLIN`/`NLIN2`/`NLIN3`/`NLIN4`, `HME2` find planet by name, `hyp`/`hy6` target selection with `hm`, `ee3` and `TT147`. **The scope line named `TT16a` as "find planet by name" and it is not** — it prints "g", for grams, on the market screen; `HME2` is the search, and it is built. | **Met, and stronger than the stated criterion.** Not goldens but whole-screen comparisons against the shipped routines: both charts for all eight galaxies (the short-range one at four home positions), 1,200 crosshairs, 1,250 crosshair moves, 112 fuel circles, and `TT123` exhaustively over all 65,536 value-and-step pairs. `HME2` compared over 1,024 searches. **The stated criterion cannot be met and should not be**: the fuel circle is `CIRCLE2` and every system's blob on the short-range chart is `SUN`, both of which keep a line heap that belongs to 3c — so a golden of either chart would be a golden of a chart with its circles missing. They are seams whose ARGUMENTS are compared instead, which pins them before the drawing exists. `hyp` was scoped out on first reading as needing commander state, and that was too pessimistic in the way §6.12 describes: the state is an INPUT, not something this slice has to own, so it arrives as a value the way the market's does and the logic is ported. All 1,024 combinations of docked, countdown, CTRL, view, fuel and crosshair position compared — every one of the six branches reached — on the text, the cursor, the countdown, the seeds saved for the jump and the screen. What genuinely remains outside is `Ghy` (the galactic hyperdrive reads the equipment you are carrying), `hyp1`/`TT18` (arrival: the market roll and the tunnel), and `MT26`'s keyboard half of the `F` flow. |
| **2c Trade and equipment** ✅ **Built 2026-09-03** | `TT151`/`var`/`GVL` prices, `TT167` market, `TT219` buy, `TT210` sell, `gnum`, `TT213` inventory, `STATUS`, `EQSHP` with `prx`, `qv`, `refund`, `hm`, cash (`LCASH`, `MCASH`, `GCASH`), `TT162`/`TT160`/`TT161` units. | **Met for everything that is arithmetic.** Every price the game can quote — seventeen goods, eight economies, every value of the market's random byte, 34,816 in all — plus 512 generated markets. And with 2d's commander block in place, the four routines the buying and selling are built on: `LCASH`, `MCASH`, `GCASH` over every price, and `tnpr` over 86,016 capacity checks. §6.15 records what `tnpr` turned out to be. **The market screen and `gnum` are built too**: `TT167` with `TT151`, `TT152` and the unit printers, compared **character for character with the cursor stamped on every character** across all eight economies and six market randomisers — 405 characters a screen, 48 screens. Stamping the cursor is what makes it a real comparison; the characters alone would pass a port that printed every line one cell left. `gnum`'s body — one keystroke of a typed number — is compared over **393,216 keystrokes**, every value against every key at six availabilities, by stepping the shipped routine to whichever of its five exits it reaches. **`gnum`'s loop and the six state control codes landed 2026-09-03 as well.** `ReadNumber` is the loop, ported against a `KeySource` seam and compared over thirteen scripted key sequences; §6.18 records the testability defect that building it exposed. `StateTokens` closes the `ValueTokens` seam slice 1c-a opened — `csh`, `tal`, `ypl`, `cpl`, `cmn` and `fwl`, compared character for character over 72 cases — which is what every docked screen needs to print a cash or a fuel line at all. **All three market screens are built** — `TT219` (buy) over seven scripted key sequences, `TT210`/`TT213` (sell and inventory, one routine told apart by QQ11) over thirteen — each compared character for character with the cursor stamped, plus the cash, the hold, the market, the seam ordering and the random state. That is the proof the deferral was wrong: what these screens wait on is a KEY, and a key is a seam. **`STATUS` is built too** — twenty situations compared character for character, plus the case flags and the system `TT111` settled on. **And `EQSHP` closes the slice**: the equipment shop with `prx`, `qv`, `eq` and `refund`, over thirty-six scenarios, with the price table extracted from the binary like every other. **Slice 2c is complete.** |
| **2d Commander and saves** ✅ **Built 2026-09-03** | `NA%` default commander, `JAMESON`, `CHK`/`CHK2`/`CHK3`, `sve`/`lod` replaced by `SaveBlock` (the exact original byte layout so an original C64 save imports) + `SaveStore` in the exe writing to LocalAppData; `TRNME`/`GTNME` name entry; `DFAULT`; `MT26`'s line editor. `qu5`, the `Y/N` prompts. **The 2026-09-03 note here was wrong and is corrected:** it claimed `qu5` and `yesno` have no label in the assembled C64 build. They do — `YESNO` at 33262 and `QU5` at 34988 — and the check behind the claim had looked for the lowercase spellings just after diagnosing that exact trap for `TRNME`/`GTNME`. Both are built. What the row genuinely got wrong is only the case of `trnme`/`gtnme`. | **Met for the format; the file I/O and the name entry are not built.** The seventy-seven-byte block with every field named from the assembled build, both checksums, and the save and load layout — compared against `CHECK`, `CHECK2`, `SVE`'s copy and `DFAULT` over 221 blocks (the shipped default, all-zeros, all-255, a single bit walked through all 77 bytes, and 64 pseudo-random fills). The block is held as BYTES with named offsets rather than as a struct: the save file is those bytes, so making them the storage removes the serialiser a `.d64` import could drift from. Three findings recorded in §6.14. **Re-examined 2026-09-03, and most of what was deferred is not blocked.** `MT26` — which is the line editor the name entry calls, with RETURN, ESCAPE, DELETE, a beep on a rejected character and a character range read from `RLINE` — takes its keys through `TT217`, and `TT217` is the `KeySource` seam slice 2c built and proved. `TRNME` is an eight-byte copy and `GTNME` is a copy, a token, a call to `MT26` and a length check. All three are portable against what exists today, exactly as the four trading screens turned out to be — **and were built the same day**: fourteen line scripts and four name prompts compared character for character with the cursor stamped, plus the length, the carry, the text colour and every byte of the buffer. §6.19 records what `MT26` turned out to be. **Built 2026-09-03, and the file too.** `SV1`'s whole arithmetic half compared FILE BYTE FOR BYTE against the shipped routine over sixty-seven commanders — and that comparison found `SaveCommander` had never written `CHK2`, which §6.20 records. `CommanderStore` is the seam for the two Kernal calls and `Outpost/SaveStore` implements it against LocalAppData, untested by design and by necessity. **And `SVE`'s menu dispatch closes the slice, built 2026-09-03.** Twenty-one paths through the disk access menu compared against the shipped routine running WHOLE — only the two Kernal calls, the setup around them and the control-code routines that leave the text system are stood in for, so `DETOK`, `MT26`, `GTNMEW`, `TRNME`, `BPRNT`, `CHECK`, `DFAULT`, `JAMESON` and `YESNO` are the game's own code. Every character with the cursor stamped, the keys consumed, the device traffic, both commanders byte for byte, the competition number and the carry. §6.21 records what it found: a failed load leaves `JSR LOD`'s frame on the stack, so leaving the menu afterwards resumes `loading` and renames the commander; option 4's carry comes from `DFAULT`'s last `CMP`, not from `SVE`; `BPRNT`'s field width is uninitialised; and slice 2d's own comments had bit 6 of the competition flags wrong. Twenty-one mutations, twenty-one caught after the one survivor was fixed structurally rather than labelled. **Slice 2d is complete.** |
| **2e First playable** 🟠 **Built 2026-09-03; everything a machine without a display can check is green, and it has not yet been RUN** | `Game` top-level state (`BR1`, `BAY`, `TT170`, `DOENTRY`), key dispatch for the docked screens (`DOKEY`, `RDKEY`, `TT217`), `CanvasPresenter`, `KeyMap`, frame pacing; the title screen **without** the rotating ship (that needs LL9 — a placeholder box until 3b). | **Both owner decisions are now taken, and one of them was answered wrongly in this row until 2026-09-03.** (1) **Risk R3 is settled: count cycles, and the counter is built** — but the row used to say the choice was between measuring a rate and picking one, and §6.17 records why that was the wrong framing. The C64 main loop has no frame cap at all, so there is no rate to pick: the loop is cycle-budgeted and free-running, and a separate 50 Hz PAL vertical-sync tick serves `DELAY`, `TT16` and `FREEZE`. (2) **Verification is split, ruled 2026-09-03**: the replay-hash half runs in CI through a null presenter — no window, no GPU, so it goes on the Ubuntu leg — and "is every docked screen legible, does the cadence feel right" is a human sign-off recorded here. That is ADR-003 §3 extended rather than a new mechanism. **The key dispatch and the start sequence are built, 2026-09-03.** `TT102`'s decision half — which of eighteen labels a key press reaches — compared over **16,384 dispatches**: all 256 key codes against four values of `QQ12`, four views, the counter running and stopped, and the hyperspace key held and not. It performs nothing, because the labels it names span three phases; what a caller does with the answer is the caller's. §6.24's neighbours record what it turned out to be, and the ledger row carries the four findings. `TT170`, `BR1`, `BAY` and `DOENTRY` are built too, over a seam for each of the eight things they reach outside the slice; §6.25 and §6.26 record what they turned out to be, the second of them after the ledger turned out to have filed `DOENTRY` as the program's entry point when it is the docking routine. **Every routine 2e names in `GameLogic` is now built, and a whole docked session runs through them** — started by `TT170`, driven by `TT102`'s dispatch, through one null presenter that satisfies all five seam interfaces, with the trade arithmetic checked to flow from the buy screen to the hold to the inventory. That is the CI half of this slice's acceptance criterion. §6.27 records what it cannot check and why the other half is not a formality. What is left is the SHELL: the presenter, the key map and the frame pacing — plus `TT107`'s hyperspace countdown, which is flight state and lands with phase 3. **The shell is written and CI compiles it, 2026-09-03.** It splits in two on purpose. The half that is a DECISION rather than an API call — the sixteen-colour palette, the integer-scaled letterboxed viewport, the fixed-timestep accumulator and the Windows-key-to-C64-position map — is in `Presentation.cpp` and `KeyMap.cpp`, is covered by `ShellTests.cpp`, and runs on both CI legs; the centring property in it caught a real defect (halving a negative difference truncated towards zero, so a window narrower than the canvas clipped unevenly). The half that is Direct3D — `Window.cpp`, `CanvasPresenter.cpp`, `Shell.cpp` and the composition root in `Main.cpp` — was written without a machine to run it on, so the Windows CI leg now restores the project's NuGet packages and **builds `Outpost` unpackaged in both configurations**, which is what turns "written blind" into "a compiler has read it". Every line of it compiled first time; what failed was the LINK, on a C++/WinRT version mismatch that had been latent since the projects were created and that only a binary linking `GameLogic.lib` against `Outpost`'s objects could ever have exposed (§6.30). The shaders are compiled by the build too, rather than at run time, so nothing in the shell is unread by a compiler (§6.31). The threading question `TextPrint.h` left open is answered: a NESTED MESSAGE PUMP inside `TT217`, single-threaded, because the thing the game blocks on is the player — a game thread would need the canvas double-buffered under a mutex and an answer for a thread parked inside `NextKey` at close, and buys nothing. `TT66` and `CLYNS` were ported to answer the screen seams properly (§6.29). **What is still needed is a person at a Windows machine**: the acceptance criterion's other half is "is every docked screen legible, does the cadence feel right", and a hosted runner has no display — compiling and linking is a different question from drawing. The first thing that person should do is measure a main-loop iteration and write the number down. |

### Phase 3 — Flight and the 3D pipeline

| Slice | Scope | Accept |
|---|---|---|
| **3a Ship slots and motion** | `ShipSlot`, `Bubble` (`FRIN`, `MANY`, `UNIV`, `NWSHP`, `NWS1`, `KILLSHP`, `KS1`–`KS4`, `ZINF`, `RESET`/`RES2`, `ZES1`/`ZES2`, `GINF`), `MVEIT` 1–9, `MVT1`, `MVT3`, `MVT6`, `MVS4`, `MVS5`, `MV40`, `TIDY`, `MAS1`–`MAS4`, `TAS1`–`TAS6`, `DCS1`, `ABORT`, `sightcol`. | Oracle: run `MVEIT` on a slot with sampled orientations/speeds/roll/pitch for N iterations; byte-identical `INWK`. |
| **3b Ship drawing** 🟠 **The projection is built, 2026-09-04** | `LL9` 1–12, `LL61`, `LL62`, `LL118`, `LL120`, `LL123`, `LL129`, `LL145` 1–4 clipping, `SHPPT`, ✅ `PROJ`, ✅ `PLS6`, ✅ `DVID3B`/`DVID3B2`, `PLUT`. Title screen rotating ship. | Oracle: for sampled ship types and orientations the list of clipped line segments matches; golden of the title screen Cobra at frames 1, 30, 60. **The scope line is now twice corrected.** §6.34 removed `LL5` (ported in phase 1) and `LSX2`/`LSY2` (3c's heap, not the ship heap). §6.35 removes `PL2` — `PROJ` reaches `PL2-1`, which is `PROJ`'s own `RTS`, and `PL2` itself erases the planet and sun heap — and adds `PLS6`, which the 3c row had grouped with the planet code by name. **`PROJ` and everything it divides through are built and swept**: 65,280 divides, 65,536 divides by a ship's z, 65,536 screen offsets across all four of `PLS6`'s exits, and 3,072 projections including the half-written case. **The ship line heap and the three routines that read it followed the same day**: `LL155`, `LL81`, `EE51` and `SHPPT`, compared on the whole canvas rather than on a byte, and `SHPPT` compared as a SEQUENCE because it reads a coordinate the previous projection left behind. Twenty-nine mutations caught across the two units, two equivalent. §6.36 is the one finding that came from a coverage assertion rather than from a comparison. |
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
| 8 | Slice 0e: the repository is public and tracks `MasterFile/`. | **Make the repository private.** | The 0e row; Risk R1, now marked realised. |
| 8b | …and, later the same day: is that worth doing? | **No — reversed. The repository stays public, and the exposure is accepted knowingly.** | ADR-001 §5 (recorded as a reversal, not a deletion); the 0e row; Risk R1. The engineering rules that bound the exposure to 13 files — `Upstream/` as a submodule, no assembled binaries, nothing copied into `GameLogic/` — all stay. |
| 9 | The portable test runner that existed only in a working directory. | **Commit it and gate CI on it.** MSVC stays the authority. | ADR-004 §6; `Tests/PortableRunner/`; the `linux-tests` job |

**Decision 8 was not a design question, and 8b is the one worth reading.** The pair records a
policy this document had stated and the repository had never met, and then the owner's answer to
what to do about it: accept it rather than mitigate it. The engineering half — `Upstream/` as a
submodule, no assembled binaries committed, nothing lifted into `GameLogic/` — did hold, which is
why the exposure is 13 files rather than 3,000, and it is what keeps the acceptance reversible.

The distinction the corpus is careful about: **an accepted risk is not a closed one.** Risk R1
stays realised, slice 0e stays open, and the fallback if permission is refused gets more expensive
the longer the history is public, because a history that has been read cannot be unread. That cost
is part of what decision 8b accepts.

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
| 2026-09-04 | **The ship line heap, and the first thing the port draws for a ship** — `LL155`/`LL27`, `LL81`, `EE51` and `SHPPT`, all four compared against the shipped code on the whole 10,240-byte canvas. The heap is the arena from `K%` to `LS%` addressed absolutely, because `XX19` points into it and `KILLSHP` shuffles runs of it down; an array per ship cannot express either. `SHPPT` is compared as a SEQUENCE (§6.33 again): it tests `PROJ`'s accumulator rather than its carry, and one of `PROJ`'s overflow exits leaves the accumulator zero and `K3+1` holding the last ship's high byte, so a ship really can be drawn where the previous one was. **§6.36 is the finding worth reading**: the first version of that test agreed with the game byte for byte on all twelve positions and exercised one branch, because `DVID3B` returns 256 TIMES the ratio and every position used z = 1. The comparison passed; the coverage assertion is what failed. Both headers now state the scale, which neither the upstream summary line nor the routine's name does. Also: `INWK+31` was called `SHIP_MISSILES_OFFSET` by 3a and is a packed byte — bit 3 is what `EE51` reads to decide whether there is anything to rub out — so it is now `SHIP_STATE_OFFSET` with its bits named, rather than two names for one offset. |
| 2026-09-04 | **The projection chain built — `DVID3B`, `DVID3B2`, `PLS6`, `PROJ`** — which is every pixel the space view will draw, and three more ledger moves with it (§6.35). `DVID3B2` was filed with the movement code because it reads `INWK+6..8`; its callers are the projection and the planet drawing, and nothing in `MVEIT`'s tree touches it. `PLS6` was filed with the planet drawing because the row wrote `pls3`–`pls6` as a RANGE OF NAMES; its only callers are `PROJ`'s two. And `PL2` was in 3b because `PROJ` branches to `PL2-1`, which is `PROJ`'s own `RTS` and not `PL2` at all — the row named the routine the address arithmetic points near. That last one is enumerable rather than anecdotal: the C64 build has 43 backward label-with-offset targets, 27 of them alternative entry points into the same source file (as `MVT1-2` is, ported correctly in 3a) and **six that land in the file before the one that names them** — `LASLI-1`, `LL10-1`, `PL2-1`, `SFS1-2`, `WPLS-1`, `ypl-1`. One down, five to check as their slices open. Also recorded: a mutation the sweep does NOT catch, because the loop shape it changes is equivalent, and the header comment that had claimed otherwise. |
| 2026-09-04 | **Slice 3b opened with its own §6.12 pass, before any of it was written.** Three corrections to the row (§6.34): `LL5`, `LL28` and `LL38` are already ported, from phase 1; the row names `LSX2`/`LSY2` as "the ship line heap" and they are the PLANET AND SUN heap, which belongs to 3c — the ship heap is the `LS%`/`SLSP` region 3a already modelled; and `LL9` jumps to `DOEXP`, a seam the row does not name. The middle one is a new shape of the pattern: not a row written from what a routine is about, but **two structures with confusable names and the row picking the wrong one**, which asking "what does this read?" cannot catch and asking "who writes this?" catches in one grep. In the other direction: `LL9`'s only external calls are `DORND` and `FMLTU`, both ported, so the hardest routine in Elite has two dependencies and both exist. |
| 2026-09-04 | **Slice 3a's acceptance criterion met: `MVEIT` runs.** Sixteen ships for twenty iterations each with byte-identical `INWK`, plus the seam counts asserted — `SCAN` and `TACTICS` are stubs but how often each is reached is behaviour, and the sun is never scanned at all. Eight routines were ported ahead of it and every one matched first time; `MVEIT` did not, and §6.33 records the two reasons, both invisible to a single call. `BPL MV43` branches to the SUBTRACTION, so "signs agree" means subtract here and add everywhere else in the file; and the `ADC` after `JSR MLTU2` has no `CLC`, so it runs on the multiplier's exit carry, which the port was discarding. The generalisation is cheap: where a routine's output feeds back into its own input, compare a RUN and not a call. |
| 2026-09-04 | **Phase 3 opened with the ledger pass §6.12 asked for, and the ship data extracted.** `MVEIT` reads the blueprint's maximum speed and `NWSHP` three more of its bytes, so 3a touches data the row files under 3b — §6.12's pattern for the seventh time, caught by looking this time rather than by failing. The blueprints are extracted as ONE 8,073-byte region indexed by address rather than 33 arrays, because two of them declare more data than fits before the next one begins (the splinter by 24 bytes, the Thargon by 60), those same two have no `_EDGES` label, and the game has no concept of a blueprint's end anyway. §6.32 also records the same mistake made one level up and caught by the new suite: the region was first sized from the `SHIP_` labels, and the pointer table was walked to 39 entries when it is 33 — past the end it returns `E%`'s bytes as addresses, which came out as 1, 24865 and 41120. |
| 2026-09-03 | **The executable links, and building it found a defect that had been latent since the projects were created.** Every one of the ~1200 lines of Win32 and Direct3D written blind COMPILED first time; the failure was thirty-one `LNK2038: mismatch detected for 'C++/WinRT version'` at the link step, because `Outpost` carries the CppWinRT NuGet package (3.0) and every other project takes the SDK's (2.0). `GameLogicTests.dll` had never noticed, because it and `GameLogic.lib` are both 2.0 — the mismatch needs a binary that links one against the other, and until this slice none existed. §6.30 records it, and the fix: `GameLogic` and `NeuronCore` never used C++/WinRT at all, they inherited it from the shared precompiled header, so `NEURON_NO_CPPWINRT` takes it back out and `Outpost/pch.h` is the one place that keeps it. |
| 2026-09-03 | **Slice 2e's shell built, and the Windows CI leg now compiles the executable.** The split is the point: the palette, the viewport, the step accumulator and the key map are decisions rather than API calls, so they are in `Presentation.cpp` and `KeyMap.cpp` where the suite reaches them on both legs; Direct3D, the message pump and the composition root are the rest, and CI restores the project's packages and builds them unpackaged in Debug and Release. `TRANTABLE` is EXTRACTED rather than replaced, and the ledger said otherwise — `TT217` gives the dispatch a key NUMBER and the screens a CHARACTER, so a map straight to a character agrees with the game on the keys somebody thought to try. The blocking-`KeySource` question ADR-004 and `TextPrint.h` left open is answered: a nested message pump, single-threaded. §6.29 records a misreading of `TT66` that a confident paragraph nearly shipped, and §6.28 one 6502 byte the port keeps in two variables. |
| 2026-09-03 | **A whole docked session runs, which is the CI half of slice 2e's acceptance criterion.** One null presenter satisfying all five seam interfaces, a scripted keyboard, and a session that starts through `TT170` and is driven by `TT102`'s dispatch into every docked screen the port has. It asserts what no per-routine test can: two tonnes of food bought on the buy screen are two tonnes in the hold when the inventory prints it, and the cash falls by twice the price the market quoted. §6.27 records what it cannot check — LAYOUT, because `CHPR` advances the cursor and `CHPR` is the presenter, so a null one has no cursor to compare. The human half of the criterion is the only thing that covers a session's layout, and this half being green is not the criterion being met. |
| 2026-09-03 | **`DOENTRY` built, and the ledger had it filed as the program's entry point.** It is the DOCKING routine — the mission dispatcher that runs when the ship arrives at a station — which makes the sixth stale scope line and the first that was wrong about what a routine IS. 12,800 commanders docked and compared on which of seven labels each reaches. §6.26 records the finding underneath: the Trumbles mission's cash test compares ONE BYTE of a four-byte value, so eligibility is a band 1,536 credits wide recurring every 6,553.6 rather than a threshold — a player with 5,017.6 credits is offered the mission and one with 10,000 is not. The upstream source's own two comments on those three instructions disagree with each other and neither is right. Kept rather than fixed, with a test that fails if anyone supplies the correction. Eleven mutations, eleven caught. |
| 2026-09-03 | **`TT170`, `BR1` and `BAY` built** — the start sequence, which is slice 2e's `Game` top-level state. Seven title sequences compared seam for seam with their arguments, plus every byte of state the run leaves behind, with the disk menu running for real on the "Y" branch. §6.25 records what it turned out to be: the ship's coordinates are two bytes of the COMMANDER BLOCK, which the oracle established by disagreeing with a port that kept them separately; the cold start runs `RES2` twice through two fall-throughs; the theme brackets one branch and not the other; `DFAULT` runs twice on the "Y" path; the current system is snapped to the nearest generated one before play begins; and `BR1` does not return, it runs off its end into `BAY`. Fourteen mutations, fourteen caught. |
| 2026-09-03 | **The top-level key dispatch built** — `TT102`'s decision half, which is slice 2e's "key dispatch for the docked screens". 16,384 dispatches compared: every one of the 256 key codes against four values of `QQ12`, four views, the counter running and stopped, and the hyperspace key held and not, by stepping the shipped routine until it reaches one of nineteen addresses that ARE the answer. Launch is tested before the docked split; "H" is read from the key matrix rather than from the accumulator, so holding it discards whatever else was pressed; `QQ12` is tested two different ways in the same routine. Eight mutations, eight caught — two only after the sweep was widened to values of `QQ12` the game never writes, which is where the two tests of it disagree. |
| 2026-09-03 | **CI went red on a project path relative to the wrong thing, and now cannot again.** `..\Outpost\SaveStore.cpp` from a project two directories down needs two `..`; MSBuild resolves an `Include` against the PROJECT. `tools/check_projects.py` runs on the Ubuntu leg and resolves every path the way MSBuild does — and also catches the reverse, a source on disk that no project names, which the portable runner's glob would compile and the Windows build would silently omit. It found three problems already in the tree. §6.24 records the shape. |
| 2026-09-03 | **`SaveStore` compiled and tested, and it had a defect.** The file had been committed and left uncompiled because the only machine that could build it ran Windows, and its header argued that having no oracle meant having no test. Ten lines of Win32 stand-in in the portable runner's shim made both untrue. `PathFor` accepted `CON` — every legacy Windows device name is letters and digits and typeable at the commander name prompt, and Win32 resolves such a stem to the DEVICE whatever directory and extension surround it, so a player with that name would have had their commander written to the console. Five tests now cover the round trip, the names that must be refused, the near misses that must not be, the length check and the CR-terminated name. §6.23 records the argument. |
| 2026-09-03 | **The Data on System screen built, which closes slice 2a.** All 2,048 systems in all eight galaxies compared character for character with the cursor stamped, `PDESC` included. The row had deferred `TT25` as "cursor and canvas work"; it is a token printer, a number printer and `TRADEMODE`, which is the fifth stale scope line this port has found. §6.22 records what the screen turned out to be: the economy is two words folded out of one number by a comparison whose carry comes from the `CMP` and not the `LSR`, the four species words share a scratch byte so the appearance decides the noun, and every planet's radius is between 2,816 and 6,911 km because of a `CLC / ADC #11`. Seventeen mutations, fifteen caught and two provably equivalent. Also adds `tools/c64_source.py`, which evaluates the upstream library's version conditionals instead of leaving them to be read by eye. |
| 2026-09-03 | **`SVE`'s menu dispatch built, which closes slice 2d.** Twenty-one paths through the disk access menu compared against the shipped routine running whole — only the Kernal and the control-code routines that leave the text system stood in for — on every character with the cursor stamped, the keys consumed, the device traffic, both commanders byte for byte, the competition number and the carry. §6.21 records four findings, the first of them a bug a player can hit: a failed load leaves `JSR LOD`'s return address on the stack, so the next exit from the menu resumes `loading` — renaming the commander and telling `TT102` to restart the game when nothing was loaded. Option 4's carry comes from `DFAULT`'s last `CMP`; `BPRNT`'s field width is whatever the last caller left; and bit 6 of the competition flags is the C64 version stamp, not "loaded from a file" as slice 2d's comments had it. Twenty-one mutations, twenty-one caught. |
| 2026-09-03 | **The save flow built, and it found a defect in slice 2d's shipped code (§6.20).** `SaveCommander` wrote two of the three checksums the original writes and never `CHK2`, so a file the port saved would load into the game flagged as tampered. Nothing caught it because `LoadCommander` only reads that byte, the round-trip test skipped it, and the comparison against the shipped routine **reproduced the arithmetic by hand and shared the same misreading**. Running the real `SV1` disagreed on the first commander. Sixty-seven commanders now compared file byte for byte, plus the competition number — a four-byte chain stored out of order folding the checksum, the competition flags, the third cash byte and the high byte of the kill tally. `LSR SVC` halves the save count rather than incrementing it. `Outpost/SaveStore` implements the store against LocalAppData. |
| 2026-09-03 | **`MT26`, `TRNME`, `GTNME` and `TR1` built — the keyboard half of 2d.** Fourteen line scripts and four name prompts compared character for character with the cursor stamped, plus the length in Y, the carry ESCAPE sets, the text colour and every byte of the buffer. §6.19 records what the line editor turned out to be: one print serving both a typed character and the bell, by way of an `EQUB &2C` that swallows the `LDA #7` after it. Ten mutations, nine caught; the survivor — `TRNME`'s fall-through into `TR1` — is provably undetectable and labelled as such. Only the FILE now waits for 2e. |
| 2026-09-03 | **2d's remaining scope re-examined.** ~~`qu5` and `yesno` have no label in the assembled C64 build at all~~ — **wrong, and corrected the same day**: `YESNO` is at 33262 and `QU5` at 34988. The check had looked for the lowercase spellings immediately after diagnosing that same trap for `trnme`/`gtnme`, which really were the BBC's spellings of routines the C64 calls `TRNME` and `GTNME`. Of what is really there, `MT26`, `TRNME` and `GTNME` are portable today — `MT26` reads its keys through `TT217`, which is the seam slice 2c built. Only the file itself waits for 2e, because `SVE` and `DFAULT` end at the C64 Kernal's save and load calls. That is the third stale scope line found today, after slice 1a's and slice 2c's. |
| 2026-09-03 | **The private-repository ruling reversed: it stays public, knowingly (§8, decision 8b).** The finding that prompted the original ruling stands and the false clause is gone — slice 0e's "nothing is pushed to a public remote" had never been true. What changes is the disposition: the exposure moves from *to be mitigated* to *accepted, with a name on it*. Risk R1 stays realised and 0e stays open, because what closes it is a written answer from the rights holders and nothing else. **Also: `Outpost.exe` had no entry point at all** — the template compiles only `pch.cpp`, so building the solution failed with LNK2019 on `WinMain`. CI never saw it because it builds the test project by name. |
| 2026-09-03 | **Slice 2c closed: the equipment shop, and a seam boundary drawn in the wrong place.** `EQSHP` with `prx`, `qv`, `eq` and `refund`, over thirty-six scenarios. The finding that mattered was not in `EQSHP` but in the three screens already built: each reset the cursor and the case flags after calling `SetUpTradeScreen`, on the reasoning that pixels are the seam's and state is the port's. That is wrong — `TRADEMODE` is `TTX66` entire, cursor and flags included — and **only a screen that redraws itself could show it**, because the other three run once from a state that happened to match. The equipment shop loops after every purchase, and its second pass printed the title in the wrong place and the wrong case. Also: no station in the game sells exactly twelve or thirteen items, because `CMP #12 / BCC / LDA #14` jumps the cap from eleven to fourteen. |
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
