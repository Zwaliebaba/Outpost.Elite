#include "pch.h"

#include "Shell.h"

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

  namespace
  {
    /// 6502: dn2 -- JSR BEEP / LDY #50 / JMP DELAY.
    constexpr std::uint8_t BEEP_PAUSE_FRAMES = 50;

    /// 6502: BRIS -- LDA #216 / JSR DETOK / LDY #100 / JMP DELAY.
    constexpr std::uint8_t BRIEFING_TOKEN = 216;
    constexpr std::uint8_t BRIEFING_FRAMES = 100;

    /// 6502: MT23 and MT29 -- the two rows they move to before falling into MT13.
    constexpr std::uint8_t MT23_ROW = 10;
    constexpr std::uint8_t MT29_ROW = 6;

    /// 6502: MT8 -- LDA #6 / JSR DOXC.
    constexpr std::uint8_t MT8_COLUMN = 6;

    /// 6502: MT9 -- LDA #1 / JSR DOXC / JMP TT66, and STA does not touch A, so TT66 gets the 1 too.
    constexpr std::uint8_t MT9_VIEW = 1;

  } // namespace

  bool GameShell::Turn()
  {
    /*
     * 6502: comirq1 -- the raster interrupt, which runs whether or not the game is doing anything.
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
     * 6502: COMIRQ1's SID half, as many times as the audio device is short a frame.
     *
     * Before the present rather than after, because the present is what blocks: the frames rendered
     * here are what the device plays while this thread waits on the display, and a queue filled
     * afterwards would be a frame later than it needs to be.
     */
    if (m_audio != nullptr && m_sound != nullptr && m_music != nullptr)
    {
      m_audio->Pump(*m_sound, *m_music);
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

    return m_presenter.Present(m_canvas, width, height);
  }

  std::uint8_t GameShell::NextKey()
  {
    std::uint8_t key = 0;
    while (!m_window.TakeKey(key))
    {
      if (!Turn())
      {
        Abandon();
      }
    }

    // 6502: LDA TRANTABLE,X -- the CHARACTER, because that is what TT217 returns in A and what
    // every screen that calls it compares against. The dispatch reads the position instead, and
    // takes it from the window directly.
    return CharacterFor(key);
  }

  void GameShell::Abandon()
  {
    /*
     * The window is gone and the game is somewhere inside a ported routine with no way to be told.
     * Unwinding is not available -- most of `GameLogic` is `noexcept` -- and returning a character
     * would put the caller into a loop that never ends, so the process stops here.
     *
     * THE GRAPHICS ARE RELEASED BY HAND FIRST, because ending the process here means no destructor
     * anywhere runs and `~CanvasPresenter` is one of them. Memory does not care -- the OS takes it
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

  void GameShell::SetUpTradeScreen(std::uint8_t _view)
  {
    // 6502: TRADEMODE -- TT66, then FLKB, then DOVDU19 (a palette change this build does not act on).
    ClearToView(_view);
    FlushKeyboard();
  }

  void GameShell::ClearToView(std::uint8_t _view)
  {
    /*
     * 6502: TT66 -- the whole routine, since slice 3d-d-iii-a.
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
    if (m_flight == nullptr)
    {
      m_view = _view; // 6502: STA QQ11, which is all of it that can be done without the world
      return;
    }

    Elite::SetUpScreen(m_flight->Screen(), _view);
  }

  void GameShell::ClearBottomRows()
  {
    if (m_text == nullptr || m_printer == nullptr || m_extended == nullptr || m_message == nullptr)
    {
      return;
    }
    Elite::ClearMessageRows(m_canvas, *m_printer, *m_text, *m_extended, *m_message);
  }

  void GameShell::ClearScreen()
  {
    // 6502: clss -- CHPR reaching past the last row clears the screen and prints again. The caller
    // does the printing; this is the clear.
    ClearToView(m_view);
  }

  void GameShell::BeepAndPause()
  {
    Beep();
    WaitFrames(BEEP_PAUSE_FRAMES);
  }

  void GameShell::Beep()
  {
    // 6502: BEEP -- and every caller on this side (dn2, R5, DK4) drops the carry it returns.
    if (m_sound != nullptr)
    {
      (void)Elite::Beep(*m_sound, false);
    }
  }

  void GameShell::ResetMissileIndicators()
  {
    // 6502: msblob -- ported in slice 3d-d-iii-b, because `KILLSHP`'s seam needed it, so this is a
    // forward rather than a stub. `NOMSL` is the commander's, which is why the count is passed
    // rather than read: the routine draws as many blocks as the ship still carries.
    if (m_flight != nullptr)
    {
      const Elite::FlightScreen& screen = m_flight->Screen();
      Elite::ResetMissileIndicators(m_canvas, screen.commander.At(Elite::Field::Missiles));
    }
  }

  // ---- waiting and the keyboard ------------------------------------------------------------------

  void GameShell::WaitFrames(std::uint8_t _frames)
  {
    /*
     * 6502: DELAY -- wait for _frames VERTICAL SYNCS, and that is literally what this is: `Turn`
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

  void GameShell::FlushKeyboard()
  {
    m_window.FlushKeys(); // 6502: FLKB
  }

  void GameShell::ClearKeyLogger()
  {
    m_window.FlushKeys(); // 6502: ZEKTRAN -- sixty-five bytes of KEYLOOK and `thiskey`
  }

  // ---- the start sequence -------------------------------------------------------------------------

  void GameShell::ResetUniverse()
  {
    // 6502: RESET, and it falls into RES2 rather than calling it -- which is why the port's
    // `ResetGame` ends with `ResetShipAndBubble` and this does not call `ResetShip` as well.
    if (m_flight != nullptr && m_dockedFlag != nullptr)
    {
      Elite::ResetGame(m_flight->Loop(), *m_dockedFlag);
    }
  }

  void GameShell::ResetShip()
  {
    /*
     * 6502: RES2 -- the ship, both line heaps, the dashboard state and the stardust.
     *
     * This was the shell's own approximation of one instruction of it (`LDA #&10 / STA COL2`) for
     * as long as the stardust, the heaps and the dashboard were phase 3's. All three exist, so the
     * seam is gone and the routine runs: §6.73's rule, which is that a seam scoped before the thing
     * behind it existed has to be revisited once it does.
     *
     * It is NOT idempotent, and the cold start calls it twice (§6.25) -- once through `RESET`'s
     * fall-through and once through `DEATH2`'s. The port reproduces both calls rather than
     * collapsing them.
     */
    if (m_flight != nullptr)
    {
      Elite::ResetShipAndBubble(m_flight->Loop());
    }
  }

  void GameShell::StartTheme()
  {
    // 6502: startat -- and BDENTRY's writes to the chip go through the output's direct log, so they
    // land before the interrupt's next frame rather than being lost to it.
    if (m_music != nullptr && m_audio != nullptr)
    {
      Elite::StartTheme(*m_music, m_audio->Direct());
    }
  }

  void GameShell::StopTheme()
  {
    // 6502: stopat.
    if (m_music != nullptr && m_sound != nullptr && m_audio != nullptr)
    {
      Elite::StopMusic(*m_music, *m_sound, m_audio->Direct());
    }
  }

  void GameShell::ShowFrame()
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

  Elite::TitleKey GameShell::ScanTitleKeys(Elite::KeyLogger& _keys)
  {
    /*
     * 6502: JSR RDKEY at the bottom of `TLL2` -- and the PRESENT that comes with it on this
     * platform.
     *
     * `LL9` has just drawn the ship into the canvas and nothing else stands between this frame and
     * the next, so the turn belongs here: the C64 had a VIC-II showing the bitmap continuously and
     * this does not. It is also the only place the keyboard can be read at all, because the table
     * `Held` walks is filled by the message pump `Turn` runs.
     */
    /*
     * AND THE WAIT, WHICH IS THE POINT. `TITLE` runs `MVEIT` and `LL9` and comes straight back
     * round -- there is no `JSR WSCAN` anywhere in it (§6.17) -- so the ship turns at whatever rate
     * a 6510 gets through those two, which `CycleTests` measures at 121,276 cycles: 8.43 turns a
     * second. Presenting once per turn made the display decide instead, and on a 165 Hz panel the
     * ship span twenty times too fast (§6.110).
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
       * The rate is read fresh every time because it CHANGES: the ship is a dot when it starts and
       * a wireframe across the middle of the screen when it arrives, and those cost 15,600 and
       * 121,276 cycles. `INWK+7` is the byte `TLL2` walks down, so it is what the curve is indexed
       * by -- the port is reading the same counter the original's cost depends on.
       */
      const double period = TitleTurnSeconds(m_flight->Screen().work[7]);

      m_spinLeftover += elapsed;
      if (m_spinLeftover >= period)
      {
        // One turn, and the rest of the backlog is dropped rather than repaid: a stall should cost
        // the ship a turn, not spin it faster to catch up.
        m_spinLeftover = (m_spinLeftover >= 2.0 * period) ? 0.0 : (m_spinLeftover - period);
        break;
      }
    }

    return (m_flight != nullptr) ? m_flight->ScanMatrix(_keys) : Elite::TitleKey{true, 0u};
  }

  std::uint8_t GameShell::ShowTitleScreen(std::uint8_t _token, std::uint8_t _shipType, std::uint8_t _distance)
  {
    /*
     * 6502: TITLE -- ported in full now, so this is a forward rather than a placeholder.
     *
     * The rotating ship was a box for as long as `LL9` was slice 3b's. It has not been since 3b
     * landed; what kept the box was that nothing revisited the seam, which is the same pattern the
     * launch path hit three times (§6.73). `AddShip` becoming public for `NWSPS` was the last piece.
     *
     * IT RETURNS `thiskey`, THE KEY NUMBER, and that is the fix as much as the ship is. `BR1`
     * compares the answer against 39 -- the internal number for "Y" -- and this used to return
     * `NextKey()`, which is the CHARACTER. 89 never equals 39, so the disk menu could not be opened
     * from the title screen at all (§6.107).
     */
    if (m_flight == nullptr || m_extendedPrinter == nullptr || m_dockedFlag == nullptr)
    {
      return 0;
    }

    Elite::FlightLoop& loop = m_flight->Loop();
    Elite::TitleScreen title{loop, *this, *m_extendedPrinter, loop.options, loop.keys, *m_dockedFlag};
    return Elite::ShowTitleShip(title, _token, _shipType, _distance);
  }

  // ---- the control codes that leave the text system ------------------------------------------------

  void GameShell::Run(std::uint8_t _code)
  {
    switch (_code)
    {
    case 8:
      // 6502: MT8 -- LDA #6 / JSR DOXC. The DTW2 store is the printer's and is already done.
      if (m_text != nullptr)
      {
        m_text->column = MT8_COLUMN;
      }
      return;

    case 9:
      // 6502: MT9 -- LDA #1 / JSR DOXC / JMP TT66.
      ClearToView(MT9_VIEW);
      return;

    case 21:
      // 6502: CLYNS. The two flags are the printer's and are already set.
      ClearBottomRows();
      return;

    case 23:
    case 29:
      // 6502: MT23 and MT29 -- one routine, two entries, and the row is the only difference.
      // WHITETEXT is an RTS in this build, and MT13's stores are the printer's.
      if (m_text != nullptr)
      {
        m_text->row = (_code == 23) ? MT23_ROW : MT29_ROW;
        m_text->column = 1;
      }
      return;

    case 22:
    case 24:
      /*
       * 6502: PAUSE and PAUSE2 -- spin the title ship and wait for a key. The ship is `LL9` and
       * is phase 3b's; the WAIT is not, and doing it is the difference between a briefing screen
       * a player can dismiss and one the game runs straight past.
       */
      (void)NextKey();
      return;

    case 25:
      // 6502: BRIS -- LDA #216 / JSR DETOK / LDY #100 / JMP DELAY.
      if (m_extendedPrinter != nullptr)
      {
        m_extendedPrinter->Print(BRIEFING_TOKEN);
      }
      WaitFrames(BRIEFING_FRAMES);
      return;

    default:
      /*
       * 11 (`NLIN4`, a rule across the screen) is phase 3's, with the border box it belongs to.
       *
       * 26 (`MT26`, read a line) and 27, 28, 30, 31 (tokens under `GCNT` and `DISK`) are reached
       * only from the MISSION briefings, which are phase 4. `MT26` is ported and could be called
       * from here; what it has no answer for yet is whose buffer the line goes into, and the
       * mission that reads it is the thing that would say.
       */
      return;
    }
  }

} // namespace Outpost
