#include "pch.h"

#include "CanvasPresenter.h"
#include "FlightSession.h"
#include "KeyMap.h"
#include "Presentation.h"
#include "SaveStore.h"
#include "Shell.h"
#include "Window.h"

#include "Canvas.h"
#include "Commander.h"
#include "DockedKeys.h"
#include "Docking.h"
#include "Equipment.h"
#include "ExtendedTokens.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "LoaderScreen.h"
#include "Market.h"
#include "MarketScreen.h"
#include "Rng.h"
#include "SaveGame.h"
#include "StartUp.h"
#include "StateTokens.h"
#include "StatusScreen.h"
#include "SystemScreen.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Universe.h"
#include "ViewChange.h"

#include <array>
#include <chrono>
#include <memory>

/*
 * The composition root (slice 2e).
 *
 * ADR-004 section 1 puts the wiring here rather than in any one screen, and there is a concrete
 * reason rather than a stylistic one: the text system has a CYCLE in it. The character printer
 * needs a sink, the token printer needs the character printer, the state tokens need the token
 * printer and the commander, and the token printer needs the state tokens BACK. `SetValueTokens`
 * is what breaks that cycle, and something has to be the thing that calls it.
 *
 * `DockedSessionTests.cpp` builds the same graph out of a null presenter and drives it through
 * every docked screen, which is what makes this file's shape verified rather than asserted. What
 * is different here is only the far side of each seam: a real canvas instead of nothing, a real
 * window instead of a script, and files instead of an array.
 *
 * IT NOW HAS TWO OUTER LOOPS AND NOT ONE, because the game does. `MLOOP`'s second half polls the
 * keyboard and dispatches, and every docked screen it reaches ends by blocking in `TT217`; `TT100`
 * runs a frame whether or not a key was pressed and only then falls into `MLOOP`. `QQ12` chooses
 * between them, exactly as `FRCE`'s `LDA QQ12 / BEQ` does, and `PlanSteps` -- the fixed-timestep
 * accumulator ADR-005 section 3 asks for -- finally has the caller it was written for.
 *
 * WHAT IS STILL REFUSED is hyperspace and the charts, which are phase 4's, and they are listed by
 * name in `Perform` rather than defaulted so that adding one is a compiler error here.
 */
namespace
{

  /// The window opens at this scale, which is 960x600 -- large enough to read on a modern display
  /// and small enough to fit inside one. The player can resize; the viewport follows.
  constexpr int INITIAL_SCALE = 3;

  /*
   * Everything a docked game is, wired together once.
   *
   * The declaration order is the construction order and it is load-bearing, which is why the
   * members are grouped by what they depend on rather than by what they are.
   */
  struct Game
  {
    Game()
      : shell(window, presenter, canvas, view),
        screen(canvas, text, &shell),
        characters(screen),
        recursive(characters),
        values(recursive, text, commander, name, currentSeeds, selectedSeeds, false),
        extended(characters, recursive, rng, &shell),
        trade{recursive, characters, extended, text, shell, shell, rng},
        save{recursive, characters, extended, screen, text, shell, shell, store, numbers},
        flight(window, canvas, text, characters, recursive, message, commander, rng, status, view, explosionCount, current.techLevel)
    {
      recursive.SetValueTokens(&values);
      recursive.SetCursor(&text);
      shell.Attach(recursive, text, characters.state, message);
      shell.AttachExtended(extended);
      shell.AttachFlight(flight, dockedFlag);

      // 6502: DTW2 -- the extended printer starts between sentences, which is what the first
      // capital letter of the first screen depends on.
      characters.state.sentenceStart = 0xFF;
    }

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // ---- the platform ---------------------------------------------------------------------------
    Outpost::Window window;
    Outpost::CanvasPresenter presenter;
    Elite::Canvas canvas;

    /*
     * 6502: QQ11 -- which screen is showing.
     *
     * Here rather than inside the shell because BOTH halves write it: `TT66` is the shell's seam
     * and `LOOK1`, `TT110` and `ChangeView` are the flight session's, and they are the same byte.
     * It is declared before `shell` because `shell` binds it.
     */
    std::uint8_t view = 0;

    Outpost::GameShell shell;
    Outpost::SaveStore store;

    // ---- the text system ------------------------------------------------------------------------
    Elite::TextState text;
    Elite::TextPrinter screen;
    Elite::CharacterPrinter characters;
    Elite::TokenPrinter recursive;
    Elite::Rng rng;
    Elite::NumberWorkspace numbers;

    // ---- the commander and the universe ----------------------------------------------------------
    Elite::CommanderBlock commander = Elite::DefaultCommander();
    std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
    std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> image{};
    std::array<std::uint8_t, 16> buffer{};
    bool useDisk = false;

    Elite::SystemSeeds currentSeeds{};
    Elite::SystemSeeds selectedSeeds{};
    Elite::CurrentSystem current;
    Elite::MarketState market;
    Elite::FlightStatus status;
    Elite::MessageState message; ///< 6502: DLY, de, MCH and messXC

    std::uint8_t crosshairX = 0;
    std::uint8_t crosshairY = 0;
    std::uint8_t explosionCount = 0;
    std::uint8_t dockedFlag = 0;

    // ---- the screens ------------------------------------------------------------------------------
    Elite::StateTokens values;
    Elite::ExtendedTokenPrinter extended;
    Elite::TradeScreen trade;
    Elite::SaveScreen save;

    /// Last, because it holds references to nearly everything above it.
    Outpost::FlightSession flight;
  };

  /// The start sequence's argument list, which `Run` builds for the cold start and `Leave` rebuilds
  /// for `DEATH2`. It is an aggregate of references, so building it twice costs nothing and sharing
  /// one would mean keeping a struct alive across the whole program for two call sites.
  [[nodiscard]] Elite::GameStart StartOf(Game& _game)
  {
    return Elite::GameStart{_game.shell,      _game.save,       _game.text,           _game.commander, _game.name,
                            _game.image,      _game.buffer,     _game.useDisk,        _game.current,   _game.selectedSeeds,
                            _game.crosshairX, _game.crosshairY, _game.explosionCount, _game.dockedFlag};
  }

  /*
   * One key, and whatever screen it reaches.
   *
   * 6502: what `TT102` does with the label it chose. The dispatch itself is `ActionForKey`, which
   * is compared against the shipped routine over 16,384 states; this is the other half, and the
   * actions that need phase 3 are refused rather than silently ignored -- a docked game that did
   * nothing for a launch key would look exactly like one that had wired it up.
   */
  void Perform(Game& _game, const Elite::KeyOutcome& _outcome)
  {
    switch (_outcome.action)
    {
    case Elite::KeyAction::StatusMode:
    {
      /*
       * 6502: STATUS's condition -- `LDA QQ12 / BNE st6`, and then `LDY JUNK / LDA FRIN+2,Y`.
       *
       * The two middle bytes were zeroes while `InSpace` was unreachable, because docked is the one
       * state in which nothing else is read. They are wired now for the same reason the flight loop
       * is: pressing F1 in space is a docked key that works above the split, and a condition of
       * "Green" with three Vipers on the scanner is not a stub, it is a wrong answer.
       *
       * `FRIN+2,Y` steps past the planet and the sun and then past Y pieces of junk, so with a full
       * bubble it lands on the list's terminator rather than off the end. The bound is checked all
       * the same, and reads zero -- `LineHeap::Read`'s rule, for the same reason.
       */
      const Elite::Bubble& bubble = _game.flight.Screen().bubble;
      const std::size_t beyond = static_cast<std::size_t>(bubble.junk) + 2u;
      const Elite::ShipCondition condition{_game.dockedFlag, bubble.junk,
                                           (beyond < bubble.slots.size()) ? bubble.slots[beyond] : std::uint8_t{0}, _game.status.energy};
      Elite::StatusScreen(_game.trade, _game.commander, condition, _game.crosshairX, _game.crosshairY, _game.selectedSeeds);
      return;
    }

    case Elite::KeyAction::DataOnSystem:
    {
      // 6502: JSR TT111 / JMP TT25 -- the screen reads what the search leaves behind.
      const Elite::NearestSystem found =
        Elite::FindNearestSystem(_game.commander.GalaxySeeds(), _game.crosshairX, _game.crosshairY,
                                 _game.commander.At(Elite::Field::SystemX), _game.commander.At(Elite::Field::SystemY));
      _game.selectedSeeds = found.seeds;
      Elite::SystemDataScreen(_game.trade, _game.selectedSeeds, found.data, found.distance);
      return;
    }

    case Elite::KeyAction::MarketPrice:
      // 6502: TT167. The screen reset above it is TRADEMODE, which the caller does.
      _game.shell.SetUpTradeScreen(Elite::BUY_CARGO_VIEW);
      Elite::PrintMarketScreen(_game.recursive, _game.characters, _game.text, _game.current.economy, _game.market, false);
      return;

    case Elite::KeyAction::BuyCargo:
      Elite::BuyScreen(_game.trade, _game.commander, _game.market, _game.current.economy, false);
      return;

    case Elite::KeyAction::SellCargo:
      Elite::ListCargo(_game.trade, _game.commander, _game.market, _game.current.economy, Elite::SELL_CARGO_VIEW);
      return;

    case Elite::KeyAction::Inventory:
      Elite::InventoryScreen(_game.trade, _game.commander, _game.market, _game.current.economy);
      return;

    case Elite::KeyAction::EquipShip:
      Elite::EquipShipScreen(_game.trade, _game.commander, _game.current.techLevel);
      return;

    case Elite::KeyAction::DiskAccess:
    {
      const Elite::DiskMenuResult menu =
        Elite::DiskAccessMenu(_game.save, _game.commander, _game.name, _game.image, _game.buffer, _game.useDisk);
      // 6502: BCC P%+5 / JMP QU5 / JMP BAY -- and QU5 is DFAULT, which installs the loaded image.
      if (menu.newCommander)
      {
        (void)Elite::LoadCommander(_game.image, _game.commander, _game.name);
      }
      return;
    }

    case Elite::KeyAction::Launch:
      /*
       * 6502: TT110 -- and it is dispatched in BOTH halves of the loop, because `TT102` tests for
       * it ABOVE the docked/flight split. Pressing "1" docked leaves the station; pressing it in
       * flight falls through `TT110`'s own `LDX QQ12 / BEQ NLUNCH` and is the front view.
       *
       * `_selected` comes back written: the launch runs `TT111` for the SEEDS rather than for the
       * distance, because the planet's appearance is generated from the system you are leaving.
       */
      Elite::Launch(_game.flight.Loop(), &_game.shell, _game.dockedFlag, _game.crosshairX, _game.crosshairY, _game.current.techLevel,
                    _game.selectedSeeds);
      return;

    case Elite::KeyAction::ChangeView:
      // 6502: LOOK1 with X = the view. The dispatch already decided which one through two `EQUB
      // &2C`s, so this performs the answer rather than reading the key again.
      Elite::ChangeView(_game.flight.Screen(), _outcome.view);
      return;

    /*
     * The rest belong to phases the port has not reached. They are listed rather than defaulted
     * so that adding a phase-4 screen is a compiler error here instead of a key that does nothing.
     */
    case Elite::KeyAction::Hyperspace:
    case Elite::KeyAction::CountdownOnly:
    case Elite::KeyAction::LongRangeChart:
    case Elite::KeyAction::ShortRangeChart:
    case Elite::KeyAction::ShowDistance:
    case Elite::KeyAction::SearchBySystemName:
    case Elite::KeyAction::HomeCrosshairs:
    case Elite::KeyAction::MoveCrosshairs:
    case Elite::KeyAction::Nothing:
      return;
    }
  }

  /// 6502: TT102 -- decide, then do. The two are separate because the start sequence FORCES a key
  /// and hands back what the dispatch made of it, so it has already decided by the time it returns.
  void PressKey(Game& _game, std::uint8_t _key)
  {
    Perform(_game, Elite::ActionForKey(_key, _game.dockedFlag, _game.shell.View(), _game.status.hyperspaceCountdown, false));
  }

  /*
   * 6502: the three jumps that leave `M%` and do not come back -- `JMP DOENTRY`, `JMP DEATH` and
   * `JMP ESCAPE` (§6.82).
   *
   * The port hands them back as a `LoopOutcome` because none of them returns; this is where the
   * jump is actually taken. Two of the three are wired and one is refused, and which is which is
   * decided by what exists rather than by what is convenient.
   */
  void Leave(Game& _game, Elite::LoopOutcome _outcome)
  {
    switch (_outcome)
    {
    case Elite::LoopOutcome::Docked:
    {
      /*
       * 6502: DOENTRY -- ported in slice 2d, so this is the whole arrival.
       *
       * IT CANNOT BE REACHED TODAY. Part 9's docking check reads `SSPR`, and the only thing that
       * sets `SSPR` is `NWSPS`, which is phase 4's and is the stub in `FlightSession`. The wiring
       * is here anyway because the routine is built and the alternative is a hole that looks like
       * a decision.
       */
      const Elite::DockingResult arrival = Elite::DockAtStation(_game.shell, _game.flight.Screen(), _game.flight.Loop().clip, &_game.shell,
                                                                _game.dockedFlag, _game.view, false);

      // 6502: JMP BAY, which every one of the seven exits eventually reaches -- directly on the
      // `EN6` path, and after a briefing on the other six. The briefings are phase 4's, so the
      // port takes the tail they share rather than inventing a screen for them.
      const Elite::ForcedKey bay = (arrival.outcome == Elite::DockingOutcome::DockingBay)
                                     ? arrival.bay
                                     : Elite::EnterDockingBay(_game.dockedFlag, _game.view, _game.status.hyperspaceCountdown, false);
      Perform(_game, bay.outcome);
      return;
    }

    case Elite::LoopOutcome::Died:
      /*
       * 6502: DEATH, which falls into DEATH2 -- and the port takes DEATH2 alone.
       *
       * What is skipped is `DEATH` itself: the dying sound, `ASL DELTA` twice, the 24-row screen
       * that hides the dashboard, and the loop that scatters wreckage through `SFS1`. All four are
       * phase 4's or phase 5's. What is NOT skipped is the restart, because that is `JSR RES2` and
       * then a fall into `BR1`, and both of those exist -- so death takes the player back to the
       * title screen and the docking bay exactly as it should.
       *
       * Neither routine restores the energy banks. That is the game's behaviour and not an
       * omission here: `RESET` fills them and only the COLD start calls it (ADR-003).
       */
      {
        _game.shell.ResetShip();

        Elite::GameStart restart = StartOf(_game);
        const Elite::ForcedKey begun = Elite::StartGame(restart);
        Perform(_game, begun.outcome);
      }
      return;

    case Elite::LoopOutcome::Escaped:
      /*
       * 6502: ESCAPE -- phase 4's, with `SESCP` that launches the pod and the cargo and equipment
       * loss that pays for it. Refused rather than approximated: a pod that took the player back to
       * the station without emptying the hold would be a cheaper escape than the game sells.
       *
       * A default commander cannot reach this -- `KY13` is ANDed with `ESCP` -- so it needs a
       * loaded commander who has bought one.
       */
      return;

    case Elite::LoopOutcome::Continued:
      return;
    }
  }

  /*
   * 6502: TT100 -- one pass of the flight half of the main loop, and then `MLOOP` under it.
   *
   * THE STEPS ARE COUNTED RATHER THAN TAKEN ONE PER PRESENT. `Present` blocks on the display's
   * vertical sync and the game was written for the C64's, so tying the two together would run the
   * game at the monitor's rate: correct at 60 Hz and two and a half times too fast at 144. ADR-005
   * section 3's accumulator is what decouples them, and `FlightFrameSeconds` is the measured cost it
   * counts against (§6.114).
   *
   * A BACKLOG LONGER THAN THE CLAMP IS DROPPED, which is `PlanSteps` doing what it was built for:
   * a breakpoint or a closed lid produces an accumulator holding minutes, and running it out would
   * make the game appear to hang and then teleport. There is nowhere to report the drop to in a
   * windowed build, which is why `stalled` is read and discarded here rather than ignored.
   */
  void Advance(Game& _game, double _elapsedSeconds, double& _accumulated)
  {
    /*
     * 6502: how long `M%` takes, which is the only thing that decides how fast the game runs.
     *
     * THE RATE IS NOT THE REFRESH. §6.17 found that the C64's main loop has no `WSCAN` in it, so
     * the loop runs at whatever the processor manages and the game slows down when the bubble
     * fills -- and this port ran it at the NTSC vertical refresh, four to five times faster than
     * the machine (§6.114). `FlightFrameSeconds` is the measured cost, indexed by how many slots
     * of `FRIN` are occupied, which is what the measurement varied.
     */
    const Elite::Bubble& bubble = _game.flight.Screen().bubble;
    std::uint8_t ships = 0;
    for (const std::uint8_t type : bubble.slots)
    {
      // 6502: `FRIN`'s zero is the list's terminator, not a hole in it.
      if (type == 0u)
      {
        break;
      }
      ++ships;
    }

    const Outpost::StepPlan plan = Outpost::PlanSteps(_elapsedSeconds, _accumulated, 1.0 / Outpost::FlightFrameSeconds(ships));
    _accumulated = plan.leftoverSeconds;
    (void)plan.stalled;

    for (int step = 0; step < plan.steps; ++step)
    {
      const Elite::LoopOutcome outcome = Elite::MainFlightLoop(_game.flight.Loop());
      if (outcome != Elite::LoopOutcome::Continued)
      {
        Leave(_game, outcome);
        return;
      }

      /*
       * 6502: and then `MLOOP`'s second half, which the flight loop falls into -- `JSR TT17` and
       * `TT102`, once per frame and AFTER it.
       *
       * The queue is the window's rather than the matrix scan's, and it is a different thing from
       * the key logger `FlightSession::ScanKeyboard` fills: `TT102` wants the key that was pressed
       * and the flight loop wants the keys being held, which is why the game reads the hardware
       * twice per frame and so does this.
       *
       * AND `TT17` IS THE HALF THAT WAS MISSING. The comment above described both reads from the
       * day this loop was written and only one of them was here, so `DOKEY` -- ported, swept and
       * green -- was never called by anything but its own test: no key the player HELD reached the
       * game, which is every flight control there is (§6.111).
       */
      Elite::ScanFlightControls(_game.flight.Loop(), _game.flight);

      std::uint8_t key = 0;
      if (_game.window.TakeKey(key))
      {
        PressKey(_game, key);
      }
    }
  }

  int Run(HINSTANCE _instance)
  {
    auto game = std::make_unique<Game>();

    game->window.Create(_instance, INITIAL_SCALE);
    game->presenter.Create(game->window.Handle());

    /*
     * 6502: the Elite loader's parts 5 and 6 -- the colours the game is drawn in.
     *
     * BEFORE ANYTHING ELSE, because everything else assumes it. Screen RAM and colour RAM are not
     * the game's to fill: the loader fills them, once, and the game then writes bits into a bitmap
     * whose palette is already decided cell by cell. Start without it and every routine below
     * draws exactly what it should and the screen stays black -- the border box, the dashboard
     * picture and all seven dials included.
     */
    Elite::SetUpLoaderScreen(game->canvas);

    // 6502: NA% -- the commander the disk menu's "load" compares against, and the one SVE writes.
    Elite::SaveCommander(game->commander, game->name, game->image);

    Elite::GameStart start = StartOf(*game);

    // 6502: TT170 -- the cold start. It ends by pressing "8" for the player and entering the docked
    // half of the main loop, which is why there is no separate "draw the first screen" step.
    const Elite::ForcedKey begun = Elite::ResetAndStartGame(start);
    if (begun.loop == Elite::MainLoop::Docked)
    {
      // 6502: the market is rolled on arrival rather than by the start sequence, and the market
      // screen reads it -- so a game that skipped this would print a table of zeroes.
      Elite::GenerateMarket(game->rng, game->current.economy, game->market);

      // 6502: BAY forces "8" and TT102 has already dispatched it, so this PERFORMS that outcome
      // rather than deciding it again -- deciding twice would work today and stop working the
      // moment the dispatch depends on something the first decision changed.
      Perform(*game, begun.outcome);
    }

    /*
     * 6502: `FRCE`'s `LDA QQ12 / BEQ P%+5 / JMP MLOOP / JMP TT100` -- the whole main loop, and the
     * flag is what chooses between its two halves.
     *
     * MLOOP's second half polls the keyboard, dispatches, and goes round; every docked screen it
     * reaches ends by blocking in `TT217`, so a docked game costs one present per key. `TT100` runs
     * a frame first and only then falls into the same poll, which is why the flight half has an
     * accumulator and the docked half does not: a docked game has nothing to step.
     *
     * The POSITION goes to the dispatch and not the character, which is the whole reason `KeyMap`
     * maps a Windows key to a C64 matrix position: `TT102` compares against 37 for "8" and never
     * against `'8'`. A shell that handed it the translated character would find that no docked
     * screen key worked at all.
     */
    double accumulated = 0.0;
    auto last = std::chrono::steady_clock::now();

    while (game->shell.Turn())
    {
      const auto now = std::chrono::steady_clock::now();
      const double elapsed = std::chrono::duration<double>(now - last).count();
      last = now;

      if (game->dockedFlag != 0)
      {
        /*
         * The leftover is dropped rather than carried across the dock. It is never more than one
         * step -- `PlanSteps` consumes the whole backlog and hands back the remainder -- so this is
         * not what protects a launch from a long docked session; the clamp inside `PlanSteps` is.
         * What it does is start the next flight on a whole step instead of on a fraction of one
         * measured before the market screen, which is a leftover with no meaning left in it.
         */
        accumulated = 0.0;

        std::uint8_t key = 0;
        if (game->window.TakeKey(key))
        {
          PressKey(*game, key);
        }
        continue;
      }

      Advance(*game, elapsed, accumulated);
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
