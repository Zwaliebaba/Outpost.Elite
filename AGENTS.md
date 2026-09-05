# Outpost: Elite — how to work here

Conformance rules for anyone, human or agent, writing code in this repository.

Two things to read before you start:

1. **This file** — naming, layout, build, and the working rules.
2. **[Design/](Design/)** — the architecture decisions. [Design/README.md](Design/README.md)
   indexes them; they are normative, and where this file and an ADR disagree, the ADR wins on
   *what* to build and this file wins on *how it is spelled*.

**What this repository is.** A C++ port of Commodore 64 Elite, from the annotated 6502 source
in [MasterFile/](MasterFile/) and [Upstream/](Upstream/). The port is faithful first and
modernised later ([ADR-001](Design/ADR/ADR-001-scope-and-fidelity.md)); the assembled original
is the test oracle ([ADR-003](Design/ADR/ADR-003-verification.md)).

**This is a greenfield tree. Nothing is grandfathered.** The rules below apply to every line
from the first one. The one exception is the vendored upstream assembler source, which is not
ours and is never edited.

---

## 1. Naming convention (normative — no exceptions)

[`.clang-tidy`](.clang-tidy) is the machine-readable statement of this table and the single
source of truth for the option values. This document does not repeat them, so there is nothing
to drift.

| Kind | Convention | Example |
|---|---|---|
| Type (class, struct, enum, concept, alias) | `PascalCase` | `ShipSlot`, `SignMag24` |
| Function, method | `PascalCase` | `NextRandom()`, `DrawLine()` |
| Member variable | `m_camelCase` | `m_canvas` |
| Static member | `sm_camelCase` | `sm_instance` |
| Global | `g_camelCase` | `g_stopRequested` |
| Parameter | `_camelCase` | `_shipIndex`, `_seed` |
| Local | `camelCase` | `lineCount` |
| Constant (`constexpr`, `static constexpr`) | `UPPER_CASE` | `MAX_SHIPS`, `CANVAS_WIDTH` |
| Enumerator | `PascalCase` | `Coriolis`, `EscapePod` |
| Macro | `SCREAMING_SNAKE` | `ASSERT`, `DEBUG_ASSERT` |
| Namespace | `PascalCase` | `Neuron`, `Elite` |
| File | `PascalCase.cpp` / `.h` | `ShipMove.cpp` |

### The rules behind the table

**R1 — The leading underscore on parameters is deliberate.** It is legal C++: the reserved
forms are `_Uppercase`, anything containing `__`, and `_lowercase` **at global scope**. A
parameter is never at global scope, so `_seed` is safe. Never introduce a reserved form — no
`_Impl`, no `__helper`, no file-scope `_cache` (use `g_cache` in an anonymous namespace).

**R2 — A type name carries no prefix, and that includes abstract ones.** No `IFoo`, `CFoo`,
`SFoo`, `EFoo`, `FooBase`, `FooImpl`, or `_t` suffixes. Name the concept and let the concrete
types say what they are.

**R3 — Compile-time constants are `UPPER_CASE`**; enumerators stay `PascalCase`. `sm_` is
reserved for *mutable* statics, which are rare and must document their thread-safety.

**R4 — Acronyms capitalize as words**: `SidSynth`, `VicPalette`, `RngState` — never
`SIDSynth`. Identifiers from an external SDK keep that SDK's spelling (`ID3D12Device`,
`HRESULT`, `IXAudio2`) and are never renamed to fit.

**R5 — `m_` marks encapsulated state, not every field.** A `class` with invariants prefixes
private members `m_`. A public aggregate — a config struct, a POD passed to the presenter —
uses plain `camelCase` fields so brace initialization reads naturally.

**R6 — Units and spaces belong in names; types do not.** `speedPerStep`, `angleTurns`,
`xCanvas` are encouraged. Never encode the type: no `iCount`, `pShip`, `strName`.

### R7 — The port carries its origin (this repository's own rule)

**Every function ported from 6502 names its original label on the declaration:**

```cpp
/// 6502: DORND — generate the next random number.
[[nodiscard]] RngResult NextRandom() noexcept;
```

`tools/inventory.py` collects these `// 6502:` markers and reconciles them with
[Design/Source-Inventory.md](Design/Source-Inventory.md) and the master files' include list.
Where several 6502 entry points merged into one function, name them all. The ledger names a
multi-part routine once (`mveit_part_1_of_9` … `part_9_of_9`) and a numbered family as a range
(`bdro1`–`bdro15`); `inventory.py` expands both, and `--strict` -- which CI runs -- fails on any
master-level include no row names (plan §6.120). Where the upstream
commentary explains a trick, leave a one-line pointer to the label rather than reproducing the
essay — the source is vendored and a reader can go and read it.

---

## 2. Repository map

| Path | What it is | May you edit it? |
|---|---|---|
| `NeuronCore/` | Foundation static library: the shared precompiled-header content and `Debug.h`'s assert/trace family. No game semantics. | Yes |
| `GameLogic/` | **The port.** Namespace `Elite`. Deterministic, no window, no GPU, no audio device, no clock, no file system, no float. Draws into an in-memory canvas and emits sound events. | Yes |
| `Outpost/` | The executable: composition root, window, D3D12 canvas presenter, SID synthesiser, key map, save store. The only project that knows both the game and the platform. | Yes |
| `Tests/GameLogicTests/` | MSVC CppUnitTest DLL: the 6502 interpreter, the oracle fixture, and the suites. | Yes |
| `Tests/PortableRunner/` | The same suite under g++: three shim headers, a generator and a shell script. Compiles the test files unmodified — see its own README. | Yes |
| `Design/` | ADRs, the conversion plan, the source inventory, the risk register. | Yes — see §7 |
| `MasterFile/` | The 13 annotated master `.asm` files. **Reference only** — never compiled, never on an include path. | **No** |
| `Upstream/` | The vendored upstream source tree, pinned by commit. **Not ours.** | **No — never edit, never reformat** |
| `tools/` | Repository checkers and the data extractors. | Yes |
| `x64/`, `.vs/`, `*.user`, `Generated Files/` | Build and IDE output. | **No — and never commit them** |

**Project dependencies, and the edges run one way:**

```
NeuronCore.lib            the foundation everything builds on
├── GameLogic.lib         references NeuronCore only
└── Outpost.exe           references GameLogic and NeuronCore
GameLogicTests.dll        references GameLogic and NeuronCore
```

`GameLogic` never references `Outpost`. Nothing references the executable.

---

## 3. Files and layout

- **Flat project directories.** All of a project's `.h`/`.cpp` sit directly in its folder. No
  code subdirectories — grouping lives in `.vcxproj.filters` only. (`Tests/` holds one folder
  per test project; that is a solution-level split, not a code subdirectory.)
- **File names are unique repo-wide**, and also unique against the CRT, the STL and the
  Windows SDK, **case-insensitively**. A header named `Math.h` or `Random.h` shadows a standard
  one for every translation unit that can see the folder, and the errors land inside the STL
  with nothing pointing at you. This is why the port uses `Arith.h` and `Rng.h`.
- **Includes are unqualified**: `#include "Rng.h"`. Each project lists the roots it is
  entitled to as `$(SolutionDir)<Project>` include paths.
- **Every added, removed or renamed file updates both** the `.vcxproj` and the
  `.vcxproj.filters` of its project, in the same commit.

---

## 4. Layout and formatting

[`.clang-format`](.clang-format) is the authority; [`.editorconfig`](.editorconfig) repeats only
what an editor needs before the first save. The shape: **Allman braces, 2-space indent, 140
columns, no tabs, `namespace` contents indented, pointer binds left** (`std::uint8_t* _dst`).

Include order is **not** sorted automatically and is grouped by hand: `pch.h` first, then
Windows headers, then SDK headers, then project headers, then the standard library.

Format the lines you write. Do not reformat files you are only passing through, and **never
reformat anything under `Upstream/` or `MasterFile/`.**

---

## 5. C++ rules for this codebase

- **C++20** (`stdcpp20`), MSVC v145, `ConformanceMode` on, x64 is the platform that matters.
  Do not turn conformance off to make something compile.
- **`GameLogic` is deterministic and platform-free.** No wall clock, no OS entropy, no
  `<chrono>`, no `<random>`, no `rand()`, no file or registry access, no Win32 calls, and
  **no `float` or `double` anywhere** ([ADR-002](Design/ADR/ADR-002-numeric-model.md)). The
  replay-equality suite is the gate; `tools/check_gamelogic.py` is the backstop.
- **8-bit semantics are preserved exactly.** Same widths, same wraparound, same lookup tables.
  Where the original truncates to a byte, truncate. Widen only inside a helper whose result is
  narrowed the same way the original narrowed it. Tables are extracted data, never recomputed
  with `std::sin`.
- **COM lifetimes are RAII through `winrt::com_ptr`**, not `Microsoft::WRL::ComPtr` and never
  raw `Release()`. Create with `Thing(IID_PPV_ARGS(thing.put()))`, query with
  `thing.try_as<IOther>()`. This is a COM-helper sanction only — C++/WinRT is not used as a UI
  or async framework, and there is no XAML in this application
  ([ADR-005](Design/ADR/ADR-005-presentation.md) §5).
- **An `HRESULT` that must succeed goes through `winrt::check_hresult`.** Exceptions:
  capability probes are control flow; shutdown paths log instead of throwing. The thrown error
  is caught once, at the composition root. Assertions are `Debug.h`'s `ASSERT` family.
- **No external libraries without the owner's explicit approval.** Pre-approved: the Windows
  SDK (Win32, D3D12/DXGI, XAudio2) and C++/WinRT as above. The 6502 interpreter and the SID
  synthesiser are **written here** rather than taken from an emulator project — present the
  case and **stop** if you think a third-party library is justified.
- **Errors that are the user's fault are diagnostics, not crashes.** Anything reading content
  or configuration reports what was wrong and fails closed; it never asserts on bad input.
- **Two words and one container the Windows toolchain owns.** `near` and `far` are still macros
  after `<windows.h>` — `const bool near = ...` compiles as a declaration with no name — and it has
  cost a CI leg once and two compile errors since (§6.110). And `std::vector<bool>` is bit-packed:
  `operator[]` returns a proxy, and MSVC's `Assert::AreEqual` static-asserts that it has no
  `ToString` for one while g++ compiles it without a word (§6.116). Store `std::uint8_t`. Both
  reach CI through the fast leg green, so neither is a warning you get locally.

---

## 6. Build and verify

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    Outpost.slnx /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal `
    /flp:logfile=build.log`;errorsonly
```

Run MSBuild through the **PowerShell** tool, not Bash: git-bash mangles the `/p:` switches.
`build.log` empty means clean; do not judge success from the tail of stdout, because parallel
warnings scroll the real error away.

Tests are an MSVC CppUnitTest DLL:

```powershell
vstest.console.exe x64\Debug\GameLogicTests.dll
```

**Oracle tests need the reference binaries** and report *skipped — oracle absent* without them
(ADR-003 §1). That is expected on a fresh machine until BeebAsm has assembled the upstream
tree; it is not a passing suite. Tests that hand-assemble the routine under test need nothing
and always run.

Repository checks:

**Run them with `python tools/check_all.py`**, which runs all eleven in CI's order and takes no
arguments. Do not retype the list into a loop: that is how a push went red on 2026-09-05 with the
one check that would have caught it left out (§6.127). What it runs:

```
python tools/inventory.py --check-includes    # every master INCLUDE resolves in Upstream/
python tools/inventory.py --strict            # coverage ledger: every master-level include has a row
python tools/check_projects.py                # .vcxproj paths resolve; nothing on disk is unlisted; pch.h is every source's first line
python tools/check_outpost.py                 # Outpost/ still calls GameLogic names, with the right arity
python tools/check_docs.py                    # no table row is wider than its header
python tools/check_counts.py                  # every <!--count:name--> number in a document matches the tree
python tools/check_gamelogic.py --self-test   # the determinism guard still detects violations
python tools/mutate.py --check                # every recorded mutant still applies to the code it names
python tools/c64_source.py --check-all        # the source resolver reads every file the build assembles
python tools/extract_tables.py --check        # the generated tables match the assembled binaries (needs the oracle)
```

**A NUMBER IN A DOCUMENT IS A CLAIM, AND `check_counts.py` IS THE TEST BEHIND IT.** Prose about a
decision ages well; a number beside it ages badly and in silence (§6.145). So a number that
describes the tree AS IT IS carries a marker — `the suite is <!--count:tests-->349 tests` — and the
check reads the tree and compares. Numbers in the plan's journal entries are HISTORY, carry no
marker and are never touched: "321 tests" was true the day it was written and must stay. Before
writing a new live number, `python tools/check_counts.py --list` says what the tree holds.

**`check_docs.py` exists because a Markdown table drops what it cannot fit.** GitHub renders a
table with the header's number of columns and discards every cell past it without a word, so a
row that has grown an extra `| ... |` on the end reads perfectly in the raw file and is missing
its last paragraphs on the web. Thirteen rows of `Source-Inventory.md` had done that, one of them
holding nineteen invisible cells (§6.72). **Append a slice's result INSIDE the notes column, with
`<br><br>` between entries, never as a new cell.**

**`check_outpost.py` exists because the portable runner compiles no part of `Outpost/`.** It is
Win32 and DirectX 12, so a hosted Linux runner cannot build it -- and that leaves a whole
executable outside every check runnable there. Renaming a `GameLogic` type breaks the app with the
Linux suite still green, which is how `DockedShip` becoming `FlightStatus` reached the Windows job.
The check asserts the names still resolve AND that every `Elite::` call passes as many arguments
as the declaration takes -- the second half added after the name check alone let a fifth parameter
on `ClearMessageRows` reach the Windows job, the second break of the same afternoon through the
same hole. It still cannot check parameter TYPES at an unchanged arity, and only building the app
can.

**Add a new file to its `.vcxproj` AND its `.vcxproj.filters`.** The portable runner globs the
directory and will happily compile a file no project names; MSVC will not, so the two builds
quietly test different things. `check_projects.py` fails on that, on a path that does not resolve
(`Include` is relative to the PROJECT, not to the repository), and on a filters file that has
drifted from its project.

**Mutation-test a finished unit, and RECORD THE MUTANTS.** A slice is not done until each of its
decisions has been shown to matter: change one constant, one comparison or one flag in the ported
source, run the suite, and a mutation that nothing catches is either a gap in the tests or an
equivalent worth measuring and recording.

**The mutants go in `tools/mutants.json` and the run is `tools/mutate.py`.** Do not do this by
hand any more. A hand-edited mutant is thrown away when the run ends, which made every tally in
this corpus an assertion nobody could re-check — Risk R13, and §6.119 is the demonstration that a
tally can be confidently wrong.

```
python tools/mutate.py --list             # what is recorded, and for which slice
python tools/mutate.py --unit tactics     # run one unit's mutants
python tools/mutate.py --id ta-253        # run one
python tools/mutate.py --check            # they all still apply, without building (this is in CI)
```

Add a `{id, file, find, replace, expect, note}` per mutant when the slice lands. `find` must match
its file EXACTLY ONCE — the tool refuses anything else, because a mutant that applies nowhere runs
the unmutated suite and reports a survivor. `expect` is `caught` unless the note says why not.

Four things the tool does that a hand run kept getting wrong, so that reading them here is enough:

- **The baseline is proven before any mutant is believed.** With the oracle missing the suite
  reports `N passed, 1 failed` on every run (`OracleIsPresent`, by design); a harness that reads
  only that line reports every mutant as caught, and three tallies were published from exactly
  that (§6.119). The unmutated suite runs first and must be green.
- **A timeout is a catch.** Turning `cnt - 1` into `cnt - 2` in a loop that stops at zero makes an
  odd count run for ever: the suite times out, no summary line is printed, and a harness looking
  for one calls it a tooling failure. It is the strongest possible catch.
- **A worktree with no symlinks in it.** The old recipe symlinked the submodule into a detached
  worktree and warned that every `git checkout -f` ate the link. The tool COPIES instead — the
  reference files are text and the oracle's `versions/c64` tree is 4.4 MB — so the trap is gone by
  construction rather than documented.
- **A unit's test filter is verified before it is trusted.** A filter that selects the wrong tests
  is worse than no filter, because it produces a confident number about code it never ran. Record
  one filter per `TEST_CLASS` and the count they select; the tool checks it against the unmutated
  build.
- **Every unit carries a `selftest` mutant** — an unmissable change the suite cannot fail to catch,
  run first, and the run stops if it survives. It is the harness's own `OracleIsPresent`: without
  it, a list of "survivors" could be a run that never rebuilt, which is R13 realised (§6.119). Add
  one when you add a unit; `--check` fails if a unit has none.
- **It builds HEAD, not your working tree**, and says so when something selected is uncommitted.

What it does NOT do is recover the tallies already published. Those mutants are gone; the fifteen
survivors §6.125 named are in the file because their names pinned them, and the rest stay
unreproducible. R13 is open on that half.

**Read a routine through `tools/c64_source.py`, not by eye.** The upstream library is one tree
serving ten versions of Elite, and a routine's C64 form is whatever survives its `IF` / `ELIF` /
`ELSE` / `ENDIF` conditionals -- which nest, and which include `NOT(...)` blocks that are easy to
skim past. Porting the BBC Master's version of a routine by mistake is a real failure mode, met
more than once here.

```
python tools/c64_source.py --code library/common/main/subroutine/tt25.asm
```

It evaluates the conditionals against the master build's own symbol values and errors on a symbol
it does not know rather than guessing FALSE.

**Report what you actually did.** "Builds clean, not run" and "builds, and the arithmetic suite
is green against the oracle" are different claims. Never imply the second when you did the first.

### CI

[`.github/workflows/build-and-test.yml`](.github/workflows/build-and-test.yml) runs the same
things on a push. Three jobs, because they need different machines and different amounts of
patience:

- **Repository checks** (Ubuntu, ~10s): every checker listed above except `extract_tables`. This
  is the job that would have caught slice 0a's `.gitmodules` gap, because it starts from a fresh
  clone every time.
- **Suite on Ubuntu (portable runner)** (Ubuntu, ~85s): builds BeebAsm at the pinned commit (cached
  across runs), assembles the reference build, checks the generated tables, then builds and runs
  the whole suite through `Tests/PortableRunner/`. Not the authority (ADR-004 §1) — it is here so a
  broken push says so in a minute rather than five, and because a second compiler catches what the
  first tolerates. It is also more permissive than MSVC in ways nothing measures (§6.116).
- **Debug x64 build and tests** (Windows, ~5 min): builds BeebAsm with `cl`, assembles the
  reference build, checks the tables, builds `Tests\GameLogicTests\GameLogicTests.vcxproj` in
  Debug and Release, runs the suite in Release (the exhaustive sweeps are four times dearer
  unoptimised), then restores the executable's NuGet packages and **builds `Outpost.vcxproj`
  unpackaged in both configurations** — the only compiler that ever reads `Outpost/`.

Two things about the Windows job are deliberate and are explained in the workflow itself:

- **It assembles the game before it builds ours.** A run without the oracle fails exactly one
  test by design (Risk R9), so a CI that skipped this step would be permanently red for a reason
  nobody would keep reading.
- **The Debug build of the tests is compiled and never run.** It exists so the configuration a
  developer builds locally cannot rot; the suite runs in Release. Dropping it would save about a
  minute per push and is an owner call, not a default.

**Nothing derived from `Upstream/` leaves the runner.** The assembled blocks, the label map and
BeebAsm are all built from source this project does not own (ADR-001 §5); the only artefact is
the test result file. Do not add an upload that changes that.

---

## 7. Working rules

- Change the lines the task requires and no others. No drive-by reformatting, no opportunistic
  renames.
- Port one routine at a time, with its oracle test, and keep the original's structure until the
  test is green. Tidying is allowed *after* a routine is green and stays green.
- If a rule here blocks the task, say so in your report rather than quietly bending it.
- If a design decision needs to change, change the ADR — do not leave code and `Design/`
  disagreeing.
- **Original bugs are ported, not fixed.** If you find one, port it, add it to
  [ADR-001](Design/ADR/ADR-001-scope-and-fidelity.md) §6, and leave the fix for phase 6.

---

## 8. Before you hand work back

- [ ] Naming conforms to §1, and every ported function carries its `// 6502:` label (R7).
- [ ] Files are PascalCase, flat, unique repo-wide including against the CRT and STL.
- [ ] Every added/removed/moved file is in both the `.vcxproj` **and** the `.filters`.
- [ ] `GameLogic` gained no clock, no randomness, no float, no Win32 call.
- [ ] `Design/Source-Inventory.md` updated for anything you ported, INSIDE the notes column
      rather than as a new cell; `tools/inventory.py` and `tools/check_docs.py` run.
- [ ] It builds — Debug at minimum — and you said which configurations you actually built.
- [ ] Tests for the layer you touched were run, and you said which, and whether the oracle was
      present.
- [ ] Nothing under `Upstream/` or `MasterFile/` changed.
