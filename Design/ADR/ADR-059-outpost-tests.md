# ADR-059 — OutpostTests: the shell's drawing on WARP, against golden images

**Status:** accepted 2026-10-14, with the change that implements it, under the owner's ruling D94. It amends the layout ADR-004 records, adding a sixth project.

## Context

§6.5 asks that each docked screen be drawn headless on WARP from a prepared state and compared with a checked-in golden image: a difference reported, a missing image failed.

The docked screens are drawn by the shell. `Outpost/DockedFrame.cpp` and `Outpost/DockedMarket.cpp` draw them into an `Engine::Canvas` from the game's view (ADR-058). No test project built anything of the shell, and the shell is an executable, not a library.

`Build/CheckProjectFiles.py` forbids a source file in two projects, so a test project cannot compile the shell's files a second time. A stale or divergent copy of an object is what that rule prevents.

WARP needs Windows, so the images cannot be made where the code is written here. The owner has to see them before they become golden.

## Decision

1. **`Tests/OutpostTests`, a sixth project**, a unit-test DLL like `EngineTests`. It includes `Engine`, `GameLogic` and `Outpost`.
   - It links `Engine.lib` and `GameLogic.lib` by project reference. `GameLogic.lib` is only for `FindInRepository`.
   - It links the shell's drawing as the shell's own build compiled it: `Outpost\pch.obj`, `DockedFrame.obj` and `DockedMarket.obj` from `$(SolutionDir)$(Platform)\$(Configuration)\Outpost\`.
   - A project reference to `Outpost` that links nothing orders the build. The objects are therefore always the shell's current ones, and no file is in two projects.
2. **`GoldenImage`, in the test project.**
   - It writes what a test drew to `x64/Rendered/<name>.png` through WIC. The PNG is 24-bit, from `Renderer::Capture` at 1920 × 1080.
   - It compares the drawing with `Tests/OutpostTests/Golden/<name>.png`. A pixel differs when a channel is off by more than 8.
   - A missing or unreadable golden fails the test, with the paths in the message. A difference is written to the test's log, not failed (§6.5).
   - CI uploads `x64/Rendered` as the artifact `rendered-images` whatever the run's outcome. The owner looks at each image and commits it to `Golden/` once it is right (D94).
   - A test whose golden is missing therefore fails until then, by design: the first run of a new screen is red until its image is accepted.
3. **What the first tests draw**, from views prepared as the design's numbers:
   - `D01-Market`: D01 as `docked/01-market.png` draws it.
   - `D01-Market-States`: a refusal strip, [B] BUY and [S] SELL off, and the pointer on [S] SELL.
   - `D01-Market-Bought`: a purchase's strip, and the pointer on a row.
   - Besides the images, `DockedMarketTests` checks where clicks land, row by row and control by control, including the key bar. It also checks that the strips' words are D11's Market column's, word for word.
4. **AGENTS.md** names the sixth project and the artifact.

## What was measured, and what was not

The drawing code was compiled with g++ (`-Wall -Wextra -Wconversion`, no warnings) and clang-tidy 18 (clean) against a portable `Canvas`. Its canvas items were rasterised by a stand-in with the Inter typeface in place of Bahnschrift, and the result was compared by eye with `docked/01-market.png`, panel by panel. The layout's measures were read off the design's pixels:

- the bars, panels, chamfers, rows, controls and key bar to the pixel;
- the type sizes from the capitals' heights.

None of these figures is a golden image: those come from WARP.

| Not run here | the MSVC build of `OutpostTests`, `Window.cpp`'s mouse and `Application.cpp`'s wiring; the tests themselves; the goldens, which the owner records from CI's artifact |
|---|---|

## What this forecloses

Each docked screen of piece 5 adds its drawing to the objects `OutpostTests` links, and its golden images under `Golden/`. The shell's drawing stays in the shell, compiled once. If the objects to link grow past a handful, the shell's drawing becomes a library of its own, which is a new ADR.
