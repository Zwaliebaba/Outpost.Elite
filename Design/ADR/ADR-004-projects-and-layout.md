# ADR-004 — Projects and layout

**Status:** accepted 2026-10-09, when the first projects were created (AGENTS.md §2). The layout follows the starting point in [Reverse-Engineering-Plan.md §5, Phase 2](../Reverse-Engineering-Plan.md#phase-2--the-host). The owner can reopen it.

## Context

Phase 2 builds an 8086 interpreter and the PC it runs on (plan §3 C). Phase 3 replaces the original's routines with C++, and Phase 4 removes the interpreter (ADR-003, D7). The work happens in two places:

- **Windows with Visual Studio**, where the solution is built and the game is played.
- **Linux cloud sessions with GCC and Clang and no MSVC**, where most of the reverse engineering is done.

An interpreter that only compiles on Windows could never be run in the place the binary is being studied. The fastest way to settle what the static map cannot is to run the original headless and watch it: whether the code at 0x1B7A ever executes, what each docked screen draws, what each routine is called with.

AGENTS.md §2 fixes the rest of the shape:

- flat project folders;
- one-way edges;
- the engine not knowing the game;
- tests in a `*Tests` project per library.

## Decision

1. **One solution at the root, `Outpost.Elite.slnx`**, x64 only, built through the solution (AGENTS.md §3).

2. **Three projects now:**

   | Project | Kind | Namespace | Holds |
   |---|---|---|---|
   | `Machine` | static library | `Machine` | The 8086 interpreter, and the PC it runs on: memory, the timer, the interrupt controller, the keyboard controller, the CGA, the speaker gate, the joystick port. Also the BIOS and DOS services, emulated at the call level for exactly the functions the reference uses (plan §1), and the MZ loader. |
   | `MachineTests` | native unit tests | `MachineTests` | Tests of `Machine`. |
   | `CpuConformance` | console executable | `CpuConformance` | Runs the SingleStepTests 8088 suite against `Machine`'s interpreter (ADR-003 item 1). CI builds it. `Tools/CpuConformance.py` fetches the suite, converts it and runs it, because the suite is about 3 million tests and does not belong in the repository or in every CI run. |

3. **Three projects later, named now so that the edges are known:**

   | Project | Kind | Namespace | Holds |
   |---|---|---|---|
   | `Engine` | static library | `Engine` | The Win32 window, the D3D12 presenter, XAudio2 and input. It knows neither the game nor the machine (R9). |
   | `GameLogic` | static library | `Elite` | The port. |
   | `Outpost` | executable | `Outpost` | The game: it puts the others together. |

   The edges run Outpost → Engine, Outpost → GameLogic, Outpost → Machine, and GameLogic → Machine. The last one exists only through Phase 3, while native routines read and write the original's memory, and it goes in Phase 4 together with `Machine` (D7). Each test project depends on its library and nothing else.

4. **`Machine` (and later `GameLogic`) is standard C++ and includes no Windows header.** It compiles with MSVC in the solution and with GCC 13 or Clang 18 outside it. The Linux build is a development tool under `Tools/`: it compiles the library's sources and a driver, never a second build system for the product, and it only proves that the code runs. MSVC with `/W4 /WX` is still the build that gates (AGENTS.md §3). `Engine` and `Outpost` are Windows-only.

5. **C++ features are limited to what both compilers have.** That means C++23 as far as GCC 13's library supports it. `<print>` is out, for example; `<expected>` and `<span>` are in. When the Linux side moves to a newer GCC this limit moves with it, and this ADR is amended.

## What this forecloses

- Windows types, `<windows.h>` or any SDK header in `Machine` or `GameLogic`.
- An edge from `Engine` to `Machine` or `GameLogic`. The engine is handed pixels, samples and key events, never the machine.
- A second build system for the product. The Linux compile is a tool, and the solution is the build.
- `CpuConformance` gating every CI run. It is built by CI and run by its script. What gates the interpreter in CI is `MachineTests`.
