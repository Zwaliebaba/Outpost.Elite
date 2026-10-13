# ADR-035 — The space view, second part: the sun, the planet, the sky, the dust, the beams and the sights drawn by the renderer

**Status:** accepted 2026-10-12, with the change that implements it, the second part of piece 3 of Phase 6 after the cockpit ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It carries out D45, the rest of D47's bloom, and the owner's rulings D64 and D65. It amends [ADR-033](ADR-033-the-space-view-hybrid.md)'s layers and [ADR-034](ADR-034-the-cockpit-hud.md)'s table of what moves on: bloom arrives here, and Hor+ and tearing stay for the full window.

## Context

After [ADR-034](ADR-034-the-cockpit-hud.md) the cockpit stood round the 1987 field of view, its ships drawn by the renderer and everything else the game's own CGA pixels: the sun and the planet as flat discs, the stardust, the laser beams, the sights. Those pixels exist only in the 1987 field, so the view cannot go wider (Hor+, D50) until the renderer draws them itself. The owner ruled the order (D64): this part moves them into the scene, still at the 1987 field; the next goes full-window.

Where the reference is silent, or where the canvas and the reference differ, the owner ruled:

- **D64 (2026-10-12).** The sun and the planet are spheres whenever any part of them is in view, where 1987 draws a disc only while all of it fits the view. The planet's surface is one of the canvas's two kinds, terrestrial or a banded gas giant, its kind and colours from the system's seed in integer arithmetic, interim until the owner redraws them.
- **D65 (2026-10-12).**
  - **The sights are 1987's four**, one for each laser type, traced in the canvas's amber.
  - **The beams keep 1987's geometry**: its endpoints, pairs, jitter and one frame each. They are drawn bright, tapering and blooming, in 1987's colours.
  - **The sun is 1987's colour**, white hot at its middle, with a corona and bloom.
  - **The title's planet is the current system's**, at 1987's place and size: Lave on a new game.

## Decision

1. **The layers leave to the renderer what it draws** (`SpaceViewLayers`). An object or a drawing the renderer draws in its place is marked as it is drawn:
   - the sun and the planet (`NoteRendered`, in `DrawObject`);
   - the stardust (`UpdateStardust`), each beam (`DrawLaserBeams`, `ApplyEnemyLaserHit`'s), the sights (`FinishSpaceViewFrame`) and the title's planet (`EndSpaceViewRendered`).

   Its pixels are the renderer's, as a model's are, and neither layer holds them. The layers also keep which slot last changed each pixel (`OwnerOf`).
   - **A plain object drawn after one the renderer draws goes over**, not under. The game paints from the farthest in, so such an object is nearer than the sun, the planet or a ship before it. Without the rule, a far ship's dot in front of the planet would vanish behind the planet's sphere. Across the corpus the under layer now holds no pixel: the sun and the planet are drawn first, and every dot after them.
2. **The snapshot carries what the renderer needs** (`FlightSnapshot`), outside the counted state, as ADR-032's does:
   - the frame's dust, as `DrawDust` drew it (`DrawnStardust`): a point, or a streak while the jump drive is engaged;
   - the beams, which the platform notes as they are drawn (`Hardware::NoteSpaceViewBeam`) and hands to the snapshot it publishes;
   - the view's laser type, whose sights are drawn (`GetViewLaser`);
   - a supernova's heat;
   - the system the planet is (`galaxyNumber`, `currentSystemIndex`);
   - the title's planet (`TakeTitleSnapshot`).

   `DrawLaserBeams` takes the `Hardware` for that; nothing of the game's state changes, and the turn probe is unchanged.
3. **The sun and the planet are spheres** (`SphereOf`).
   - **Where:** at the view position the frame gave them, times their scale shift's power of 2, turned back by the camera.
   - **How large:** the radius whose disc `SizePlanet` and `SizeSun` size at 256 view units, 50 × 256 and 100 × 256 units (`PLANET_DISC_SCALE`, `SUN_DISC_SCALE`). While a supernova burns, the sun is the heat's rows at its distance.
   - **The title's planet** stands at its disc's place, at any depth (`TitlePlanetSphere`).
4. **How each system looks** (`PlanetLookOf`, `SkyLookOf`, D45, D64), from its galaxy and number in integer arithmetic:
   - **The planet:** three in eight are gas giants. The palette is one of six terrestrial or four gas-giant palettes, each channel varied by up to 12. The sea level is 35–75% of the surface, the cloud cover 4–31%, the bands 4–12 pairs, the turbulence 27–86%, and there is a seed.
   - **The sky:** one of six nebulae, its density, the stars' density, and a seed.
5. **The renderer draws them** (`Renderer`, `Engine/Shader/`), in the scene's FP16 target with 4× MSAA, in this order:
   - **Spheres** (`SpherePS`). Each is traced through every pixel of the inset over `PresentVS`'s triangle, and writes its own depth, so a ship nearer covers it and one further is covered.
     - A star is white at its middle and its colour at its rim.
     - Terrain is sea below its level and land above, under streaming cloud. Bands run across its latitudes, stirred by noise.
     - The noise is value noise summed over octaves (`Noise.hlsli`), from the sphere's seed, fixed in the scene's frame.
     - A share of the surface maps to the noise value that covers it (`NoiseBelow`).
   - **The light round them** (`SphereGlowPS`): a star's corona, or a planet's haze on its lit side. It is added at the sphere's depth.
   - **The dust**, as strokes (`StrokeVS`, `StrokePS`): capsules in the view's pixels, tapering, smoothed over a pixel. They lie over the spheres and under the models.
   - **The models**, as before.
   - **The beams**, as strokes over everything: 1.8 rows wide at the gun and 0.5 at the end, at 2.5 times their colour's light, so they bloom.
6. **Bloom** (`BloomPS`, D47). The resolved scene's light past 1 is taken at a quarter of its size, then blurred across and down by a Gaussian of nine texels in five taps, two texels a step. The composite adds it.
7. **The sky** is drawn in the composite, behind the under layer, fixed in the scene's frame:
   - **Stars:** a star in each cell of a 96 × 96 grid on each face of a cube round the camera that the seed's hash picks, a pixel or two across.
   - **A nebula:** two colours where the seed's noise rises.
8. **The sights** (`Cockpit`) are the canvas's: 1987's four sprites traced as amber lines and trapezoids, over the 16 × 16 pixels where `DrawLaserSights` puts them.
9. **The shell** (`SpaceView`):
   - **The spheres, the sky and the bloom:** it gives the renderer the spheres, the system's sky, and the bloom at full share.
   - **The dust:** the frame's dust, each particle moved toward where the next frame drew it unless it jumped more than 12 pixels, being born again.
   - **The beams:** the frame's beams, shown for the one update the frame is shown.
   - **A new flight or view:** its own strokes, not the frame before's.

### What moves on

| Part | Arrives with | Why there |
|---|---|---|
| Hor+ (D50) | the full window, next | nothing the view needs is left only in the 1987 field but the game's words, which stay at its middle |
| Tearing | the full window | as ADR-033 has it |
| 3D stardust | the full window | 1987's dust is 2D, in the view's pixels; a wider view needs dust beyond them, which is a change of its own |
| The planets' and the sky's own looks | the owner's drawings | the palettes here are interim (D64) |

### Measured 2026-10-12

| Check | Result |
|---|---|
| The renderer's pixels are the snapshot's | `SpaceViewLayersTests` plays `combat-orerve.replay` and checks every frame 1 ms after it was published. Every pixel the renderer draws that is no slot's lies on a dust point or within 1.5 pixels of a streak or a beam, in the sights' 16 × 16 or within the title's planet. The 1.5 is `DrawClippedLine` clipping an enemy's beam to whole pixels, which moved one by up to 1.2. Leaving out the beams' note, the dust's points, or a sphere's scale shift each fails it. |
| The same over the corpus | A scratch copy of the test played all 26 replays, checking each frame still on the screen a millisecond after it was published: 8,527 frames, 7,126 models, 88,866 points of dust, 2,494 beams, 7,256 frames with sights, every pixel explained. The under layer held no pixel. The copy as committed checks frames already replaced too, which fails eight replays on `main` as well: a death or a jump replaces its last frame within the millisecond. |
| The spheres are where the discs were | Over the same corpus, 1,119 whole discs of the sun or the planet, where no nearer object painted over them and the view did not cut them. Each disc's centre is within 2.45 pixels of its sphere's projection, and its radius within 2.14 pixels: 86% to 100% of the sphere's. A disc of r rows spans 2r − 1 rows and 2r + 1 pixels across, so its radius is the mean of its half-height and half-width. It is smaller than its sphere because `ScaleByInverseDistance` divides by one more than the integer root of the distance, rounds down, and the sun's fringe makes its edge ragged. The committed test holds both within 2.5. |
| A supernova's sun | No replay reaches a supernova's heat; `FlightSnapshotTests` checks the heat's radius, and that the snapshot takes the heat only once `SupernovaHeat` holds the count at 1. |
| The looks | 762 of the 2,048 planets of the eight galaxies are gas giants, near the three in eight aimed at; each system's seed differs; Lave is terrestrial, its seed and its sky's pinned (`PlanetLookTests`). |
| The noise's shares | `Fractal` over 200,000 points of the sphere: heights mean 0.477, spread 0.104; clouds 0.511 and 0.103; near normal. So a share p of the surface lies below 0.49 + 0.26 (p − 0.5), within a few percent from 25% to 75%, as Φ⁻¹(p) ≈ 2.5 (p − 0.5) there. Without it, a cover of 58% clouded three quarters of Lave. |
| What the shaders draw | A scratch port of `SpherePS`, `SphereGlowPS`, the sky and the bloom to NumPy, never committed, rendered Lave near and far, the sun beside it, a gas giant and a terrestrial world close up. It found the clouds above and set the haze's strength at 0.7. |
| On WARP (`SceneSphereTests`, CI) | A sphere where its projection puts it, lit full on at its middle and as large as it projects; a square nearer covers it, and it covers one at its middle. A star white at its middle with a corona past its rim, which a planet has not. A beam along its line over a model, a dot of dust at its pixel, bloom round a bright stroke only when asked for. The sky's stars where the scene and the under layer are empty, and none when it is not shown. |
| The shaders compile | `SpherePS`, `SphereGlowPS`, `StrokeVS`, `StrokePS`, `BloomPS` and `CompositePS` with DXC 1.8 under HLSL 2018 rules, as scratch checks; FXC runs in CI |
| Compiles | the engine's, the shell's and the tests' new and changed files, syntax only, with mingw-w64's g++ 13 and its Windows SDK headers, `-Wall -Wextra -Wconversion -Wpedantic -Werror`: clean |
| clang-tidy | the root `.clang-tidy` with clang-tidy 18 on every changed file: no finding but two the base has too, which clang-tidy 22 in CI does not raise |
| The turn probe | each test's sequence of idle and working turns identical to `main`'s |
| The suite | `GameLogicTests` passes under g++, 164 tests |
| Not run here | the MSVC build, `SceneSphereTests` on WARP, and the game: CI builds and runs the tests, and the owner plays it |

## What this forecloses

- **A disc that vanishes as its edge touches the view's.** The sun and the planet are drawn whenever any part of them is in view (D64).
- **The renderer reading the game's pixels of the sun, the planet, the dust, the beams or the sights.** It draws them from the snapshot.
- **A planet's look from its 1987 description.** The kind and the colours are the seed's (D64).
- **A noise library or a texture of planets.** The surfaces are the shaders' own noise, from the seed, and no art is licensed (D45, R14).
