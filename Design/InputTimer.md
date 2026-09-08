# Input and time — what the port does today, and the plan to modernise both

**Status:** analysis and plan, opened 2026-09-08. Nothing in this document is built. It reads after
[Modernize.md](Modernize.md), because it starts from the shape M6-0 left and proposes slices that
sit beside M6-a; where it touches a decision an ADR owns, it says which ADR changes.

**What was read.** `Outpost/Window.*`, `KeyMap.*`, `Shell.*`, `Main.cpp`, `FlightSession.*`,
`Presentation.*`, `CanvasPresenter.*`, `SoundOutput.*`; `GameLogic/Controls.*`, `DockedKeys.*`,
`Game.*`, `GameLoop.*`, `FlightLoop.*` (the `TT17` half), `PauseScreen.*`, `NameEntry.*`,
`StartUp.*`, `Flight.*` (`TITLE`), `Ports.h`, `Presenter.h`; ADR-005 §3 and §4, ADR-007, plan
§5.3, §5.4, §6.17, §6.110, §6.114, §6.115, §6.128, §6.159; Modernize.md §2.1, §4.4, §4.5, §4.8.
The upstream submodule was empty in the environment this was written in, so the original's
`TT217`, `RDKEY`, `DK4`, `DKS3`, `TT17`, `TT16`, `DELAY` and `WSCAN` were fetched from the pinned
commit `aa3f7ee` directly and read as the C64 branch of each. Every claim below about the original
comes from those eight files; every claim about the port comes from the tree at `15f0e74`.

---

## 0. The three questions, answered first

**Is the joystick gone?** The hardware read is gone and the game's *idea* of a joystick is not, and
the second half is live and reachable from the keyboard. `RDKEY`'s `dojoystick` branch (CIA port A)
is correctly not ported. But `JSTK` is still set to `&FF` by `TITLE` and cleared only when the title
screen is dismissed by a key that is **not** the fire key — so a player who answers "PRESS SPACE OR
FIRE, COMMANDER." with `A` has told the game they have a joystick. `DOKEY`'s joystick branch *is*
ported (`Controls.cpp`, "the joystick's own re-centring") and then runs every frame: with no roll key
held the roll rate snaps to 128, with no pitch key held the pitch snaps to 128, and the damping the
keyboard player is supposed to get never happens. Two more bytes, `JSTGY` and `JSTE`, exist only so
that the pause screen's thirteen-toggle walk keeps its layout; they are toggled and read by nothing.
Finding I-4 has the detail and §5.1 the ruling this needs.

**Is the keyboard handled to modern standards?** No, and the specific way it is not is the one the
question names. The original's blocking read, `TT217`, is *release-then-press with a two-frame
debounce*: it waits until no key is held, then waits for a press. The port's `NextKey` pops a queue
that the window fills on **every `WM_KEYDOWN`, auto-repeats included**, sixteen deep, oldest
dropped. So a key held a little too long, or two keys typed quickly, is delivered to the next screen
as presses the original would never have seen. That is the mechanism behind a screen being skipped
or a prompt answering itself, and it is a choice `Window.cpp` records as deliberate on a wrong
premise (I-1). Beside it: the pause screen can be entered with Backspace and **cannot be left**,
because the resume key is unbound (I-3) — and the owner has said, 2026-09-08, that the pause
functionality may be removed, which this plan takes: §5.9 says what goes with it and what has to
replace the thirteen settings that live nowhere else; the map is virtual-key based and layout-dependent (I-5);
held keys stick across focus loss (I-6); and closing the window calls `ExitProcess` from inside a
ported routine because the blocking read is a nested message pump (I-7).

**How is time done, and can it be modernised?** There is no timer. The whole program is paced by
`IDXGISwapChain::Present(1, 0)` — one turn of the outer loop is one vertical blank of *the player's
monitor* — and three separate `double` accumulators over `steady_clock` decide how many game steps
each turn is worth, against a cycle-cost model of the 6510 measured with the oracle. The flight and
title pacing are right in principle (§6.17, §6.110, §6.114). What is wrong: `DELAY` — every docked
pause, debounce and beep — counts *presents*, so it lasts a different time on a 60 Hz, 144 Hz or
165 Hz panel; the 50 Hz PAL tick §6.17 asked for was never built (T-1). The three accumulators have
three different backlog rules and the stall flag is thrown away (T-2). The hold loops re-upload and
re-draw an unchanged canvas every vsync while waiting, and spin a core when the swap chain is
occluded rather than minimised (T-3). The cost model runs on the NTSC clock while the sound
interrupt runs on the NTSC frame and §6.17 says the default should be PAL (T-4). And the one hard
sequencing fact: **every remaining cost measurement must be taken before M6-b takes the live
oracle out of CI** — M6-f deletes the interpreter, but M6-b is when nothing runs it — or it can
never be taken (T-5).

The plan (§6) is twelve slices in two tracks. The first slice on each track is small and fixes the
defects a player meets in the first five minutes; the later ones are the modernisation proper — an
`InputFrame` per step, `TT217` ported into the library where it can be compared, a layered key map
on scan codes, a single frame clock with a simulated vertical blank, and the docked screens turned
into C++20 coroutines so that the nested pump, the `ExitProcess` and the three call depths of
`Present` all go away without the screens ceasing to be a line-by-line port.

---

## 1. Input as it is today

```
Windows message            Outpost::Window                 Outpost::GameShell / Main.cpp        Elite::Game
───────────────────        ─────────────────────────       ───────────────────────────────      ──────────────────────────
WM_KEYDOWN/WM_SYSKEYDOWN ─► PressKey(vk, down=true)
                              CursorKeysFor(vk) ─► m_held[axis], m_held[shift]     (the chart's TT17 pair)
                              C64KeyFor(vk)     ─► m_held[pos] = true
                                                ─► m_pressed.push_back(pos)        EVERY key-down, repeats included; cap 16, drop oldest
WM_KEYUP/WM_SYSKEYUP     ─► PressKey(vk, false) ─► m_held[...] = false             (nothing leaves the queue)

                            Held(pos)  ◄──────────────── GameShell::Held ◄──────── Elite::ScanKeyboard   (RDKEY: 65 Held() calls, lowest held = thiskey)
                            TakeKey()  ◄──────────────── GameShell::NextKey ◄────── every docked prompt  (TT217: pop; pump-and-present while empty)
                            TakeKey()  ◄──────────────── Main.cpp, once per step ─► Game::Step / StepDocked / StepPaused (thiskey → TT102)
                            FlushKeys()◄──────────────── GameShell::Flush ◄──────── FLKB call sites (three)
```

Seven pieces, and what each decides:

| Piece | Where | What it decides |
|---|---|---|
| Virtual key → C64 matrix position | `KeyMap.cpp` `BINDINGS`, `CURSORS` | The modern layout ADR-005 §4 records: arrows steer, `,`/`.` speed, F1–F6 screens, F7–F10 views, letters keep their C64 letter, Space and the number row keep their C64 positions on top. Many-to-one by design. |
| The held table | `Window::m_held[65]` | Level state per position, written on down and up. Read by `Held` only. |
| The press queue | `Window::m_pressed` | Edge state — but an edge per `WM_KEYDOWN`, so a held key produces one entry per OS auto-repeat. |
| The dispatch key | `Main.cpp` `TakeKey` per step | `thiskey`: the queue's head, or zero. `TT102` compares positions. |
| The scan | `Elite::ScanKeyboard` | `RDKEY` minus the hardware: zeroes the logger, walks 65 positions through `Keyboard::Held`, forgets the nine act-keys off the space view, and — the port's own rule, marked as such — forgets the four steering keys on a chart because the arrows do both jobs. |
| The blocking read | `GameShell::NextKey` | `TT217` as a queue pop; `TRANTABLE` turns the position into the character the screens compare. Blocks by running `Turn()` (pump, audio, present) until the queue has something. |
| The pause loop | `Main.cpp` / `Game::StepPaused` | One pass of `FREEZE` per queue entry. |

The two halves are consistent with each other in one respect only: both are keyed by the C64 matrix
position, so `TT102` and `TRANTABLE` both work from one map. In every other respect the *level*
half is the original's and the *edge* half is the port's invention.

---

## 2. Findings — input

Severity: **A** a player hits it in normal play and it is wrong; **B** wrong but rare or documented;
**C** not modern, no player-visible defect today.

### I-1 (A) — `NextKey` is a queue pop; `TT217` is release-then-press with a debounce

The original, C64 branch of `tt217.asm`:

```
.t     LDY #2 / JSR DELAY      two vertical syncs
       JSR RDKEY / BNE t       loop until NO key is held
.t2    JSR RDKEY / BEQ t2      loop until a key is held
       LDA TRANTABLE,X
```

Three properties follow. A key that was down when the prompt appeared is **discarded**, not
delivered. A key held down produces **one** character, however long it is held. And two presses
need a release between them. The port's read has none of the three: `Window::PressKey` queues on
every `WM_KEYDOWN`, `Shell.cpp` pops one per `NextKey`, and the comment in `Window.cpp` justifies
keeping auto-repeat because "holding a cursor key is how the crosshairs are moved across the chart"
— which is true and irrelevant, because the crosshairs are moved from the *held* table by `TT17`
and never from the queue.

What a player sees. The name editor ends on RETURN and the save report ends on "press any key"
(`SaveGame.cpp:67`): RETURN held past Windows' repeat delay answers both. `gnum` reads a quantity
digit by digit (`Market.cpp:462`): a "1" held past the delay types "11". Pressing "3" for the
equipment screen and holding it a beat selects the third item. The disk menu, the yes/no question
(`SaveGame.cpp:116`) and the buy screen all read the same queue. Keys typed while a screen is still
drawing are kept and delivered to whatever asks next, which is the type-ahead the original could not
do and the reason a fast player sees screens go past. The three `FLKB` sites the original has
(`NameEntry.cpp:50`, `SystemScreen.cpp:138`, `SetUpTradeScreen`) are not enough, because the
original never had a queue to empty — its `FLKB` clears a KERNAL buffer `RDKEY` does not read.

### I-2 (B) — `TT102` is dispatched from the edge queue; the original dispatches the held key

`MLOOP` reaches `TT102` with `thiskey`, which `RDKEY` produced from the *matrix* on that pass. The
port hands it the queue's head. Docked, the difference is invisible except that a held F-key
redraws its screen at the OS repeat rate instead of every pass; in flight, `Game::Step` reads the
queue once per step and the pause key (`CPX #&40`) is therefore an edge where the original's is a
level. Not a defect, but the second half of I-1's inconsistency, and the fix is the same object: one
`InputFrame` carrying both.

### I-3 (A) — The pause screen can be entered and not left

The C64 numbering the port uses is `64 - (KERNAL matrix code)`, which is why RETURN is 63, F1 is
60 and Space is 4. Reading `dk4.asm`'s C64 branch against `KeyMap.cpp`:

| Key in `DK4` / `TGINT` | 6502 | Position | C64 key | Bound in `KeyMap.cpp` to |
|---|---|---|---|---|
| freeze | `CPX #&40` | 64 | INST/DEL | Backspace — **so Backspace pauses the game**, faithfully |
| resume | `CPX #&0D` | 13 | CLR/HOME | **nothing** |
| quit to title | `CPX #&07` | 7 | ← | Escape (also the escape pod) |
| sound off | `CPX #&02` | 2 | Q | **nothing** |
| sound on | `CPX #&33` | 51 | S | **Up arrow** (the pitch key that sits on S's position) |
| toggle 1, `TGINT[0]` | `&01` | 1 | RUN/STOP | **nothing** |
| toggle 3, `TGINT[2]` (author names, which also gates toggles 11–13) | `&29` | 41 | X | **Down arrow** |
| toggle 7, `TGINT[6]` | `&1B` | 27 | K | **nothing** |
| toggle 13, `TGINT[12]` | `&24` | 36 | B | **nothing** |

The other nine toggles (A, F, Y, J, M, D, P, C, E) reach their letters. So: press Backspace in flight
or in dock and the game freezes; the only bound key that leaves `FREEZE` is Escape, which is `JMP
DEATH2` — the death sequence and the title screen. Four options cannot be toggled at all and one
is on the wrong key. Every one of these positions is a `KeyMap` row away; nothing in `GameLogic`
is wrong. The comment in `Game::StepPaused` also claims the OS repeat rate "already provides" the
debounce `DKS3`'s twenty-frame `DELAY` gives — it does not: Windows repeats at about thirty a
second after its initial delay, `DKS3` allows one flip per 0.4 s.

The table is kept as the record of what was found. The fix is not the binding: the owner ruled on
2026-09-08 that the pause functionality may go, and §5.9 removes the screen and gives its thirteen
settings a home that does not need a frozen game to reach.

### I-4 (B) — The joystick is half gone, and the half that stays is reachable from the keyboard

`TITLE` (`Flight.cpp:381,434`): `JSTK` is set to `&FF` before the loop and `INC JSTK` runs only
when the loop is left by a non-fire key. The upstream comment says it plainly: the prompt "reads as
a choice of two equal ways to continue and is actually the joystick question". In the port `A` is
the fire key, so `A` on the title screen selects a joystick that does not exist. `DOKEY`'s
joystick branch (`Controls.cpp:293`) then runs on every frame and re-centres both axes whenever
their keys are up, which defeats `DAMP`. `TT17afterall`'s joystick branch for the chart crosshairs
is *not* ported (`FlightLoop.h`), so the port is in a state neither machine could be in: joystick
flight controls, keyboard chart controls. `JSTGY` and `JSTE` are toggles 5 and 6 of the pause
screen and are read by nothing. All three bytes are in the replay digest and in the commander file
and stay; what has to change is who is allowed to set `JSTK` (§5.1).

### I-5 (C) — Virtual keys, no scan codes, no character path

`KeyMap` binds `VK_*` values. Positional controls (the arrows, `,` and `.`) are safe; the letters
are not — on a QWERTZ or AZERTY layout `A`, `M` and the rest move, `VK_OEM_3` is `@`/backquote
only on UK/US layouts, and none of the numpad digits or numpad Enter is bound. There is no
`WM_CHAR` path, so the line editor and `gnum` see `TRANTABLE`'s C64 characters rather than what
the layout would type; that is correct for fidelity (the C64's character set is what the screens
compare against) but it is the reason the digit row and the F-keys have to share positions
(ADR-005 §4). The modern shape is: **scan codes** for anything positional (flight controls,
crosshairs), **virtual keys** for the letters and digits the game names, and a **per-mode layer**
so the pause screen's letters and the text editor's digits do not have to be the flight map's
(§5.3). Raw Input (`WM_INPUT`) is *not* needed for this; the scan code is in `WM_KEYDOWN`'s
`lParam` bits 16–24 and that is enough for a game that does not need to distinguish two keyboards.

### I-6 (A) — Focus loss leaves keys held

`Window::OnMessage` handles neither `WM_KILLFOCUS`, `WM_ACTIVATE` nor `WM_ACTIVATEAPP`. Alt+Tab
while holding an arrow leaves `m_held[17]` true until the key is pressed again in this window; the
ship rolls until it is. `WM_SYSKEYDOWN` is also routed into `PressKey`, so Alt+arrow steers. The
fix is a dozen lines and belongs in the first platform slice.

### I-7 (B) — The blocking read is a nested pump, and closing the window is `ExitProcess`

`Window.h` argues the three options honestly and picks the nested pump as the cheapest. Its
costs, now that everything else is built: `Turn()` — pump, audio, present, raster sync — runs at
three call depths (the outer loop, `WaitFrames`, `NextKey`); a resize arrives inside a screen
routine; the window's close is handled by `GameShell::Abandon`, which releases the presenter by
hand and calls `ExitProcess` because most of `GameLogic` is `noexcept` and there is no way to
unwind; and no test drives the real loop, because the real loop is not a function. The state
machine that the port's own `Game::Mode` already spells out for the *outer* loop stops at the
docked screens, which stay procedures with a wait in the middle. §5.4 proposes the way out that
keeps them line-by-line.

### I-8 (C) — Two platform rules inside `GameLogic`

`Elite::ScanKeyboard` drops the four steering positions on a chart view, and the comment says it
is the port's rule because the arrows do two jobs in the modern layout. `PressKey`'s `keys[0] =
_key` and `Game::Step`'s `PAUSE_KEY` test are the original's. The chart rule is a *layout*
consequence and moves into the platform's layered map when the map has layers (§5.3); until then
it is correctly marked and harmless.

### I-9 (C) — What is right and should be kept

`ScanKeyboard` walking down so that `thiskey` is the lowest-numbered held key; `TT102` taking the
position; `HINT` and `CTRL` read live from the matrix; the arrows' two-position `CursorKeys`;
Space keeping position 4; F7 being launch because `f0` is one key. None of the plan below changes a
byte of `GameLogic`'s behaviour for the same key sequence.

---

## 3. Time as it is today

```
Main.cpp Run()                                     GameShell::Turn()                 CanvasPresenter::Present
──────────────────────────────────────────────     ─────────────────────────────     ─────────────────────────
while (shell.Turn())                          ──►  SyncVideoRegisters (COMIRQ1 x2)
  elapsed = steady_clock delta                     SoundOutput::Pump   (fill XAudio2 queue to 4 frames of 737 samples: the SID's own clock)
  mode = game.ModeNow()                            Window::Pump        (PeekMessage loop)
  Paused : one StepPaused per queued key           TakeResize → Resize
  Docked : PlanSteps(elapsed, 1/FlightFrameSeconds(0))   ≤4 passes of StepDocked        minimised → WaitMessage
  Flight : PlanSteps(elapsed, 1/FlightFrameSeconds(ships)) ≤4 passes of Step        ──►  Resolve, upload, draw, Present(1, 0)  ◄── THE ONLY WAIT

Presenter port (reached from inside GameLogic routines):
  WaitFrames(n)        = n × Turn()                                  6502: DELAY — n vertical syncs
  Present()            = 1 × Turn()                                  the tunnel's free vsync
  HoldTitleFrame(d)    = Turn() until TitleTurnSeconds(d) has elapsed   (own steady_clock + own leftover)
  HoldFlightFrame(n)   = Turn() until FlightFrameSeconds(n) has elapsed (own steady_clock + own leftover)
```

The cost model (`Presentation.h`) is the part that is right and measured: a flight frame costs
47,784 cycles with an empty bubble and about 81,000 with anything in it; a title turn costs 15,600
cycles for a dot and 121,276 for the settled wireframe; divided by the NTSC clock, 1,022,727 Hz.
`PlanSteps` is ADR-005 §3's fixed-timestep accumulator with a four-step clamp and a `stalled`
report. The sound is the one subsystem with its own clock: `SoundOutput::Pump` runs the interrupt
as many times as the XAudio2 queue is short, so the SID advances at 59.8 Hz whatever the display
does.

---

## 4. Findings — time

### T-1 (A) — `DELAY` counts the player's monitor

`WaitFrames(n)` is `n` calls of `Turn()`, and `Turn()` ends in `Present(1, 0)`. So every
`JSR DELAY` in the docked game is measured in *display* refreshes: `TT217`'s two-frame debounce,
`dn2`'s fifty, `DKS3`'s twenty, `ReadLine`'s eight, the docking pause, the equipment beeps.
`wscan.asm`'s own comment says the machine refreshes at 50 Hz PAL or 60 Hz NTSC; §6.17 concluded
"the port exposes it and defaults to PAL" — and no such tick exists. On a 60 Hz panel a fifty-frame
pause is 0.83 s, on a 144 Hz panel 0.35 s, on a 165 Hz panel 0.30 s, and on a variable-refresh panel
whatever interval the driver chooses. `Shell.cpp` records the 60 Hz case as "the PAL and NTSC
difference rather than a defect in this loop", which was true of the monitor it was written on and
is not a property of the design. The flight and title loops are *not* affected — §6.110 and §6.114
decoupled them — which is why this is the one timing item a player notices and nobody has yet.

### T-2 (B) — Three accumulators, three policies, one flag discarded

`Main.cpp` holds `accumulated` and `dockedLeftover` over `PlanSteps` (clamp at four steps, then
drop the rest and set `stalled`, which the loop reads and discards). `GameShell` holds
`m_spinLeftover` and `m_flightFrameLeftover`, each with its own `steady_clock` sample and a
different rule (drop the backlog when it reaches two periods). The flight accumulator is zeroed on
every docked turn; the death hold's leftover is never zeroed between deaths; `m_lastSpin`'s
zero-initialised time point makes the first elapsed enormous and relies on the clamp. None of this
is a bug a player sees. All of it is why there is no single place to put a stall log, a PAL/NTSC
switch or a speed option, and it is the shape §4.8 of Modernize.md says the executable should
lose.

### T-3 (B) — Waiting means redrawing, and an occluded swap chain means spinning

Each `Turn()` in a hold loop re-resolves the canvas, re-uploads it, re-records the command list and
presents — for a picture that has not changed, thirteen times per flight frame on a 165 Hz panel.
Harmless on a desktop GPU, wasteful on a laptop, and it hides a second issue: `Present(1, 0)`
returns `DXGI_STATUS_OCCLUDED` *immediately* when the window is hidden behind another or on another
virtual desktop, and `Turn()` only idles on `WaitMessage` when the client area is zero. A game on a
hidden desktop runs the outer loop at whatever rate the pump manages. The waitable-object flip
model (ADR-005 §1 already chose flip model) is the standard answer to both (§5.6).

### T-4 (B) — NTSC in the cost model, NTSC in the sound, PAL in the decision

`Presentation.h` and `SoundOutput.h` both say "the NTSC machine this build is for" and use
1,022,727 Hz and a 65 × 263 cycle frame. §6.17 and R3 say the default should be PAL and the choice
exposed. The two are 3.8 % apart in flight rate and 17 % apart in vertical-sync rate — the second
is what `DELAY` will use once T-1 is fixed, so the decision has to be taken then. It is an owner
decision (§7), not a technical one: the GMA85 variant `elite-build-options.asm` configures is the
question.

### T-5 (A for sequencing) — What is unmeasured has to be measured before M6-b

`FlightFrameSeconds` is two bands because the crowded bubble could not be timed (`TACTICS` does not
return for a station in the fixture); the docked pass is paced at the empty-bubble *flight* cost as
a stand-in, so the hyperspace countdown's tick rate and the chart crosshairs' speed while docked
rest on a number nobody measured; `TT16` calls `WSCAN`, so crosshair movement is vertical-sync
quantised on the C64 and free-running here; and the planet is in no measured scene. Every one of
those is a cycle count read off the interpreter. Modernize.md's M6-b takes the live oracle out of
CI and M6-f deletes it; after M6-b the cost model is frozen at whatever it holds, forever. This is the one item in this document
that has a deadline rather than a priority.

### T-6 (C) — Frames the library asks for and the executable drops

`RunLoopTail` returns the two vertical syncs part 5 asks for on a docked pass and `Game::Step`
discards them (correctly, on the flight pass); `StepDocked` does not run `RunLoopTail` at all, so
the Trumbles do not breed while docked; `StepPaused` discards `DKS3`'s twenty, which is moot once
§5.9 removes the screen. The first two are named in the code. With a simulated vertical-blank tick
(§5.5) the executable can honour them; without one it cannot, which is why they are debts of T-1
rather than separate items.

### T-7 (C) — Audio is pumped from the render loop

The SID render is self-clocked, which is the right design, but it is *driven* once per `Turn()`.
Anything that stops the pump — the modal size loop while the title bar is dragged, a long docked
draw, a breakpoint — starves the XAudio2 queue after four frames (about 67 ms) and the sound
stutters. The modern shape is to render on the XAudio2 callback thread from a log the game thread
hands over; §5.7 sizes it as an optional slice because the stutter is the only symptom.

### T-8 (C) — What is right and should be kept

The accumulator is outside `GameLogic` and takes whole steps (ADR-007 §2); the cost model is a
measurement with its limits written beside it; steps are the unit of replay; `Present` blocks
rather than a timer sleeping; the minimised window costs nothing. The plan keeps every one of these.

---

## 5. Target design

### 5.1 `JSTK` is the platform's to set, and today nothing may set it

Rule: the game may believe a joystick is configured only when the platform has one to read. Until
the gamepad slice (I-4 in §6) exists, the title's fire test is answered as "a key" — `JSTK` ends
the title screen at zero — and `DOKEY`'s joystick branch is therefore never entered. When a gamepad
is present, the original's rule returns unchanged: fire button on the title screen selects it, and
the platform then fills `KY3`–`KY7` from the pad exactly as `dojoystick` did. This is a port
decision of the same kind as ADR-005 §4's layout ruling — which physical device reaches `KEYLOOK`
is the platform's — and needs one sentence in ADR-005 §4, not an ADR-001 §6 row: no player-visible
original behaviour is being reproduced *wrongly*, one is being made unreachable until the hardware
it describes exists. The three bytes stay in `Universe`, in the digest and in the commander file.

### 5.2 `InputFrame`, and `TT217` moves into the library

```cpp
namespace Elite
{
  /// One step's worth of keyboard, over the C64 matrix positions (plan §2.1, at last).
  struct InputFrame
  {
    std::array<std::uint8_t, 65> held{};    ///< level: non-zero while the position is down
    std::array<std::uint8_t, 65> pressed{}; ///< edge: went down since the previous frame, and needs a release before it can again
  };

  [[nodiscard]] bool Game::Step(const InputFrame& _input) noexcept;        // thiskey = lowest held, as RDKEY produces it
  void Game::StepDocked(const InputFrame& _input) noexcept;               // StepPaused is gone with the screen (§5.9)

  class Keyboard
  {
  public:
    [[nodiscard]] virtual bool Held(std::size_t _key) = 0;   // the only method left
  };

  /// 6502: TT217 -- two frames, wait for no key, wait for a key, translate. Compared against the
  /// oracle over the CIA matrix the interpreter has modelled since M6-0-a-4.
  [[nodiscard]] std::uint8_t ReadKey(Universe& _universe, Ports& _ports) noexcept;
}
```

Three consequences. Auto-repeat and type-ahead disappear **by construction**, because there is no
queue: `pressed` is computed by the platform as a rising edge of `held` between two frames, and
`ReadKey` needs a release like the original does. The platform's `Keyboard` shrinks to the one
question the hardware answers, which is where M3-b-3d already left `RDKEY`. And `NullSeams`,
`ScriptedKeys` and the recorder M6-a plans all script the same thing — a sequence of `InputFrame`s
— so the docked-session fixture stops needing a "press RETURN and N alternately" escape hatch and
drives the screens the way a player does. `FLKB` becomes a no-op at the seam or leaves the port;
which one is a reading of `flkb.asm`'s C64 branch during the slice.

`ReadKey` still blocks in the sense that it calls `Presenter::WaitFrames` and loops on `Held`; the
nested pump survives this slice and dies in §5.4. What changes here is the *semantics*, which is
the defect.

### 5.3 A layered key map on scan codes

`KeyBinding` gains a scan code and a layer:

```cpp
enum class KeyLayer : std::uint8_t { Flight, Docked, TextEntry };

struct KeyBinding
{
  std::uint16_t scanCode = 0;   ///< make code, extended bit folded in; positional, layout-independent
  int virtualKey = 0;           ///< or a VK, for the letters and digits the game names
  std::uint8_t c64Key = NO_KEY;
  std::uint8_t layers = ALL;    ///< bit set of KeyLayer
  const char* what = "";
};
```

Rules. The flight set (`KY1`–`KY7`, the crosshairs) binds by scan code, so the arrows and `,`/`.`
are the physical keys on every layout. Letters and digits bind by virtual key, as now. The
**Docked** layer drops the steering positions, which retires the chart rule inside
`Elite::ScanKeyboard` (I-8) into data. The **TextEntry** layer, selected while `ReadKey` is the
consumer, is where a later phase-6 remapping can give the digits back to the F-keys; it is not
built here, only left room for. The layer is chosen by the executable from `Game::ModeNow()` and
from whether `ReadKey` is the active consumer, which the library exposes as one boolean on `Game`.
There is no Paused layer because there is no pause screen after §5.9; the first draft of this
section had one, on the C64's own letters, and it is the alternative D3 records.

`ShellTests` keeps its table walk and gains one assertion per layer: every position the library
names for that mode has a binding in it. That test alone would have caught I-3.

### 5.4 The docked screens as coroutines, and the pump moves to the top

`Window.h` weighed a game thread, a state-machine rewrite and the nested pump. C++20 offers the
fourth option the file could not consider in September: a screen routine keeps its shape,
`ReadKey` and `WaitFrames` become awaitables, and the outer loop resumes the screen when the
platform has what it was waiting for.

```cpp
Elite::Task<void> BuyScreen(Universe& _universe, Ports& _ports)   // was void; body otherwise unchanged
{
  ...
  const std::uint8_t key = co_await ReadKey(_universe, _ports);   // was = ReadKey(...)
  ...
}
```

The library gains a small `Task` type and a `Suspend` awaitable with no allocator surprises (a
fixed-size frame arena in `Game`, because `noexcept` and the determinism guard forbid the default
heap path from throwing its way into the outer loop). `Game::Step` drives the active task one
resumption at a time; a `WaitFrames(n)` suspends until `n` simulated vertical blanks have been
delivered (§5.5); a `ReadKey` suspends until the frame stream satisfies `TT217`'s three waits.
Nothing in a screen's *ordering* changes, so the character-stream and canvas comparisons in the
suite are unaffected, and the replay digest stays step-for-step because a step is still a step.

What it buys: `Turn()` is called from one place; the window's close is a flag the outer loop reads
and the presenter's destructor runs; the suite can drive the real loop — `Run()` becomes a function
over `Game` and a `Platform`, which is what Modernize.md §4.8 wanted; and the M6-a recorder records
`InputFrame`s and step counts, nothing else. What it costs: every docked routine that reaches a
wait, transitively, changes its return type and its call sites gain `co_await` — mechanical, wide
(the screens, the missions, the disk menu, the line editor, `TITLE` and `BRIEF`'s waits), and the
one slice in this plan whose diff is large. The portable runner's g++ 13 and MSVC v145 both build
coroutines under `-std=c++20`; `check_gamelogic.py` needs no new rule because `<coroutine>` brings
no clock, no float and no I/O.

Stress-tested against the alternatives once more, because Window.h's reasoning was sound when it
was written: the game thread still needs the canvas snapshotted under a lock and an answer for a
thread parked in a wait at close; the state-machine rewrite still stops the screens being a
line-by-line port, which ADR-001 exists to keep until M6 says otherwise. Coroutines are the
state-machine rewrite done by the compiler, with the source left readable as the routine it ports.

### 5.5 One clock, one machine, one scheduler

```cpp
namespace Outpost
{
  struct MachineTiming            // chosen once at start-up; default is the owner's call (§7)
  {
    std::uint32_t clockHz;        // 985,248 PAL, 1,022,727 NTSC
    std::uint32_t cyclesPerFrame; // 63*312 PAL, 65*263 NTSC
  };

  class FrameClock                // the ONLY reader of steady_clock in the program
  {
  public:
    void Tick() noexcept;                            // once per outer-loop turn
    [[nodiscard]] std::int64_t ElapsedCycles() noexcept;  // integer 6510 cycles since the last Tick, at MachineTiming::clockHz
  };

  class Scheduler                 // replaces PlanSteps and both hold loops; testable on both legs
  {
  public:
    struct Plan { int steps; int verticalBlanks; bool stalled; };
    [[nodiscard]] Plan Advance(std::int64_t _elapsedCycles, std::uint32_t _stepCostCycles) noexcept;
  };
}
```

The unit is the cycle, as an integer, which removes the `double` from the loop and makes the
clamp and the backlog rules one rule. Two virtual clocks derive from it: the **step budget**,
charged per step at the cost model's price (`FlightFrameSeconds` becomes `FlightFrameCycles`; the
title turn likewise), and the **vertical blank**, one every `cyclesPerFrame`. `WaitFrames(n)` and
`Present()` consume vertical blanks; `HoldTitleFrame` and `HoldFlightFrame` charge cycles. The
display's refresh decides only when the canvas is *shown*. A stall is counted and reported through
`OutputDebugString` and a counter the title bar can show, which is what ADR-005 §3 asked for and
`(void)plan.stalled` does not do. Both accumulators are reset on a mode change in one place.
`ShellTests::TheStepPlannerNeverSkipsOrDoublesSilently` moves over whole; `TheTitleShipIsPacedBy
WhatATurnCosts` gains a vertical-blank case, and a new test asserts that fifty vertical blanks are
one second at PAL and five sixths at NTSC regardless of how many presents happened.

### 5.6 The presenter: a waitable swap chain, and present only what changed

`DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` with `SetMaximumFrameLatency(1)`; the outer
loop waits on the object (which is the vertical-blank pacing `Present(1, 0)` gives today) and calls
`Present` only when the canvas generation counter — one byte on `Canvas`, bumped by every write
that changes the planes — has moved. When `Present` reports `DXGI_STATUS_OCCLUDED` the loop waits
on the object *and* `MsgWaitForMultipleObjects` with a 100 ms cap, so a hidden window costs a
wake-up ten times a second rather than a core. On a variable-refresh panel the frame clock, not the
present interval, paces the game, so the display can run at whatever rate it likes. This slice is
the executable only and has no oracle; its acceptance is the presenter still passing the golden
screenshot and a Task Manager reading.

### 5.7 Audio on its own thread

`IXAudio2VoiceCallback::OnBufferEnd` renders the next SID frame on the audio thread from the
`SidWriteLog` the game thread published (a two-slot exchange, one atomic). `SoundOutput::Pump`
becomes "publish the log"; the queue never starves while the pump is stalled. Optional, because T-7
is the only symptom; listed because it is the one place the program would gain a second thread and
that deserves a sentence in ADR-005 §2 rather than appearing in a diff.

### 5.8 Gamepad, later, on the same frame

XInput (Windows SDK, pre-approved) or `Windows.Gaming.Input` fills `InputFrame::held` for `KY3`–
`KY7` and the view keys, exactly as `dojoystick` filled `KEYLOOK` — the left stick or d-pad for
roll and pitch, a button for fire — and the title's fire test reads the pad's button. `JSTGY` and
`JSTE` come back to life as the pad's axis inversion, which is what they were. Nothing in
`GameLogic` changes; the layered map gains a `Gamepad` source column. This is plan §6 Phase 6's
"key remapping and gamepad" item and needs its own ADR when it comes; the plan here only makes it a
platform-only change when it does.

### 5.9 Removing the pause screen, and what has to replace it

**What it is.** `DOKEY` falls into `DK4` on every frame; `CPX #&40` freezes on INST/DEL, and
`FREEZE` is a loop of `WSCAN`, `RDKEY`, `Q` for sound off, `DKS3` over ten toggles (thirteen behind
`PATG`), `S` for sound on, ← for `JMP DEATH2`, CLR/HOME to return. In the port it is the
`PAUSE_KEY` test in `Game::Step`, `m_paused` and `Mode::Paused`, `StepPaused`, the paused branch
of `Main.cpp`, and `PauseScreen.*` with `PauseScreenTests` and two cases of `GameTests`. Ledger row
148 files `dk4` and `mutokch` under it. Nothing in `mutants.json` names the unit, so the floor is
untouched.

**What depends on it, and this is the part to be clear about before deleting anything.** The
thirteen bytes `DKS3` walks are the game's *only* settings interface, and they are not in the
commander file — `Commander.h` and `SaveGame.cpp` name none of them — so today every setting
resets at launch and can be changed only by freezing the game. Four of them matter beyond taste:
`PATG` gates five `AND PATG` tests in the spawner and the docked pass's two-frame delay, so it is
a difficulty setting wearing a credits toggle; `MUTOK` is the docking music, which is the one a
player wants mid-session; `DAMP` and `DJD` are how the ship feels; and `JSTK`/`JSTGY`/`JSTE` are
the joystick, which §5.1 and §5.8 already take over. `DNOIZ` sits beside them. And the ← key is
the only way from a flight back to the title screen without dying.

**What replaces it.**

1. **A settings file**, `Outpost/Settings.*`: a key=value text file under `LocalAppData`, read at
   start-up into the same `Universe` bytes `DKS3` wrote, written by hand (the "no external
   libraries" rule; C++/WinRT's `ApplicationData` would work packaged and not unpackaged, and R8
   says both launch paths must). The defaults are the game's. `NoteMusicSwitch` — `MUTOKCH`'s
   "start the music now if the computer is flying" — survives as the routine a runtime settings
   change calls; `ToggleOption`, `ApplyOptionKey` and `PressPauseKey` go. A settings *screen* is
   phase 6 and gets its own ADR; until then the one thing lost against the C64 is changing an
   option without restarting, which is stated here so it is not filed as a regression.
2. **Auto-pause**, in the scheduler (§5.5): while the window is inactive or minimised, `Advance`
   plans zero steps and *drops* the elapsed cycles rather than banking them, the audio voice
   stops, and on activation the game resumes on a whole step. This is what a windowed player
   means by pause, it already half-exists (minimised → `WaitMessage` → the clamp), and it needs no
   key. A deliberate in-focus pause key is a phase-6 question, not a port one.
3. **Quit to title** is the window's close, which saves nothing and loses nothing (the commander is
   on disk or it is not — `Shell.cpp`'s own argument). A menu is phase 6.
4. **Backspace is delete** and nothing else; position 64 keeps its binding for the line editor.

**What the rules say.** This is the first deliberate removal of an original *feature* rather than
of a hardware read, and ADR-001 §4 gates phase-6 changes on "the suites green with the option
off" — there is no option to turn off. So it is recorded as an owner ruling in ADR-005 §4, with a
pointer from ADR-001 §4, and ledger row 148 moves `dk4`, `dks3` and `mutokch`'s toggle half to
Dropped with the sentence "pause screen removed by owner ruling 2026-09-08; the toggles are
`Outpost/Settings.*`". `m_paused` was never in the digest; `FlightReplayTests`'s recorded key
stream is checked for `&40` before the test in `Game::Step` is deleted, and if it holds one the
record is re-taken under rule R-e with this section as the named change.

---

## 6. The plan

Two tracks, each starting with the slice that fixes what a player hits first. Effort is in
sittings, on this corpus's scale. **Gate** is what turns the slice green; every slice runs
`python tools/check_all.py` and the suite, and every one that touches `Outpost/` lands through
the Windows CI leg (R15).

### Track I — input

| Slice | What | Gate | Ratchets and checks touched | Sittings |
|---|---|---|---|---|
| **I-0 Remove the pause path, and focus** | §5.9 items 3 and 4: the `PAUSE_KEY` test leaves `Game::Step`; `m_paused`, `Mode::Paused`, `StepPaused` and `Main.cpp`'s paused branch go; `PauseScreen.*` shrinks to `NoteMusicSwitch`; `PauseScreenTests` and the two `GameTests` cases go with what they compared; ledger row 148 and ADR-005 §4 amended. `Window`: clear `m_held` on `WM_KILLFOCUS`/`WM_ACTIVATEAPP(FALSE)`, ignore `WM_SYSKEYDOWN` with Alt held for anything but F10. | Suite green; the replay record checked for `&40` first (§5.9); play: Backspace deletes and does nothing else, Alt+Tab with an arrow held. | `inventory.py --strict` on the row; `check_outpost.py` on `StepPaused`'s removal | 1 |
| **S-1 Settings file** | §5.9 item 1: `Outpost/Settings.*` reads the thirteen bytes and `DNOIZ` into `Universe` at start-up, defaults the game's, malformed lines reported and ignored (AGENTS.md §5: a user's file is a diagnostic, not a crash). `ShellTests` covers the parser on both legs. | Every byte `OptionBlock` named has a key; a file with each set is honoured; a bad file launches with defaults and a message. | none | 1 |
| **I-1 `TT217` into the library** | `Elite::ReadKey` over `Keyboard::Held` and `Presenter::WaitFrames`, compared against the oracle's `TT217` on the CIA matrix with held-then-released scripts; `Keyboard::NextKey` removed; `Window::m_pressed` removed; `Main.cpp` derives `thiskey` per step from the held table (lowest held, as `RDKEY`); `ScriptedKeys` and `NullSeams` become held-key scripts with releases. `FLKB` resolved by reading its C64 branch. | Oracle comparison of `ReadKey`; `DockedSessionTests` green with no escape-hatch keys; the replay digest unchanged (no step changed); play: hold RETURN through a save, hold "1" on the buy screen. | `effects-seams` unchanged (`Keyboard` stays a port, smaller); `check_outpost.py` arity on `NextKey`'s removal | 2 |
| **I-2 `InputFrame` and the layered map** | `InputFrame` as §5.2; the two `Step`s take it; `Window` produces one per outer turn (held from scan codes for the flight set, edge computed with release); `KeyBinding` gains `scanCode` and `layers`; the Docked layer retires the chart rule from `ScanKeyboard`. | `ShellTests` per-layer completeness; `ControlsTests` for `ScanKeyboard` unchanged on the space view; replay digest unchanged. | `main-lines` (250) — `Main.cpp` shrinks; `outpost-elite-names` (61) may fall, lower the ceiling in the same commit | 2 |
| **I-3 `JSTK` at the seam** | §5.1: the title's fire test cannot select a joystick until a pad exists; one sentence in ADR-005 §4; `StartUpTests` gains the case. | Oracle comparison of `TITLE` still green (the scripted matrix presses a non-fire key); play: `A` on the title, then fly, damping present. | none | 0.5 |
| **I-4 Coroutines** | §5.4: `Task`, the awaitable `ReadKey`/`WaitFrames`/`Present`/holds, the screens converted, `Run()` a function over `Game` and a `Platform` that answers the four ports; `GameShell` and `FlightSession` absorbed as Modernize.md §4.8 says; `Abandon` deleted. | Every existing comparison green without change to what it asserts; a new `GameLoopTests` case drives `Run()`'s loop through a docked session, a launch, a pause and a close on the Linux leg; the digest unchanged. | `effects-seams` 5 → 4 if `Presenter` and `Keyboard` fold into `Platform`; `main-lines` falls; `aggregate-refs` unchanged (8) | 4–5 |
| **I-5 Gamepad and remapping** | §5.8 plus a remap file in `LocalAppData`; phase 6, own ADR. | Own ADR's. | — | 3, later |

### Track T — time

| Slice | What | Gate | Ratchets and checks touched | Sittings |
|---|---|---|---|---|
| **T-0 Measure while the oracle exists** | `FlightLoopTests`: a crowded-bubble scene with `TACTICS` returning (a station whose AI path terminates, or the station trapped and its cost added from a separate measurement); a scene with the planet; the docked `MLOOP` pass; `TT16`'s `WSCAN`. Recorded as rows of `FLIGHT_FRAME_COSTS` and a new `DOCKED_PASS_COST`, each asserted by a test as the title curve is. | The new rows exist and the tests assert them; §6 journal entry naming the numbers as history. **Before M6-b.** | `check_counts` if any doc states the count | 2 |
| **T-1 `MachineTiming`, `FrameClock`, `Scheduler`** | §5.5, in `Presentation.*`; `PlanSteps` and both hold loops replaced; `WaitFrames` counts simulated vertical blanks; the stall log; PAL/NTSC as one constant set with the default the owner rules; `SoundOutput` takes the same `MachineTiming`; **auto-pause while inactive** (§5.9 item 2). | `ShellTests` moved and extended (vertical-blank cases, the inactive case plans zero steps and banks nothing); play on a 60 Hz and a high-refresh panel: `dn2`'s beep pause is one second on both at PAL; Alt+Tab away for a minute and back, the game is where it was. | `main-lines`; ADR-005 §3 amended | 2 |
| **T-2 Honour the dropped frames** | With T-1's tick: `StepDocked` runs `RunLoopTail` and the executable waits its two blanks. | Trumbles breed while docked, compared against the oracle's `MLOOP` on a docked pass; the digest re-taken with a journal entry naming the defect (rule R-e). | `mutants.json` gains a mutant for the docked tail | 1 |
| **T-3 Waitable swap chain** | §5.6: latency-waitable flip model, canvas generation counter, present-on-change, occlusion idle. | Golden screenshot unchanged; CPU at rest with the window hidden. | ADR-005 §1 one paragraph | 1 |
| **T-4 Audio thread** | §5.7. Optional. | `SidRenderTests` unchanged; no stutter while dragging the title bar. | ADR-005 §2 one paragraph | 1 |

### Sequencing

```
T-0 ──────────────────────────────────────────────► (must land before Modernize.md M6-b)
I-0, I-3 ── independent, a day each, first;  S-1 right after I-0 (the settings the removal orphans)
I-1 ──► I-2 ──► I-4 ◄── T-1 (I-4 wants the simulated blank; T-1 wants nothing from I)
                 T-1 ──► T-2, T-3 ──► T-4
I-5 after I-4 and its own ADR
```

I-0 and I-3 are the two a player meets and cost a day between them; S-1 follows I-0 in the same
week, because removing the screen without it leaves `PATG` and the docking music with no way to be
set. I-1 is the first real fix and is oracle-comparable, which is why it comes before the platform
changes rather than after. T-1 can run beside I-1 and I-2 on the other track. I-4 is the slice
that makes the executable the two hundred lines ADR-004 §1 described, and it is deliberately last
on its track so that every seam it folds has been made small first.

### Sequencing against Modernize.md's M6

Asked on 2026-09-08 whether this plan conflicts with M6 or can run beside it: it can run beside it,
under one rule and two ordering constraints, and no slice in either plan undoes the other's work.

**The rule is M6-b's.** Once recorded fixtures answer the suite instead of the live oracle, nothing
new can be compared against the original, and a test that asks the oracle something it did not
ask while the interpreter was present fails by design (§4.10, R19). So every slice here that
changes or adds LIBRARY behaviour lands before M6-b, and each is the same kind of work M6-a is
doing with its four gaps:

| Slice | Why it is bound to M6-b | Lands |
|---|---|---|
| I-1 | `ReadKey` is an oracle comparison; it also changes how the fixtures' scripted keyboards press keys, which changes the call sequence some tests make | before M6-a's recording run, or with one re-record while the interpreter is present |
| T-2 | compares the docked `MLOOP` pass against the oracle and moves the replay record under rule R-e | before M6-b |
| T-0 | needs `Cpu6502` to count; the cycle tests run against the live interpreter, which M6-b takes out of CI | before M6-b — the hard cliff, not M6-f |
| I-3 | must not touch the compared `TITLE`; if the replay script dismisses the title with the fire key the digest moves, which is a re-record | before M6-b |
| I-0 | deletes a *Port* row and its oracle tests; M6-a's coverage review reads the ledger's Port rows | before M6-a's review, so the review does not demand a test for a routine that no longer exists |

**The two ordering constraints are about files, not behaviour.** M6-c renames every 6502-shaped
identifier and M6-d rewrites every transcribed comment, across the same docked screens, `Game.*`
and `Controls.*` that I-2 and I-4 rewrite. I-2 lands before M6-c starts. I-4 lands either before
M6-c or after M6-d, and after is the better order: M6-d is the eight-to-ten-sitting slice, and a
coroutine diff over freshly rewritten comments reviews more easily than the reverse.

**Everything executable-only is free.** T-1, T-3, T-4, S-1 and the window half of I-0 touch
`Outpost/` and `Presentation.*` only; the oracle never sees them and M6 never edits them.

What this plan does to M6's own claims: nothing permanent. `ReadKey`'s comparison is recorded like
any other and the input path is pinned by fixtures after M6-b exactly as the rest of the library
is. One documented number moves — Modernize.md's "five seams, final for M6" becomes four if I-4
folds `Presenter` and `Keyboard` into a `Platform` — and that is a sentence to amend, not a
conflict.

So the interleaving is: I-0, S-1, I-3, T-0, I-1 and T-2 now, beside M6-a's four gaps and before
the recorder runs; T-1 and T-3 whenever; I-2 before M6-c; I-4 after M6-d. **Ruled 2026-09-08 by the
owner: the first six run now.**

### What the plan does to the documents

ADR-005 §3 gains the simulated vertical blank, the auto-pause and the `MachineTiming` default; §4
gains the `JSTK` rule, the layered map and the pause screen's removal, with a pointer from ADR-001
§4; §1 and §2 one paragraph each if T-3 and T-4 land. `Source-Inventory.md` row 148 moves `dk4`,
`dks3` and `mutokch`'s toggle half to Dropped. Modernize.md §4.8's "one `Platform` class" and
§4.4's `Step(const InputFrame&)` are what I-2 and I-4 build, so those sections gain a ✅ and a
pointer here rather than new text; §4.4's mode diagram loses its Paused node. Plan §5.4 is rewritten
to describe the frame rather than the key table. `AGENTS.md` §5's determinism list is unchanged;
`<coroutine>` is not on the banned list and should not be.

---

## 7. Decisions needed from the owner

Three, and the plan proceeds on the stated default for each until ruled.

| # | Question | Default assumed |
|---|---|---|
| D1 | **PAL or NTSC as the machine the port times against?** §6.17 said PAL; every constant in the tree says NTSC; the GMA85 build variant is the fact that decides. Affects flight rate by 3.8 % and every docked pause by 17 %. | **NTSC**, because that is what is measured and shipped today, with PAL selectable. Reversed in one constant if the variant says otherwise. |
| D2 | **May the docked screens become coroutines (I-4)?** It is the largest diff in this plan and changes the signature of every routine that waits. The alternative is to keep the nested pump with I-1's semantics fixed, which removes the defects and leaves the architecture. | **Yes**, sequenced last on its track. |
| D3 | **The pause screen.** Ruled 2026-09-08: it may be removed, and §5.9 removes it. Recorded here because the alternative was fully worked and is the fallback if the settings file (S-1) is not wanted: bind the C64's own keys — Backspace freezes, Home resumes, Escape quits, `Q`/`S` the sound, the thirteen `TGINT` letters their letters — behind a Paused layer of the map. | **Removed**; S-1 carries the settings. |

---

## 8. What this plan does not do

It does not change what `GameLogic` computes for a given sequence of frames: every slice's gate is
the existing comparisons green and the replay digest unmoved, except T-2, which fixes a named gap
under rule R-e, and I-0, which removes a feature by ruling and says so in the ADR. It does not add
a library. It does not build the gamepad or the remap file; it
makes them platform-only. It does not measure the rendering pipeline's own cost on the PC, because
nothing here is CPU-bound — the outer loop's expense is the wait it is supposed to be doing. And it
does not touch the sound's clock, which is the one clock in the program that was already right.
