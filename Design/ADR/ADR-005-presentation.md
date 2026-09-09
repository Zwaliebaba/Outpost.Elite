# ADR-005 — Presentation: Window, Canvas Blit, Sound, Input, Timing

**Status:** Accepted · 2026-09-02 (§5 settled by owner ruling: keep MSIX, drop WinUI 3;
§1's sprite overlay and VIC-II effects settled 2026-09-05 — both composite in `Canvas::Resolve`
over a new `VideoState`, with the border the presenter's). **§1's two settled items are both
built as of 2026-09-06** — the sprite overlay (`VideoState`, `SPRITE.bin`, the compositor in
`Resolve` — §6.148) and the raster effects (slice 4f — §6.155) — **and the second was wrong about
two of its three subjects.** `welcome` is `VIC+&21`, the background colour inside the image, not
the border, so nothing went to the presenter; and `HFX` is not an effect this build has at all. The
paragraphs below are corrected in place and say which claim was which. **Amended 2026-09-09 by the owner rulings recorded on [Platform.md](../Platform.md) §12: §1 gains the waitable presenter, §2 is REVERSED (the sound interrupt is clocked by the simulated blank), §3 gains the integer scheduler and records the speed knob declined in favour of a fixed-rate flight model as its own track, §4 gains the `InputFrame` with event accumulation and the layered map. ADR-010, written at that track's close, supersedes this document.**
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

- **Picture → texture → quad.** **Amended 2026-09-08 by the resolution track
  ([Design/Resolution.md](../Resolution.md), ADR-008): 640×400 for 320×200, `ScreenPresenter` for
  `CanvasPresenter`, and the window opens at 2×.** `ScreenPresenter` (in `Outpost/`) owns an
  `R8_UINT` 640×400 texture updated each presented frame from `Game::Frame()` through an upload
  ring, and a pixel shader that maps index → C64 palette (the 16 VIC-II colours; the dashboard's
  screen-RAM/colour-RAM maps `sdump`/`cdump` are already resolved into indices by the canvas)
  and samples with point filtering.
- **Integer scale, letterboxed.** The largest integer factor that fits the client area, black
  bars around it. 320×200 has no square-pixel identity on a modern display; a 5:4 or 4:3
  aspect option is phase 6.
- **Presentation of a step.** The executable presents the canvas once per `Step`. Intermediate
  XOR states inside a step (a ship's old lines erased, new ones drawn) are not shown; the
  original showed them only as flicker.

- **Except where the original drew for longer than a frame, and the test is the cycle count.**
  Amended 2026-09-05, after the launch tunnel was found invisible. The clause above was written
  about a ship's old lines being erased before its new ones are drawn, and it does not reach an
  effect the original spent half a second on: `HFS1` costs 483,905 cycles for its thirty-four
  circles, 14,232 each against the 17,095 an NTSC frame has, and `TT110` runs it twice. The C64
  never had to ask to be seen — the VIC-II scans the bitmap out continuously, so a long routine
  is an animation for free — and a canvas is seen only when something presents it, so a routine
  like this needs a seam that presents while it draws. `Elite::TunnelEffects` is the first, at one
  frame per circle. **The rule:** a state that lives for less than a frame is flicker and is not
  shown; a state the original held for many frames is the effect, and the port owes it a present
  per frame. Which one a routine is, is measured with `Cpu6502`'s cycle counter and not judged
  (§6.109). Vsync on; the DXGI flip-model swap chain from
  Frontier's `GpuSwapChain`.
- **SETTLED 2026-09-05 — the sprite overlay composites in `Canvas::Resolve`. BUILT 2026-09-05
  (§6.148); the crosshairs, the burst and the Trumbles all appear.** The C64 drew eight hardware
  sprites above the bitmap: the laser crosshairs, the explosion burst and the Trumbles, and nothing
  else the game uses. When this was written, `SIGHT` was ported and wrote the sprite pointers and
  colour and no code composited them, so the crosshairs did not appear (plan §6.100). What follows
  is the reasoning that decision was taken on, kept as written.

  **The reason is not the one the open question gave.** It argued for the canvas because that is
  testable on both CI legs, and that argument is weaker than it looks: compositing has no oracle
  WHEREVER it lives, because the game never rendered a composited image into memory, so moving it
  into `GameLogic` makes it runnable under test without making it verified. The decisive reason is
  structural. `Canvas` keeps the game's BYTES, laid out exactly as the original's memory, and that
  is the thing the oracle compares — it is untouched either way. `Resolve()` is already the
  un-oracled "what a person would see" layer downstream of it: it already turns the multicolour
  bitmap, screen RAM and colour RAM into indices, already has no byte-level oracle, and is already
  covered by golden hashes and by targeted assertions on known cases. Sprites are the same kind of
  transformation over the same inputs. Putting them anywhere else splits one function across two
  binaries and buys nothing.

  **What actually makes this a design change, which the open question missed.** `Resolve` took only
  the canvas when this was written, and the sprite state was not in it. The pointers are (they are canvas
  writes at `SIGHT_SPRITE_CELL`, compared byte for byte already), but the enable mask, the colour
  and the raster mode were `SightEffects` — a WRITE-ONLY seam out to the presenter — and the Trumble
  positions `MVTRIBS` writes were not modelled at all. **There were TWO such write-only seams rather
  than one**: `ExplosionEffects`, added by slice 4b-b for `PTCLS2`'s burst sprite, carried the
  sprite-expand registers and sprite 1's nine-bit position and enable bit the same way (§6.144). Its
  test compares the ARGUMENTS against the shipped code's registers, read-modify-writes included,
  which is as far as a seam can be verified and is the pattern `VideoState` kept. So this needed the
  register state to become DATA that `GameLogic` owns and both `Resolve` and the presenter read: an
  explicit `VideoState` struct, not a getter on `SightEffects`. The reason it must not be a getter is
  already written on `MaskSprites`: a getter invites a port to compute what the hardware is holding.

  **How it came out.** `Elite::VideoState` is the struct, `Elite::Apply*` are the seam calls as free
  functions so a presenter's override is one line, `Canvas::Resolve(_out, _video)` composites sprite
  7 first and sprite 0 last, and `FlightSession` owns the struct. Slice 4d-a is the case that proves
  the shape was right rather than merely tidy: `MVTRIBS` READS a sprite register back before it adds
  a velocity, so it takes a `VideoState&` and could not have been ported against a write-only seam
  at all (§6.149).

  **Prerequisite, and it should be done first regardless. Done.** `SPRITE.bin` was a fourth assembly
  `tools/labels.py` did not build — 84 lines of source and 448 bytes, seven sprite definitions. It is
  built on the `LOADER_ASSEMBLY` pattern (`SPRITE_ASSEMBLY` in `labels.py`, its own reference pair,
  kept out of the oracle image because `CODE% = &7C3A`). The definitions were byte-checked against it
  until 2026-09-07, when the oracle comparison of the generated tables was retired.

  **What stays unverified, said plainly.** The blit rule itself — sprite-over-bitmap priority, the
  multicolour sprite bit pairs, and the x-expand flag. That is documented VIC-II behaviour and it is
  small, but it is the first drawing in the port with nothing to compare against. The honest
  mitigation is a golden hash plus one hand-checked screenshot on the owner's machine, in the shape
  slice 2e already established — not a claim of oracle coverage.
- **SETTLED 2026-09-05 — the VIC-II raster effects model in `Canvas::Resolve` too, on the same
  `VideoState`. BUILT 2026-09-06 as slice 4f (§6.155) — AND WRONG ABOUT TWO OF ITS THREE
  SUBJECTS.**

  **`HFX` is not in this build.** Upstream's `hfx.asm` is `SKIP 1` and says the flag is unused in
  this version; `DOHFX` assembles with both its instructions commented out in the original source;
  the C64's `LL164` is four instructions and does not write it; and the C64's `COMIRQ1` does not
  read it. The hyperspace tearing belongs to the BBC and the 6502 Second Processor, whose `IRQ1`
  really does read the flag. There was never anything here to build, and §6.98 grouped the three
  bytes because they sit together in memory rather than because one routine reads them all.

  **`welcome` is not the border.** It is `VIC+&21`, background colour 0 — inside the 320×200 image,
  supplying the `%00` bit pair of every multicolour cell — and the upstream comment on the
  instruction says "we change the background colour of the space view". `VIC+&20` is the border and
  the handler never writes it. So the apportionment below is the wrong way round: **all** of this
  landed in `Canvas::Resolve` and **none** of it in the presenter.

  What was right is the third subject and the reasoning. The paragraphs as written follow;
  `moonflower` (the energy bomb drops the upper half to standard bitmap mode),
  `welcome` (the border colour it cycles while the bomb runs) and `HFX` (the hyperspace tearing,
  `DOHFX`) are ordinary bytes in `ScreenState` that `FlightSession::SyncVideoRegisters` carries
  outbound and `Canvas::Resolve` has no model for (plan §6.98). A player of the shipped game sees
  the bomb and the jump; a player of the port sees neither.

  This is the sprites' decision and it goes the same way, for the same structural reason and with
  one difference worth naming: these are not an overlay but a MODE CHANGE on pixels `Resolve`
  already produces. `moonflower` switches the upper half between multicolour and standard bitmap,
  which changes how the same bytes decode — so it belongs inside `Resolve`'s decode and nowhere
  else, and putting it in the presenter would mean decoding the bitmap twice in two languages.
  `welcome` is the border, which is outside the 320×200 image entirely and is the one part that is
  genuinely the presenter's (the letterbox bars are already its). `HFX` is a per-row shift of the
  space view, which is a `Resolve` concern.

  So: `moonflower` and `HFX` in `Resolve`, `welcome` in the presenter as the letterbox colour, and
  all three read from the same `VideoState`. What is NOT open is whether they exist (plan §6.120).

  **CORRECTED 2026-09-06 (§6.155).** Two of those three clauses are wrong and the last sentence is
  the reason they went unchallenged for a day: "whether they exist" was taken as settled for all
  three because §6.120 had found the BYTES, and a byte existing is not an effect existing. What is
  built is `moonflower` and `welcome`, both in `Resolve`, neither on a `VideoState` — the raster
  registers sit on `Canvas` beside `m_background` and `m_dashboardShown`, which were already there
  and which `LoaderScreen` writes with no `VideoState` in sight. `VideoState` is the SPRITE
  registers and its tests are named for that; putting one byte of the raster split in it would have
  meant threading a sprite struct through the loader.

- **Ordering, so neither of the two rulings above blocks anything.** Neither is on phase 4's
  critical path. The crosshairs need only `SIGHT`, which is built; the Trumbles need `MVTRIBS`
  (slice 4d); `moonflower` and `welcome` need the energy bomb (4b) and `HFX` needs hyperspace (4c).
  **And that is how the second one got left behind**: every dependency it named was built by
  2026-09-05, the sprite half went in the same day, and nothing scheduled the rest. Ordering by
  "blocks nothing" says when work MAY start and never says who starts it.
  The `SPRITE.bin` assembly and the `VideoState` struct are the shared prerequisite and are worth
  doing early, because both are small and everything else waits on them.
- **The 256-wide space view's horizontal placement** inside the bitmap and the dashboard row
  split. **Answered 2026-09-03, and it never needed the screenshots this clause asked for.**
  Slice 0b-b was cancelled, and the plan's §6.5 accounted for only two of its dependents; this
  was the third. Both numbers are in the game's own address table: `ylookup` adds `0x20` to
  every row, which is a **four character cell left margin**, so the space view's x 0..255
  occupies cells 4..35 of 40 (two x-units per multicolour pixel, 128 pixels of 160); and
  `ylookup[144]` is exactly character row 18, so the **space view is rows 0..143** and the
  dashboard starts at y 144. Measured by `CanvasSpikeTests.cpp`, and they become constants in
  `Canvas.h`. **On the picture the same measurements hold doubled**: the view is x 0..511 in cells
  8..71 of 80, and the dashboard starts at y 288 (`Picture.h`, and the frame's own interior is the
  reason a re-flowed screen's text stays inside cells 8..71 — Resolution.md §8.1).

- **The presenter waits on the swap chain's latency object, presents on change and idles when hidden
  — 2026-09-09 ([Platform.md](../Platform.md) T-3).** `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`
  with a queue of one, so the loop runs right after a blank, samples, steps and presents into the
  next; `Present` only when the frame surface's generation counter moved; `DXGI_STATUS_OCCLUDED` waits
  on `MsgWaitForMultipleObjects` with a cap. Vsync, the integer scale and the letterbox are unchanged.
  ADR-008 owns the surface; this is the executable's half, and PresentMon's input-to-photon number is
  what the slice is accepted on.

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

**Amended 2026-09-05 (slices 5a and 5b, plan §6.129).** The event is `SidWrite { reg; value; }` and there is no
`offsetSamples`: the C64 writes the chip from ONE raster interrupt per frame, so every write in a
frame has the same offset and the field would carry nothing. The unit of emission is the frame --
`RunSoundInterrupt` fills a `SidWriteLog` per call -- and the executable applies a frame's writes
and then renders 17,095 cycles of chip for it. The interrupt is clocked off the audio device's queue
depth rather than off the display or the wall clock, because the device consumes samples at exactly
the rate they are rendered for. `SidSynth` is per-cycle and integer, with sync and ring modulation
and without the filter; "Frontier's `AudioDevice`" above does not exist in this tree and
`SoundOutput` talks to XAudio2 directly.

**REVERSED 2026-09-09 (owner ruling, [Platform.md](../Platform.md) §12 R1, slice T-4).** The
interrupt is clocked by the SIMULATED VERTICAL BLANK the scheduler delivers — once per blank, on the
game thread — and not by the device's queue depth. The paragraph above was right about the chip and
silent about the state: the sound buffer's counters are game state in every replay digest, and
clocking them from a device made `Universe` the one thing in the program that advanced on a clock the
game does not own. The device's rate still paces the CHIP — the synthesiser renders a ring of
per-blank logs on the XAudio2 callback thread at the sample rate, and an empty ring renders the last
registers as a 6581 does when its processor is busy — so each half keeps the clock that is its own.
The record is re-taken once, with a zero-delivery run as the column that proves the flight did not
move (ADR-007 §4's first case).

### §3 Timing

- The original's main loop had no fixed period. Slice 0b measures its iteration rate in VICE
  in three scenes (empty space, three ships, eight ships). `AppConfig` carries
  `stepsPerSecond`, defaulting to the measured single-ship figure (expected 10–20), and the
  executable runs `Step` on a fixed timestep accumulator against `Clock`, presenting after each
  step and idling to vsync. Steps are never skipped or doubled silently; a stall logs.
- A "variable rate like the original" mode (step as fast as the scene would have allowed on a
  1 MHz 6510) needs a cost model of the original loop and is a phase-6 item if anyone wants it.
  Risk R3 owns the uncertainty here.

- **The flight loop is cycle-budgeted too, from 2026-09-05.** The fixed rate above was the NTSC
  vertical refresh, and §6.17 had already established that the C64's main loop is not driven by
  the refresh — there is no `WSCAN` in it. A frame measured against the shipped `M%` costs 47,784
  cycles with an empty bubble and about 81,000 with ships in it, so the loop runs at 21 frames a
  second at best and 12.6 in a real bubble, not 60. `Outpost::FlightFrameSeconds` is that
  measurement, indexed by how many slots of `FRIN` are occupied, and it is **two bands rather than
  a curve** because the two ship scenes measured sit 7% either side of one number. The crowded
  end — where the original slows down most, and where the slowdown is part of the difficulty —
  is not measured yet and is paced at the one-ship cost (§6.114).

- **The crowded end and the docked pass are measured, 2026-09-08 (Design/Archive/InputTimer.md T-0),
  while the interpreter is still in the tree.** `FLIGHT_FRAME_COSTS` is four rows keyed by
  occupied slots, planet and sun included, linear between them: 47,784 cycles empty, 82,236 with
  the planet and its companion, 150,113 with three fighters, 293,354 with eight — three and a half
  frames a second in a full fight, which is the slowdown the game shipped with. The sun and the
  station cost differently and the row is their midpoint. The docked pass is not paced by a
  flight floor any more: `MLOOP` with `QQ12` set is two vertical syncs (`LDY #2 / JSR DELAY`,
  unless `QQ11 AND PATG` is odd, which only the Data on System screen with the names on is)
  around 4,472 cycles of work, so the docked half runs at just under half the sync rate, and
  `DockedPassSeconds` prices the syncs `Game::StepDocked` asked for (T-2). `TT16`'s extra sync per crosshair step is
  recorded and waits for the simulated blank (T-1) to be honoured.

- **One integer scheduler, the blank, and the rate that is NOT an option here — amended 2026-09-09
  ([Platform.md](../Platform.md) §3.2; rulings D3 and D4).** T-1 replaces `PlanSteps` and both hold
  loops with one scheduler in integer 6510 cycles at the machine's rate — NTSC, settled by ADR-001's
  own variant line, PAL selectable in `Settings.txt` — which delivers whole flight steps at the
  measured cost, whole docked passes at their syncs, and one vertical blank every `cyclesPerFrame`.
  `DELAY` counts those blanks on every panel; a stall costs a frame and never a burst, and is counted
  where `(void)plan.stalled` discarded it; an inactive or minimised window plans nothing and banks
  nothing, which is the pause §4 promised. **A `speed` knob that scaled the cost model was put and
  DECLINED.** The owner ruled that the flight step's rate is a game-logic change and not a setting: a
  fixed-rate flight model — one step per blank, the per-step constants rescaled — as its own track and
  ADR after Platform.md's RN-6, with the faithful cadence kept selectable (ADR-001 §4). Until then the
  cost model is the only pace, and the scheduler's step cost is the hook that model plugs into.

- **The title screen is cycle-budgeted as well. Added 2026-09-05.** `TITLE`
  is not driven by the vertical sync — §6.17's scan found `WSCAN` called from `DELAY`,
  `TT16+7` and `FREEZE` and nowhere else — so its ship turns at whatever rate a 6510 gets
  through `MVEIT` and `LL9`, and a fixed rate is the wrong shape of answer. `TitleTurnSeconds`
  interpolates a table of measured costs, indexed by how far away the ship still is: 15,600
  cycles a turn while `LL9` is drawing a dot and 121,276 when it is drawing the settled
  wireframe, read off the shipped routines by `CycleTests`. **That is Risk R3's mitigation in
  use rather than merely built**, and it is deliberately narrow: one routine, one ship, and it
  says so where the numbers are. The flight loop stays on a fixed rate until somebody measures
  the same way for an arbitrary frame (§6.110).

### §4 Input

- `Window` (written here, `Outpost/`) produces the per-frame key table; `KeyMap` maps
  virtual keys to `Elite::InputFrame` bits. Remapping and gamepad are phase 6.

- **The default map is a modern PC layout, not the C64's. Amended 2026-09-05; what it said
  before is below.** The arrow keys steer (roll left/right, climb/dive), `,` and `.` set speed,
  `A` fires, the secondary flight controls keep their C64 letters (`T`, `U`, `M`, `E`, `J`,
  `C`, `P`) with TAB for the energy bomb and ESCAPE for the escape capsule, the six information
  screens are F1–F6, and the four views — front, rear, left, right, the first of which is also
  launch — are F7–F10. The `DINT`/`FINT`/`HINT`/`OINT`/`YINT` letter keys and `@` are unchanged.

- **Space keeps its C64 meaning as well, because the game asks for it by name.** Amended
  2026-09-05, after the title screen was found ignoring it. Moving speed to `,` and `.` left
  Space unbound, and an unbound key never reaches the queue at all — so "PRESS SPACE OR FIRE,
  COMMANDER." printed a prompt the shell could not answer, and `TITLE` takes any key. Space is
  bound to position 4, which is the C64's own Space, so it dismisses every "press space" prompt
  and increases speed in flight exactly as it did on the machine. **The rule this is an instance
  of:** a layout may move a control, but it may not leave a key the game's own TEXT names bound
  to nothing.

- **The map is therefore many-to-one, and that is a consequence rather than a convenience.**
  `gnum` and the line editor read a key as the CHARACTER `TRANTABLE` gives its position, so the
  number row cannot be unbound to free the digits for the function keys: "4" keeps position 53
  and F1 is given the same one. The cost is stated once here so nobody files it as a bug — 4–9
  still reach their screens from `TT102`, because `TT102` compares the POSITION and any key that
  can type a "4" is a key that opens the Galactic Chart. Separating the two needs a second table
  chosen by the current view, which is a phase-6 remapping question and not this decision.

- **What it said before, kept as the record:** "The default map is the C64's: the function keys
  `f0`–`f9` as documented in the masters (F1 launch, 1–9 the docked screens, F3/F5/F7 the
  views), the `DINT`/`FINT`/`HINT`/`OINT`/`YINT` letter keys, and the flight keys, whose exact
  set is read from the `KYTB` table in slice 1a rather than assumed here." Two things made that
  wrong. `KYTB` **is not used by the C64 build at all** — the upstream file says so in as many
  words, and the flight keys are the `KY1`–`KY7` and `KY12`–`KY20` positions inside `KEYLOOK` —
  so the sentence deferred to a table that was never going to answer it, and the flight half of
  the map was still unbound after phase 3 had named every one of those positions. And the C64's
  own arrangement (roll on `<`/`>`, pitch on `X`/`S`, speed on Space and `?`) is a keyboard
  layout rather than a game rule: ADR-001's fidelity requirement is about what the machine
  COMPUTES, and which physical key reaches `KEYLOOK+17` is not part of it.
- The original polls key state each iteration and also reads single keys with a wait in the
  docked screens (`RDKEY`, `TT217`); the port's `InputFrame` carries both level and edge bits so
  both idioms port unchanged (plan §2.1).

- **The pause screen is removed. Owner ruling 2026-09-08 (Design/Archive/InputTimer.md §5.9, slice I-0).**
  `DK4`'s `CPX #&40` froze the game on INST/DEL and `FREEZE` was the only settings interface the
  game had: thirteen toggles, two sound keys and a quit to the title. The port had it as
  `Game::Mode::Paused` and could enter it and not leave it, because CLR/HOME was never bound
  (InputTimer.md I-3). The ruling removes the screen rather than binding its keys: INST/DEL is an
  ordinary key, the thirteen bytes are set at start-up from the executable's settings file
  (InputTimer.md S-1), and a windowed player's pause is the executable stopping the steps while
  the window is inactive (§3, to be built as InputTimer.md T-1). This is the first deliberate
  removal of an original FEATURE rather than of a hardware read, and ADR-001 §4 points here.

- **The game may believe it has a joystick only when the platform has one to read. Owner ruling
  2026-09-08 (Design/Archive/InputTimer.md §5.1, slice I-3).** `TITLE` leaves `JSTK` set when the fire
  key dismisses it, which on a C64 selects the stick `RDKEY` then reads from CIA port A. The port
  reads no port A, so `Game` clears `JSTK` after each start sequence unless `Keyboard::HasJoystick`
  answers true, which nothing does until the gamepad slice. `TITLE` itself is unchanged and still
  compared; the settlement runs after it. When a controller exists the original's rule returns:
  fire on the title screen selects it, and `JSTGY`/`JSTE` become its axis reversals.

- **The blocking read is `TT217`, ported. 2026-09-08 (Design/Archive/InputTimer.md I-1).** `Elite::ReadKey`
  waits two frames, waits for no key, waits for a key and translates, over the port's `Held`, and is
  compared against the original with the matrix changing under it. The executable's `NextKey` is
  that routine; the queue of `WM_KEYDOWN`s it popped until then -- auto-repeats included -- is gone,
  and the dispatch takes one genuine press per step from the window, never a repeat. A key held
  when a prompt appears is not its answer, a key held down is one character, and two presses need
  a release between them, which is what the C64 did and what "a screen was skipped" was the
  absence of.

- **The dispatch takes an `InputFrame`, and a tap is never lost — 2026-09-09
  ([Platform.md](../Platform.md) I-2).** One struct per step: sixty-five held bytes over the matrix
  and the press for `TT102`, produced by the platform after the message pump and immediately before
  the step it feeds. Events are accumulated into it: a key that went down at any point since the last
  frame is reported held for that one step, so a tap shorter than a flight step — up to 290 ms by the
  cost model — reaches the game. The C64 scanned its matrix once a frame and lost such a tap; this is
  the one reactivity gain in flight that changes what no step computes, and the replay's held keys
  span steps, so no record moves. The bullet above that said the frame "carries both level and edge
  bits" is this, with the edge one key rather than sixty-five.

- **The map has layers, which is the phase-6 remapping question the many-to-one bullet deferred —
  ruled 2026-09-09 ([Platform.md](../Platform.md) I-2).** `KeyBinding` gains a scan code for the
  positional keys, so the arrows and `,`/`.` are the physical keys on every layout, and a layer set
  `{Flight, Docked, TextEntry}`. The Docked layer drops the steering positions, which retires the
  chart rule from `Elite::ScanKeyboard` into data and is exactly "a second table chosen by the current
  view". Every layer keeps the Space rule above: no key the game's own text names is bound to nothing,
  and the TextEntry layer names every position that types. A gamepad fills the same frame later on its
  own ADR; XInput is the recommendation, and `GameInput` is a package the owner has not been asked for.

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
