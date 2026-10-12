# ADR-031 — The renderer's first piece: the picture on two frames in flight, and the engine's tests on WARP

**Status:** accepted 2026-10-12, with the change that implements it, piece 1 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It is the first part of the renderer that D47 rules and AGENTS.md R12 leaves to the time the renderer is built. The owner's ruling D60 sets its scope. It amends [ADR-004](ADR-004-projects-and-layout.md) with a project, `EngineTests`, and [ADR-009](ADR-009-windows-shell.md), whose `Presenter` it replaces. **Amended 2026-10-12 by [ADR-032](ADR-032-the-flight-snapshot.md) (D61):** piece 2 lands as data and tests, so tearing, which the table below gives to piece 2, moves on to piece 3 with the first frames drawn between updates. **Amended 2026-10-12 by [ADR-033](ADR-033-the-space-view-hybrid.md) (D62):** the scene's targets arrive with the space view's first part; bloom moves on to the sun and the planet, and tearing to the full window.

## Context

D47 rules how the renderer is built. That covers the device and two frames in flight; a linear FP16 scene with 4× MSAA, a `D32_FLOAT` depth buffer in reversed Z, a resolve and bloom; the 2D layer through an sRGB view of the back buffer; and text from a DirectWrite glyph atlas. D48 adds the 1920×1080 design units, and the plan put all of it in piece 1.

Piece 1's exit can observe almost none of it: the game plays as before, and what it draws is still the CGA picture (D46). The scene targets would have no scene until piece 3's space view, and the 2D layer and the text no shape or word until piece 3's HUD. Built now, they would be code that nothing draws through, and that no check could tell from a broken version.

The shell also had no check that it draws at all. CI compiled `Engine` and `Outpost`, but whether a picture reached the screen was known only when the owner played. Piece 5 needs a headless renderer anyway: §6.5 draws each docked screen on WARP and compares it with a golden image (D53).

**The owner's ruling D60 (2026-10-12):** piece 1 builds what its exit can observe. Each other part of D47 and D48 arrives with the content that first uses it, and piece 1 brings a test of the renderer on WARP.

## Decision

1. **`Renderer` replaces `Presenter`** (`Engine/Renderer.h`):
   - one device and one direct queue;
   - **two frames in flight**, each with its own command allocator, upload buffer and fence value. The CPU records and fills one frame while the GPU may still draw the other.
   - The picture's texture is one. A frame's copy into it is ordered behind the last frame's draw, on the same queue, by the barrier that returns it to the copy state.
   - The picture's pass is `Presenter`'s, unchanged: the same shaders and the same sharp bilinear filter (ADR-009).
2. **The swap chain waits for a frame latency of one.** It is a flip-discard swap chain of two `R8G8B8A8_UNORM` buffers, made with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, its maximum latency set to 1.
   - **Pacing.** `Present` waits on the latency object, for up to a second, before it records a frame, and presents at sync interval 1. Pacing is still the display's.
   - **What changes.** `Presenter` waited for the GPU at the end of every frame. `Renderer` waits at the start, and only until the swap chain can take the frame, so the CPU's work overlaps the GPU's.
   - **What still waits for the GPU.** `Resize` and the destructor wait for it explicitly.
3. **Where the picture goes is a function of its own,** `PlacePicture`: the largest whole-number multiple of its logical size that fits, centred, the odd pixel right of and below it (D46). On a target smaller than the logical size it falls back to the largest fit, now rounded down to whole pixels. `Presenter` placed it on a fractional viewport there.
4. **`Capture` draws without a window.** It draws one frame as `Present` would into a client area of the given size, on a target of the renderer's own, and reads it back as 0x00RRGGBB, on any device, WARP included. `Renderer::Desc::warp` chooses Direct3D 12's software device. It is how the tests see what is drawn, and the harness §6.5's golden images will use.
5. **`EngineTests`** is a test project in `Tests/EngineTests/`, beside `GameLogicTests`. It depends on `Engine` and nothing else, and links `d3d12.lib` and `dxgi.lib`, which the Windows SDK provides (R14). `Build/RunTests.py` and CI find it as they find every `*Tests.vcxproj`. Its `RendererTests`:
   - **`PlacesThePictureAtAWholeNumberScale`:** `PlacePicture` at 1080p, 1440p, 4K and 21:9, at one pixel over scale 1, and below scale 1 by either side.
   - **`DrawsThePictureOnWarp`:** a 4×2 picture of eight colours drawn on WARP at scale 2 into a 20×10 target. Every pixel of the border must be the border's colour exactly. Every pixel of the picture must be its source pixel's colour, each channel within 2.
   - **`EachFrameDrawsItsOwnPicture`:** five frames, alternating two pictures, through both frames in flight and round again. Each must draw the picture it was given.
6. **What D60 moves out of piece 1:**

| Part of D47 or D48 | Arrives with | Why there |
|---|---|---|
| The scene: FP16 with 4× MSAA, `D32_FLOAT` in reversed Z, the resolve and bloom | piece 3, the space view | the first scene |
| The 2D layer: an sRGB view of the back buffer and one signed-distance shader | piece 3, the HUD | the first shapes |
| Text: the DirectWrite glyph atlas and `tnum` (D39, D55) | piece 3, the HUD | the first words |
| The 1920×1080 design units at one scale (D48) | piece 3, the HUD | the first layout in design units; the CGA picture keeps D46 |
| Tearing where the display supports it | piece 2, the snapshot | the first frames drawn between the game's updates. Tearing means sync interval 0, which at 20 updates a second needs a frame cap, or the loop spins |

PerMonitorV2, the rest of D48 in piece 1, is in already (ADR-009 item 2).

### Verified 2026-10-12

| Check | Result |
|---|---|
| Compiles | `Engine/Renderer.cpp`, `Outpost/Application.cpp` and `Tests/EngineTests/RendererTests.cpp`, syntax only, with mingw-w64's g++ 13 and its Windows SDK headers, `-Wall -Wextra -Wconversion -Wpedantic`: clean. The shader headers stood in for `CompiledShader/`. |
| clang-tidy | the root `.clang-tidy` on the same three files, clang-tidy 18 against the same headers: no finding |
| `PlacePicture` | each case of the test computed by hand |
| Checkers | `CheckProjectFiles` (five projects) and `CheckFormat` are clean |
| Not run here | the MSVC build, the WARP tests and the game: CI builds the solution and runs `EngineTests`, and the owner plays it at 100 to 200% display scale, which is piece 1's exit |
| Played 2026-10-12 | by the owner: piece 1's exit holds |

## What this forecloses

- **Waiting for the GPU at the end of a frame.** Code that needs the GPU idle waits for it explicitly, as `Resize` does.
- **A renderer feature with no headless test.** Each part of D47 that arrives later is drawn on WARP in `EngineTests` before the game draws through it.
- **Building a part of D47 ahead of what draws through it.** D60 places each part with its content; to move one earlier is a new ruling.
