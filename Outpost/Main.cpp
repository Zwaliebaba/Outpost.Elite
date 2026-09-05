#include "pch.h"

#include "CanvasPresenter.h"
#include "FlightSession.h"
#include "KeyMap.h"
#include "Presentation.h"
#include "SaveStore.h"
#include "Shell.h"
#include "SoundOutput.h"
#include "Window.h"

#include "Canvas.h"
#include "Commander.h"
#include "DockedKeys.h"
#include "Docking.h"
#include "Equipment.h"
#include "ExtendedTokens.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "GameLoop.h"
#include "Hyperspace.h"
#include "LoaderScreen.h"
#include "Market.h"
#include "MarketScreen.h"
#include "Music.h"
#include "PauseScreen.h"
#include "Rng.h"
#include "SaveGame.h"
#include "SoundEffects.h"
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
 * HYPERSPACE IS NO LONGER REFUSED. `hyp` starts the countdown, `CountdownOnly` spends it and calls
 * `TT18`, and an arrival ends in the launch the original falls through to (slices 4c-b and 4c-d).
 * What is still refused is `ShowDistance` and `SearchBySystemName`, and they are listed by name in
 * `Perform` rather than defaulted so that adding one is a compiler error here.
 *
 * THE GALACTIC DRIVE IS REACHABLE, and what unblocked it was reading `CTRL` rather than
 * reasoning about it. This comment used to say Ctrl was a MODIFIER that `Window` and `KeyMap`
 * could not report because they deliver matrix positions -- but `CTRL` is `LDX #6` falling into
 * `DKS4`, so it IS a matrix position, number 6, and the seam had been able to express it since
 * slice 2e. `JumpOf` reads it and Ctrl-H takes the drive (slices 4c-b and 4c-d built the rest).
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
        values(recursive, text, commander, name, current.seeds, selectedSeeds, false),
        extended(characters, recursive, rng, &shell),
        trade{recursive, characters, extended, text, shell, shell, rng},
        save{recursive, characters, extended, screen, text, shell, shell, store, numbers},
        flight(window, canvas, text, characters, recursive, message, commander, rng, status, view, explosionCount, current.techLevel, sound,
               music, audio)
    {
      recursive.SetValueTokens(&values);
      recursive.SetCursor(&text);
      shell.Attach(recursive, text, characters.state, message);
      shell.AttachExtended(extended);
      shell.AttachFlight(flight, dockedFlag);
      shell.AttachVideo(flight.Video()); // ADR-005 §1 -- the sprites composite in Resolve
      shell.AttachSound(audio, sound, music);

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
     * 6502: the sound variables, the music player and the SID they write.
     *
     * All three are here rather than in the flight session because BOTH halves of the loop make
     * sound: the docked screens beep and the title screen starts the theme through the shell, and
     * the flight loop fires lasers through the session. The output is the platform's and is the one
     * object in this struct that can fail to open, in which case the game runs in silence.
     */
    Elite::SoundBuffer sound;
    Elite::MusicPlayer music;
    Outpost::SoundOutput audio;

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
    std::uint8_t useDisk = 0;

    /*
     * 6502: QQ15 and QQ2 -- the selected system's seeds, and the current system's inside `current`.
     *
     * THERE WAS A SECOND COPY OF QQ2 HERE, and it is gone (§6.140). The token printer was bound to
     * it and the start sequence wrote the other one, so the status screen's "Present System" was
     * blank from the cold start until the first hyperspace jump, which was the one path that copied
     * across. `current.seeds` is the byte; the printer reads it directly.
     */
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
     * 6502: safehouse -- the seeds of the system the countdown is running towards.
     *
     * Separate from `selectedSeeds` (`QQ15`) because the player keeps moving the crosshairs while
     * the countdown runs, and `TT18` arrives at what was chosen when the key was pressed rather
     * than at whatever is under the crosshairs when it expires. `QQ8` is here for the same reason:
     * `hyp` measures the distance once and `TT18` spends that much fuel.
     */
    Elite::SystemSeeds jumpTarget{};
    std::uint16_t jumpDistance = 0;

    /*
     * 6502: DK4's `CPX #&40 / BNE DK2` -- and the frozen state it leaves behind.
     *
     * The original does not have this byte: it FREEZES, in a loop that reads the keyboard and does
     * not return until CLR/HOME. A windowed program cannot stop pumping messages, so the freeze is
     * a state the outer loop is in rather than a loop inside it -- which is the same trade
     * `PlanSteps` makes for the frame rate (ADR-005 §3).
     */
    bool paused = false;

    /*
     * 6502: JSTGY and JSTE -- two of the thirteen that NOTHING ELSE IN THE PORT READS.
     *
     * They are the joystick's y-inversion and its enable, and the flight controls read `JSTK` for
     * both. They are here because `DKS3` walks a contiguous run and the run is thirteen long: a
     * port that left them out would shift every option after them by two, and the "D" key would
     * switch the music instead of the disk.
     */
    std::uint8_t joystickGeometry = 0;
    std::uint8_t joystickEnabled = 0;

    /// 6502: MUTOKOLD -- what `MUTOKCH` saw last, which is how it notices the switch moving.
    std::uint8_t musicSwitchWas = 0;

    /// 6502: DNOIZ -- non-zero disables the sound, and the pause screen stores the KEY CODE in it.
    std::uint8_t soundDisabled = 0;

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
   * 6502: DAMP through MUSILLY -- the thirteen configuration bytes, in the assembler's order.
   *
   * THE ORDER IS THE ONLY DEFINITION THERE IS of which key toggles which option (§6.139), so this
   * function is the whole of the port's statement of it and `TheTogglesMatchDKS3` is what proves
   * the statement right. Six of the thirteen live in structs that other slices own, which is why
   * this is pointers rather than a struct of its own: making them contiguous would touch
   * eighty-seven call sites to buy what a sweep already establishes.
   */
  [[nodiscard]] Elite::OptionBlock OptionsOf(Game& _game)
  {
    Elite::ControlOptions& controls = _game.flight.Loop().options;
    Elite::MusicOptions& music = _game.music.options;

    return Elite::OptionBlock{
      &controls.dampingDisabled,          // 6502: DAMP
      &controls.recentreDisabled,         // 6502: DJD
      &controls.authorNames,              // 6502: PATG
      &_game.status.damageFlash,          // 6502: FLH
      &_game.joystickGeometry,            // 6502: JSTGY
      &_game.joystickEnabled,             // 6502: JSTE
      &controls.joystick,                 // 6502: JSTK
      &music.dockingMusicOff,             // 6502: MUTOK
      &_game.useDisk,                     // 6502: DISK
      &_game.flight.Screen().heaps.pltog, // 6502: PLTOG
      &music.dockingMusicForced,          // 6502: MUFOR
      &music.dockingPlaysTheme,           // 6502: MUDOCK
      &music.effectsDuringMusic,          // 6502: MUSILLY
    };
  }

  /*
   * 6502: QQ12, QQ22, QQ8 and safehouse -- what `hyp` and `TT18` read besides the chart.
   *
   * Built here rather than held as a member for the reason `ChartOf` is: the bytes belong to the
   * commander and the dashboard and this is the argument list the two routines want.
   */
  [[nodiscard]] Elite::JumpState JumpOf(Game& _game)
  {
    Elite::JumpState jump;
    jump.docked = _game.dockedFlag;
    jump.countdown = _game.status.hyperspaceCountdown;
    jump.distance = _game.jumpDistance;
    // 6502: JSR CTRL -- key-logger entry 6, read LIVE, because that is when the original reads it.
    jump.controlHeld = _game.window.Held(static_cast<std::uint8_t>(Elite::KEY_CONTROL));
    jump.target = _game.jumpTarget;
    return jump;
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
   * actions that need phase 4 are refused rather than silently ignored -- a game that did nothing
   * for the hyperspace key would look exactly like one that had wired it up.
   */
  /*
   * 6502: FRCE -- the main loop entered with a key already "pressed".
   *
   * Declared ahead of `Perform` because `BAY2` forces one, and `BAY2` is reached from inside two of
   * the actions `Perform` performs. The recursion is one level deep and cannot be more: the key it
   * forces is f9, and the Inventory screen forces nothing.
   */
  void PressKey(Game& _game, std::uint8_t _key);

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

      /*
       * 6502: BAY2 -- LDA #f9 / JMP FRCE, and the screen reaches it BOTH ways out. A letter gets
       * there through gnum's `CMP #10 / BCS BAY2`; the seventeenth item gets there through TT222's
       * `LDA QQ29 / CMP #17 / BCS BAY2`. There is no third exit, which is why this is unconditional.
       *
       * `BuyScreen` returns for both rather than jumping, because BAY2 is the DISPATCH'S and the
       * dispatch is here. Without it the buy screen stays on the display after a cancel, so the
       * letter key looks dead when it has done exactly what the original does (§6.128, §6.140).
       *
       * It forces a KEY rather than performing the action, because FRCE is entered with a key and
       * lets TT102 decide again -- so cancelling out of a purchase goes down the same path as
       * pressing "9", rather than down a second one that happens to agree today.
       */
      PressKey(_game, Elite::KEY_INVENTORY);
      return;

    case Elite::KeyAction::SellCargo:
      Elite::ListCargo(_game.trade, _game.commander, _game.market, _game.current.economy, Elite::SELL_CARGO_VIEW);

      /*
       * 6502: TT212's `JSR dn2 / JMP BAY2` -- and only the beep is the screen's.
       *
       * `ListCargo` already makes it, on the exit that runs out of items and not on the one a letter
       * takes; that asymmetry is the original's and stays inside the screen. What is left for the
       * dispatch is the jump, and both exits share it (§6.128, §6.140).
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
       * 6502: BNE t95 / JMP TT18 -- the jump itself, and slice 4c-b is what put it within reach.
       *
       * `hyp` above starts the countdown and this is where it expires, which is why the player can
       * keep flying while it runs. `PerformJump` says which of its four ends it reached; the launch
       * is the caller's on the one that arrived, exactly as `TT18`'s fall-through into `TT110` is.
       */
      if (_game.status.hyperspaceCountdown != 0u)
      {
        return; // 6502: BNE t95
      }

      {
        Elite::JumpState jump = JumpOf(_game);
        Elite::SystemData described;
        described.economy = _game.current.economy;
        described.government = _game.current.government;
        described.techLevel = _game.current.techLevel;

        const Elite::JumpResult jumped = Elite::PerformJump(
          _game.flight.Loop(), _game.current, _game.selectedSeeds, jump, described, _game.market, _game.flight, nullptr, _game.crosshairX,
          _game.crosshairY, _game.commander.GalaxySeeds(), _game.window.Held(static_cast<std::uint8_t>(Elite::KEY_CONTROL)),
          _game.flight.Loop().options.authorNames != 0u);

        _game.jumpDistance = jump.distance;

        if (jumped == Elite::JumpResult::Arrived)
        {
          // 6502: the fall-through into `TT110`, which is the launch the arrival ends with.
          Elite::Launch(_game.flight.Loop(), nullptr, _game.dockedFlag, _game.crosshairX, _game.crosshairY, _game.current.techLevel,
                        _game.selectedSeeds);
        }
      }
      return;
    }

    /*
     * 6502: hyp -- decide whether the jump can happen, and start the countdown if it can.
     *
     * `TT18` is not called from here. `hyp` prints the target's name and sets `QQ22`, and the jump
     * itself happens when the countdown reaches zero in `CountdownOnly` above -- which is why the
     * player can keep flying, or moving the crosshairs, while it runs. What this does take from
     * `hyp` is the target: `safehouse` is written once, here, so that moving the crosshairs
     * afterwards changes where you are LOOKING and not where you are going.
     */
    case Elite::KeyAction::Hyperspace:
    {
      Elite::ChartView chart = ChartOf(_game);
      Elite::JumpState jump = JumpOf(_game);
      Elite::FlightScreen& screen = _game.flight.Screen();

      const Elite::JumpOutcome decided = Elite::RequestHyperspace(_game.canvas, screen.draw, _game.recursive, _game.extended, _game.text,
                                                                  chart, jump, _game.commander.GalaxySeeds(), &_game.shell);

      _game.status.hyperspaceCountdown = jump.countdown;
      _game.jumpDistance = jump.distance;
      _game.jumpTarget = jump.target;
      _game.crosshairX = chart.cursorX;
      _game.crosshairY = chart.cursorY;

      /*
       * 6502: Ghy -- reached by `hyp`'s `JSR CTRL / BMI Ghy`, which `JumpOf` now answers from the
       * held-key table. `CTRL` reads key-logger entry 6, so Ctrl-H fits the map the game already
       * has; it was believed to be a modifier the seam could not carry, and was not.
       */
      if (decided == Elite::JumpOutcome::Galactic)
      {
        /*
         * 6502: QQ21 -- and `Ghy` ROTATES the six galaxy seeds in place, so they cannot be passed
         * by value. `GalaxySeeds()` reads them out of the commander block; the six bytes go back
         * one at a time afterwards, because the block is the storage and `SystemSeeds` is a view
         * of it.
         */
        Elite::SystemSeeds galaxy = _game.commander.GalaxySeeds();
        Elite::GalacticJump(_game.flight.Loop(), _game.current, galaxy, _game.selectedSeeds, jump, chart, nullptr);

        for (int byte = 0; byte < 6; ++byte)
        {
          _game.commander.At(static_cast<Elite::Field>(static_cast<int>(Elite::Field::GalaxySeeds) + byte)) =
            galaxy.bytes[static_cast<std::size_t>(byte)];
        }

        _game.status.hyperspaceCountdown = jump.countdown;
        _game.jumpTarget = jump.target;
        _game.jumpDistance = jump.distance;
        _game.crosshairX = chart.cursorX;
        _game.crosshairY = chart.cursorY;
      }
      return;
    }

    /*
     * The rest belong to phases the port has not reached. They are listed rather than defaulted
     * so that adding a phase-4 screen is a compiler error here instead of a key that does nothing.
     *
     * `SearchBySystemName` is the one that is nearly here: `MT26` reads a line and is ported, and
     * what it still has no answer for is whose buffer the name goes into (§2e).
     */
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
       * 6502: DEATH, then DEATH2 -- and the port now takes both.
       *
       * `DEATH` is built (§6.117), so what a player sees on dying is the sequence rather than an
       * immediate restart: the sound, FOUR TIMES the speed, the border rubbed off with its own
       * EOR, a new stardust field, "GAME OVER", five pieces of wreckage and sixty-four iterations
       * of the flight loop to fly them past. `DEATH2` is the tail -- `JSR RES2` and a fall into
       * `BR1` -- which this already did and still does.
       *
       * It said "a quarter-turn of the speed" until 2026-09-05, as did `Flight.h`. `ASL DELTA`
       * twice is a multiply, the port and its test have always had it right, and the debris
       * rushing past at four times your last speed is exactly what the sequence looks like.
       *
       * Neither routine restores the energy banks. That is the game's behaviour and not an
       * omission here: `RESET` fills them and only the COLD start calls it (ADR-003).
       */
      {
        Elite::Die(_game.flight.Loop(), _game.flight);

        _game.shell.ResetShip();

        Elite::GameStart restart = StartOf(_game);
        const Elite::ForcedKey begun = Elite::StartGame(restart);
        Perform(_game, begun.outcome);
      }
      return;

    case Elite::LoopOutcome::Escaped:
    {
      /*
       * 6502: ESCAPE -- built in slice 4b-a, and this is the last of the three jumps that leave
       * `M%` to be wired (§6.82 named all three; `DOENTRY` and `DEATH` have been wired since 3d).
       *
       * The routine ends `JMP GOIN`, which is the docking -- so the arrival is the caller's, the
       * way `TT18`'s fall into `TT110` was. A default commander cannot reach here at all: `KY13` is
       * ANDed with `ESCP`, so it needs one that has bought a pod.
       */
      Elite::AbandonShip(_game.flight.Loop(), _game.commander.At(Elite::Field::Fuel));

      // 6502: JMP GOIN -- `stopbd` and then `DOENTRY`, which is the arrival slice 2d built.
      _game.flight.StopDockingMusic();
      Leave(_game, Elite::LoopOutcome::Docked);
      return;
    }

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
      const Elite::LoopOutcome outcome = Elite::MainFlightLoop(_game.flight.Loop()); // 6502: JSR M%
      if (outcome != Elite::LoopOutcome::Continued)
      {
        Leave(_game, outcome);
        return;
      }

      /*
       * 6502: the rest of `TT100`, then `MLOOP` -- and until slice 4c-d none of it was here.
       *
       * `RunLoopHead` is the message countdown and `DEC MCNT`; the spawner (slice 4c-a) runs ONE
       * PASS IN 256, when that counter reaches zero, which is the difference between a bubble that
       * fills at the game's rate and one that fills 256 times too fast; and `RunLoopTail` is part
       * 5, which cools the laser, redraws the dials every pass and breeds the Trumbles. §6.138 is
       * why all three are functions with sweeps behind them rather than fragments transcribed here.
       */
      Elite::FlightLoop& loop = _game.flight.Loop();

      if (Elite::RunLoopHead(loop, _game.shell) == Elite::LoopHead::Spawn)
      {
        Elite::RunSpawning(loop.screen.bubble, loop.screen.work, loop.screen.rng, _game.commander, _game.current, _game.status,
                           _game.explosionCount, loop.screen.flight.blueprint, false);
      }

      /*
       * The frames part 5 asks to wait for are DROPPED here, and saying so is better than pretending
       * otherwise. `JSR DELAY` is two vertical syncs on a docked screen, and this is the FLIGHT
       * pass -- `QQ11` is zero on every call that reaches here, so the option's branch is never the
       * one that waits. The docked loop below is where it would matter, and that loop is paced by
       * `PlanSteps` rather than by vsync counts (ADR-005 §3).
       */
      static_cast<void>(Elite::RunLoopTail(loop, _game.commander, loop.options.authorNames, false));

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
        /*
         * 6502: `DOKEY` FALLS INTO `DK4`, which the port has never followed -- `Controls.cpp` says
         * so in a comment and slice 4e is what answers it. `CPX #&40 / BNE DK2`: the pause key
         * freezes the game and everything else carries on to the dispatch.
         */
        if (key == Elite::PAUSE_KEY)
        {
          _game.paused = true;
          return;
        }

        PressKey(_game, key);
      }
    }
  }

  /*
   * 6502: FREEZE -- the loop the game is in while it is paused, one pass per key.
   *
   * The original does not return until CLR/HOME and reads the keyboard itself. A windowed program
   * has to keep pumping messages, so the loop is turned inside out: this is called instead of
   * `Advance` while `paused` is set, and each key the window delivers is one pass round `FREEZE`.
   * Nothing is drawn and nothing moves, which is what freezing is.
   */
  void AdvancePaused(Game& _game)
  {
    std::uint8_t key = 0;
    if (!_game.window.TakeKey(key))
    {
      return;
    }

    const Elite::PausePass pass =
      Elite::PressPauseKey(OptionsOf(_game), _game.soundDisabled, _game.musicSwitchWas, _game.flight.Loop().control.dockingComputer, key);

    /*
     * 6502: JSR MUTOKCH -- the music is phase 5's, and this is the seam it reaches through. The
     * `Stop` answer goes through `stopbd`, which starts the music again when `MUFOR` is set, so
     * the two answers are not "on" and "off" -- they are "start it now" and "ask `stopbd`".
     */
    if (pass.music == Elite::MusicChange::StartNow)
    {
      Elite::StartDockingMusicNow(_game.music, _game.audio.Direct());
    }
    else if (pass.music == Elite::MusicChange::Stop)
    {
      Elite::StopDockingMusic(_game.music, _game.flight.Loop().screen.status.titleReset, _game.sound, _game.audio.Direct());
    }

    /*
     * The twenty frames per toggle are DROPPED, and saying so is better than pretending. `JSR
     * DELAY` is there to stop one key press flipping a switch twenty times while the player holds
     * it; this loop is driven by key EVENTS from the window, which repeat at the system's rate and
     * not at the frame's, so the debounce the delay provides is already there.
     */
    static_cast<void>(pass.delayFrames);

    if (pass.outcome == Elite::PauseOutcome::Resumed)
    {
      _game.paused = false; // 6502: CPX #&0D -- and `DK2`'s `RTS`
    }
    else if (pass.outcome == Elite::PauseOutcome::Quit)
    {
      // 6502: CPX #&07 / JMP DEATH2 -- which does not come back, so neither does the pause.
      _game.paused = false;
      Leave(_game, Elite::LoopOutcome::Died);
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

      /*
       * 6502: FREEZE -- and it comes FIRST, because a frozen game is frozen in both halves.
       *
       * `DK4` is reached from `DOKEY`, which the flight loop calls, so the pause key is a flight
       * key; but what `FREEZE` does is refuse to return, and the docked loop cannot run while it
       * is refusing either. One test above both halves is what that shape becomes here.
       */
      if (game->paused)
      {
        AdvancePaused(*game);
        continue;
      }

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
          /*
           * 6502: MLOOP's head, which a docked pass reaches too -- the two countdowns sit ABOVE
           * part 5's `LDA QQ11` gate, and everything below it is about the space view.
           *
           * It is `Elite::CoolTheGuns` and not a copy here: `RunLoopTail` runs the same function on
           * a flying pass, so the arithmetic has one home (§6.146). What a docked pass still does
           * NOT run is the REST of part 5 -- the author-names delay and the Trumble breeding, both
           * of which the original reaches while docked. That is a gap this merge did not create and
           * does not close; it needs `RunLoopTail`'s frame count plumbed into the docked pace, which
           * is slice 4d's neighbourhood rather than a merge's.
           */
          Elite::CoolTheGuns(game->status);

          game->crosshairStep = Elite::ScanFlightControls(game->flight.Loop(), game->flight, game->view);

          std::uint8_t key = 0;
          (void)game->window.TakeKey(key); // 6502: `thiskey`, which is zero when nothing is held
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
