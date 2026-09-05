#include "pch.h"

#include "CanvasPresenter.h"
#include "FlightSession.h"
#include "KeyMap.h"
#include "Presentation.h"
#include "SaveStore.h"
#include "Shell.h"
#include "Window.h"

#include "Canvas.h"
#include "CargoScreens.h"
#include "Commander.h"
#include "Dashboard.h"
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

    /*
     * 6502: what `TT17` leaves in X and Y -- the crosshair steps, held between the scan and the
     * dispatch that uses them.
     *
     * On the 6502 they are registers and the two routines are consecutive; here `TT102`'s work is
     * a function call away, so they have to live somewhere. This is that somewhere, and it is in
     * `Game` rather than in the shell because both halves of the loop write it.
     */
    Elite::CrosshairStep crosshairStep;
    std::uint8_t explosionCount = 0;
    std::uint8_t dockedFlag = 0;

    /*
     * The key the Buy Cargo screen was left with, for the next docked pass to press (ADR-006).
     *
     * `BAY2`'s shape -- `FRCE` entered with a key already down -- but with the key the player
     * pressed rather than a forced f9, and pressed by the LOOP rather than from inside `Perform`:
     * a screen that pressed "1" from within the "1" handler would nest a level deeper on every
     * press, and the original's one-deep recursion (f9, which forces nothing) was a property of the
     * key it forced rather than of the mechanism.
     */
    std::uint8_t forcedKey = 0;

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
   * 6502: what a chart reads -- QQ9, QQ10, QQ0, QQ1, QQ11 and QQ14, gathered where they live.
   *
   * The crosshairs are the game's own two bytes and the home position is INSIDE the commander
   * block (§2e's finding: `QQ0` and `QQ1` are `TP+1` and `TP+2`), so this is a view onto four
   * different owners rather than a struct anybody keeps.
   */
  [[nodiscard]] Elite::ChartView ChartOf(Game& _game)
  {
    Elite::ChartView view;
    view.cursorX = _game.crosshairX;
    view.cursorY = _game.crosshairY;
    view.homeX = _game.commander.At(Elite::Field::SystemX);
    view.homeY = _game.commander.At(Elite::Field::SystemY);
    view.view = _game.view;
    view.fuel = _game.commander.At(Elite::Field::Fuel);
    return view;
  }

  /*
   * 6502: TT22 and TT23 -- draw whichever chart the view says.
   *
   * TT23 LIFTS THE CLIPPER'S LIMITS AND PUTS THEM BACK. `LDA #199 / STA Yx2M1 / STA dontclip` at
   * the top and `LDA #0 / STA dontclip / LDA #2*Y-1 / STA Yx2M1` at the bottom, because the
   * short-range chart draws system discs below the space view's floor. Both bytes belong to the
   * drawing rather than to the chart, which is why they are set here and not inside `TT23` (§6.45).
   */
  void DrawChart(Game& _game)
  {
    const Elite::ChartView chart = ChartOf(_game);
    Elite::FlightScreen& screen = _game.flight.Screen();

    if (_game.view == Elite::SHORT_RANGE_CHART_VIEW)
    {
      screen.heaps.yx2M1 = Elite::CHART_SCREEN_BOTTOM;
      _game.flight.Loop().clip.dontclip = Elite::CHART_SCREEN_BOTTOM;

      Elite::DrawShortRangeChart(_game.canvas, screen.draw, _game.recursive, _game.text, chart, _game.commander.GalaxySeeds(),
                                 &_game.flight);

      _game.flight.Loop().clip.dontclip = 0u;
      screen.heaps.yx2M1 = Elite::SPACE_VIEW_BOTTOM; // 6502: LDA #2*Y-1
      return;
    }

    Elite::DrawLongRangeChart(_game.canvas, screen.draw, _game.recursive, _game.text, chart, _game.commander.GalaxySeeds(), &_game.flight);
  }

  /// 6502: TT22 and TT23's opening `JSR TT66`, which the routines leave to their caller, and then
  /// the chart itself.
  void ShowChart(Game& _game, std::uint8_t _view)
  {
    _game.shell.ClearToView(_view);
    DrawChart(_game);
  }

  /*
   * 6502: the head of `MLOOP` -- main game loop part 5's two countdowns, before anything else.
   *
   * BOTH ARE COOLING AND NEITHER WAS PORTED. `GNTMP` is the laser temperature the LT dial reads and
   * `LASCT` is the pulse laser's own countdown, and the two together are why a gun works at all:
   * part 3 of the flight loop refuses to fire while `LASCT` is non-zero and jams the gun for good at
   * a `GNTMP` of 242. Both are written by firing and this is the only place either comes down, so
   * without it a pulse laser fires exactly once per flight and the LT bar only ever rises.
   *
   * `LASCT` FALLS BY TWO AND NOT BY ONE -- `DEX / BEQ P%+3 / DEX / STX LASCT`, where the branch
   * skips the second `DEX` so it stops at zero rather than wrapping past it. `GNTMP` falls by one.
   *
   * They are ABOVE part 5's `LDA QQ11` gate, so they run on a docked pass as well as a flying one,
   * and both loops call this for that reason rather than only the one that has a laser.
   */
  void CoolTheGuns(Elite::FlightStatus& _status) noexcept
  {
    // 6502: LDX GNTMP / BEQ EE20 / DEC GNTMP.
    if (_status.laserTemperature != 0u)
    {
      _status.laserTemperature = static_cast<std::uint8_t>(_status.laserTemperature - 1u);
    }

    // 6502: LDX LASCT / BEQ NOLASCT / DEX / BEQ P%+3 / DEX / STX LASCT.
    if (_status.laserCount != 0u)
    {
      std::uint8_t left = static_cast<std::uint8_t>(_status.laserCount - 1u);
      if (left != 0u)
      {
        left = static_cast<std::uint8_t>(left - 1u);
      }
      _status.laserCount = left;
    }
  }

  /*
   * 6502: FRCE -- the main loop entered with a key already "pressed".
   *
   * Declared ahead of `Perform` because `BAY2` forces one, and `BAY2` is reached from inside two of
   * the actions `Perform` performs. The recursion is one level deep and cannot be more: the key it
   * forces is f9, and the Inventory screen forces nothing.
   */
  void PressKey(Game& _game, std::uint8_t _key);

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
      /*
       * ADR-006: the Market Prices screen, in place of `TT167`'s `PrintMarketScreen`.
       *
       * The ported routine is still in `Market.h` with its oracle test; this is the read-only
       * buy/sell listing the owner asked for, and it does its own TRADEMODE. `current.seeds` is
       * QQ2, which `DOENTRY` copies from the selected system on arrival, so the title names the
       * system the market belongs to.
       */
      Elite::MarketPricesScreen(_game.trade, _game.canvas, _game.current.seeds, _game.current.economy, _game.market, false);
      return;

    case Elite::KeyAction::BuyCargo:
      /*
       * ADR-006: the combined Buy Cargo screen, in place of `TT219`'s `BuyScreen`.
       *
       * The screen returns the position of the key that left it -- a screen key `TT102` acts on --
       * and the docked loop presses it on its next pass. That is `BAY2`'s `JMP FRCE` with two
       * differences, both deliberate: the key is the one the player pressed rather than a forced
       * f9, so leaving for the market goes to the market; and it is pressed by the loop rather
       * than from here, so pressing "1" on the buy screen redraws it without nesting.
       */
      _game.forcedKey = Elite::BuyCargoScreen(_game.trade, _game.shell, _game.canvas, _game.commander, _game.market, _game.current.economy,
                                              _game.dockedFlag);
      return;

    case Elite::KeyAction::SellCargo:
      Elite::ListCargo(_game.trade, _game.commander, _game.market, _game.current.economy, Elite::SELL_CARGO_VIEW);

      /*
       * 6502: TT212's `JSR dn2 / JMP BAY2` -- and only the beep is the screen's.
       *
       * `ListCargo` already makes it, on the exit that runs out of items and not on the one a letter
       * takes; that asymmetry is the original's and stays inside the screen. What is left for the
       * dispatch is the jump, and both exits share it.
       */
      PressKey(_game, Elite::KEY_INVENTORY);
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
    case Elite::KeyAction::LongRangeChart:
      // 6502: JMP TT22.
      ShowChart(_game, Elite::LONG_RANGE_CHART_VIEW);
      return;

    case Elite::KeyAction::ShortRangeChart:
      // 6502: JMP TT23.
      ShowChart(_game, Elite::SHORT_RANGE_CHART_VIEW);
      return;

    case Elite::KeyAction::HomeCrosshairs:
    {
      /*
       * 6502: TT103 / ping / TT103 -- erase the crosshairs, move them home, draw them again.
       *
       * It is a TAIL call and skips the countdown, which is the one path through `TT102`'s chart
       * half that does not reach `TT107` -- so this returns rather than falling through, exactly
       * as the dispatch's own comment says.
       */
      Elite::FlightScreen& screen = _game.flight.Screen();
      Elite::ChartView chart = ChartOf(_game);

      Elite::DrawTargetCrosshairs(_game.canvas, screen.draw, chart);
      Elite::CrosshairsToCurrentSystem(_game.commander, _game.crosshairX, _game.crosshairY);

      chart.cursorX = _game.crosshairX;
      chart.cursorY = _game.crosshairY;
      Elite::DrawTargetCrosshairs(_game.canvas, screen.draw, chart);
      return;
    }

    case Elite::KeyAction::MoveCrosshairs:
    {
      /*
       * 6502: ee2 -- JSR TT16, and then TT107.
       *
       * The steps are `TT17`'s, from the key LOGGER rather than from the key that was dispatched:
       * `TT102` is reached every pass of `MLOOP` with whatever `thiskey` holds, including nothing,
       * and it is the held cursor key that moves the crosshairs (§6.115). Zero on both axes is the
       * usual answer and `TT16` is called with it anyway, because that is what the original does.
       */
      Elite::FlightScreen& screen = _game.flight.Screen();
      Elite::ChartView chart = ChartOf(_game);

      Elite::MoveCrosshairs(_game.canvas, screen.draw, chart, _game.crosshairStep.x, _game.crosshairStep.y);

      _game.crosshairX = chart.cursorX;
      _game.crosshairY = chart.cursorY;
    }
      [[fallthrough]];

    case Elite::KeyAction::CountdownOnly:
    {
      /*
       * 6502: TT107 -- tick the hyperspace countdown, and it is TWO counters and one number.
       *
       * `QQ22+1` is what is on screen and `QQ22` is the tick within each of its steps, reset to
       * five every time it runs out. Every chart pass ends here, which is why the countdown keeps
       * running while you move the crosshairs.
       *
       * IT PRINTS THE NEW NUMBER AND THEN THE OLD ONE. `CHPR` draws by EOR, so printing the number
       * that is already there is what RUBS IT OUT -- the pair of calls is one update, and doing
       * them in the other order would leave the old digit on screen.
       */
      if (_game.status.hyperspaceCountdown == 0u)
      {
        return;
      }

      --_game.status.hyperspaceCounter; // 6502: DEC QQ22
      if (_game.status.hyperspaceCounter != 0u)
      {
        return;
      }

      Elite::PrintCountdown(_game.characters, _game.text, static_cast<std::uint8_t>(_game.status.hyperspaceCountdown - 1u));
      _game.status.hyperspaceCounter = 5u; // 6502: LDA #5 / STA QQ22
      Elite::PrintCountdown(_game.characters, _game.text, _game.status.hyperspaceCountdown);

      --_game.status.hyperspaceCountdown; // 6502: DEC QQ22+1

      /*
       * 6502: BNE t95 / JMP TT18 -- and `TT18` is the jump itself, which is phase 4's: it deducts
       * the fuel, copies the target over the commander's system and flies the tunnel. The counter
       * cannot reach zero in this build anyway, because `hyp` is what starts it and `hyp` is
       * refused below -- so this is a hole that nothing can fall into rather than one left open.
       */
      return;
    }

    /*
     * The rest belong to phases the port has not reached. They are listed rather than defaulted
     * so that adding a phase-4 screen is a compiler error here instead of a key that does nothing.
     *
     * `SearchBySystemName` is the one that is nearly here: `MT26` reads a line and is ported, and
     * what it still has no answer for is whose buffer the name goes into (§2e). `Hyperspace` is
     * `hyp`, which needs `TT18` and the tunnel behind it.
     */
    case Elite::KeyAction::Hyperspace:
    case Elite::KeyAction::ShowDistance:
    case Elite::KeyAction::SearchBySystemName:
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

      // 6502: the two `DEC`s at the top of `MLOOP`, which the flight loop falls into.
      CoolTheGuns(_game.status);

      /*
       * 6502: main game loop part 5 -- `LDA QQ11 / BNE P%+5 / JSR DIALS`, EVERY PASS.
       *
       * The dials were drawn in one place only, `ShowDashboard`'s `JSR DIALS`, which is the copy
       * `wantdials` makes when the dashboard first arrives as a picture with every bar empty. That
       * is the ONE-OFF, and the port had mistaken it for the whole of it: the speed, roll and dive
       * bars redrew when the view changed and never again, so a ship being flown had a dashboard
       * that was accurate at the moment it appeared and frozen from then on.
       *
       * The gate is the C64 build's own and not a guard invented here: `QQ11` non-zero is a chart
       * or a trading screen, and only the space view has a dashboard under it. It sits AFTER the
       * frame and BEFORE `TT17`, where part 5 puts it, because `DIALS` reads what the frame just
       * wrote -- drawing it first would show the previous frame's speed.
       *
       * `DIALS` is a redraw and not a tick: part 3's energy bars run one pass in four off `MCNT`,
       * and that counter is the flight loop's, so the four-pass cycle comes out of `M%` rather than
       * out of how often this is called.
       */
      Elite::FlightScreen& dashboard = _game.flight.Screen();
      if (dashboard.view == 0u)
      {
        Elite::DrawDials(dashboard.canvas, dashboard.draw, dashboard.math, dashboard.geometry, dashboard.flight, dashboard.status,
                         dashboard.commander.At(Elite::Field::Fuel), dashboard.compass, dashboard.bubble);
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
      (void)Elite::ScanFlightControls(_game.flight.Loop(), _game.flight, _game.flight.Screen().view);

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
    double dockedLeftover = 0.0;
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

        /*
         * 6502: MLOOP's tail -- `JSR TT17` and then `TT102`, EVERY PASS and not only when a key
         * was pressed.
         *
         * The port dispatched on key EVENTS, which is right for every docked screen except the two
         * that read the keyboard as a state: a chart moves its crosshairs while a cursor key is
         * HELD, and `TT102` reaches that through `TT17`'s X and Y rather than through the key it
         * was handed. So the loop now does what `MLOOP` does -- scan, then dispatch whatever
         * `thiskey` is, including nothing -- and `ActionForKey`'s own fall-through turns a pass
         * with no key into `MoveCrosshairs` on a chart and `CountdownOnly` everywhere else (§6.115).
         *
         * THE PASSES ARE PACED, for §6.114's reason one screen further on: a pass per PRESENT is
         * 165 crosshair steps a second on this display. `MLOOP`'s docked pass ends in a jump to
         * whichever screen the key chose, so it cannot be timed the way a flight frame was -- the
         * flight frame's empty-bubble cost is used instead, and it is a floor rather than a
         * measurement, because a docked pass draws no ships and is cheaper than that.
         */
        const Outpost::StepPlan docked = Outpost::PlanSteps(elapsed, dockedLeftover, 1.0 / Outpost::FlightFrameSeconds(0));
        dockedLeftover = docked.leftoverSeconds;

        for (int pass = 0; pass < docked.steps; ++pass)
        {
          // 6502: MLOOP's head, which a docked pass reaches too -- the gate below it is what is
          // about the space view, not these.
          CoolTheGuns(game->status);

          game->crosshairStep = Elite::ScanFlightControls(game->flight.Loop(), game->flight, game->view);

          std::uint8_t key = 0;
          if (game->forcedKey != 0u)
          {
            // 6502: FRCE -- the key the Buy Cargo screen was left with is pressed before the queue
            // is read, and once (ADR-006).
            key = game->forcedKey;
            game->forcedKey = 0;
          }
          else
          {
            (void)game->window.TakeKey(key); // 6502: `thiskey`, which is zero when nothing is held
          }
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
