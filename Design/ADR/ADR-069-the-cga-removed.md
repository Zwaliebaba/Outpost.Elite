# ADR-069 — The CGA removed: the tunnels drawn by the renderer, D01 and D08 in flight, the pictures retired

**Status:** accepted 2026-10-18, with the change that implements it. It follows the owner's rulings:
- D124, the charts sharing the selected system;
- D125, F8, F9 and F10 in flight;
- D126, the tunnels and the hyperspace rings in a design of this ADR's;
- D127, the CGA removed and the pictures retired.

## Context

By piece 5 of Phase 6 every docked screen, both charts, the cockpit and the space view were the shell's (ADR-058 to ADR-068). The CGA was still emulated under them:
- 16 KiB of video memory in the GameState, its registers in `Cga`, its font in `CgaFont`;
- the dashboard drawn into that memory each frame, the message line and the title's `---- E L I T E ----` too;
- the docked frame, the status, inventory and price screens printed on its text page under the views that replaced them;
- the station tunnel and the hyperspace rings, which no view drew, shown as the CGA's picture (D68);
- `PictureDigest`, a hash of video memory and the registers, recorded beside every state digest (D33) as `picture` lines and in `KnownPictures.txt`.

In flight, F8 and F9 still printed the market prices and the status on the text page, and F10 the inventory, which no view drew (D96 retired F10 docked).

The owner found the charts disagreeing on the jump's target, and asked for the tunnels and the CGA to go.

## Decision

1. **The charts share the selected system (D124).** A chart opens with its cursor on the selected system, where the chart shows it, and shows it as the target. Closing a chart selects the system at the cursor only when the cursor moved: a click, Space, D or Find. The legend box goes from both charts.
2. **F8 and F9 in flight show D01 and D08, F10 nothing (D125).**
   - F8 publishes the `MarketView` of the system with `flight` set (`FlightMarketViewOf`). The shell draws D01's table and hold, no selected row, no selected panel and no trade, and `IN FLIGHT · PRICES ONLY · DOCK TO TRADE` as its footer.
   - F9 publishes the `StatusView` with `flight` set.
   - Both draw `IN FLIGHT · <SYSTEM> SYSTEM` in the top bar and the flight's key bar, F1 FRONT, with ESC DISC and F3 EQUIP off. A click on the bar sends the flight's key, as on a chart in flight (D110).
   - F10 neither shows a screen nor closes one in flight. `flight-screens.replay` presses it on the status screen, which now stays; `TwinWitchScreens` takes F9 where it took F10.
3. **The tunnels are the renderer's (D126).** A tunnel's frame is published with `Host::PublishTunnel` and a `SnapshotTunnel`: its kind, its centre, and up to ten shapes, each a half width, a half height and a colour.
   - The station's are the rectangles the game drew in the frame (`StationTunnelOf`), from `tunnelRectangles`.
   - The hyperspace's are the rings `DrawHyperspaceRings` drew, which it now returns.
   - The shell draws them as strokes, which bloom, on black with no sky:
     - each rectangle's edges, wider the nearer it is, with rails along the corridor's corners to the next rectangle and, past the last, three times as far out, across the wider window (Hor+, D50);
     - each ring as a loop of 48 strokes, wider as it grows.
   - The layers hold nothing of a tunnel's frame (`SpaceViewLayers::FinishTunnel`).
4. **The CGA goes (D127).**
   - **The state.** Video memory leaves the GameState, with the text page's attribute and the working texts only the printed screens read: the fuel's digits, the missiles' digit, the price text, the selected menu row and laser type. The state writes 14,322 bytes.
   - **The display.** `Display` keeps what is left: the frame clock, whose retrace rule (ADR-008 item 2) paces the frames exactly as `Cga::ReadStatus` did, and the colour select, which names the space view's colours and the fuel leak's red. `DisplayPalette` holds the 16 colours.
   - **The routines.** These go, and the screens' rules, waits and keys stay:
     - the dashboard's pixel routines; `UpdateDashboard` keeps its rules and the cache the snapshot reads;
     - the message line's and the title's pixels;
     - the text page's printers and the docked frame;
     - the chart frame's and the space view's copies to video memory; `PresentSpaceView` hands the drawing buffer to the layers (`Host::PresentSpaceView`).
   - **The tables.** The cockpit image, the frames' texts, the status labels and the dashboard's bitmaps go with them.
   - **The digest.** `GameStateDigest` hashes the state and the colour select.
   - **The pictures.** `PictureDigest`, the `picture` lines and `KnownPictures.txt` are retired. The outcomes stay the guard (D33).
   - **The shell.** It no longer presents a CGA picture: between the screens it draws, it presents the last canvas again.

## How it was measured

Every digest point of the corpus and the twins was dumped before the change, on `775db49`, and after it (`OUTPOST_ELITE_DUMP_STATE`), and compared member by member. Nothing differs but:
- the members removed;
- the working seeds at the five points D124's sync moved, re-recorded with it.

The digests were then re-recorded. Every outcome holds, but the one F10 step of `flight-screens.replay`, which now leaves the status screen shown (`inFlight=1`).

| Measure | Value |
|---|---|
| Combat replay's frames | 498 in all, 70 of them tunnels', holding 308 shapes |
| The state | 14,339 bytes before the working texts went, 14,322 after |
| Lines | 7,101 removed and 1,067 added before this ADR, 2,004 of the removed the cockpit image |

The suite passes, 167 tests under g++, on two runs in check mode. `DisplayTests` covers the retrace rule and the colour select; `SpaceViewLayersTests` holds every tunnel frame to the snapshot's shapes. `CheckFormat` and `CheckProjectFiles` are clean. g++ (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`) is clean on `SpaceView.cpp`, `DockedMarket.cpp` and `DockedStatus.cpp`; the D01 and D08 flight frames were drawn through the portable canvas and checked by eye.

| Not run here | the MSVC build and clang-tidy 22, which CI runs; `Application`, which only Windows compiles; the tunnels on the renderer, and `D01-Market-InFlight` and `D08-Status-InFlight` on WARP, whose images CI draws; the game played |
|---|---|

## What this forecloses

- The renderer keeps its picture path (`Present` with pixels, `PlacePicture`, `PresentPS.hlsl`). Nothing in the game or the shell draws into it now, and the shell creates it 1 by 1. Removing it reshapes the renderer's root signature, its descriptor heap and four test suites, which cannot be built here; it is left to its own change.
- The state no longer holds what the player saw. A change that moves only the picture is caught by the shell's golden images, not by the digests.
- The tunnels' look is this ADR's, for the owner to replace.
