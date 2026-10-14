# ADR-050 — The video modes are set on the CGA: no BIOS video services

**Status:** accepted 2026-10-14, with the change that implements it, part of step 7 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D77.

## Context

Since ADR-041 the BIOS's int 10h survived as `VideoModes`: set mode for modes 0 to 6, the palette call by read-modify-write of the data area's copy of colour select, the teletype, and the power-on state, mode 3, set on the card and in video memory when the platform was built. The game asks for two modes, 4 in `SetGraphicsMode` and 1 in `SetTextMode`, and the palette once, in `SetGraphicsMode`, which writes colour select itself straight after. The teletype's one caller, the farewell, went with ADR-048.

## Decision

1. **`Cga::SetMode`** sets the card to one of the game's two modes, `CgaMode::Graphics320` and `CgaMode::Text40Columns`, as the BIOS left it: the video off, CRTC registers 0 to 15 from the BIOS's table, then mode control the mode's value and colour select 30h. The game clears video memory itself, zero for graphics and 0720h words for text, in `SetGraphicsMode` and `SetTextMode`, which now take the state.
2. **The palette call goes.** What it wrote to colour select was overwritten by `SetGraphicsMode`'s own write, with nothing between that could read it.
3. **The card powers on clear.** No mode is set when the platform is built: every register and all of video memory are zero until the game's first mode set, as a CGA's registers are before a BIOS programs them. No digest reads the state before that first set.
4. **`VideoModes` goes**, with `Hardware::SelectPalette`, the mode number's range check, and `VideoModesTests`; `CgaTests` checks both modes' registers.

## What moved, and how it was measured

| What | Result |
|---|---|
| Digests, pictures and outcomes | none moved: the suite passes in check mode with no re-base and no recording, 166 tests under g++ |
| The start state | its video memory is zero where it was 0720h words; its hash, which covers the members and not video memory, is unchanged |

| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |
|---|---|

## What this forecloses

- **The other BIOS modes.** 80-column text, 640x200 and the monochrome-burst modes have no way in; `Cga::Render` still draws them, as the card would, should a register be written so.
- **A picture before the game's first mode.** The card is off until then, and the shell presents nothing before the first frame.
