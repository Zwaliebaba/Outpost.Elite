# ADR-033 — The space view, first part: the ships as models in the CGA picture's view

**Status:** accepted 2026-10-12, with the change that implements it, the first part of piece 3 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It carries out part of D47 and D49 under the owner's ruling D62. It amends [ADR-031](ADR-031-the-renderer-first-piece.md)'s table of what D60 moved: the scene's targets arrive here, and bloom and tearing move on to the part that goes full-window. It completes [ADR-032](ADR-032-the-flight-snapshot.md)'s list of what D61 left to piece 3, for the ships. **Amended 2026-10-12, after the owner's play test:** the station's slot flickered over its front face, two triangles in one plane at equal depth; a face's items now keep the game's painting order (item 3).

## Context

Piece 3 replaces the CGA space view with a 3D one: the ships from their blueprints, lit by the sun (D47, D50), drawn between the game's updates (D49). The pieces before it left it a renderer with two frames in flight (ADR-031) and a snapshot of each frame of flight (ADR-032), and nothing that drew a ship.

The 1987 view is more than its ships. In the same 256 × 126 pixels the game draws:
- the sun and the planet as discs;
- a far ship as a dot, and a far station as a disc;
- the laser beams, the stardust and the sights;
- whatever a frame draws over its own objects.

Each needs a design of its own before a 3D view can draw it: D45's planets and sky from the seed, the HUD of `cockpit-hud-overlay/`, particles. A view that drew only the ships would lose the rest.

**The owner's ruling D62 (2026-10-12):**
- **Space view first, hybrid.** The new view replaces only the CGA space view's rectangle, in flight, and the dashboard below it stays the CGA's. Whatever the new view does not draw yet stays the game's own pixels. The sequences it does not draw (the tunnels, the jumps, explosions' screens and the title) keep the CGA picture. The part after this one puts in the canvas's HUD and cockpit frame and goes full-window, Hor+ (D50).
- **1987 colours, lit.** Each face keeps its 1987 colour, mapped to a provisional palette, flat-shaded and lit by the sun.

## Decision

1. **A ship's model is its blueprint, read from the game itself** (`GameLogic/ShipModel.h`).
   - `ShipModelOf` runs the blueprint's own handler (`BuildBoxCornerVertices`, or `BuildDodoVertices` for the Dodo station) and its vertex program, on a scratch `GameState`, with every angle 0 and the centre at the origin. It reads back the vertices and walks the face list as `DrawVisibleFaces` and `DrawFaceItems` do. So a model is the blueprint the game draws, with no copy of its numbers to drift from it.
   - **A face, not a triangle, is seen or not.** A face's triangles need not lie in its test triangle's plane, and the game draws all of them, and its edges, exactly when its test triangle faces the viewer. So each face carries its test triangle's unit normal, `(t0 - t1) × (t2 - t1)`, which points out of the model. The renderer culls by that normal, not by winding.
   - **The rotations are the game's.** `ObjectRotation` is a box's: its roll negated in (x, y), then its yaw in (x, z), then its pitch in (y, z), as `BuildBoxCornerVertices` turns the corners before the player's rotation turns them on. The Dodo builds its rings at the roll's phase, which turns it the other way round its roll, so `ModelRotation` reverses the roll for a model marked `rollReversed`. `ViewRotation` is `RotateToViewDirection`'s turn of (x, z) by minus the view. `CameraRotation` is the view's turn after the player's.
   - **A face's colour is its fill pattern's mix** (`PatternMix`): the share of each of the four CGA colours in the pattern's 8 pixels.
2. **The space view's layers** (`GameLogic/SpaceViewLayers.h`) are what the game drew in the view besides its models, in its own pixels. They are the platform's, outside the counted state, and read the game's state without writing any of it.
   - `DrawFarthestObject` reports each object it draws (`Hardware::BeginSpaceViewObject`, `EndSpaceViewObject`), and `DrawObject` reports when the object is a blueprint's model (`NoteSpaceViewModel`). `RunFlight` begins a frame's watch (`BeginSpaceViewFrame`).
   - **Under** is what each object that is not a model changed in the drawing buffer: the sun and the planet, a dot, a distant station.
   - **Over** is what the screen shows, read from video memory once `FinishSpaceViewFrame` copied it, where it differs from the buffer as the last object left it. That is the beams, the stardust and the sights.
   - Each slot drawn as a model is marked in the snapshot (`SnapshotObject::model`), with the view position it was drawn at (`viewX`, `viewY`, `viewZ`). A station far off is drawn at its compass position doubled (`DrawStationIfDistant`), not at its own, so the scene places each model where the frame drew it: `ScenePosition` turns the view position back by the camera.
   - `Platform::SpaceViewShown` says whether the screen still shows the last published frame's view. The CGA must be in its graphics mode from the start of video memory (`Cga::ShowsGraphicsFromStart`), and the view's bytes must be unchanged there (`SpaceViewLayers::Shown`). The escape pod's screen is 40-column text over the same bytes, and the title draws through `FinishSpaceViewFrame` outside `RunFlight`; neither is shown. A frame's view reaches the screen in work, which paced time does not divide, so between two frames of a flight it holds. The tunnels, the jumps, the charts and every other screen end it.
3. **The renderer draws a scene in an inset of the picture** (`Engine::Renderer`, `SceneFrame`, `SceneInset`).
   - **The scene's targets are D47's:** `R16G16B16A16_FLOAT` with 4× MSAA and a `D32_FLOAT` depth buffer in reversed Z with no far plane, at the inset's size on screen in physical pixels, resolved each frame.
   - **Models** are registered once (`AddModel`): triangles and lines, each vertex carrying its face's normal and a point of the face's plane, and its palette weights. `SceneVS` clips every vertex of a face whose normal points away from the camera, so the face goes whole. Faces are lit by the light's direction, with an ambient of 0.35; lines are drawn as they are, a 128th nearer than their faces so that no face hides them.
   - **A face's items keep the game's painting order.** The game paints a face's triangles and edges one after another, each over those before, and the Dodo's slot is two black triangles painted last on its front face, in its plane. Each vertex carries its item's place on its face, its layer, and `SceneVS` draws layer n as if n / 8192 of its distance nearer. The most items on any face is 19, so a face's last item is lifted under 0.25%; the lines' 1 / 128 stays over all of them.
   - **The composite** (`CompositePS`) draws into the inset: the over layer's pixel where it holds one; else the scene over the under layer by the scene's coverage, in linear light; else the under layer's pixel, or palette colour 0. A layer's pixel covers its share of the inset, nearest.
   - The inset's projection is the caller's, in the picture's source pixels: a focal length across and down, and where the axis meets the inset.
4. **The shell draws the space view** (`Outpost/SpaceView.h`). It registers the 30 ship models by type, with each triangle's pattern mix and each edge's colour, and draws the scene while the platform's view is shown. Each frame:
   - it makes the engine's scene of the last two snapshots: the camera `CameraRotation` at the origin, and each model at `ScenePosition` turned by `ModelRotation`;
   - it draws between them one update late (`BlendFactor` on paced time, `Interpolate`);
   - it gives the inset `ProjectVertices`' projection. That is the view's 256 × 126 pixels from x 32 and line 12, two of the picture's dots to a pixel, and 256 pixels across and 256 lines down for each unit of x / z and y / z. The axis is at the middle of pixel 128 and of the view's row 63, because the game rounds a coordinate towards zero, or to the nearest when it is small, so pixel 128 straddles the axis.
   - It takes the four colours from the CGA's colour select register as the game set it (`Cga::GraphicsColors`), and the light from the sun's position, or from behind the camera where there is no sun.

### What the hybrid draws, and what it does not

| In the view | Drawn by |
|---|---|
| The ships, the station near, the debris of an explosion, the escape pod and the cargo: every object drawn as its blueprint | the scene, between the last two updates |
| The sun, the planet, a far ship's dot and a far station's disc | the under layer, the game's pixels of the latest update |
| The beams, the stardust, the sights | the over layer, likewise |
| The dashboard, the message line, every other screen | the CGA picture, unchanged |

Three artefacts follow from that, and last until the parts that replace each layer:
- A ship behind the sun or the planet is drawn in front of it, because the under layer is under every model.
- The layers are the latest update's, while the ships are drawn up to one update behind (D49): a beam ends where the target was one update later than the target is drawn.
- Where a model's CGA silhouette and its scene silhouette differ, the scene's shows. The under layer holds no pixel a model covered, so nothing of the old silhouette is left.

### What moves on

| Part of D47 or D48 | Arrives with | Why there |
|---|---|---|
| Bloom | the sun and the planet as the scene draws them (D45) | nothing the scene draws yet glows |
| Tearing where the display supports it | the full-window part | the view draws between updates already at the display's rate under sync interval 1; tearing needs a frame cap of its own, and the full window is where it is seen |
| The 2D layer, text and the design units | the HUD | as ADR-031 has it |

### Measured 2026-10-12

| Check | Result |
|---|---|
| Models where the game draws them (`ShipModelTests`) | All 30 types, at 5 object angles, 3 player angles and the 4 views. Each vertex of `ViewRotation · PlayerRotation · ModelRotation` applied to the model, plus the centre, is within 5 units plus 1% of its distance from the centre of where the game's handler and program put it. The worst comes to 4.2 units over the 1%. The 5 units are the game's rounding: its averages shift right and round down, up to 4 units on a vertex built from several, and the Dodo's ring sines are high bytes only. |
| Items layered in painting order | Every face of the 30 models layers its items 0, 1, 2... in the order of its face list, the most on a face 19; the Dodo's slot is layered over its front face's own triangles (`ShipModelTests`). On WARP a patch a layer above a square, in its plane and drawn before it, shows over it (`SceneInsetTests`). |
| Faces seen as the game sees them | 5,318 faces whose test triangle's shortest edge is 40 units or more, seen 0.15 or more from edge on. The model's normal faces the viewer exactly when the game's own winding test on its own rounded vertices draws the face. Nearer edge on, or smaller, the game's rounding decides; a face 0.006 from edge on was seen to differ. |
| The layers explain the screen (`SpaceViewLayersTests`) | Every frame of `combat-orerve.replay` is checked 1 ms after it was published: 417 frames, 40 objects drawn as models. Every pixel of the view is its over layer's, a model's or its under layer's, and the screen is what the CGA shows of it. A pattern drawn through the game's own `PresentSpaceView` lands in under at its pixel. Each of three wrong mappings (the buffer row, the pixel order in a byte, the line's bank) fails that test. |
| The same over the corpus | A scratch copy of the test, never committed, played all 26 replays the same way. 7,514 frames were shown and checked, with 6,113 objects drawn as models; every pixel was explained. Ten frames were not shown a millisecond after they were published: each was the last before a jump, a death or a screen of its own. |
| No fall back mid-flight | Over the corpus, 7,505 frames were followed by the next. A frame still shown was followed within 51 ms, but for one followed after 1,085 ms with the view still in place. A frame hidden before the next was followed after 1,118 ms or more, with a jump, a death or a screen between. So the test holds that a frame followed within 250 ms is never hidden before the next. An earlier form of the rule, "a frame the next continues", was wrong: a mis-jump into witch space runs inside the same flight loop, so the camera can continue across it. |
| The camera | `CameraRotation` is `ViewRotation` after `PlayerRotation`; the left view sees the player's ahead on its right; `ScenePosition` turned by the camera is the view position, within a unit, in every view (`FlightSnapshotTests`) |
| The shaders | `SceneVS`, `ScenePS` and `CompositePS` compile with DXC 1.8 under HLSL 2018 rules, as scratch checks; FXC, which the build uses, runs in CI |
| Compiles | `Engine/Renderer.cpp`, `Outpost/SpaceView.cpp`, `Outpost/Application.cpp` and `Tests/EngineTests/SceneInsetTests.cpp`, syntax only, with mingw-w64's g++ 13 and its Windows SDK headers, `-Wall -Wextra -Wconversion -Wpedantic -Werror`: clean, with stand-ins for `CompiledShader/` |
| clang-tidy | the root `.clang-tidy` with clang-tidy 18 on every changed file: no finding |
| The turn probe | each test's sequence of idle and working turns identical to `main`'s |
| The suite | `GameLogicTests` passes under g++ `-O2` and clang++ 18 with every digest, picture and outcome unchanged |
| Not run here | the MSVC build, `SceneInsetTests` on WARP, and the game: CI builds the solution and runs `EngineTests`, and the owner plays it |

## What this forecloses

- **A copy of a blueprint's numbers.** A model is read from the game's own handler and face list each time it is made; a change to a blueprint changes its model.
- **Culling a ship's triangles by winding.** A face is culled whole, by its test triangle's normal, as the game culls it.
- **Drawing a model at its slot's position.** It is drawn where the frame drew it, which for a far station is not where it is.
- **The new view drawing over a screen that is not flight.** It draws only while the screen shows the last frame of flight, byte for byte.
