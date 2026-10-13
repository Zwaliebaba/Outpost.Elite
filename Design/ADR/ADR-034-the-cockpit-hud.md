# ADR-034 — The cockpit: the canvas frame and HUD round the 1987 view, the 2D layer and its text

**Status:** accepted 2026-10-12, with the change that implements it, the second part of piece 3 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It carries out D39, D51, D55 and the rest of D47's 2D layer and text, and part of D48, under the owner's ruling D63. It is the HUD's ADR that D39 and D51 ask for. It amends [ADR-031](ADR-031-the-renderer-first-piece.md)'s table of what D60 moved: the 2D layer, the text and the design units arrive here.

## Context

The owner played [ADR-033](ADR-033-the-space-view-hybrid.md)'s space view and ruled the next part: the canvas's cockpit (`Design/Canvas/cockpit-hud-overlay/`) and HUD in place of the CGA dashboard, full-window. Three things stood in the way.

- **The view's width.** The canvas window is about 2.2:1, and D50 asks for Hor+. But the sun, the planet, the stardust, the beams and the sights are still the game's CGA pixels, which exist only in the 1987 field of view. A wider view would show ships at its sides with no sun, planet or dust, which would pop in at the 1987 edge.
- **Every other screen** is still the CGA's until piece 5: the tunnels, the jumps, the charts, the docked screens and the title.
- **What the canvas does not show** (D54): the message line, the ECM's E, the missile's armed and locked states, and the other views.

**The owner's ruling D63 (2026-10-12):**
- **The 1987 field in the frame.** The canvas frame and HUD now, with the view at its 1987 field of view, scaled up and centred in the frame's window, the strips beside it dark. Hor+ comes once the scene draws the sun, the planet, the dust, the beams and the sights.
- **The CGA picture, full, outside flight**, as now: the cockpit appears only in flight, and the switch between them is visible.
- **Placeholders, which the owner redraws later**, for D54's items, in the canvas's own style. The message line is the top bar's text, the view's name reads REAR, LEFT or RIGHT VIEW there, the E is a box beside the S, and a missile box is outlined while armed and filled while locked.

## Decision

1. **The flight snapshot holds the dashboard** (`SnapshotDashboard`, `GameLogic/FlightSnapshot.h`). It is what `UpdateDashboard` drew, as it left it:
   - the bars' bytes, the speed, the roll and the pitch, the energy, the missiles and their state, and the condition;
   - the S and the E, from their cached glyphs;
   - the compass's dot, from the station's blip bytes;
   - each ship's scanner blip, from its slot;
   - the message line's text: the message shown, which is the view's name when none is up.

   The game's message line is the canvas's top-bar title, so the title is the message line.
2. **The gauges are a table in the game** (`GameLogic/Gauges.h`, D51). Bars and numbers are percent of each value's range, fuel's number is in light years, and the missiles are a count. The levels are the condition light's (`UpdateConditionColor`), and the laser's is where it will not fire.

| Gauge | Field | Range | Number | Warning (yellow) | Danger (red) | Critical (flashing) |
|---|---|---|---|---|---|---|
| Fore shield, aft shield | `foreShield`, `aftShield` | 0–FFh | percent | below 80h | 0 | — |
| Fuel | `commander.fuel` | 0–FFh | light years, fuel / 36 to the tenth below | — | — | — |
| Cabin temperature | `cabinTemperature` | 0–FFh | percent | from 80h | from C0h | from E0h |
| Laser temperature | `flight.laserTemperature` | 0–FFh | percent | — | from F0h, where it will not fire | — |
| Altitude | `altitude` | 0–FFh | percent | below 80h | below 28h | below 20h |
| Speed | `playerSpeed` | 0–30h | none | — | — | — |
| Roll, dive and climb | `rollRate`'s low and high bytes | −23 to 23 | none | — | — | — |
| Energy 1 to 4 | `playerEnergy`, a bank each FFh, filled in turn | 0–3FFh | none | below 300h | below 200h | below 100h |
| Missiles | `missileCount`, `missileState` | 0–4 | count | armed: outlined yellow | locked: filled red | — |
| Condition | `conditionColor` | green, yellow, red, flashing | — | | | |

   A normal gauge is the canvas's amber, a warning yellow and danger red; critical flashes red on bit 9 of paced milliseconds, as the condition light flashes.
3. **The 2D layer** (`Engine/Canvas.h`, D47, D48). A canvas is a list of shapes and lines of text in 1920 × 1080 design units, placed at one scale, min(W / 1920, H / 1080), and centred (`PlaceDesign`).
   - **One shader draws every shape** (`CanvasVS`, `CanvasPS`): boxes and their edges, convex quadrilaterals shaded top to bottom, lines, ellipses and rings, dashed or not, and glyphs. Each is an instance over its bounds grown by a pixel. Its coverage comes from its signed distance, smoothed over one screen pixel.
   - **It is drawn through an sRGB view of the back buffer**, colours premultiplied, so they blend in linear light. A capture's target is typeless, with both views.
4. **Text** (`Engine/GlyphAtlas.h`, D39, D55). DirectWrite lays a line out in Bahnschrift, semi-condensed for labels and normal for numbers, both semi-bold. Its figures are tabular (`tnum`, through `IDWriteTypography`) and its letters spaced as the design spaces them, at the screen's own pixel size. Each glyph is rasterised once, in grayscale, into a 1024² 8-bit atlas, and stands on a whole pixel. A line is centred on its capitals' height about its anchor. DirectWrite is the Windows SDK's (`dwrite.lib`, R14). A full atlas starts again empty.
5. **The cockpit** (`Outpost/Cockpit.h`), drawn from a frame's dashboard in the design's colours:
   - the frame: the side panels, the top bar with its notch, and the console with its gold edges;
   - the top bar's title with a rule each side;
   - the gauges: six left, then the missiles; speed, roll, dive and climb, and four banks right;
   - the scanner, each blip where `XorScannerBlip` puts it, its stick of height to it, red when hostile;
   - the condition light and the compass's dot, solid when its target is ahead and a ring behind;
   - the S and, while the ECM fires, the E.

   **The view** is placed in the frame's window (`Cockpit::VIEW`): the 1987 field, 256 × 126 pixels at the CGA's 4:3, about 1.69:1, 1304 × 770 design units from (308, 40). `SpaceView` stretches the view and its projection over any place, in the picture's dots for the title or in design units here.
6. **The application** draws the cockpit while the screen shows a frame of flight or of the title, and the CGA picture otherwise.

   *Amended 2026-10-12 by D64, after the owner's play-test: at first the title kept the CGA picture with its ship over it. The 1987 title is the cockpit screen (`RunTitle` shows screen layout 2), so it is drawn in the cockpit too. Its dashboard is the cockpit's picture, which `UpdateDashboard` never draws over there: `TitleDashboard` marks it not live, so `GaugesOf` gives every bar empty and every number blank, and the cockpit draws no indicator marker, no missile count and the condition light dark. The top bar reads ---- E L I T E ----, which 1987 writes on the message line; Press any key stays the game's pixels in the view. With nothing left to draw over the CGA picture, `SpaceView::PICTURE_PLACE` goes.*

### What moves on

| Part | Arrives with | Why there |
|---|---|---|
| Hor+ (D50) | the scene drawing the sun, the planet, the dust, the beams and the sights | until then the CGA layers cover only the 1987 field. *ADR-035 draws them; Hor+ comes with the full window, next. Since ADR-036 the game's words are drawn over the cockpit, and the credits and the pause screen are in it (D66).* |
| Bloom | the sun and the planet as the scene draws them (D45) | nothing the scene draws yet glows. *Arrived with ADR-035.* |
| Tearing | the full window | as ADR-033 has it |
| The message line's, the ECM's, the missiles' and the other views' own designs | the owner's drawings (D54) | the placeholders stand until then |

### Measured 2026-10-12

| Check | Result |
|---|---|
| The gauges' levels (`GaugesTests`) | 20,000 states of the shields, the energy, the cabin's temperature and the altitude, from a pinned seed: the worst gauge's level is the colour the game's own `UpdateConditionColor` gives the condition light, every time. Moving the altitude's danger level by one unit fails it. |
| The table's ends | full: 100, 7.0 light years, full speed and banks, the indicators held to ±23; 150h of energy fills the first bank and 50h of the second |
| The snapshot's dashboard | every field taken from the state as set: the bars, the glyphs, the compass's dot about its centre (CFh, 27h) and a hostile ship's blip |
| The 2D layer on WARP (`CanvasTests`) | a box's inside and the border outside it, exact; a ring on its outline and not inside; a quadrilateral shaded black to white is BCh at its middle, half the light in sRGB, so it blends in linear light; design placement at 1080p, 21:9 and 5:4; a scene's inset in design units, and the border round it |
| Text on WARP | Bahnschrift installed; "1111" and "8888" the same width, tabular; "88" drawn about its anchor and nowhere else |
| A preview of the layout | a scratch program, never committed, drew `Cockpit::Draw`'s canvas on the CPU with `CanvasPS`'s distances ported, text as boxes: the layout matches `cockpit-hud-01.png`'s |
| The shaders | `CanvasVS` and `CanvasPS` compile with DXC 1.8 under HLSL 2018 rules, as scratch checks; FXC runs in CI |
| Compiles | the engine's, the shell's and the tests' new and changed files, syntax only, with mingw-w64's g++ 13 and its Windows SDK headers, `-Wall -Wextra -Wconversion -Wpedantic -Werror`: clean |
| clang-tidy | the root `.clang-tidy` with clang-tidy 18 on every changed file: no finding. CI's clang-tidy 22 against MSVC's headers then found three that 18 against mingw-w64's cannot see: `Renderer`'s constructor was `noexcept` over a debug `std::map`, the glyph collector's DirectWrite methods lacked the SDK's `nothrow`, and the gauge test seeded `std::mt19937` with a constant. The test now draws from a pinned xorshift32 of its own, and moving the altitude's danger level by one still fails it. |
| The turn probe | each test's sequence of idle and working turns identical to `main`'s |
| The suite | `GameLogicTests` passes under g++ `-O2` and clang++ 18 |
| On CI | the MSVC build, clang-tidy 22, and `CanvasTests` on WARP, Bahnschrift among the fonts the runner has; `Capture` gave its sRGB view a descriptor of its own once the tests showed both views on one |
| The title (D64) | `TheTitlesDashboardIsDark`: every gauge empty, blank and normal, the condition dark, the title on the message line, where the same zeros live would be red. Across the corpus, every frame of the title publishes the title's dashboard and every frame of flight a live one, and none of the title's has a blip; leaving out `RunTitle`'s `TitleDashboard` fails it. The turn probe is unchanged. |
| Not run here | the game: the owner plays it |

## What this forecloses

- **The HUD reading the CGA's video memory.** It is drawn from the snapshot's dashboard, the game's state as the frame left it.
- **A gauge's level of its own.** The levels are the condition light's; a new one is a ruling.
- **Text through Direct2D or D3D11On12.** Glyphs come from DirectWrite into the renderer's own atlas (D47).
- **Hor+ before the scene draws what the CGA layers hold.**
