# Platform — rendering, input and time as one frame

**Status:** design, opened 2026-09-09 against `f50775b`; **nothing in it is built. Its four decisions
and four more on the ADRs were RULED the same day** — §12 has the eight outcomes, and every ADR they
touch carries its amendment. It replaces
[Archive/Rendering.md](Archive/Rendering.md) and [Archive/InputTimer.md](Archive/InputTimer.md),
two plans for one layer written a day apart with a seam between them that the program does not
have: the frame boundary the first wants *is* the vertical blank the second wants, the coroutine
that removes the second's nested pump is what makes the first's present a frame, and the surface
the first splits in two is the snapshot that lets the second present from one place. Both are kept
whole in `Archive/` as the record of what was found and of the ten slices built from them; §1 says
what was still live on the day and where it is now. §4 is the list of decisions the owner is asked
for, with a recommendation against each; the track proceeds on the stated default until ruled.

**What it is for.** Three things the owner asked for on 2026-09-09, in the order they constrain each
other: adopt what is standard practice for rendering, input and time in a native game; change
nothing a player would recognise as the look or the feel; and make the game **more reactive**. §0
says what the third one can and cannot mean inside the second, because two of its three components
are free and the third is a ruling.

**What was read.** Every file of `Outpost/`; `GameLogic/Game.*`, `Ports.h`, `Presenter.h`,
`Controls.*`, `Picture.*`, `Canvas.h`, `Universe.h`, `GameLoop.cpp`, `Flight.cpp`, the four `*2x`
headers; `Tests/GameLogicTests/ShellTests.cpp`, `FlightReplayTests.cpp`, `DockedSessionTests.cpp`,
`NullSeams.h`, `FlightPort.h`; `tools/check_twins.py`, `check_counts.py`, `modernize_ratchet.json`;
ADR-001, ADR-005, ADR-007, ADR-008, ADR-009; Resolution.md §3, §7, §8; Modernize.md §2.1, §4.4,
§4.5, §4.8, §5; plan §2.1, §5; both archived documents whole. Every count below names the command or
the section that took it, and none is a claim about a tree other than `f50775b` unless it carries a
marker.

**Depends on:** ADR-001 §4 (an option is off by default and the suites are green with it off),
ADR-005 (amended by this in §1, §3 and §4 — §7 below), ADR-007 (the executable owns the clock and
the count of passes), ADR-008 §2 and §4 (the twin rule and the hash exclusion, which are the whole
licence for every rendering change here), ADR-009 §2 and §3 (what pins the port, and what re-taking
a record costs now).
**Feeds:** the ADR-005 and ADR-008 amendments in §7, an ADR-010 written at the track's close in the
shape of ADR-007 and ADR-008, Modernize.md §4.8, the plan's Phase 6 row, the risk register.

---

## 0. Three answers first

**What "more reactive" can mean here, and which parts are free.** A key reaches the screen through
three delays, and they have different owners:

| Delay | What it is today (`f50775b`, 60 Hz panel) | Who owns it | Can it move without a visible change? |
|---|---|---|---|
| **Pipeline** — from the OS message to the photons | The window is pumped once a turn and a turn ends in `Present(1, 0)` on a two-buffer flip chain with the default queue depth, so a frame can sit one to three refreshes behind the step that drew it: 17 to 50 ms (§2.3) | The executable | **Yes.** A latency-waitable swap chain with a queue depth of one, the keyboard sampled immediately before the step it feeds, and the present immediately after: about one refresh, measured rather than argued (§3.7, T-3) |
| **Sampling** — how often the game looks at the keys | Once per flight step, and a flight step is paced at what the 6510 took: 47 ms empty, 147 ms with three fighters, 290 ms with eight (`Presentation.h`'s table, InputTimer.md T-0). Docked, once per two vertical syncs | The cost model, which is fidelity | **Only by changing the game logic, which is ruled (§4 D3).** The step rate is the game's feel — the turn rates and the fight are tuned per step — so a knob that scaled it was declined; the ruling is a fixed-rate flight model as its own track after RN-6, with the faithful cadence selectable under ADR-001 §4. What this track does inside the row is accumulate input events into the frame, so a tap between steps is never lost (§3.3) |
| **Stalls** — a hang, a burst, a starve | A title-bar drag stops the pump and starves the audio in 67 ms; a breakpoint or a lid produces four steps in a burst; a hidden window spins; closing a docked game is `ExitProcess` | The executable | **Yes.** One loop, one clock, a time-clamped backlog, an audio thread and auto-pause (§3.2, §3.6) |

So the design spends freely on the first and third rows, touches the second only where no step
changes (event accumulation) and leaves its rate to the ruled track; every slice's gate is pixel
identity against the picture the tree already draws.

**The `2x` files are not old copies, and neither set can go.** `Dashboard2x.cpp`, `Lines2x.cpp`,
`ShipDraw2x.cpp` and `TextPrint2x.cpp` (245, 292, 33 and 106 lines; `wc -l` on `f50775b`) hold the
twelve `/// 2x of:` twins of ADR-008: routines that compute **where** at 640×400, called at the same
site as the faithful routine that decides **what** — the clipping, the heap run, the `Drawn` bit,
the carry the explosion rolls on. The un-suffixed files (`Lines.cpp` 631, `ShipDraw.cpp` 1,553,
`Dashboard.cpp` 438, `TextPrint.cpp` 521) are those faithful routines, and they write the C64 canvas
that every replay digest folds (ADR-009 §2). Delete the twins and the presented picture goes blank —
RS-6 removed the upscale fallback, so "a picture nobody has drawn on resolves blank" (ADR-008,
Consequences). Delete the un-suffixed files and nothing links, because the twins take the faithful
routine's results as arguments. `DashboardPicture2x.cpp` is 1,663 lines of which 1,600 are the
640×112 art table, beside `DashboardImage.cpp`'s C64 art; both are read. **What *is* duplicated is
the surface**: two bitmaps are drawn on every frame and one of them is never seen. That is by
design (ADR-008 §1, "the canvas is not replaced; it is joined"), it was the cheapest evidence the
port had while the oracle read it, and since M6 it is evidence of a different kind. Whether to
retire it is the largest decision in this document and it is §4 D1, priced in §3.4 — it is not a
cleanup and it is not this session's to take.

**What one design changes that two did not.** Read together, the two archived plans have one
finding in common that neither states: **a present is a frame boundary.** Rendering.md needs a
boundary the program lacks (its R-1) and puts it in the loop tail; InputTimer.md needs the presents
out of the routines (its I-7) and puts them behind an awaitable. Do both and the boundary is wherever
a routine says `co_await Present()`, the hyperspace rings and the launch tunnel stop being a case
that "owns a frame boundary of its own" (Rendering.md §12.3), and the three sites RN-0 could not
classify are decided by the gate rather than by a ruling (§3.4). The second thing is the clock: the
simulated vertical blank InputTimer.md T-1 builds for `DELAY` is the same tick the sound interrupt
runs on in the C64 — `COMIRQ1` is a raster interrupt — so one scheduler paces the steps, the waits
*and* the chip, and the program's state becomes a function of its input frames and its elapsed
cycles and of nothing else (§3.6). Neither document could see that from its own side.

---

## 1. What is carried forward, and what is history

Everything the two documents found is re-checked against `f50775b` here in one table, so that a
reader of the archive can tell a finding that still describes the tree from one that a built slice
closed. **Status** is one of *stands*, *built*, *closed*, *voided*; **Now** is where this design
carries it.

| From | Finding or slice | Status on 2026-09-09 | Now |
|---|---|---|---|
| Rendering R-1 | No frame boundary; neither `Clear` is called by the program | **stands** — `grep -rn "Clear()"` finds one caller, in `VideoStateTests.cpp` | §3.4: the boundary is the present |
| Rendering R-2 | Docked screens and the dashboard art are drawn once per view change | **stands** | §3.4: the backdrop surface |
| Rendering R-3, R-8 | The erase feeds the RNG — narrowed by R-8 to heap metadata (`_heap.Read(_ship.heap) >= 4u`) | **stands** | §3.4: the canvas keeps its erases; §4 D1 prices removing them |
| Rendering R-4 | The picture is not in the state hash; T1 measured by `TheReplayIsTheSameWithNoTwins` | **closed 2026-09-09** | the licence for §3.4 |
| Rendering R-5 | T3 ("every erase has a twin erase") has no cheap test | **closed 2026-09-09**: RN-1 deleted the rule and RN-2 recorded it. What replaced it does have a cheap test — `check_twins.py` rule 4, failing both ways | §5 RN-2, §10 |
| Rendering R-6 | The picture's evidence is byte-shaped; a non-bit-plane picture rewrites five test files | **stands** | §4 D1's bill, §9 |
| Rendering R-7, R-9, R-10 | Most of the dashboard is stored not XORed; the heap is a spawn limiter; the sun is differential, the rings rewind | **stand** | §3.4 and D1: what a one-surface program keeps |
| Rendering RN-0 | The backdrop/frame split; the gate `ThePictureIsAsRecorded` | **part-built** — gate in, split not; 74 sites classified, 3 open | §5 RN-0 (finish); the 3 sites in §3.4 |
| Rendering RN-1, RN-2 | The frame boundary; ADR-008 amended | **built 2026-09-09** | §5, same names; §10 |
| Rendering ~~RN-3~~ to ~~RN-5~~ | Display list, per-primitive colour, dirty rectangles | **declined 2026-09-08**, kept | §9, still declined; D1 is what would reopen them |
| InputTimer I-1, I-3, I-4, I-6 | The queue pop; the pause screen unleavable; the joystick half gone; keys held across focus loss | **built** (I-1, I-0, I-3, I-0) | history |
| InputTimer I-2 | `TT102` dispatched from an edge where the original dispatches the held key | **stands**, half closed by I-1 | §3.3, §5 I-2 |
| InputTimer I-5, I-8 | Virtual keys, no scan codes, no layers; the chart rule inside `ScanKeyboard` | **stand** | §3.3, §5 I-2 |
| InputTimer I-7 | The blocking read is a nested pump; close is `ExitProcess` | **stands** — `Turn()` is reached at three call depths, sixteen library routines wait directly and twenty more reach one of them (§2.2) | §3.5, §5 I-4 |
| InputTimer I-9, T-8 | What is right and stays | **stand** | §6 |
| InputTimer T-1, T-2, T-6 | `DELAY` counts the monitor; three accumulators, one flag discarded; the docked syncs priced but not waited | **stand** — `Main.cpp` holds two accumulators and `Shell.h` two more, all `double` | §3.2, §5 T-1 |
| InputTimer T-3 | Waiting redraws two surfaces; an occluded chain spins | **stands** — `Present` resolves 256,000 indices and uploads them on every turn, changed or not | §3.7, §5 T-3 |
| InputTimer T-4 | NTSC in the constants, PAL in an old decision | **settled by ADR-001 itself**: the build variant is `_VARIANT=1`, "the GMA85 NTSC release" (ADR-001, Context). NTSC is the machine; PAL stays selectable | §4 D4, closed |
| InputTimer T-5 | Measure before the oracle goes | **built** (T-0), and the numbers are now constants nothing re-derives | history; `Presentation.h` |
| InputTimer T-7 | Audio pumped from the render loop | **stands** | §3.6, §5 T-4 |
| InputTimer I-6 (slice) | Characterisation tests for the input path | **built 2026-09-09** | the guard I-2 runs under |
| InputTimer D1 | PAL or NTSC | **settled**, as above | — |
| InputTimer D2 | Coroutines for the docked screens | **open**, default yes | §4 D2, restated with the alternative priced |
| InputTimer D3 | The pause screen | **ruled** removed, built | history |

---

## 2. The frame as it is

### 2.1 One picture of the loop

```
Main.cpp Run()                              GameShell::Turn()                       ScreenPresenter::Present
────────────────────────────────────        ─────────────────────────────────       ─────────────────────────────
while (shell.Turn())                   ──►  SyncVideoRegisters  (COMIRQ1 ×2)
  elapsed = steady_clock delta (double)     SoundOutput::Pump   (interrupt until the XAudio2 queue holds 4 frames)
  Docked : PlanSteps(elapsed, dockedLeftover, 1/DockedPassSeconds(syncs))  ≤4 × StepDocked(TakePressed())
  Flight : PlanSteps(elapsed, accumulated,   1/FlightFrameSeconds(ships))  ≤4 × Step(TakePressed())
                                            Window::Pump        (PeekMessage loop; a close is a flag)
                                            TakeResize → Resize
                                            minimised → WaitMessage
                                                                                ──► Resolve(picture, canvas, video) 256,000 bytes
                                                                                    upload, one triangle, Present(1, 0)  ◄── the only wait

Reached from INSIDE library routines, each one a Turn() per iteration:
  WaitFrames(n)          n × Turn()                       DELAY: n vertical syncs of the PLAYER'S monitor
  Present()              WaitFrames(1)                    the tunnels' free sync
  HoldTitleFrame(d)      Turn() until TitleTurnSeconds(d)  own steady_clock, own double leftover
  HoldFlightFrame(n)     Turn() until FlightFrameSeconds(n) own steady_clock, own double leftover
  ReadKey                WaitFrames(2), then Present() per scan until release-then-press
  window closed          Abandon(): Destroy() the presenter, ExitProcess(0)
```

Four facts, each measured on `f50775b`, and each is a finding one of the archived documents made
and the other depends on:

- **There is one place that waits and four that reach it.** `Turn()` is called from `Run`'s loop
  condition and from `WaitFrames`, `HoldFlightFrame` and `HoldTitleFrame` (`Shell.cpp`); `Present`
  is `WaitFrames(1)`. So the program presents at three call depths, and a resize, a close and a
  focus change all arrive inside whichever ported routine happens to be waiting.
- **Two surfaces are drawn on every frame and one is presented.** Every canvas draw has a twin at
  the same site (`check_twins.py`, 16 translation units); `ScreenPresenter::Present` resolves the
  picture *with* the canvas — for the raster state and the sprite pointers the picture does not
  hold (`Picture.h`) — into 256,000 indices, uploads them and draws, on every turn, whether or not a
  byte moved.
- **Four clocks.** `steady_clock` read in three places with three backlog rules (`Main.cpp`'s
  `accumulated` and `dockedLeftover`; `Shell.h`'s `m_spinLeftover` and `m_flightFrameLeftover`),
  all `double`, with `PlanSteps` clamping at four steps and the `stalled` flag read and discarded;
  and the SID advancing on the audio device's own consumption, at 59.83 Hz whatever the display
  does. `DELAY` counts none of them — it counts presents.
- **Nothing in the program has a frame boundary.** `Canvas::Clear` and `Picture::Clear` have no
  caller in `GameLogic/` or `Outpost/`; a thing leaves the screen by being drawn again.

### 2.2 The blocking reads, counted

A crude call-graph pass over `GameLogic/*.cpp` on `f50775b` (a script that finds every function
body containing a call to `present.WaitFrames`, `present.Present`, `present.Hold*`, `keyboard.NextKey`
or `ReadKey`, then their callers) gives the diff I-4 has to make: **sixteen routines wait directly**
(`ReadKey`; `ChooseView` and `EquipShipScreen`; `DrawHyperspaceTunnel`, `ShowTitleShip` and `Die`;
`ReadNumber`; `PrintCashLeft`, `Complain` and `ListCargo`; `ShowBriefingShip`, `WaitForKeyPress`
and `ShowIncomingMessage`; `ReportAndReturnToMenu`, `AskYesNo` and `DiskAccessMenu`), **ten reach
one of those** (`PauseForKey`, `RunControlCode`, `RunConstrictorBriefing`, `OfferTrumble`,
`EnterWitchspace`, `StartGame`, `BuyScreen`, `InventoryScreen`, `Game::Perform`, `Game::Leave`),
**eight reach those** (`BriefMission1`, `EnterWitchspaceCheating`, `RunTextCode`,
`ResetAndStartGame`, `Game::Reset`, `Game::PressKey`, `Game::MissionOf`, `Game::Step`) and **two
more** (`PrintByte`, `Game::StepDocked`). Thirty-six functions, and the top of the tree is `Game`
itself: the cold start waits on the title screen and a flight step can end in a death sequence, a
docking and a briefing. The slice recounts with the compiler; this is the estimate.

### 2.3 Where the time goes today

At 60 Hz, a flight with three fighters in the bubble (a step every 147 ms):

| From | To | Worst case | Why |
|---|---|---|---|
| key down | `WM_KEYDOWN` dispatched | one refresh, 17 ms | the pump runs once a turn, and a turn is a present |
| dispatched | read by a step | one step, 147 ms | `TakePressed` is read once per step (`Main.cpp`) |
| step | `Present` called | 0 | the loop returns to `Turn()` at once |
| `Present` called | on the wire | one to three refreshes, 17 to 50 ms | `Present(1, 0)` queues; the default maximum frame latency is three and nothing lowers it |

The middle row is the game and is §4 D3's; the other three are the executable's and add up to as
much as a step. Docked, the second row is `ReadKey`'s two-sync debounce plus one present per scan —
which is 33 ms on this panel and 14 ms on a 144 Hz one, wrong in both directions, and the thing T-1
makes the same everywhere.

---

## 3. The frame as it will be

### 3.1 One loop, one clock, one present

```cpp
namespace Outpost
{
  int Run(Platform& _platform, Elite::Game& _game)              // Modernize.md §4.8's two hundred lines, at last
  {
    FrameClock clock;                                           // the ONLY reader of steady_clock (§3.2)
    Scheduler scheduler{_platform.Timing()};                    // cycles in, {steps, blanks} out; the speed knob lives here

    while (!_platform.Closed())
    {
      _platform.WaitForFrame();                                 // the swap chain's latency object, or MsgWait when hidden or inactive
      _platform.Pump();                                         // messages; a close, a resize and a focus change are flags read here

      const Elite::InputFrame input = _platform.Sample();       // late latch: the keys as they are NOW, not as they were at the top
      const Budget budget = scheduler.Advance(clock.Tick(), _platform.Active(), _game.Cost());

      _game.Advance(input, budget);                             // whole steps and whole blanks; a task resumes until it suspends (§3.5)
      _platform.Audio().Publish(_game.TakeSoundFrames());       // one interrupt log per blank the budget delivered (§3.6)

      if (_game.FrameChanged())                                 // the frame surface's counter moved (§3.4)
      {
        _platform.Present(_game.Frame(), _game.State().video);  // resolve, upload, one triangle, Present(1, 0)
      }
    }
    return 0;
  }
}
```

Five properties, each of which one of the archived documents wanted and could not have alone:

1. **`Present` has one caller.** No library routine presents, waits or pumps; the ones that used to
   suspend instead (§3.5), and the loop is a function the suite can drive on the Linux leg.
2. **The clock is read once per turn and nowhere else**, in integer cycles at the machine's rate
   (§3.2). `double` leaves `Outpost/`, which was the one arithmetic ADR-007 §2 kept there for lack
   of an integer form.
3. **A frame is a frame**: what the loop presents is the frame surface as the game left it after
   whole steps, never a step's intermediate state — ADR-005 §1's clause, now structural.
4. **Input is sampled as late as possible and read as a value.** The `InputFrame` is what the game
   was given; the replay scripts the same struct; the platform cannot be asked a question mid-step.
5. **The window's close, resize and focus loss are flags read at one point**, so `Abandon` and
   `ExitProcess` go and every destructor runs.

`Platform` is the class Modernize.md §4.8 named: `GameShell`, `FlightSession` and the window's key
table folded into one object implementing `Elite::Presenter` (now the awaitable source, §3.5),
`Elite::Keyboard` (now `Held` and `HasJoystick` alone, §3.3) and `Elite::CommanderStore`. The
`effects-seams` ceiling (<!--count:effects-seams-->5 today) falls by however many the fold removes.

**Two clauses of the ADRs shape the sketch above and are honoured rather than argued with**
(§11). `Game::Advance` is a *budget consumer* and not a fourth `Step`: it calls `Step` and
`StepDocked` as they are, chosen by `Mode`, and resumes a task between them, so ADR-007 §2's
finding — `FRCE` chooses between routines and the caller's choice is the game's — survives inside
it. And `Run` is written over an *abstract* `Platform` in a file with no Win32 in it, so that it
joins the five `Outpost/` sources the portable runner already compiles (`EXECUTABLE_SOURCES` in
`generate_runner.py`) and the `GameLoopTests` case of §3.9 can exist; the concrete window, chain
and device implement that interface in files the Windows leg alone compiles. ADR-004 §1 says
presentation is "none of it unit-tested", and this is the amendment: what has no Win32 in it is
tested, and the runner has compiled such files since slice 2e.

### 3.2 Time: `MachineTiming`, `FrameClock`, `Scheduler`

```cpp
struct MachineTiming { std::uint32_t clockHz; std::uint32_t cyclesPerFrame; };   // NTSC 1,022,727 / 65×263 by ADR-001; PAL 985,248 / 63×312 selectable
class FrameClock { public: [[nodiscard]] std::int64_t Tick() noexcept; };         // integer 6510 cycles since the last Tick; the one steady_clock

struct Budget { int flightSteps; int dockedPasses; int verticalBlanks; bool stalled; bool paused; };

class Scheduler
{
public:
  [[nodiscard]] Budget Advance(std::int64_t _elapsedCycles, bool _active, Cost _cost) noexcept;
  // No speed knob (§4 D3, declined): the step cost is the one number the fixed-rate track replaces
};
```

Rules, and each replaces one of §2.1's four clocks or one of InputTimer.md's three backlog rules:

- **Two counters from one supply of cycles.** The *step budget* is charged per flight step at
  `FLIGHT_FRAME_COSTS(ships)` and per docked pass at `DOCKED_PASS_CYCLES` plus the syncs the pass
  asked for (the constants `Presentation.h` already holds, integer, unchanged); the *vertical blank*
  is one every `cyclesPerFrame`. `WaitFrames(n)` and `Present()` consume blanks; the two holds
  consume cycles; `CHART_CURSOR_SYNCS` is honoured at last.
- **A stall costs a frame, never a burst.** The clamp is by *time* and not by *count*: a backlog
  longer than one period after the turn's steps are taken is dropped and `stalled` is set. Today's
  four-step clamp lets a lid-close pay back four steps of 290 ms in one turn — a 1.2-second lurch —
  and the two hold loops already use the time rule (`Shell.cpp`, "a stall should cost the sequence
  a frame"). One rule, in one place, and the stall is *counted*: an `OutputDebugString` line and a
  counter in the title bar of a debug build, which is what ADR-005 §3 asked for and `(void)plan.stalled`
  is not.
- **Auto-pause.** `_active` false — the window inactive or minimised — plans nothing and *drops*
  the elapsed cycles rather than banking them; on activation the game resumes on a whole step where
  it was. This is what a windowed player means by pause (InputTimer.md §5.9 item 2), it needs no
  key, and it closes the "Alt+Tab for a minute and come back to a teleport" case.
- **No speed knob, by ruling (§4 D3).** The step cost is the cost model's and nothing scales it.
  What the ruling gives instead is the hook: the fixed-rate flight model that follows RN-6 charges
  one blank per step where this charges `FLIGHT_FRAME_COSTS(ships)`, and the scheduler does not
  otherwise change.
- **The sound interrupt runs once per blank the budget delivers**, on this thread, into a log the
  audio thread renders (§3.6). It is the fourth clock of §2.1 folded into the first.

`ShellTests::TheStepPlannerNeverSkipsOrDoublesSilently` moves over whole; a new case asserts that
fifty blanks are five sixths of a second at NTSC and one second at PAL however many presents
happened, that an inactive turn plans nothing and banks nothing, and that a backlog of ten periods
yields one step and a stall.

### 3.3 Input: `InputFrame`, layers, scan codes

```cpp
namespace Elite
{
  struct InputFrame
  {
    std::array<std::uint8_t, 65> held{};   ///< level, over the C64 matrix positions; what `RDKEY` reads
    std::uint8_t pressed = 0;              ///< `thiskey` for `TT102`: the port's edge today, the lowest held key once I-2 measures it
  };
}
```

- **The platform produces one frame per turn and the game takes it as a value.** `Held` stays on
  the port for `ScanKeyboard`'s walk — it reads the same array — and `NextKey` and `Flush` leave it:
  `ReadKey` is the routine, and `FLKB` on this build is `LDA #15 / TAX / RTS`, a flush of nothing
  with three call sites that go with the method. The fixtures that scripted *characters* through
  `NextKey` script *frames* instead, which is the shape InputTimer.md §5.2 wanted and I-1 deferred.
- **Signature first, meaning second, separate commits.** Adapt the replay's driver to `InputFrame`
  with the same keys and prove all three digests unmoved; only then let anything change what a key
  means. This is the rule InputTimer.md's revalidation added, because the replay is both I-2's guard
  and its patient.
- **Edge against level is measured, not ruled.** The original's `TT102` dispatches the lowest key
  *held* on that pass; the port dispatches the newest genuine *press*. I-6's characterisation tests
  and `DockedSessionTests` say whether level dispatch changes any screen a player can reach; the
  expectation is that it does not, because every docked screen blocks in `ReadKey` before the next
  pass and every flight action a held key repeats is idempotent. If it does, the port's edge stays
  and the row in ADR-005 §4 says so.
- **A layered map on scan codes.** `KeyBinding` gains a scan code (lParam bits 16 to 23 with the
  extended bit folded in; no Raw Input, which buys nothing for a game that does not tell two
  keyboards apart) and a layer set `{Flight, Docked, TextEntry}`. The flight set binds by scan code
  so the arrows and `,`/`.` are the physical keys on every layout; letters and digits bind by virtual
  key as now; the **Docked** layer drops the steering positions, which retires the chart rule from
  `Elite::ScanKeyboard` into data (I-8) and stops a typed `S` reaching `control.pitch` on a docked
  screen (I-5). The layer is chosen by the executable from `Game::ModeNow()` and from whether a
  `ReadKey` task is the active consumer, which `Game` exposes as one boolean. **ADR-005 §4 calls
  exactly this "a second table chosen by the current view, which is a phase-6 remapping question"**,
  so I-2's second commit is a phase-6-class change by the corpus's own words and lands with the
  §4 amendment, not ahead of it; and every layer keeps §4's standing rule that a key the game's own
  text names is never left bound to nothing — the TextEntry layer names every position that types.
- **Late latch, and a tap is never lost.** `Sample()` runs after the pump and immediately before
  `Advance`, so a step reads the keys as they are at the moment it runs rather than as they were when
  the turn began — and it *accumulates*: a key that went down at any point since the last sample is
  reported held for that one step. At 147 to 290 ms between flight steps a pure sample loses a tap of
  fire, a view key or hyperspace; the C64 scanned once a frame and lost it too, so this is the one
  reactivity gain in flight that changes what no step computes. The replay's scripted keys span
  steps, so no record moves; the rule is Quake's `kb.msec` without the fraction, and it is what the
  owner's question of 2026-09-09 about event-driven input turned out to be asking for (§12).
- **A gamepad is a column, later.** XInput (in the pre-approved SDK; Xbox-shaped pads only, which
  is the honest limit) fills `held` for `KY3` to `KY7`, the views and fire, and answers
  `HasJoystick`, at which point `TITLE`'s fire test selects the stick exactly as `dojoystick` did
  and `JSTGY`/`JSTE` come back as its axis reversals. `GameInput` is the modern API and is a NuGet
  package, which AGENTS.md §5 says to put to the owner rather than assume. Own ADR when it comes.

`ShellTests` keeps its table walk and gains one assertion per layer: every position the library
names for that mode has a binding in it — the test that would have caught I-3.

### 3.4 The picture: a backdrop, a frame, and the boundary at the present

Option A stands as ruled (Rendering.md §9): the canvas and the game are not touched; the picture
becomes a rendered frame. What this design adds is *where* the boundary is.

- **Two pictures, as RN-0 planned.** A **backdrop**, written once per view change by the docked
  screen routines, the wipes and the dashboard copy, and never cleared per frame; a **frame**,
  refreshed from the backdrop at every boundary, drawn on by everything per-frame, and presented.
  The refresh is a `memcpy` of the bitmap plane and the cell palettes — 36,000 bytes — plus the
  dashboard's index plane only when it moved; the 36 KB `Universe` grows by is ruled (Rendering.md
  §9.3). **The backdrop is a second `Picture` field on `Universe` and takes `picture`'s treatment
  exactly**: skipped by `HashState` with the reason beside the fold, no cells in `StateCells`, and
  `TheReplayIsTheSameWithNoTwins` still green — ADR-007 §6's rule that a byte in `Universe` gets a
  cell has one named exception and this joins it (ADR-008 §4).
- **The boundary is the present, wherever it is asked for.** After I-4 every present is a
  `co_await` (§3.5), so a tunnel that draws thirty-four rings with a present after each is
  thirty-four frames without any special case, and Rendering.md §12.3's three unclassified sites
  fall out: `SeedStardustField` and its two callers write the **frame** (`MoveStardust` re-emits
  every particle each pass); `DrawHyperspaceRings` presents inside its loop, so each ring is its own
  frame and the rewind of `LSP` that erased the last ring on the canvas is a drop on the picture;
  `ClearAllShips` at a view change is a drop, because the view change's wipe rewrites the backdrop
  before anything presents. **Each of the three is a default the gate decides, not a ruling**:
  `ThePictureIsAsRecorded` holds sixteen checkpoints of the scripted flight, the launch tunnel among
  them, and a default that changes a checkpoint is wrong and is moved to the other surface. **None
  of the three records makes a hyperspace jump**, so RN-0 records one more checkpoint through
  `DrawHyperspaceRings` before the rings' default is trusted.
- **Erase twins become drops** at RN-1. **BUILT 2026-09-09, and the list above was both too long and
  too short.** Too long: `COMPAS`'s first dot, `me1`'s re-print and `TT103`'s first crosshair write
  the BACKDROP after RN-0, which is never cleared, so their erases still work and still need their
  twins. Too short: it named routines, and the property is what matters — anything that erases by
  redrawing its own geometry. Seven went: `EraseShip`, `EraseSun`, `EraseBall`, the stardust's first
  plot, the laser beam's second draw, the explosion cloud's second draw, and **`LL9` part 9**, which
  is one line inside `OpenHeapRun` and appears in no list of erases anywhere. `check_twins.py` gains
  its fourth table, `ERASE_NEEDS_NO_TWIN`, with a reason per entry, and a rule 4 that fails both
  ways. **T3 is then deleted rather than tested** (RN-2, done).
- **The dashboard's index plane is not cleared**; it is stored-not-XORed already (R-7) and the bars,
  blips and compass re-emit onto it. A dirty flag per region is the cheap half of Rendering.md's
  declined RN-5 and is all that is needed.
- **The frame surface carries a generation counter**, bumped by every write that lands on it, and
  `FrameChanged()` in §3.1 is that counter against the last presented value. Present-on-change
  therefore costs nothing to build once the split exists, and it is what stops a hold loop uploading
  two surfaces thirteen times a flight frame on a 165 Hz panel (T-3's finding).

**And then one surface, if D1 is ruled.** With the frame boundary in and every picture-side erase a
drop, the picture no longer needs the exclusive-or that ADR-008 §1 made it VIC-II-shaped for. The
canvas still does — and the canvas is the only reason the twelve twins exist as *twins* rather than
as the routines' own stores. Retiring it is RN-6 in §5 and is priced there: the decisions stay
where they are (the clipper, the heap, the flags, the carry — R-8 and R-9 say those are game state
and none of them is a canvas byte), the ~20 exclusive-or sites' canvas stores go, `Picture::Resolve`
stops needing a canvas beside it once the raster state and the sprite pointers it reads from there
move to the `ScreenState` and `VideoState` they belong to, and the twin rule, `check_twins.py`,
the `/// 2x of:` markers and ADR-008 §2 go with it. The bill is evidence, and §4 D1 states it.

### 3.5 The docked screens, the title and the tunnels: coroutines

`Elite::Task<void>` and four awaitables — `WaitFrames(n)`, `Present()`, `HoldTitleFrame(d)`,
`HoldFlightFrame(n)` — plus `ReadKey` as a task of its own. A routine keeps its shape: the body is
unchanged and the wait gains `co_await`. `Game::Advance` resumes the active task until it suspends
on something the budget cannot yet satisfy; between tasks it runs whole flight steps as planned.
Thirty-six functions change return type (§2.2), which is mechanical and wide and the one large
diff in this track — and it lands *after* M6-d, as InputTimer.md sequenced it, because a coroutine
diff over freshly rewritten comments reviews more easily than the reverse.

Three things the archived plan did not settle, settled here:

- **The death sequence becomes a task, and that amends a recorded reason.** `Die` holds sixty-five
  flight frames through `HoldFlightFrame` and is reached from `Game::Leave`; Modernize.md M4-d and
  ADR-007 §2 kept it *synchronous* "because making it a state would change the pacing". A task is
  not a mode-machine state and the awaitable charges the same cycles the hold charged, so the
  pacing is unmoved and `TheDeathIsAsRecorded` is the proof; the clause is amended at I-4 to say
  what it now means. One case that looked like a trap is not: ADR-001 §6's failed-load bug, where
  the original re-enters the disk menu with a frame still on the stack, is ported as a single
  `loadFramePending` flag rather than as recursion (`SaveGame.cpp`), so the coroutine chain has no
  unbounded case and the arena can be sized from §2.2 alone.

- **No heap in the frame path.** A coroutine frame is heap-allocated unless the promise says
  otherwise; the promise takes its storage from a fixed arena on `Game` sized for the deepest chain
  §2.2 found (a briefing inside a docking inside a step), and a chain deeper than that is an
  assertion and not an allocation. `noexcept` and the determinism guard both need this, and
  `check_gamelogic.py` needs no new rule: `<coroutine>` brings no clock, no float and no I/O —
  and it bans the *identifiers* `clock` and `time` in `GameLogic/`, so the awaitables and the arena
  are named for what they hold (`blanks`, `cycles`) and never for either word.
- **Cancellation is destruction.** Closing the window sets a flag the loop reads; the loop returns,
  `Game` is destroyed, the active task's frame is destroyed with it and every destructor runs. There
  is no value `ReadKey` has to return to a caller that cannot be told the game is over, which is the
  whole of `Abandon`'s reason for existing.

**The alternative, priced now rather than dismissed.** A game thread — the option `Window.h`
rejected in September — changes no library signature at all: the platform's `WaitFrames` blocks on
a condition variable the main thread's loop signals per blank, and the nested pump goes without a
`co_await` anywhere. Two of its three objections are gone since then: RN-0's frame surface *is* the
snapshot the presenter needed handed to it under a lock, and the parked-thread-on-close case is a
flag the wait returns on plus a process exit with the thread parked, which is what today does
anyway. What it does not give is the third property of §3.1: `Run()` as a function the Linux leg
drives through a docked session, a launch and a close, because the waits are then Win32. That test
is the reason to prefer the coroutine, and §4 D2 is where the owner weighs a large mechanical diff
against a test the tree has never had.

### 3.6 Sound: one clock, and the chip on its own thread

- **The interrupt runs under the scheduler.** `RunSoundInterrupt` is called once per vertical blank
  the budget delivers, on the game thread, producing one `SidWriteLog` per blank; today it runs "as
  many times as the XAudio2 queue is short" (`SoundOutput.h`), which makes the sound buffer's
  counters — game state, folded in every replay digest — the one part of `Universe` that advances
  on a device's clock rather than on the game's. After this the program's state is a function of its
  input frames and its elapsed cycles and nothing else, which is the property a replay of the *app*
  needs and the corpus has not had. **This reverses a decision, not merely adds to one**: ADR-005 §2
  as amended at slice 5a chose the device's queue depth as the interrupt's clock "because the device
  consumes samples at exactly the rate they are rendered for". That reasoning was about the *chip*
  and still holds for it — the audio thread renders at the device's rate — and was silent about the
  *state* the interrupt mutates. **Ruled 2026-09-09 (§12 R1)**: reversed, and ADR-005 §2 carries it.
- **The chip renders on the XAudio2 callback thread** from a ring of logs the game thread publishes
  (one atomic index, no lock). An empty ring — a title-bar drag, a breakpoint — renders the chip
  forward with no new writes, which is what a 6581 does when the CPU is busy: it holds its
  registers. The stutter T-7 names goes; the pump never starves because there is no pump.
- **What this costs the replay.** The scripted flights run through the null port and never run the
  interrupt; delivering blanks to `Game` would run it and move every digest. So the interrupt's
  delivery is a count the driver can hold at zero, the three records are proved unmoved with it at
  zero, and the record is *then* re-taken with it delivered — Modernize.md rule 1's first case, the
  digest deliberately widened, with the zero-delivery run as the second column that proves the
  flight did not move. One journal entry, and `TheReplayIsTheSameWithNoTwins`'s shape a third time.

### 3.7 The presenter: waitable, on change, idle when hidden

`ScreenPresenter` keeps its texture, its eighteen root constants and its one triangle, and changes
in four places, all in `Outpost/`:

- `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` at creation, `SetMaximumFrameLatency(1)`,
  and `WaitForFrame()` in §3.1 is `WaitForSingleObjectEx` on the object. The loop then runs right
  after a vertical blank, samples, steps and presents into the next one: §2.3's last row becomes one
  refresh, and the measurement is PresentMon's `MsBetweenPresents` and `MsUntilDisplayed` on a
  scripted key, taken before and after and written into T-3's journal entry.
- The RESOLVE is done only when the picture's inputs moved, and the present is done every turn.
  **This sentence said the opposite until T-3 built it** — "`Present` is called only when
  `FrameChanged()`; a turn that changed nothing waits on the object and does no CPU or GPU work" —
  and it cannot work: the latency object is a semaphore released when a PRESENTED frame retires, so
  a turn that skipped the present would leave nothing to retire and the next wait would run to its
  cap. The present is also the vertical sync every hold in `Shell.cpp` counts turns against. What
  actually costs is the 256,000-pixel resolve and the 250 KB upload behind it, and those are what a
  turn that changed nothing now skips; the texture is a separate resource from the back buffer, so
  it survives the flip. `Picture::ResolveSignature` is the question "did anything `Resolve` reads
  move", answered beside the reads and held there by `PictureTests`.
- `DXGI_STATUS_OCCLUDED` puts the loop on `MsgWaitForMultipleObjects` with a 100 ms cap, so a hidden
  window costs ten wake-ups a second and not a core; minimised stays `WaitMessage` as today.
- Vsync stays on and tearing stays off: at three to twenty-one steps a second a variable-refresh
  panel has nothing to offer, and the game is paced by the scheduler and never by the display.

The manifest is already `PerMonitorV2`, so nothing here is scaled by the compositor. Borderless
fullscreen is a `SetWindowPos` and a resize with a flip chain and is phase 6 (§9).

### 3.8 Reactivity, priced

The same table as §2.3, after the track:

| From | To | Worst case | What changed |
|---|---|---|---|
| key down | sampled | one refresh | the pump and the late latch run every turn, right after the blank |
| sampled | read by a step | one step | **unchanged unless D3 is on** |
| step | on the wire | one refresh | present-after-step into a queue of one |

Docked: `ReadKey`'s debounce is two *simulated* blanks — 33 ms at NTSC on every panel — and a scan
per turn. Stalls: none of the three §0 named survives. What a player would notice is that nothing
they see has changed and everything they press lands a frame or two sooner; what a player in a
fight would notice is nothing, until D3.

### 3.9 What pins it

Every slice runs `python tools/check_all.py` and the suite, and lands through the Windows leg
(R15). The instruments, in the order a slice meets them:

| Instrument | What it holds | Slices it gates |
|---|---|---|
| `ThePictureIsAsRecorded` (RN-0's gate) | sixteen whole-frame digests of the scripted flight | every rendering slice: the picture is pixel-identical or the slice is wrong |
| the three replay records | the game, step for step | I-2's signature half; RN-0 to RN-2 by construction (the picture is not folded); T-4's interrupt with the second column |
| `TheReplayIsTheSameWithNoTwins` | that the picture consumes nothing the game notices | RN-1, which drops every erase twin |
| `ControlsTests` (I-6) | what the four input routines answer today | I-2's meaning half |
| `DockedSessionTests` | every screen reachable and distinct | I-2, I-4 |
| `ShellTests` | the planner, the map, the settings, the viewport | T-1, T-3, I-2 |
| a docked screen fixture that can see the picture | **does not exist** (Rendering.md §12.2) — `Session` prints through a transcript sink | RN-0 builds it, in `PictureTextTests`' shape |
| a `GameLoopTests` case over `Run()` | **does not exist** | I-4 builds it: a docked session, a launch, a close, on the Linux leg |
| PresentMon | input-to-photon, in milliseconds | T-3 |
| a person | the cadence, the keys, the screens | every slice; ADR-009 §2's last row |

---

## 4. Decisions for the owner — RULED 2026-09-09

Four were asked and all four were ruled the day the document opened, three as recommended and one
differently (§12 has the exchange). The rows keep the question and the recommendation as they were
put, with the ruling against each, because the alternative that was rejected is part of the record.

| # | Question | Recommendation as put, and the RULING |
|---|---|---|
| **D1** | **One surface: retire the C64 canvas once the picture has a frame boundary (RN-6)?** The canvas is drawn every frame and never seen; it exists because the twins compute *where* and the faithful routines store *what* onto it, and because the replay digests fold it. Retiring it removes the twin rule, `check_twins.py`, the `/// 2x of:` markers, ~20 canvas exclusive-or stores, `DashboardImage.cpp`'s C64 art and most of ADR-008 §2; it leaves every decision where it is (R-8, R-9). **The bill**: four ADRs write the canvas in independently and each is amended — ADR-001 §1 ("the verification view"), ADR-002 §4 (the four planes) and §5, ADR-003 §3 (the canvas in the hash), ADR-007 §4 — plus ADR-008 whole; the three replay records move, because the fold *narrows* — ADR-009 §3 and ADR-007 §4 allow a re-take in two cases and this is a third, so it needs its own ruling and the two-column proof (§5 RN-6); the 53 picture tests lose the canvas as their reference and become picture goldens; the mutant floor loses `Canvas.cpp`, which it was written never to; the raster bytes and sprite pointers `Resolve` reads off the canvas move to `ScreenState`/`VideoState`. About eight sittings | **Yes, last in the track, on the two-column gate.** It is the only step that makes the rendering path one thing, and it is what reopens per-primitive colour and a display list (Rendering.md's declined RN-3/RN-4) at a price they are worth. The corpus's own warning applies and is the reason for *last*: verification you delete is not verification you get back (Rendering.md §5.2), so the picture's whole-frame record must be the instrument that replaces the canvas before the canvas goes. **RULED: yes, as recommended.** ADR-001 §1, ADR-002 §4, ADR-003 §3, ADR-007 §4 and ADR-008 carry it |
| **D2** | **Coroutines (I-4) or a game thread for the blocking reads?** Both remove the nested pump, `Abandon` and `ExitProcess`. Coroutines change thirty-six signatures and give a `Run()` the suite drives on Linux; a thread changes none and does not | **Coroutines**, as InputTimer.md defaulted, after M6-d. The test is worth the diff. **RULED: coroutines, after M6-d.** ADR-007 §2 and Modernize.md's M4-d row carry the death sequence's change |
| **D3** | **Is the step rate exposed as a setting?** `speed = 100` in `Settings.txt`, the machine's own pace, and anything else is ADR-001 §4's option. It is the *only* lever on §0's second row, and it changes the feel by construction | **Build the knob in T-1 and expose it, default 100.** One integer, and the alternative is a reactivity ceiling of three frames a second in a full fight that a player of the port will attribute to the port. **RULED DIFFERENTLY: no knob — change the game logic.** The owner asked how modern shooters take input; the answer (events collected, a snapshot polled at a fixed tick, the picture interpolated between ticks) made the knob the wrong lever, because it only changes how often the *original's* step happens. The ruling is a **fixed-rate flight model** — one flight step per simulated vertical blank, the per-step constants rescaled and widened where a per-blank increment is fractional — **as its own track and ADR, after RN-6, with the faithful cadence kept selectable** (ADR-001 §4's option rule). This track keeps the cost model as the only pace and gives the model its hook (§3.2); the event accumulation of §3.3 is the reactivity this track does deliver in flight. ADR-001 §4 and ADR-005 §3 carry it |
| **D4** | **PAL or NTSC as the machine?** | **Settled by ADR-001's own context line: `_VARIANT=1` is the GMA85 NTSC release.** NTSC is the machine; `MachineTiming` carries PAL and `Settings.txt` may select it. Recorded here so it is not asked a third time. **Confirmed**, not re-asked |

Not a decision: **look and feel.** Every slice's gate is the picture the tree draws today, frame for
frame, and the cost model the tree paces by today, step for step; the only thing that may change
either is the fixed-rate model D3 rules, which is a later track, an option, and off by default.

---

## 5. The track

**The plan that executes this table is [Platform-Build.md](Platform-Build.md)** (2026-09-09): the
slices below as steps an agent can take, in one fixed order, with the files, the tests, the gate
commands and the protocol every slice follows. Where it departs from a section here it says so and
the slice's journal entry amends the section.

One track, because the slices depend on each other across the two old ones. Names are kept where
the slice is the archived one so its history resolves; new slices get new names. **Gate** is what
turns the slice green beyond `check_all.py` and the suite; sittings are the corpus's unit.

| Slice | What | Gate | Ratchets and checks | Sittings |
|---|---|---|---|---|
| **T-1** ✅ **built 2026-09-09 (§10)** | §3.2 in `Presentation.*`; `PlanSteps` and both hold loops replaced; `WaitFrames` counts simulated blanks; the time clamp; auto-pause; the stall counter; `SoundOutput` takes the same `MachineTiming`. No speed knob (D3) | `ShellTests` moved and extended (§3.2's three new cases); play: `dn2`'s beep pause is five sixths of a second on a 60 Hz and a 144 Hz panel; Alt+Tab away a minute, back on the same step | `main-lines` (<!--count:main-lines-->258) falls; ADR-005 §3 | 2 |
| **T-3** ✅ **built 2026-09-09 (§10)** | §3.7: the waitable object, latency one, **resolve**-on-change over `Picture::ResolveSignature` (not present-on-change — see §3.7 and §10), occlusion idle | The picture's own goldens unchanged; the signature's own test; **owner's, outstanding**: PresentMon before and after on a scripted key, and CPU at rest with the window hidden | ADR-008 one paragraph | 1–2 |
| **RN-0** ✅ **built 2026-09-09 (§10)** | The backdrop and frame surfaces; the 71 classified sites moved; the three defaults of §3.4; the docked-screen picture fixture of §3.9 | `ThePictureIsAsRecorded` unmoved; the new docked fixture green; `TheReplayIsTheSameWithNoTwins` | none; the 36 KB is ruled | 2 |
| **RN-1** ✅ **built 2026-09-09 (§10)** | the boundary in `GameLogic/Frame.h`, nineteen sites through it; the clear on and LAZY, and the frame **cleared**, not refreshed from the backdrop (that would ghost); **seven** erase twins become drops, the seventh being `LL9` part 9's, which no list had; the tunnel's rings move to the backdrop; `check_twins.py`'s fourth table and its rule 4 | Both picture gates unmoved: a correct erase and a correct clear produce the same frame, which is the slice's whole claim | `check_twins.py` | 2 |
| **RN-2** ✅ **built 2026-09-09 (§10)** | §1 says two surfaces and 215,364 bytes; §4 names the backdrop; the Status table carries the split and the boundary. **T3 is deleted**, in §2 and in the Status table, with what replaced it named in both — `check_twins.py`'s rule 4, which unlike T3 has a check that fails both ways | `check_docs.py`; the ADR's Status table | — | 0.5 |
| **I-2** `InputFrame` and the layered map | §3.3: the struct, the two `Step`s over it, `Window` producing one per turn from scan codes with events accumulated into it, the layers, `NextKey` and `Flush` off the port. **Two commits**: the signature with the same keys, then the meaning | All three digests unmoved after the first commit; I-6's tests and `DockedSessionTests` after the second, with the level-against-edge answer journaled; `ShellTests` per-layer completeness | `outpost-elite-names` (<!--count:outpost-elite-names-->62) may fall; `effects-seams` unchanged | 2 |
| **I-4** Coroutines and `Platform` | §3.5 and §3.1: `Task`, the awaitables, the thirty-six routines converted in groups, `Run()` a function over `Game` and `Platform`, `GameShell` and `FlightSession` absorbed, `Abandon` deleted | Every existing comparison green without change to what it asserts; the `GameLoopTests` case of §3.9 on the Linux leg; all digests unmoved | `effects-seams` 5 → 4 or 3; `main-lines` falls | 4–5 |
| **T-4** Sound | §3.6: the interrupt under the scheduler, the log ring, the chip on the callback thread | `SidRenderTests` unchanged; the zero-delivery replay unmoved and the record re-taken with delivery, journaled as rule 1's first case; no stutter while dragging the title bar | ADR-005 §2 one paragraph | 1–2 |
| **C-1** Cleanup | §9's inventory: `GameShell::ClearToView` (no caller since M5-e; "public because `Main.cpp` changes screens through it" and it does not), `ScreenPresenter::Ready` (no caller), `Keyboard::Flush` and its three call sites (with I-2), the four `double` accumulators (with T-1), `FlightSession` (with I-4). **Not the `2x` files** (§0) | `check_outpost.py`; the Windows build | `outpost-elite-names` | 0.5, spread over the slices named |
| **RN-6** One surface (D1, ruled) | The canvas stores go; `Resolve` reads raster state from `ScreenState` and `VideoState`; the twins become the routines' own stores and lose their suffix; `check_twins.py`, T1 to T4 and `DashboardImage.cpp` go; the picture tests become goldens | **The two-column proof**: `HashState` with the canvas excluded is taken on every checkpoint before the slice and must be identical after it; only then are the records re-taken under a new rule case named in ADR-010. `ThePictureIsAsRecorded` unmoved throughout | `mutant-files` falls (journaled); `check_twins.py` leaves `check_all.py`; ADR-008 amended or superseded | 6–8 |
| **I-5** Gamepad and remapping | §3.3's last bullet, a remap file, its own ADR | Its ADR's | — | 3, phase 6 |
| **P-0** ADR-010 | "The platform, as built": the loop, the clocks, the frame, the input, the sound, the surface, in the shape of ADR-007 and ADR-008, with the numbers this track measured; it supersedes ADR-005 (ruled, A4) | `check_docs.py`, `check_counts.py`; README row | — | 1 |
| **FR** The fixed-rate flight model (D3, ruled) | **Its own design and ADR, after RN-6 and P-0; not this track's.** One flight step per simulated blank; every per-step constant rescaled (the control bumps, the eighth-frame recharge, the one-in-256 spawn roll, the countdowns, the ship and dust motion) and widened where a per-blank increment is fractional, under ADR-002 §3's rule; the faithful cadence selectable, the faithful records untouched, a second record for the model. The presentation half — a display list and interpolation between steps at the display's rate — is what ADR-002 §1's role-scoped float ban (A1) exists for | Its own; ADR-001 §4's rule: green with the option off | — | later |

### Sequencing

```
T-1 ──┬──────────────────────────────────────► T-4          (T-4 needs the blank the scheduler delivers)
      │
T-3 ──┤   (both executable-only; free of every library slice; the safest work on the board)
      │
RN-0 ─┼─► RN-1 ─► RN-2 ──────────────────────────────────┐
      │                                                    ├─► RN-6 (D1) ─► P-0 ─► FR (D3, own track)
I-2 ──┴─► I-4 (after M6-d finishes) ─────────────────────┘
                 └─► I-5 (own ADR)                C-1 rides inside I-2, I-4 and T-1
```

T-1 and T-3 first, because they are the two a player on a high-refresh panel meets and neither
touches `GameLogic/`. RN-0 and I-2 can run in either order and beside each other; I-4 wants both,
because the boundary it puts behind `co_await` is RN-1's and the frame it resumes on is I-2's. RN-6
is last and is the one slice whose gate is a proof rather than an equality. About twenty-five
sittings to P-0 without RN-6 and thirty-two with it; FR is its own track and is not counted.

---

## 6. What the track obeys, and what it does not touch

Modernize.md §5's rules as they stand after M6 — the replay record moves in exactly two cases and
the journal names which (rule 1, with ADR-009 §3's caveat that there is no arbiter now but the
code); no width changes; a mutant re-anchored never dropped; the ratchet only down; every
signature change reaches `Outpost/` in the same commit; one pattern per slice; original bugs stay
ported; new files in both project files; the journal written as the slice lands. ADR-008's T1 and
T2 hold through RN-2 and until RN-6 retires the rule with the surface. ADR-001 §4's option rule is
what D3 is under.

What no slice touches: the universe, the RNG's consumption, the AI, the canvas bytes and the heap
(until RN-6, and then only the *stores*), the cost model's numbers, the sixteen VIC-II colours, the
integer scale and the letterbox, the 8:5 aspect. What is kept from InputTimer.md I-9 and T-8:
`ScanKeyboard` walking down; `TT102` taking the position; `HINT` and `CTRL` read live; the arrows'
two-position `CursorKeys`; Space at position 4; F7 as launch; the accumulator outside the library
and taking whole steps; the cost model as a measurement with its limits written beside it; `Present`
blocking rather than a timer sleeping.

---

## 7. What the track does to the documents

**Every ruling below that needed no slice was written into its ADR on 2026-09-09** (§12): ADR-001
§1 and §4, ADR-002 §1 and §4, ADR-003 §3, ADR-004 §1, ADR-005 §1 to §4, ADR-006 §6, ADR-007 §2, §4
and §6, ADR-008 §4 and its Status table, ADR-009 §2 and §3, and Modernize.md's M4-d row. What is left
for the slices is the as-built wording: the numbers T-3 measures, the case I-2's second commit
journals, and ADR-010 at P-0.

- **ADR-005 §1** gains the waitable chain and present-on-change (T-3) — one paragraph, since
  ADR-008 owns the layer and this is the executable's half of it. **§3** is rewritten at T-1: the
  simulated blank, the integer scheduler, auto-pause, the time clamp, `MachineTiming` with NTSC
  ruled by ADR-001, and the speed option under ADR-001 §4. **§4** gains the `InputFrame`, the layers
  and the scan-code rule at I-2, and loses "the port's `InputFrame` carries both level and edge bits"
  as a future tense. **§2** gains the interrupt-under-scheduler paragraph at T-4.
- **ADR-008** loses T3 at RN-2 and corrects §1's byte count; at RN-6, if ruled, §1, §2 and §4 are
  superseded by ADR-010 and the ADR's Status table says so.
- **ADR-005 §2 is REVERSED at T-4**, not extended: the interrupt is clocked by the scheduler's blank
  and the device's rate paces the chip alone. That is a ruling, and the slice carries it.
- **ADR-007 §2** ("the count of passes stays outside because it is floating point") gains a sentence
  at T-1: it stays outside because the clock is the executable's, and it is integer now. At I-4 its
  "the death sequence stays synchronous" (and Modernize.md's M4-d row) is amended to "a task whose
  awaitable charges what the hold charged", with `TheDeathIsAsRecorded` as the evidence.
- **ADR-004 §1** ("presentation ... none of it unit-tested") is amended at I-4: what has no Win32 in
  it is tested on both legs, which the portable runner has done for five of its files since 2e.
- **Modernize.md §4.4 and §4.8** gain a ✅ and a pointer here at I-2 and I-4, and §4.8's
  `ScreenPresenter` sentence gains the `Platform` fold. **Plan §5** already points here.
- **`Design/README.md`**: the reading-order row this commit adds; a decisions row for ADR-010 at
  P-0. **`AGENTS.md` §6**: `check_twins.py` leaves the list at RN-6.
- **The risk register** gains nothing until a slice fires one; §8 is where the track's own risks
  live, as Resolution.md §12 and Rendering.md §8 kept theirs.

---

## 8. Risks

| # | Risk | Mitigation |
|---|---|---|
| RN-a | A region nobody noticed was persistent blanks after RN-1 and no test looks at it | Both picture gates, and the docked fixture RN-0 builds so the screens the replay never visits are seen too. What neither sees is a screen no fixture reaches, which is the hand-check |
| RN-c | A per-frame twin writes the backdrop, which is a smear that survives a clear | The backdrop is `const` to every per-frame path; only the view-change path holds a mutable reference |
| RN-d | `Universe` grows by 36 KB | Ruled; stated |
| P-a | **The coroutine arena is sized wrong** and a chain the estimate missed asserts in play | §2.2's depth measured with the compiler at I-4, the arena sized with headroom, and the assertion is a diagnostic naming the chain, never a silent allocation |
| P-b | **Level dispatch changes a screen the fixtures do not reach** | I-2's second commit runs I-6, `DockedSessionTests` and the hand-check on every docked screen with a key held; the port's edge is kept if any moves |
| P-c | **The interrupt under the scheduler moves the records for a reason that is not the widening** | The zero-delivery column: the same run with no blanks delivered must reproduce the old records to the bit before the new ones are taken |
| P-d | **RN-6 retires the canvas and a decision was hiding in a store** — a routine that read a canvas byte back to decide something | `grep` on `f50775b` finds one canvas read outside `Resolve` and the tests, `ViewChange.cpp:77`, an exclusive-or pattern's read-modify-write with no decision on it; the two-column proof is the instrument if the grep is wrong, and it fails before any record is re-taken |
| P-e | **The fixed-rate model's record is mistaken for the faithful one** | There is no knob in this track (D3); when FR comes, its record is a second table beside the three, never a re-take of them, and ADR-001 §4's rule is that the faithful three stay green with the model off |
| P-f | **Two threads, one `Universe`** — the audio thread reads game state | It never does: it reads a ring of logs the game thread wrote after its step, and the ring is the only shared object |

Rendering.md's RN-b (the colour change's rewrite) and RN-e (closed) do not carry: the first is
RN-6's and D1's, the second is history.

---

## 9. Improvements suggested, in and out of the track

From experience of this shape of program, with what each costs and where it sits. **In the track**
already: the time-clamped backlog (§3.2), the late latch (§3.3), the sound interrupt under the
scheduler (§3.6), the two-column proof as the gate for a narrowing (§5 RN-6), PresentMon as the
number T-3 is accepted on (§3.7), and cancellation-by-destruction (§3.5).

**Worth doing, small, not scheduled**:

- **Borderless fullscreen on F11 and Alt+Enter** — a `SetWindowPos` to the monitor's rectangle and
  a resize; the flip chain and the integer scale need nothing else, and the DXGI transition the
  presenter refuses today stays refused. Phase 6, one sitting, and the window's last placement and
  size go in `Settings.txt` beside it.
- **`ID3D12InfoQueue` set to break on error in debug builds**, so a validation message stops the
  debugger where it happens rather than scrolling past; three lines beside the debug layer's enable.
- **A debug overlay in the title bar** — steps per second, blanks per second, stalls — which is the
  stall counter T-1 builds, shown.
- **Decode the picture on the GPU.** The picture is VIC-II-shaped for the erase, and a bit plane
  with a cell palette is 36 KB where the resolved indices are 256 KB; a pixel shader that reads the
  plane and the palette is twenty lines and the upload shrinks sevenfold. Only worth it if T-3's
  present-on-change leaves an upload cost anyone can measure, and moot after RN-6 turns the plane
  into whatever a one-surface picture wants to be.
- **`WM_ENTERSIZEMOVE`**: with T-4 the audio no longer stutters while the title bar is dragged and
  the picture freezing is what every game does; a `WM_TIMER` that turns the loop inside the modal
  size loop is the fix if the freeze is ever wanted gone, and it is not worth its complexity now.

**Cleanup, with the evidence** (C-1 in §5), measured on `f50775b`:

| What | Evidence | Goes with |
|---|---|---|
| `GameShell::ClearToView` | no caller outside `Shell.*`; its comment says `Main.cpp` uses it and `Main.cpp` does not | C-1, now |
| `ScreenPresenter::Ready` | no caller | C-1, now |
| `Keyboard::Flush` and three `FLKB` call sites | `FLKB` is three instructions that flush nothing on this build (I-1's reading); the executable answers with nothing; only fixtures count it | I-2, when the port shrinks |
| `Keyboard::NextKey` | the routine is `Elite::ReadKey`; the method is the fixtures' character source until `InputFrame` exists | I-2 |
| four `double` accumulators over three `steady_clock` reads | §2.1 | T-1 |
| `FlightSession` | three methods over a pointer the composition root could hold; Modernize.md §4.8 already folds it | I-4 |
| `DashboardImage.cpp`, the canvas stores, the `2x` suffix, `check_twins.py` | **only under D1** | RN-6 |
| the `2x` files as duplicates | **not duplicates** — §0 | never |

**Declined, and why**: per-primitive colour and a display list stay declined until RN-6 makes them
a change to one surface instead of a rewrite of five test files (Rendering.md's RN-3 and RN-4, and
R-6 is still the bill); Raw Input, because the scan code is in `WM_KEYDOWN` already; a fixed
higher step rate as the *default*, because it is the feel and ADR-001 §4 says an option is off; a
second thread for the game, for §3.5's reason; `GameInput` over XInput, until the owner is asked
about the package; and **a speed knob**, put and declined on 2026-09-09 in favour of the fixed-rate
track (D3), because scaling how often the original's step happens is not the modernisation the owner
meant by "change the game logic".

---

## 10. Journal

**2026-09-09 — opened.** Written from the owner's request of the same day: replace the designs
covering rendering, input and time with one, move what they held that is obsolete to
`Design/Archive/`, change nothing a player would recognise, make the game more reactive, ask where
unclear, and remove the duplicate code — "the `2x` files". Both documents were read whole and
re-checked against `f50775b` (§1); the executable and the library seams were read and counted
(§2). Three things came out of reading the two as one that neither says alone, and they are §0's
third answer: the present is the frame boundary, the vertical blank is the sound interrupt's clock,
and RN-0's unclassified sites are the gate's to decide. The `2x` question turned out to be the
largest design decision in the layer and not a cleanup, and it is D1 with its price. Nothing is
built; the track's first two slices touch no library file and are where building starts.

**2026-09-09, later — every ADR read against the design (§11).** Asked whether the ADRs limit the
track in a way it had not taken into account. Twenty-three clauses set against §3 to §5: sixteen
held, six were corrected in the sections they touch, and one — ADR-005 §2's clock for the sound
interrupt — is a reversal the design had described as an addition and now names as a ruling. The
pass also removed a worry: the failed-load stack bug is ported as a flag, so I-4's arena has no
unbounded case.

**2026-09-09 — T-1 built, in three commits, and the clamp is not the one the plan drew.**
`Outpost::Scheduler` and `Outpost::FrameClock` are the four `double` accumulators and the three
`steady_clock` reads; `DELAY` counts the machine's simulated blanks on every panel; an inactive
window plans nothing and banks nothing; a dropped backlog is counted and said. `SoundOutput` takes
the machine instead of naming NTSC in two more constants, and `Settings.txt` has a `machine` key.
Eleven tests, 140 → 147, all three CI jobs green.

**The clamp took a shape §3.2 did not specify, and the shape matters.** "By time and not by count"
is right and is not enough on its own: a budget of four frames of the machine, applied plainly,
would refuse a flight frame of a full bubble outright — 293,354 cycles against a budget of 68,380 —
and the game would stop the moment a fight started. So the first affordable step is unconditional
and the budget bounds only the CATCH-UP after it. That second clause is what keeps the one docked
screen that runs at 228 passes a second (Data on System with the author names, whose pass waits for
nothing) running at 228 rather than at the display's rate, and it is why the plan's named test
`ThreePeriodsAreThreeSteps` is not in the tree: three periods is `PlanSteps`' answer, and the object
that replaced it deliberately gives a different one.

**Two things moved to where they belonged, and both improved the result.** The stall report was to
go in `Main.cpp`; it is in `GameShell::Turn`, because that is where the clock is read — a stall
inside a docked `DELAY` or a title hold happens at a call depth the outer loop does not see for
another turn, and a diagnostic a turn late is about the wrong frame. And `ApplySettingsFile` split
into `ReadSettingsFile` plus the `ApplySettings` that already existed, because `machine` decides how
the clock, the scheduler and the sound are CONSTRUCTED and the universe they are built around does
not exist yet: read, build, then apply.

**`main-lines` 238 → 257, raised once and then held.** The rise is the clock, the scheduler and the
mode reset arriving in the composition root, which is platform composition; P6 counts the executable
holding game state and dispatch, and none of that came back. The tool refused the raise until it was
asked for explicitly, which is rule 5 working. What is worth recording is the second attempt: the
third commit pushed the count to 271 and the honest reading was not "raise it again" but that the
reasoning had been written twice — once in `Main.cpp` and once in the header that owns the concept —
so the composition root's prose went back to pointers and the count returned to 257 exactly.
`outpost-elite-names` 63 → 62 with `GameShell::ClearToView`, which said it was public because
`Main.cpp` changed screens through it and had had no caller since M3-c.

**And a defect in the harness, found by the slice and fixed in it.** `tools/mutate.py` built HEAD
plus `git diff HEAD`, which covers TRACKED files: a slice that adds a header and includes it from a
modified file put the include in the worktree and not the header, so every unit failed to build —
while the banner above, which reads `git status --porcelain`, had just listed the new file as
carried. The baseline guard caught it, which is what the baseline guard is for; what it could not do
was say why, and a tool that names a file it did not copy is Risk R13's shape rather than a
nuisance. It copies the untracked files beside the diff now.

**Not done here, and it is the owner's**: the play checks T-1 is accepted on — `dn2`'s beep the same
length on a 60 Hz and a 144 Hz panel, and Alt+Tab away for a minute and back on the same step.

**2026-09-09, later still — eight rulings, and the ADRs amended (§12).** All four of §4 and four on
the ADRs were put to the owner and ruled the same day: three as recommended, D3 differently. The
owner's counter-question on event-driven input produced the one change to what this track builds —
event accumulation into the `InputFrame` (§3.3) — and the ruling that the flight step's rate is a
game-logic change and not a setting, which is a track of its own after RN-6 with the faithful cadence
selectable. Nine ADRs carry their amendments as of this entry; the speed knob is gone from §3.2, §5
and §8; §0's second row says what the track does and does not do about the sampling rate.

**2026-09-09 — T-3, first commit: the wait moved from the bottom of the turn to the top.** The swap
chain is created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, the queue is set to one
frame instead of the driver's default three, and `GameShell::Turn` opens by waiting on the chain's
latency object. Nothing about the picture changed; what changed is its age. Presenting into a
three-deep queue and then sampling the keyboard meant a key could be read up to three refreshes
before the pixels it moved reached the glass — 50 ms on a 60 Hz panel, against a game whose own
step is 47. Waiting first and sampling immediately after puts the sample and the present inside one
refresh.

**`ResizeBuffers` did not need a change, and the plan asked whether it would.** `Resize` already
reads `GetDesc1` and hands `description.Flags` straight back, so the flag round-trips without a
line — which is luck rather than design, and the comment beside the flag now says so, because a
resize that dropped it would leave a valid handle that is never signalled. That is a hang, not a
wrong picture, and it would not show up until somebody dragged a window edge.

**`Present` answers three things now, and the third was being lost.** `DXGI_STATUS_OCCLUDED` is a
SUCCESS code, so while the return was a `bool` a window hidden behind another one — or on a virtual
desktop nobody is looking at — presented nothing, said "carry on", and went round again at whatever
rate the message pump managed, burning a core on a picture no one could see. `Occluded` now idles in
`WaitWhileOccluded`, woken by the latency object, by any input, or by a hundred milliseconds,
whichever is first.

**One deviation from the build plan, and it is a boundary rather than a behaviour.** The plan had
the shell perform the occluded wait over a handle the presenter exposed. The wait is one call; an
accessor handing out a raw `HANDLE` to one caller is a worse seam than a method that names what the
wait is for, and Win32 waits belong beside the Direct3D (ADR-004 §1). So the handle stayed private
and the accessor went the way of `Ready`, which C-1 deleted in the same commit for having no caller
at all.

**What this commit is NOT verified by, and it should be said plainly.** Neither `ScreenPresenter`
nor `Shell` is in `EXECUTABLE_SOURCES`, so the Ubuntu leg compiles none of it: 147 green here and
all 13 repository checks green say nothing about this change. The Windows job is the only witness,
and the two numbers that decide whether the slice is worth its complexity — Task Manager at rest
with the window covered, and PresentMon's `MsUntilDisplayed` on a scripted key before and after —
are still the owner's to take.

**2026-09-09 — T-3, second commit: the resolve is skipped, the present is not, and §3.7 was wrong
about which.** The design said a turn that changed nothing does not PRESENT and waits on the
latency object instead. It cannot: that object is a semaphore released when a presented frame
RETIRES, so a turn that skipped the present leaves nothing to retire and the next wait runs to its
one-second cap — a stall dressed as a saving. The present is also the vertical sync every hold in
`Shell.cpp` counts turns against, so removing it would take the pacing with it. §3.7's second
bullet is rewritten to say what the tree does: present every turn, and skip the 256,000-pixel
resolve and the 250 KB upload behind it, which is where all the cost was. What is left on an
unchanged turn is a clear, one triangle over a texture that is already resident, and the present.

**The signature moved out of the executable, and the ratchet is what moved it.** It was written
first as a free function in `ScreenPresenter.cpp`, enumerating what `Picture::Resolve` and
`CompositeSprites` read. `outpost-elite-names` went 62 → 66 and refused, which was the correct
answer to a wrong design rather than an obstacle: a list of what two `GameLogic` functions read
belongs beside those functions, not a project away. `Picture::ResolveSignature` is that list, and
moving it bought the thing the executable could never have had — a test. `PictureTests` now
asserts, for each of thirteen reads, that changing it moves the pixels AND moves the signature;
deleting any one fold from the function fails it. The count came back to 62.

**And the first version of that test proved nothing, which is worth recording.** It ran against a
default picture — every cell palette black on black, the colour RAM zero, the dashboard not shown —
where flipping bitmap bits changed no pixel a person could see, so every case passed vacuously. The
test now asserts that each change moved the pixels BEFORE asserting the signature noticed, and it
took two attempts to make each of the thirteen cases actually move one. A test that cannot fail is
worse than no test, because it is counted.

**One thing kept that the test does not justify.** `Present` resolves anyway every 64 turns
regardless of the signature. The signature is a hand-kept list, and a read added tomorrow to a
mutation the test does not make would produce a screen that stops updating — silent, total, and on
a path neither CI leg can see. Sixty-four turns is a third of a second to a second depending on the
panel, so the net turns that failure into visible lag rather than a freeze, at 1.5 % of the resolve
it is insuring. It is insurance and not a substitute, and the comment beside it says so.

**A duplicate address removed on the way past.** `Canvas::SCREEN_CELLS + 0x3F8u` was written out in
`CompositeSprites` and in `SIGHT_SPRITE_CELL`, and the signature wanted it a third time. It is
`Canvas::SPRITE_POINTERS` now, once, with the paragraph about why reading the first block reads
both moved onto it.

**147 → 149 tests**, all 13 checks, `outpost-elite-names` and `main-lines` at their ceilings. The
Windows leg is still the only witness to the presenter half; commit 1's run was green.

**2026-09-09 — RN-0, first commit: the backdrop exists and changes nothing, which is the point.**
`Universe::backdrop` is declared beside `picture`, under the same exclusion — `HashState` walks
past both, `StateCells` names neither, and `TheStateHashDeliberatelyDoesNotSeeIt` now writes to
both and requires the digest not to move. `Picture::Resolve`, `Hash` and `ResolveSignature` take
the backdrop; the two surfaces composite by exclusive-or on all three planes. **149 passed, 0
failed, all 13 checks** with no site moved, so every digest the tree holds — the three replays,
`RECORDED_PICTURE`, every picture golden — is byte for byte what it was. That is what the commit
had to prove and it is all it claims.

**The plan's own step would not have compiled to that.** Platform-Build.md §2.3 had the bitmap
composited but the cell palettes and the dashboard plane read "from the backdrop", which at this
commit is blank: a black screen and a hundred moved digests, not a no-op. Composite all three the
same way and it is identity while either side is empty, and still correct once the sites move,
because a cell written only to the backdrop reads `backdrop ^ 0`. The step now says so.

**What it costs, said before the next commit meets it.** A cell palette written to BOTH surfaces
exclusive-ors into nonsense rather than one winning. The site table forbids that and
`ThePictureIsAsRecorded` would catch it, but commit 2's journal is the place to record whether any
of the 74 sites turns out to do it.

**The byte count for RN-2.** A second `Picture` is another 107,682 bytes, so a `Universe` goes from
123 KB to 231 KB — not the 36 KB §3.4 assumed. `Universe.h` carries the number and RN-2 is where
ADR-008 §1 gets it.

**2026-09-09 — RN-0, the fixtures, recorded BEFORE the sites move.** The plan had these built after
the sites moved and the order is inverted, for the reason the entry above gives: recorded first,
they say what the split PRESERVED; recorded after, they only say what it produced. Two tables,
149 → 151 tests, every existing digest unmoved.

**The docked session drew nothing at all, and that is why it had no picture gate.** It printed
through a terminal `TranscriptSink` — characters into a string, full stop — so `CHPR` never ran, the
cursor only moved where a routine moved it deliberately, and the canvas and the 640×400 surface were
blank on every screen. `DockedSessionTests`' own header has said so since slice 2e and called it the
one thing a null presenter cannot check. The sink passes the character on to a real
`Elite::TextPrinter` now, wired as `PictureTextTests`' `Docked` fixture is and as `Game` wires the
real game, with the picture attached. **All six existing docked tests passed unchanged with `CHPR`
running**, which was not a foregone conclusion and is why it was tried before a second fixture was
invented: those assertions are about which screen was reached and whether the screens differ from
each other, and neither moved.

`TheDockedScreensAreAsRecorded` then digests seven screens — status, market, buy, sell, inventory,
equip, data on system — off the same script `TheScreensDoNotPrintTheSameThingAsEachOther` uses, so
the two cannot drift apart unnoticed.

**`TheHyperspaceRingsAreAsRecorded` covers the other place with no record**: 35 presents through
`DrawHyperspaceTunnel`, of which the first is `HYPNOISE`'s own vertical sync before any ring is
drawn. It matters because `DrawHyperspaceRings` is classified as writing the FRAME on the grounds
that each ring is its own present, and nothing else checks that classification.

**Both tables carry a clause that stops them passing vacuously, and both were mutation-checked.**
Deleting the picture attachment from the docked printer fails on "status and market resolved to the
same picture"; passing `nullptr` where `DrawTunnel` hands the tunnel its surface fails on "every
ring of the tunnel resolved to the same picture". A digest table matches anything once it has been
re-recorded from a broken tree, so the distinctness clause is the half that has to be there before
the numbers mean anything at all.

**What is gated now that was not.** 27 of RN-0's 81 sites are on docked screens and 3 are the
tunnel's; until today none of those 30 had a whole-frame test of any kind. The remaining commits
move sites against these two tables and `ThePictureIsAsRecorded`.

**2026-09-09 — RN-0, the sites moved: 54 to the backdrop, 28 left on the frame.** All three picture
tables hold unmoved, 151 tests, 13 checks. The gate ruled on all three of the defaults §3.4 left
open and confirmed each: `SeedStardustField`, `SeedStardustAndClearShips` and `FlipStardust` stay on
the frame, `DrawHyperspaceRings` stays, `ClearAllShips` stays. **Five corrections to the site table,
four of them found by the gate rather than by argument.**

**1. A wipe is not a draw site, and the table does not classify it.** With one surface, blanking the
screen blanked everything on it. Split, `SetUpScreenPixels` can only blank the surface it is handed,
and the transients are on the other — so the scripted flight's last checkpoint, the docked screen
the arrival leaves, came back with stardust on it. The wipe takes both surfaces now and clears the
frame itself, in the routine rather than at its two call sites, because a third call site would
forget. That is RN-1's mechanism arriving early and doing exactly what RN-1 will ask of it.

**2. Sites that write the same CELL must move as a unit.** Moving the glyphs alone made all seven
docked screens resolve to the same picture: the glyph's cell palette went to the backdrop, the
wipe's stayed on the frame, and the composite exclusive-ors the two palette bytes into black on
black. That is precisely the hazard recorded in the plan before this commit started, arriving on the
first move that could produce it. Glyphs, wipes and message rows moved together afterwards.

**3. Blips and the compass stay on the FRAME**, against the table's dashboard row. `ClearAllShips`
erases every blip through the pointer the table sends to the frame, so a blip drawn on the backdrop
would be erased on the frame — invisible today, because exclusive-or does not care which surface,
and a stale blip stranded on the backdrop for ever once RN-1 clears the frame each pass. R-7's
argument for the dashboard is that it is "written absolutely rather than exclusive-ored", which is
true of the dials and the indicators and is not true of a blip. Draw and erase share a surface.

**4. The chart's sun discs go to the backdrop; the flight's sun stays on the frame.** The table
names routines, and `DrawSun` has two call sites with opposite lifetimes: the short-range chart
draws discs it "never intends to move", and the flight sun is erased and redrawn every pass.

**5. The twin switch is ONE switch over two surfaces.** `DrawingTwins(_picture)` asks whether the
twin that writes THIS surface should run, which is the right question and reads whichever surface
the site was handed. With two surfaces, switching one off leaves every twin that writes the other
still drawing and `TheReplayIsTheSameWithNoTwins` measuring half of what it claims — while passing,
because every digest would be exactly where it was. So the two flags are never written separately:
`Universe::SetDrawingTwins` is the only writer and moves both by construction, and the plane
assertion is made on both surfaces. The build plan asked for one flag reached through the universe
from all forty-nine guard sites; this is the same guarantee for two lines instead of forty-nine, and
it leaves the guard reading the surface in front of it rather than reaching back out to ask. Checked
by mutation: a switch that reaches only the frame fails on "a twin drew on the backdrop with the
twins switched off".

**What the green does and does not prove, said plainly.** The composite is `backdrop ^ frame`, so
moving an EXCLUSIVE-OR write between surfaces is provably invisible: exclusive-or is associative and
both halves still land. The gate therefore says nothing about the transient sites — which is most of
them. What it does check is every ASSIGNMENT that used to wipe out another site's writes, because an
assignment resets only its own surface's accumulator. That is where all four corrections above came
from, and it is the whole of RN-a's failure mode. The classification of the pure exclusive-or sites
rests on the argument, not on the tables, until RN-1 clears the frame and makes it visible.

**2026-09-09 — RN-1, first commit: the boundary exists and clears nothing.** `GameLogic/Frame.h`
wraps the four ways the game waits and ends the frame afterwards; nineteen call sites go through it;
`Game::EndFrame` gives the two loops outside the library the same boundary — `Main.cpp` after the
turn that presented, and the replay driver after each checkpoint. `CLEAR_THE_FRAME` is false, so
this is `if constexpr` around nothing and **151 tests, 13 checks and every digest prove the plumbing
alone**. The clear arrives in the next commit together with the six erase twins it replaces, because
those two are only equal to each other and neither is a no-op by itself.

**Nineteen sites, not the sixteen the plan counted**, and the difference is exactly the three with
no `Universe&` in scope: `ReadLine`'s settle pause, `DrawHyperspaceRing`, and `PrintCashLeft`'s beep
— which does have one and was missed. So the wrappers take the PRESENTER AND THE FRAME rather than a
universe. That is not only for the three: after RN-0 a site's `Picture*` may be either surface, and
a wrapper that assumed would clear the backdrop on every docked screen. The ring passes its own
frame, and `ReadLine` passes null and says why, which is a docked prompt with nothing of it on the
frame.

**The checkpoint hashes before the boundary, not after.** The order is the whole of what the replay
records: a checkpoint is what a person would have seen, and the clear follows it so the next pass
draws onto an empty frame exactly as the executable's loop does. Reversed, it would record a blank
frame every time and pass for ever.

**`main-lines` 257 → 258, raised by one, and the alternative was considered and rejected.** The one
line is the boundary in the composition root. It cannot move into `GameShell::Turn`, which looks
like its natural home: `Turn` is also the present inside a `DELAY`, so clearing there would blank a
held picture for the other forty-nine frames of fifty. That is also why the boundary is at the four
WAITS and not at every present. T-1's raise was repaired by finding the reasoning written twice;
this one has no second copy to delete.


**2026-09-09 — RN-2, the half of it that is true.** ADR-008 §1 said "One surface beside the canvas"
and costed it at 107,682 bytes taking a `Universe` to 123 KB. There are two since RN-0: 215,364
bytes and 231 KB, and the heading, the prose and the number now say so, with the exclusive-or
composite and why it was the only one that could be a no-op on the day the second surface arrived.
§4 names the backdrop as built rather than as coming, and says the exclusion is asserted by a test
rather than trusted. The Status table gains a row for the split.

**T3 IS NOT DELETED, and the step said to delete it.** "Every erase has a twin erase" is still in
force: RN-1's first commit put the boundary in place with the clear switched OFF, and a rule that
goes when the clear goes on cannot go while it is off. `check_twins.py` still holds every erase to
its twin and would be right to. The ADR says this in both places it mentions T3 — the rule in §2 and
the row in the Status table — rather than recording a deletion that has not happened. RN-2 is half
done and its row says so; the other half rides with RN-1's second commit, which is where it belongs,
because the rule and the clear are only equal to each other.

**2026-09-09 — RN-1, second commit: the clear is on, and the drops are SEVEN.** Every picture gate
is unmoved — `ThePictureIsAsRecorded`'s sixteen checkpoints, `RECORDED_DOCKED_PICTURES`,
`RECORDED_RINGS` — with 152 tests and 13 checks green. That equality is the slice's whole claim: a
correct erase and a correct clear produce the same frame.

**The seventh erase is the finding, and it is not called `Erase` anything.** `LL9` part 9
(`OpenHeapRun`, `ShipDraw.cpp`) opens a ship's heap run by drawing LAST pass's ship a second time to
rub it out. It is one line inside a routine whose name says nothing about erasing, it is absent from
Archive/Rendering.md §4.4's list of nine, and it is absent from this document's own drops list. It
was still running on the frame after the other six went, and it caused BOTH of the faults that were
being hunted as two: where the ship had not moved between passes the ghost cancelled the new draw
and the checkpoint held too LITTLE; where it had, the ghost stood beside it and the checkpoint held
too MUCH. Six checkpoints were out by −67, +70, −30, +357, +677 and +188; dropping this one line
took all six to zero.

**The instrument that named it, after four diagnoses reached by inference were all wrong.** Every
write that LANDED on the frame was recorded with the offset, the mask, and a tag naming the drawing
primitive it came from, for one pass either side of each moved checkpoint; then the offsets whose
writes exclusive-ored back to nothing were grouped by the pair of primitives that had cancelled
them. At checkpoint 400 that read "74 offsets cancelled, every one of them a ship line against
another ship line" — which is not a fact anybody would have guessed and is one nobody has to. **The
rule this slice ends with is that the class of routine RN-1 handles is a PROPERTY, not a list:
anything that erases by redrawing its own geometry cancels itself on a cleared frame, whatever it is
called.**

**The hyperspace tunnel's rings are the BACKDROP's, and RN-0 had them on the frame.** They
accumulate: each circle is presented and the next is drawn on top of what is already there until
`LOOK1` wipes the screen, which is `DrawHyperspaceRing`'s own comment ("they erase themselves
because `LOOK1` below clears the screen anyway"). Anything that survives a present is the backdrop's
by RN-0's own rule. `RECORDED_RINGS` was recorded to catch exactly this and did, at the first
present that follows another ring — the second one. So `DrawHyperspaceRing` takes TWO surfaces now:
the one the circles are drawn on and the one the present ends. They differ in exactly one place in
the game and this is it, and hand both the same surface and the tunnel wipes itself — which is the
mutation that proves the record is load-bearing. With the rings on the backdrop the digests are bit
for bit what they were.

**A first attempt at this passed every test and was still wrong.** Leaving the rings on the frame
and having the ring's present end NOTHING gives the same pixels — there is no boundary during the
tunnel either way — so no test could tell them apart. It contradicted RN-1's own principle that a
present is the end of a frame, and it left persistent content on the surface defined as what a pass
re-emits, which is a trap for I-4's flight loop rather than a bug today. Two parameters, and the
mutation that fails, is the version that says what is true.

**The clear is LAZY and that is not an optimisation.** `Picture::EndFrame` marks; the next write
that lands clears first. An eager clear is identical on the glass and wrong to everything that looks
BETWEEN two passes — the presenter, the replay's checkpoint, every recorded picture digest — because
all of them would read a blank surface instead of the frame that was just finished. Planting the
eager version fails three tests, one of them `ThePictureIsAsRecorded` at checkpoint 1.

**The boundary is per PASS, and the replay driver had it per checkpoint.** `Main.cpp` ends the frame
every turn; the replay ended it once in a hundred steps, so a hundred passes piled up. That was the
single largest correction of the sitting and it is one line in the fixture.

**Four routines lost their `Picture*` and one lost its side effect.** `EraseSun`, `EraseBall`,
`ErasePlanetOrSun` and `EraseSunRow` do not take a picture any more: a parameter that is
deliberately ignored is a worse lie than its absence, and the contract is now in the signature
rather than in a comment. `ClipSunRow` was made PURE — it zeroed the heap entry it was asked about,
so a twin could not ask without consuming game state — and `DrawSunFromState2x` draws the whole sun
from the widths `SUN` leaves behind, because a differential renderer cannot survive a clear.

**T3's replacement, and it is a replacement rather than a deletion.** `check_twins.py` gains a
fourth rule and an `ERASE_NEEDS_NO_TWIN` table with a reason per entry, and the rule is the OPPOSITE
of rule 2: a routine named there must still draw on the canvas and must still call no twin at all.
Put a twin back and the check fails; delete the canvas drawing and it fails too, so no entry can go
stale in silence. Three of the seven drops sit in routines that have nothing else to draw and are in
the table; the other four are inside routines that legitimately call a twin for something else and
the table says which and why.

**Two tests were rewritten to the new contract and one was added.**
`ErasingThePlanetClearsBothSurfaces` asserted the old rule and became
`ErasingThePlanetLeavesThePictureToTheFrameBoundary`; `TheSunLandsOnBothSurfacesAsItDrifts` now ends
the frame between its three, which is what makes a whole-sun twin comparable with a differential
canvas. `ClearBlanksEveryPlaneAndTheFrameBoundaryIsLazy` is new — `Clear` had never had a test
because nothing called it — and it opens the new frame with each plane's own mutator in turn. That
last part was a measurement, not a preference: written as one case with a pixel as the first write,
the bitmap's clear blanked the other two planes as well and the case passed with `BeginFrameIfStale`
deleted from `SetCell` and from `SetDot`.

**`CLEAR_THE_FRAME` is gone.** A `constexpr bool` that is now always true is scaffolding, and
scaffolding left up is read as structure.

**2026-09-09 — RN-2, the other half: T3 is deleted.** ADR-008 §2 records the deletion rather than
the rule, and says what replaced it: an erase the frame boundary replaces has NO twin, held by
`check_twins.py`'s rule 4 and its `ERASE_NEEDS_NO_TWIN` table. **The replacement is a better rule
than the one it replaces for a reason worth keeping**: T3 was the one rule in the ADR with no cheap
test, which is Rendering.md's risk R-5 — a missing erase looks like a smear three frames later —
and rule 4 fails in both directions, so it cannot go quietly wrong. R-5 closes with it.

The Status table gains the boundary as a built row beside T3's deletion, and §5's three rendering
rows are ✅. The count of dropped twins is SEVEN everywhere it appears, not six, and the seventh is
named, because the whole lesson of the slice is that a list of routines called "erase" was never
going to be the right instrument.

---

## 11. Every ADR read against this design, 2026-09-09

Asked, after the design was written: do the ADRs limit it in a way it has not taken into account?
All nine were read in full and every clause that touches rendering, input, time, ownership or the
record was set against §3 to §5. **Verdict** is one of: *held* (the design already obeys or amends
it, and says where), *corrected today* (the design was silent or wrong and the section named now
says so), *needs a ruling* (a clause the design reverses, which only the owner can). Nothing found
changes a slice's order; three things changed a slice's content.

| ADR | Clause | What the design does | Verdict |
|---|---|---|---|
| 001 §1 | The 320×200 canvas is "the verification view" | Untouched through RN-2; retired only under D1, and D1's bill now names this clause | held |
| 001 §3, §4 | An option is off by default; the suites are green with it off; the gate names the oracle | D3 is such an option; P-e keeps the replay at 100. The gate's wording is stale (ADR-009 §2 is the gate now) and is §7's to amend | held |
| 001 §6 | Original bugs stay ported | The failed-load stack bug is a flag, not recursion, so I-4's arena has no unbounded case (§3.5) | corrected today |
| 001 "what faithful does not mean" | "the port runs a fixed rate measured from the original" | It runs a cycle-budgeted variable rate and has since §6.114; the scheduler keeps that. A stale sentence, not a limit | held |
| 002 §1 | No `float`/`double` in `GameLogic/` | Nothing in the track puts one there: cycles are integers, the picture is integer. The rule *would* block a display list with sub-pixel geometry, which is why RN-4 stays declined and why the previous review recommended scoping the ban by role | held |
| 002 §4 | The canvas is four VIC-II planes; "sub-pixel accuracy and anti-aliasing are not built, and are not phase-6 items either" | Not touched before RN-6; the sentence forbids a visual modernisation this track does not attempt, and D1 is what would reopen it | held |
| 002 §5 | Erase-by-XOR is available; the heaps are ported because `LL9` and `SUN` decide by them | The heaps stay under every slice including RN-6 (R-9); only picture-side erases become drops | held |
| 003 §3 | The replay hash folds the canvas; the null-presenter ruling | Folded until RN-6; a null `Presenter` under coroutines is awaitables that resume at once, which is trivial — the ruling's own test of the seam passes | held |
| 004 §1 | "Presentation lives in the executable ... none of it unit-tested" | `Run` over an abstract `Platform` in a portable file, joining `EXECUTABLE_SOURCES`; §3.1 and §7 say so | corrected today |
| 004 §2 | File names unique repo-wide, against the CRT, STL and SDK, case-insensitively | `Task.h`, `Scheduler.h`, `FrameClock.h`, `Platform.h` are checked against the SDK before they are created; none is known to collide, and the rule is the slice's checklist | held |
| 005 §1 | Present once per step; a routine the original held for many frames owes a present per frame; integer scale; vsync on | The boundary at every present is that clause made structural (§3.4); the scale, letterbox and vsync are untouched (§3.7) | held |
| 005 §2 | The interrupt is clocked off the device's queue depth | **T-4 reverses this** for the state half and keeps it for the chip; §3.6 and §7 say so now | ruled (R1, §12) |
| 005 §3 | Fixed timestep, steps never skipped or doubled silently, a stall logs; auto-pause "to be built as T-1" | The time clamp drops a backlog and *counts* it, which is the clause's "never silently"; auto-pause is T-1's | held |
| 005 §4 | The modern layout; a key the text names is never unbound; "a second table chosen by the view is a phase-6 remapping question"; `InputFrame` "carries both level and edge bits" | I-2's layers *are* that second table, so the slice is phase-6-class and lands with the §4 amendment; the text-names rule binds every layer; `pressed` is one key rather than 65 edge bits, and the sentence is amended | corrected today |
| 006 §1, §8 | Edges point down; nothing below `Ports` includes a Windows header; C++20 | `Task` and the awaitables are `GameLogic`'s and include nothing of Windows; coroutines are C++20 | held |
| 006 §2, §3 | No width changes; value in, value out | `InputFrame` is 65 bytes and a byte; `Budget` is a value; nothing widens | held |
| 007 §1, §6 | `Universe` is a plain aggregate with no virtual; a byte of state gets a cell unless named | The arena and the tasks live on `Game`; the backdrop joins `picture`'s named exception (§3.4) | corrected today |
| 007 §2 | Three `Step`s, not one; the count of passes stays outside; the death sequence stays synchronous | `Advance` consumes a budget and calls the three (§3.1); the count stays outside for the clock's reason; `Die` becomes a task and the clause is amended at I-4 with `TheDeathIsAsRecorded` as evidence (§3.5, §7) | corrected today |
| 007 §4, 009 §3 | A record moves in exactly two cases | T-4 is the first case with a proof column; RN-6 is a third case and D1 asks for it | held |
| 008 §1, §2 | VIC-II-shaped for the erase; T1 to T4 | The shape stays through RN-2 with its reason lapsed, which RN-2 records; **T3 went at RN-1's second commit and RN-2 recorded it**; T1 and T2 bind every twin until RN-6 | held |
| 008 §4 | The picture is excluded from the hash; the twins-absent replay proves it | The licence for every rendering slice; the backdrop inherits it; `TheReplayIsTheSameWithNoTwins` gates RN-1 | held |
| 009 §2 | What pins the port — and `ThePictureIsAsRecorded` is not yet in its table | Every rendering gate here is that record; P-0 adds it to the table | held |
| 009 §4 | "New behaviour must say what it IS and assert it" | Every gate in §5 is an equality the tree can take today, none a comparison | held |

**What the pass changed.** Three sections were silent where an ADR spoke and now are not: the
backdrop's exclusion from the digest (§3.4), the death sequence as a task and the arena's bound
(§3.5), and the portability of `Run` (§3.1). One thing the design had described as an addition is a
reversal and is now named as one (§3.6, ADR-005 §2) — and ruled the same day (§12 R1). One slice turned out to be phase-6-class by the
corpus's own definition and is sequenced accordingly (I-2's second commit). Nothing in the nine
forbids the track as ordered; what they forbid — sub-pixel geometry, a wider view, a display list
with float in it — the track does not attempt, and the previous review's recommendations on scoping
the float ban and consolidating the canvas clauses are what would let a later track attempt them.

---

## 12. The rulings, 2026-09-09

Eight questions were put to the owner on the day the design opened: the four of §4 and four on how
the ADRs should change after the review of all nine (§11). Every ruling was written into the
document that owns it the same day; this table is the index. One question was answered with a
question — *"should we not move the input from sampling to event driven?"* — and the answer to
that reshaped D3, so it is recorded here rather than smoothed into the row.

| # | Question | Ruling | Recorded in |
|---|---|---|---|
| D1 | Retire the C64 canvas once the picture has a frame boundary (RN-6)? | **Yes**, last in the track, after RN-2 and I-4, on the two-column gate | ADR-001 §1, ADR-002 §4, ADR-003 §3, ADR-007 §4, ADR-008 Status; §5 RN-6 |
| D2 | Coroutines or a game thread for the blocking reads? | **Coroutines, after M6-d.** The death sequence becomes a task | ADR-007 §2; Modernize.md M4-d; §3.5, §5 I-4 |
| D3 | Expose the step rate as a `speed` setting? | **No knob — change the game logic.** A fixed-rate flight model, one step per simulated blank with the per-step constants rescaled, as its own track and ADR after RN-6, **faithful cadence selectable**. Event accumulation into the frame is what this track delivers in flight | ADR-001 §4, ADR-005 §3 and §4; §3.2, §3.3, §5 FR |
| D4 | PAL or NTSC? | **NTSC**, settled by ADR-001's own variant line; PAL selectable | ADR-005 §3 |
| R1 | The sound interrupt's clock | **The simulated blank clocks the interrupt**; the device paces the chip alone. ADR-005 §2 is reversed | ADR-005 §2; §3.6, §5 T-4 |
| A1 | ADR-002's float ban: by directory or by role? | **By role, now.** Simulation integer for good; presentation may use float; no float enters `GameLogic/` until a slice names and exempts a renderer file. §4's "not phase-6 items either" struck | ADR-002 §1 and §4 |
| A3 | ADR-004 §1's "presentation is not unit-tested" | **Amend the sentence, keep `EXECUTABLE_SOURCES`**; no fifth project | ADR-004 §1; §3.1 |
| A4 | ADR-005: amend or supersede? | **Amend now; ADR-010 supersedes it at P-0** | ADR-005 status; §5 P-0 |

**The exchange behind D3, because it changed the design.** Asked whether input should move from
sampling to event-driven: modern shooters do both in a fixed order — events collected by the OS,
drained once per fixed simulation tick into a snapshot the game polls (Doom's `ticcmd`, Quake's
`kb.msec`, Source's `usercmd`), with the picture interpolated between ticks at the display's rate.
The port already has the first two layers, and `InputFrame` is a `usercmd`; what it lacked was the
accumulation that stops a tap between ticks being lost (§3.3, added) and the third layer, which needs
a display list and a renderer that may use float (A1, ruled) — neither of which this track builds.
The tick's *rate* is the game's, and that is what "change the game logic" ruled: not a knob on the
original's cadence but a fixed-rate model beside it.
