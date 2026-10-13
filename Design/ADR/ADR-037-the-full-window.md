# ADR-037 — The space view, third part: the full window, Hor+, the renderer's stardust and tearing

**Status:** accepted 2026-10-13, with the change that implements it, the third part of piece 3 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It carries out D50, D67 and the owner's ruling D69, and tearing from D47 and D60. It closes the rows of [ADR-034](ADR-034-the-cockpit-hud.md)'s and [ADR-035](ADR-035-the-scene-draws-the-sky.md)'s tables of what moves on that name the full window.

## Context

After ADR-035 the renderer drew the scene in the cockpit's window only over the 1987 field: 256 × 126 pixels stretched over 1,304 × 770 design units (`Cockpit::VIEW`). The window round it, the trapezoid between the side panels, the top bar and the console, is wider: from x 186 to x 1734 at its top and from x 122 to x 1798 at its bottom. D50 ruled the 1987 vertical field of view kept and more shown at the sides (Hor+). At today's scale the field widens from |x / z| ≤ 0.5 to about 0.65, about 36 of the view's pixels each side.

Three things stood in the way.

- **The game draws nothing beside its view.** `ClassifyViewPosition` marks an object visible only while |x| and |y| are at most z / 2, and `TransformAndDrawObjects` draws only what it marks. Horizontally that cone is exactly the view's 256 pixels. A ship beside the view gets no model, no dot, nothing.
- **1987's stardust exists only in its field**, as 30 points of the view's pixels.
- **Nothing tears yet.** The swap chain presented with vertical sync.

The owner ruled:

- **D67 (2026-10-13).** The stardust is the renderer's own: 3D particles over the whole window that move with the ship's speed and turns, in place of 1987's 2D dust. The dust is no part of the game's state, so no replay or outcome changes.
- **D69 (2026-10-13).**
  - **Beside the view, the renderer draws what the game would**, by 1987's own rule: a model, or a dot where the game's level of detail says a dot. What 1987 draws only as its own pixels, a distant station's disc for one, stays in the 1987 field.
  - **Frames tear wherever the system supports it**, with no vertical sync, capped at the display's refresh rate. On a display without variable refresh, tear lines show; the owner accepted that.

## Decision

1. **The snapshot carries 1987's rule for each object** (`FlightSnapshot`, `Scene`):
   - whether it is in the view's cone this frame, which the game draws (`SnapshotObject::inView`, `InView`);
   - how `DrawObject` would draw it were it in the cone, from what it decides by and writing nothing (`SnapshotObject::drawing`, `DrawingOf`):

     | Object | Drawing |
     |---|---|
     | The sun, the planet | none: the renderer's spheres |
     | A station at depth 0, or at depth 1 with its compass z below 1B58h | a model |
     | Any other station | none: `DrawDistantStation`'s disc, the game's own pixels |
     | An object out of range this frame (`ShipWithinRange`), or a flashing one in its off frames | none |
     | One whose size (+3Eh) passes its level of detail (+3Fh) | a dot |
     | Any other | a model |

   - whether it flashes (`SnapshotObject::flashes`). `FlashedOff` counts a flashing object's frames only as the game draws it, so one beside the view keeps its state;
   - the player's velocity (`velocityX`, `velocityY`, `velocityZ`), what every object moves back by in a frame, and whether the frame drew the stardust (`stardust`): a frame of flight, not the title's, a frame of words or a tunnel's.
2. **An object the frame did not draw is placed by its own coordinates** (`WorldPosition`). `ScenePosition` reads the slot's view position, which the game stores only while the object passes the near clip, so it is stale behind the camera.
3. **The scene fills the window** (`Cockpit::WINDOW`, `SpaceView::Frame`):
   - **the inset** is the frame's band from the top bar to the console, 1,920 × 770 design units, behind the side panels, which cover its edges;
   - **the projection** is the view's at the view's scale, its axis where the 1987 view's middle is;
   - **the layers**, the game's own pixels, lie over the 1987 field alone (`SceneInset::layersLeft` and the rest, `CompositePS`). Beside them are only the scene and the sky;
   - **the strokes** are in the layers' pixels wherever the layers lie (`Stroke.hlsli`), so a stroke may run off them.
4. **The renderer draws beside the view what the game would** (`SpaceView::SceneOf`, `TakeSideDots`):
   - a model for each object out of the cone whose drawing is a model;
   - a dot for each whose drawing is a dot, as `DrawObjectAsDot` draws one: three pixels across and a row down, in colour 3, under the models.

   Everything the game drew in its cone is still the game's: its models, and its own pixels in the layers.
5. **The stardust is the renderer's** (`SpaceView::MoveStardust`), D67:
   - **its motes** are 1,650, fixed in the scene's axes, in a cube round the camera 2,048 units from its middle to a face, and each wraps round it;
   - **they move back by the player's velocity** once an update, in paced time, at most four updates at once, so a stall does not sweep them away;
   - **each is drawn from where it is to where it was an update before**: a dot at most speeds and a streak on the jump drive, whose velocity is 32 times the speed. It is drawn in 1987's colour of the dust, fading over the furthest 30% of the cube; one nearer than 64 units is not drawn;
   - **they stand still and are not drawn** while the frame draws no dust: the title, the credits, the pause screen and the tunnels.

   1987's dust is not drawn. The layers still give its pixels to the renderer, as ADR-035 has it, and the renderer draws its own dust in their place.
6. **A tunnel's frame shows no sky** (`SpaceView::Frame`). Its picture is black round its rings, and the window beside it is too.
7. **Tearing** (`Renderer`), D69, wherever DXGI reports `DXGI_FEATURE_PRESENT_ALLOW_TEARING`:
   - the swap chain is created with `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` and resized with it;
   - each frame is presented with sync interval 0 and `DXGI_PRESENT_ALLOW_TEARING`;
   - frames are paced to one a refresh at most (`PaceToRefresh`), after the swap chain's frame latency of one is waited on. The refresh rate is the current display mode of the monitor the window is on, from `IDXGISwapChain::GetContainingOutput` and `EnumDisplaySettingsW`, looked up each second, and 60 Hz if it reports none. A high-resolution waitable timer waits for the next refresh, or an ordinary one before Windows 10 1803.
   - Where tearing is not supported, frames are presented with vertical sync as before.

### What moves on

| Part | Arrives with | Why there |
|---|---|---|
| A distant station's disc, and anything else the game draws only as its own pixels, beside the view | when the renderer draws them | D69 leaves them to the 1987 field |
| The game's words in a wider view | as they are | they stay at 1987's places, at the view's middle (ADR-036) |
| A frame cap on a display with variable refresh above its range | the owner's rulings on settings | the cap is the display mode's refresh |

### Measured 2026-10-13

| Check | Result |
|---|---|
| The rule is the game's, in the view | A scratch copy of `SpaceViewLayersTests` played all 26 replays, checking each frame still on the screen a millisecond after it was published. Of every object in the view's cone that did not flash, `DrawingOf` said a model exactly where the game drew one: 7,001 models, with 151 more it called dots. A flashing object's count has moved on a frame by the time the snapshot is taken, which failed `mask-mission.replay` until flashing objects were left out; they are not held to it. |
| The rule beside the view | The same sweep followed the 128 objects that came into the cone from beside it. Of the 52 already in a snapshot the frame before, each was drawn as a model on coming in exactly where the rule had had it as one beside the view: 51 models, and one it had as none, drawn as none. Of the 76 new in the frame before, 7 agreed too; the rule had the other 69 as none, the game not having classified them yet, and the game drew them as models a frame later. 1987 draws a new object no sooner in its own view. |
| Where an object the frame did not draw stands | Over the same sweep, `WorldPosition` was within 2.3 units of `ScenePosition`, and within 1.1% of the object's distance, for every object the game drew as a model. |
| The committed tests | `EachFrameExplainsTheScreen` (`combat-orerve.replay`) holds the rule to every frame. `ShipsComeIntoTheViewAsTheRuleForesaw` (`witch-space.replay`) holds it to the 13 ships there that come into the view. Making every object a model, or every object none, fails them. Dropping the range test, or making a distant station a model, fails neither: no replay brings an object out of range into the view, or has a distant station's disc in it. Those two branches are `DrawObject`'s own tests, read off its code. |
| The stardust's density | 1,650 motes in the cube, 300 times at random: 29 in the 1987 field at once, near 1987's 30, at a mean depth of 1,470 units, near the 1,280 at which a mote grows as 1987's dust does at speed 40. |
| The layers' place (`SceneSphereTests`, CI) | `TheLayersLieWhereTheInsetPutsThem`: the layers in the inset's middle, their corners' pixels at its corners, the under layer within them and nothing beside them, and a stroke on their grid. Written for WARP; it runs in CI. |
| The shaders compile | `CompositePS`, `StrokePS` and `StrokeVS` with DXC 1.8 under HLSL 2018 rules, as scratch checks; FXC runs in CI |
| The turn probe | each test's sequence of idle and working turns identical to `main`'s |
| The suite | `GameLogicTests` passes under g++, 166 tests |
| Compiles | the engine's, the shell's and the tests' changed files, syntax only, with mingw-w64's g++ 13 and its Windows SDK headers, `-Wall -Wextra -Wconversion -Wpedantic -Werror`: clean |
| Not run here | the MSVC build, the WARP test, tearing, the side ships, the dust and the game: CI builds and runs the tests, and the owner plays it |

## What this forecloses

- **A ship that vanishes at the 1987 field's edge.** Beside the view, the renderer draws it as the game would.
- **1987's 2D stardust.** The dust is the renderer's, in 3D (D67).
- **Vertical sync where tearing is supported.** Frames tear there, paced to the refresh (D69).
- **A wider field of view than the window's at today's scale.** The vertical field is 1987's, and ships are the size they were (D50).
