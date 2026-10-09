# AGENTS.md — Engineering Rules

Operating instructions for every agent (and human) writing code in this repository. **Read this before generating a single line.**

This repository is a greenfield C++23 game and a hobby project with one developer: a Direct3D 12 game built on Windows with MSVC. This file is about **how code is written here** — naming, layout, build settings and the standing rules of the codebase. It is not the design. What the game *is* is the reference binary that [ADR-007](Design/ADR/ADR-007-reference-elites.md) names — the 1987 *Elite* in `ELITES.EXE`, with its patches and the one change [ADR-001](Design/ADR/ADR-001-scope-reference-and-fidelity.md) makes — and [`Design/Reverse-Engineering-Plan.md`](Design/Reverse-Engineering-Plan.md) sequences the work of porting it.

**There is no C++ yet.** This repository holds this file, the root configuration files, `.gitignore`, `.github/`, the reference binary `ELITES.EXE`, the design record under `Design/`, the checkers under `Build/` and a development tool under `Tools/` — no solution, no projects, no source. Nothing below is a target to migrate towards; it describes the code as it must be written from the first line. There is no legacy here and nothing is grandfathered, so a whole-tree run of any checker comes back clean — trivially today, and by conformance from then on.

**Where these rules come from.** They are carried over from two sibling repositories: `Outpost.Warzone`, where the formatter and linter settings were measured against roughly 223,000 lines, and `Nomad-Commander`. That lineage is why `.clang-format` and `.clang-tidy` are what they are, and it is why code can move between the trees without a rename or a reflow pass. **What did not come across is the other trees' design, their decisions or their plan.** A decision taken there binds nothing here.

**What is authoritative, in order:**

1. **This file** — conformance: naming, style, build settings, and how to work here.
2. **`Design/ADR/`** — engineering decisions taken while building, one file per decision (§6). Numbering started at `ADR-001` in this repository and does not continue another's.
3. **The surrounding code** — for anything neither of the above covers, match the file you are editing.

The design sits alongside this file rather than above it: the reference says what is built, and this file says how. A design question is answered by what the reference does — measured, not remembered — and goes to the owner only where the reference is silent or a change from it is wanted, and the owner's answer is written down before the code is.

If a rule here conflicts with a habit from another codebase, this file wins. If you think a rule is wrong or your task cannot be done without deviating, **say so in your report — never deviate silently.**

---

## 1. Naming convention (normative — no exceptions)

| Kind | Convention | Example |
|---|---|---|
| Type (class, struct, enum, concept, alias) | `PascalCase` | `SwapChainTarget` |
| Function, method | `PascalCase` | `PresentFrame()` |
| Member variable | `m_camelCase` | `m_deviceRemoved` |
| Static member (mutable) | `sm_camelCase` | `sm_activeDevice` |
| Global | `g_camelCase` | `g_instance`, `g_frameCount` |
| Parameter | `_camelCase` | `_fileName`, `_entityId` |
| Local | `camelCase` | `shadedColor` |
| Compile-time constant | `UPPER_CASE` | `WIDTH_PIXELS`, `CELL_PIXELS` |
| Enumerator | `PascalCase` | `DeviceLost`, `OutOfVideoMemory` |
| Macro | `UPPER_CASE` | `ENGINE_ASSERT` |
| Namespace | `PascalCase` | `Engine` |
| File | `PascalCase.cpp` / `.h` | `SwapChainTarget.cpp` |

**Note the split that catches people out: a `constexpr` is `UPPER_CASE`, an enumerator is `PascalCase`.** They are both compile-time and they are spelled differently on purpose — an enumerator is a *value of a type* and reads as one at the use site (`PageFault::OutOfVideoMemory`), while a constant is a number with a name and is meant to look like one. [`.clang-tidy`](.clang-tidy) enforces both, and it is the single source of truth for the option values; this document states the rules in prose and does not repeat the settings, so there is nothing to drift.

### The rules behind the table

**R1 — The leading underscore on parameters is deliberate.** It is legal C++: the reserved forms are `_Uppercase`, anything containing `__`, and `_lowercase` **at global scope**. A parameter is never at global scope, so `_fileName` is safe. Never introduce a reserved form — no `_Impl`, no `__helper`, no file-scope `_cache` (use `g_cache` in an anonymous namespace).

**R2 — A type name carries no prefix or affix, and that includes abstract ones.** An interface is `Transport`, not `ITransport`. A base class is not `BaseTransport` or `AbstractTransport`. PascalCase means the name and nothing else. This bans `CFoo`, `SFoo`, `EFoo`, `IFoo`, `FooBase`, `FooAbstract`, `FooImpl` and `_t` suffixes. Name the concept and let the concrete types say what they are:

```
Transport             ← the concept
├── UdpTransport      ← a socket-backed one
└── LoopbackTransport ← in-process, for tests
```

That tree is an illustration of the rule, not a description of anything. A base class for one derived class is ceremony: name the concept, and add the layer when a second thing needs it.

clang-tidy can require an *absent* prefix but cannot see a *present* suffix, so the repository checker carries the other half (§6).

**R3 — Compile-time constants are `UPPER_CASE`.** `constexpr`, `inline constexpr` and `static constexpr` members: `WIDTH_PIXELS`, `TICKS_PER_SECOND`, `CELL_PIXELS`. `sm_` is reserved for *mutable* statics, which are rare and must document their thread-safety.

**R4 — Acronyms capitalize as words**: `HlslSource`, `DxgiFactory`, `UdpTransport` — never `HLSLSource`. Identifiers from an external SDK keep that SDK's spelling (`ID3D12Device`, `DXGI_FORMAT`, `HRESULT`, `IDXGISwapChain4`) and are never renamed to fit.

**R5 — Template parameters are PascalCase**: `T`, `Fn`, `BlockBytes`, `Ts...`.

**R6 — Units belong in names; types do not.** `durationTicks`, `speedUnitsPerSecond`, `radiusMeters`, `volumePercent` are encouraged — a game measured in ticks, seconds, pixels and distances makes unit ambiguity a real defect class, and it is one the compiler cannot catch for you. Never encode the type: no `iCount`, `pEntity`, `strName`.

**R7 — A file is named for its primary type**, PascalCase, `.h` / `.cpp` only. `.hpp`, `.cc` and `.inl` are not used; template implementations live in the header. Exceptions, because MSBuild and the Visual Studio wizards spell them this way: `pch.h`, `pch.cpp`, `framework.h`, `targetver.h`, `Resource.h`.

**R8 — `m_` marks encapsulated state, not every field.** A `class` with invariants prefixes private members `m_`. A public aggregate — a `Desc` config struct, a wire record, a POD handed to the renderer — uses plain `camelCase` fields so brace initialization reads naturally.

**R9 — One namespace per layer, and the engine does not know the game.** Reusable engine code gets its own namespace; game code gets another. The split is a rule rather than a filing preference: if an engine type has to know a game concept by name in order to do its job, it is in the wrong layer. Test suites use `namespace <Project>Tests`.

**R10 — No `using namespace` at file scope in a header.** It leaks into every translation unit that includes it, and the failure it causes appears somewhere else. In a `.cpp` it is allowed for the unit-test framework and nothing else; otherwise qualify the name or write a local alias.

**R11 — One spelling per family, and it is the SDK's.** `color`, `initialize`, `serialize`, `normalize`, `quantize`, `synchronize`, `behavior`, `neighbor`, `center`, `gray`, `canceled`. Neither spelling is wrong English; the defect is a tree where a reader has to know which half they are in and a grep for one finds half the uses. `D3D12_CLEAR_VALUE::Color` settles which half wins. Prose is not checked — a design document may spell `flavour` and `harbour`; an identifier spells `flavor` and `harbor`.

### Worked example — this is the target style

```cpp
// Engine/SceneTarget.h
#pragma once

#include <cstdint>

namespace Engine
{

// R3: constant → UPPER_CASE. R6: the unit is in the name.
inline constexpr std::uint32_t SCREEN_WIDTH_PIXELS = 1920;
inline constexpr std::uint32_t SCREEN_HEIGHT_PIXELS = 1080;

// Enumerator → PascalCase, unlike the constants above.
enum class TargetFault : std::uint8_t
{
  DeviceRemoved,
  BadFormat,
  OutOfVideoMemory
};

/// The colour framebuffer the game draws into, and the depth buffer that goes with it.
/// R2: no prefix on the type. R8: private state carries m_.
class SceneTarget
{
public:
  struct Desc                                            // R8: aggregate → plain fields
  {
    std::uint32_t widthPixels;                           // R6: unit in the name
    std::uint32_t heightPixels;
    DXGI_FORMAT colorFormat;                             // R4: SDK spelling kept as-is
  };

  [[nodiscard]] static bool Create(ID3D12Device* _device,        // R1: _ on parameters
                                   const Desc& _desc,
                                   SceneTarget& _outTarget) noexcept;

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept { return m_widthPixels; }

private:
  ID3D12Resource* m_depthTarget = nullptr;
  std::uint32_t m_widthPixels = 0;
  bool m_deviceRemoved = false;
};

} // namespace Engine
```

### Enforcement

| Rule | Enforced by |
|---|---|
| The naming table, R1, R3, R5, R8 | [`.clang-tidy`](.clang-tidy), gated in CI over the whole tree |
| R2 affixes, R7 file names and project registration, R11 spellings, §2 flat directories, shader names and functional filters | `Build/CheckProjectFiles.py`, gated in CI |
| R4, R6, R9, R10 | Review. Check your own diff against the table before handing it back. |

**Both checkers exist and gate in CI** (§6). `Build/RunClangTidy.py` runs `.clang-tidy` over every translation unit the solution builds, and `Build/CheckProjectFiles.py` carries the rules in its row. Until there is C++ in the tree, both pass with nothing to check.

---

## 2. Repository shape

The concrete layout — the solution, the projects and the edges between them — is settled when the first project is created, and recorded here and in an ADR at that point. Until then, these are the standing constraints any layout has to satisfy.

**Project directories are flat, with exactly two sanctioned subdirectories.** C++ source — headers and `.cpp` alike — lives directly in its project's folder. **There is no `src/`, no `include/`**, and no other split of a project by file kind. This is not taste: `.clang-tidy`'s `HeaderFilterRegex` matches headers exactly one level in, so **a header in a subdirectory is silently unchecked** — no findings, no warning, and nobody notices for months. The two exceptions are the shader pipeline:

- **`<Lib>/Shader/`** holds the HLSL, hand-written, named for the shader and its stage: `<Shader>VS.hlsl` for a vertex shader and `<Shader>PS.hlsl` for a pixel shader. What two shaders share — a struct, a constant-buffer layout — goes in a PascalCase `<Name>.hlsli` beside them, registered in the `.vcxproj` as a `None` item and in the `.filters` like every other file, and compiled only through the shaders that include it.
- **`<Lib>/CompiledShader/`** holds what the compiler wrote: one header per `.hlsl`, `<Shader>VS.h` and `<Shader>PS.h`, each declaring a byte array `g_<Shader>VS` / `g_<Shader>PS`. It is **build output** — produced by an `FXCompile` item in the `.vcxproj` on every build, listed in `.gitignore`, skipped by every checker, and never edited or committed. The `.cpp` that binds the pipeline state includes it and nothing else does.

**Shaders are compiled into the executable.** The bytecode reaches the GPU from those generated byte arrays, and nothing else: no `.cso` beside the `.exe`, no shader loaded from disk at runtime, no `D3DCompile` and no `d3dcompiler_47.dll` dependency. A shader change is a rebuild.

**The edges run one way, and a layer never reaches sideways.** Engine code is built on by game code and never the reverse (R9), and two libraries at the same level share what is below them rather than each other. An edge that only exists "for now" is an edge, and it is the one that will be impossible to remove later.

**The project files are part of the source.** Adding, removing or moving a file means editing the owning `.vcxproj` **and** its `.filters`. A file that compiles locally but is missing from the project fails only in CI — or worse, links a stale object nobody notices.

**Filters are functional.** A `.filters` file groups a project by what the code *does* — `Rendering`, `Audio`, `Input`, `Shader` — never by what kind of file it is. The Visual Studio defaults `Source Files`, `Header Files` and `Resource Files` are deleted when a project is created and never come back, and a `.h` sits in the same filter as its `.cpp`.

**There are no vendored SDKs and no package manager.** The build depends on the Windows SDK and the MSVC standard library, and on nothing else. See R14.

**Build and IDE output is never committed** — `x64/`, `.vs/`, `*.user`, and anything a build step generates.

---

## 3. Build and verify

**x64 is the only platform.** No Win32/x86 configuration in any project or solution; do not add one, and do not write code that only works at 32 bits.

**The compiler settings are the settings.** Toolset `v145` (Visual Studio 2026), `/std:c++latest`, `/permissive-`, `/W4` with **warnings as errors**, `/fp:precise`, `/arch:AVX2` (R16). There is no CMake. If a build error tempts you to change the toolset, lower the language standard, turn off `/permissive-` or silence a warning — **stop and report instead.**

**Debug and Release are aligned by rule, not by luck.** Every setting that is not *about* optimisation reads identically in both configurations: language standard, conformance, warning level, include directories, precompiled header, floating-point model, instruction set. The two differ in exactly four things — `Optimization`, `_DEBUG` vs `NDEBUG`, `FunctionLevelLinking`/`IntrinsicFunctions`, and the linker's folding and LTCG switches. (MSBuild spells those four through a few more properties — `UseDebugLibraries`, `RuntimeLibrary` as the debug or release CRT, `LinkIncremental`, `WholeProgramOptimization`, `EnableCOMDATFolding`, `OptimizeReferences` — and that list is the whole of what may differ.)

That alignment matters more than it looks, because **CI builds Debug only** (§6). Release is compiled by whoever ships, and a Release that quietly lost an include directory or sat on an older language standard would not be discovered until then. A static check of the two configurations is what stands in for the build nobody runs.

**Build through the solution, never a `.vcxproj` directly.** Output paths and cross-project include directories are anchored on `$(SolutionDir)`, and MSBuild defines `SolutionDir` only for a solution build. Building a project file directly resolves every one of those paths against the *project* folder instead of the repository root. **It does not fail — that is the problem.** Output lands in the wrong folder, so the next solution build links against whichever copy is staler, and every cross-project include path becomes a directory that does not exist. The breakage is latent: it bites the first time a file reaches across projects, which may be weeks after someone got into the habit. To build one project, use `/t:<ProjectName>` on the solution.

```powershell
# Everything, from the repository root, naming the solution.
msbuild <Solution>.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo

# One project, still through the solution.
msbuild <Solution>.slnx /t:<ProjectName> /p:Configuration=Debug /p:Platform=x64 /m /nologo

# Release, before you claim anything about it.
msbuild <Solution>.slnx /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
```

**A project does not put its own directory on the include path.** `cl.exe` already searches the directory of the including file first for a quoted include, so `#include "FileSys.h"` from a `.cpp` in the same folder resolves without help. Only the directories of *other* projects are listed, as `$(SolutionDir)<Project>`.

**Run the tests**, through `vstest.console.exe`, over every suite the build produced.

**vstest reports "no tests found" as a pass.** An empty suite is therefore worse than no suite: it is a green check mark over a library nobody exercised. Every test project ships a placeholder `SuiteSmoke` for exactly this reason; delete it when the first real test lands, never before.

**Run the checkers before you push.** They are seconds of Python and they are what CI runs:

```powershell
python Build\CheckFormat.py           # clang-format, whole tree. --fix rewrites the offenders
python Build\CheckProjectFiles.py     # build shape, project registration, R2/R7/R11
python Build\RunClangTidy.py          # needs a Developer PowerShell (INCLUDE must be set)
```

**A green build says nothing about whether the game draws.** For anything touching rendering, input, audio or presentation, launch the executable and look at it.

**Report what you actually did.** "Builds clean, not run" and "builds and runs" are different claims. Never imply the second when you only did the first, and say which configurations you built.

---

## 4. Layout and formatting

[`.clang-format`](.clang-format) is the authority for C++ layout: 2-space indent, 140 columns, Allman braces, pointer and reference bound left, includes never reordered. [`.editorconfig`](.editorconfig) covers everything clang-format does not — CRLF, UTF-8, final newline, trailing whitespace, and the non-C++ formats — and repeats the two numbers an editor needs before the first save.

**This tree is formatted, and CI keeps it that way.** A whole-tree format check here is a no-op. Format what you write; if the check fires, run `--fix` and commit the result rather than arguing with it.

- **Do not reformat what your task did not touch.** The check being green tree-wide means a drive-by reformat produces pure churn and buries your actual change.
- **Include order is load-bearing and grouped by hand**, which is why `SortIncludes` is `Never`: `pch.h`, then `<windows.h>` before any D3D12/DXGI/XAudio2 header, then the rest of the SDK, then project headers, then the standard library. A formatter reordering these behind a change's back is a correctness risk, not a style preference.
- **One header owns the Windows macro family, and nothing else defines any of it.** `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `NOMCX`, `NOSERVICE`, `NOHELP` are set in that one header, before `<windows.h>`, and the project files deliberately define none of them. Two owners of one macro is C4005, and `/WX` makes that fatal — `/D` spells a bare macro as `1` where a `#define` spells it as nothing, so the collision is guaranteed rather than possible. If you need `<windows.h>`, include that header; do not add the macros yourself.
- Do not silence a diagnostic with `#pragma warning(disable: ...)` to make a build pass. Fix the cause, or report it.

---

## 5. Rules for this codebase

**R12 — Graphics is Direct3D 12.** COM lifetimes are RAII from the first line — a raw `AddRef`/`Release` pair in new code is a defect, not a style. How the renderer is shaped — passes, render targets, resolution, scaling, window style, multisampling, text — is not settled here; it is a design and engineering decision taken when the renderer is built, and recorded as an ADR (§6).

**R14 — No third-party dependencies and no package manager.** The Windows SDK and the MSVC standard library, and nothing else. If you believe something is unavoidable, propose it in your report with what it buys and what it costs — do not add it. This is a closed list, not a high bar.

**It binds what the executable is built from, not what a development tool needs.** Scripts under `Build/` and `Tools/` never ship and never link, so a baker that needs Pillow does not reopen this rule. **Third-party *content* is a different question and it is the owner's**: art, fonts and sound are allowed, and anything under a licence needs the owner's approval before it lands, with the licence text travelling with the bytes.

For Direct3D that list means what the Windows SDK installs: `d3d12.h`, `dxgi1_6.h`, `DirectXMath.h`, `wrl/client.h` (`Microsoft::WRL::ComPtr` is the COM smart pointer R12 asks for) and the `fxc`/`dxc` compilers that `FXCompile` drives. It excludes what a D3D12 sample reaches for by reflex, because each is NuGet or GitHub content and not SDK content: the DirectX Agility SDK and its `d3dx12.h`, DirectX-Headers, DirectXTK12, DirectXTex, and the DirectX Shader Compiler as a redistributable. Resource barriers and heap descriptions are written by hand.

**R15 — Memory is plain C++.** `new`/`delete` where it must be, RAII everywhere, standard containers by default. No pool, slab or free-list allocator without a decision recorded in `Design/ADR/`.

**R16 — The floating-point model and instruction set are stated, not inherited.** Every project compiles `/fp:precise` and **`/arch:AVX2`**, stated explicitly in the project file rather than inherited from an MSVC default — a default is not a decision, and the symptom of losing one is two builds of the same code disagreeing about the same sum with no line to blame. Both settings are identical in Debug and Release.

**What `/arch:AVX2` costs is named rather than waved at.** It sets an AVX2 floor — Intel Haswell (2013) and AMD Excavator (2015); an older CPU meets an illegal instruction, not a message. And it lets MSVC contract `a*b+c` into an FMA even under `/fp:precise`, which changes float results, and may contract differently at different optimisation levels. Float code that must produce bit-identical results across builds cannot rely on it.

**If the game needs a deterministic core** — a simulation that replays, lockstep networking, a result that must reproduce from a seed — that is a decision recorded as an ADR, and inside that core: no `float` where a fixed-point or integer quantity will do (hold a fraction as integer hundredths and say so in the name, R6), no iteration over an unordered container whose order reaches the outcome, no wall-clock time (a tick is the clock, and wall time maps to ticks at one seam), and randomness from a pinned PRNG with a recorded seed — never `std::random_device`, never a hash of an address.

**R17 — A string you do not write is `const`.** `/permissive-` turns on `/Zc:strictStrings`: a literal is `const char[N]` and will not bind to `char*`. The fix is `const` on the signature, never a cast at the call site — a `const_cast` here is a lie about a literal that lives in a read-only section, and writing through it is a real crash rather than a theoretical one.

**R18 and up are reserved.** A design document does not only say what to build; some of what it says constrains how the code is *shaped* — which state a decision routine may read, what an emitted event has to carry with it, where tuning values live. Those are conformance rules with a design source, and they are written here as R18 onward when there is a design to cite, without renumbering anything above. Until then, do not invent one and do not import one from another tree: a rule with no source behind it is a rule nobody can settle an argument with.

---

## 6. Working rules

**Stay in scope.** Do what the task asks. Adjacent code that offends you is not part of the task — note it in your report and move on. Unrequested "while I was in there" changes are the main way a young tree acquires regressions it cannot bisect.

**Record decisions as ADRs.** An engineering decision — a file format, a wire protocol, a subsystem's shape, an exception to a rule here — goes in `Design/ADR/` as one file per decision, numbered in order from `ADR-001-<slug>.md`, stating the context, the decision and what it forecloses, in the same commit as the change that implements it. Figures in an ADR are measured, not estimated — if you quote one, say how you measured it. A decision nobody wrote down gets re-litigated every few months by whoever forgot it.

**The checkers came first.** `Build/CheckFormat.py`, `Build/CheckProjectFiles.py` and `Build/RunClangTidy.py` are what §1, §2 and §3 lean on, and they landed before the first line of C++, so every rule in their rows has been machine-checked from the start.

**What CI runs.** [`.github/workflows/build.yml`](.github/workflows/build.yml) has two jobs: a Windows job that checks the build shape, builds **Debug|x64**, runs the test suites and then clang-tidy; and a Linux job that checks formatting on a pinned clang-format. **Every step that has something to run blocks; a step whose input does not exist yet is skipped, not faked.** Each gate is guarded on the file it needs — the checker script, the solution, the built test DLLs — so the workflow is honest about today's empty tree and starts gating the moment that file lands. The guards are the only concession: nothing is `continue-on-error`, and a script that exists and fails still fails the build. Remove a guard once its input is permanently there, not before, and never add one to get past a red build.

**CI does not build Release.** The Windows build is the slow half of the pipeline and a second configuration roughly doubles it for a tree where the two differ only in optimisation. What stands in for it is the static alignment check on the two configurations (§3) — and, before a release, an actual `Configuration=Release` build by whoever is shipping. If you change something that could plausibly break only under optimisation, build Release yourself and say so.

**Commits and PRs.** Branch off `main`; small, focused commits with an imperative subject describing the change, not the process. One change per PR. CI must be green. Never commit build output, `.vs/` or `.user` files.

---

## 7. Before you hand work back

- [ ] Naming conforms to §1 — `_` on parameters, `m_` on class state, `UPPER_CASE` constants, `PascalCase` enumerators, no `I`/`C`/`Base` affixes.
- [ ] Only the lines the task required were changed; no reformatting, no drive-by fixes.
- [ ] New, removed or moved files are in the `.vcxproj` **and** the `.filters` of every project involved.
- [ ] No project's `ConformanceMode`, `LanguageStandard`, `WarningLevel` or `TreatWarningAsError` was changed, and no warning was silenced with a pragma.
- [ ] Debug and Release still agree on everything §3 says they must.
- [ ] No new third-party dependency (R14).
- [ ] The checkers pass — or, for one not yet written, the report says which and why.
- [ ] It builds Debug|x64, and every test suite runs and passes.
- [ ] If it touches rendering, input, audio or presentation: it was **run**, not just built.
- [ ] `Design/ADR/` has a new file if the change *was* a decision.
- [ ] Your report states plainly what you verified, what you assumed, and any rule here you had to bend.
