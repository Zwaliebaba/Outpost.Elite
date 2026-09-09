#include "pch.h"

#include "Shell.h"

#include "Game.h"

#include "FlightSession.h"
#include "Presentation.h"
#include "SoundOutput.h"

#include "Music.h"
#include "SoundEffects.h"

#include "Flight.h"
#include "KeyMap.h"

namespace Outpost
{

  bool GameShell::Turn()
  {
    /*
     * THE TURN BEGINS BY WAITING FOR THE DISPLAY, and this is the whole of slice T-3
     * (Design/Platform.md §3.7).
     *
     * The wait used to be at the BOTTOM, inside `Present(1, 0)`, and the difference is not where a
     * thread sleeps but how old the keys are when the picture made from them appears. Presenting
     * into a queue the driver holds one frame deep and then sampling the keyboard means the sample
     * is a frame older than the pixels it produces; waiting on the chain's own latency object first
     * and sampling immediately after puts the two inside the same refresh. Everything below --
     * the pump that fills the key table, the scheduler's steps, the resolve -- is then as late as
     * it can be and still make this frame, which is what "sample late, present early" means.
     *
     * It is safe before there is a swap chain and after there is not: `WaitForFrame` returns at
     * once when the handle is null, so the start-up turns and the shutdown ones simply do not wait.
     */
    m_presenter.WaitForFrame();

    /*
     * THE CLOCK IS READ HERE, ONCE, AND NOWHERE ELSE IN THE PROGRAM (Design/Platform.md §3.1).
     *
     * A turn is a turn however the game got to it -- the outer loop, a `DELAY`, a tunnel's present,
     * the title ship's hold -- so this is the one place that sees all of them. What the scheduler
     * does with the time is its own (`Scheduler.h`): whole steps at what a step costs, blanks at
     * the machine's frame, and nothing at all while the window is inactive, which is the pause.
     */
    m_scheduler.Feed(m_clock.Tick(), m_window.Active());

    /*
     * And a dropped backlog is said out loud, here rather than in the outer loop (ADR-005 §3).
     *
     * ADR-005 §3 has asked for a stall to be logged since 2026-09-02 and `Main.cpp` read the flag
     * and discarded it, with the honest note that a windowed build had nowhere to report one to.
     * It is reported HERE because this is where the clock is read: a stall inside a docked `DELAY`
     * or a title screen's hold happens at a call depth the outer loop does not see for another
     * turn, and a diagnostic a turn late is a diagnostic about the wrong frame.
     */
    if (const std::uint32_t stalls = m_scheduler.TakeStalls(); stalls != 0u)
    {
      m_stallsSeen += stalls;
      OutputDebugStringA("Elite: the scheduler dropped a backlog it could not run\n");
#if defined(_DEBUG)
      m_window.ShowStallCount(m_stallsSeen); // where somebody PLAYING can see it, not only a debugger
#endif
    }

    /*
     * The raster interrupt, which runs whether or not the game is doing anything.
     *
     * It is HERE and not in the outer loop because `WaitFrames` and `NextKey` present too, and both
     * of those are reached from inside ported routines. A frame is a frame however the game got to
     * it, and putting the handler anywhere else would leave the screen mode stale for exactly the
     * frames a player is looking hardest at -- a `DELAY` and a "press any key".
     */
    if (m_flight != nullptr)
    {
      m_flight->SyncVideoRegisters();
    }

    /*
     * COMIRQ1's SID half, as many times as the audio device is short a frame.
     *
     * Before the present rather than after, because the present is what blocks: the frames rendered
     * here are what the device plays while this thread waits on the display, and a queue filled
     * afterwards would be a frame later than it needs to be.
     */
    if (m_audio != nullptr && m_sound != nullptr && m_music != nullptr && m_game != nullptr)
    {
      m_audio->Pump(*m_sound, *m_music, m_game->Sounds());
      m_game->ClearSounds();
    }

    if (!m_window.Pump())
    {
      return false;
    }

    int width = 0;
    int height = 0;
    m_window.ClientSize(width, height);

    if (m_window.TakeResize())
    {
      m_presenter.Resize(width, height);
    }

    /*
     * A minimised window has nothing to present to, and `Present` returns immediately -- so without
     * this the loop would spin a core at whatever rate the pump manages. `WaitMessage` blocks until
     * something arrives, which is the correct idle and is what makes a minimised game cost nothing.
     */
    if (width <= 0 || height <= 0)
    {
      WaitMessage();
      return !m_window.Closed();
    }

    /*
     * Three answers, and only one of them is "carry on" (`ScreenPresenter::PresentResult`).
     *
     * `DXGI_STATUS_OCCLUDED` is a SUCCESS code, so while this returned a `bool` a window hidden
     * behind another one -- or sitting on a virtual desktop nobody is looking at -- presented
     * nothing, returned true, and went round again at whatever rate the message pump managed. That
     * is a core burnt on a picture no one can see. `WaitWhileOccluded` idles instead, and wakes on
     * the first input or uncovering rather than on a timer alone, so nothing is missed by it.
     */
    switch (m_presenter.Present(*m_picture, *m_canvas, m_video, width, height))
    {
    case ScreenPresenter::PresentResult::Lost:
      return false;

    case ScreenPresenter::PresentResult::Occluded:
      m_presenter.WaitWhileOccluded();
      return !m_window.Closed();

    case ScreenPresenter::PresentResult::Presented:
      break;
    }

    return !m_window.Closed();
  }

  std::uint8_t GameShell::NextKey()
  {
    /*
     * TT217 -- and it is `Elite::ReadKey`, the routine, over this object's `Held` and its
     * presenter (InputTimer.md I-1). The queue this popped until I-1 delivered auto-repeats and
     * type-ahead to prompts the original answered from a matrix it had first watched go quiet;
     * the library's read has the two-frame debounce, the wait for release and the wait for a
     * press, and it presents once a scan, which is what pumps the window and fills the table
     * `Held` answers from. The close-on-window-gone rule is unchanged: `WaitFrames` and `Present`
     * call `Abandon` when the window has gone, and this is reached only through them.
     */
    return Elite::ReadKey(m_flight->Universe(), *m_ports);
  }

  void GameShell::Abandon()
  {
    /*
     * The window is gone and the game is somewhere inside a ported routine with no way to be told.
     * Unwinding is not available -- most of `GameLogic` is `noexcept` -- and returning a character
     * would put the caller into a loop that never ends, so the process stops here.
     *
     * THE GRAPHICS ARE RELEASED BY HAND FIRST, because ending the process here means no destructor
     * anywhere runs and `~ScreenPresenter` is one of them. Memory does not care -- the OS takes it
     * back either way -- but the Direct3D debug layer reports what is still live when the process
     * dies, so closing the window used to print forty live D3D12 objects and three DXGI ones. None
     * of them was a leak; they were all still owned, by an object that never got to let go.
     *
     * This is the ONLY exit a docked game normally takes. Every screen ends blocked in `TT217`, so
     * the player clicking the X arrives here rather than at `Run`'s loop condition.
     */
    m_presenter.Destroy();

    ExitProcess(0); // does not return; the declaration is [[noreturn]] for that reason
  }

  // ---- waiting and the keyboard ------------------------------------------------------------------

  void GameShell::WaitFrames(std::uint8_t _frames)
  {
    /*
     * Wait for `_frames` of the MACHINE's vertical syncs (Design/Platform.md T-1).
     *
     * IT COUNTED PRESENTS UNTIL 2026-09-09 and that was the defect InputTimer.md T-1 named: a turn
     * ends in `Present(1, 0)`, so a turn was one refresh of the PLAYER's monitor, and every
     * `DELAY` in the docked game -- `TT217`'s two-frame debounce, `dn2`'s fifty, `DKS3`'s twenty,
     * the equipment beeps -- lasted 0.83 seconds on a 60 Hz panel, 0.35 on a 144 Hz one and 0.30
     * on a 165 Hz one. The comment that stood here called that "the PAL and NTSC difference §6.17
     * records rather than a defect in this loop", which was true of the monitor it was written on
     * and is not a property of the design.
     *
     * What it counts now is simulated blanks, which the scheduler derives from elapsed cycles at
     * the machine's own frame -- so fifty of them are five sixths of a second on every panel there
     * is, and a turn that delivered none simply goes round again.
     */
    int owed = static_cast<int>(_frames);
    while (owed > 0)
    {
      if (!Turn())
      {
        Abandon();
      }
      owed -= m_scheduler.TakeBlanks();
    }
  }

  void GameShell::Flush()
  {
    // A load, a register transfer and a return on this build, a flush of nothing;
    // the window has had no queue to empty since InputTimer.md I-1, so the answer is the
    // original's.
  }

  bool GameShell::Held(std::size_t _key)
  {
    // The matrix walk's read of one row. Everything around it -- the `SETL1` bracket, the
    // sprite mask, `ZEKTRAN`'s clear and the countdown that produces `thiskey` -- is
    // `Elite::ScanKeyboard`'s since M3-b-3d.
    return m_window.Held(static_cast<std::uint8_t>(_key));
  }

  // ---- the start sequence -------------------------------------------------------------------------

  /*
   * `StartTheme` AND `StopTheme` WERE HERE AND ARE NOT ANY MORE (M3-b-2b).
   *
   * `startat` and `stopat` are `Elite::StartTheme` and `Elite::StopMusic` over `Universe::music`,
   * and the start sequence calls them itself through `Ports::sid`. What this object was adding was
   * the null checks, and the checks were on members that are always bound by the composition root.
   */

  void GameShell::HoldFlightFrame(std::uint8_t _ships)
  {
    /*
     * Present the SAME picture until a flight frame is due, which is what the VIC-II was doing
     * while the 6510 computed the next one. `FlightFrameCycles` is the measured cost model
     * (§6.114), read fresh because it depends on how full the bubble is -- and during a death the
     * bubble empties as the wreckage flies past, so the rate is not a constant.
     */
    /*
     * ONE STEP'S WORTH OF THE SCHEDULER'S TIME, at what a frame costs with this many ships in the
     * bubble -- which is the accumulator this used to keep for itself (Design/Platform.md T-1).
     *
     * The backlog rule it had is the scheduler's now and is the same rule: a stall costs the
     * sequence a frame rather than running the rest of it at double speed (§6.110). A turn that
     * answers more than one step has caught up, and the extra is spent here rather than banked,
     * because a hold is one frame by definition.
     */
    const std::uint32_t cost = FlightFrameCycles(_ships);

    for (;;)
    {
      if (!Turn())
      {
        Abandon();
      }

      if (m_scheduler.TakeSteps(cost) > 0)
      {
        return;
      }
    }
  }

  void GameShell::Present()
  {
    /*
     * One circle of a launch or hyperspace tunnel has been drawn; show it and let a frame pass.
     *
     * This is `DELAY` with a count of one, and it is the same thing for the same reason: `Turn`
     * ends in a present, so a turn is a vertical sync. What is being restored here is not a wait
     * the 6502 performed -- it performed none -- but the DISPLAY the 6502 had, which showed each
     * circle for the 14,232 cycles the next one took to compute (§6.109).
     */
    WaitFrames(1u);
  }

  void GameShell::HoldTitleFrame(std::uint8_t _distance)
  {
    /*
     * `LL9` has just drawn the ship into the canvas and nothing else stands between this frame and
     * the next, so the turn belongs here: the C64 had a VIC-II showing the bitmap continuously and
     * this does not. It is also what fills the table `Held` reads, because that is the message
     * pump `Turn` runs -- so a scan without one of these would see a keyboard nobody had polled.
     */
    /*
     * AND THE WAIT, WHICH IS THE POINT. `TITLE` runs `MVEIT` and `LL9` and comes straight back
     * round -- there is no wait for vertical sync anywhere in it (§6.17) -- so the ship turns at
     * whatever rate a 6510 gets through those two, which `CycleTests` measures at 121,276
     * cycles: 8.43 turns a second. Presenting once per turn made the display decide instead, and
     * on a 165 Hz panel the ship span twenty times too fast (§6.110).
     *
     * SO THIS PRESENTS UNTIL A TURN IS DUE, and the frames in between are the same picture -- which
     * is exactly what the VIC-II was doing while the 6510 computed the next one. The accumulator is
     * the flight loop's arrangement with one difference: the period is not a constant, because what
     * a turn costs depends on how much of the ship there is to draw (`TitleTurnCycles`).
     */
    /*
     * The rate changes as the sequence runs: the ship is a dot when it starts and a wireframe
     * across the middle of the screen when it arrives, and those cost 15,600 and 121,276 cycles.
     * `_distance` is `INWK+7`, the byte `TLL2` walks down, and the library passes it (M3-b-3d).
     */
    const std::uint32_t cost = TitleTurnCycles(_distance);

    for (;;)
    {
      if (!Turn())
      {
        Abandon();
      }

      if (m_scheduler.TakeSteps(cost) > 0)
      {
        break;
      }
    }
  }

  /*
   * `ShowTitleScreen` WAS HERE AND IS NOT ANY MORE (M6-0-h-2). It was a forward to
   * `Elite::ShowTitleShip` -- `TITLE`, ported in full since §6.107 -- and `BR1` makes the
   * call itself, which is what the original's own call to `TITLE` is.
   */

  // ---- the control codes that leave the text system ------------------------------------------------

} // namespace Outpost
