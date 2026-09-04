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

### 6.80 A loop that stops on a page, and the register it leaves behind

`TTX66K` clears the bitmap as far as the dashboard:

```
 LDX #HI(SCBASE)
.BOL1
 JSR ZES1k
 INX
 CPX #HI(DLOC%)
 BNE BOL1
```

`DLOC%` is &5680. The loop compares X against &56, not against &5680, so it stops one page BELOW
the dashboard and the partial page that follows finishes the job. The port wrote the obvious
`for (page = 0; page < DASHBOARD_BITMAP; page += 256)`, which runs one page too many because
&1600 is still less than &1680.

**And the bound is not the only thing that leaks.** X is left at &56 and three things downstream
read it: `ZES2k` takes it as the partial page's high byte, the hand-written `STA (SC),Y` finishes
the byte `ZES2k` cannot reach, and — on the text-screen path — `.BOL2` picks the loop straight
back up from there. One wrong comparison moved all four.

The oracle caught it on the first byte of the dashboard: game 29, port 0. Twenty-nine is the
marker the comparison fills the screen with, and that is the whole reason it is a marker: **on a
screen that started at zero, a byte the routine cleared and a byte it never touched are the same
byte.** §6.39 established that for a storing routine; this is the first time it has caught a
clearing one.

The general shape is worth having: **a 6502 loop's exit VALUE is as much a part of it as its exit
condition**, and rewriting one as a C++ `for` over a range throws the exit value away. Two
routines in this unit depend on it — `.BOL2` continuing from X, and `wantdials`'s second copy
continuing from the `V` and `SC` the first one left advanced — so the port passes those on at the
call site and says so, rather than recomputing them and hoping they agree.

### 6.79 An instruction spelled as data, again, and this time it is a screen height

`BOX2` draws the border. It opens:

```
.BOX2
 LDX #18
 STX T2
```

and `TTX66K` reaches it by running off its own end:

```
 LDX #25
 EQUB &2C
```

`&2C` is `BIT abs`, and its two operand bytes are the `A2 12` of `LDX #18`. So a `JSR BOX2` loads
18 and a fall-through keeps 25 — **and the difference is the height of the screen**: a text screen
is 25 character rows and the space view, with the dashboard under it, is 18. Seven rows of border,
decided by whether control arrived at a label or two bytes past it.

This is the fifth time this port has met the idiom, after `PZW`'s red, `DILX`'s four entry points,
`MESS`'s swallowed `STA YC` and `LASLI`'s. §6.63 named the shape and §6.74 named what the shapes
have in common. What is new here is the SIZE of what the byte decides: the previous four hid a
load or a store, and this one hides the difference between two screen layouts.

The port takes the count as an argument, the way `DrawBar` takes `DILX`'s shift count, and the
comparison calls the routine at both entries — at its label, and at `BOX2 + 2`, which is exactly
where the swallowed instruction ends. A port that had taken 18 from a constant would have agreed
with the game on the space view and drawn seven rows too few on every text screen, which is the
kind of divergence that looks like a rendering bug for a week.

### 6.78 The §6.12 pass on slice 3d-d-iii-a, and a binary the oracle does not load

`LOOK1`, `WARP` and the `TT66` chain §6.77 moved out of 3d-d-ii. **252 instructions, 23 external
call targets, 14 of them already ported** — `ADD`, `DIALS`, `FLFLLS`, `FLIP`, `GINF`, `HLOIN`,
`MT2`, `NWSTARS`, `SIGHT`, `TT162`, `TT27`, `WPSHPS`, `ee3` and `MAS2`/`m`. Four more are inside
the unit (`BOXS2`, `NOSPRITES`, `ZES1k`, `ZES2k`), `NOISE` already has a seam, and `DOVDU19`
needs one.

**More of `TTX66K` is comparable than it looks.** `abraxas` and `caravanserai` read like VIC-II
registers and are not: they are ordinary variables holding the values the raster interrupt pokes
into `VIC+&18` and `VIC+&11` on its next pass. `wantdials` writing them is plain state, and only
the handler that reads them is hardware. That is the opposite mistake from §6.73's — there a
routine was filed as hardware because two of its writes went to registers; here two writes that
look like registers are memory.

**But the dashboard bitmap is not in the oracle.** `wantdials` copies 2,240 bytes from `DSTORE%`
into `DLOC%` — eight whole pages and then &C0 more, through `mvblockK` and its second entry
`mvbllop`. `DSTORE%` is `SCBASE + &AF90`, which is &EF90, and **nothing assembles anything
there**: `SHIPS.bin` runs out at &EF8C, three bytes short, and `DSTORE%` is filled by the C64's
LOADER from `CODIALS.bin` — a file named only in a comment in `elite-loader.asm` and INCBINed
nowhere.

**And the 2,240 bytes are not the first 2,240.** `mvbllop` is entered with Y = &C0 and stores at
Y before counting DOWN, stopping when Y reaches zero — so its page contributes offsets &C0 down
to 1, not 0 to &BF. The copy therefore covers 0 to 2,047 and 2,049 to 2,240: **a hole at 2,048
and one byte past the end of seven character rows.** Both of those bytes are zero in the image, so
nothing shows, and neither is invisible to a byte-for-byte comparison — the port's array is 2,241
bytes for that reason and its test asserts the hole rather than describing it. The port had the
obvious reading (`data() + 2048` for 192 bytes) and it read one byte off the end of the array.

So a comparison of `wantdials` written today would copy zeros in the oracle and zeros in the
port, agree perfectly, and prove nothing about the one thing the routine exists to do.

Where the bytes actually are is settled by the ORIGINAL build script, not by the modern one.
`S.COMLODS.txt` line 1000 reads `OSCLI("L.:2.C.CODIALS "+STR$~(O%+&18))`, then advances by
`&8C0 + &21` — and `&8C0` is 2,240, the exact length `wantdials` copies. So the dashboard image is
loaded &18 bytes into the loader's own output, ships inside `COMLOD.bin`, and reaches `DSTORE%`
because the loader puts it there at run time. `C.CODIALS.bin` itself is 2,248 bytes with its last
eight zero.

**The first task of this unit is therefore not a routine.** It is to establish which bytes of that
file the game ends up copying, load them at `DSTORE%` in `Binaries.txt`, and give the port the
same bytes as data the way `Font.cpp` holds the font — with a test that fails if `DSTORE%` is
blank, because a blank one is what makes the comparison silently vacuous.

Which bytes is settled by rendering them: decoding the file's first 2,240 as seven character rows
of forty cells produces the dashboard with `FS`/`AS`/`FU`/`CT`/`LT`/`AL` down the left,
`SP`/`RL`/`DC` and 1 to 4 down the right, the scanner's ellipse in the middle and ELITE along the
bottom, aligned to the cell grid — which a load offset would have broken. **That is a
comparison against a picture rather than against a number**, and it is the only kind available
when the thing being placed is an image and the evidence for where it goes is a line of BBC BASIC
in a 1980s build script.

**This is the first thing the port has needed that the assembled build does not contain.** Every
table so far came out of the image, by label, because everything so far was assembled into it —
the font included, which is `INCBIN`ed and so has a `FONT` label at &0B00. A run-time load has no
label and no block, and the difference is invisible until a routine reads from it. The check that
would have caught it earlier is the same one §6.12 asks: not "is this routine ported?" but "what
does it READ?", extended one step further than the port has been asking it — **what does it read
that was never assembled?**

`mvblockK` is worth reading twice on its own account. `LDY #0`, then `LDA (V),Y / STA (SC),Y /
DEY / BNE mvbllop`: offset 0 is copied FIRST, then the loop counts down 255, 254 … 1, so a page
comes across in the order 0, 255, 254, …, 1 rather than either way round. The result is the same
and the trace is not, which matters for a port that compares intermediate state.

### 6.77 A routine counted as ported when half of it is

§6.73's pass listed fifteen external call targets for slice 3d-d-ii and said eight were already
ported. `TT66` was one of the eight. Row 74 of the ledger has it ✅, from slice 2e.

Row 74 also says, in the same cell, that what was ported is *"the TEXT STATE half of the two
screen seams, which is GameLogic's; the pixels stay seams because `TTX66K` is the dashboard, the
sprites, the border box and the colour bands, and those are phase 3's."* Both halves of that
sentence are true and the tick at the front of the row is what a scoping pass reads.

`LOOK1` calls `TT66` for the pixels, not for the text state. Measured: `LOOK1`, `WARP` and the
`TT66` chain they need — `TTX66`, `TTX66K`, `BOXS`, `BOX2`, `BLUEBAND`, `BLUEBANDS`, `wantdials`,
`zonkscanners`, `NOSPRITES`, `ZES1k`, `ZES2k` — are **252 instructions and 23 external call
targets**, which is larger than everything 3d-d-ii has built so far. It is a unit, not a
loose end.

So **`LOOK1` and `WARP` move to 3d-d-iii**, ahead of the loop parts, and 3d-d-ii is what has been
built: `BUMP2`, `REDU2`, `DOKEY`'s flight half, `SPIN`/`SPIN2` and `SIGHT`.

**A tick is a claim about a row and a scoping pass asks about a routine.** §6.71 recorded the
opposite error two units ago — a row saying something was outstanding when the ledger had it built
— and the fix there was "reconcile the row against the ledger in both directions". That is not
enough on its own. This row and the ledger agree; what they agree on is a routine that is half
done, and neither the tick nor the prose is wrong. **What a pass needs is the call target, not the
routine name**: `LOOK1` reaches `TT66`'s pixels, and the port has `TT66`'s text state, and those
are two different questions that share a label.

### 6.76 The blueprints live underneath the video chip

`VIC = &D000`. `XX21 = &D000`. Both, on the same machine, and the C64 banks between them: with
I/O switched in, `STA VIC+&27` reaches the sprite colour register; with RAM switched in,
`LDA (XX0),Y` reads the ship blueprints. Elite flips the bank as it goes.

The oracle's memory is flat. So a routine that writes a VIC register writes over a blueprint, and
`SIGHT` writes two — `VIC+&27` and `VIC+&15`, which land on bytes &27 and &15 of `SHIPS.bin`,
inside the pointer table, corrupting the blueprints for ship types 19 and 20. Every case after it
in the same image would read a wrong pointer.

The comparison takes a fresh image per case for that reason. It is cheap here — 192 cases — and
the alternative, restoring the two bytes by hand, hides the constraint in a line of bookkeeping
instead of stating it.

**And the flatness is also what makes those two registers comparable at all.** The port puts them
behind a seam, because a register is not memory; the oracle catches them as memory, because it
has nowhere else to put them; and the two are then compared against each other. An emulator
faithful enough to model the banking would have had to be asked what it did with the write, and
the answer would have been "nothing you can read".

This is the first routine in the port to touch a VIC register from `GameLogic`'s side of a seam.
Everything before it wrote the canvas or called a seam that did; `SIGHT` does both, four
instructions apart, which is why the overlap surfaced here and not earlier.

### 6.75 A tool that was stricter than the assembler

`tools/c64_source.py` resolves a library file to what the C64 build assembles, and it stops on an
unknown symbol rather than guessing — its own docstring says that guessing is the mistake it
exists to prevent. Four of the build's 627 includes stopped it, and each one looked like a
missing entry in `SYMBOLS`, which is an invitation to invent a value.

None of them was a missing symbol.

**`USA%` and `Q%`.** BeebAsm identifiers may end in `%` and the tool's word pattern did not, so
`IF NOT(USA%)` was read as the symbol `USA` followed by a stray modulo operator. Both are set in
`elite-source.asm` itself rather than in the build options, which is also why nobody had added
them: the tool's table was built from the options file.

**`factor`, in `item.asm`.** A macro argument. There is no value for it outside a call, and the
macro assembles once per call with a different answer each time — so the honest output is both
branches and a note saying which condition could not be decided. The tool now does that.

**`_EXECUTIVE`, twice.** Defined only in the 6502SP builds, and reached through `ELIF` in files
whose C64 branch has already been taken. **BeebAsm never evaluates a branch condition once an
earlier branch is live**, so the assembler never looks at those two lines. The tool did. It was
being stricter than the thing it models, and strictness in the wrong place reads exactly like a
gap in the model.

The fix is verified rather than asserted: all 627 includes now resolve, and for the 623 the old
tool could read the output is **byte-identical**. `--check-all` does that sweep and runs in CI, so
the next file the tool cannot read is a failing build rather than a puzzle in the middle of a
scoping pass.

**The shape worth keeping.** Three of this port's tools now exist because a manual pass got
something wrong: `c64_source.py` after §6.62's `TAS2`, `check_outpost.py` after a rename reached
the Windows job twice in an afternoon, `check_docs.py` after §6.72. This is the first one that
was wrong in the direction of being too careful, and it cost the same as the others: a
`nosprites.asm` reported as zero instructions in a scoping run, because the harness around it
treated a tool error as an empty file.

### 6.74 An idiom that moves a value and leaves a copy behind

`SPIN` decides what a destroyed ship drops:

```
 JSR DORND
 BPL oh
 TYA
 TAX
 LDY #0
 AND (XX0),Y
 AND #15
```

`TYA / TAX` is the 6502's way of writing `X = Y`, because there is no instruction that does it
directly. Every reader knows the idiom, and the knowing is the trap: the copy goes THROUGH the
accumulator, so by the time `AND (XX0),Y` runs, A holds the ship type and not the random number
`DORND` put there four instructions earlier. **The count is the type masked by the blueprint. The
roll decides only whether anything is dropped at all** — a Cobra that explodes twice drops the
same amount both times, on the half of the explosions that drop anything.

The port had it the obvious way round: roll AND blueprint AND 15. That is what the routine looks
like it does, it is what a summary of it would say, and it is wrong. **The oracle found it on the
first blueprint whose byte 0 disagreed with the roll** — type 9, where the game spawned nine and
the port spawned eight.

This is the fourth idiom in this port whose familiar reading is the wrong one, after §6.63's
`EQUB &2C`, §6.64's branch to a label that is a return, and §6.66's `BIT` swallowing a `STA`. They
have a shape in common and it is worth naming: **each is a construct whose PURPOSE is obvious and
whose SIDE EFFECT is load-bearing.** Recognising `TYA / TAX` is what stops you reading it; the
port that had never seen the idiom would have traced the accumulator and got it right.

The sweep now asserts that at least one blueprint held the count below the type's own low nibble,
because without that the `AND (XX0),Y` could be dropped entirely and every case would still pass
on a build whose blueprints all have those four bits set.

### 6.73 The §6.12 pass on slice 3d-d-ii, and a ledger row with one verb for two jobs

The player's controls. **203 instructions and 15 external call targets**, against 3d-d's 866 and
64 — an ordinary unit, and the first part of 3d-d that is.

| To build | Instructions |
|---|---|
| `DOKEY`'s flight half | 77 |
| `WARP` | 36 |
| `SIGHT` | 35 |
| `LOOK1` | 15 |
| `SPIN`/`SPIN2` | 14 |
| `BUMP2`/`REDU2` | 22 |
| `CTRL` | 1 |

Eight of the fifteen targets are already ported, `MAS2` and `m` among them from 3d-d-i two hours
ago. `DOCKIT` and `SFS1` are phase 4's. `NOISE` has a seam. `RDKEY`, `SETL1` and `DOVDU19` need
one.

**`SIGHT` is half hardware and half canvas, and §6.69 filed all of it as hardware.** It writes
`VIC+&27` and `VIC+&15`, which are the sprite colour and enable registers and are not memory at
all. It also writes `&63F8` and `&67F8` — the sprite pointers, which live in the last eight bytes
of each 1KB block of screen RAM, and screen RAM is inside the canvas. `SCBASE` is `&4000` and the
canvas is `&2800` bytes, so those are offsets `&23F8` and `&27F8` of an array the port already
has and already compares byte for byte. Both, because the VIC-II is flipped between the two
blocks and the game keeps them in step. And it sets `TRIBCT` from `TRIBTA`, which is plain game
state.

So **the C64's laser crosshairs are a sprite** — that is why the routine looks like nothing else
in the drawing code, and why a reader who knows the BBC version would not expect it. Two thirds
of `SIGHT` is comparable and §6.69's seam would have thrown that away. It matters beyond `SIGHT`
itself: `LOOK1` falls into it, so changing the view cannot be compared end to end until the
canvas half exists.

**And row 145 is a Replace row with a Port inside it.** It files `dokey`, `dk4`, `ctrl`,
`dks4-dks5`, `dkj1` and nine more under *"Replace. The CIA keyboard-matrix scan is replaced by
`WM_KEYDOWN`"*. That is right about the scan. It is not right about what `DOKEY` does with the
result: 77 instructions of damping, clamping and re-centring on `JSTX` and `JSTY` that read no
hardware, touch no CIA register and are exactly as comparable as anything else in this port. The
row says as much in passing — *"the original polling logic in `dokey`/`ctrl` ports unchanged"* —
and then files the whole of it under the other verb. **A row with one verb cannot describe a
routine that does two things**, and this is the same shape as §6.45's finding that where a
routine lives and what it reads are separate questions.

Two smaller corrections in the same row. **`dkj1` is not in this build**: the file is INCLUDEd
and every line of it is commented out, so the C64 assembles nothing from it, and the row's
*"`dkj1` (joystick) maps to the same bits"* describes a routine that is not there. On the C64 the
joystick is read inside `RDKEY` and `TT17`, which are the seam — so the joystick is not a porting
question here at all. And **`CTRL` is one instruction**, `LDX #6` falling into `DKS4`: entirely
the key seam, with nothing to compare.

### 6.72 Thirteen rows of findings nobody could read

Every slice ends by appending its result to the ledger row that scheduled it, as a new cell on
the end of the row. `Source-Inventory.md`'s tables have four columns. Thirteen rows had five,
six, eight — one had twenty-three.

GitHub renders a table with the header's number of columns and **silently discards every cell
past it**. So the row that records what `LL9` cost, what its mutation sweep caught and which of
its findings turned out to be the port's fault has been showing three cells and a name since the
day it was written. The raw file looks exactly right, which is why it survived: the append is
made in the raw file, read back in the raw file, and reviewed in a diff.

The fix is mechanical — merge the overflow into the notes column, separated by `<br><br>` so the
dated entries stay distinct — and `tools/check_docs.py` now fails CI on any row wider than its
header. The check is four lines of logic and it took one to catch the last case, in the plan's
own phase-3 table.

**The interesting part is what kind of failure this is.** Everything the port checks, it checks
by comparing two things that should agree: the port against the game, a table against the binary,
`Outpost/`'s call sites against `GameLogic`'s headers. This had no second thing to compare
against, because a document has no oracle — and so a defect sat in the most-written file in the
repository for a month, in plain sight, in a form where reading the file could not reveal it.
The lesson is not "check your Markdown". It is that **an artefact with no reader other than its
author has no feedback loop at all**, and the design record is exactly that until someone opens
it on the web. Three of this port's other artefacts are in the same position right now: the
acceptance goldens for 2e, 3b and 3c, which nobody has run because they need a Windows machine.

### 6.71 Two instructions that cannot run, and one the ledger had already built

`cntr` damps a control reading towards its centre, and it ends:

```
.BUMP  INX / BNE RE1
.REDU  DEX / BEQ BUMP
.RE1   RTS
```

`REDU` exists to stop the value reaching zero — the slider runs 1 to 255 and zero is off the end
of it. It is also unreachable. `BUMP`'s `INX` produces zero only from X = 255, and `BUMP` is
entered from `BPL BUMP` with X < 128, or by falling past `BMI RE1` with X = 128 exactly. Nothing
else jumps to either label in the whole build: `BUMP2` and `REDU2` are a different routine that
`DOKEY` calls, they only share the family name, and the assembled image has no other reference.

So the port leaves the two instructions out. **What matters is not the omission but who is
allowed to authorise it.** "This code cannot run" is a claim about every input, made by the
person with the strongest motive to believe it, and this port has already had one of those turn
out wrong in the other direction — §6.64's `dec27`, where a branch that read like "skip ahead"
was a return. So the test arms a trap on `REDU` for the whole sweep and asserts the hit count is
zero afterwards. Two hundred and fifty-six readings by three settings of `auto` and three of
`DAMP`: 2,304 calls, no hits. The trap also returns early rather than executing the two
instructions, so an input that did reach it would fail the value comparison as well — the claim
is checked twice, from opposite directions, and neither check is a comment.

Twenty-two mutations across the two routines — both flag tests and both polarities, the sign
test, the bump, the decrement, the undo, and for `ECMOF` each of the four things it does plus a
bulb toggled twice, a `PlaySound` where the `StopSound` should be, and the wrong effect number.
All twenty-two caught, no survivors and none equivalent.

**A dead-code finding is worth recording even when nothing follows from it.** Elite is 24 years
of accumulated hand assembly and this is the first unreachable instruction pair the port has
found in it; the next one may not be dead, and the difference between the two cases is a sweep,
not a reading.

**And the same slice found a routine the ledger had already built.** §6.69's split listed
`tnpr1` among 3d-d-i's six, on the strength of the 3d row naming it as a prerequisite. Row 69 of
`Source-Inventory.md` has it ticked off in slice 2c, on 2026-09-03, with 86,016 capacity checks
behind it. The 3d row was written before 2c ran and never revisited; §6.69's pass took the row's
word for what was outstanding instead of the ledger's word for what was done.

That is §6.12's failure with the sign reversed. The usual shape is a row claiming something is
ported when it is not — §6.41, and three of the fifteen in §6.69's own pass. This is a row
claiming something is outstanding when it is finished, which costs nothing but a wasted look, and
would have cost a duplicate implementation if the look had been shorter. **A scoping pass has to
reconcile the row against the ledger in both directions**, and until now it has only ever been
run in one.

### 6.70 The fifteenth flag, and a rule that saved the work before it started

§6.69 flagged `MAS3` before slice 3d-d-i was written: `JSR SQUA2 / ADC R`, twice, with no `CLC`,
and `SquareUnsigned` returning the byte alone. On the record so far that reads like a defect.

§6.65's rule says to ask two questions and in which order. *Does the instruction that reads it
care?* — it is an `ADC`, so only a SET carry is observable. *Is it ever set?* — the exhaustive
sweep for `SQUA` and `SQUA2` already existed, so widening the model cost one assertion line
(§6.42's pattern for the fifth time), and the answer over all 512 inputs is NO. `MU1`, the path
taken when A is zero, opens `CLC`; `MU11` ends on a `ROR P` that never carries out for a square.

**So the port was already right about `MAS3` before the flag was modelled at all**, and the
widening buys a proof rather than a fix. That is a different outcome from the previous four
dropped flags and it is the first one the rule PREDICTED — the finding was raised, the rule said
which half to measure first, and the measurement said no change was needed. §6.60's carry is the
same shape; §6.65's is not, because there it lands in an `SBC` and a clear carry borrows.

The assertion is `carries == 0` and not an uncounted loop, because "always clear" is a claim about
all 512 inputs and this is the sweep that can make it. A comment saying so would have been the
same reasoning with nothing checking it — which is how §6.68's laser comment came to be wrong
about its own arithmetic.

### 6.69 The §6.12 pass on slice 3d-d, and a unit that is a slice and a half again

The flight loop, and the pass says the same thing about 3d-d that §6.59 said about the whole 3d
row: it is too big to be one unit. **866 instructions**, against 3d-a's 180 and 3d-b's 250 —
607 in the sixteen loop parts and 259 in the helpers around them — and **64 distinct `JSR`/`JMP`
targets**, which is more than any slice this port has done.

**Where the 64 go.** Twenty-six are ported. Fourteen belong to phase 4 and stay seams: `ANGRY`,
`HITCH`, `OOPS`, `EXNO` and its two friends, `FAROF`/`FAROF2`, `SHD`, `DENGY`, `MVTRIBS`, `NWSPS`,
`SFS1`. Eight are hardware or presentation and belong behind a seam like the sound: `BEEP`,
`NOISE`, `SETL1`, `DOVDU19`, `startbd`, `stopbd`, `SIGHT`, `BOMBOFF`. `DEATH` is 3d-e's. That
leaves fifteen to build, and the row names five of them.

| To build | Instructions |
|---|---|
| the sixteen loop parts | 607 |
| `DOKEY`'s flight half | 88 |
| `WARP` | 40 |
| `LOOK1` | 20 |
| `MAS1`–`MAS4` | 48 |
| `SPIN`/`SPIN2`, `cntr` | 36 |
| `FRMIS`, `ECMOF`, `KS1`, `CTRL`, `tnpr1`, `U%` | ~35 |

**Three the ledger says are ported and are not.** `LOOK1` appears in `DockedKeys.h` only as the
name of an outcome the docked dispatch defers — "`JMP LOOK1`, in flight only" — which is a
mention, not a port. `KS1` and `MAS1`–`MAS4` appear nowhere at all. A `grep` for the label finds
the first and calls it built, which is the same trap §6.41 recorded when a routine was marked
ported and absent; the check has to be for a DEFINITION, and this pass nearly repeated it.

**The proposed split**, on the same principle as §6.59's — each unit resting on the last:

| | |
|---|---|
| **3d-d-i** ✅ | `MAS1`–`MAS4` **built 2026-09-04** in `FlightLoop.h/.cpp` (11 mutations, 11 caught), then `cntr` there and `ECMOF` in `Dashboard.h/.cpp` as `StopEcm`. `tnpr1` was **already built in slice 2c** and should never have been on this list (§6.71). `FRMIS` needs phase 4's `FRS1` and `ANGRY`, and `KS1` ends `JMP MAL1` — a jump back INTO the loop, not a call — so both move to 3d-d-iii |
| **3d-d-ii** ✅ | **Built 2026-09-04**: `BUMP2`, `REDU2`, `DOKEY`'s flight half, `SPIN`/`SPIN2` and `SIGHT`. §6.73's pass added `SIGHT` (two thirds of it is canvas and game state, not hardware) and `BUMP2`/`REDU2` (`DOKEY`'s, not `cntr`'s). `CTRL` is one instruction falling into the key seam, with nothing to compare. **`LOOK1` and `WARP` move to 3d-d-iii** — they need `TT66`'s PIXELS, and the port has `TT66`'s text state (§6.77) |
| **3d-d-iii-a** ◐ | **Built 2026-09-04**: the dashboard bitmap loaded at `DSTORE%` (§6.78), then `ZES1k`/`ZES2k`, `mvblockK`/`mvbllop`, `BOXS`, `BOXS2`, `BOX2`, `BLUEBAND`/`BLUEBANDS`, `zonkscanners`, `NOSPRITES`, `wantdials` and `TTX66K` in `ViewChange.h/.cpp`. **`TTX66`, `LOOK1` and `WARP` are what is left**, and they are blocked on the same seam: slice 2e ported `TT66`'s text state and left its pixels behind `TradeScreenEffects::ClearToView`, so the full routine means replacing that seam with a call — a phase-2 change, not a phase-3 one (§6.77) |
| **3d-d-iii-b** | the sixteen loop parts themselves, with `FRMIS` and `KS1`, which by then call nothing unbuilt but phase 4 |

Doing it in that order means the loop is written last, against a set of routines that have each
been compared to the game on their own. Written first, it would be sixteen parts and fifteen
unported helpers at once, and a divergence anywhere in it would have 866 instructions to hide in.

**And 3d-d-i already has a flag waiting.** `MAS3` sums three squares with `JSR SQUA2 / ADC R` and
no `CLC` between them, twice over — so it reads whatever carry `SQUA2` exits with, and `SQUA2`
runs into `MU11`, which ends on a `ROR P` like every other multiplier here. The port's
`SquareUnsigned` returns the byte alone. That is the fifteenth, and §6.65's rule says which
question to ask first: it lands in an `ADC`, so what matters is not whether the flag is dropped
but whether it is ever SET — an `ADC` cannot see a clear carry, and `SQUA2` returning a set one is
what has to be measured before the port is called wrong. `MVT3`, which `MAS1` needs, is ported;
`SQUA2` is, and returns too little.

### 6.68 Three uncleared adds in one routine, and only two of them matter

`LASLI` picks where the laser beams converge, and it does it in nine instructions with three
`ADC`s and no `CLC` anywhere:

```
 JSR DORND / AND #7 / ADC #Y-4 / STA LASY
 JSR DORND / AND #7 / ADC #X-4 / STA LASX
 LDA GNTMP / ADC #8 / STA GNTMP
```

`AND` does not touch the carry, so the first `ADC` adds `DORND`'s exit carry, the second adds the
second `DORND`'s, and the third adds whatever the second `ADC` produced. §6.65's split says to ask
of each one whether it is constant, and here the answer differs within a single routine.

**The two coordinates are data-dependent.** The generator decides the carry, so the convergence
point spans NINE rows and nine columns where `AND #7` alone would give eight. That is how the
sweep proves it -- count the distinct values, and a ninth can only have come from a carry in.

**The third cannot be set.** `AND #7` plus 124 plus at most one is 132, which does not carry out
of a byte, so `LDA GNTMP / ADC #8` always adds exactly eight and a shot costs exactly eight heat.

The port's first draft said all three were data-dependent and the comment said the heat was "eight
or nine depending on where the beam landed". The test asserted that, and **failed** -- the
coverage counter caught the port's own prose rather than its arithmetic, which is a use for one
that had not come up before. Every byte the routine writes already matched the oracle; what was
wrong was the explanation, and only a claim stated as an assertion could have been.

**`ABORT2` has a register side effect surviving a `JSR`.** `STX MSTG / LDX NOMSL / JSR MSBAR /
STY MSAR` -- and Y at that last store is the ZERO `MSBAR` ended on, not the colour the caller
passed in. So every call clears "the missile is seeking a lock" whatever colour it sets the light
to. Slice 3d-b's `MSBAR` test asserted the `LDY #0` because it was there; this is the caller that
turns that assertion into a requirement.

**And `MESS`'s `DTW4` bit 6 is unobservable for every input the game gives it.** Bit 6 suppresses
the flush a form feed would cause, and dropping it survives the sweep. That is a measurement and
not a shrug: only recursive tokens 4, 65, 95, 126 and 132 contain a form feed, and the six tokens
the game sends `MESS` are 0, 40, 100, 101, 116 and 120. A caller that sent one of the five would
tell the two apart, and the game has none.

### 6.67 A function named for one entry point that implemented another

`CLYNS` clears the bottom three text rows. It opens `LDA #0 / STA DLY / STA de` and then falls
into `CLYNS2`, which does the clearing. The port's `ClearMessageRows` is `CLYNS2`'s body under
`CLYNS`'s name, and the header said why: "DLY and `de` are the message-delay counters and are
flight state, so they are not here."

That was true when it was written and it was still the wrong call. **`CLYNS2` has no callers.** It
is a label inside `clyns.asm` and nothing in the library -- any version, not just this one --
branches to it. So the two entry points are not a choice a caller makes; every caller wants the
stores, and a routine that omits them is not either entry point.

It went unnoticed because the omitted bytes had nowhere to go. Slice 3d-c gives them one, and
`MESS` is what needs them: it calls `CLYNS` and then tests `DLY`, and a stale `DLY` sends it
through `me1` to erase a message that is no longer there. So the defect had a caller waiting two
slices away.

**The shape to watch for: a routine ported from a label that is not its entry point.** It is not
the same mistake as §6.62's fall-through, which is a routine that ends later than it looks; this
is one that starts later than it looks, and the tell is the same -- a label in the middle of an
instruction stream is not a boundary just because it has a name.

### 6.66 The §6.12 pass on slice 3d-c, and a `BIT` that eats the wrong instruction

Six routines -- `ABORT`, `ABORT2`, `me1`, `MESS`, `mes9`, `LASLI` -- and three fall-through chains
between them, all three flagged on first read by the warning `c64_source.py` gained in §6.62:
`ABORT` runs into `ABORT2`, and `me1` runs into `MESS` runs into `mes9`.

**Everything it needs is built except one seam.** `TT27`, `DOXC`, `MT15`, `CLYNS`, `YC`, `QQ17`,
`DTW4` and `DTW5` are phase 2's and all ported; `DORND`, `LL30` and `MSBAR` are ported; `MSTG` is
`Bubble::missileTarget` and `NOMSL` is the commander's. `DENGY` is phase 4b's and stays a seam. The
new state is small: `MSAR`, `DLY`, `MCH`, `de` and `messXC` for the message, `LASX` and `LASY` for
the laser burst.

**And `MESS` has a `BIT` in the wrong place.** The C64 build assembles

```
A9 10     LDA #16          \ the message row on the space view
A6 A0     LDX QQ11
F0 06     BEQ infrontvw
20 D4 B3  JSR CLYNS
A9 19     LDA #25          \ "the text row for the message if this is not a space view"
2C 85 33  BIT &3385        \ ... which eats the STA YC that was going to store it
A2 00     LDX #0           \ .infrontvw is the 85 33, so the branch DOES store
```

The `EQUB &2C` idiom skips the instruction it swallows, and here what it swallows is `STA YC` --
so on the space view the branch lands on the store and the row is 16, and on any other view the
fall-through throws the store away and the row is whatever `CLYNS` left, which is 21. The 25 is
loaded and discarded.

The upstream annotation says both halves and does not notice they contradict: `LDA #25` is
commented "the text row for the message if this is not a space view" and the `EQUB` immediately
below it "skip the next instruction". One of those is the intent and the other is the behaviour.
Moving the `EQUB` one instruction earlier would give what the comments describe, which is what
makes this look like a slip in the original rather than a trick.

ADR-003 settles what the port does: reproduce it. The interest is in how it was found -- not by
reading the listing, where the two comments read as a pair, but by disassembling the bytes at
`MESS` because the listing's `EQUB &2C` sat somewhere the idiom does not usually put it. **The
rule that follows: an `EQUB &2C` is a claim about the NEXT TWO BYTES, so read those bytes rather
than the instruction the source prints after it.** `PZW` has the same idiom and puts it in the
usual place (§6.63); the difference between the two is invisible in the listing and obvious in the
assembly.

### 6.65 The fourteenth flag, and why two identical flags are not the same finding

`DIALS` part 2 reads `LDA BETA / LDX BET1 / BEQ P%+4 / SBC #1 / JSR ADD / JSR DIL2`, and the `SBC`
has no `SEC`. So it runs on whatever carry `DIL2` left, and `DIL2` ends
`LDA SC+1 / ADC #&01 / STA SC+1` on a screen-address high byte that cannot carry out. The carry is
therefore always CLEAR, the subtraction takes two rather than one, and the pitch indicator sits a
step further along than "BETA minus one" would put it.

That is the same sentence §6.60 wrote about `SPS2`'s carry reaching `SP2`, and the two findings
came out opposite ways round under the mutation pass. `SP2`'s `ADC #195` with the carry replaced
by a literal `false` SURVIVES -- adding a clear carry adds nothing, so the two programs are the
same one. This `SBC` with the carry replaced by `true` is CAUGHT, because a clear carry there
BORROWS.

So a dropped flag has two questions and the port has been running them together: *is it constant?*
and *does the instruction that reads it care?* Working out the first is what tells you the value;
only the second says whether the port was wrong. Fourteen flags in, the tally is worth stating
plainly -- an `ADC` cannot see a clear carry and an `SBC` cannot ignore one, so every uncleared
`SBC` found from here is a defect until measured otherwise, and every uncleared `ADC` is a
question about whether the carry is ever set.

### 6.64 Knowing what a label is, and not knowing what branching to it does

§6.63 found `dec27` before slice 3d-b was written and got it right: a label inside `TT26` marking
that routine's own `RTS`, borrowed as a branch target, "not a routine, not a port, one line". Then
the slice was written with `LDA MCNT / AND #3 / BNE dec27` ported as *skip the energy bars and
carry on*, and the first oracle comparison failed on the second case.

**It returns from `DIALS`.** So three passes in four draw the speed, the roll and the pitch and
stop there, and the energy bars, the shields, the fuel, both temperatures, the altitude AND the
compass are all one pass in four. Five sixths of the dashboard, redrawn at a quarter of the rate
the port had it.

The lesson is narrower and more useful than "read the source". Identifying a borrowed `RTS` tells
you what the *label* is; it does not tell you what *branching to it* does, and those are different
facts. `BNE somewhere` reads as flow control within a routine whatever `somewhere` turns out to
be, so the note that resolves the label has to say what the branch does with it — "`dec27` is an
`RTS`" is a fact about a byte, and "this `BNE` is an early return" is the fact the port needed.
The §6.12 pass now records the second wherever it records the first.

**And `DLOC%` has no left margin.** `ylookup` adds 32 bytes to every row -- four character cells,
the space view's margin -- and `Canvas::RowOffset` reproduces it. `DLOC%` is written out as
`SCBASE + 18*8*40` directly, so the dashboard starts four cells further left than a row of the
space view does. The port added the margin to both and put every dial 32 bytes to the right. Two
address bases in one screen, only one of which carries the margin, and nothing names the
difference.

**The slice also removed a §6.28 that had been shipped since 2d.** `DockedShip` held `DELTA` as
`speed` while `FlightState` held it as `delta` -- one 6502 byte in two C++ fields, in a struct
named for `DOENTRY`, which resets it, rather than for the seven routines that read it. It is
`FlightStatus` now, in `Dashboard.h`, with `CABTMP`, `ALTIT`, `ECMA` and `FLH` added and `DELTA`
gone. The sixth appearance, and the first found by needing the state rather than by a test.

**Renaming it broke the Windows app, and nothing local could tell.** `Outpost/Main.cpp` used
`DockedShip`; the portable runner builds `GameLogic` and the suite and nothing else, because
`Outpost/` is Win32 and DirectX 12. So every check here passed and the first sign was a red
Windows job. `tools/check_outpost.py` now reads every `Elite::Name` the app mentions and asserts
it is DECLARED in `GameLogic/*.h` -- declared rather than mentioned, because the comment recording
the rename would otherwise have satisfied it. It cannot catch a signature change, and says so:
only compiling the app finds those, and that needs a Windows machine. **The gap is structural and
worth naming: this port has a whole executable that no hosted check compiles**, and it is where
the acceptance goldens live too.

### 6.63 The §6.12 pass on slice 3d-b, and a routine with four entry points

Nine routines and a table. Four findings, and all four were checked against the ASSEMBLED bytes
rather than the listing, because two of them are things a listing does not show.

**`DILX` is one routine with FOUR entry points, and three of them are byte arithmetic.** It
assembles to `4A 4A 4A 4A` — four `LSR A` — with `.DIL` at `DILX+4`, so an entry point is a shift
count: `JSR DILX` divides the reading by sixteen, `JSR DILX+2` by four, `JSR DIL-1` by two, and
`JSR DIL` not at all. All four are used. Part 4 calls the first two for the shields, the
temperatures and the altitude; part 1 calls `DIL-1` for the speed; part 3 calls `DIL` for the
energy bars. A port that treats `DILX` as one function gets three of its seven callers wrong, and
gets them wrong by a factor of two or four rather than by a bit — and `DIL-1` is the one that
would survive review, because it reads like a typo.

**`PZW` hides a branch in a data byte.** It assembles to `... F0 02 8A 2C A9 55`: `BEQ +2`, `TXA`,
then `2C` — `BIT abs`, whose two operand bytes ARE the `A9 55` (`LDA #RED`) that follows it. So
falling into it skips the load and returning through the branch performs it. One instruction
spelled as data, and the two paths differ only in whether `A` ends up holding `X`. The port models
it as the if/else it is; the note is that a port written from a disassembly would emit a read of
`$55A9` and be right for the wrong reason.

**`dec27` is an `RTS`.** §6.59 listed it as one of the twelve prerequisites the 3d row was
missing. It is a label inside `TT26` marking that routine's own `RTS`, and `DIALS` part 3 branches
to it to return early — the energy bars are redrawn on one pass in four and that is how the other
three leave. Not a routine, not a port, one line. The same shape as §6.35's `PL2-1`, and the
second time the ledger has counted a borrowed `RTS` as work.

**And `CTWOS` is not `DTWOS`, but on this build it may as well be.** Four masks the C64 build can
index — `DIL2` reads `CTWOS,X` with X below four, which is what §6.8 sizes it by — and its first
four bytes are the four `DTWOS` already carries. Two labels, two addresses, one set of values, so
they are extracted separately: a port that shared one array would be asserting something about the
game that the game does not say.

**The open question is where the readings live**, and it is a §6.28 in the making rather than one
already made. `FSH`, `ASH`, `ENERGY` and `GNTMP` exist in the port inside `DockedShip` — a struct
named for `DOENTRY`, which RESETS them, rather than for the dashboard, which reads them — and
`DELTA` is in there AND in `FlightState`. `ALTIT`, `CABTMP`, `ECMA` and `FLH` exist nowhere.
`XX12` exists as `GeometryWorkspace::xx12`, six bytes of `LL51` dot products, and `DIALS` part 3
uses the same four zero-page bytes for the energy bars. So 3d-b is where the live flight state has
to become one thing instead of two, and the answer is a rename plus a deletion rather than a new
struct — the port should not end this slice with three homes for `DELTA`.

### 6.62 A routine that ends in the middle of itself

`TAS2` turns three coordinates into a direction: shift `K3`'s three sixteen-bit magnitudes left
together until the largest fills its high byte, then halve each high byte and put the sign back on
top. Thirty-four instructions, one loop, nothing to argue about. The port was written from
`tools/c64_source.py --code`, which prints the instruction stream and nothing else, and the
instruction stream ends `STA XX15+2`.

**It ends there because the next instruction is in another file.** `TAS2` has no `RTS`. It runs
straight on into `NORM`, which sums the squares of the three bytes it just wrote, takes the square
root and divides each of them by it — so what `TAS2` produces is not a direction with seven bits
of magnitude, it is that vector scaled to a length of 96, and everything downstream reads the
normalised one.

**§6.11 already made this a rule** — "checking whether a routine actually returns, before porting
it as a function, is now part of reading one" — and it was written about `DVID4`, which falls into
`LL28`'s body. So this is not a new lesson; it is the rule failing in the one place it is hardest
to apply, and that is worth more than the finding. A file is a plausible unit. It has a name, a
header comment, one entry label and a tidy end, and the tool that reads it prints exactly what is
in it. Nothing in the output of `--code` distinguishes a routine that ends from a routine that
stops.

So the fix is in the tool rather than in the discipline. `c64_source.py` now reads the master
file's include list and says, at the bottom of any file whose last instruction is not `RTS`, `RTI`,
`JMP` or `BRK`, which file the build assembles next. It reports six of the eleven routines this
slice touched, including three the port had already got right by accident of reading carefully.
`KILLSHP` is the interesting false positive: it ends `TYA / BNE KSL3 / BEQ KSL1`, and the `BEQ` is
unconditional in practice because the `TYA` above it set the flag — so the tool says "when the
branch is not taken" and leaves the judgement where it belongs.

**The failure had a shape worth recognising on its own.** The first sweep compared `K3` byte for
byte and then `XX15` byte for byte, in that order, and `K3` PASSED on every case while `XX15`
failed on the first. Arithmetic that is wrong is wrong in the state it computes; this was right in
all of it and wrong only in what came after. *The inputs and the working agree, the output does
not* is what a fall-through looks like from a test, and it is worth checking before reading the
arithmetic again.

Four of these now: `DVID4` into `LL28` (§6.11), `SOLAR` into four more routines (§6.58), `SPS1`
into `TAS2`, and `TAS2` into `NORM`.

**And `TAS2`'s loop has a second exit that cannot be taken**, which the mutation pass is what
found. `TAL2` reads `ASL K3+9 / ROL A / BCS TA2`, then shifts the three axes, then `BCC TAL2` —
so the loop appears to end either when the ORed high bytes overflow or when the z axis alone
does. It cannot be the second. `A` is the OR of the three high bytes and `K3+9` is the OR of the
three low bytes with bit 0 forced on, so the pair `(A : K3+9)` DOMINATES each axis pair
`(hi : lo)` bit for bit, and shifting both left preserves that in the top byte. `BCS TA2` tests
bit 7 of `A` before its shift; `BCC TAL2` tests bit 7 of `K3+7` before its shift, in the same
iteration at the same shift count. Dominance means the first fires whenever the second would, and
it is tested six instructions earlier — so `BCC TAL2` is a `JMP` spelled as a branch, exactly
like `KILLSHP`'s trailing `BEQ KSL1`.

The measurement is the mutation itself: with the branch deleted the whole suite still passes,
which means it never fires across 6,464 normalisations — 5,184 `TAS2` cases, 800 reached through
`SPS1` and `SPS4`, and 480 through `COMPAS` — and had it fired, `K3` would have diverged from the
oracle's byte for byte. §6.43's rule again: an equivalent mutation is worth measuring rather than
asserting, and the number is what makes the argument checkable.

### 6.61 Two seams for one routine, and what replaced their call counts

§6.59 found `SCAN` behind two seams with two different signatures and said 3d would collapse them.
It did, and closing them turned out to be the more interesting half.

**Both are gone.** `MoveShip` and `ClearAllShips` call `DrawScannerBlip` directly, which cost
`MoveShip` three arguments — a `Canvas`, a `DrawWorkspace` and `QQ11` — and that is the point:
`MVEIT` DRAWS, and a signature that hid it was hiding something true. `BubbleEffects` had only the
one method, so it disappeared with it and `SpawnEffects` stopped deriving from a class that no
longer existed.

**`WPSHPS` writes two globals the seam had swallowed.** `STA TYPE` before each call and
`STX XSAV` around it — the first because `SCAN` reads `TYPE` as a global rather than taking it,
the second because the loop index has to survive the call. The seam took the type as an argument
and wrote neither, so the port left `TYPE` holding whatever the flight loop had last put there.
Both are asserted after the walk now.

**And the counts they were asserted against were weaker than what replaced them.** Both tests
counted how often the seam was reached, which is what you can measure when the routine does not
exist. With it built, the SCREEN says more: `MVEIT` scans an ordinary ship twice a pass and the
ship has usually moved far enough in between that the two blips land in different places, so both
survive the EOR. A canvas that agrees byte for byte proves the count, the positions and the colour
together; the count only ever proved the first.

Making that comparison non-vacuous took one change to the fixture. `MVEIT`'s twenty-iteration test
fills `INWK` with `(offset * 0x1D) ^ 0x41`, which puts bit 6 in every coordinate high byte — and
`SCAN`'s `AND #%11000000` rejects exactly that, so the two screens would have agreed by both being
blank. §6.39's shape one slice later, and the same fix: put the ship where the routine under test
will do something.

**The `SOLAR` test never looked at the screen at all.** It compares the bubble, the heap, the
commander, `INWK` and the generator state — and `SOLAR` falls through into `nWq`, which plots
twelve specks, and `WPSHPS`, which scans the fleet. Neither was compared, and the test had a
`Canvas` in scope the whole time. It is compared now and it passes; the finding is that it was
green without being asked.

### 6.60 The thirteenth dropped flag, and why a constant one still has to be modelled

`SPS2` divides a coordinate by twenty to get its position on the compass, and `SP2` then does
`TXA / ADC #195` and `LDA #156 / SBC T` with no `CLC` or `SEC` between the call and the
arithmetic. So `DVID4`'s exit carry lands in both, and the port was returning `R` alone.

**What the flag is.** `DVID4` has four exits and three of them leave the carry CLEAR. The eight
division steps cannot set it: `ASL A / STA P` puts a zero in `P`'s bit 0, and after eight `ROL P`s
that zero is what comes out — so the carry at the fall-through into `LL28`'s body is always clear
and what a caller sees is the logarithm divide's. That is set on the saturating exit (`LL222`,
which pins `R` at 255) and clear on the other three.

**And for `SPS2` it is always clear**, provably: a restoring division leaves a remainder smaller
than its divisor, `SPS2` fixes the divisor at twenty, and the saturation test is `A >= Q`. So
`ADC #195` always adds 195 with nothing extra, and `SBC T` always borrows — 156 comes out as 155.

Which raises whether it is worth modelling at all, and it is, for a reason that is not
faithfulness in the abstract. The port had `156 - T` before the flag was threaded, and that is
wrong by one for every compass position in the game. Working out that the flag is constant is
exactly the work that tells you WHICH constant, and a port that skipped the flag skipped that too.
The exhaustive `DVID4` sweep already existed, so widening the model cost one assertion line
(§6.42's pattern for the fourth time), and `SPS2`'s own sweep asserts the flag is clear on all 512
cases rather than assuming it.

**And the mutation pass shows the flag is dead in one of its two uses and load-bearing in the
other**, which is the sharpest statement of why constant does not mean ignorable. Replacing
`across.carry` with a literal `false` in the `ADC #195` survives — a clear carry adds nothing, so
the two are the same program. Replacing `down.carry` with `true` in the `SBC T` is caught, because
a clear carry there BORROWS. One always-clear flag, invisible on the add and worth a pixel on the
subtract, and no amount of reading the add would have told you which.

### 6.59 The §6.12 pass on slice 3d, and a row that is a slice and a half

Run before any of 3d is written, as every slice since §6.34 has been. This one is different in
scale rather than in kind: **45 source files and 210 external labels**, against slice 3c's 36.
The row is not wrong so much as too big to be one slice, and the pass says where the seams are.

**The row names sixteen files this build does not have.** It lists the main loop as
`mainloop_part_1_of_16` and so on; the C64 build includes
`main_flight_loop_part_1_of_16.asm`. And `spblb` is not a file at all — this build takes
`spblb-dobulb.asm`, which defines `SPBLB` and `DOBULB` together. Both are cosmetic in the row and
neither is cosmetic in a script: a scoping pass driven by the row's own names silently finds
nothing for seventeen of the forty-five files and reports a much smaller slice than there is.

**What the 210 labels are.** 78 are ported, 20 belong to phase 4 (`TACTICS`, `ANGRY`, `HITCH`,
`OOPS` and the rest to 4a; `DEATH2`, `EXNO`, `FAROF`, `SHD`, `DENGY` to 4b; `NWSPS`, `MVTRIBS` to
4c), 7 to other slices, 11 are presentation or hardware, and 77 are state and tables rather than
routines. **Twelve are prerequisites the row does not name:**

| Prerequisite | What it is |
|---|---|
| `ABORT`, `ABORT2` | unlock the missile; `ABORT2` is five instructions and reaches `MSBAR`, which the row does name |
| `DOT` | the compass dot — `CPIX2` or `CPIX4` depending on its colour |
| `KS1` | `LDX XSAV / JSR KILLSHP / LDX XSAV / JMP MAL1` — a loop wrapper, not a subroutine |
| `LOOK1` | changing view, which 3c's `FLIP` header already said was 3d's |
| `CTWOS` | the dashboard's two-pixel mask table (`CTWOS2` is extracted; this one is not) |
| `scacol` | the per-type blip colour, at 9854 |
| `cntr` | the damping routine, deferred out of phase 1 with row 94's seven and never picked back up |
| `U%` | clears the 57-byte key logger |
| `SETL1` | **not game logic at all** |
| `BOX`, `dec27`, `tnpr1` | text and border, filed under other rows |

`SETL1` is worth its own line. It is `SEI / STA L1M / LDA l1 / AND #%11111000 / ORA L1M / STA l1 /
CLI` — self-modifying code inside a raster interrupt handler, with interrupts disabled around it.
That is hardware, and it belongs behind a seam like the sound does, not in `GameLogic`. A scoping
pass that only asks "is this ported?" would have scheduled it.

**And `SCAN` is behind two seams with two different signatures.** `ShipEffects::UpdateScanner(
ShipBlock&)` for `MVEIT`, and `BubbleEffects::ScanShip(const ShipBlock&, std::uint8_t)` for
`WPSHPS` — one routine, two interfaces, disagreeing about whether the type is an argument and
whether the block is mutable. The game's `SCAN` reads `TYPE` and `INWK` and writes neither, so
the second is right and the first is wrong on both counts. When 3d ports it the two collapse into
one function and both seams go. §6.28's shape a fifth time, and the first time it has appeared in
the SEAMS rather than in the state — which is worth noticing, because a seam is exactly where a
port stops checking.

**The proposed split**, ordered so each unit rests on the last:

| | |
|---|---|
| **3d-a** 🟢 | `SCAN`, `COMPAS`, `DOT`, `SP1`/`SP2`, `SPS1`–`SPS4`, `TAS2`, `scacol` — **built 2026-09-04** in `Scanner.h/.cpp`, 44 mutations with 42 caught and two measured equivalents. `TAS2` was not in the list and is not optional: `SPS1` falls into it and `SPS4` jumps to it, and it falls into `NORM` (§6.62). Both seams gone (§6.61). |
| **3d-b** 🟢 | `DIALS` 1–4, `DIL`/`DILX`/`DIL2`, `MSBAR`, `ECBLB`/`ECBLB2`/`SPBLB`, `PZW`, `CTWOS` — **built 2026-09-04** in `Dashboard.h/.cpp`, 45 mutations with 44 caught and one equivalent. `DILX` is one routine with four entry points and three of them are shift counts (§6.63); `dec27` is an early RETURN and five sixths of the dashboard is one pass in four (§6.64); and the fourteenth flag is the first that an `SBC` reads rather than an `ADC` (§6.65). |
| **3d-c** 🟢 | `MESS`/`me1`/`mes9`, `ABORT`/`ABORT2`, `LASLI`/`LASLI2`/`las` — **built 2026-09-04** in `Messages.h/.cpp`, `Lasers.h/.cpp` and `Dashboard.h/.cpp`. Fixed `CLYNS`, which had been `CLYNS2` under its name since 1c (§6.67), and settled which of `LASLI`'s three uncleared adds matter (§6.68). |
| **3d-d** | the sixteen flight-loop parts with `LOOK1`, `KS1`, `WARP`, `SPIN`, `CTRL`, `cntr`, `U%`, the `DOKEY` flight half and the docking check — **866 instructions and 64 call targets, so §6.69 splits it again into 3d-d-i, ii and iii**, and adds `MAS1`–`MAS4`, `FRMIS`, `ECMOF` and `tnpr1`, which the row does not name |
| **3d-e** | `LAUN`/`LL164` and `DEATH` — the two set pieces |

3d-a first because `SCAN` is buildable today: it rests on `CPIX4` and `CTWOS2`, both ported, and
one table that needs extracting.

**What 3d-a cost, against that estimate.** The table extracted as expected and `CPIX4` needed one
change — it now hands back the cursor it leaves in `SC`, `Y` and `X`, which `SCAN` walks on from.
The three things the pass did not see: `TAS2` and `NORM` are part of the compass chain and neither
is in the row (§6.62); `DVID4`'s exit carry is read by `SP2` through `SPS2`, so a slice-3a routine
had to be widened to build a slice-3d one (§6.60); and closing the two seams changed `MoveShip`'s
signature and deleted a class (§6.61). A dependency pass that asks "what does this call?" finds
none of those, because none of them is a call.

### 6.58 One byte with two names, five routines with one exit, and a bounty that moves a planet

Slice 3c's last unit is the bubble — `KILLSHP`, `SOS1` and `SOLAR` — and it turned up three
things, each of which the port had already got wrong in a different way.

**`SSPR` is `MANY + SST`.** `MANY` is at 1117, `SSPR` at 1119, and `SST` is 2. So "is the space
station present" and "how many space stations are in the bubble" are ONE BYTE with two names.
That is why nothing ever sets `SSPR` when a station is created — `NWSHP`'s `INC MANY,X` has
already done it — and why `KS4`'s `STA SSPR` is how the count gets cleared. The port had modelled
it as a separate field, for about an hour; the sweep caught it on the first station kill, on the
`MANY+2` comparison rather than on anything named `SSPR`. §6.28's shape for the fourth time, and
this time the two names are in the ORIGINAL rather than introduced by the port.

**`SOLAR` does not return where it appears to.** It ends `LDA #129 / JSR NWSHP` with no `RTS`, so
it falls into `NWSTARS`, which falls into `nWq`, which falls into `WPSHPS`, which falls into
`FLFLLS`. **Five routines, five rows in the ledger, one fall-through** — arriving in a new system
fills the stardust, takes every ship off the screen and resets both line heaps as part of the
same call. §6.45 found the four-routine tail of this chain when scoping; the head was one further
back and nothing had looked. The symptom was the generator's state moving on the oracle side and
not the port's, which is the cheapest possible way to be told: **compare the RNG and a routine
that secretly calls it cannot hide.**

**And a bounty moves a planet.** `SOLAR` does

    .nobirths  LSR FIST / JSR ZINF / LDA QQ15+1 / AND #%00000011 / ADC #3 / STA INWK+8

and `ZINF` touches no flag, so the `ADC #3` runs on the bit `LSR FIST` shifted out. The planet's
distance from the player therefore depends on whether their legal status was ODD. Nobody designed
that. It is what happens when a routine is written straight through without a `CLC`, it has been
in every copy of the game ever sold, and it is the twelfth uncleared flag this port has had to
recover.

The pattern across all twelve is now worth stating as a checklist item rather than a lesson,
because it has stopped being surprising. Before porting a routine: **read the instruction after
every `JSR`, and read the instruction after every shift.** Eleven of the twelve would have been
caught by the first half and this one by the second.

### 6.57 An exhaustive sweep that was exhaustive in the wrong dimension

`nWq` fills the stardust field from scratch, and it does it like this:

    .SAL4  JSR DORND / ORA #8 / STA SZ,Y / STA ZZ
           JSR DORND / STA SX,Y / STA X1
           JSR DORND / STA SY,Y / STA Y1
           JSR PIXEL2
           DEY / BNE SAL4

Four calls and no `CLC` anywhere, so each speck's first random byte runs on the carry the
PREVIOUS speck's plot left. The eleventh dropped flag, and this one is a plotting routine rather
than an arithmetic one — `PIXEL`'s exit carry turns out to be exactly **`ZZ >= 80`**, because
three of its four exits leave the flag from a comparison against a distance threshold and the
fourth, the near case that plots a four-pixel block, falls past `CMP #80` without branching.

The finding is not the flag. It is what happened when the model needed verifying. Phase 1 built a
sweep for `PIXEL2` over **all 65,536 coordinate pairs** — genuinely exhaustive, named
`RelativePixelMatchesTheShippedRoutine`, and it pinned `ZZ` at 255 for every one of them. So it
never reached two of the three distance branches, and the exit carry it would have measured was
constant.

Changing one line — `ZZ` varying with the coordinates instead of fixed — costs nothing, covers
all three branches, and confirmed the new carry model over the whole space on its first run.

That is §6.52's shape in a test whose name says "exhaustively", and the two together suggest the
question to ask of any sweep: **exhaustive in WHICH dimension?** A sweep over all values of the
inputs a routine is *about* can still be constant in the byte that decides what it does. `PIXEL2`
is about x and y; it branches on `ZZ`.

### 6.56 Three equivalents, and two of them are facts about the original

The sun's mutation pass caught 24 of 27 after the drift table was widened from nine cases to
twenty — six of the nine survivors were branches about WHERE the centre is, and each needed a
placement worked out from the branch rather than guessed at, because `CHKON` has to accept every
one and a centre off the screen is only reachable when the radius brings its edge back on.

The three that remain are equivalent, and two of them say something about the shipped code.

**Crossing the two ends over cannot change the picture.** `PLF44` draws the difference between
last frame's line and this frame's as `HLOIN(x1_new, x1_old)` and `HLOIN(x2_new, x2_old)`, and
the port could equally draw `HLOIN(x1_new, x2_new)` and `HLOIN(x1_old, x2_old)` — the old line
and the new one. Under EOR those are the same set of pixels, for any four coordinates:

    [a,b) ⊕ [c,d) = 1{x≥a} ⊕ 1{x≥b} ⊕ 1{x≥c} ⊕ 1{x≥d} = [a,c) ⊕ [b,d)

so the crossing-over is not arithmetic at all. It is there so that each `HLOIN` draws a SHORT
span — the sliver that changed — rather than a full-width line. On a 1MHz machine that is the
difference between the sun being affordable and not, and it is invisible to any test that
compares pixels. The port keeps it because the reason it exists is worth being able to read; a
"simplification" here would be correct and would quietly throw away the routine's whole point.

**And one store in `PLF11` is dead.** The path for a row that had nothing on it ends

    JSR EDGES / BCC PLF16 / LDA #0 / STA LSO,Y / BEQ PLF6

and `EDGES`, on both of its carry-set exits, has already done `LDA #0 / STA LSO,Y` itself. The
store cannot be observed. Removing it from the port changes nothing, and it stays for the same
reason — a reader comparing the two listings should not have to work out which of two identical
stores is the live one.

The third is structural: `PLF6`'s `DEY / BEQ PLF8` exit leaves the row counter at zero, and
part 4's loop is `DEY / BNE PLFL3`, so entering part 4 with a zero counter does nothing. The two
exits differ in intent and not in effect.

Across slice 3c's six units the tally is now **twenty-two survivors: ten provable equivalents and
twelve gaps in the data**, and the split is worth keeping in mind next slice. Equivalents cluster
where the original does something for SPEED — the crossing-over here, `CNT`'s always-even step in
§6.51, `MULTU`'s structurally-zero carry in §6.43. Gaps cluster where the test data was chosen
for plausibility rather than derived from a branch.

### 6.55 The sun draws the difference, and a flag from the square root

`SUN` is the last routine of slice 3c and the cleverest thing in the drawing code. It is not a
filled circle. It is a stack of horizontal lines, one per screen row, whose half-widths are
`sqrt(K^2 - v^2)` with a few random bits added so the edge is ragged — and it **never erases and
redraws**. For each row it holds last frame's half-width and this frame's, clips the first against
last frame's centre and the second against this frame's, and draws only the two slivers that
differ:

    LDX LSO,Y / STA LSO,Y / BEQ PLF11
    LDA SUNX / STA YY ... / TXA / JSR EDGES / LDA X1 / STA XX / LDA X2 / STA XX+1
    LDA K3   / STA YY ... / LDA LSO,Y / JSR EDGES / BCS PLF23
    LDA X2 / LDX XX / STX X2 / STA XX / JSR HLOIN
    .PLF23  LDA XX / STA X1 / LDA XX+1 / STA X2 / JSR HLOIN

A sun drifting across the screen therefore costs two short lines a row rather than two long ones,
and on a 1MHz machine that is the difference between the sun being possible and not. It is also
why `SUNX` exists at all: the routine's last act is `LDA K3 / STA SUNX`, so this frame's centre
becomes next frame's old one, and a test that calls it once cannot see any of this.

Three things it got wrong, all in the walk rather than the arithmetic:

**`DEC V / BNE PLFL / DEC V+1` decrements unconditionally.** The branch decides only whether the
high byte follows it down — so `V` reaching zero is what flips the walk from coming in towards the
sun's centre to going out the other side, and it does it by making `V+1` negative rather than by
testing anything. The port decremented only when `V` was non-zero, which drifts by one row per
sun.

**`PLF6`'s `DEY / BEQ PLF8` leaves through the routine's TAIL, not through part 4.** A sun that
reaches the top of the screen has no rows above it to erase; the other exit, `PLF10`'s `CPX K`,
falls into part 4 because it does. Two exits from one loop, going to different places, and the
only sign is which label each branch names.

**And the tenth dropped flag.** `LL5` ends `... ROL A / TAX / DEC T / BNE LL6 / RTS`, and `DEC`
does not touch the carry — so `SUN`'s `JSR LL5 / LDY Y1 / JSR DORND` runs the generator on the
last bit to fall out of the square root. The sun's ragged edge is seeded by its own radius. The
exhaustive sweep over all 65,536 radicands verified the widened model on its first run, for one
line, which is §6.42's argument for the fifth time.

Ten dropped flags now, across §6.4, §6.11 (twice), §6.33, §6.42 (twice), §6.47, §6.50, §6.53 and
this. The rule has not changed since §6.11 first stated it; what has changed is how routinely it
is worth checking. **Before porting any routine, read the instruction after every `JSR` in it.**
That single habit would have caught eight of the ten.

### 6.54 Ten survivors, nine of them one mistake

The planet's mutation pass caught 24 of 34, and the ten survivors were not ten problems. Nine of
them were the same one: **every orientation vector in the sweep was positive and small.**

`PLS1` is nine instructions and it does three things a positive small axis cannot show. It masks
a sign bit off the magnitude (`AND #%01111111`). It SATURATES at 254 when the quotient needs two
bytes. And it returns `K+3`, the divide's sign, in Y. Four mutations of those behaviours all
survived, and so did three of `PLS3`'s, which negates a negative axis and has a special case for
a negation that comes out zero.

The remaining two were single missing values rather than a class: `PL9`'s `CMP #6` needed a
planet whose radius was exactly 5, and `PLANET`'s `CMP #48` needed a distance byte of exactly 48.

Adding four orientations — small and positive, some axes negative, large enough to saturate, and
the extremes — plus three distances straddling the size floor took the sweep from 72 cases to 408
and caught all ten. The cost was two seconds.

The pattern across §6.48, §6.51 and this one is now clear enough to state as a working rule. **A
surviving mutation is a question about the test data far more often than it is a question about
the code.** Of the nineteen survivors across slice 3c's five units, seven were provable
equivalents and twelve were data that never reached a branch — and in every one of those twelve
cases the fix was a handful of extra values, not a new test.

### 6.53 The planet: three defects, and the one that took the longest to see

`PLANET` and `PL9` came out wrong in three places, and they are worth separating because only one
of them is the kind a careful reading catches.

**The inverted sign.** `PLS22` decides which half of the turn it is on with

    CMP #33 / LDA #0 / ROR A / STA XX16+4

and `ROR` on a zero accumulator puts the carry into bit 7 — so the byte is 128 when the
comparison SET the carry, which is when the value reached 33. The port had it the other way
round, and every meridian's second axis came out on the wrong side of the planet. Two
instructions, and the mistake is reading `LDA #0 / ROR A` as "clear it" rather than as "collect
the flag".

**The ninth dropped carry.** `ADD` has three exits and not one of them clears the carry: the
same-sign path leaves what `ADC T1` produced, the `BCS MU9` path leaves it set by definition, and
the negating path leaves the second `SBC U`'s. `PLS22` then does `STA T / BPL PL42` and, at
`PL42`, `TXA / ADC K3` — nothing in between touches a flag. So a meridian's position on the
screen depends on it. The field was added to `AddSignedResult` rather than the signature changed,
because the other twenty-odd callers read only `high` and `low`, and the existing 200,000-case
sweep verified the wider model on its first run. That is §6.42's argument for the fourth time.

**And the one that took longest.** `PLS3` is

    .PLS3  JSR PLS1 / STA P / LDA #222 / STA Q / STX U / JSR MULTU / LDX U ...

and `PLS1` ends with two `INX`s. So `STX U` saves the STEPPED index, and `PLS3` hands back
X = 17 when it was called with X = 15. `PL26` calls it twice in a row without touching X in
between, and gets two different axes — which is the whole point, because the crater's offset has
an x and a y. The port passed 15 both times and drew every crater in the wrong place.

What makes this one different is that nothing in `PLS3` says so. `PLS1`'s two `INX`s are eleven
lines away in another file, and the only sign at the call site is the ABSENCE of an `LDX` before
the second call. **A routine that steps a register is part of its callers' arithmetic**, and the
port's `AxisResult` now returns the index for exactly that reason.

### 6.52 The third time a whole-canvas comparison proved nothing

§6.36 and §6.39 both recorded a sweep that agreed with the game byte for byte while the code
under test barely ran. This is the third, on the same slice, and the counter that caught it was
put there because of the other two.

The planet sweep compared 54 planets on the entire canvas, both line heaps and eleven zero-page
bytes, and passed. Then an assertion that `TGT` had been set — 31 by a meridian, 64 by a crater,
neither by anything else — failed with **0 meridians, 0 craters, 54 plain**. About half the unit,
`PL9`'s parts 2 and 3 and the whole `PLS` family, had never executed.

The cause was a misread coordinate again, and again in the same direction as §6.39's. A ship's
z is TWENTY-FOUR bits — `INWK+6`, `+7`, `+8` — and the planet's radius is `96 * 256 * 256 / z`,
so `K+1` is zero, and `PL9`'s `LDA K+1 / BEQ PL25` lets the markings through, only above
z = 24576. Every placement in the sweep was nearer than that: the closest was 512, giving a
radius of 12,288. The port and the game agreed perfectly about a planet far too big to have
markings, fifty-four times.

Corrected, the same sweep draws 8 meridians and 9 craters — and failed immediately, on all three
of §6.53's defects. The lesson is not new, but the frequency is worth stating: **three times in
one project, a byte-for-byte comparison against the shipped game has passed while the routine
under test did nothing.** The oracle answers *do we agree*. Only a counter answers *about what*,
and the counter has to name a state the code reaches rather than a call it makes.

### 6.51 Five survivors, five proofs, and where they all came from

The ball drawing's mutation pass caught 24 of 29, and every one of the five survivors turned out
to be provably equivalent — the opposite result to §6.48's, on the same discipline. What they have
in common is worth naming: **all five are consequences of `CNT` being a multiple of 2, 4 or 8.**

`CIRCLE` sets `STP` to 8, 4 or 2 and `CIRCLE2` steps `CNT` by it from zero, so `CNT` is always
even. That single fact settles:

- **The `CMP #33` threshold could be 32.** They differ only at `CNT = 32`, and `SNE[32 AND 31]`
  is `SNE[0]`, which is zero — so the value being negated is `K * 0 / 256` and the negation is a
  no-op that also leaves the carry where the unnegated path already had it.
- **`FMLTU2`'s exit carry could be dropped.** It feeds `LDA CNT / ADC #15 / AND #63 / CMP #33`,
  and one extra changes that answer only when `(CNT + 15) AND 63` is exactly 32 — that is,
  `CNT ≡ 17 (mod 64)`, which an even counter never is. §6.50 widened the routine for this call
  and the widening is unobservable *here*; `PLS22` steps `CNT2` differently and `DOEXP` reads the
  same flag from `FMLTU` directly, so it is not unobservable everywhere.
- **The loop could stop at 66 instead of 65.** `CNT` never lands on 65, because 65 is odd.
- **Both entry carries into `CIRCLE2` could be anything.** The flag reaches only the first
  `FMLTU2`, whose returned carry this code discards and whose returned byte does not depend on it
  (§6.50); `CPX #33` overwrites it two instructions later. `CIRCLE`'s `CPX #60` result is
  therefore dead, and so is the parameter that carries it.

All five stay as written. A port that "simplified" any of them would be relying on the same five
proofs without having done them, and the next person to change `STP` — `HFS2` sets it directly,
and `TT128` sets it for the chart's range circle — would break code that had no comment saying
why it was safe.

### 6.50 A flag that is a pass-through, and the two callers out of seven that read it

Porting `CIRCLE2` needed `FMLTU2`'s exit carry: `JSR FMLTU2 / TAX / LDA #0 / STA T / LDA CNT /
ADC #15`, with no `CLC`. The eighth dropped flag, and the third to be recovered for the price of
one line in a sweep phase 1 had already made exhaustive (§6.42).

What makes this one worth its own paragraph is that the flag is not simply an output. `FMLTU`
has four exits and two of them touch no flag at all:

    .FMLTU  STX P / STA widget / TAX / BEQ MU3 / LDA logL,X / LDX Q / BEQ MU3again ...
    .MU3       LDX P / RTS                    <- A is still zero, carry is THE CALLER'S
    .MU3again  LDA #0 / LDX P / RTS           <- same

So on a zero operand the routine hands the caller's own carry straight back, and the port needs
an entry carry as well as a returned one. That could have been a large change — `FMLTU` has five
callers across the build — and it is not, because of a property worth stating: **the returned
BYTE is zero on both pass-through exits whatever the flag was.** Only a caller that reads the
carry can tell the difference, and exactly two do — `DOEXP` (`JSR FMLTU / ADC R`) and `CIRCLE2`
through `FMLTU2`. The other five follow the call with a `STA`.

That is the check worth generalising, because it is cheaper than threading everything: when a
routine's flag turns out to be an operand, **read the instruction after each `JSR` before
deciding how far the change reaches.** Five of the seven call sites here needed nothing, and
knowing which five took one grep.

The sweep now runs both entry carries over all 65,536 pairs for `FMLTU` and `FMLTU2` alike, so
the pass-through is measured rather than reasoned about.

### 6.49 A counter with fourteen users, and the workspace it did not belong to

`CNT` is one byte at zero page 170 and it is written and read by `LL9` parts 6 and 8, `BLINE`,
`CIRCLE2`, `PLS22`, `SUN` parts 1 and 3, `TACTICS`, `DOEXP`, `PTCLS2`, `SPIN` and `STATUS`. The
port had it inside `GeometryWorkspace`, which is `LL9`'s — put there because `LL9` was the first
routine to need it, which is §6.45's mistake in its purest form.

The interesting part is that leaving it there would have been *unobservable*. Every one of the
fourteen initialises `CNT` before reading it back, so no unit hands it to another and two C++
fields could never diverge — the same argument that settled `XX2` against `K3` in slice 3b. The
difference is what it costs to be right rather than unobservably-not-wrong: `XX2` and `K3` are
fourteen bytes apart with different lifetimes and merging them would have meant restructuring
`LL9`; `CNT` is one field, and moving it to `MathWorkspace` — beside `T`, `T1` and `U`, where a
shared scratch byte belongs — took one edit and was verified immediately by the 1,881-ship sweep
that already existed.

So the rule the two cases together suggest is not "always merge" or "measure and leave it". It is:
**when the merge is cheap, do it and stop reasoning; when it is expensive, measure the bound and
write the measurement down.** What is not acceptable is the third option the port took here,
which is to leave it in the first workspace that needed it and never ask.

### 6.48 Three mutations survived, and none of them was equivalent

§6.43 argued that a mutation which cannot change behaviour is not a hole in a test, and that
calling one a hole is how a suite grows assertions that measure nothing. The planet and sun heap
gave the other half of the lesson on the same day. Twenty-six mutations, twenty-three caught, and
the three survivors looked at first like the same sort of thing. None of them was.

**`WPLS`'s guard is `LDA LSX / BMI`, and the sweep only ever put 0 or 255 there.** `!= 0` and
`& 0x80` agree on both, so the mutation was invisible — but the game can leave other values in
that byte, and 1 and 127 are exactly the two that tell the tests apart. A sweep over the two
values a routine *usually* sees does not measure a test on bit 7.

**`CHKON` has four exits that test bit 7 of a sixteen-bit sum, and the sweep's coordinates all
fitted in nine bits.** Every centre it tried was 0 to 300, so the high byte was 0 or 1 and no
negative branch was ever entered. Reading the sweep suggested it was thorough — sixteen hundred
cases over eleven centres, seven radii and both screen extents — and it was thorough in the
dimension that did not matter. The mutation is what said so.

**And one survivor was a badly written mutation of mine**: `FLFLLS` zeroing entry 0 as well is
harmless because the next line overwrites it with 255. Rewritten as "the loop stops one row
early", it was caught at once.

So the discipline is: a survivor is a question, not a verdict. Answer it with the reason it
cannot matter — and the reason has to be a property of the *game*, measurable, like §6.43's
always-clear carry — or admit it is a gap and widen the sweep. Of the five survivors across the
two units of this slice, two were provable equivalents and three were gaps, and only running the
mutations distinguished them. Both sweeps are wider now and all twenty-seven real mutations are
caught.

### 6.47 A carry dropped for two months, and what a chosen grid cannot sample

`WPLS2` is the first thing in the project to draw a few hundred arbitrary lines in a row, and on
its first run against the shipped game it differed. By one pixel, on one line of a heap of 255,
and 50 of the line's other bytes were identical:

    row 37, col 72:  game 128, port 192
    row 38, col 72:  game  96, port  32

One pixel on the wrong side of a character-row boundary — which is not a `WPLS2` defect at all.
It is `LOIN`, ported in slice 1d-a and swept 3,528 times since.

`LOIN`'s downward shallow path sets its screen pointer up like this:

    .DOWN  ... ADC ylookupl,Y / STA SC / BCC P%+5 / INC SC+1 / CLC / SBC #247 / STA SC
           BCS P%+4 / DEC SC+1 / TYA / AND #7 / EOR #%11111000 / TAY ...

and then reaches its loop through a `TAX`, a `BIT`, four table loads, an `LDX`, sometimes an
`INX` and a `BEQ`. **Not one of those touches the carry.** So the first `ADC Q2` — the accumulator
step that decides where the line's next pixel goes — runs on the carry out of `SBC #247`, which is
set whenever the pointer's low byte had already reached 248. The port started the accumulator at
carry clear.

That is the **seventh** uncleared 6502 flag to be the defect (§6.4, §6.11 twice, §6.33, §6.42
twice, and this) and the third in this one routine. §6.11's rule stands and can now be sharpened:
*a routine's carry chain does not begin at its loop.* It begins at whatever last wrote the flag,
which may be twelve instructions earlier and in a different labelled block.

The more useful finding is why it survived so long. 1d-a's sweep is 3,528 lines chosen to reach
**every branch** — both gradients, both directions, swapped and unswapped, degenerate spans,
lines within one cell and across several. That is the right way to build a small sweep and it was
not enough, because this routine's state is not a set of branches. It is a carry chain whose
behaviour depends on the accumulator's PHASE, and the phase depends on the start position's low
byte and the slope **together**. A grid of nine values per axis samples that space at sixty-odd
points out of four billion, and every one it picked happened to have a pointer under 248.

The mutation test says so precisely: with the defect reinstated, the grid sweep still passes and
a sample of four thousand random lines fails. Both are now in the suite, and the honest statement
of what they are for is different for each — **the grid reaches rare branches, the sample covers
phase space, and neither substitutes for the other.** Any routine whose output depends on
accumulated state, rather than on which branch it took, wants both.

One mutation is recorded as equivalent: dropping the carry on the UPWARD path changes nothing,
because that path's last carry-writer is `ADC #0` on the screen pointer's high byte, and the
canvas is 0x2800 bytes, so the addition cannot carry out of eight bits for any row the routine
can be asked for. The port threads it anyway, because the next person to read the two branches
should not have to work out which of them is safe.

### 6.46 One byte, two writers, and neither of them owned it

`SWAP` says whether the last line came back with its ends exchanged. Slice 3b modelled it as part
of `ClipState` — what `LL145` reports — and `LOIN` kept its own copy in a local variable. Both
were reasonable and together they were wrong, because there is one byte at 1780 and the writers
and readers do not pair up:

| | writes `SWAP` | reads `SWAP` |
|---|---|---|
| `LL145` | parts 1 and 4 | `BLINE` |
| `LOIN` | parts 1, 2 and 5 | `WPLS2` |

`WPLS2` asks `LOIN` a question that the port had given only `LL145` the ability to answer, and
the symptom was a planet erased with segments joined onto the wrong endpoints. It is now on
`DrawWorkspace`, which both routines already take, and `ClipState` keeps `XX13` and `dontclip`.

This is §6.28's shape — one 6502 byte, two C++ variables — arriving for the third time, and it
is worth naming what makes it happen: **the port models a byte as a routine's output, and the
game models it as a place.** When a second routine writes the same place, a return value cannot
express it. The check that catches this is not a test; it is one grep for the label before
deciding where it lives, which is what §6.45's pass did for `XX` and `YY` on the same afternoon
and would have done for this one had it been asked.

### 6.45 The §6.12 pass on 3c's second unit, and the comment it falsified was mine

Run before writing the planet and sun line heap, and the first thing it found was a sentence
committed an hour earlier. `Stardust` held `XX(1 0)` and `YY(1 0)` with the note *"they are here
rather than in `MathWorkspace` because nothing outside the stardust reads them"*. A scan of every
file the C64 build assembles says otherwise:

| Label | Files that touch it |
|---|---|
| `YY` | `stars1`, `stars2`, `stars6`, `pix1`, **`edges`**, **`wpls`**, **`sun` parts 2, 3 and 4** |
| `XX` | `stars1`, `stars2`, `stars6`, `mls2`, `mut1`, `mut2`, **`sun` part 3** |
| `newzp` | `stars2`, and nothing else |

`XX` is at 93, `YY` at 95 and `SUNX` at 97 — three consecutive sixteen-bit values, which is what
they are: a shared coordinate pair, not a workspace one routine owns. The two users are never
live at the same time, which is precisely why nothing would ever have failed and why the comment
would have survived until someone tried to give `EDGES` a `Stardust&`. `XX` and `YY` are now in
`MathWorkspace`; `newzp` stays, because it really is the stardust's alone.

The correction that matters is not the move. It is that **where a routine lives and what it reads
are separate questions**, and the ledger has now conflated them seven times. `MLS2`, `MUT1` and
`MUT2` stay in `Stardust.cpp` — their only callers in the entire build are `STARS1` and `STARS6`,
so that is filing by what they do — and they take `MathWorkspace` rather than `Stardust`, because
the bytes they read are shared. Row 94 files them under `Arith.cpp`; row 94 is wrong about the
file and right about the parameter, and the port had it exactly the other way round.

Three more findings from the same pass:

**`NWSTARS` is not a routine.** It is `LDA QQ11 / BNE WPSHPS`, falling through `nWq` → `WPSHPS` →
`FLFLLS` → `RTS`. Four labels the ledger lists separately, one chain, and the head cannot be
ported without the tail — which means the planet and sun line heap has to come *before* the
stardust's initialiser rather than after it. `WPSHPS` reaches `SCAN`, so the chain also carries a
seam.

**`PL44` is defined in two files.** `edges.asm` and `pls6.asm` both have a `.PL44`, each behind an
`IF`, and only `pls6`'s is in this build — so `CHKON`'s `BMI PL44` branches into the tail of a
routine slice 3b already ported, not into the one sitting next to it in the source. Both happen
to be `CLC / RTS`, so a port that picked the wrong one would be right by luck. That is worth
saying plainly: **the check that a label resolves to what you think is not vindicated by the
output agreeing.**

**`Yx2M1` is a variable, and the header calls it a constant.** The upstream comment for `CHKON`
documents `CPX #2*Y-1`; the C64 assembles `CPX Yx2M1`, a byte at 184 that `TT23` sets to **199**
and `TT23`'s own tail and `RES2` set back to **143**. It travels in lockstep with `dontclip` —
the same two-instruction pairs write both — so it is the second byte of the same view-extent
state §6.38 found, and the slice that makes `TT23` write one must write both. The clipper is
unaffected: `LL118` and `LL145` compare against the literal, and the scan confirms `Yx2M1` has
exactly four readers — `CHKON` and `SUN` parts 1 and 2.

### 6.44 Three views, three routines, and the six instructions that route between them

The stardust is the whole of Elite's sense of motion — twelve specks in a box, moved and redrawn
every frame — and the temptation on reading it is to see one routine written out three times with
the signs changed. `STARS1` and `STARS6` are 100 instructions each and about eighty of them are
the same eighty. They are not the same routine.

Five things differ between the front view and the rear, and only the first two are what
"backwards" predicts: the position steps subtract where the front view adds, and the distance
grows rather than shrinking. The other three are not derivable from the direction of travel at
all. The coordinates are computed in the opposite order (x then y, against y then x). The roll's
two sign bytes are **swapped**, so `STARS6` opens with `EOR ALP2` where `STARS1` opens with
`EOR ALP2+1`. And the pitch step multiplies by the negated x, where `STARS1` — through the
`STA Q / JSR MUT2` pair, which leaves `A` holding what it has just stored — squares its own scale
factor instead. The kill tests differ too: three in the front view, on x, y and distance, and two
in the rear, on y and distance only, with different thresholds.

A port written as "STARS1 with a sign parameter" would agree with the game on a stationary
player, disagree subtly under roll, and be unrecoverably adrift after a few seconds of flight.
Both routines are written out here for that reason (ADR-003), and eleven mutations of `STARS6`
against `STARS1`'s shape are all caught.

The routing between the three is six instructions and one of them matters:

    .STARS  LDX VIEW / BEQ STARS1 / DEX / BNE ST11 / JMP STARS6 / .ST11 JMP STARS2

`STARS2` is entered with the view **already decremented**, so its own `CPX #2` is comparing
against the left view rather than against 2 as a view number. Read the two routines apart — which
is how a ledger row invites you to read them, one line each — and `RAT` comes out inverted, the
left view slides the way the right view should, and every screen still looks like stardust. The
port models `MoveStardustSideways` as taking the real view and doing `static_cast<std::uint8_t>(
_view - 1u) >= 2u`, and `TheDispatcherMatchesSTARS` runs all four views through `STARS` itself so
that the `DEX` is observable rather than argued about.

`STARS2` also has side effects the other two do not. `ST2` turns `ALPHA`, `ALP2` and `BET2` over
on the way in and back on the way out, and recomputes `ALP2+1` and `BET2+1` as exact complements
whether or not they went in that way, leaving `RAT` and `RAT2` behind it. A whole call is very
nearly a no-op on the flight state and not quite one, which is why `FlightState` is taken by
non-const reference here and by const reference in the other two, and why seven flight bytes are
compared alongside the canvas and the field.

### 6.43 An always-clear flag, measured rather than argued

§6.42 widened three multipliers to return their exit carry because the stardust reads it in the
very next instruction. Mutation testing then produced a result worth keeping: replacing that
carry with a hard `false` in both movers changes **no output at all**, on 280 frames of whole-
canvas comparison per view.

The reason is structural. `MU11` is `LSR P` followed by eight `ROR P`, and `LSR` shifts a zero
into bit 7 — so that zero walks down one place per rotate and falls out of the ninth shift as the
carry, whatever `P` and `Q` hold. `MU1`, the `Q = 0` path, has an explicit `CLC`. `MULTU`'s exit
carry is therefore always clear, and every `ADC` and `SBC` in the game that follows it without a
`CLC` is running on a zero it can rely on.

That argument is a paragraph of reasoning about a listing, which §6.29 is the standing warning
about, so it is not what the project rests on: the exhaustive sweep now asserts `IsFalse` on the
**oracle's** carry over all 65,536 pairs. The property is measured on the game.

Two things follow. The widening stays — it documents a dependency correctly and costs nothing,
and the same field on `MLTU2` is not constant (§6.33 was a real defect). And the two surviving
mutations are recorded as **equivalent**, with the measurement that makes them so, rather than as
gaps: a mutation that cannot change behaviour is not a hole in a test, and calling one a hole is
how a suite grows assertions that measure nothing.

### 6.42 A dropped exit carry, for the fourth and fifth time

Building the stardust needed `LL38`'s carry and `MLU1`/`MLU2`'s, neither of which the port
returned. `LL9`'s face loop does `JSR LL38 / BCS ovflw`, and the stardust does
`JSR MLU1 / STA YY+1 / LDA P / ADC SYL,Y` — no `CLC` in either, because the flag is an operand.

`CombineSigned` now returns `SignedSum{value, carry}` and `MultiplyByX`, `MultiplyUnsigned` and
`MultiplyMagnitudeByQ` return `WideResult{high, carry}`. In every case the fix was cheap because
phase 1 had already built an **exhaustive** sweep for the routine: adding a carry assertion to a
loop that already runs all 65,536 pairs turns "I think this is right" into a measurement for the
price of one line, and all four passed on the first run.

That is the argument for exhaustive sweeps where the input space allows one, and it is a
different argument from the usual one. Their value is not only what they catch on the day. It is
that when the model of a routine has to be **widened** two months later, the sweep that already
exists verifies the wider model for free — and the alternative, writing a fresh test for a flag
nobody has needed until now, is exactly the work that gets skipped.

This is now the fifth and sixth time an uncleared 6502 flag has been the defect (§6.4, §6.11
twice, §6.33, and these). The rule §6.11 named still holds and is worth restating in its
strongest form: **on the 6502 a flag is an operand.** A routine's return value is every flag its
callers read, and the way to find out which those are is to look at the instruction after each
`JSR` — not at the routine.

### 6.41 A ✅ is one mark for a whole row, and one of them was not true

Scoping 3c turned up a routine the ledger says is ported and that does not exist. `PIX1` sits in a
row marked ✅ — *"`hloin`, `pixel`, `pixel2`, `pix1`, `cpix4`, `cpix2-cpixk` … Ported 2026-09-03
(slice 1d-a)"* — and there is no `PIX1` anywhere in `GameLogic/`. The row's own notes give the
test for each of the other five and none for it, which is the tell.

It is not a hard routine: `JSR ADD / STA YY+1 / TXA / STA SYL,Y`, falling into `PIXEL2`. What it
needs is `SYL`, the stardust's y fractions, which did not exist in slice 1d-a — so it was quietly
skipped, and nothing noticed, because **`tools/inventory.py` reconciles include FILES against rows
and the ✅ is a human claim about all of a row's labels at once.** A row can carry six labels and
five ports and still pass every check in the repository.

Found by asking a different question: for every label that has its own include file and sits in a
✅ row, does anything in `GameLogic/` mention it? That turns up thirteen, of which most are
artefacts — the `_part_N_of_M` names, extracted data, `qw` (two instructions inside `TT27`, which
is ported), and `mls1`, whose row already says honestly that only its `MULTS` body was done.
`PIX1` is the one confirmed gap, and it matters because `STARS1`, `STARS2` and `STARS6` all call
it.

**Four are unresolved and are recorded rather than claimed either way**: `setxc`, `setyc`,
`setxc-doxc`, `setyc-doyc` and `tnpr1` each have their own include file, sit in a ✅ row, and have
no marker. Each is two or three instructions and each is plausibly folded into a neighbour, which
is legitimate — but "plausibly" is what this project's method exists to avoid, and confirming them
means reading five call sites rather than guessing. That is a job of its own.

The tooling question this raises is the same shape as the one already open on `tools/inventory.py`
— it matches only `//\s*6502:` line comments and misses 292 block-comment markers. Both are
judgements about what the ledger COUNTS rather than bugs, and both belong in a change of their own
rather than in the middle of a slice.

### 6.40 The §6.12 pass on slice 3c: twelve prerequisites, eleven unnamed

Run before any of 3c is written. The slice is the planet, the sun and the stardust — thirty-six
source files — and its complete external surface is thirty-six labels. Twenty-four of them are
already ported; `SCAN` is 3d's and `KS2`, `KS4`, `ABORT` and `MESS` belong to `KILLSHP`'s row.
**Twelve are prerequisites that do not exist yet, and the plan's row for the slice names one.**

| Routine | Where the ledger files it | Why 3c needs it |
|---|---|---|
| `MLS1`, `MLS2`, `MLU1`, `MUT1`, `MUT2` | row 94, `Arith.cpp` | `STARS1`, `STARS2` and `STARS6` multiply through them |
| `DV41`, `DV42` | row 94, `Arith.cpp` | the same three divide through them |
| `HLOIN2` | row 116, `Lines.cpp` | `SUN` parts 2 and 4, and `WPLS` |
| `ZINF` | row 44, `Bubble.cpp` | `SOLAR` clears a block with it |
| `BLINE`, `CIRCLE`, `CIRCLE2` | row 117, `Circles.cpp` | `CIRCLE2` and `PLS22` draw arcs through `BLINE` |

Every one of the seven in row 94 was deferred out of phase 1 with the same sentence: *"Each reads
game state the port has not defined yet — ship slots (`INWK`), the rotation angles (`ALP1`), the
stardust arrays or the damping flag — so they land with the workspace they belong to rather than
with the kernel."* That was right when it was written and the reason has since expired: `INWK` and
`ALP1` came with 3a, and the stardust arrays are 3c's own. **A deferral reason is not a schedule**,
and nothing put them back on one — which is the same shape as the six homes §6.35 to the slice-3b
log record, one level up. There the ledger filed a routine by what it sits next to; here it
deferred a routine and no row picked it up again.

The closure stops cleanly, which is the useful half of the finding. What those twelve themselves
reach is `CHKON`, `EDGES` and `RTS2` — all already in 3c's row — plus `FMLTU2`, `HLOIN`, `LL145`,
`LOIN` and `MU6`, all ported. So the slice is thirteen routines larger than its row says and not
one routine deeper.

Two smaller corrections while the pass was open. The row writes *"`CIRCLE` uses"*, which names
neither `CIRCLE2` nor `BLINE`, and `BLINE` is the one that matters — it builds the planet and sun
line heap in `LSX2`/`LSY2` and clips through `LL145`, so it is the routine 3c's whole erase-by-EOR
scheme rests on. And the row does not name `SOS1`, which `SOLAR` calls; the ledger does.

### 6.39 `LL9`, and a whole-canvas comparison that proved nothing

The hardest routine in Elite is ported. All thirty-three ship types in twelve placements — 396
ships — compared against the shipped game on the entire 10,240-byte canvas, the entire line heap,
`INWK`, the two bytes it writes into `K%` through `INF`, the face flags in `XX2` and every
projected vertex in `XX3`. It matched on the first run apart from one thing, and that one thing
was §6.37's warning coming true in the very next unit.

**The `XX2` / `K3` aliasing, settled by measurement rather than by argument.** `XX2` is at 53,
`K3` at 53 and `K4` at 67, so `XX2+0`, `+1`, `+14` and `+15` are the projected screen position as
well as face flags. On the `SHPPT` path the original's `PROJ` writes them and this port's separate
`Projection` does not, so the four differ by construction — and the test found it immediately, on
the fifth placement, reporting `XX2+0` as 192 where the port had 0. 192 is `128 + 64`: the
projected x coordinate, sitting in a face flag.

Rather than reason about whether that matters, it was measured. Across all thirty-three
blueprints, every face index named by any vertex or any edge is either **below that ship's own
face count** — so `EE29` or `EE30` writes it — or **exactly 15**, which part 3 sets to 255 before
either of them runs. There is no blueprint that can read a stale face flag at all, aliased or
otherwise. The port's separation is therefore unobservable, on the same footing as replacing
`UNIV` with an array index: a divergence with a bound that was counted, not asserted.

**And §6.36 happened twice more, both times in the test data rather than the port.**

The first was mild: the placements were chosen without reference to blueprint byte 13, the range
past which a ship becomes a dot, so 317 of 363 cases were rejections. The sweep agreed with the
game about doing nothing.

The second was not mild. **Every orientation vector the test supplied was exactly zero.** Elite
stores a ship's three vectors as sixteen-bit sign-magnitude pairs — `(lo, hi)`, with the magnitude
in the HIGH byte and the sign in its bit 7 — so a unit vector of 96 is `hi = 96, lo = 0`. The test
wrote 96 into the low byte. `LL21` reads each pair as `ASL lo / ROL hi`, which is nine bits of the
high byte and nothing else, so all nine components scaled to zero. The suite passed: 396 cases,
whole-canvas comparisons, every byte matching. It drew 17 wireframes where the corrected data
draws 208.

That is the sharpest form of §6.36 so far, and it deserves stating as a rule rather than as an
anecdote: **a whole-canvas comparison against the shipped game is not evidence that the routine
under test did anything.** The oracle answers "does the port agree", and a port agrees perfectly
about a ship with no orientation, no visible faces and no lines. Only the counters answer "was
there anything to agree about" — and on this unit they were the difference between a test that
exercised the face loop, the vertex loop, the clipper and the heap, and one that exercised four
early returns.

**One more dropped register, the third (§6.33).** `LL38`'s exit carry is its documented overflow
flag, and `LL9`'s face loop branches on it: `BCS ovflw` halves the ship's position and starts the
face again. The port returned only the byte. The fix was small, but the check on it was not
invented for the occasion — phase 1's exhaustive `LL38` sweep now compares the carry as well, and
passes, so the model is verified by 65,536 cases that already existed.

**Sixteen mutations, fourteen caught, and the two that were not are stated rather than glossed.**
The first pass caught twelve; four survived, and hunting inputs for them was the wrong instinct —
what fixed two of them was WIDENING WHAT IS COMPARED. `XX18` is the ship's position as the face
loop leaves it, and the `ovflw` retry is the only thing that touches it after the dot products, so
comparing those nine bytes is how a retry that ran, or failed to, becomes visible from outside the
routine. Adding them, and running all three orientations against every placement rather than one
apiece, took the sweep from 396 ships to 1,881 and caught both.

The two that remain:

- **`CNT`'s carry into the vertex index is unreachable**, and that is a count rather than a
  guess: the vertex loop runs once per six bytes of vertex data, at most thirty-seven times, so
  `CNT` reaches 148 and never wraps. The `ADC` that carries into `XX17` therefore always carries
  zero, and a port that passed a constant zero would be correct for every blueprint that exists.
- **The edge loop's heap-full test fires but its consequence does not.** Thirteen of the 1,881
  ships fill the allowance in blueprint byte 5 — the transporter reaches 149 with nine edges still
  to walk — and yet removing the `break` entirely leaves all 1,881 outputs byte-identical. So in
  none of them is there a DRAWABLE edge after the fill; the condition is exercised and its effect
  is not. That is a gap in the sweep and it is recorded as one.

**What is behind the seam and why.** `PLANET` and `DOEXP` are tail jumps into 3c and the explosion.
The six instructions that SET UP an explosion cloud are behind the seam too, and that is a
judgement worth recording: what they write is `DOEXP`'s state, and the `JSR DORND` among them runs
on whatever carry `LOIN` last left — which this port cannot determine without reading all
thirty-two of `LOIN`'s unrolled copies. The test asserts the seam is never reached rather than
pretending the path is covered.

### 6.38 The upstream headers document the BBC, and their constants are not the C64's

The line clipper — `LL145` and `LL147` with `LL118`, `LL120`, `LL123` and `LL129` under them — is
seven routines that jump into each other's bodies, and it went in as one unit: 16,384 slope steps,
8,192 point moves and 8,192 clipped lines, both entry points, every outcome counted.

**`XX13`'s documented values are wrong for this build.** The header says it comes back as 0, 95 or
191. On the C64 it is 0, **143** or **71**, because the constant is `Y*2-1` and the C64's `Y` is 72
where the BBC's is 96. The mechanism survives — 143 has bit 7 set and 71 does not, exactly as 191
and 95 do, and the `BPL` at `LL83` reads that bit — so a port written from the header's numbers
would branch correctly and clamp to the wrong row. This is the first place in the port where the
upstream *prose* was right and its *numbers* were not, and it generalises: the library serves ten
versions from one set of headers, and a "Returns" block documents whichever version its author had
in front of them. The resolved source is the authority for a value; the header is the authority for
what the value means.

**`LL145` reads a flag the ledger does not mention, and it belongs to slice 2.** `dontclip` is set
to 199 by `TT23` so that the short-range chart can draw over the whole screen instead of being
clipped to the space view, and set back to 0 by `RES2` and by `TT23` on its way out. §6.12's
pattern usually points inward — a row that under-scopes what a routine reads. This one points
OUTWARD: a 3b routine reading a byte a slice-2 routine owns, where the slice-2 routine is already
ported and does not write it. It is a parameter on `ClipState` now, and 3c's `CIRCLE2` and `BLINE`
will need `TT23` to start setting it. Better recorded here than found then.

**And the chart raises the screen's bottom edge TWO different ways, which must not be unified.**
`TT23` sets `dontclip` to 199 and, in the same two instructions, `Yx2M1` to 199 as well; `RES2`
puts them back to 0 and to `2*Y-1`. `Yx2M1` is the screen's last row as a VARIABLE — and
`LL145` and `LL118` do not read it. In the C64 build they use the assembled literal `Y*2-1`, and
the chart gets past them by switching clipping off entirely. The routines that do read `Yx2M1` are
`SUN` and `CHKON`, both of which are 3c. So `SPACE_VIEW_BOTTOM` being a compile-time constant is
right for the clipper and will be WRONG for the sun: 3c needs a runtime byte, and reusing the
constant there would draw the chart's sun clipped to a dashboard that is not on the screen.

**One defect, and the sweep found it on the first run.** `LL122`, the multiply half of the slope
step, does its first `LSR S / ROR R / ASL Q` *before* the loop, and the "have we run out of
multiplier" test lives only at `LL126` — which shifts again before testing. So a multiplier of
zero still gets TWO shifts and not one. The port shifted once, and the failing case was the
smallest one in the sweep: gradient zero, R = 128, game leaves 32, port left 64. Every other
routine in the unit matched first time.

**And a §6.29 in miniature, caught by a mutation rather than by a test.** In `LL115` the x
difference's high byte is negated into the ACCUMULATOR and never stored back, and the first draft
of the comment on it said that was load-bearing: `XX12+3` keeps its signed value while the scaling
loop shifts the magnitude, and a port that "tidied" it by storing the magnitude back would lose
the direction the caller reads two instructions later. Plausible, and wrong. `XX12+3` is not read
again between the negation and `LL116`, which overwrites it with `S`. The mutation that stores it
back passes every one of the 32,768 comparisons, because it is equivalent. Fourteen mutations,
thirteen caught and that one recorded — and it is the second time now that a confident sentence
about *why* something is written a certain way turned out to be the wrong half of a correct port.

The other miss was not equivalent and has been fixed. `dontclip` is tested with `BIT / BMI`, so the
contract is BIT 7, and `TT23` only ever writes 199 while `RES2` only ever writes 0 — both of which
a whole-byte test agrees with. A port reading the whole byte would have matched the game
everywhere. The sweep now includes `dontclip = 1`, which the game never produces, to hold the port
to the contract rather than to the two values it will see. **Where a flag's contract is narrower
than its use, the sweep has to carry a value the game does not.**

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

**And `XX15`, where the first answer was expensive and wrong.** `XX15` is six bytes to the
geometry and four to the line drawing, and they are the same six: `X1`, `Y1`, `X2`, `Y2` are
`XX15` to `XX15+3`. `LL145` is what settles that this is a calling convention and not storage
reuse — it takes three SIXTEEN-bit coordinates in `XX15(5 0)` and returns four EIGHT-bit ones in
`X1`, `Y1`, `X2`, `Y2`, overwriting its own arguments as it clips. `XX15+1` is `x1_hi` on the way
in and `Y1` on the way out. There is no point at which a copy between two structures could be
made, so keeping `DrawWorkspace` and a separate geometry vector is not an option that survives
reading the routine.

The first conclusion drawn from that was that `DrawWorkspace` has to become an addressable block,
`x1`/`y1`/`x2`/`y2` stop being plain fields, and 154 call sites across `Lines.cpp`, `Charts.cpp`,
`ShipDraw.cpp` and four suites get renamed — a commit of its own before `LL9`. That was wrong, and
one grep says so: across all twelve parts of `LL9`, all four of `LL145`, and `LL51`, `LL61`,
`LL62`, `LL118`, `LL120`, `LL123` and `LL129`, **`XX15` is never indexed by a register.** Every
access is `XX15+n` with a literal `n`. `XX1`, `XX2`, `XX3`, `XX12`, `XX16` and `XX18` are all
indexed by X or Y and do have to be arrays; `XX15` does not, and neither do `CNT`, `SWAP`, `XX0`,
`XX13`, `XX17`, `XX19`, `XX20` or `XX4`.

So the change is two more named bytes on `DrawWorkspace` and no rename at all, and it lands with
`LL51` — the first routine that reads them — rather than in a commit of its own. The general point
is worth keeping: **whether a 6502 workspace needs to be an array in the port is decided by
whether a register ever indexes it, and that is one grep rather than a judgement.** It is the same
question §6.8 asks about a table's SIZE, asked about its SHAPE.

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
| **3b Ship drawing** 🟢 **Built 2026-09-04** | ✅ `LL9` 1–12, ✅ `LL61`, ✅ `LL62`, `LL118`, `LL120`, `LL123`, `LL129`, ✅ `LL145`/`LL147` 1–4 with ✅ `LL118`, ✅ `LL120`, ✅ `LL123`, ✅ `LL129`, ✅ `SHPPT`, ✅ `LL51`, ✅ `PROJ`, ✅ `PLS6`, ✅ `DVID3B`/`DVID3B2`, ✅ `PLUT`. Title screen rotating ship. | Oracle: for sampled ship types and orientations the list of clipped line segments matches; golden of the title screen Cobra at frames 1, 30, 60. **The scope line is now twice corrected.** §6.34 removed `LL5` (ported in phase 1) and `LSX2`/`LSY2` (3c's heap, not the ship heap). §6.35 removes `PL2` — `PROJ` reaches `PL2-1`, which is `PROJ`'s own `RTS`, and `PL2` itself erases the planet and sun heap — and adds `PLS6`, which the 3c row had grouped with the planet code by name. **`PROJ` and everything it divides through are built and swept**: 65,280 divides, 65,536 divides by a ship's z, 65,536 screen offsets across all four of `PLS6`'s exits, and 3,072 projections including the half-written case. **The ship line heap and the three routines that read it followed the same day**: `LL155`, `LL81`, `EE51` and `SHPPT`, compared on the whole canvas rather than on a byte, and `SHPPT` compared as a SEQUENCE because it reads a coordinate the previous projection left behind. Twenty-nine mutations caught across the two units, two equivalent. §6.36 is the one finding that came from a coverage assertion rather than from a comparison. **The clipper followed**: `LL145`/`LL147` with `LL118`, `LL120`, `LL123` and `LL129`, 32,768 comparisons across the three suites, one defect (`LL122`'s entry shift is outside its loop) and two ledger findings in §6.38 — `LL145` reads slice 2's `dontclip`, and `XX13`'s documented values are the BBC's, not this build's. **`LL9` itself went in last and matched first time**: 1,683 ships — every type in fifteen placements and three orientations — compared on the whole canvas, the whole heap, `INWK`, the `K%` bytes, `XX2` and `XX3`. The one divergence was the `XX2`/`K3` overlap §6.37 predicted, settled by counting rather than arguing. §6.39 is the finding: the FIRST version of that test passed 396 whole-canvas comparisons while every orientation vector it supplied was zero. `PLUT` closed the slice, and it belongs in `ShipMove.cpp` — the sixth home the ledger had filed by what a routine is NEAR rather than by what it does. **What is left of 3b is the half a hosted runner cannot do**: the golden of the title screen's rotating Cobra at frames 1, 30 and 60, which needs a person at a Windows machine, as 2e's does. |
| **3c Planet, sun, stardust** 🟢 **Built 2026-09-04** | `PLANET`, `PL9` 1–3, `PLS1`–`PLS6`, `PLS22`, `WPLS`/`WPLS2`, `WP1`, `EDGES`, `CHKON`, `PL21`, `SUN` 1–4 with its heap, `CIRCLE`, `CIRCLE2`, `BLINE`, `SOS1`, ✅ `STARS`, ✅ `STARS1`, ✅ `STARS2`, ✅ `STARS6`, `NWSTARS`, ✅ `FLIP`, `WPSHPS`, `FLFLLS`, `SOLAR`, `NWQ` — **and the twelve prerequisites §6.40 found the row missing**: `MLS1`, `MLS2`, `MLU1`, `MUT1`, `MUT2`, `DV41` and `DV42` from row 94, `HLOIN2` from row 116, `ZINF` from row 44, and `BLINE`/`CIRCLE`/`CIRCLE2` from row 117. The seven in row 94 were deferred out of phase 1 because they read state that did not exist; `INWK` and `ALP1` arrived with 3a and the stardust arrays are this slice's own, so the reason has expired. **Seven of the twelve are built**: `MLS1`, `MLS2`, `MLU1`, `MUT1`, `MUT2`, `DV41` and `DV42` went in with the stardust, and `PIX1` — which §6.41 found marked ported and absent — with them. `HLOIN2`, `ZINF`, `BLINE`, `CIRCLE` and `CIRCLE2` remain. | Goldens of the launch view at Lave (planet + sun + stardust) at several iterations; oracle for `PLS`/`CHKON` arithmetic.<br><br>**The stardust unit is complete, 2026-09-04.** Six parallel arrays, seven wrappers, `PIX1`, `FLIP`, all three movers and the dispatcher: 280 frames per view compared on the whole canvas, all six arrays, the generator's state and — for the side views — seven flight bytes and `newzp`. Thirty of thirty real mutations caught, two equivalent and measured (§6.43). Three findings: the three views are three routines and not one with a sign (§6.44), the `DEX` in `STARS`'s six-instruction dispatch means `STARS2` compares against the LEFT view rather than against 2, and `MLU1`/`MLU2`/`LL38` needed their exit carries returned (§6.42) — the fifth and sixth time an uncleared flag has been the defect. **Slice 3c is complete.** Its acceptance criterion — goldens of the launch view at Lave — needs a person at a Windows machine, as 2e's and 3b's do.<br><br>**The planet and sun line heaps followed the same day**: `LSO`/`LSX`, `LSX2`/`LSY2` and `LSP`, with `EDGES`, `HLOIN2`, `FLFLLS`, `WP1`, `WPLS`, `WPLS2`, `PL2`, `CHKON` and `PL21`. Both sizes come from the layout rather than from an estimate, and the two ball arrays are one block because `BLINE` indexes across their join. 27 of 27 mutations caught, after three survivors turned out to be gaps rather than equivalents (§6.48). **The two defects it found were both in already-shipped slices**: `SWAP` is one byte with two writers and the port had it as `LL145`'s return value, so `WPLS2` could not ask `LOIN` (§6.46); and `LOIN`'s downward setup ends `SBC #247`, whose borrow its accumulator reads twelve instructions later — dropped since 1d-a, one pixel of one line in nine, and invisible to a 3,528-case sweep chosen to reach every branch (§6.47). A four-thousand-line sample now runs beside that grid, and the mutation test confirms the grid alone still misses it. |
| **3d Flight loop and dashboard** — **scoped 2026-09-04 into 3d-a … 3d-e (§6.59); the row is a slice and a half** | Main flight loop 1–16 (`main_flight_loop_part_N_of_16`, NOT `mainloop_part_N`), `DIALS` 1–4, `DILX`/`DIL2`, `COMPAS`/`SP1`/`SP2`/`SPS*`, `SCAN` (sprite blips as canvas draws), `MSBAR`, `ECBLB`/`ECBLB2`/`SPBLB` (in `spblb-dobulb.asm`), `PZW`, `MESS`/`me1`/`mes9`, `LASLI`, `LAUN`/`LL164` hyperspace tunnel, `DEATH` (the "GAME OVER" fly-by), `WARP` (J), `CTRL`, `DOKEY` flight half, `SPIN`, `cargo` canisters, docking check (`ISDK` path in loop part 10–11). **Plus the twelve prerequisites §6.59 found the row missing**: `ABORT`/`ABORT2`, `DOT`, `KS1`, `LOOK1`, `CTWOS`, `scacol`, `cntr` (deferred out of phase 1 with row 94's seven), `U%`, `BOX`, `dec27` and `tnpr1`. `SETL1` is NOT one of them: it is self-modifying code inside a raster interrupt handler and belongs behind a seam like the sound, not in `GameLogic`. | Launch from Lave, fly, dock manually, hyperspace to Diso, dock. Goldens of the dashboard; replay hashes for the whole trip. |

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
| 2026-09-04 | **Slice 3d-d-iii-a builds the screen setup, and stops one seam short.** The dashboard bitmap loaded at `DSTORE%` first (§6.78), then `ZES1k`/`ZES2k`, `mvblockK`/`mvbllop`, `BOXS`, `BOXS2`, `BOX2`, `BLUEBAND`, `zonkscanners`, `NOSPRITES`, `wantdials` and `TTX66K`. Three findings. **The dashboard copy is 2,240 bytes and they are not the first 2,240** — `mvbllop` stores at Y and counts down to 1, so offset 2,048 is skipped and 2,240 is written, and the port read one byte past its own array until the marker caught it. **`BOX2`'s height is a data byte**: `EQUB &2C` eating its `LDX #18` is the difference between a 25-row text screen and an 18-row space view (§6.79). **And a loop that stops on a PAGE reads as one that stops on an address** — `CPX #HI(DLOC%)` — with X left where three later instructions read it (§6.80). `TTX66`, `LOOK1` and `WARP` are blocked on replacing slice 2e's screen seam, which is a phase-2 change. |
| 2026-09-04 | **Slice 3d-d-ii, and `LOOK1` and `WARP` are not in it.** Built: `BUMP2`, `REDU2`, `DOKEY`'s flight half, `SPIN`/`SPIN2` and `SIGHT`. Four findings. **The wreckage count is not random** — `TYA / TAX` is how the 6502 writes `X = Y`, the copy goes through the accumulator, and the `AND (XX0),Y` four instructions later masks the ship TYPE rather than the byte `DORND` left there; the port had it the obvious way round and the oracle disagreed on the first blueprint whose byte 0 differed (§6.74). **`REDU2`'s clamp has a hole** that produces zero on an exact match, against its own comment, counted rather than described. **`SIGHT` is two thirds canvas and game state**, not the seam §6.69 filed it as: the sprite pointers live in screen RAM. **And `TT66` is half ported** — `LOOK1` needs its pixels and the port has its text state, so `LOOK1`, `WARP` and the 252-instruction `TTX66` chain move to 3d-d-iii (§6.77). **28 mutations, 27 caught, 1 measured equivalent** — `DELTA`'s clamp is `CMP #22 / BCC / LDA #22`, so `< 22` and `<= 22` give the same 22 at 22 and nothing can tell them apart. One of the 27 was caught by NOT TERMINATING: `cnt - 2` in a loop that stops at zero runs for ever on an odd count, the suite times out, and the harness filed the strongest possible catch as a compile error. And one survivor was a real gap rather than an equivalent — the `DOCKIT` stub only WROTE `INWK+27`, so `LDA DELTA / STA INWK+27` before the call was invisible; the real routine READS that byte, and the stub EORs it now. |
| 2026-09-04 | **The source resolver was stricter than the assembler.** Four of the build's 627 includes stopped `c64_source.py`, each looking like a missing entry in `SYMBOLS`, and none of them was: BeebAsm identifiers may end in `%` and the word pattern did not; a macro argument has no value outside a call; and `ELIF` conditions were evaluated in branches BeebAsm never looks at, so `_EXECUTIVE` — defined only in the 6502SP builds — was demanded by a file whose C64 branch was already live. All 627 now resolve and the 623 the old tool could read produce byte-identical output; `--check-all` sweeps them in CI. **Strictness in the wrong place reads exactly like a gap in the model**, and it cost a `nosprites.asm` reported as zero instructions in a scoping run (§6.75). |
| 2026-09-04 | **Thirteen ledger rows had been recording findings into a void.** A slice ends by appending its result to the row that scheduled it, as a new cell; `Source-Inventory.md`'s tables have four columns and thirteen rows had grown to five, six, eight — one to twenty-three. GitHub renders a table at the header's width and drops the rest in silence, so what `LL9` cost and what its mutation sweep caught has been invisible since the day it was written, in a file that reads correctly raw. Merged into the notes column with `<br><br>` between entries, and `tools/check_docs.py` now fails CI on any row wider than its header — it caught one more, in this document's own phase-3 table. **The failure mode is the absence of a reader**: everything else this port checks has a second thing to compare against, and a design document has none until someone opens it on the web (§6.72). |
| 2026-09-04 | **Slice 3d-d-i finishes with `cntr` and `ECMOF`.** `cntr` joins the distance helpers in `FlightLoop.cpp` — flight loop part 2 is its only caller — swept exhaustively at 2,304 calls, every reading by three settings of `auto` and three of `DAMP`; 22 mutations, 22 caught. **Its last two instructions cannot run**: `.REDU DEX / BEQ BUMP` needs `BUMP`'s `INX` to wrap, which needs X = 255 arriving at `BUMP`, and `BUMP` is only ever entered with X < 128 or X = 128. The port leaves them out and a trap on `REDU` armed across the whole sweep records no hits, because "this cannot run" is a claim about every input and not one the port may make on its own authority (§6.71). `ECMOF` lands in `Dashboard.cpp` as `StopEcm`, next to `ECBLB2` rather than in phase 4's `Ecm.cpp` where the ledger files it; `ECBLB` is a toggle, so the sweep runs from a lit bulb and from a dark one, where the routine lights it. **And `tnpr1` should never have been on 3d-d-i's list** — row 69 has it built in slice 2c with 86,016 checks behind it. §6.69's pass took the 3d row's word for what was outstanding instead of the ledger's word for what was done, which is §6.12's failure with the sign reversed. |
| 2026-09-04 | **Slice 3d-d-i opens with the flight loop's distance helpers.** `MAS1`–`MAS4` in `FlightLoop.h/.cpp`: 24,576 cases for `MAS2` and 4,096 for `MAS4`, both exhaustive in the byte they OR into; 2,744 sums for `MAS3` with 1,030 of them saturating, reached both through `MA30` and through the final `BCC`; and 4,096 for `MAS1` over the coordinates that make its sixteen-bit doubling overflow, because that overflow is the whole reason for its third byte. 11 mutations, 11 caught. **`MAS2` is the third multi-entry routine this slice has met** after `DILX`'s four and `CLYNS`'s two, and the first where both entries are deliberate. **And two of 3d-d-i's six routines move out**: `FRMIS` needs phase 4's `FRS1` and `ANGRY`, and `KS1` ends `JMP MAL1`, which is a jump back into the loop rather than a call — neither can be compared to the game before 3d-d-iii exists. |
| 2026-09-04 | **Slice 3d-c: messages, the missile lock and the laser.** `MESS`/`me1`/`mes9` in `Messages.h/.cpp`, `ABORT`/`ABORT2` in `Dashboard.h/.cpp`, `LASLI`/`LASLI2`/`las` in `Lasers.h/.cpp`. 84 messages, 96 missile locks and 480 shots compared on the bitmap. **`MESS` matched first time including the `EQUB &2C` that eats the `STA YC`** rather than the load above it, and a mutation that puts 25 in the row is caught — which is what turned §6.66 from a reading into a measurement. **§6.67 fixes `CLYNS`**, which the port had implemented as `CLYNS2` under `CLYNS`'s name since slice 1c, missing the two stores every real caller wants: `CLYNS2` has no callers anywhere in the library, so the two entry points were never a choice a caller makes. **§6.68 splits `LASLI`'s three uncleared `ADC`s**: the two coordinates read `DORND`'s carry and span nine rows and nine columns where `AND #7` alone gives eight, while the third cannot carry at all, so a shot costs exactly eight heat. The port's first draft said all three were data-dependent and **the coverage assertion caught the prose rather than the arithmetic** — every byte already matched the oracle. And `ABORT2`'s `STY MSAR` stores the zero `MSBAR` ended on rather than the colour it was passed, a register side effect surviving a `JSR`. |
| 2026-09-04 | **Slice 3d-b: the dashboard.** `DIALS` 1–4 with `DIL`, `DILX` and `DIL2`, `PZW`, `MSBAR`, `ECBLB`, `ECBLB2` and `SPBLB` in `Dashboard.h/.cpp`, with `CTWOS` extracted. **`DILX` is one routine with four entry points** and three of them are byte arithmetic — `4A 4A 4A 4A` then `.DIL`, so `JSR DILX` divides by sixteen, `DILX+2` by four, `DIL-1` by two and `DIL` not at all, and all four are used; **`PZW` hides a branch in a data byte**, an `EQUB &2C` whose operands are the `LDA #RED` after it (§6.63, both checked against the assembled bytes rather than the listing). **Two defects the oracle caught, both structural.** `dec27` is not a skip but a RETURN, so the energy bars, the shields, the fuel, both temperatures, the altitude and the compass are one pass in four — §6.63 had identified the label correctly and said nothing about what branching to it does, which are different facts (§6.64). And `DLOC%` carries no left margin where `ylookup` does, so the first comparison put every dial 32 bytes right. **A §6.28 shipped since 2d is gone**: `DockedShip` held `DELTA` as `speed` while `FlightState` held it as `delta`; the struct is now `FlightStatus`, named for its readers. **Renaming it broke the Windows app with every local check green**, because the portable runner compiles no part of `Outpost/` — `tools/check_outpost.py` now asserts every `Elite::` name the app uses is declared in `GameLogic`, and says plainly that it cannot catch a signature change. **45 mutations, 44 caught, one equivalent** (`Q ^ 3` is `3 - Q` for the Q below four that reaches it). Four of the five that first survived were the dial thresholds, and they were a GAP: `DIL` compares the SHIFTED reading, so a `T1` of 14 against 13 shows only at a `DELTA` of 26 or 27. The test already compared `T1` at exit and that did not help, because `ADD` opens `STA T1` and part 2 overwrites it before `DIALS` returns — an exit-state comparison is not a substitute for a case that exercises the branch. **And mutation testing moved into a detached worktree**, because the alternative is a working tree holding deliberately broken code for half an hour at a time. |
| 2026-09-04 | **Slice 3d-a: the scanner and the compass.** `SCAN`, `COMPAS`, `DOT`, `SP1`/`SP2`, `SPS1`–`SPS4` and `TAS2` in `Scanner.h/.cpp`, with `scacol` extracted. 16,384 blips compared on the whole bitmap — every depth and height the range check admits, both signs of each, and the three shapes of stick measured from the marks rather than worked out from the inputs — plus 128 positions across, all 34 ship types, all four guards, 65,536 compass positions and 1,728 normalisations, and **44 mutations with 42 caught**. **Three findings.** `TAS2` has no `RTS`: it falls into `NORM`, so the vector it produces is scaled to a length of 96 and not the seven-bit one its instruction stream shows — and §6.11 made "does it return?" a rule two months ago, so the fix went into `c64_source.py`, which now names the next include whenever a file's last instruction is not an `RTS` or a `JMP` (§6.62). `DVID4`'s exit carry is read by `SP2` through `SPS2` in an `ADC` and an `SBC` with nothing between: the thirteenth dropped flag, always clear for a divisor of twenty, and always clear is exactly why `156 - T` had to become `155 - T` (§6.60). And closing `SCAN`'s two seams deleted a class, gave `MoveShip` a `Canvas`, and replaced two call counts with two screen comparisons — one of which was blank on both sides until the fixture put the ship where `SCAN` would draw it, §6.39's shape again (§6.61). **Both survivors are measured equivalents rather than gaps**, and they are the same kind of thing seen twice: `TAS2`'s `BCC TAL2` is a `JMP` spelled as a branch, because the ORed operands it is compared against dominate it bit for bit; and `SP2`'s `ADC #195` cannot see a carry that is always clear, while the `SBC T` twelve instructions later can, because there a clear carry borrows. |
| 2026-09-04 | **Slice 3d opened with its §6.12 pass, before any of it was written, and the row turned out to be a slice and a half.** 45 source files and 210 external labels against 3c's 36, so it is now split into 3d-a … 3d-e with the units ordered so each rests on the last. **The row names sixteen files this build does not have** — `mainloop_part_N` where the C64 includes `main_flight_loop_part_N_of_16`, and `spblb` where it takes `spblb-dobulb.asm` — which is cosmetic in a row and not in a script: a pass driven by the row's own names finds nothing for seventeen of the forty-five files and reports a much smaller slice than there is. **Twelve prerequisites the row does not name**, among them `cntr`, deferred out of phase 1 with row 94's seven and never picked back up — the second time that row has stranded something. **`SETL1` is not game logic**: `SEI / STA L1M / ... / CLI`, self-modifying code inside a raster interrupt handler, and a pass that only asks "is this ported?" would have scheduled it. **And `SCAN` sits behind two seams with two different signatures** — `ShipEffects::UpdateScanner(ShipBlock&)` and `BubbleEffects::ScanShip(const ShipBlock&, std::uint8_t)` — disagreeing about whether the type is an argument and whether the block is mutable. §6.28's shape a fifth time and the first in the SEAMS rather than the state, which is where a port stops checking. |
| 2026-09-04 | **The bubble, and slice 3c is complete.** `KILLSHP` with `KS1`–`KS4`, `SOS1` and `SOLAR`. `KILLSHP` does not release heap space, it RELOCATES: every ship above the dead one moves down a slot and its line heap down by the dead ship's size, so the region stays packed with no free list. 136 kills compared on the slot list, all ten blocks, the type counts, the junk count, `SLSP` and the **whole line heap**, plus `TP`, `TALLY` and three seams; 5 systems for `SOLAR`. **§6.58 has three findings, each one a different way of being wrong.** `SSPR` is `MANY+SST` — one byte with two names, in the ORIGINAL this time rather than introduced by the port, which is why nothing ever sets `SSPR` when a station is created. `SOLAR` has no `RTS`: it falls into `NWSTARS`, `nWq`, `WPSHPS` and `FLFLLS`, so **five ledger rows are one fall-through** and arriving in a system fills the stardust and clears the ships as part of the same call — caught because the generator's state moved on the oracle side and not the port's. And `LSR FIST / JSR ZINF / ... / ADC #3` means **the planet's distance depends on whether the player's bounty was odd**: the twelfth uncleared flag, and the first to be a shift rather than a call. The checklist is now two lines: before porting a routine, read the instruction after every `JSR`, and read the instruction after every shift. |
| 2026-09-04 | **`ZINF`, and the four-row chain `NWSTARS` heads.** `NWSTARS` is two instructions falling into `nWq`, which falls into `WPSHPS`, which falls into `FLFLLS` — four rows in the ledger and one routine in the build, which is why §6.45 flagged it when scoping rather than after. 60 resets over five fleets, three view types, both particle counts and both entry carries, compared on the whole canvas, all three stardust arrays, every slot's state byte, both line heaps, the generator's four bytes and the scanner seam. **§6.57 is the finding**, and it is about a test rather than a routine. `nWq` calls `DORND` three times and `PIXEL2` once per speck with no `CLC` anywhere, so each speck's first random byte runs on the carry the PREVIOUS speck's plot left — the eleventh dropped flag, and `PIXEL`'s exit carry turns out to be exactly `ZZ >= 80`. Verifying that model meant looking at phase 1's sweep for `PIXEL2`, which covers **all 65,536 coordinate pairs** and pinned `ZZ` at 255 for every one of them: genuinely exhaustive in x and y, and constant in the byte the routine branches on, so two of its three distance cases had never run. One line fixed it. The question to ask of any sweep is not whether it is exhaustive but **exhaustive in which dimension** — a sweep over what a routine is ABOUT can still be constant in what it BRANCHES on. |
| 2026-09-04 | **The sun, and slice 3c's drawing is complete.** `SUN`'s four parts, and it is the cleverest routine in the drawing code: not a filled circle but a stack of horizontal lines whose half-widths are `sqrt(K^2 - v^2)` with a few random bits for a ragged edge, and it **never erases and redraws** — for each row it holds last frame's half-width and this frame's, clips the first against last frame's centre and the second against this frame's, and draws only the two slivers that differ. That is why `SUNX` exists, and why a test that calls it once sees none of it: 80 frames over 20 drifts, four consecutive frames each. 24 of 27 mutations caught. **Three defects** (§6.55), all in the walk rather than the arithmetic: `DEC V / BNE PLFL / DEC V+1` decrements UNCONDITIONALLY and the branch only decides whether the high byte follows, which is how the walk flips from coming in to going out; `PLF6`'s `DEY / BEQ PLF8` leaves through the routine's TAIL rather than through part 4, so two exits from one loop go to different places; and `LL5` leaves a carry that `SUN`'s `JSR DORND` reads, so the sun's ragged edge is seeded by the last bit out of its own square root. **The tenth dropped flag**, and the exhaustive sweep over all 65,536 radicands verified the widened model for one line. **§6.56**: the three surviving mutations are equivalents and two are facts about the shipped code — crossing the two ends over cannot change the picture under EOR (`[a,b)⊕[c,d) = [a,c)⊕[b,d)` for any four coordinates), so it exists purely to keep each `HLOIN` short, which is invisible to a pixel comparison and is the routine's whole point; and one `STA LSO,Y` in `PLF11` is dead, because `EDGES` has already written that zero on the path that reaches it. |
| 2026-09-04 | **The planet: `PLANET`, `PL9` 1-3, `PLS1`-`PLS5` and `PLS22`.** Elite's planets have two looks and one bit of the system's tech level picks between them — type 128 gets MERIDIANS, two great circles seen at whatever angle the planet is turned to, and type 130 gets a CRATER offset along the nose vector. 408 planets compared on the whole canvas, both line heaps and eleven zero-page bytes, with the sun behind a counted seam. 34 of 34 mutations caught. **Three defects** (§6.53). `PLS22` decides which half of a turn it is on with `CMP #33 / LDA #0 / ROR A`, and `ROR` COLLECTS the carry rather than clearing it — the port had the sign inverted and every meridian's second axis came out on the wrong side. `ADD`'s exit carry is the ninth dropped flag: none of its three exits clears it and `PLS22` reads it twice, once to place a meridian and once to hand to `BLINE`. And `PLS3` hands back a STEPPED index, because its `STX U` comes after a `JSR PLS1` that ends with two `INX`s — `PL26` calls it twice without touching X and gets two different axes, which is the whole point, and the port passed the same axis twice and drew every crater in the wrong place. Nothing at the call site says so but the ABSENCE of an `LDX`. **§6.52 is the finding worth reading**: the first version of this sweep compared 54 planets byte for byte and passed while `PL9`'s parts 2 and 3 never executed once. Every placement was too close — z is TWENTY-FOUR bits and the radius is `96*256*256/z`, so the markings only appear above z = 24576, and the nearest case in the sweep was 512. **The third time in this project that a whole-canvas comparison has passed while the routine under test did nothing** (§6.36, §6.39), caught by a counter that exists because of the other two. **§6.54**: nine of the ten surviving mutations were one mistake — every orientation vector in the sweep was positive and small, so `PLS1`'s sign mask, its saturation and its returned sign byte were all unreachable. A surviving mutation is a question about the test data far more often than about the code. |
| 2026-09-04 | **The ball: `BLINE`, `CIRCLE2` and `CIRCLE`, so the planet can be drawn as well as erased.** 441 circles and 216 segments compared against the shipped game on the whole canvas, both heaps, `K5`, `K6`, `STP`, `FLAG` and `CNT` — the heap as much as the picture, because a circle drawn correctly onto a wrong heap looks right once and leaves the screen dirty for ever after. 24 of 29 mutations caught and **all five survivors proved equivalent** (§6.51), which is the opposite result to the same afternoon's §6.48 and rests on one fact: `CNT` is always a multiple of 2, 4 or 8, so it is never 17 mod 64, never 65, and at 32 it indexes `SNE[0]`, which is zero. **§6.50**: `FMLTU2`'s exit carry is the eighth dropped flag, and it is a PASS-THROUGH rather than an output — `FMLTU`'s two zero exits (`BEQ MU3`, `BEQ MU3again`) touch no flag, so the caller's own carry comes straight back. That could have been a five-caller change and was not, because the returned BYTE is zero on both those exits whatever the flag was: only a caller that reads the carry can tell, and exactly two do. The generalisable check is to read the instruction after each `JSR` before deciding how far a flag change reaches. **§6.49**: `CNT` is one byte with FOURTEEN users and the port had it inside `GeometryWorkspace`, which is `LL9`'s, because `LL9` needed it first. Leaving it there would have been unobservable — every user initialises before reading — but unlike `XX2` against `K3` the merge was one field, so it moved to `MathWorkspace` and the 1,881-ship sweep verified it immediately. When the merge is cheap, do it and stop reasoning; when it is expensive, measure the bound and write the measurement down. |
| 2026-09-04 | **The planet and sun line heaps, and two defects in code that shipped two slices ago.** `LSO`/`LSX`, `LSX2`/`LSY2` and `LSP` with `EDGES`, `HLOIN2`, `FLFLLS`, `WP1`, `WPLS`, `WPLS2`, `PL2`, `CHKON` and `PL21` — the erase-by-EOR machinery the whole planet and sun rest on. Both heap sizes are read off the assembled layout (`LSO` 1408 to 1607, `LSX2` and `LSY2` 256 bytes each and adjacent), and the ball's two arrays are modelled as one block because `BLINE` reads `LSY2-1,Y`. 27 of 27 mutations caught. **§6.47 is the finding**: `WPLS2` is the first thing in the project to draw a few hundred arbitrary lines in a row, and it disagreed with the game by ONE PIXEL on one line of 255 — not a `WPLS2` defect but a `LOIN` one, live since slice 1d-a. Its downward setup ends `SBC #247` and reaches its loop through twelve instructions that touch no flag, so the accumulator's first step runs on that borrow; the port started it clear and was right whenever the screen pointer had not reached 248, which is eight lines in nine. The seventh uncleared 6502 flag to be the defect and the third in that one routine. What makes it worth a section is why 3,528 chosen lines missed it: **the sweep was chosen to reach every branch, and this routine's state is a carry chain whose behaviour depends on phase, not on branch.** A four-thousand-line sample now runs beside the grid; the mutation test confirms the grid alone still misses the defect and the sample catches it, and says plainly that neither substitutes for the other. **§6.46**: `SWAP` is one byte at 1780 with two writers, `LL145` and `LOIN`, and two readers that do not pair up with them — `BLINE` reads the clipper's and `WPLS2` reads `LOIN`'s. The port had modelled it as the clipper's return value and `LOIN` kept a local copy, so `WPLS2` was asking a question the port could not answer. §6.28's shape for the third time: the port models a byte as a routine's output and the game models it as a place. **§6.48**: three mutations survived and none was equivalent — `WPLS`'s guard is a `BMI` and the sweep only used 0 and 255; `CHKON` has four exits on bit 7 of a sixteen-bit sum and every coordinate tried fitted in nine bits; and the third was a mutation I wrote badly. A survivor is a question, not a verdict. |
| 2026-09-04 | **The stardust is built — three views, three routines, and slice 3c is a third done.** The six parallel arrays, the seven wrappers §6.40 found missing from the row, `PIX1` (which §6.41 found marked ported and absent), `FLIP`, `STARS1`, `STARS2`, `STARS6` and the dispatcher. 280 frames per view compared against the shipped game on the whole canvas, all six arrays, the generator's four state bytes and — for the side views — seven flight bytes and `newzp`; four CONSECUTIVE frames per case, because a mover's output is the next frame's input (§6.33). Thirty of thirty real mutations caught. **§6.44 is the finding**: `STARS1` and `STARS6` share about eighty of their hundred instructions and are still two routines, not one with a sign — the coordinates are computed in the opposite order, the roll's two sign bytes are swapped, and the pitch multiplies by the negated x where the front view squares its own scale factor. Only the subtractions and the growing distance follow from "backwards". With them, the `DEX` in `STARS`'s six-instruction dispatch: `STARS2` is entered with the view already decremented, so its `CPX #2` compares against the LEFT view, and a port that reads the two routines apart comes out mirrored. **§6.42**: `LL38`'s and `MLU1`/`MLU2`'s exit carries were being dropped, the fifth and sixth time an uncleared 6502 flag has been the defect — and each was verified for the price of one line, because phase 1's sweeps for those routines are exhaustive and already run every input. **§6.43**: two mutations survived, and rather than being written off, the property that makes them equivalent — `MULTU` leaves carry clear for every one of the 65,536 input pairs, because `LSR P` shifts a zero in and eight `ROR P`s walk it out — is now asserted on the ORACLE. A mutation that cannot change behaviour is not a hole in a test. Two further mutations were real gaps: one field of twelve specks never lands on `STARS2`'s `room == newzp`, so the sweep now runs five. |
| 2026-09-04 | **Slice 3c opened with its §6.12 pass, before any of it was written.** Its complete external surface is thirty-six labels; twenty-four are ported, five belong to other slices, and **twelve are prerequisites that do not exist and the row names one of them**. Seven come from row 94 — `MLS1`, `MLS2`, `MLU1`, `MUT1`, `MUT2`, `DV41`, `DV42`, which `STARS1`, `STARS2` and `STARS6` multiply and divide through — and all seven were deferred out of phase 1 with the same sentence about state that did not exist yet. That state arrived with 3a. **A deferral reason is not a schedule**, and nothing put them back on one; it is the same shape as the six homes filed by adjacency, one level up. The other five are `HLOIN2` (row 116, which `SUN` and `WPLS` draw through), `ZINF` (row 44, which `SOLAR` clears a block with) and `BLINE`/`CIRCLE`/`CIRCLE2` (row 117). The useful half: the closure stops there — what those twelve reach is either in 3c's row already or ported — so the slice is thirteen routines wider than its row and not one routine deeper. **And the pass found a ✅ that was not true** (§6.41): `PIX1` sits in a row marked ported and does not exist in `GameLogic/`. It needs `SYL`, which did not exist in slice 1d-a, so it was skipped — and nothing noticed, because `tools/inventory.py` reconciles include files against rows and the ✅ is one mark for all of a row's labels. `STARS1`, `STARS2` and `STARS6` all call it. Four more labels with their own include files sit in ✅ rows with no marker — `setxc`, `setyc` and their `doxc`/`doyc` companions, and `tnpr1` — and are recorded as unconfirmed rather than claimed either way. |
| 2026-09-04 | **Slice 3b is complete.** `PLUT` was the last of it, and it goes to `ShipMove.cpp` rather than to the drawing where the ledger files it: it is a transform of `INWK` in the same family as `MVS4` and `MVS5`, the source's own category for it is Flight, and its caller is the main flight loop. That is the **sixth** home this ledger got wrong, all for the same reason — a routine filed by what it sits NEXT TO in the source rather than by what it does. `DVID3B2`, `PLS6`, `PL2`, `LL51`, `LL61` and now `PLUT`. Worth saying plainly for phase 4's planning: the ledger is a reliable coverage list and an unreliable map, and every slice so far has had to correct it before writing a line. |
| 2026-09-04 | **`LL9` is ported — the hardest routine in Elite, and slice 3b is complete but for `PLUT`.** 1,683 ships compared against the shipped game on the entire canvas, the entire line heap, `INWK`, the two bytes written into `K%` through `INF`, the face flags and every projected vertex. It matched first time apart from the `XX2`/`K3` aliasing §6.37 predicted — and that is settled by a measurement, not an argument: **no blueprint of the thirty-three names a face index that `EE29` or `EE30` does not write**, so a stale flag cannot be read at all. **§6.39 is the finding worth keeping**: the first version of the test passed 396 whole-canvas comparisons while every orientation vector it supplied was exactly zero, because Elite stores them as (lo, hi) pairs with the magnitude in the HIGH byte. It drew 17 wireframes where the corrected data draws 940. A whole-canvas comparison against the shipped game is not evidence that the routine under test did anything. Mutation testing then found three more paths unreached and the placements were extended until they were, with two of the three shown reachable by counting the blueprints first. |
| 2026-09-04 | **The line clipper, and `LL51`** — `LL145`/`LL147`, `LL118`, `LL120`, `LL123`, `LL129` and the dot products `LL9`'s geometry runs on. 32,768 comparisons in three suites; one defect, in `LL122`, whose entry shift sits outside its loop so a multiplier of zero still gets two shifts (§6.38). Two ledger findings with it. **`LL145` reads `dontclip`**, which `TT23` sets to 199 so the short-range chart can use the whole screen — §6.12's pattern pointing OUTWARD for the first time, at a slice-2 routine that is already ported and does not write it yet. And **`XX13`'s documented values are the BBC's**: 0, 95 and 191 in the header, 0, 143 and 71 in this build, because `Y` is 72 rather than 96. The mechanism survives — bit 7 is set in 143 as it is in 191 — so a port written from the header would branch correctly and clamp to the wrong row. The upstream prose is the authority for what a value means; the resolved source is the authority for the value. |
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
