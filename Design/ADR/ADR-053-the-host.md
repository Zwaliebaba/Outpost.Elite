# ADR-053 — The Host: one handle on the clock and the devices, and no Hardware

**Status:** accepted 2026-10-14, with the change that implements it, part of step 7 of Phase 7 ([Reverse-Engineering-Plan.md §5, Phase 7](../Reverse-Engineering-Plan.md#phase-7--wholly-native)), under the owner's ruling D90.

## Context

A routine that waited or drove a device was given `Hardware`, a typed boundary (ADR-014) that named each operation for the PC's port or service and passed it to the `Platform`. Since the ports and services went (ADR-041 to ADR-052), most of its members were one line each, and the `Platform` held everything they reached. The program and the two interrupt handlers reached the game through `Guest`, `NativeEntry` and `InstallNativeRoutines`, which stood where the original's entries were. D90 ruled that one `Host` holds the clock and the waits, the keys, the mouse, the sound, the CGA and the files, and is passed where `Hardware` was.

## Decision

1. **`Host` is the `Platform`, renamed** (`Host.h`), with the few members of `Hardware` that did more than forward: starting and stopping the tick, the end of an interrupt, taking a key, and the end of a space-view object the renderer draws. Every routine that took `Hardware& _hardware` takes `Host& _host`.
2. **The devices are called by their own names.** The game sets the tone on `Sound()` and the mode, colour select, mode control and cursor on `Video()`, and reads the status there; the mouse, the files and the file stamp are the Host's. A beam is noted as the `SnapshotBeam` it is, and a pacing point's milliseconds are spent as they are.
3. **`Game` installs the program.** `Start` is the Host's entry, and `TimerInterrupt` and `KeyboardInterrupt` its tick's and keys' handlers; `Guest`, `NativeEntry`, `NativeRoutines` and the subsystems' entry lists go.
4. **`Hardware` goes**, and so do the test rigs' `Native()`; `PlatformRig` and `PlatformTests` are `HostRig` and `HostTests`.

## What moved, and how it was measured

Nothing: every digest, picture and outcome is as recorded, and the suite passes in check mode with no re-base and no recording, 158 tests under g++.

| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |
|---|---|

## What this forecloses

- **A second handle on the devices.** A routine reaches the clock and the devices through the Host it is given, and nothing else.
- **Entries by address.** The game's program and handlers are what `Game` installs, not a table of the original's entries.
