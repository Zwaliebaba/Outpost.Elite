# Platform — the build plan, slice by slice, written for an agent

**Status:** opened 2026-09-09 against `32a5faa`, the tip of PR #25, where [Platform.md](Platform.md)
is ruled and nothing of it is built. **This is the execution plan for that design**: every slice
in Platform.md §5, in order, as steps an agent can take without a person beside it — the files, the
signatures, the tests to write, the commands that gate it, the numbers to update and the entry to
journal. It does not re-argue the design; where a step below departs from Platform.md's wording it
says so and why, and the slice's journal entry amends the design (§0 rule 9).

**How to use it.** Read AGENTS.md whole, then Platform.md §3, §4, §6 and §12, then this document's
§0, then the slice you are building. One slice per branch and per pull request. Do not start the
next slice until the previous one is merged to `main` with CI green on all three jobs.

**What this environment can and cannot verify**, because the plan is written around it:

| Can, here on Linux | Cannot, and what stands in |
|---|---|
| `python tools/check_all.py` — all thirteen repository checks | Compiling `Outpost/Window.cpp`, `ScreenPresenter.cpp`, `Shell.cpp`, `Main.cpp`, `SoundOutput.cpp` — Win32. **The Windows CI job is the compiler**; `check_outpost.py` is the local proxy for names and arities and cannot see types |
| `Tests/PortableRunner/run_tests.sh` — the whole suite, plus the five `Outpost/` files `generate_runner.py` names in `EXECUTABLE_SOURCES` | Playing the game. A gate that says "play:" is the owner's, and the journal entry says "not played here" rather than implying it was |
| `python tools/mutate.py --unit X --runner portable` — the mutants | PresentMon, a GPU, a 144 Hz panel. T-3's number is the owner's to take |

---

## 0. The protocol every slice follows

Read once; every slice below assumes it.

1. **Branch** `claude/platform-<slice>` from `main`. Confirm `python tools/check_all.py` and
   `Tests/PortableRunner/run_tests.sh` are green on `main` before changing anything; if they are
   not, stop and report — a red base is not this slice's to fix silently.
2. **Read the slice's row in Platform.md §5 and the sections it cites**, and the archived design
   where the row names it (`Archive/Rendering.md`, `Archive/InputTimer.md`); the archive holds the
   findings, the counts and the sites.
3. **Measure before changing.** Every count this plan quotes was taken on `32a5faa` with the command
   beside it; re-take it on your base and, if it differs, believe the tree and say so in the journal.
4. **New files go in both project files** (`.vcxproj` and `.vcxproj.filters`) in the same commit,
   with a name unique repo-wide and against the CRT, STL and Windows SDK, case-insensitively
   (AGENTS.md §3). A new `Outpost/` file with no Win32 in it is added to `EXECUTABLE_SOURCES` in
   `Tests/PortableRunner/generate_runner.py` AND to `Tests/GameLogicTests/GameLogicTests.vcxproj`
   as `..\..\Outpost\<Name>.cpp`, the way `Presentation.cpp` is.
5. **The four instruments, after every commit**, and all four green before it is pushed:
   ```
   python tools/check_all.py
   Tests/PortableRunner/run_tests.sh              # N passed, 0 failed
   python tools/mutate.py --check-selftests --runner portable
   git status --short                              # nothing unintended, no build output
   ```
6. **Marked numbers.** `check_counts.py` fails when a `<!--count:NAME-->` number in any document
   disagrees with the tree. Adding a test moves `tests` (README.md, Resolution.md); adding a check
   moves `checks`; Platform.md carries `main-lines`, `outpost-elite-names` and `effects-seams`.
   Run `python tools/check_counts.py --list`, fix the marked numbers in the same commit, and never
   touch an unmarked number in a journal — that is history.
7. **The ratchet only goes down** (Modernize.md §5 rule 5). A count that fell: `python
   tools/check_modernize.py --update`, and the slice's name in `tools/modernize_ratchet.json`'s
   `slice` field and in the journal. A count that rose is a stop: either the rise is the slice's
   deliberate, journaled reason (S-1 raised `main-lines` by three and said so; I-4 raises
   `aggregate-refs` by one and §2.7 says why) or it is a defect.
8. **A record moves only under a named case** (ADR-007 §4: widened, a defect fixed, or — for RN-6
   alone — narrowed with its proof column). A replay digest, `RECORDED_PICTURE` or a docked digest
   that moves in any other slice means the slice changed the game: **stop, do not re-record, report
   the first checkpoint that moved and what you changed last.** Never edit a recorded table to make
   a test pass.
9. **Journal as you land**: a dated entry in Platform.md §10 in the corpus's voice — what was built,
   what was measured, what the estimate got wrong, which ceilings moved — and the slice's row in §5
   marked ✅ with the date. If the build changed the design (a boundary somewhere else, a count that
   was wrong), amend the section of Platform.md it changed in the same commit and say so in the entry.
10. **Commit messages** in the tree's shape: a title line `<Slice>: <what it did, as a sentence>`, a
    body that says what was built, what was measured and what moved, no model identifiers, and the
    attribution trailer the session gives you. Small commits; each one green on the four instruments.
11. **Push and watch CI.** The Windows job is the only compile of `Outpost/`. A red Windows job on
    your push is yours: read the log, fix, push. Do not merge red; do not re-run to see if it goes
    away.
12. **Report** in the shape of §4 at the end of this document. "Builds, not run" and "green on the
    portable runner" are different claims; make the one that is true.

**Never**: fix an original bug (ADR-001 §3); put `float`, a clock, a thread or a file in
`GameLogic/`; skip or weaken a test to go green; re-record a digest without its case; widen a slice
because you are "in there" (Modernize.md §5 rule 8); merge with the Windows job red.

---

## 1. Order, and what each slice needs green before it starts

```
T-1 ──► T-3 ──► RN-0 ──► RN-1 ──► RN-2 ──► I-2a ──► I-2b ──► I-4 ──► T-4 ──► RN-6 ──► P-0
 │       │                                                        ▲
 │       └── executable only; may run before or beside RN-0..RN-2 │
 └── executable only; the first slice because every later one reads its scheduler
C-1 is not a slice: its items ride inside T-1, I-2 and I-4 (§2.9)
FR (the fixed-rate flight model) is its own design and is NOT in this plan (Platform.md §12 D3)
```

Platform.md §5 allows T-1, T-3 and RN-0 in any order. This plan fixes one, because an agent should
not choose: **T-1 first** (every later slice reads its scheduler and it touches no library file),
**T-3 second** (still executable-only, and RN-0 gives it the frame counter it wants), then the
rendering slices, then input, then the coroutines that need both, then the sound, then RN-6.
I-4 waits for Modernize.md's M6-d to be complete: check `python tools/check_modernize.py --list`
shows `opcode-transcriptions` at 0 — it does on `32a5faa` — so nothing is waiting on it now.

---

## 2. The slices

### 2.1 T-1 — `MachineTiming`, `FrameClock`, `Scheduler` (executable only; 2 sittings)

**Goal.** One integer clock and one scheduler replace the four `double` accumulators
(`Main.cpp`'s `accumulated` and `dockedLeftover`, `Shell.h`'s `m_spinLeftover` and
`m_flightFrameLeftover`) and `PlanSteps`; `DELAY` counts simulated vertical blanks; a stall costs a
frame and is counted; an inactive window plans nothing. No speed knob (Platform.md §12 D3). The
nested pump stays until I-4, so the scheduler is written to be fed from `Turn()` at any call depth.

**Reads.** Platform.md §3.2; `Outpost/Presentation.h` (the cost tables, `PlanSteps`,
`TitleTurnSeconds`, `FlightFrameSeconds`, `DockedPassSeconds`), `Main.cpp`, `Shell.h/.cpp`,
`SoundOutput.h` (its `CLOCK_HZ`/`FRAME_CYCLES`), `Window.h/.cpp`, `SettingsFile.cpp` (the `KEYS`
table and the `joystick` special case), `Tests/GameLogicTests/ShellTests.cpp`
(`TheStepPlannerNeverSkipsOrDoublesSilently`, `TheTitleShipIsPacedByWhatATurnCosts`,
`TheFlightFrameAndTheDockedPassArePacedByWhatTheyCost`).

**Files.**
- New `Outpost/Scheduler.h`, `Scheduler.cpp` — `MachineTiming`, `Scheduler`; no Win32, no `<chrono>`;
  in `EXECUTABLE_SOURCES` and `GameLogicTests.vcxproj` (rule 4).
- New `Outpost/FrameClock.h`, `FrameClock.cpp` — the one `<chrono>` reader; Windows-only listing is
  fine (nothing tests it), but it has no Win32 either, so list it portably too.
- New `Tests/GameLogicTests/SchedulerTests.cpp`.
- Changed: `Presentation.h/.cpp` (cycle functions replace the seconds ones; `PlanSteps` deleted),
  `Main.cpp`, `Shell.h/.cpp`, `SoundOutput.h/.cpp`, `Window.h/.cpp`, `SettingsFile.h/.cpp`,
  `ShellTests.cpp`, both `Outpost` project files, the test project file.

**The API to build** (Platform.md's sketch, shaped for a scheduler that is fed from inside hold loops):

```cpp
namespace Outpost
{
  struct MachineTiming
  {
    std::uint32_t clockHz;        // 1'022'727 NTSC (ADR-001's variant; the default), 985'248 PAL
    std::uint32_t cyclesPerFrame; // 65 * 263 NTSC, 63 * 312 PAL
    [[nodiscard]] static constexpr MachineTiming Ntsc() noexcept;
    [[nodiscard]] static constexpr MachineTiming Pal() noexcept;
  };

  class FrameClock          // Outpost/FrameClock.h -- the ONLY steady_clock in the program
  {
  public:
    explicit FrameClock(MachineTiming _timing) noexcept;
    [[nodiscard]] std::int64_t Tick() noexcept;   // integer cycles at clockHz since the last Tick; 0 on the first
  };

  class Scheduler           // Outpost/Scheduler.h -- integer, portable, tested on both legs
  {
  public:
    explicit Scheduler(MachineTiming _timing) noexcept;

    /// Once per turn, at any call depth. Inactive DROPS the cycles rather than banking them (auto-pause).
    void Feed(std::int64_t _elapsedCycles, bool _active) noexcept;

    /// Whole steps the budget affords at `_stepCostCycles`, at most MAX_STEPS_PER_CALL; the backlog
    /// left after them is dropped when it exceeds one step, and `stalled` is set (the time clamp).
    [[nodiscard]] int TakeSteps(std::uint32_t _stepCostCycles) noexcept;

    /// Vertical blanks elapsed since the last call, one per cyclesPerFrame; a backlog beyond one
    /// frame is dropped the same way.
    [[nodiscard]] int TakeBlanks() noexcept;

    /// The stalls since the last call, for the counter and the debug title.
    [[nodiscard]] std::uint32_t TakeStalls() noexcept;

    /// Both accumulators to zero: a mode change, the place Main.cpp zeroed `accumulated` by hand.
    void Reset() noexcept;

    [[nodiscard]] MachineTiming Timing() const noexcept;
  };

  // Presentation.h: the three seconds functions become cycles, and the tables are unchanged.
  [[nodiscard]] std::uint32_t TitleTurnCycles(std::uint8_t _distanceHigh) noexcept;   // was TitleTurnSeconds
  [[nodiscard]] std::uint32_t FlightFrameCycles(std::uint8_t _ships) noexcept;         // was FlightFrameSeconds
  [[nodiscard]] std::uint32_t DockedPassCycles(std::uint8_t _syncs, MachineTiming) noexcept; // DOCKED_PASS_CYCLES + syncs * cyclesPerFrame
}
```

Interpolation in the two cycle functions is integer: `below + (above - below) * along / span` in
64-bit, then narrowed; the old `double` interpolation and the new integer one agree to within one
cycle on every row, which the moved test asserts. `NTSC_CLOCK_HZ` and `NTSC_FRAME_CYCLES` leave
`Presentation.h`; `MachineTiming` is their home.

**Steps, as three commits.**

1. **The scheduler and its tests, beside the old code.** Write `Scheduler.*`, `FrameClock.*`,
   `SchedulerTests.cpp`; add the cycle functions to `Presentation.*` without removing the seconds
   ones. Tests (all on both legs):
   - `NoTimeNoSteps`, `AStepLandsOnTheSecondCallAndTheRemainderCarries`, `ThreePeriodsAreThreeSteps`
     — `TheStepPlannerNeverSkipsOrDoublesSilently`'s cases, in cycles.
   - `ALongGapYieldsOneStepAndAStall`: feed ten step-costs; `TakeSteps` answers
     `MAX_STEPS_PER_CALL` at most and the backlog is dropped; `TakeStalls` is 1; the next call
     answers 0.
   - `FiftyBlanksAreFiveSixthsOfASecondAtNtscAndOneAtPal`: feed `50 * cyclesPerFrame` in
     twelve uneven pieces on each timing; `TakeBlanks` sums to 50 on both, however the pieces fell.
   - `AnInactiveTurnPlansNothingAndBanksNothing`: `Feed(x, false)` then `Feed(0, true)`:
     `TakeSteps` and `TakeBlanks` are 0.
   - `TheCycleTablesAgreeWithTheSecondsTheyReplace`: for every ship count 0..10 and every distance
     1..96, `FlightFrameCycles`/`TitleTurnCycles` equals the old function times `clockHz`, rounded,
     within one cycle. (Delete this test in commit 2 with the seconds functions; it exists to prove
     the port of the arithmetic.)
   - The hundred-second drift property from the old test, in cycles.
2. **The switch.** `Main.cpp`: `FrameClock clock{timing}; Scheduler scheduler{timing};` in `Run`;
   `Turn()` gains `scheduler.Feed(clock.Tick(), window.Active())` as its first line (so every call
   depth feeds it); the flight loop takes `scheduler.TakeSteps(FlightFrameCycles(ships))`, the
   docked loop `TakeSteps(DockedPassCycles(dockedSyncs, timing))`; `accumulated = 0.0` on a mode
   change becomes `scheduler.Reset()`. `Shell.cpp`: `WaitFrames(n)` loops `Turn()` until
   `TakeBlanks()` has summed to `n`; `HoldTitleFrame(d)` and `HoldFlightFrame(ships)` loop `Turn()`
   until `TakeSteps(cost)` answers at least 1 (a second step in the same answer is dropped: a hold
   is one frame); the two `std::chrono` members and their leftovers go. `SoundOutput` takes a
   `MachineTiming` at construction and its two constants become members. Delete `PlanSteps`,
   `StepPlan`, the seconds functions and the seconds test; `ShellTests`' two pacing tests assert the
   cycle tables. `Window` gains `[[nodiscard]] bool Active() const noexcept` — false from
   `WM_ACTIVATEAPP(FALSE)` and `WM_KILLFOCUS`, true from their opposites, and false while the client
   area is empty. Stalls: after each `TakeSteps`, `for (auto n = scheduler.TakeStalls(); n; --n)`
   `OutputDebugStringA("stall\n")`; in `_DEBUG` builds the window title becomes
   `L"Elite  [stalls: N]"` through `SetWindowTextW`.
3. **The machine as a setting.** `SettingsFile` gains a `machine = ntsc|pal` line handled like
   `joystick` is (a named special case, not a `KEYS` row, because it is not a `Universe` byte);
   `SettingsReport` carries a `MachineTiming timing` defaulting to NTSC; `Main.cpp` builds the clock,
   the scheduler and `SoundOutput` from it; an unknown value is reported and NTSC used. Two
   `ShellTests::TheSettingsFile` cases: each value parses; a bad value reports and defaults.

**Gate.** All four instruments; `check_outpost.py` clean (it reads `Main.cpp`'s calls); the
Windows job green. Owner's play checks, journaled as the owner's: `dn2`'s beep pause the same length
on a 60 Hz and a 144 Hz panel; Alt+Tab away a minute, back on the same step; the title bar counts a
stall when a breakpoint is released.

**Ratchets and numbers.** `main-lines` moves either way: the accumulators go (−12) and the
scheduler comes (+8); whichever way, `--update`, journal, and fix Platform.md's
`main-lines` marker (spelled `<!--count:NAME-->` there). `tests` marker in README.md and Resolution.md (+6, −1).

**Journal.** What the integer interpolation disagreed with the `double` by (expect 0 or 1 cycle per
row), and whether `Turn()`'s single `Feed` at every call depth double-fed anywhere (it must not:
`Tick()` is monotone and `Feed` is idempotent on zero elapsed).

### 2.2 T-3 — the presenter: waitable, on change, idle when hidden (executable only; 1–2 sittings)

**Goal.** `ScreenPresenter` waits on the swap chain's frame-latency object with a queue of one,
presents only when something changed, and idles on occlusion. Platform.md §3.7.

**Reads.** `ScreenPresenter.h/.cpp`, `Shell.cpp` (`Turn`), `FlightSession.cpp`
(`SyncVideoRegisters` — the six canvas setters it calls are the raster state `Picture::Resolve`
reads from the canvas), `Picture.h`, `VideoState.h`, `Controls.h` (`SIGHT_SPRITE_CELL`).

**Steps, as two commits.**

1. **The latency object.** In `Create`: `chain.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`;
   after `as<IDXGISwapChain3>()`: `SetMaximumFrameLatency(1)` and
   `m_frameLatency = m_swapChain->GetFrameLatencyWaitableObject()`. `Resize` passes the same flag to
   `ResizeBuffers` (it must, or the call fails). New `void WaitForFrame() noexcept`:
   `WaitForSingleObjectEx(m_frameLatency, 1000, TRUE)`; `Destroy` closes the handle. `Present`
   returns an enum `{Presented, Occluded, Lost}` instead of `bool`: `DXGI_STATUS_OCCLUDED` is
   `Occluded`, the two device-removed codes are `Lost`. `Turn()` calls `WaitForFrame()` before the
   pump, and on `Occluded` waits `MsgWaitForMultipleObjects(1, &latency, FALSE, 100, QS_ALLINPUT)`
   instead of spinning; `ScreenPresenter` exposes the handle for that one call. Delete
   `ScreenPresenter::Ready` (no caller; C-1).
2. **Present on change.** **BUILT DIFFERENTLY — read Platform.md §10 before touching this.** The
   present cannot be the thing that is skipped: the latency object is a semaphore released when a
   presented frame RETIRES, so a skipped present leaves nothing to retire and the next wait runs to
   its cap, and the present is besides the vertical sync every hold in `Shell.cpp` counts turns
   against. What the tree skips is the RESOLVE and the upload, on `Picture::ResolveSignature` --
   which is in `GameLogic` beside the reads it lists, not in the shell, because `outpost-elite-names`
   refused the shell version and was right to. The paragraph below is the plan as written and is
   kept for the record. `Picture` gains `std::uint32_t Generation() const noexcept` and a member
   `m_generation` bumped by every mutator (`WriteBitmap`, `ExclusiveOrBitmap`, `SetCell`, `SetDot`,
   `ExclusiveOrDot`, `Clear`) — beside `m_drawing`, not game state, not folded, not resolved; a
   `PictureTests` case asserts `Hash` ignores it and each mutator moves it. The shell keeps a
   `PresentedSignature { std::uint32_t picture; RasterState raster; VideoState video; }` where
   `RasterState` is a small struct the shell fills from the canvas's six raster getters (add
   `const` getters beside the six setters `SyncVideoRegisters` calls, if they do not exist) plus
   the eight sprite-pointer bytes at `SIGHT_SPRITE_CELL`; `Turn()` presents only when the
   signature differs from the last presented one, and always on the first turn and after a resize.

**Gate.** Windows job green; the picture's own tests unchanged; the owner's: Task Manager at rest
with the window hidden behind another (expect under 1 % of a core), and PresentMon's
`MsUntilDisplayed` on a scripted key before and after, written into the journal by the owner.
This slice has no test on the Linux leg beyond `PictureTests`' generation case.

**Journal.** Whether `ResizeBuffers` needed the flag (it does — say that you hit it if you did), and
the two numbers if the owner took them.

### 2.3 RN-0 — the backdrop and the frame (library; 2 sittings)

**Goal.** `Universe` gains a second `Picture`, the **backdrop**; the persistent draw sites write it;
the transient ones keep writing `picture`, which becomes the **frame**; `Resolve` composites the two.
Pixel-identical to today by construction. The docked screens get a picture fixture. The hyperspace
rings get a recorded checkpoint.

**A departure from the design, and the reason.** Platform.md §3.4 and Archive/Rendering.md RN-0 say
the frame is *refreshed from the backdrop each frame* while the twins "still exclusive-or". Those
two cannot both be true: an erase twin that redraws last frame's ship onto a freshly copied frame
draws a ghost. So RN-0 does not refresh anything. **The frame is composited over the backdrop by
exclusive-or at resolve** — bit = backdrop.bit ^ frame.bit in the upper region, cell palettes and the
dashboard plane from the backdrop alone — which is exactly the picture the tree draws today
(exclusive-or is associative, and every transient write is an exclusive-or), costs no copy at all,
and leaves RN-1 to start clearing. The slice's journal entry amends §3.4's "memcpy of 36,000 bytes"
to this, and RN-2 corrects ADR-008 §1's byte count for two full `Picture`s (215,364 bytes) rather
than the 36 KB the design assumed; if the owner wants the frame narrowed to a bitmap-only type
afterwards it is a follow-on and not this slice.

**Reads.** Archive/Rendering.md §12 (the classification of the 74 sites; the three defaults);
`Universe.h`, `Picture.h/.cpp`, `StateHash.cpp` (the fold that skips `picture`, line ~397),
`Tests/GameLogicTests/StateCells.cpp` (the same, ~138), `StateHashTests.cpp`
(`TheStateHashDeliberatelyDoesNotSeeIt`), `FlightReplayTests.cpp` (`Trace`, `Fly`, the checkpoint
lambda, `RECORDED_PICTURE`), `DockedSessionTests.cpp` (`Session`, `ScriptedKeys`,
`TranscriptSink`), `PictureTextTests.cpp` (how a fixture attaches a picture to the printer),
`Flight.cpp` (`DrawTunnel`, `DrawHyperspaceTunnel`), `tools/check_twins.py`.

**The sites.** `grep -n "universe.picture\|_universe.picture\|\.picture)" GameLogic/*.cpp` finds
them (74 on `32a5faa`; recount). The rule that classifies each:

| Writes | Goes to | Because |
|---|---|---|
| the dashboard plane (dials, indicators, blips, compass, the dashboard copy) | **backdrop** | stored or self-erasing; R-7 |
| the upper bitmap and is re-emitted every flight pass by exclusive-or (ships, the point, the explosion cloud, the planet, the sun, the stardust, the two laser draws) | **frame** | transient |
| the upper bitmap once per view change or per message (glyphs, wipes, borders, the charts, messages, `ClearMessageRows`, the loader, `Launch`'s crosshairs, `SetUpScreenPixels`) | **backdrop** | persistent; R-2 |
| `SeedStardustField`, `SeedStardustAndClearShips`, `FlipStardust` | **frame** | Platform.md §3.4's default; the gate decides |
| `DrawHyperspaceRings` (through `DrawTunnel`) | **frame** | each ring is its own present |
| `ClearAllShips` | **frame** | the view change's wipe rewrites the backdrop |

A site moves by changing the pointer it passes: `&_universe.picture` → `&_universe.backdrop`. Twins
do not change. `check_twins.py` does not care which surface a twin gets and needs no change here.

**Steps, as three commits.**

1. **The field and the composite.** `Universe`: `Picture backdrop;` declared beside `picture`, with
   the comment saying it takes `picture`'s treatment (ADR-007 §6, ADR-008 §4). `StateHash.cpp`: skip
   it beside `picture`, with a sentence. `StateCells.cpp`: the same. `StateHashTests`: the
   exclusion test covers both. `Picture::Resolve` and `Hash` take `const Picture& _backdrop`;
   `ResolveBitmapCell` reads `backdrop.ReadBitmap(offset) ^ ReadBitmap(offset)` and the cell from
   the backdrop; the dashboard cell from the backdrop. **THE LAST TWO CLAUSES CONTRADICT THIS
   COMMIT'S OWN GATE, found 2026-09-09 while reading ahead from T-3, and the fix is one word.** No
   site has moved yet at this commit, so every cell palette and every dashboard index is still in
   the FRAME; reading them "from the backdrop" reads a blank one, and the four instruments the
   paragraph below promises green would return a black screen and a hundred moved digests. Composite
   all three planes the same way the bitmap is composited — `backdrop ^ frame`, on the palette byte
   and on the dashboard index as well as on the bitmap bit. That is identity while either side is
   blank, which is what makes this commit a no-op; it stays correct after commit 2 moves the
   persistent sites, because a cell written only to the backdrop reads `backdrop ^ 0`; and it is
   what the surrounding argument already says the composite is. The "from the backdrop alone"
   wording describes where those planes will LIVE once the sites have moved, not how to read them.
   The one thing it costs is that a cell palette written to BOTH surfaces would exclusive-or into
   nonsense rather than one winning — which the site table forbids and `ThePictureIsAsRecorded`
   would catch, so say in the journal whether any site turned out to do it.
   Every caller changes in the same commit:
   `ScreenPresenter::Present` (its signature gains the backdrop — `check_outpost.py` will say so),
   `FlightReplayTests`' checkpoint (`picture.Hash(canvas, backdrop)`), the five `Picture*Tests`
   files (pass an empty backdrop where they resolve). With no site moved yet, the backdrop is blank
   and every hash is unchanged: **all four instruments green with nothing else touched.**
**THE THREE STEPS BELOW ARE IN THE WRONG ORDER, found 2026-09-09 after step 1 was built.** Step 2
moves the sites and step 3 builds the fixtures that would notice if a move was wrong, so step 2 is
performed with no gate over most of what it touches. `ThePictureIsAsRecorded` watches a scripted
FLIGHT; `DockedSessionTests` does not look at pixels at all — it has no `Hash`, no `Resolve` and no
recorded table — and **27 of the 81 sites are on docked screens** (`Charts`, `Equipment`, `Game`,
`MarketScreen`, `Missions`, `StartUp`, `GameLoop`). A misclassified chart or market glyph would
move nothing any test asserts and would be found by a person playing, which is the failure mode
this corpus is built to avoid. **Do step 3 first**, recording both tables on the unmoved tree,
then step 2 against them: the tables then say what the split preserved rather than what it
produced. Step 3's own wording already assumes it runs second ("recorded on this commit's tree —
after step 2, so they record the split, which is identical to before it") — that sentence is the
one to invert, and the identity it claims is exactly what recording first would let the move
PROVE rather than assert. Renumber when performing, and say in the journal which order was used.

2. **The sites.** Move the persistent sites to the backdrop per the table. After each file:
   `run_tests.sh` and `ThePictureIsAsRecorded` green, or the site you just moved is transient and
   goes back. Expect the three defaults to hold; if one does not, the gate has ruled and the journal
   says which way.
3. **The two fixtures.** (a) `DockedSessionTests`: `Session` attaches `universe.picture` (the frame)
   and `universe.backdrop` to its printer the way `PictureTextTests` does, and a new
   `TheDockedScreensAreAsRecorded` records `picture.Hash(canvas, backdrop)` after each of the seven
   screens `EveryDockedScreenIsReachableInOneSession` visits, as `RECORDED_DOCKED_PICTURES[7]`,
   printed on every run like `RECORDED_PICTURE` is. (b) `FlightReplayTests`: a new
   `TheHyperspaceRingsAreAsRecorded` builds a `FlightPort`, attaches a watching presenter whose
   `Present()` pushes `picture.Hash(canvas, backdrop)`, runs `Elite::DrawHyperspaceTunnel(universe,
   ports)` and asserts the per-ring vector against `RECORDED_RINGS[]`. Both tables are recorded on
   this commit's tree — after step 2, so they record the split, which is identical to before it.

**Gate.** `ThePictureIsAsRecorded` unmoved through steps 1 and 2; the two new records taken at
step 3 and green; `TheReplayIsTheSameWithNoTwins` green (the backdrop's `SetDrawing` is the
frame's: `DrawingTwins` guards both through one flag — give `backdrop` no flag and have the
guard sites that write it test `picture.Drawing()`; simplest is a `Universe::DrawingTwins()` that
reads `picture` and every site uses it).

**Ratchets and numbers.** `tests` +2. `outpost-elite-names` may rise by one (`backdrop`) — journal
it, as I-1 journaled its rise for the same reason. `UniverseImage`'s size comment in `Universe.h`:
215,364 bytes for the two pictures.

**Journal.** The count of sites moved per surface; which of the three defaults the gate confirmed;
the amendment to Platform.md §3.4 (composite, not copy) and the byte count.

### 2.4 RN-1 — the frame boundary (library; 2 sittings)

**Goal.** The frame is cleared at every present; the frame-side erase twins become drops; the
routines that relied on erasure to leave the right pixels redraw from state. Pixel-identical.

**Where the boundary is, and why it is a library call.** The replay never calls the executable's
loop, so a clear done in `Shell::Turn` would be invisible to every gate. The boundary is therefore
`Elite::Game::EndFrame()`: `m_universe.picture.Clear()` and nothing else, called by `Main.cpp` after
`Present` returns and by the replay driver after each checkpoint (`Step` → `checkpoint()` →
`game.EndFrame()`), so the checkpoint hashes the frame as presented. The four in-library presents
(`WaitFrames`, `Present`, the two holds) are wrapped by free functions in a new `GameLogic/Frame.h`
— `Elite::PresentFrame(Universe&, Ports&)` etc. — that call the port and then clear the frame; the
sixteen wait sites (§2.7 lists them) call the wrappers. Under I-4 these wrappers become the
awaitables and the clear moves into `co_await`, which is why they exist as functions now.

**The drops**, measured on `32a5faa` — the picture-side arm of each of: `EraseShip`
(`ShipDraw.cpp`), `EraseSun`, `EraseBall`, `ErasePlanetOrSun` (`PlanetDraw.cpp`), the stardust's
erase in `MoveStardust` (`Stardust.cpp`), the laser beam's second `DrawLaserLines`
(`FlightLoop.cpp`). **Not** `COMPAS`'s first dot, `me1`'s re-print or `TT103`'s first crosshair:
those write the backdrop after RN-0, and an exclusive-or erase on a surface that is never cleared
still works. Archive/Rendering.md §4.4 listed all nine; six are the frame's.

**The redraws.** After the clear, a routine that drew only the *difference* leaves a hole:
- **The sun** (R-10): `SUN` keeps last frame's half-width per row in `LSO` and draws the two
  changed pieces. The frame needs the whole sun each pass. Add `DrawSunFromState2x(Picture&,
  const PlanetSunState&)` in `PlanetDraw` (a twin: it computes *where* from the widths the faithful
  routine already keeps, at twice the scale, and decides nothing — T1 holds), called where
  `DrawPlanetOrSun` draws the sun, replacing the differential twin arm.
- **The rings** rewind `LSP` so the next circle overwrites the last; with a clear per present each
  ring is its own frame and nothing is needed.
- **Ships, the planet, the stardust, the lasers, the explosion** are drawn whole every pass already.

**THREE THINGS THIS STEP GETS WRONG, found 2026-09-09 by building commit 1 and trying commit 2.**
Commit 1 is built and green; the notes below are what commit 2 walked into.

**(a) The clear must be LAZY, or every digest records a blank frame.** An eager `Clear()` at the end
of a pass leaves the surface empty for everything that looks BETWEEN passes — the replay's
checkpoint, `ThePictureIsAsRecorded`, a presenter reading it a moment later. On the glass an eager
and a lazy clear are identical; to an observer they are not, and the observers are the whole
verification story here. So `Picture::EndFrame()` should MARK, and the next write that lands should
clear first (`BeginFrameIfStale` in each mutator, one predictable branch beside the bounds check
that is already there). The surface then holds the completed frame from the moment it is finished
until the next one starts being drawn, which is when a person is looking at it.

**(b) The rings need a drop too; "nothing is needed" is wrong.** With a clear per present, the
rewind-and-redraw that erases the previous circle draws it back onto an empty frame as a ghost.
`RECORDED_RINGS` moves at present 2 with the clear on and no other change, which is the measurement.

**(c) `ErasingThePlanetClearsBothSurfaces` encodes the OLD contract** and fails by design once the
planet's erase twin is dropped ("the erase left ink on the picture"). It has to be rewritten to the
new one — the frame is cleared, not erased — and that rewrite is part of the slice rather than a
casualty of it.

**And a warning about the stardust.** `PlotStardust` is called at the old position and again at the
bottom of the loop, and which of the two is the erase is not clear from the argument names —
`wasAcrossLow`/`wasDownLow` are passed to the first, and both plot `x1, y1`. Dropping the wrong one
makes the starfield vanish from the frame, which shows up as `ThePictureIsAsRecorded` drawing
checkpoint 0's digest at checkpoint 1 (both frames then being backdrop-only). Read
`MoveStardustAhead` whole before touching it; do not infer from the parameter names.

**(d) — RETRACTED 2026-09-09. THE GATE IS NOT DEFECTIVE; THE FINDING BELOW WAS WRONG.** It is kept
because a corpus that quietly deletes its mistakes teaches nothing, and because the measurement that
overturned it is the one to trust.

The claim was that `ThePictureIsAsRecorded` records "the backdrop and almost nothing else" because
the checkpoint lands at a bad moment. It does not. Counting the CANVAS's own bitmap beside the
frame's at every checkpoint settles it — the canvas is the authority, is never split and is never
cleared:

| step | 0 | 40 | 100 | 200 | 300 | 340 | 342 | 400 |
|---|---|---|---|---|---|---|---|---|
| canvas bytes above its docked baseline of 2092 | 0 | 302 | 316 | 308 | **24** | **23** | **19** | 41 |
| frame bytes | 0 | 563 | 581 | 582 | **12** | **13** | **11** | 82 |
| frame ÷ twice the canvas's | — | 0.93 | 0.92 | 0.94 | 0.25 | 0.28 | 0.29 | 1.00 |

**The frame IS the current picture and tracks the canvas at twice the scale.** Twelve bytes at step
300 is not a blind gate; the scripted flight is passing through near-empty space and the CANVAS has
only twenty-four bytes of transient content there too. The low ratios at those three checkpoints are
byte-packing on very small numbers, not a discrepancy.

**So the ruling to sample at the present is not needed**, on top of not being implementable: the
checkpoint is once per pass, at the same place the executable presents, and is the right sampling
point. The gate's real limitation is the one established at RN-0 and it is different: the composite
is an exclusive-or, so moving an exclusive-or write between surfaces is provably invisible. That is
why the eighty site moves went unnoticed. It has nothing to do with when the sample is taken.

**AND THE 1559 WAS ACCUMULATION, NOT A CLEARED FRAME.** With the clear on and the erase twins
dropped, the frame held two and a half times the ink the canvas justified — because the replay ended
the frame at the CHECKPOINT, once in a hundred steps, so a hundred passes of drawing piled up with
nothing erasing them. Ending the frame after every `Step`, which is what `Main.cpp` does every turn,
removes it. **The boundary is per pass and the replay driver must call it per pass**; RN-1's first
commit put it at the checkpoint and that is the line to change.

**What is still open after all this**, and it is a much smaller question than the one it replaces:
with a per-pass boundary the frame is populated at some checkpoints and empty at others.

**MEASURED 2026-09-09, and it is not about which write comes last.** A probe counting mutator calls,
lazy clears and surviving ink per pass, on steps 36 to 44 of the scripted flight:

| | writes to the frame | clears | bitmap bytes left |
|---|---|---|---|
| clear on, six erase twins dropped | 1246 | 1 | **0** (697 at step 42) |
| the same, stardust erase twin put back | 1294 | 1 | **0** |
| the same, plus the whole field drawn from state at the end of `MoveStardust` | 1342 | 1 | **0** |

**ANSWERED 2026-09-09 by counting writes that LAND, split by primitive.** Per pass, steps 36 to 41:

| | |
|---|---|
| mutator calls | 1246 |
| landed bitmap writes | 1214, **every one an exclusive-or**, none an assignment |
| distinct bitmap offsets touched | 569 — so each is written about **twice** |
| `DrawLine2x` calls | **40** |
| `PlotRelativePixel2x` / `DrawShipLines2x` / `DrawCanvasRow2x` | 12 / 0 / 0 |
| bitmap bytes surviving the pass | **0** |

**Forty line-twin calls, twenty geometries drawn twice at the same coordinates.** That is the
planet: `DrawBall` erases by REDRAWING THE SAME RUN, so with a cleared frame the second draw cancels
the first and the ball vanishes. At these steps the ball is the only thing in the space view, which
is why nothing at all survives.

**So the class of routine RN-1 has to handle is wider than the plan says.** Archive/Rendering.md's
R-10 names two differential renderers, the sun and the hyperspace rings, and calls them the exception.
They are not: **the planet's ball is a third**, it is not in R-10, it is not in the drops list, and it
is the one the scripted flight actually exercises. Anything that erases by redrawing its own geometry
cancels itself on a cleared frame, and the drops list has to be derived from that property rather
than from the six routines the plan happened to name.

The three earlier diagnoses of this measurement — a bad sampling moment, then accumulation, then a
missing whole-field draw — were all wrong, and each was reached by inference from a count that was
measuring calls rather than effects. The counters that settled it are landed writes, distinct
offsets, exclusive-or against assignment, and calls per twin.

**THE SECOND COMMIT, ATTEMPTED 2026-09-09: from 15 of 16 checkpoints moved to 7.** Not finished, and
not committed — the tree stays at the first commit. What follows is the state to resume from.

**Four changes together took it from 15 to 7**, and none of them works without the others:

1. `CLEAR_THE_FRAME` on, with the clear LAZY (`Picture::EndFrame` marks; the next landed write
   clears), for the reason under (a).
2. **The replay driver ends the frame after every `Step`**, not at the checkpoint. This is the single
   largest correction. `Main.cpp` ends it every turn and the replay ended it once in a hundred steps.
3. The frame-side erase twins dropped in `EraseShip`, `PlotStardust`, the laser's second
   `DrawLaserLines`, and — the ones the plan does not list — at the TOP of `EraseSun` and
   `EraseBall`, where dropping once covers everything below.
4. `DrawSunFromState2x` for the sun, and `ClipSunRow` made pure so a twin can ask it what it holds
   without zeroing the heap. The purity is a prerequisite, not a tidy-up.

**The seven that still move, with the frame measured against the canvas** (`frame ÷ 2 × canvas
transient`; 0.9 to 1.0 is right):

| step | 40 | 100 | 200 | 300 | 400 | 500 | 600 | 700 | 800 | 900 | 1100 | 1170 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ratio | 0.93 | 0.92 | **1.76** | 0.25 | **0.18** | **0.57** | **0.53** | **1.22** | **1.40** | 0.03 | **1.49** | 0.00 |
| | held | held | moved | held | moved | moved | moved | moved | moved | held | moved | held |

**They are not one fault but two.** Steps 200, 700, 800 and 1100 carry too MUCH — something inside a
single pass is drawing twice with nothing to cancel it. Steps 400, 500 and 600 carry too LITTLE —
something is still erasing, or a whole-geometry draw is missing. The busy steps are the ones with
ships in the bubble, which is where to look first. Each is the same hunt as the planet's: counters
for landed writes, distinct offsets and calls per twin, then read the routine the count names.

**Do not re-record the tables to make this pass.** A correct erase and a correct clear give the same
frame; that equality is the slice's whole claim, and nine of the sixteen checkpoints already
demonstrate it.

**THE FOUR "TOO MUCH" CHECKPOINTS ARE THE PLANET'S MARKINGS, measured 2026-09-09.** Per-pass
counters either side of checkpoint 200, with the four changes above in place:

| pass | ink before → after | landed | distinct | `DrawLine2x` calls |
|---|---|---|---|---|
| 197 | 578 → **1084** | 1291 | 1087 | **20** |
| 198 | 1084 → 582 | 589 | 582 | 16 |
| 199 | 582 → **1086** | 1295 | 1089 | **20** |
| 200 | 1086 → 583 | 595 | 583 | 16 |
| 201 | 583 → 580 | 589 | 580 | 16 |

**The passes alternate, and the difference is four line-twin calls worth about seven hundred
writes.** Sixteen calls is the planet's outline; the extra four are its markings, drawn through
`DrawEllipse` from `DrawPlanetDetail`. The canvas transient does not alternate (308 throughout), so
on the canvas the markings are present every pass. On the frame they appear on one pass and not the
next, which is what moves the four checkpoints.

So `DrawPlanetDetail` is the file to read next, and the question to answer there is narrow: **under
what condition are the markings drawn, and why is it not every pass.** Either they are conditional
and the frame must redraw them from state like the sun, or an erase on that path is still running.

**The three "too little" checkpoints (400, 500, 600) are a separate fault** and have not been
investigated. Pass 500's counters — landed 612 over only 196 distinct offsets, 40 line calls, 2 ship
calls — show heavy overdrawing of a small area, which is a different shape from the planet's and
wants its own hunt.

**Two sittings of forensics have now gone into this slice and the method is settled**: counters for
landed writes, distinct offsets, exclusive-or against assignment, and calls per twin, then read the
routine the counts name. Every conclusion reached by inference instead has been wrong.

**TEN OF SIXTEEN CHECKPOINTS NOW MATCH EXACTLY**, measured 2026-09-09 against the tree's own ink at
each checkpoint rather than against a ratio. The five drops are: `EraseShip`, `PlotStardust`, the
laser's second `DrawLaserLines`, and at the TOP of `EraseSun` and `EraseBall`. The laser one fixed
checkpoint 3 on its own.

| step | 0 | 40 | 100 | 200 | 300 | 340 | 342 | 400 | 500 | 600 | 700 | 800 | 900 | 1000 | 1100 | 1170 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| tree's frame ink | 0 | 563 | 581 | 582 | 12 | 13 | 11 | 82 | 179 | 293 | 488 | 1081 | 9 | 8 | 404 | 0 |
| with the slice | 0 | 563 | 581 | 582 | 12 | 13 | 11 | **15** | **249** | **263** | **845** | **1758** | 9 | 8 | **592** | 0 |
| delta | · | · | · | · | · | · | · | −67 | +70 | −30 | +357 | +677 | · | · | +188 | · |

**And the six that remain are NOT a missed erase.** Per-twin counts for the same passes, tree
against slice, show the drops doing exactly and only what they should:

| pass | landed | `DrawLine2x` | `DrawShipLines2x` | `PlotRelativePixel2x` | `PlotPixel2x` |
|---|---|---|---|---|---|
| 396 | 330 → 318 | 32 → 32 | 4 → 4 | **24 → 12** | **13 → 7** |
| 697 | 1614 → 1598 | 28 → 28 | 2 → 2 | **24 → 12** | **16 → 8** |
| 700 | 1292 → 1276 | 24 → 24 | 2 → 2 | **24 → 12** | **17 → 9** |

The dot count halves because the stardust erase is gone, and the point count halves with
`EraseShip`. Lines and ship lines are untouched. **So every pass draws the right things; what
differs is what SURVIVES it.** At step 400 the tree's frame holds 82 bytes and one pass of the
slice's holds 15, which means the tree's 82 is an ACCUMULATION across passes and not one pass's
work — something is drawn once and left, and the per-pass clear takes it away.

**AND IT IS NOT THAT EITHER.** At checkpoint 400 the tree's frame holds 82 bytes and the NEXT PASS
TOUCHES ALL 82 OF THEM — nothing persists across passes, so the boundary is not the question. The
per-pass boundary is right.

**Every drop was then verified by restoring it one at a time**, which is the check that should have
come first:

| restored | checkpoints moved |
|---|---|
| nothing (all five drops in place) | **6** |
| the ship erase | 6 — neutral here, though it must still go |
| the stardust erase | **13** |
| the laser erase | 7 |
| the sun's and the planet's erases | 9 |
| additionally dropping the explosion cloud's erase | 6 — neutral |

**So all five drops are necessary and none is wrong, and the six that remain are not caused by any of
them.** Something else still draws and erases the same geometry inside a single pass. It is not the
ships, the stardust, the laser, the sun, the planet or the explosion cloud — each of those has been
tested by restoring or adding its drop and watching the count.

**Where to look next**, with everything above already ruled out: the compass and the scanner blips
write the dashboard INDEX plane rather than the bitmap, so they cannot be it; `ClearAllShips` and the
ship-as-point (`DrawShipAsPoint`, whose `PlotPixel2x` count halves with the ship erase) have not been
tested. The method is the one that found the planet: per-twin counters for a moved pass against the
same pass on the tree, then read the routine whose count differs.

**FOUND 2026-09-09, AND IT WAS NEITHER OF THOSE TWO.** `ClearAllShips` runs at a view change and not
per pass, and `DrawShipAsPoint` erases through `EraseShip`, which was already dropped. **It is `LL9`
part 9 — `OpenHeapRun` in `ShipDraw.cpp` — which opens a ship's heap run by drawing LAST pass's ship
a second time to rub it out.** One line, inside a routine whose name says nothing about erasing,
absent from Archive/Rendering.md §4.4's list of nine and from the drops list above. Dropping it took
all six remaining checkpoints to zero in one edit.

**It caused BOTH faults, which is why they looked like two.** Where the ship had not moved between
passes the ghost cancelled the new draw and the checkpoint held too little (400, 500, 600); where it
had, the ghost stood beside it and the checkpoint held too much (700, 800, 1100).

**The instrument was per-WRITE and not per-call, and that is what all the earlier failures were.**
Every write that landed on the frame was recorded with its offset, its mask and a tag naming the
primitive that made it, for one pass either side of each moved checkpoint; then the offsets whose
writes exclusive-ored back to nothing were grouped by the pair of primitives that had cancelled
them. Checkpoint 400 came back "74 offsets cancelled, every one a ship line against another ship
line", which points at a routine rather than at a hypothesis to test.

**So the rule is a property and not a list.** Anything that erases by redrawing its own geometry
cancels itself on a cleared frame, whatever it is called and whatever file it is in. The plan's six,
plus the explosion cloud, plus this one, is seven — and the seventh is the one no list would have
contained, because every list here was built from names.

**THE SUPERSEDED FINDING FOLLOWS, kept for the record.** Measured
2026-09-09 by counting non-zero bitmap bytes on both surfaces at every checkpoint of the scripted
flight, first on the tree as it stands and then with the clear on and the erase twins dropped:

| checkpoint (step) | 0 | 40 | 100 | 200 | 300 | 340 | 342 | 400 |
|---|---|---|---|---|---|---|---|---|
| frame ink, tree as it stands | 0 | 563 | 581 | 582 | **12** | **13** | **11** | 82 |
| frame ink, clear on + drops | 0 | 563 | 1559 | 2034 | 1568 | 404 | 21 | 537 |
| backdrop ink, both | 4212 | 4212 | 4212 | 4212 | 4212 | 4212 | 4212 | 4212 |

**Twelve pixels of frame at step 300.** `ThePictureIsAsRecorded` is therefore recording, for most
of its checkpoints, the BACKDROP and almost nothing else: the checkpoint lands at a moment when the
pass's transients have been erased and the next pass has not drawn them. That is why RN-0's site
moves were invisible to it, and it is why the table cannot be the thing RN-1 holds identical — with
a frame that is cleared and redrawn whole, the checkpoint sees the ships and the starfield a player
is looking at, and the digests move by design.

So RN-1 cannot both clear the frame and leave `RECORDED_PICTURE` unmoved.

**THE OWNER RULED "SAMPLE AT THE PRESENT" ON 2026-09-09, AND BUILDING IT DISPROVED ITS PREMISE.**
There is no present to sample. `HoldFlightFrame` appears in `GameLogic` exactly once, in `Die`, and
`FlightLoop.cpp` contains no call to the presenter at all: **an ordinary flight pass never presents
inside the library.** The pacing is `Main.cpp`'s, through `Shell::Turn`, which the replay does not
run. A watch attached over the whole flight fires during `Launch`'s tunnel and then not again until
the end — sixteen checkpoints came back with two distinct digests between them, fourteen of them
the picture as the launch left it.

`RECORDED_RINGS` works because the tunnel DOES present in the library, per circle. The flight does
not, and that is the difference.

**So the ruling needs the flight loop to have a present before it can be carried out**, and that is
I-4's work — `Run()` becoming a library function — not RN-1's. Until then the checkpoint, once per
pass at the same place the executable presents, is the only frame boundary the replay can see, and
"sample at the present" and "sample at the checkpoint" are the same instruction with no present to
distinguish them.

**What is still unexplained, and should be settled before anything is re-recorded.** The frame holds
twelve non-zero bytes at step 300 and thousands once cleared. Something erases the pass's transients
before the pass ends, or the frame is accumulating a difference rather than a picture. Neither
reading has been established; the ink table is the evidence and the next sitting should start by
explaining it, not by re-recording around it.

**Steps, as three commits.** (1) `Game::EndFrame`, `Frame.h`'s wrappers, the sixteen sites, the
replay driver and `Main.cpp` calling them — with `Clear` still a no-op behind a `constexpr bool`
so the digests prove the plumbing alone. **BUILT 2026-09-09; nineteen sites, not sixteen.** (2) The clear switched on, the six drops made, the sun
twin added, `check_twins.py` given a fourth table `ERASE_NEEDS_NO_TWIN` with a reason per entry.
(3) The `PictureTests` case for `Clear` (there is none, because nothing called it).
**(2) AND (3) BUILT TOGETHER 2026-09-09: SEVEN drops and not six, and one thing the plan did not
know about — the hyperspace tunnel's rings, which RN-0 put on the frame and which belong on the
backdrop because they accumulate until `LOOK1` wipes the screen. `RECORDED_RINGS` caught it at the
second present, which is what it was recorded for. `DrawHyperspaceRing` takes two surfaces as a
result — the one the circles land on and the one the present ends — because the tunnel is the one
place in the game where those two answers differ. `CLEAR_THE_FRAME` was deleted rather than left
standing at true, and `EraseSun`, `EraseBall`, `ErasePlanetOrSun` and `EraseSunRow` lost their
`Picture*` parameter, because a parameter deliberately ignored is a worse lie than its absence.**

**Gate.** `ThePictureIsAsRecorded`, `RECORDED_DOCKED_PICTURES`, `RECORDED_RINGS` unmoved — the
slice's whole claim is that a correct erase and a correct clear produce the same frame;
`TheReplayIsTheSameWithNoTwins` green (dropping every frame-side erase twin is precisely what T1
exists to catch); `check_twins.py` green with its fourth table.

**Journal.** Whether any checkpoint moved at step 2 and which routine had drawn a difference nobody
had noticed (RN-a); the sun twin's line count.

### 2.5 RN-2 — ADR-008 amended (0.5 sitting)

T3 deleted from ADR-008 §2 with the sentence saying why; §1's byte count corrected to two pictures;
the Status table's T3 row marked deleted at RN-1 with the date. Platform.md §5's three rendering
rows ✅. `check_docs.py`, `check_counts.py`. No code.

### 2.6 I-2 — `InputFrame`, event accumulation, the layered map (library and executable; 2 sittings, three commits)

**Goal.** One struct per step carries the keys; a tap between steps is never lost; the map gains
scan codes and a `{Flight, Docked}` layer set; `NextKey` and `Flush` leave the port when the
fixtures no longer need them. Platform.md §3.3; ADR-005 §4 as amended.

**Reads.** `Controls.h` (`Keyboard`, `ScanKeyboard`, `ReadKey`, the key constants), `Game.h/.cpp`
(`Step`, `StepDocked`, `m_universe.keys[0] = _key`), `Window.h/.cpp` (`PressKey`, `TakePressed`,
`m_held`), `KeyMap.h/.cpp` (`BINDINGS`, `CURSORS`), `Main.cpp`, `Shell.cpp` (`Held`),
`FlightReplayTests.cpp` (`_port.held[...]`, `game.Step(0u)`), `FlightPort.h` (`Held`, `held`),
`ControlsTests.cpp` (I-6's six tests, especially the chart case and the `ReadKey` script),
`ShellTests.cpp` (`TheKeyMap`), `DockedSessionTests.cpp` (`ScriptedKeys`).

**Commit 1 — the signature, same keys, digests unmoved.**
```cpp
namespace Elite   // Controls.h
{
  struct InputFrame
  {
    std::array<std::uint8_t, 65> held{};   ///< level over the matrix positions, what RDKEY reads
    std::uint8_t pressed = 0;              ///< thiskey for TT102, or 0
  };
}
[[nodiscard]] bool Game::Step(const InputFrame& _input) noexcept;      // _input.pressed where _key was
[[nodiscard]] std::uint8_t Game::StepDocked(const InputFrame& _input) noexcept;
```
`Game` copies the frame into a member `m_input` at the top of each `Step`; `ScanKeyboard` keeps
reading `Keyboard::Held` this commit (the port answers from the same table the frame was built
from, so nothing changes). `Window` gains `InputFrame Sample() noexcept` — the held table copied
and `TakePressed()` — and `Main.cpp` passes `window.Sample()`; `FlightPort` builds its frame from
`held` with `pressed = 0` exactly where it called `game.Step(0u)`. **All three digests unmoved**, or
stop.

**Commit 2 — the meaning.** Three changes, each with its instrument:
- **Accumulation.** `Window::PressKey` on a genuine down also sets `m_since[key] = true`; `Sample()`
  ORs `m_since` into the frame's `held` and clears it. The frame's `held` is what `Keyboard::Held`
  answers from this commit on (the shell keeps the last sampled frame), so `ScanKeyboard` sees the
  latched tap. Instrument: a `ShellTests` case over `Window` cannot exist (Win32); the rule is
  tested at the seam — `ControlsTests` gains `ATapLatchedIntoTheFrameIsScanned`, which drives a
  frame with a key latched and not held through `ScanKeyboard`. The replay's keys span steps, so no
  digest moves.
- **Scan codes and layers.** `KeyBinding` gains `std::uint16_t scanCode` (make code with the
  extended bit folded into bit 8) and `std::uint8_t layers` (a bit set; `LAYER_FLIGHT`,
  `LAYER_DOCKED`); `BINDINGS`' seven flight rows and the four cursor rows bind by scan code, every
  other row by virtual key; the four steering rows carry `LAYER_FLIGHT` only, everything else both.
  `Window::PressKey` receives the scan code from `lParam` bits 16–24 and the current layer from the
  shell (`Game::ModeNow()`: `Docked` → `LAYER_DOCKED`); `C64KeyFor(vk, scan, layer)`.
  **Retire the chart rule**: delete the four-position drop from `Elite::ScanKeyboard` and its
  comment. I-6's `ControlsTests` chart case pins the old answer and will fail — that failure is the
  point; rewrite the case to assert the new one (the steering positions are not in the frame on a
  docked layer, so the scan sees none) and journal that the rule moved into data. `ShellTests::
  TheKeyMap` gains `EveryPositionAModeNamesHasABindingInItsLayer`: every constant in `Controls.h`
  and `DockedKeys.h` the library dispatches on for that mode is bound in that layer.
- **Level against edge, measured.** With the frame in hand, `Game::Step` and `StepDocked` may
  derive `thiskey` from `held` the way `RDKEY` does (`ScanKeyboard`'s answer) instead of taking
  `pressed`. Try it in a separate commit inside this one: run `DockedSessionTests`, `ControlsTests`,
  the three digests and the two picture records. If all hold, keep level and delete `pressed`; if a
  docked screen is reached twice or a digest moves, keep the port's edge and journal which screen
  decided it. **This is a measurement the plan does not pre-empt.**

**Commit 3 — the port shrinks, if the fixtures allow.** `Keyboard::NextKey` and `Flush` leave the
port once every fixture that scripted characters scripts frames instead: `DockedSessionTests`'
`ScriptedKeys`, `SaveGameTests`, `LaunchTests`, `NullSeams`, `FlightPort`. A scripted frame source
advances between `ReadKey`'s scans the way I-6's `ReadKey` test does — through the presenter
`FlightPort` offers to be watched — and `ReadKey` reads the frame's `held`. Delete the three
`FLKB` call sites (`SystemScreen.cpp:143`, `NameEntry.cpp:49`, `MarketScreen.cpp:124` — recount)
with the method. **If this rewrite is more than one sitting, stop after commit 2, leave `NextKey`
and `Flush` on the port as I-1 did, and journal it as I-2c for the next branch**; I-4 needs them gone
and will take it otherwise.

**Gate.** Commit 1: digests. Commit 2: I-6 as rewritten, `DockedSessionTests`, the digests, both
picture records, `ShellTests`' layer completeness. Commit 3: `DockedSessionTests` green with no
escape-hatch keys (`overran` false on every screen).

**Ratchets and numbers.** `outpost-elite-names` may fall (the queue's names go) — `--update`;
`effects-seams` unchanged until commit 3 (the port stays a port, smaller). `tests` markers.

### 2.7 I-4 — coroutines and `Platform` (library and executable; 4–5 sittings, in groups)

**Goal.** No library routine presents, waits or pumps. `Task`, four awaitables, `ReadKey` as a task;
the thirty-six routines converted in groups; `Run()` a function over `Game` and an abstract
`Platform`; `GameShell` and `FlightSession` folded into the concrete one; `Abandon` and
`ExitProcess` gone; a `GameLoopTests` case drives the loop on the Linux leg. Platform.md §3.5, §3.1.

**Precondition.** `python tools/check_modernize.py --list` shows `opcode-transcriptions 0`
(M6-d complete). I-2 merged, ideally through commit 3.

**Reads.** Platform.md §2.2 (the thirty-six, by name), §3.5; `Frame.h` (RN-1's wrappers, which
become the awaitables); `Game.h/.cpp` whole; every file §2.2 names; `Ports.h`; `NullSeams.h`,
`FlightPort.h`, `DockedSessionTests.cpp` (the fixtures that answer `Presenter`); `Main.cpp`,
`Shell.*`, `FlightSession.*`.

**The types**, in a new `GameLogic/Task.h` (name checked against the SDK — there is no `Task.h` in
the Windows SDK or the STL; `ppltasks.h` is the nearest and does not collide):

```cpp
namespace Elite
{
  /// What a suspended task is waiting for; Game::Advance satisfies it from the budget.
  struct Wait { enum class Kind : std::uint8_t { None, Blanks, TitleTurn, FlightFrame } kind; std::uint8_t count; };

  class TaskArena;   // fixed storage for coroutine frames, owned by Game; sized in the slice (below)

  class Task         // the promise allocates from the arena named in Ports; final_suspend never; destruction cancels
  {
  public:
    struct promise_type;
    [[nodiscard]] bool Done() const noexcept;
    void Resume();                         // once; the promise records the next Wait
    [[nodiscard]] Wait Pending() const noexcept;
    ~Task();                               // destroys the frame: a closed window destroys the screen mid-wait, and every destructor runs
  };

  // The awaitables, replacing Frame.h's wrappers: each records its Wait in the promise and suspends;
  // on resumption the frame has been cleared (RN-1's boundary lives here).
  [[nodiscard]] auto WaitBlanks(std::uint8_t _blanks) noexcept;
  [[nodiscard]] auto PresentFrame() noexcept;               // WaitBlanks(1)
  [[nodiscard]] auto HoldTitleTurn(std::uint8_t _distance) noexcept;
  [[nodiscard]] auto HoldFlightStep(std::uint8_t _ships) noexcept;

  /// TT217 as a task: co_await WaitBlanks(2); scan; loop while any key; co_await PresentFrame() per scan.
  [[nodiscard]] Task ReadKey(Universe& _universe, Ports& _ports) noexcept;   // answers through _universe.lastKey or a promise value
}
```

**The arena and the ratchet, decided here.** The promise's `operator new` takes its storage from
the `TaskArena&` it finds in the coroutine's `Ports&` argument (every converted routine takes
`Universe&, Ports&`; the promise's allocating `operator new(std::size_t, Universe&, Ports&, ...)`
overload sees them). So `Ports` gains a reference, `aggregate-refs` goes 8 → 9, and rule 5 says a
ceiling only goes down. **Raise it in the same commit and journal it** — the S-1 precedent: a
reference that is platform composition and not game state — rather than thread a thirty-seventh
parameter. Size the arena from the deepest chain §2.2 found (a briefing inside a docking inside a
step) plus headroom, measured with the compiler: instrument `operator new` to record the high-water
mark through `GameLoopTests` and `DockedSessionTests`, set the arena to twice it, and make
exhaustion an `ASSERT` that names the chain. `check_gamelogic.py` bans the identifiers `clock` and
`time`: name nothing in `Task.h` with either word.

**The port that goes.** `Elite::Presenter` and its four methods are deleted; `Ports::present`
goes; `effects-seams` 5 → 4 (or 3 if `Keyboard` folded at I-2 commit 3 — it did not; `Held` stays).
Fixtures that watched the presenter (`FlightPort::watching`, the docked `Session`'s counters) watch
`Game::OnWait(callback)` instead — a hook `Advance` fires with the `Wait` it is about to satisfy,
which is where the replay's `deathFrames` count and the hyperspace ring checkpoints come from now.

**`Game::Advance`**, the budget consumer (ADR-007 §2's three routines survive inside it):

```cpp
struct Budget { int flightSteps; int dockedPasses; int blanks; };   // from the Scheduler, counted outside

void Game::Advance(const InputFrame& _input, Budget& _budget) noexcept
{
  while (m_task && !m_task->Done())            // a screen, a title, a tunnel, a death is running
  {
    if (!Satisfy(m_task->Pending(), _budget)) return;   // not enough budget yet: come back next turn
    m_universe.picture.Clear();                          // RN-1's boundary, now inside the await
    m_task->Resume();
  }
  m_task.reset();
  switch (ModeNow())                            // FRCE's choice, unchanged
  {
  case Mode::Flight: while (_budget.flightSteps-- > 0 && Step(_input)) {} break;   // Step may start a task (a death, a docking) and return false
  case Mode::Docked: while (_budget.dockedPasses-- > 0) { m_lastSyncs = StepDocked(_input); } break;
  }
}
```

`Satisfy` spends blanks for `Blanks`, one flight step's worth for `FlightFrame` (the scheduler
prices it from the ship count the task carries), one title turn's worth for `TitleTurn`.

**Conversion order, one commit per group, every digest and both picture records unmoved after each:**

| Group | Routines (from Platform.md §2.2) | Note |
|---|---|---|
| G1 the docked leaves | `ReadKey`; `ReadNumber`; `PrintCashLeft`, `Complain`, `ListCargo`, `BuyScreen`, `InventoryScreen`; `ChooseView`, `EquipShipScreen`; `ReportAndReturnToMenu`, `AskYesNo`, `DiskAccessMenu`; `NameEntry`'s wait | `Game::Perform`'s arms that call them become `m_task = ...` and return; `DockedSessionTests` drives `Advance` in a loop until no task is pending instead of calling `Perform` synchronously |
| G2 the missions and the text codes | `PauseForKey`, `WaitForKeyPress`, `ShowIncomingMessage`, `ShowBriefingShip`, `RunControlCode`, `RunConstrictorBriefing`, `OfferTrumble`, `BriefMission1`; `RunTextCode` and `PrintByte` in `ExtendedTokens.cpp`; `Game::MissionOf` | **The wide part.** A control code that pauses is reached from inside the token printer, so the printer's print path returns a `Task` and every caller `co_await`s it. Convert the printer's chain first, with no wait inside it, and prove the transcript unchanged; then the codes |
| G3 the flight | `DrawTunnel`, `DrawHyperspaceTunnel`, `EnterWitchspace`, `EnterWitchspaceCheating`, `Die`, `ShowTitleShip`, `StartGame`, `ResetAndStartGame`; `Game::Reset`, `Leave`, `PressKey`, `Step`, `StepDocked` | `Reset` becomes a task the composition root advances until docked; `Leave(Died)` starts `Die` as a task; ADR-007 §2's amended clause is this commit's evidence (`TheDeathIsAsRecorded`, `deathFrames` from the hook) |
| G4 the loop | `Outpost/Loop.h/.cpp` (portable: `Run(Platform&, Game&)` over an abstract `Platform` with `WaitForFrame`, `Pump`, `Sample`, `Active`, `Closed`, `Present`, `Publish`; in `EXECUTABLE_SOURCES`); `Outpost/Platform.h/.cpp` (Win32: the window, the chain, the device, the key table, `SyncVideoRegisters`; absorbs `GameShell` and `FlightSession`, both deleted); `Main.cpp` = create, `Run`, `Guarded` | `Abandon` and `ExitProcess` go; the presenter's `Destroy` is called by `~Platform` |
| G5 the test | `Tests/GameLogicTests/GameLoopTests.cpp`: a scripted `Platform` (frames from a list, a budget per turn, a close after N turns) drives `Run` through the cold start, a docked session across three screens, a launch, ten flight turns and a close; asserts the mode sequence and that every destructor ran (a counter on the scripted platform) | Linux leg |

**Gate.** After every group: the three digests, `RECORDED_PICTURE`, `RECORDED_DOCKED_PICTURES`,
`RECORDED_RINGS`, `TheReplayIsTheSameWithNoTwins`, `check_twins.py`, `check_outpost.py`; after G4
the Windows job. `DockedSessionTests::TheScreensDoNotPrintTheSameThingAsEachOther` is the
instrument that catches a screen that stopped waiting.

**Ratchets and numbers.** `aggregate-refs` 8 → 9 (raised, journaled); `effects-seams` 5 → 4;
`main-lines` falls to the two hundred ADR-004 §1 described — `--update`; `outpost-elite-names`
falls; `tests` +1; Platform.md's three markers.

**Journal.** The arena's measured high-water mark and its size; the number of signatures actually
changed against the thirty-six estimated; what G2 cost against the estimate; ADR-007 §2 and
Modernize.md's M4-d row pointed at the commit.

### 2.8 T-4 — the sound interrupt under the scheduler, the chip on its thread (1–2 sittings)

**Goal.** `RunSoundInterrupt` runs once per blank the budget delivers, on the game thread, into a
ring of `SidWriteLog`s; the XAudio2 callback thread renders the ring. ADR-005 §2 as reversed.

**Reads.** `SoundOutput.h/.cpp` (`Pump`, `RunFrame`, `QueuedBuffers`, the ring), `Music.h`
(`RunSoundInterrupt`), `Game.h` (`Sounds`, `ClearSounds`), `FlightReplayTests.cpp`,
`SidRenderTests.cpp`.

**Steps, as three commits.**
1. **The interrupt moves.** `Game::Advance` runs `RunSoundInterrupt(m_universe.sound,
   m_universe.music, log)` once per blank the budget delivered, appending each log to a
   `std::vector<SidWriteLog> m_soundFrames` the platform takes with `TakeSoundFrames()`; the game's
   own writes (`Sounds()`) are prepended to the first log of the turn. `SoundOutput::Pump` becomes
   `Publish(frames)`: it pushes each log into a lock-free ring (one `std::atomic<std::size_t>` write
   index, one read index; `<atomic>` is `Outpost/`'s and allowed). **Guarded by a count**: the
   replay driver constructs its budget with `blanks = 0` on this commit, so no interrupt runs in a
   test and every digest is unmoved. `SoundOutput` without a device still consumes the ring, so the
   counters advance exactly as many times as blanks were delivered — the "without a device" clause
   of `SoundOutput.h` is honoured by the scheduler now rather than by the pump.
2. **The record, widened.** The replay driver delivers blanks at the scheduler's rate for a flight
   step (`FlightFrameCycles(ships) / cyclesPerFrame`, integer, with the remainder carried) so the
   interrupt runs in the replay as it does in the app. Every digest moves. **Take the zero-delivery
   column first**: the same run with `blanks = 0` must reproduce the old three records to the bit
   (assert it in a test that stays: `TheReplayWithNoSoundInterruptIsTheOldRecord` — keep the old
   tables under a new name). Then re-record the three under ADR-007 §4's first case, journaled.
   `ThePictureIsAsRecorded` must not move: the interrupt draws nothing; if it moves, stop.
3. **The thread.** `SoundOutput` implements `IXAudio2VoiceCallback`; `OnBufferEnd` renders the next
   log from the ring (or, ring empty, renders `FRAME_CYCLES` of chip with no writes — the
   registers hold) and submits it; `Pump`'s render loop and `QueuedBuffers` go. Two frames are
   primed at start-up so the first callback has work.

**Gate.** `SidRenderTests` unchanged (the synthesiser did not move); the zero-delivery test and the
re-taken records; the Windows job; the owner's: drag the title bar for five seconds, no stutter.

**Journal.** ADR-005 §2 pointed at the commit; the number of blanks per step the driver delivers;
that `Pump` is gone.

### 2.9 C-1 — cleanup, inside the slices that own each item

| Item | Evidence on `32a5faa` | Slice |
|---|---|---|
| `GameShell::ClearToView` | no caller outside `Shell.*` (`grep -rn ClearToView Outpost GameLogic Tests`) | delete in T-1 commit 2, since `Shell.cpp` is open there |
| `ScreenPresenter::Ready` | no caller | T-3 commit 1 |
| the four `double` accumulators and `PlanSteps` | §2.1 | T-1 |
| `Keyboard::Flush`, three `FLKB` sites, `Keyboard::NextKey` | §2.6 | I-2 commit 3, or I-4 G1 |
| `FlightSession` | three methods over a pointer; absorbed by `Platform` | I-4 G4 |
| `Elite::Presenter` and `NullSeams`' four overrides | replaced by the awaitables | I-4 G1 |
| `SoundOutput::Pump`, `QueuedBuffers` | replaced by `Publish` and the callback | T-4 |
| the `2x` files | **not cleanup** (Platform.md §0); their suffix goes at RN-6 | RN-6 |

### 2.10 RN-6 — one surface (library; 6–8 sittings; ruled, last)

**Goal.** The canvas stops being drawn; `picture`/`backdrop` are the only surfaces; the twins become
the routines' own stores and lose their suffix; the twin rule, `check_twins.py` and
`DashboardImage.cpp` go; the picture tests become goldens. The decisions do not move. ADR-001 §1,
ADR-002 §4, ADR-003 §3, ADR-007 §4, ADR-008 as amended.

**Preconditions.** RN-2, I-4 and T-4 merged. Every record green.

**The proof column first, and it gates everything.** Commit 1 adds
`HashStateWithoutSurfaces(const Universe&)` — `HashState` with `canvas` skipped — and a replay test
`TheDecisionsAreAsRecorded` that records it at every checkpoint of the three flights as
`RECORDED_DECISIONS`, `RECORDED_DEATH_DECISIONS`, `RECORDED_ESCAPE_DECISIONS`, plus the same for the
docked session. **Those four tables must not move through any later commit of this slice**; the
canvas-inclusive records may, once, at the end, under the narrowing case, with these as the column
that proves the flight did not.

**What reads the canvas that is not a pixel** — measured on `32a5faa` with
`grep -rn "canvas\.Read\|_canvas\.Read\|ReadCell\|SIGHT_SPRITE_CELL" GameLogic/*.cpp`: one
read-modify-write in `ViewChange.cpp:77` (a wipe pattern; presentation), the sprite pointers `SIGHT`
writes into the canvas at `SIGHT_SPRITE_CELL` and `SIGHT_SPRITE_CELL_2` (read by `Picture::Resolve`),
and the six raster flags `SyncVideoRegisters` sets on the canvas (read by `Resolve`). Recount; a
read that decides something is a stop and a report, not a workaround (Platform.md §8 P-d).

**Steps, as commits.** (2) Move the raster flags and the sprite pointers off the canvas: the flags
to `ScreenState` (they are `COMIRQ1`'s outputs and `TickRasterInterrupt` already computes them
there), the sprite pointers to `VideoState` (two bytes each for two blocks; `SIGHT` writes them
there; `Resolve` reads them there); `RECORDED_DECISIONS` unmoved. (3) `Picture::Resolve` and
`Hash` drop their `Canvas&` parameter; `ScreenPresenter::Present` too. (4) File by file, the
canvas store beside each twin goes and the twin is renamed without its suffix into the faithful
file (`Lines2x.cpp` into `Lines.cpp`, and so on; `DashboardPicture2x.cpp` stays as the art table,
renamed `DashboardPicture.cpp`); the `/// 2x of:` markers go; after each file `RECORDED_DECISIONS`
and both picture records unmoved. (5) `Canvas` loses its bitmap and cell planes and keeps nothing;
delete it; `Universe::canvas` goes; `HashState` folds no surface; `StateCells` loses the canvas
cells; `UniverseImage`'s comment. (6) `check_twins.py` leaves `check_all.py` (`checks` 13 → 12 —
fix the marker in README.md, Resolution.md, AGENTS.md); `mutants.json`: `Canvas.cpp`'s mutant goes
and `mutant-files` 4 → 3, journaled as the floor shrinking under a ruling; `DashboardImage.cpp`
goes. (7) The five `Picture*Tests.cpp` files: every assertion phrased "the canvas has X so the
picture has X" becomes a golden of the picture (a recorded hash per scene); the property sweeps
(bars over every value, the line over 18,432 lines) stay as they are. (8) **The narrowing.**
`RECORDED`, `RECORDED_DEATH`, `RECORDED_ESCAPE` re-taken once, with the journal entry naming
ADR-007 §4's third case and the four decision tables as the column; delete `HashStateWithoutSurfaces`
and make `HashState` that function (there is nothing left to skip).

**Gate.** The four decision tables unmoved at every commit; `ThePictureIsAsRecorded`,
`RECORDED_DOCKED_PICTURES`, `RECORDED_RINGS` unmoved throughout; the property sweeps green;
`check_all.py` with twelve checks; the Windows job.

**Journal.** The count of stores removed and twins merged; the byte count `Universe` fell by; which
of the reads in P-d turned out to decide something (expect none); ADR-008's Status table pointed at
the commit and the ADR marked superseded by ADR-010.

### 2.11 P-0 — ADR-010 (1 sitting)

`Design/ADR/ADR-010-the-platform.md` in the shape of ADR-007 and ADR-008: written from what was
built, not from Platform.md — the loop, the clocks, the frame, the input, the sound, the surface,
with the numbers the slices measured (the arena's high-water mark, the latency numbers if the owner
took them, the byte count). Supersedes ADR-005 (whose status line says so) and ADR-008 §1, §2 and
§3. README.md's decisions table gains its row; Platform.md's status says built and its §5 is all ✅;
ADR-009 §2's table names the picture record as the pixels' instrument. `check_docs.py`,
`check_counts.py`.

---

## 3. What is deliberately not in this plan

- **The fixed-rate flight model** (Platform.md §12 D3). Its own design and ADR after RN-6. Nothing
  here builds toward it except the scheduler's step cost being one number.
- **A display list, per-primitive colour, interpolation** (ADR-002 §1 as amended). After RN-6.
- **Gamepad and remapping** (I-5). Its own ADR.
- **Borderless fullscreen, the GPU-side decode, `WM_ENTERSIZEMOVE`** (Platform.md §9). Not
  scheduled.

---

## 4. The report a slice hands back

In this order, and nothing implied:

1. The slice and the commits, by hash and title.
2. The four instruments' output, quoted: `N passed, 0 failed`, `all 13 repository checks pass`,
   the selftests line, and the Windows job's URL and conclusion.
3. Every record that moved, which case it moved under, and the journal entry that says so — or
   "no record moved".
4. Every ceiling that moved and the direction.
5. What was measured against what the plan estimated (sites, signatures, bytes, lines).
6. What is the owner's to do (a play check, a PresentMon number, a screenshot) and is not done.
7. What the next slice needs that this one did not deliver.
