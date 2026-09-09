#include "pch.h"

#include "FrameClock.h"
#include "ScreenPresenter.h"
#include "FlightSession.h"
#include "Presentation.h"
#include "SaveStore.h"
#include "Scheduler.h"
#include "SettingsFile.h"
#include "Shell.h"
#include "SoundOutput.h"
#include "Window.h"

#include "Game.h"
#include "Universe.h"

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
 * swap chain, the audio device, the files and the CLOCK are here, and everything the game does
 * with a key is there. `Outpost::Scheduler` turns elapsed cycles into a count of passes and
 * `Game::Step` takes one pass, which is §2.1's `Step(InputFrame)` arrived at from the other
 * direction.
 *
 * IT IS THE CLOCK AND NOT THE FLOATING POINT THAT KEEPS THE COUNT OUT HERE (Platform.md T-1,
 * ADR-007 §2): the accumulator is integer cycles now, and reading a clock is what the library
 * may not do.
 *
 * IT HAS TWO OUTER LOOPS AND NOT ONE, because the game does. `MLOOP`'s second half polls the
 * keyboard and dispatches, and every docked screen it reaches ends by blocking in `TT217`; `TT100`
 * runs a frame whether or not a key was pressed and only then falls into `MLOOP`. `QQ12` chooses
 * between them, exactly as `FRCE`'s test of that byte does, and `Outpost::Scheduler` -- the
 * fixed-timestep accumulator ADR-005 §3 asks for -- is what paces both.
 *
 * `DockedSessionTests.cpp` builds the same graph out of a null presenter and drives it through
 * every docked screen, which is what makes this file's shape verified rather than asserted. What
 * is different here is only the far side of each seam: a real canvas instead of nothing, a real
 * window instead of a script, and files instead of an array.
 */
namespace
{

  /// The window opens at this scale, which is 1280x800 (Resolution.md ruling 11.3). TWO and not
  /// three because the image is 640x400 since RS-0, so 3x is 1920x1200 and misses a 1080p display.
  /// The player can resize; the viewport takes the largest integer scale that fits.
  constexpr int INITIAL_SCALE = 2;
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
    App(Outpost::MachineTiming _timing, Outpost::SaveStore& _store)
      : clock(_timing),
        scheduler(_timing),
        audio(_timing),
        store(_store),
        shell(window, presenter, clock, scheduler),
        flight(window),
        game(shell, shell, store)
    {
      Elite::Universe& universe = game.State(); // the game's since M5-e-2; the sessions take it now
      shell.AttachUniverse(universe);
      flight.AttachUniverse(universe);
      // The seams the session and the shell answer that are CALLS needing the seams themselves --
      // `DOCKIT` and the title screen -- so the composition lends the struct back to both.
      shell.AttachPorts(game.PortsOf());
      shell.AttachFlight(flight);
      shell.AttachVideo(universe.video); // ADR-005 §1 -- the sprites composite in Resolve
      shell.AttachSound(audio, universe.sound, universe.music, game);
    }

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    Outpost::Window window;
    Outpost::ScreenPresenter presenter;

    /// The clock and the scheduler, ahead of everything that keeps time against them (T-1). The
    /// machine was three constants in two files and is settled once here; `scheduler.Timing()` is
    /// where anything else asks.
    Outpost::FrameClock clock;
    Outpost::Scheduler scheduler;

    /*
     * The SID, and NEITHER the effect buffer nor the player is here: `universe.sound` is what
     * `NOISE` fills since M3-b-2a and `universe.music` is what `startbd` drives since M3-b-2b,
     * which is the original's own split -- the game writes memory and the interrupt writes the chip.
     * This is the chip, and it is the one object here that can fail to open, in which case the game
     * runs in silence.
     */
    Outpost::SoundOutput audio;
    /// `Run`'s: the settings beside it decide the machine, which three members above take at
    /// construction (T-1).
    Outpost::SaveStore& store;

    Outpost::GameShell shell;
    Outpost::FlightSession flight;

    /// Last, because it binds every one of the above.
    Elite::Game game;

  };

  /*
   * How many passes of the flight half the time that has passed is worth.
   *
   * THE STEPS ARE COUNTED HERE AND TAKEN IN THE LIBRARY, and the split is the determinism guard's:
   * this side may read a clock and the library may not. `Present` blocks on the display's vertical
   * sync and the game was written for the C64's, so tying the two together would run the game at
   * the monitor's rate -- correct at 60 Hz and two and a half times too fast at 144. ADR-005 §3's
   * accumulator is what decouples them, and `FlightFrameCycles` is the measured cost it counts
   * against (§6.114): the C64's main loop has no `WSCAN` in it, so the game slows down as the
   * bubble fills, and the count is indexed by how many slots of `FRIN` are occupied.
   *
   * A BACKLOG LONGER THAN THE CLAMP IS DROPPED and the drop is REPORTED (Design/Platform.md T-1).
   * `Scheduler` does what `PlanSteps` was built for -- a breakpoint or a closed lid produces an
   * accumulator holding minutes, and running it out would make the game appear to hang and then
   * teleport -- and it does one thing more: it counts the drops, where this loop read
   * `plan.stalled` and discarded it because there was nowhere to report it to. `ReportStalls` is
   * that somewhere.
   */
  void Advance(App& _app)
  {
    const int steps = _app.scheduler.TakeSteps(Outpost::FlightFrameCycles(_app.game.ShipsInBubble()));

    for (int step = 0; step < steps; ++step)
    {
      // `thiskey`, and ZERO IS A KEY -- `TT102` runs every pass, which is how the hyperspace
      // countdown ticks whether or not the player touched anything (§6.159).
      if (!_app.game.Step(_app.window.TakePressed()))
      {
        return; // `M%` left the flight half, or `DK4` froze it
      }
    }
  }

  int Run(HINSTANCE _instance)
  {
    // Read, build, then apply: `machine` is a constructor argument to three of `App`'s members and
    // the game's thirteen bytes need a universe, which is why `SettingsFile.h` has two halves.
    Outpost::SaveStore store;
    const Outpost::SettingsReport settings = Outpost::ReadSettingsFile(store.Root());

    auto app = std::make_unique<App>(settings.timing, store);

    app->window.Create(_instance, INITIAL_SCALE);
    app->presenter.Create(app->window.Handle());

    // The thirteen bytes the pause screen toggled (InputTimer.md S-1), now that there is a universe.
    Outpost::ApplySettings(settings.parsed, app->game.State());
    app->window.Warn(settings.Summary());

    // The loader's parts 5 and 6, then `NA%`, then `TT170` -- the cold start, end to end.
    app->game.Reset();

    /*
     * `FRCE`'s two-way dispatch on `QQ12` -- the whole main loop, and the flag is what
     * chooses between its two halves.
     *
     * MLOOP's second half polls the keyboard, dispatches, and goes round; every docked screen it
     * reaches ends by blocking in `TT217`, so a docked game costs one present per key. `TT100` runs
     * a frame first and then the same poll -- so both halves are paced, the flight one by what a
     * frame costs and the docked one by the syncs the last pass asked for (InputTimer.md T-0, T-2).
     *
     * The POSITION goes to the dispatch and not the character, which is the whole reason `KeyMap`
     * maps a Windows key to a C64 matrix position: `TT102` compares against 37 for "8" and never
     * against `'8'`.
     */
    std::uint8_t dockedSyncs = Outpost::DOCKED_PASS_SYNCS; // What the last docked pass asked DELAY for
    Elite::Game::Mode lastMode = app->game.ModeNow();

    while (app->shell.Turn())
    {
      app->game.EndFrame(); // the frame `Turn` just presented is over (`Frame.h`)
      // FRCE's question about `QQ12` -- ONE question since M4-d, and TWO answers since
      // InputTimer.md I-0 took `FREEZE`'s third with the pause screen (owner ruling 2026-09-08).
      const Elite::Game::Mode mode = app->game.ModeNow();

      // The accumulator is dropped at the dock rather than carried across it, in both directions.
      // It is never more than one step, so this is not what protects a launch from a long docked
      // session (the scheduler's clamp is): it starts the next flight on a whole step rather than
      // on a fraction of one measured before the market screen.
      if (mode != lastMode)
      {
        app->scheduler.Reset();
        lastMode = mode;
      }

      if (mode == Elite::Game::Mode::Docked)
      {
        const int passes = app->scheduler.TakeSteps(Outpost::DockedPassCycles(dockedSyncs, app->scheduler.Timing()));
        for (int pass = 0; pass < passes; ++pass)
        {
          dockedSyncs = app->game.StepDocked(app->window.TakePressed()); // `thiskey`, zero when nothing was pressed
        }
      }
      else
      {
        Advance(*app);
      }

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
