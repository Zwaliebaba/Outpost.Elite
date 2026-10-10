# ADR-009 — The Windows shell

**Status:** accepted 2026-10-10, with the change that implements it: the `Engine` and `Outpost` projects that ADR-004 named ahead of time. It is the renderer decision AGENTS.md R12 leaves to the time the renderer is built, and it carries out D8 and the owner's ruling of 2026-10-10 that the shell is built here and run by the owner on Windows.

## Context

**Phase 2's exit was met headless** (ADR-008). The reference boots, docks, flies, fights, jumps, saves and reloads under `ReferenceRunner`, but nobody can play it. Plan §5 asks for a Win32 window with:

- a D3D12 presenter;
- XAudio2 playing the speaker;
- the keyboard as scan codes, the joystick through XInput and the mouse through raw input;
- the `.cdr` files in a user folder.

**R14 rules out what a D3D12 sample reaches for.** That means no `d3dx12.h`, no DirectXTK and no shader compiler at run time. AGENTS.md §2 compiles shaders into the executable.

**The picture is not square-pixelled.** The machine's `Cga` draws every mode as a 640×200 grid of palette indices (`FrameImage`). Mode 4's 320 pixels are two dots each, and the 80-column text the docked screens use is one dot per dot. D8 asks for that picture at integer scale, corrected to 4:3, so the 200 lines must fill 3/4 of the width: 480 rows at scale 1, 2.4 rows each.

**The game must keep running in paced time** (ADR-008), so that a session is a replay, and so that it stays one as Phase 3 replaces routines.

## Decision

**1. Two projects, and the engine knows neither the game nor the machine (R9).**

| | Holds |
|---|---|
| `Engine` | `Window`, the Win32 window and its keyboard, as scan-code events. `Presenter`, which draws a grid of 0x00RRGGBB pixels with D3D12. `AudioStream`, which plays 16-bit samples through XAudio2. `Win32.h`, the one header that owns the Windows macros (AGENTS.md §4). |
| `Outpost` | `Application`, which joins them to `Machine` and `GameLogic`: frames and samples out of the machine, keys into it, the session's replay out to disk. |

**2. The picture** is 640×200 source pixels, shown at a logical 640×480.

- **The texture.** Each frame `Cga::Render` makes the 640×200 grid of indices, `Application` turns it into 0x00RRGGBB through `CgaPalette`, and `Presenter` copies it through one upload buffer into a B8G8R8A8 texture.
- **The fit.** A full-screen triangle draws the texture into a viewport that is the largest whole multiple of 640×480 that fits the window, centred. The rest of the window is cleared to the CGA's border colour. A window smaller than 640×480 gets the largest fit instead.
- **The default window** is 1280×960, scale 2. A 1920×1080 display full-screen is also scale 2, and a 2560×1440 one is scale 3.
- **The filter is sharp bilinear** (`Shader/PresentPS.hlsl`). Each source pixel is flat across the screen pixels it covers, and only the one screen pixel on a boundary is blended, in proportion.
  - Across, there are a whole number of screen pixels per dot, so this is nearest-neighbour.
  - Down, there are 2.4 rows per line. Point sampling would make lines alternately 2 and 3 rows high, which shows as banding on the dashboard's one-line rules. Bilinear would blur every line.
- **The swap chain** is flip-discard, two buffers, R8G8B8A8. There is one frame in flight: `Present` waits for the GPU before it returns.
- **The pipeline is written out by hand**, without `d3dx12.h`: barriers, root signature, sampler and pipeline state. The root signature is one SRV table and four root constants, with a linear static sampler. Wherever a description holds an enumeration that zero is not a valid value of, every field is stated, which clang-tidy's `bugprone-invalid-enum-default-initialization` checks.
- **Shaders are model 5.0.** `FxCompile` builds them into `CompiledShader/PresentVS.h` and `PresentPS.h`, as `g_PresentVS` and `g_PresentPS`, and writes no `.cso`.

**3. The loop is paced by the display.**

- **Vsync.** `Present(1, 0)` returns at the display's refresh, so one turn of the loop is one refresh.
- **No display to wait for.** A minimised or occluded window has nothing to wait on, so `Present` sleeps 16 ms instead. Without this the loop spins a core at 100%.

**4. Time follows the wall clock, in paced time.**

- **Each turn runs the machine to the wall-clock moment** since the session began, in whole milliseconds, through `Elite::ReplayCycle`. That is the same conversion a replay uses (ADR-008 item 4).
- **Catching up is limited to 250 ms.** A turn never runs more than 250 ms of game time. A window dragged for a second loses that second, rather than racing through it afterwards.
- **The start moment is fixed:** DOS's clock starts at `Elite::START_MOMENT`, as in every replay. A session therefore replays exactly, and the game's clock does not show the real time of day.

**5. Sound.**

- **The samples.** `Machine::Speaker` renders samples from the time-stamped writes to the PIT and port 61h, between the cycle the last turn ended at and this one. That is 44.1 kHz, mono, 16-bit, and the timer handler's 1 kHz sound engine keeps its timing (plan §7.3).
- **The queue.** `AudioStream` queues them on one XAudio2 source voice, in a ring of 16 buffers.
- **The slack.** 60 ms of silence is queued first, as slack for the loop's uneven turns.
- **The device keeps its own clock.** It drifts from the wall clock the game follows, so the stream keeps its own count of what is queued:
  - **When the device runs dry**, the 60 ms of silence is queued again.
  - **When the queue would hold more than 90 ms**, the oldest new samples are dropped.
  - **The effect.** Sound never lags the picture by more than 90 ms. A drift of 50 ppm costs one short glitch every 10 to 20 minutes, rather than a growing lag or a crackle on every frame.
- **Without a sound device the game runs silent.**

**6. The keyboard.**

- **Scan codes, not virtual keys.** `Window` reports each key by its set-1 scan code from `WM_KEYDOWN` and `WM_KEYUP`. `Application` sends the make or break code to the machine's keyboard controller.
- **The E0 flag is dropped.** The XT has no E0 prefix: the cursor block sends the keypad's codes, and the right-hand Ctrl and Alt send the left-hand ones, as an XT-layout keyboard on a PC of the time would.
- **Repeats are passed on.** Windows' auto-repeat arrives as repeated make codes, which is what the XT's typematic sends.
- **Only keys a replay can name are sent.** A key whose code `Elite::KeyNameOf` cannot name is neither sent nor recorded, so every session can be replayed.
- **Focus.** Keys held when the window loses focus are released, so that no key stays down in the game's key-down table.
- **Keys the shell keeps:**
  - **F11** toggles a borderless full-screen window. The XT has no F11.
  - **Alt+F4** closes the window.
  - **Alt and F10** do not open the window menu, because the game uses both.

**7. Files.**

- **The reference is not embedded.** It is read from beside the executable or, failing that, from the repository the executable was built in, and checked against its hash (ADR-001, ADR-007).
- **Commanders.** DOS's current directory, and so where commanders are saved and loaded, is `Saved Games\Outpost.Elite\Commanders`.

**8. Every session is recorded as a replay.**

- **Where.** On exit the session is written to `Saved Games\Outpost.Elite\Replays\session-<date>-<time>.replay`, if it lasted more than 10 seconds.
- **What it holds.** Every key sent, a digest every 10 seconds, and a last digest, `end`, at the moment the session ended.
- **Why the `end` digest matters.** A session the machine stopped (a fault, a deadlock, or a spin the watchdog caught) is written as well, and its replay plays to the same stop. A bug seen in play is therefore a replay that reproduces it headless.
- **Checked across builds.** A session recorded in a MinGW build of `Outpost` under Wine, and played by a g++ build of `ReferenceRunner` on Linux, matched both its digests (2026-10-10).

## What this forecloses

- **Scaling.** A picture that is not an integer multiple of 640×480, except in a window too small for scale 1. No stretch to fill.
- **Wall-time interrupts.** Running the game in clocked time, or any other time, in the shell.
- **The engine reaching into the machine.** `Engine` is handed pixels, samples and key events, and nothing else.
- **A shader compiled at run time,** or `.cso` files next to the executable.

## What is not done here

- **The joystick and the mouse.** The machine already has them: the game port, and the int 33h driver (ADR-006). Two things stop them coming in this change:
  - **A replay cannot yet record them.** Each needs a verb in the replay format: axis resistances and buttons for the stick, and motion and buttons for the mouse.
  - **The mouse is a power-on condition.** Whether a driver is present is fixed at power-on (`Firmware`), and the game can see it (`IsMouseDriverInstalled`, CS:02D4). It therefore has to be one of a replay's stated conditions (ADR-008 item 4).

  They follow in their own change, which amends ADR-008. Until then the shell offers the keyboard and nothing else, which is everything the game needs.
- **Commanders saved before the session began.**
  - **Why they matter.** The session sees what is already in `Commanders`, both in the load screen's file list and in a commander it loads. A replay starts in an empty directory (ADR-008 item 4), so a session that touches an earlier commander replays only where that file is present.
  - **What changes it.** Recording the files present at the start belongs to the same amendment to ADR-008.
