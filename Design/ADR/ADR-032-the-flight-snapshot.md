# ADR-032 — The flight snapshot, and drawing between two of them

**Status:** accepted 2026-10-12, with the change that implements it, piece 2 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It carries out D49 under the owner's ruling D61. It amends [ADR-031](ADR-031-the-renderer-first-piece.md)'s table of what D60 moved: tearing moves on to piece 3.

## Context

D49 keeps the game at 20 updates a second and has the renderer draw between the last two. Positions are interpolated and orientations slerped, at the cost of up to one update of latency. The plan's piece 2 is the snapshot that makes this possible: after each update the game fills a neutral record of what it drew, and the engine, which knows no game type (R9), draws between two of them.

Nothing draws a snapshot until piece 3's space view. By D60's rule, a part arrives with what first uses it. A snapshot is data, though, and tests can hold it to the game's state, which they could not do for an empty render target.

Two facts of the game shape it.
- **The camera is the player.** The player has a pitch, a yaw and a roll (`playerPitchAngle` and the rest, 800h to a turn). Every slot's position is relative to the player, on the game's axes. `RotatePitchYawRoll` turns a position into the camera's frame: (y, z) by the pitch, then (x, z) by the yaw, then (x, y) by the roll. The view (`viewAngle`) then turns it to the front, left, rear or right.
- **Slots are reused in place.** `RemoveObject` clears a slot's active bit and nothing moves. The next spawn takes the first free slot. A slot's number alone therefore does not say whether two snapshots hold the same object.

**The owner's ruling D61 (2026-10-12):** piece 2 lands as data and tests, and nothing on screen changes. Whether a slot holds the same object as the snapshot before is inferred in the snapshot, not stamped by the game where it activates a slot.

## Decision

1. **`FlightSnapshot`** (`GameLogic/FlightSnapshot.h`) is what one frame of flight drew:
   - the player's pitch, yaw, roll and view;
   - the player's rotation as a unit quaternion (`PlayerRotation`);
   - every active slot in slot order: its number, its kind (type bits 1-5), its 24-bit position sign-extended, and its three angle words as the slot holds them.

   It is in the game's units. Making the engine's scene of it is the space view's job: the conversion of an object's angles to a rotation needs the blueprint's own rotation, which piece 3 draws and can be seen to be right.
2. **It is taken once a frame, where `RunFlight`'s frame is finished,** after `FinishSpaceViewFrame` and before `UpdateObjectsAndSpawn` moves anything. So it holds what that frame drew. `Hardware::PublishFlightSnapshot` hands it to the `Platform`, which:
   - numbers it;
   - stamps it with its clock, paced time;
   - keeps it and the one before (`LatestFlightSnapshot`, `PreviousFlightSnapshot`).

   It lives outside the counted state. The capture writes no byte the digests hash and no port, and it is not a loop's turn, so nothing the guard sees moves. The launch and docking tunnels, the hyperspace jump and the title draw their own frames outside this loop, and publish none. The space view decides what to show there.
3. **Continuity is inferred** (D61), in `ContinueFrom`, when the platform publishes a snapshot. The first frame of each flight continues nothing.
   - **The same object** is the same slot holding the same kind, no further on any axis than `MOST_STEP_PER_FRAME` = 4000h.
   - **The camera continues** when the view is the same and the player turned no more than `MOST_TURN_PER_FRAME_RADIANS` = 0.5 rad. The turn is measured as the true angle between the two frames' rotations, not angle by angle: near a pitch of a quarter turn, the yaw and roll words flip by half a turn while the ship barely moves.
4. **`PlayerRotation` is the game's rotation.** It builds `RotatePitchYawRoll`'s three plane turns as `RotateBySinCos` turns a plane, and converts the product to a quaternion. It is presentation only, in floating point, and nothing in the game reads it back, so the deterministic core keeps its integers (R16).
5. **`SceneSnapshot`** (`Engine/SceneSnapshot.h`) is the engine's record: a time, a camera position and orientation, and objects with a key, a model number, a position, an orientation and whether each continues. It names no game type.
   - **`Interpolate`** draws between two snapshots. It moves a continuing camera and each continuing object whose key the earlier snapshot holds, positions linearly and orientations by `Slerp`, the short way round. Everything else is as the later snapshot has it.
   - **`BlendFactor`** places a frame one update late: at the later snapshot's time it shows the earlier, an update on the later.

### What D61 leaves to piece 3

The plan's piece 2 listed more than this. Each item arrives with the space view or the HUD that first draws it:
- the sun's and the planet's size, and the station's appearance, read from the slots' words above;
- particles, lasers and stardust;
- the HUD's values (D51);
- the conversion of a `FlightSnapshot` into a `SceneSnapshot`, with the objects' rotations;
- tearing, which D60 put in piece 2 because frames would be drawn between updates. With nothing drawn yet, presenting at sync interval 0 would only spin, so it moves to piece 3 too.

### Measured 2026-10-12

A scratch probe, never committed, hooked every byte write of the counted state to count when a slot's type byte went from inactive to active: the true start of a new object. It then ran the whole `GameLogicTests` suite, the 26 replays of the corpus and every twin, through g++ -O2, and compared each pair of consecutive snapshots.

| Check | Result |
|---|---|
| Slot pairs in consecutive snapshots | 45,126; 45,075 the same object and 51 a new one |
| The same object, farthest step in a frame on any axis | 1,491. `MOST_STEP_PER_FRAME` is 4000h, 16,384 |
| A new object of the same kind in the same slot | 39; the nearest 115,712 from the old one. None would be taken for the same |
| A new kind in a slot whose object did not change | 0 |
| Camera pairs | 9,771, of which 14 change the view |
| The player's true turn in a frame | at most 0.21 rad in 9,742 pairs; then 15 from 1.41 rad to π, the game setting the player's angles outright (docking, a half turn). `MOST_TURN_PER_FRAME_RADIANS` is 0.5 |
| The same turn measured angle by angle | up to 400h, half a turn, in about 1% of pairs, which is why the angles are not compared one by one |
| `PlayerRotation` against `RotatePitchYawRoll` | within 0.2% of a 2000h vector, at 343 combinations of angles round every axis (`FlightSnapshotTests`) |
| The turn probe | each test's sequence of idle and working turns, 73,089,965 turns in all, identical to `main`'s |
| The suite | 147 tests pass under g++ `-O2` and clang++ 18 with every digest, picture and outcome unchanged; `SceneSnapshotTests`' three pass under both on Linux; MSVC and WARP run in CI |

## What this forecloses

- **The engine knowing the game's objects.** The engine interpolates keys and model numbers; which slot or kind they come from is the game's to say.
- **Continuity by slot number alone.** A renderer that glides an object from one snapshot to the next asks `continues`.
- **Angle-by-angle comparisons of the player's orientation.** They misread the game's Euler flips as turns.
- **A snapshot inside the counted state.** It would move every digest the moment it was written.
