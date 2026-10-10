# ADR-004 — Projects and layout

**Status:** accepted 2026-10-09, when the first projects were created (AGENTS.md §2). The layout follows the starting point in [Reverse-Engineering-Plan.md §5, Phase 2](../Reverse-Engineering-Plan.md#phase-2--the-host). The owner can reopen it. **Amended 2026-10-09:** a fourth project, `ReferenceRunner`, joins the three (item 2), and item 5's claim about `<expected>` is corrected. **Amended 2026-10-10:** `GameLogic` and `GameLogicTests` are created, with the replays (ADR-008), ahead of the port they will hold (items 2 and 3).

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

2. **Six projects now** (`ReferenceRunner` was added with the PC host, and `GameLogic` and `GameLogicTests` with the replays):

   | Project | Kind | Namespace | Holds |
   |---|---|---|---|
   | `Machine` | static library | `Machine` | The 8086 interpreter, and the PC it runs on: memory, the timer, the interrupt controller, the keyboard controller, the CGA, the speaker gate, the joystick port. Also the BIOS and DOS services, emulated at the call level for exactly the functions the reference uses (plan §1), and the MZ loader. |
   | `MachineTests` | native unit tests | `MachineTests` | Tests of `Machine`. |
   | `CpuConformance` | console executable | `CpuConformance` | Runs the SingleStepTests 8088 suite against `Machine`'s interpreter (ADR-003 item 1). CI builds it. `Tools/CpuConformance.py` fetches the suite, converts it and runs it, because the suite is about 3 million tests and does not belong in the repository or in every CI run. |
   | `ReferenceRunner` | console executable | `ReferenceRunner` | Runs the reference headless on the PC host and records what ADR-003 compares: screenshots, state digests, the boot trace and code coverage (ADR-006). Plays and records replays (ADR-008). Standard C++, so it runs where the binary is studied as well as on Windows. CI builds it. |
   | `GameLogic` | static library | `Elite` | The port. Until the first routine is replaced, what the game needs to know about the reference: its hash and the D5 byte, how to load it, the replay format and the game-state digest (ADR-008). |
   | `GameLogicTests` | native unit tests | `GameLogicTests` | Tests of `GameLogic`, including the replay corpus (ADR-008 item 6). It references `Machine` as well as `GameLogic`, because `GameLogic`'s headers are about the machine the reference runs on. |

3. **Two projects later, named now so that the edges are known:**

   | Project | Kind | Namespace | Holds |
   |---|---|---|---|
   | `Engine` | static library | `Engine` | The Win32 window, the D3D12 presenter, XAudio2 and input. It knows neither the game nor the machine (R9). |
   | `Outpost` | executable | `Outpost` | The game: it puts the others together. |

   The edges run Outpost → Engine, Outpost → GameLogic, Outpost → Machine, ReferenceRunner → GameLogic, and GameLogic → Machine. The last one exists only through Phase 3, while native routines read and write the original's memory, and it goes in Phase 4 together with `Machine` (D7). Each test project depends on its library and nothing else.

4. **`Machine` (and later `GameLogic`) is standard C++ and includes no Windows header.** It compiles with MSVC in the solution and with GCC 13 or Clang 18 outside it. The Linux build is a development tool under `Tools/`: it compiles the library's sources and a driver, never a second build system for the product, and it only proves that the code runs. MSVC with `/W4 /WX` is still the build that gates (AGENTS.md §3). `Engine` and `Outpost` are Windows-only.

5. **C++ features are limited to what both compilers have.** That means C++23 as far as GCC 13's library supports it, and as far as Clang 18 can compile that library. `<print>` is out, for example, and `<span>` and `<format>` are in. **`<expected>` is out too**, which this item first said the opposite of: libstdc++ 13 enables `std::expected` only when `__cpp_concepts` is at least 202002, and Clang 18 reports 201907 (measured 2026-10-09). A function that can fail returns its error and passes results through reference parameters instead. When the Linux side moves to newer compilers this limit moves with them, and this ADR is amended.

## What this forecloses

- Windows types, `<windows.h>` or any SDK header in `Machine` or `GameLogic`.
- An edge from `Engine` to `Machine` or `GameLogic`. The engine is handed pixels, samples and key events, never the machine.
- A second build system for the product. The Linux compile is a tool, and the solution is the build.
- `CpuConformance` gating every CI run. It is built by CI and run by its script. What gates the interpreter in CI is `MachineTests`.
