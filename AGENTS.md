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
Where several 6502 entry points merged into one function, name them all. Where the upstream
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
columns, no tabs, `namespace` contents not indented, pointer binds left** (`std::uint8_t* _dst`).

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

```
python tools/inventory.py --check-includes    # every master INCLUDE resolves in Upstream/
python tools/inventory.py                     # coverage ledger: ported / pending / unaccounted
```

**Report what you actually did.** "Builds clean, not run" and "builds, and the arithmetic suite
is green against the oracle" are different claims. Never imply the second when you did the first.

### CI

[`.github/workflows/build-and-test.yml`](.github/workflows/build-and-test.yml) runs the same
things on a push. Two jobs, because they need different machines:

- **Repository checks** (Ubuntu, seconds): `inventory.py --check-includes`, `check_gamelogic.py`
  and its `--self-test`, and the coverage ledger. This is the job that would have caught slice
  0a's `.gitmodules` gap, because it starts from a fresh clone every time.
- **Debug x64 build and tests** (Windows): builds BeebAsm at a pinned commit, assembles the
  reference build, checks the generated tables against it, then builds
  `Tests\GameLogicTests\GameLogicTests.vcxproj` and runs `vstest.console.exe`.

Two things about that second job are deliberate and are explained in the workflow itself:

- **It assembles the game before it builds ours.** A run without the oracle fails exactly one
  test by design (Risk R9), so a CI that skipped this step would be permanently red for a reason
  nobody would keep reading.
- **It builds the test project, not the solution.** The project references pull in `GameLogic`
  and `NeuronCore`; `Outpost.vcxproj` is the untouched WinUI 3 template that ADR-005 §5 defers,
  and restoring the Windows App SDK to compile a project with no `Main.cpp` buys nothing.

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
- [ ] `Design/Source-Inventory.md` updated for anything you ported; `tools/inventory.py` run.
- [ ] It builds — Debug at minimum — and you said which configurations you actually built.
- [ ] Tests for the layer you touched were run, and you said which, and whether the oracle was
      present.
- [ ] Nothing under `Upstream/` or `MasterFile/` changed.
