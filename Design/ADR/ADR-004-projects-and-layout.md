# ADR-004 — Projects and Layout

**Status:** Accepted · 2026-09-02 (§1 settled by owner ruling: own codebase, nothing lifted)
**Depends on:** ADR-001; the conventions in `.clang-format`, `.clang-tidy`, `.editorconfig`
**Feeds:** slice 0c; the *Home* column of [Source-Inventory.md](../Source-Inventory.md)

## Context

The solution began as `NeuronCore` (a foundation library with two headers) and `Outpost` (an
empty application template). The port needs: somewhere for the game that has no platform in it,
so it can be tested against the oracle and hashed for replay; somewhere for the window, GPU and
audio; an executable that composes them; and tests.

Sibling repositories in this family (`Outpost.Frontier`, `Outpost.Warzone`) already contain a
window, a D3D12 device and an XAudio2 device under a `NeuronClient` project, and an earlier
draft of this ADR proposed lifting them so that engine code could move between trees without a
rename pass.

**Owner ruling, 2026-09-02: ignore the sibling repositories and build our own codebase.** No
file is lifted. The shared-engine property is not a goal for this project, and inheriting a
project whose shape was decided for a different game costs more than writing the eight or so
files this one actually needs.

## Decision

### §1 Projects

```
NeuronCore.lib            foundation (exists): the shared precompiled-header content and Debug.h.
GameLogic.lib             THE PORT.  namespace Elite.  Deterministic and platform-free: no window, no GPU,
                          no audio device, no clock, no file system, no float.  Input: InputFrame.
                          Output: Canvas (320x200 indexed) + SoundEvents.  Depends on NeuronCore only.
Outpost.exe               composition root AND presentation: Main, AppConfig, Window, GpuDevice,
                          CanvasPresenter, SidSynth, KeyMap, SaveStore.  Written here, from scratch.
Tests/GameLogicTests.dll  MSVC CppUnitTest: Cpu6502, the oracle fixture, the suites.
```

**Presentation lives in the executable**, not in a separate engine library. It is roughly ten
files, none of which is unit-tested (a window and a swap chain are verified by looking at them),
and a library boundary drawn around code with exactly one consumer is ceremony. If it grows
past the point where that is comfortable, splitting it out later is a project file and a set of
`#include` lines, not a redesign — the seams that matter (`Canvas`, `SoundEvent`, `InputFrame`)
are between the game and the presentation, and they exist from day one.

**`GameLogic` is the name** because this repository's `.clang-tidy` header filter already names
it, and because the content is the game's logic. The namespace is `Elite`.

Dependencies, hard: `GameLogic` → `NeuronCore` only. `Outpost` → both. Tests → the library they
test. Nothing references the executable. x64 is the platform that matters; Win32 and ARM64
configurations exist and are unmaintained, and the ARM64 solution platform maps to the x64
project configuration for the libraries.

### §2 Layout rules

- **Flat project directories.** All of a project's `.h`/`.cpp` sit directly in its folder;
  grouping lives in `.vcxproj.filters` only. `Tests/` holds one folder per test project, which
  is a solution-level split rather than a code subdirectory.
- **File names PascalCase and unique repo-wide**, including against the CRT, the STL and the
  Windows SDK, case-insensitively. This is why the port uses `Arith.h` rather than `Math.h`,
  `Rng.h` rather than `Random.h`, and `Lines.cpp` rather than `Line.h`.
- **Includes are unqualified.** Each project lists the roots it is entitled to.
- **Every added file goes into both** the `.vcxproj` and the `.filters`, in the same commit.
- `pch.h` per project, including `NeuronCore.h`. For `GameLogic` that is an include, not a
  licence: it must not *call* a platform API (AGENTS.md §5).

### §3 The original source and the generated data

- `MasterFile/` stays exactly as it is: the 13 masters, reference only, never compiled, never on
  an include path. It is the index into the upstream tree and what `tools/inventory.py` parses.
- `Upstream/elite-source-code-library` is the upstream tree, a **submodule** pinned at
  `aa3f7ee` (ADR-001 §5, corrected 2026-09-03). **Never edited, never reformatted, never
  compiled by this solution.** A fresh clone has nothing there until
  `git submodule update --init` runs, and until it does `tools/inventory.py --check-includes`
  reports 0/712 and nothing from slice 1a onward can be built.
- Generated data files (`ShipBlueprintData.cpp`, `TokenTables.cpp`, `SineTable.cpp`,
  `ArctanTable.cpp`, `LogTables.cpp`, `SoundTables.cpp`, `TuneData.cpp`, `Font.cpp`,
  `DashboardImage.cpp`, `SpriteData.cpp`, `Palette.cpp`, `CommanderData.cpp`) are **checked in**,
  each carrying a header naming its generator, the upstream commit and the source file. A test
  compares each against the oracle's loaded memory (slice 1a) so a stale regeneration cannot
  pass silently.

### §4 Traceability

Every ported function's declaration carries `// 6502: <LABEL>` (AGENTS.md R7).
`tools/inventory.py` collects those markers and reconciles them with the source inventory and
the masters' include list. A marker may name something that is not its own file — a second entry
point such as `DORND2`, or a workspace field such as `RAND` — and the tool reports those
separately rather than treating them as errors.

### §5 Tooling

`tools/`, Python 3, no third-party packages:

| Script | Job | Status |
|---|---|---|
| `inventory.py` | `--check-includes` resolves every master include against `Upstream/`; the default report reconciles the ledger with the `// 6502:` markers; `--strict` fails on an unaccounted file | **Built** (slice 0c) |
| `labels.py` | Assembles the C64 variant and normalises BeebAsm's label dump and load addresses into `Design/Reference/Labels.txt` and `Binaries.txt` | **Built** (slice 0b-a) |
| `extract_tables.py` | upstream `.asm` and `C.FONT.bin` → the generated `.cpp` data | slice 1a |
| `golden_diff.py` | expected versus actual canvas → diff image | slice 1d |
| `check_gamelogic.py` | Fails if `GameLogic` grows a clock, randomness, a float, file or registry access, or a Win32 call. Strips comments and string literals first, and `--self-test` checks the checker | **Built** (slice 0f) |

## Consequences

- Four projects, not five: there is no engine-presentation library and no dependency on any
  sibling repository. Everything in the tree was written for this game.
- The window, device, swap chain, presenter and synthesiser are work this project has to do
  rather than inherit. That is the accepted cost of the ruling, and it is bounded: the game
  needs one texture, one full-screen quad and one audio source voice.
