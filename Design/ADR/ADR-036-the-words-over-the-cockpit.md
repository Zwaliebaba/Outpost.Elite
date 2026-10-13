# ADR-036 — The game's words over the cockpit, and the credits and the pause screen in it

**Status:** accepted 2026-10-13, with the change that implements it, a fix to piece 3 of Phase 6 after [ADR-035](ADR-035-the-scene-draws-the-sky.md) ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It carries out the owner's ruling D66. It amends [ADR-033](ADR-033-the-space-view-hybrid.md)'s layers and [ADR-034](ADR-034-the-cockpit-hud.md)'s cockpit. **Amended 2026-10-13, after the owner's play test (D68):** the words over the cockpit were hard to read where they crossed it, so each block of them stands on a panel (item 3); and the tunnels, leaving or docking at a station and the hyperspace rings, still showed the CGA dashboard, so they are published as the game's own picture of the view (item 4).

## Context

The owner played ADR-035's part and found two faults on the title.

- **"Press any key to start game" was half hidden.** The title draws it at view rows 112–117 and pixels 21–234 (`PRESS_ANY_KEY_PLACE`). In the cockpit's window those are design rows 724–761, from x 415 to x 1505. The canvas's centre console rises over the window from y 740 to y 810, between x 660 and x 1260, so it covered the line's lower half in its middle. The reference is silent here: 1987 has no console over the view.
- **The screen after the turning ship showed the CGA dashboard.** That screen is the credits (`ShowCredits`), drawn into the view and copied by `FinishSpaceViewFrame`. They published no flight snapshot, so the screen no longer showed the last published frame (`SpaceViewShown`), and the shell fell back to the CGA picture. The pause screen (`RunPauseOptions`) draws into the view in the same way and publishes nothing, so it fell back too.

The owner ruled:

- **D66 (2026-10-13).** The game's own words are drawn over the cockpit, so the console never hides 1987's text. The console keeps its shape, and ships still pass behind it as behind a cockpit's frame. The pause screen is fixed with the credits.

## Decision

1. **The layers keep which pixels are the game's words** (`SpaceViewLayers::HoldsWords`). These are the over layer's pixels that no object drew and that no object painted over:
   - the pixels drawn between two objects, as `BeginObject` finds them;
   - the pixels changed after the last object and before `FinishSpaceViewFrame` copies the view, as `Finish` finds them.

   An object's `EndObject` clears the mark from every pixel that object changes.
2. **The credits and the pause screen are published** (`WithoutScene`). Each one begins a frame of the layers (`BeginSpaceViewFrame`) and publishes its snapshot once the view is on the screen. That snapshot holds no object, no stardust, no sights and no title's planet, because neither screen draws any of them over the cleared view. Its dashboard and its system stay:
   - **the credits** have the title's dark dashboard, with ---- E L I T E ---- on it (`TakeTitleSnapshot`);
   - **the pause screen** has flight's dashboard, live as the pause left it.

   Neither continues the frame before it.
3. **The shell draws the words over the cockpit** (`SpaceView::DrawWords`). It runs after `Cockpit::Draw` and draws each run of a row's ink, in one colour, as one box at its pixels of the view. The ink is in the screen's colours. *Amended (D68):* each block of lines, those with no more than a blank line between them, first gets a panel in the cockpit's style: the console's colour at 90% and a gold edge, 3 pixels and 2 rows clear of its ink. The paper, 0, is the panel's. The title's line is one block, the credits one and the pause menu one.
4. **The tunnels are published as the game's own picture** (*added*, D68). Each frame of the station's tunnel (`PlayStationTunnel`, leaving or docking) and of the hyperspace rings (`PlayHyperspaceTunnel`) is published once the view is on the screen (`Hardware::PublishSpaceViewPicture`). Its snapshot is `WithoutScene`'s, with flight's dashboard, and is marked a picture (`FlightSnapshot::picture`). Its layers hold the whole view as the screen shows it, in the over layer (`SpaceViewLayers::FinishPicture`), with no model, nothing the renderer's and no words. So the cockpit stands round the tunnel, which is 1987's own pixels, and the ships seen through a station's tunnel are 1987's too.

### What moves on

| Part | Arrives with | Why there |
|---|---|---|
| Where the words stand in a wider view | the full window (D50), next | ADR-035 keeps the game's words at the view's middle; the console's rise still crosses them there |
| The tunnels drawn by the renderer | when the owner rules on it | they are 1987's pixels in the cockpit's window, as the fix the play test asked for; a redrawn tunnel is a look of its own |

### Measured 2026-10-13

| Check | Result |
|---|---|
| The words are only words | A scratch copy of `SpaceViewLayersTests` played all 26 replays, checking each frame still on the screen a millisecond after it was published, and listed the rows and pixels of every frame's words. The title's turning ship holds them only in rows 112–117 and pixels 21–234: its line. Each of the 25 replays that pass the title has a frame of the credits, two where it passes it twice, words in rows 8–77 and pixels 9–190. `flight-screens.replay`'s pause is one frame, words in rows 24–101 and pixels 32–236. No other frame holds a word. Every frame is still explained as ADR-035 measured. |
| The committed tests | `EachFrameExplainsTheScreen` (`combat-orerve.replay`) holds the title's words to their line, finds the credits published and shown, and finds that a frame of words shows nothing else. `ThePauseScreenIsAFrameOfWords` (`flight-screens.replay`) finds the pause screen published and shown, and finds no frame of words outside a pause. `PixelsAreWhereTheScreenShowsThem` holds the mark to a row drawn before any object. Leaving out the credits' publish, the pause's, `BeginObject`'s mark or `Finish`'s each fails at least one of them. |
| The turn probe | each test's sequence of idle and working turns identical to `main`'s |
| The suite | `GameLogicTests` passes under g++, 165 tests |
| Compiles | the shell's changed files, syntax only, with mingw-w64's g++ 13 and its Windows SDK headers, `-Wall -Wextra -Wconversion -Wpedantic -Werror`: clean |
| clang-tidy | the root `.clang-tidy` with clang-tidy 18 on every changed file: no finding but the one the base has too |
| Not run here | the MSVC build and the game: CI builds and runs the tests, and the owner plays it |

### Measured 2026-10-13, the amendment

| Check | Result |
|---|---|
| Every tunnel frame is published | The same scratch sweep over all 26 replays, counting each run of tunnel frames: every run of the station's tunnel is 20 frames, leaving and docking, and every run of the hyperspace rings 50. Every frame is explained, and each tunnel frame's pixels are all the over layer's, none words and none the renderer's. |
| The committed tests | `EachFrameExplainsTheScreen` finds a run of 20 and a run of 50 in `combat-orerve.replay`. Leaving out the station's publish, the hyperspace rings', or `FinishPicture` (finishing a tunnel as a frame of flight) each fails it. |
| The turn probe | each test's sequence of idle and working turns identical to `main`'s, every test |
| The suite | `GameLogicTests` passes under g++, 165 tests |
| Compiles, clang-tidy | as above, clean; clang-tidy 18 also finds dead stores in `PlayStationTunnel`'s lines this change does not touch, as it does on `main` |
| The panels | not measured: the shell has no tests, and the owner plays it |

## What this forecloses

- **A console that hides 1987's text.** The game's words are always drawn over the cockpit (D66).
- **Moving or reflowing the game's words to clear the cockpit.** They stay at 1987's places.
- **The credits, the pause screen or a tunnel falling back to the CGA picture.** Each is published as a frame of the space view. Every frame `FinishSpaceViewFrame` presents is now published.
