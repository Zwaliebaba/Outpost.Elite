# ADR-005 — Presentation: Window, Canvas Blit, Sound, Input, Timing

**Status:** Accepted · 2026-09-02 (§5 settled by owner ruling: keep MSIX, drop WinUI 3)
**Depends on:** ADR-002 (the canvas), ADR-004 (where the code lives)
**Feeds:** slices 0d, 2e, 5a

## Context

The game logic produces a 320×200 canvas of C64 colour indices and a stream of SID register
writes per step, and consumes a bitmask of logical keys. The executable has to put the canvas
on screen, turn the register writes into audio, turn the keyboard into the bitmask, and decide
how often to step. The C64 did this with a VIC-II multicolour bitmap, hardware sprites, a raster
interrupt that changed the palette between the space view and the dashboard, a 6581 SID, and a
main loop that ran as fast as the scene allowed.

## Decision

### §1 Screen

- **Canvas → texture → quad.** `CanvasPresenter` (in `Outpost/`) owns an `R8_UINT` 320×200
  texture updated each presented frame from `Game::Frame()` through an upload ring, and a
  pixel shader that maps index → C64 palette (the 16 VIC-II colours; the dashboard's
  screen-RAM/colour-RAM maps `sdump`/`cdump` are already resolved into indices by the canvas)
  and samples with point filtering.
- **Integer scale, letterboxed.** The largest integer factor that fits the client area, black
  bars around it. 320×200 has no square-pixel identity on a modern display; a 5:4 or 4:3
  aspect option is phase 6.
- **Presentation of a step.** The executable presents the canvas once per `Step`. Intermediate
  XOR states inside a step (a ship's old lines erased, new ones drawn) are not shown; the
  original showed them only as flicker. Vsync on; the DXGI flip-model swap chain from
  Frontier's `GpuSwapChain`.
- **The 256-wide space view's horizontal placement** inside the 320 bitmap and the exact
  dashboard row split are read off the reference screenshots in slice 0b and become
  constants in `Canvas.h`.

### §2 Sound

- `GameLogic` ports the effect player and the music player as **per-step state machines that
  emit `SoundEvent { std::uint8_t reg; std::uint8_t value; std::uint16_t offsetSamples; }`**
  — the SID writes the original's interrupt handlers would have made during the step, in
  order. This is exactly what the oracle can verify (ADR-003 §1).
- `SidSynth` (in `Outpost/`, one file) renders three voices — triangle, sawtooth, pulse with
  width, noise (the SID's 23-bit LFSR), ADSR with the SID's rate table, ring-mod and sync if the
  tables use them — into an XAudio2 source voice at 44.1 kHz through Frontier's `AudioDevice`.
  The filter is attempted last and may be omitted. **No reSID or other third-party core**
  (it is GPL and the house rules ban unapproved libraries).
- If the synthesiser is judged not close enough in slice 5a, the fallback is the same event
  stream selecting recorded samples captured from VICE. The seam is the event stream, so the
  fallback changes one file.

### §3 Timing

- The original's main loop had no fixed period. Slice 0b measures its iteration rate in VICE
  in three scenes (empty space, three ships, eight ships). `AppConfig` carries
  `stepsPerSecond`, defaulting to the measured single-ship figure (expected 10–20), and the
  executable runs `Step` on a fixed timestep accumulator against `Clock`, presenting after each
  step and idling to vsync. Steps are never skipped or doubled silently; a stall logs.
- A "variable rate like the original" mode (step as fast as the scene would have allowed on a
  1 MHz 6510) needs a cost model of the original loop and is a phase-6 item if anyone wants it.
  Risk R3 owns the uncertainty here.

### §4 Input

- `Window` (written here, `Outpost/`) produces the per-frame key table; `KeyMap` maps
  virtual keys to `Elite::InputFrame` bits. The default map is the C64's: the function keys
  `f0`–`f9` as documented in the masters (F1 launch, 1–9 the docked screens, F3/F5/F7 the
  views), the `DINT`/`FINT`/`HINT`/`OINT`/`YINT` letter keys, and the flight keys, whose exact
  set is read from the `KYTB` table in slice 1a rather than assumed here. Remapping and gamepad
  are phase 6.
- The original polls key state each iteration and also reads single keys with a wait in the
  docked screens (`RDKEY`, `TT217`); the port's `InputFrame` carries both level and edge bits so
  both idioms port unchanged (plan §2.1).

### §5 The window and the application shell

**Superseded 2026-09-03 by owner ruling: do not strip WinUI, ignore it and proceed.** The
`Outpost` project keeps its WinUI 3 / Windows App SDK template and its full `packages.config`,
untouched. No slice removes packages, and no slice rewrites the shell.

What that means for the plan, so nobody re-opens it later: **slice 0d is deferred, not
cancelled.** The port does not need a window until slice 2e, which is the first build a person
can actually play, and everything between here and there is game logic verified by tests. The
shell question therefore stops being a phase-0 gate and becomes a phase-2 one, to be answered
when there is a canvas worth presenting. The seams that would make either answer cheap
(`Canvas`, `SoundEvent`, `InputFrame`) already exist and are unaffected.

The two options below are kept as the record of what was considered, and neither is being acted
on now.

---

**Option A (was recommended, now deferred): keep MSIX packaging, drop WinUI 3.** The two are
separable, and this takes the half worth having: a packaged, installable, signable application,
without the XAML stack the game has no use for.

What that means concretely, and it is a shape Windows supports directly — a **packaged Win32
desktop application**:

- **The window is raw Win32.** `RegisterClassEx` / `CreateWindowEx`, a message pump on the main
  thread, `WM_KEYDOWN`/`WM_KEYUP` into a key-state table, `WM_SIZE` recreating the swap chain's
  buffers. A plain Windows-subsystem `wWinMain` in `Main.cpp` is the entry point. No XAML page,
  no `Microsoft.UI.Xaml.Application`, no `SwapChainPanel`.
- **The swap chain is a flip-model DXGI chain on the `HWND`** (`CreateSwapChainForHwnd`), which
  is the simplest thing that presents a texture and the one every debugging tool understands.
- **`Package.appxmanifest` stays**, with `runFullTrust`, and so does the MSIX single-project
  tooling that builds and signs the package. The application keeps its identity, its assets and
  its install story.
- **`packages.config` shrinks to what is used:** the MSIX build tools, the Windows SDK build
  tools, and C++/WinRT for `winrt::com_ptr` and `check_hresult` (the COM idiom in AGENTS.md §5).
  The Windows App SDK, WinUI, WebView2, AI/ML, Widgets, Search and DWrite packages go — nothing
  in a 320×200 canvas blit and a three-voice synthesiser touches any of them.

Two consequences worth stating, because they are the price of keeping packaging:

- A packaged application's writable state belongs in its own storage rather than a hand-rolled
  path, so `SaveStore` writes commander files through the packaged app data location. That is
  better than `%APPDATA%\Elite\` anyway, and it is one function either way.
- Launching under the package identity is a different debug configuration from launching the
  bare executable. The project keeps working in both, and slice 0d proves it in both, because a
  packaged-only debug loop is a slow one to live with while porting arithmetic.

**Option B: host a swap chain inside the WinUI 3 shell.** `SwapChainPanel` with
`ISwapChainPanelNative::SetSwapChain`, input from the panel's key events, packaging as it is
today. This is the option the current project shape is already closest to, and it is where
Option A's deferral leaves the tree by default.

Whichever is chosen at 2e, the acceptance is the same: a window that opens, clears, resizes and
closes cleanly, and presents the canvas at an integer scale.

## Consequences

- Presentation is thin — about ten files in `Outpost/`, all written here (ADR-004 §1) — and is
  the only place `float` and the GPU appear. That is what makes the replay and oracle suites
  meaningful for everything else.
- The seams (`Canvas`, `SoundEvent`, `InputFrame`) are the modernisation points: a
  higher-resolution renderer, a better synthesiser or a gamepad each replaces one side of one
  seam.
