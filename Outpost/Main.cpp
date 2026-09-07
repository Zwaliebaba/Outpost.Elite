#include "pch.h"

#include "CanvasPresenter.h"
#include "FlightSession.h"
#include "Presentation.h"
#include "SaveStore.h"
#include "Shell.h"
#include "SoundOutput.h"
#include "Window.h"

#include "Controls.h"
#include "Game.h"
#include "Universe.h"

#include <chrono>
#include <memory>

/*
 * The composition root, and since M3-c that is ALL it is (slice 2e, rewritten by M3-c).
 *
 * It held the universe, the text system, the ports, eight bytes of game state that are in no
 * struct, and every dispatch the main loop makes -- `TT102`'s actions, `M%`'s outcomes, `FREEZE`,
 * the docked pass and the flight pass. Eight hundred lines of 6502 in the one file no Linux runner
 * compiles, which is R15's whole surface and the reason `check_outpost.py` has grown seven halves.
 * `Elite::Game` is where all of it lives now, and what is left here is what CANNOT be there.
 *
 * THE LINE THE SPLIT FALLS ON IS THE DETERMINISM GUARD'S. `GameLogic` may not touch a clock, a
 * float, a file or a Win32 call (AGENTS.md §5), and that is exactly the boundary: the window, the
 * swap chain, the audio device, the files and the SECONDS are here, and everything the game does
 * with a key is there. `PlanSteps` turns elapsed seconds into a count of passes and `Game::Step`
 * takes one pass, which is §2.1's `Step(InputFrame)` arrived at from the other direction.
 *
 * IT HAS TWO OUTER LOOPS AND NOT ONE, because the game does. `MLOOP`'s second half polls the
 * keyboard and dispatches, and every docked screen it reaches ends by blocking in `TT217`; `TT100`
 * runs a frame whether or not a key was pressed and only then falls into `MLOOP`. `QQ12` chooses
 * between them, exactly as `FRCE`'s `LDA QQ12 / BEQ` does, and `PlanSteps` -- the fixed-timestep
 * accumulator ADR-005 §3 asks for -- is what paces both.
 *
 * `DockedSessionTests.cpp` builds the same graph out of a null presenter and drives it through
 * every docked screen, which is what makes this file's shape verified rather than asserted. What
 * is different here is only the far side of each seam: a real canvas instead of nothing, a real
 * window instead of a script, and files instead of an array.
 */
namespace
{

  /// The window opens at this scale, which is 960x600 -- large enough to read on a modern display
  /// and small enough to fit inside one. The player can resize; the viewport follows.
  constexpr int INITIAL_SCALE = 3;

  /*
   * The platform, and one `Elite::Game` on the other side of it.
   *
   * The declaration order is the construction order and it is load-bearing. THE COMPOSITION ROOT
   * HOLDS NO GAME STATE since M5-e-2: `Elite::Game` owns the universe, and the two sessions that
   * bound it at construction -- which kept it here, because `Game` needs both at ITS construction --
   * take it afterwards through `AttachUniverse`, as they take the ports. Platform, sessions, game.
   */
  struct App
  {
    App()
      : shell(window, presenter),
        flight(window),
        game(shell, shell, store, flight)
    {
      Elite::Universe& universe = game.State(); // the game's since M5-e-2; the sessions take it now
      shell.AttachUniverse(universe);
      flight.AttachUniverse(universe);
      // The seams the session and the shell answer that are CALLS needing the seams themselves --
      // `DOCKIT` and the title screen -- so the composition lends the struct back to both.
      flight.AttachPorts(game.PortsOf());
      shell.AttachPorts(game.PortsOf());
      shell.AttachFlight(flight);
      shell.AttachVideo(universe.video); // ADR-005 §1 -- the sprites composite in Resolve
      shell.AttachSound(audio, universe.sound, universe.music, game);
    }

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    Outpost::Window window;
    Outpost::CanvasPresenter presenter;

    /*
     * The SID, and NEITHER the effect buffer nor the player is here: `universe.sound` is what
     * `NOISE` fills since M3-b-2a and `universe.music` is what `startbd` drives since M3-b-2b,
     * which is the original's own split -- the game writes memory and the interrupt writes the chip.
     * This is the chip, and it is the one object here that can fail to open, in which case the game
     * runs in silence.
     */
    Outpost::SoundOutput audio;
    Outpost::SaveStore store;

    Outpost::GameShell shell;
    Outpost::FlightSession flight;

    /// Last, because it binds every one of the above.
    Elite::Game game;
  };

  /*
   * 6502: TT100 -- how many passes of the flight half a wall-clock interval is worth.
   *
   * THE STEPS ARE COUNTED HERE AND TAKEN IN THE LIBRARY, and the split is the determinism guard's:
   * this is the only arithmetic in the loop that needs a `double`. `Present` blocks on the display's
   * vertical sync and the game was written for the C64's, so tying the two together would run the
   * game at the monitor's rate -- correct at 60 Hz and two and a half times too fast at 144.
   * ADR-005 §3's accumulator is what decouples them, and `FlightFrameSeconds` is the measured cost
   * it counts against (§6.114): the C64's main loop has no `WSCAN` in it, so the game slows down as
   * the bubble fills, and the count is indexed by how many slots of `FRIN` are occupied.
   *
   * A BACKLOG LONGER THAN THE CLAMP IS DROPPED, which is `PlanSteps` doing what it was built for:
   * a breakpoint or a closed lid produces an accumulator holding minutes, and running it out would
   * make the game appear to hang and then teleport. There is nowhere to report the drop to in a
   * windowed build, which is why `stalled` is read and discarded rather than ignored.
   */
  void Advance(App& _app, double _elapsedSeconds, double& _accumulated)
  {
    const Outpost::StepPlan plan =
      Outpost::PlanSteps(_elapsedSeconds, _accumulated, 1.0 / Outpost::FlightFrameSeconds(_app.game.ShipsInBubble()));
    _accumulated = plan.leftoverSeconds;
    (void)plan.stalled;

    for (int step = 0; step < plan.steps; ++step)
    {
      // 6502: `thiskey`, and ZERO IS A KEY -- `TT102` runs every pass, which is how the hyperspace
      // countdown ticks whether or not the player touched anything (§6.159).
      std::uint8_t key = 0;
      (void)_app.window.TakeKey(key);

      if (!_app.game.Step(key))
      {
        return; // 6502: `M%` left the flight half, or `DK4` froze it
      }
    }
  }

  int Run(HINSTANCE _instance)
  {
    auto app = std::make_unique<App>();

    app->window.Create(_instance, INITIAL_SCALE);
    app->presenter.Create(app->window.Handle());

    // 6502: the loader's parts 5 and 6, then `NA%`, then `TT170` -- the cold start, end to end.
    app->game.Reset();

    /*
     * 6502: `FRCE`'s `LDA QQ12 / BEQ P%+5 / JMP MLOOP / JMP TT100` -- the whole main loop, and the
     * flag is what chooses between its two halves.
     *
     * MLOOP's second half polls the keyboard, dispatches, and goes round; every docked screen it
     * reaches ends by blocking in `TT217`, so a docked game costs one present per key. `TT100` runs
     * a frame first and only then falls into the same poll -- so both halves are paced, and the
     * docked one uses the flight frame's EMPTY-bubble cost as a floor rather than a measurement,
     * because a docked pass draws no ships and is cheaper than that (§6.114 one screen on).
     *
     * The POSITION goes to the dispatch and not the character, which is the whole reason `KeyMap`
     * maps a Windows key to a C64 matrix position: `TT102` compares against 37 for "8" and never
     * against `'8'`.
     */
    double accumulated = 0.0;
    double dockedLeftover = 0.0;
    auto last = std::chrono::steady_clock::now();

    while (app->shell.Turn())
    {
      const auto now = std::chrono::steady_clock::now();
      const double elapsed = std::chrono::duration<double>(now - last).count();
      last = now;

      // 6502: FRCE's `LDA QQ12 / BEQ`, and `FREEZE` above it -- ONE question since M4-d. It was two
      // tests this file had to keep in the right order (a frozen game is frozen in both halves, so
      // the pause test comes first); `Game::Mode` is that rule expressed once, where both bytes are.
      const Elite::Game::Mode mode = app->game.ModeNow();
      if (mode == Elite::Game::Mode::Paused)
      {
        std::uint8_t key = 0;
        if (app->window.TakeKey(key))
        {
          app->game.StepPaused(key);
        }
        continue;
      }

      if (mode == Elite::Game::Mode::Docked)
      {
        /*
         * The leftover is dropped rather than carried across the dock. It is never more than one
         * step -- `PlanSteps` consumes the whole backlog and hands back the remainder -- so this is
         * not what protects a launch from a long docked session; the clamp inside `PlanSteps` is.
         * What it does is start the next flight on a whole step instead of on a fraction of one
         * measured before the market screen, which is a leftover with no meaning left in it.
         */
        accumulated = 0.0;

        const Outpost::StepPlan docked = Outpost::PlanSteps(elapsed, dockedLeftover, 1.0 / Outpost::FlightFrameSeconds(0));
        dockedLeftover = docked.leftoverSeconds;

        for (int pass = 0; pass < docked.steps; ++pass)
        {
          std::uint8_t key = 0;
          (void)app->window.TakeKey(key); // 6502: `thiskey`, which is zero when nothing is held
          app->game.StepDocked(key);
        }
        continue;
      }

      Advance(*app, elapsed, accumulated);
    }

    return 0;
  }

  /*
   * The one place an exception is caught (AGENTS.md section 5).
   *
   * Everything below `Run` reports failure by throwing through `winrt::check_hresult`, which means
   * a missing GPU or a refused window arrives here as one message rather than as a silent exit.
   */
  int Guarded(HINSTANCE _instance) noexcept
  {
    try
    {
      return Run(_instance);
    }
    catch (const winrt::hresult_error& failure)
    {
      MessageBoxW(nullptr, failure.message().c_str(), L"Elite", MB_OK | MB_ICONERROR);
      return 1;
    }
    catch (const std::exception& failure)
    {
      const std::string what = failure.what();
      MessageBoxA(nullptr, what.c_str(), "Elite", MB_OK | MB_ICONERROR);
      return 1;
    }
  }
} // namespace

/*
 * BOTH ENTRY POINTS ARE DEFINED, and that is deliberate rather than belt-and-braces. Which one
 * the runtime wants depends on the linker's default entry symbol for `/SUBSYSTEM:WINDOWS`, and
 * that default is `WinMainCRTStartup` -- the NARROW one -- unless something sets `/ENTRY`
 * otherwise, even in a project built as Unicode. Defining both costs three lines and removes the
 * question; the unused one is never called.
 */
int APIENTRY wWinMain(_In_ HINSTANCE _instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
  return Guarded(_instance);
}
