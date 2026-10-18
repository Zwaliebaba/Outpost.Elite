# ADR-070 — Unused parts removed: the renderer's picture path and the state nothing reads

**Status:** accepted 2026-10-18, with the change that implements it, on the owner's ruling D128. It amends ADR-069, which left the renderer's picture path to a change of its own.

## Context

Once the CGA went (ADR-069), two kinds of code were left that nothing uses.

**The renderer's picture path** (ADR-031). It drew a grid of source pixels at a whole-number scale (`PlacePicture`, D46), with:
- a texture, an upload buffer for each frame in flight and a descriptor;
- a root signature and a pipeline, with `PresentPS.hlsl`, its sharp bilinear filter (ADR-009);
- `Present` and `Capture` taking the pixels and a border colour;
- a scene's inset placed in source pixels when no canvas was drawn.

Since ADR-069 the shell creates it 1 by 1 and never fills it. Only the Engine's tests drew pictures.

**The state.** Thirty members of the game's state are referenced by nothing but their declaration, their visitor and the start state:
- the short-range chart's label placement: `chartItems` and its count and end, `pendingChartLabels`, `chartLabelCursor`, `labelNudgeCount`, `labelsLeftToPlace` (replaced by D04 to D07, ADR-065);
- Data on System's digits: `techLevelTensDigit`, `techLevelUnitsDigit`, `selectedDistanceText`, `populationDigits`, `productivityDigits`, `radiusDigits` (D112);
- the text page's menus: `menuRowCount`, `menuFirstRowAttr`, `quantityText`, `cargoRowPointer`, `tradeScreenIsBuy`, `selectedLaserMount`;
- the typed line and the catalogue the original kept: `inputCursorText`, `cursorBlinkTicks`, `inputCharsLeft`, `commanderFileList`, `commanderFileCount`, `commanderFileListNext` (D10, ADR-067).

Three constants were unused: `INERT_CLASS`, `CARGO_KINDS` and `SLOT_BYTES`.

## Decision

1. **The renderer draws a background, a scene and a canvas.**
   - `Renderer::Desc` keeps `warp` alone.
   - `Present(background, scene, canvas)` and `Capture(background, width, height, scene, canvas)` clear the target to the background, and draw the scene and the canvas over it.
   - A scene's inset is in design units under a canvas, as ever (ADR-034), and in the target's own pixels with none.
   - **What goes:**
     - `PlacePicture`;
     - the picture's texture, upload buffers, descriptor, root signature and pipeline;
     - `PresentPS.hlsl`.
   - The descriptor heap starts at the scene: slots 0 to 5, where they were 1 to 6.
   - **The shared vertex shader.** It draws the one triangle that covers a viewport for the composite, the spheres and the bloom, and is renamed `FullScreenVS.hlsl` for what it does.
2. **The thirty members and three constants go.** `CommanderState` is not touched: its members are the commander file's bytes (D78), whether or not the game reads them. The state writes 13,388 bytes, 934 fewer.
3. **The Engine's tests draw what is left.**
   - `RendererTests` holds the background, each frame in flight's own, and a target of no size.
   - `SceneInsetTests` and `SceneSphereTests` place their insets in the target's pixels, at twice the units they used in source pixels at scale 2, so every pixel they check is where it was.
   - `CanvasTests` and `OutpostTests` lose only the arguments that are gone.

## How it was measured

Every digest point of the corpus and the twins, 322 of them, was dumped before and after the members went (`OUTPOST_ELITE_DUMP_STATE`) and compared key by key. Nothing differs but the members removed. The digests were re-recorded; every outcome holds.

The suite passes, 167 tests under g++, on two runs in check mode. `CheckFormat` and `CheckProjectFiles` are clean.

| Not run here | the MSVC build of the Engine, the shell and their tests; the Engine's and the shell's tests on WARP, which CI runs |
|---|---|

## What this forecloses

- The renderer cannot show a grid of pixels any more. A screen of the original's would come back as a canvas, not as a picture.
- The removed members cannot be dumped or compared against an older tree's dumps without the comparison leaving them out, as this ADR's did.
