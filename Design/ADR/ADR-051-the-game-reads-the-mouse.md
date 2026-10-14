# ADR-051 — The game reads the mouse: no int 33h

**Status:** accepted 2026-10-14, with the change that implements it, part of step 7 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D77.

## Context

The game reached the mouse through three int 33h calls: reset, the buttons and the motion. `Hardware` modelled the driver behind them: a flag for whether one was loaded, which the game's `IsMouseDriverInstalled` read where the original tested the vector, and with none loaded the ROM's IRET, which left AX as it went, 5, so a mouse game with no driver would fire for ever. A test could load a driver with no mouse behind it, whose calls threw. The disc menu offers the mouse only when a driver is found, so with the driver gone neither of those paths is one the game can take.

## Decision

1. **`Mouse` is the host's device and the game's input**: the host attaches it (`Desc::mousePresent`, `SetPresent`), moves it and sets its buttons; the game reads `Present`, `Buttons` and `Motion` and calls `Reset`, through `Hardware::MouseDevice`. The motion keeps its 16-bit wrapping counts, which `ReadMouseSteering`'s arithmetic reads.
2. **Whether a mouse is attached is the one flag.** `IsMouseDriverInstalled` returns `Present()`; the separate driver flag, `SetMouseDriverLoaded`, `CallAbsentMouse` and the IRET's AX of 5 go. A mouse that is not attached holds no button and never moves.
3. **The unread button read goes.** `ReadMouseSteering` read the buttons into `mouseButtons` and nothing read them (change 3c); it no longer asks.
4. **The tests follow.** The disc-menu twin attaches a mouse where it loaded a driver with none behind it; `TheRomsIretServesAVectorNothingHooks` goes with the IRET it tested.

## What moved, and how it was measured

Nothing: every digest, picture and outcome is as recorded, and the suite passes in check mode with no re-base and no recording, 165 tests under g++.

| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |
|---|---|

## What this forecloses

- **A driver apart from the mouse.** There is no state in which the game finds a driver and no mouse, or a mouse and no driver.
- **Nothing yet for the shell.** The shell has never attached a mouse: it leaves `mousePresent` false, so the disc menu answers that there is none. Feeding Windows' mouse to `Mouse` is the shell's work, still to do.
