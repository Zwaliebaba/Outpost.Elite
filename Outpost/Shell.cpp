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

#include <chrono>

namespace Outpost
{

  bool GameShell::Turn()
  {
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

    return m_presenter.Present(*m_picture, *m_canvas, m_video, width, height);
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

  // ---- the screen -------------------------------------------------------------------------------

  void GameShell::ClearToView(std::uint8_t _view)
  {
    /*
     * The whole routine, since slice 3d-d-iii-a.
     *
     * This was three calls and an apology for as long as the dashboard, the sprites, the border and
     * the colour bands were phase 3's (§6.77): a palette fill, a text-area clear and `SetUpTextScreen`
     * for the text state. All four exist now, `SetUpScreen` is compared against the shipped `TT66`
     * on the whole canvas over six views including text ones, and §6.81 says in as many words that
     * the 2e version was correct only because the half it left out was the half that observes the
     * intermediate `QQ17`. So the approximation goes and the routine runs.
     *
     * IT FIXES A LEAK THE FLIGHT HALF WOULD OTHERWISE HAVE OPENED. `wantdials` points `abraxas` at
     * the dashboard's block of screen RAM and puts the lower rows into multicolour; `TTX66K`'s text
     * path puts both back. A docked screen reached through the old three calls after a launch would
     * have kept the flight settings and drawn its bottom seven rows as multicolour nonsense.
     */
    if (m_flight == nullptr || m_ports == nullptr)
    {
      *m_view = _view; // 6502: the view byte alone, all that can be done without the universe
      return;
    }

    Elite::SetUpScreen(m_flight->Universe(), *m_ports, _view);
  }

  // ---- waiting and the keyboard ------------------------------------------------------------------

  void GameShell::WaitFrames(std::uint8_t _frames)
  {
    /*
     * Wait for _frames VERTICAL SYNCS, and that is literally what this is: `Turn`
     * ends in `Present(1, 0)`, so a turn is a frame. No timer, no sleep, and the wait is the same
     * length as the original's on a 50 Hz display and shorter on a 60 Hz one -- which is the PAL
     * and NTSC difference section 6.17 records rather than a defect in this loop.
     */
    for (std::uint8_t frame = 0; frame < _frames; ++frame)
    {
      if (!Turn())
      {
        Abandon();
      }
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
     * while the 6510 computed the next one. `FlightFrameSeconds` is the measured cost model
     * (§6.114), read fresh because it depends on how full the bubble is -- and during a death the
     * bubble empties as the wreckage flies past, so the rate is not a constant.
     */
    const double period = FlightFrameSeconds(_ships);

    if (m_lastFlightFrame.time_since_epoch().count() == 0)
    {
      m_lastFlightFrame = std::chrono::steady_clock::now();
    }

    for (;;)
    {
      if (!Turn())
      {
        Abandon();
      }

      const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
      const double elapsed = std::chrono::duration<double>(now - m_lastFlightFrame).count();
      m_lastFlightFrame = now;

      m_flightFrameLeftover += elapsed;
      if (m_flightFrameLeftover >= period)
      {
        // One frame, and a long backlog is dropped rather than repaid -- a stall should cost the
        // sequence a frame, not run the rest of it at double speed (the title's rule, §6.110).
        m_flightFrameLeftover = (m_flightFrameLeftover >= 2.0 * period) ? 0.0 : (m_flightFrameLeftover - period);
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
     * a turn costs depends on how much of the ship there is to draw (`TitleTurnSeconds`).
     */
    for (;;)
    {
      if (!Turn())
      {
        Abandon();
      }

      const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
      const double elapsed = std::chrono::duration<double>(now - m_lastSpin).count();
      m_lastSpin = now;

      /*
       * The rate changes as the sequence runs: the ship is a dot when it starts and a wireframe
       * across the middle of the screen when it arrives, and those cost 15,600 and 121,276 cycles.
       * `_distance` is `INWK+7`, the byte `TLL2` walks down, and the library passes it (M3-b-3d).
       */
      const double period = TitleTurnSeconds(_distance);

      m_spinLeftover += elapsed;
      if (m_spinLeftover >= period)
      {
        // One turn, and the rest of the backlog is dropped rather than repaid: a stall should cost
        // the ship a turn, not spin it faster to catch up.
        m_spinLeftover = (m_spinLeftover >= 2.0 * period) ? 0.0 : (m_spinLeftover - period);
        break;
      }
    }
  }

  /*
   * `ShowTitleScreen` WAS HERE AND IS NOT ANY MORE (M6-0-h-2). It was a forward to
   * `Elite::ShowTitleShip` -- 6502: TITLE, ported in full since §6.107 -- and `BR1` makes the
   * call itself, which is what the original's own call to `TITLE` is.
   */

  // ---- the control codes that leave the text system ------------------------------------------------

} // namespace Outpost
