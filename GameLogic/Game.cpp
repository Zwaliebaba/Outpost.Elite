#include "pch.h"

#include "Game.h"

#include "StateHash.h"

#include "Canvas.h"
#include "Commander.h"
#include "Docking.h"
#include "Equipment.h"
#include "Flight.h"
#include "Galaxy.h"
#include "GameLoop.h"
#include "LoaderScreen.h"
#include "Market.h"
#include "MarketScreen.h"
#include "Missions.h"
#include "Music.h"
#include "NameEntry.h"
#include "Rng.h"
#include "SaveGame.h"
#include "SoundEffects.h"
#include "StartUp.h"
#include "StatusScreen.h"
#include "SystemScreen.h"
#include "TextPrint2x.h"
#include "ViewChange.h"

namespace Elite
{

  /*
   * The construction order is load-bearing and is why the members are grouped by what they depend
   * on rather than by what they are: the character printer needs the sink, the token printer needs
   * the character printer, the state tokens need the token printer AND the commander, and the token
   * printer needs the state tokens back -- which is the cycle `SetValueTokens` exists to break.
   */
  Game::Game(Presenter& _present, Keyboard& _keyboard, CommanderStore& _store) noexcept
    : m_screen(m_universe.canvas, m_universe.text, &m_universe.sound),
      m_characters(m_screen, m_universe.sentences),
      m_recursive(m_characters, m_universe.text),
      m_values(m_recursive, m_universe.text, m_universe.commander, m_universe.commanderName, m_universe.current.seeds,
               m_universe.selectedSeeds, false),
      m_extended(m_characters, m_recursive, m_universe.rng),
      m_ports{m_recursive, m_characters, m_characters, m_sid, m_extended, _present, _keyboard, _store}
  {
    // Resolution.md RS-1: the printer draws the 640x400 surface beside the canvas, and reads `QQ11`
    // for the layout that says where. Attached here because this is where both first exist.
    m_screen.AttachPicture(&m_universe.picture, &m_universe.screenLayout);

    m_recursive.SetValueTokens(&m_values);
    m_extended.SetGame(m_universe, m_ports); // 6502: DT3 -- a control code that leaves is the library's

    // 6502: NA% -- the commander the cold start begins from, and a STORE rather than a member
    // initialiser: `Universe::commander` is default-constructed, so moving the byte without the
    // initialisation would have started the game with no credits and no laser.
    m_universe.commander = DefaultCommander();
    m_universe.commanderName = DefaultCommanderName();

    // 6502: DTW2 -- the extended printer starts between sentences, which is what the first capital
    // letter of the first screen depends on.
    m_universe.sentences.sentenceStart = 0xFF;
  }

  std::uint64_t Game::StateHash() const noexcept
  {
    return HashState(m_universe);
  }

  std::uint8_t Game::ShipsInBubble() const noexcept
  {
    std::uint8_t ships = 0;
    for (const std::uint8_t type : m_universe.bubble.slots)
    {
      // 6502: `FRIN`'s zero is the list's terminator, not a hole in it.
      if (type == 0u)
      {
        break;
      }
      ++ships;
    }
    return ships;
  }

  void Game::Reset() noexcept
  {
    /*
     * 6502: the Elite loader's parts 5 and 6 -- the colours the game is drawn in.
     *
     * BEFORE ANYTHING ELSE, because everything else assumes it. Screen RAM and colour RAM are not
     * the game's to fill: the loader fills them, once, and the game then writes bits into a bitmap
     * whose palette is already decided cell by cell. Start without it and every routine below
     * draws exactly what it should and the screen stays black -- the border box, the dashboard
     * picture and all seven dials included.
     */
    SetUpLoaderScreen(m_universe.canvas, &m_universe.picture);

    // 6502: NA% -- the commander the disk menu's "load" compares against, and the one SVE writes.
    SaveCommander(m_universe.commander, m_universe.commanderName, m_universe.commanderFile);

    // 6502: TT170 -- the cold start. It ends by pressing "8" for the player and entering the docked
    // half of the main loop, which is why there is no separate "draw the first screen" step.
    const ForcedKey begun = ResetAndStartGame(m_universe, m_ports, false);
    SettleJoystick();
    if (begun.loop == MainLoop::Docked)
    {
      // 6502: the market is rolled on arrival rather than by the start sequence, and the market
      // screen reads it -- so a game that skipped this would print a table of zeroes.
      GenerateMarket(m_universe.rng, m_universe.current.economy, m_universe.market);

      // 6502: BAY forces "8" and TT102 has already dispatched it, so this PERFORMS that outcome
      // rather than deciding it again -- deciding twice would work today and stop working the
      // moment the dispatch depends on something the first decision changed.
      Perform(begun.outcome);
    }
  }

  /*
   * 6502: what a chart reads -- QQ9, QQ10, QQ0, QQ1, QQ11 and QQ14, gathered where they live.
   *
   * The crosshairs are the game's own two bytes and the home position is INSIDE the commander
   * block (§2e's finding: `QQ0` and `QQ1` are `TP+1` and `TP+2`), so this is a view onto four
   * different owners rather than a struct anybody keeps.
   */
  ChartView Game::ChartOf()
  {
    ChartView view;
    view.cursorX = m_universe.crosshairX;
    view.cursorY = m_universe.crosshairY;
    view.homeX = m_universe.commander.systemX;
    view.homeY = m_universe.commander.systemY;
    view.view = m_universe.view;
    view.fuel = m_universe.commander.fuel;
    return view;
  }

  /*
   * `OptionsOf` WAS HERE AND IS NOT ANY MORE (InputTimer.md I-0, 2026-09-08).
   *
   * 6502: DAMP through MUSILLY -- the thirteen configuration bytes in the assembler's order, which
   * was the only definition of which pause-screen key toggled which option (§6.139). The screen is
   * gone by owner ruling and the executable's settings file names the thirteen by field instead:
   * `options.dampingDisabled`, `.recentreDisabled`, `.authorNames`, `status.damageFlash`,
   * `joystickGeometry`, `joystickEnabled`, `options.joystick`, `music.options.dockingMusicOff`,
   * `useDisk`, `heaps.planetDetail`, `music.options.dockingMusicForced`, `.dockingPlaysTheme` and
   * `.effectsDuringMusic` (InputTimer.md S-1).
   */

  /*
   * 6502: QQ12, QQ22, QQ8 and safehouse -- what `hyp` and `TT18` read besides the chart.
   *
   * Built here rather than held as a member for the reason `ChartOf` is: the bytes belong to the
   * commander and the dashboard and this is the argument list the two routines want.
   */
  JumpState Game::JumpOf()
  {
    JumpState jump;
    jump.docked = m_universe.dockedFlag;
    jump.countdown = m_universe.status.hyperspaceCountdown;
    jump.counter = m_universe.status.hyperspaceCounter;
    jump.distance = m_universe.jumpDistance;
    // 6502: CTRL -- key-logger entry 6, read LIVE, because that is when the original reads it.
    jump.controlHeld = m_ports.keyboard.Held(KEY_CONTROL);
    jump.target = m_universe.jumpTarget;
    return jump;
  }

  /*
   * 6502: TT22 and TT23 -- draw whichever chart the view says.
   *
   * TT23 LIFTS THE CLIPPER'S LIMITS AND PUTS THEM BACK: both bytes pushed to 199 at the top and
   * put back to zero and the space view's floor at the bottom, because the
   * short-range chart draws system discs below the space view's floor. Both bytes belong to the
   * drawing rather than to the chart, which is why they are set here and not inside `TT23` (§6.45).
   */
  void Game::DrawChart()
  {
    const ChartView chart = ChartOf();
    Universe& universe = m_universe;

    if (universe.view == SHORT_RANGE_CHART_VIEW)
    {
      universe.heaps.lowestVisibleRow = CHART_SCREEN_BOTTOM;
      universe.clip.clippingOff = CHART_SCREEN_BOTTOM;

      DrawShortRangeChart(universe, m_ports, chart, universe.commander.galaxySeeds, &m_screen);

      universe.clip.clippingOff = 0u;
      universe.heaps.lowestVisibleRow = SPACE_VIEW_BOTTOM; // 6502: the space view's floor again
      return;
    }

    DrawLongRangeChart(universe, m_ports, chart, universe.commander.galaxySeeds);
  }

  /// 6502: TT22 and TT23's opening call to `TT66`, which the routines leave to their caller, and then
  /// the chart itself.
  void Game::ShowChart(std::uint8_t _view)
  {
    // Each chart's own layout for the wide surface: the long-range chart's title has to clear the
    // rule the map is drawn under, and the short-range chart's labels have to scale with the discs
    // they name (Resolution.md section 6.3, slice RS-5-e).
    SetUpScreen(m_universe, m_ports, _view,
                (_view == SHORT_RANGE_CHART_VIEW) ? SHORT_RANGE_LAYOUT : LONG_RANGE_LAYOUT);
    DrawChart();
  }

  /*
   * One key, and whatever screen it reaches.
   *
   * 6502: what `TT102` does with the label it chose. The dispatch itself is `ActionForKey`, which
   * is compared against the shipped routine over 16,384 states; this is the other half, and the
   * actions that need phase 4 are refused rather than silently ignored -- a game that did nothing
   * for the hyperspace key would look exactly like one that had wired it up.
   */
  void Game::Perform(const KeyOutcome& _outcome)
  {
    switch (_outcome.action)
    {
    case KeyAction::StatusMode:
    {
      /*
       * 6502: STATUS's condition -- the docked flag first, then the slot two past the junk count.
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
      const Bubble& bubble = m_universe.bubble;
      const std::size_t beyond = static_cast<std::size_t>(bubble.junk) + 2u;
      const ShipCondition condition{m_universe.dockedFlag, bubble.junk,
                                    (beyond < bubble.slots.size()) ? bubble.slots[beyond] : std::uint8_t{0}, m_universe.status.energy};
      StatusScreen(m_universe, m_ports, condition);
      return;
    }

    case KeyAction::DataOnSystem:
    {
      // 6502: TT111 then TT25 -- the screen reads what the search leaves behind.
      const NearestSystem found = FindNearestSystem(m_universe.commander.galaxySeeds, m_universe.crosshairX, m_universe.crosshairY,
                                                    m_universe.commander.systemX, m_universe.commander.systemY);
      m_universe.selectedSeeds = found.seeds;
      SystemDataScreen(m_universe, m_ports, found.data, found.distance);
      return;
    }

    case KeyAction::MarketPrice:
      // 6502: TT167's TRADEMODE -- TT66 and FLKB. The same table as the buy screen, because it is
      // the same table: `TT167` and `TT219` print one market list between them.
      SetUpTradeScreen(m_universe, m_ports, BUY_CARGO_VIEW, BUY_LAYOUT);
      PrintMarketScreen(m_recursive, m_characters, m_universe.text, m_universe.current.economy, m_universe.market, false);
      return;

    case KeyAction::BuyCargo:
      BuyScreen(m_universe, m_ports, false);

      /*
       * 6502: BAY2 -- the f9 key forced into the dispatch, and the screen reaches it BOTH ways
       * out. A letter gets there through `gnum`'s test against 10; the seventeenth item gets
       * there through `TT222`'s test against 17. There is no third exit, which is why this is
       * unconditional.
       *
       * `BuyScreen` returns for both rather than jumping, because BAY2 is the DISPATCH'S and the
       * dispatch is here. Without it the buy screen stays on the display after a cancel, so the
       * letter key looks dead when it has done exactly what the original does (§6.128, §6.140).
       *
       * It forces a KEY rather than performing the action, because FRCE is entered with a key and
       * lets TT102 decide again -- so cancelling out of a purchase goes down the same path as
       * pressing "9", rather than down a second one that happens to agree today.
       */
      PressKey(KEY_INVENTORY);
      return;

    case KeyAction::SellCargo:
      ListCargo(m_universe, m_ports, SELL_CARGO_VIEW);

      /*
       * 6502: TT212 calls `dn2` and then jumps to BAY2 -- and only the beep is the screen's.
       *
       * `ListCargo` already makes it, on the exit that runs out of items and not on the one a letter
       * takes; that asymmetry is the original's and stays inside the screen. What is left for the
       * dispatch is the jump, and both exits share it (§6.128, §6.140).
       */
      PressKey(KEY_INVENTORY);
      return;

    case KeyAction::Inventory:
      InventoryScreen(m_universe, m_ports);
      return;

    case KeyAction::EquipShip:
      EquipShipScreen(m_universe, m_ports);
      return;

    case KeyAction::DiskAccess:
    {
      const DiskMenuResult menu = DiskAccessMenu(m_universe, m_ports);
      // 6502: QU5 or BAY -- and QU5 is `DFAULT`, which installs the loaded image.
      if (menu.newCommander)
      {
        (void)LoadCommander(m_universe.commanderFile, m_universe.commander, m_universe.commanderName);
      }
      return;
    }

    case KeyAction::Launch:
      /*
       * 6502: TT110 -- and it is dispatched in BOTH halves of the loop, because `TT102` tests for
       * it ABOVE the docked/flight split. Pressing "1" docked leaves the station; pressing it in
   * flight falls through `TT110`'s own docked test and is the front view.
       *
       * `_selected` comes back written: the launch runs `TT111` for the SEEDS rather than for the
       * distance, because the planet's appearance is generated from the system you are leaving.
       */
      Launch(m_universe, m_ports, m_universe.crosshairX, m_universe.crosshairY, m_universe.selectedSeeds);
      return;

    case KeyAction::ChangeView:
      // 6502: LOOK1 with X = the view. The dispatch already decided which one through two `EQUB
      // &2C`s, so this performs the answer rather than reading the key again.
      ChangeView(m_universe, m_ports, _outcome.view);
      return;

    /*
     * The rest belong to phases the port has not reached. They are listed rather than defaulted
     * so that adding a phase-4 screen is a compiler error here instead of a key that does nothing.
     */
    case KeyAction::LongRangeChart:
      // 6502: TT22.
      ShowChart(LONG_RANGE_CHART_VIEW);
      return;

    case KeyAction::ShortRangeChart:
      // 6502: TT23.
      ShowChart(SHORT_RANGE_CHART_VIEW);
      return;

    case KeyAction::HomeCrosshairs:
    {
      /*
       * 6502: TT103 / ping / TT103 -- erase the crosshairs, move them home, draw them again.
       *
       * It is a TAIL call and skips the countdown, which is the one path through `TT102`'s chart
       * half that does not reach `TT107` -- so this returns rather than falling through, exactly
       * as the dispatch's own comment says.
       */
      ChartView chart = ChartOf();

      DrawTargetCrosshairs(m_universe.canvas, chart, &m_universe.picture);
      CrosshairsToCurrentSystem(m_universe);

      chart.cursorX = m_universe.crosshairX;
      chart.cursorY = m_universe.crosshairY;
      DrawTargetCrosshairs(m_universe.canvas, chart, &m_universe.picture);
      return;
    }

    case KeyAction::MoveCrosshairs:
    {
      /*
       * 6502: ee2 -- `TT16`, and then `TT107`.
       *
       * The steps are `TT17`'s, from the key LOGGER rather than from the key that was dispatched:
       * `TT102` is reached every pass of `MLOOP` with whatever `thiskey` holds, including nothing,
       * and it is the held cursor key that moves the crosshairs (§6.115). Zero on both axes is the
       * usual answer and `TT16` is called with it anyway, because that is what the original does.
       */
      ChartView chart = ChartOf();

      MoveCrosshairs(m_universe.canvas, chart, m_universe.crosshairStep.x, m_universe.crosshairStep.y, &m_universe.picture);

      m_universe.crosshairX = chart.cursorX;
      m_universe.crosshairY = chart.cursorY;
    }
      [[fallthrough]];

    case KeyAction::CountdownOnly:
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
      if (m_universe.status.hyperspaceCountdown == 0u)
      {
        return;
      }

      --m_universe.status.hyperspaceCounter; // 6502: QQ22 stepped down
      if (m_universe.status.hyperspaceCounter != 0u)
      {
        return;
      }

      PrintCountdown(m_characters, m_universe.text, static_cast<std::uint8_t>(m_universe.status.hyperspaceCountdown - 1u));
      m_universe.status.hyperspaceCounter = 5u; // 6502: five back into QQ22
      PrintCountdown(m_characters, m_universe.text, m_universe.status.hyperspaceCountdown);

      --m_universe.status.hyperspaceCountdown; // 6502: QQ22+1 stepped down

      /*
       * 6502: t95, or TT18 -- the jump itself, and slice 4c-b is what put it within reach.
       *
       * `hyp` above starts the countdown and this is where it expires, which is why the player can
       * keep flying while it runs. `PerformJump` says which of its four ends it reached; the launch
       * is the caller's on the one that arrived, exactly as `TT18`'s fall-through into `TT110` is.
       */
      if (m_universe.status.hyperspaceCountdown != 0u)
      {
        return; // 6502: BNE t95
      }

      {
        JumpState jump = JumpOf();
        SystemData described;
        described.economy = m_universe.current.economy;
        described.government = m_universe.current.government;
        described.techLevel = m_universe.current.techLevel;

        const JumpResult jumped = PerformJump(m_universe, m_ports, m_universe.selectedSeeds, jump, described, m_universe.market,
                                              m_universe.crosshairX, m_universe.crosshairY, m_universe.commander.galaxySeeds,
                                              m_ports.keyboard.Held(KEY_CONTROL), m_universe.options.authorNames != 0u);

        m_universe.jumpDistance = jump.distance;

        if (jumped == JumpResult::Arrived)
        {
          // 6502: the fall-through into `TT110`, which is the launch the arrival ends with.
          Launch(m_universe, m_ports, m_universe.crosshairX, m_universe.crosshairY, m_universe.selectedSeeds);
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
    case KeyAction::Hyperspace:
    {
      ChartView chart = ChartOf();
      JumpState jump = JumpOf();

      const JumpOutcome decided = RequestHyperspace(m_universe.canvas, m_recursive, m_extended, m_universe.text, m_universe.sentences,
                                                    m_universe.message, chart, jump, m_universe.commander.galaxySeeds,
                                                    &m_universe.picture);

      m_universe.status.hyperspaceCountdown = jump.countdown;
      m_universe.status.hyperspaceCounter = jump.counter; // 6502: into QQ22 -- and it was never copied back (§6.159)
      m_universe.jumpDistance = jump.distance;
      m_universe.jumpTarget = jump.target;
      m_universe.crosshairX = chart.cursorX;
      m_universe.crosshairY = chart.cursorY;

      /*
       * 6502: Ghy -- reached by `hyp`'s test of `CTRL`, which `JumpOf` now answers from the
       * held-key table. `CTRL` reads key-logger entry 6, so Ctrl-H fits the map the game already
       * has; it was believed to be a modifier the seam could not carry, and was not.
       */
      if (decided == JumpOutcome::Galactic)
      {
        /*
         * 6502: QQ21 -- and `Ghy` ROTATES the six galaxy seeds in place, so they cannot be passed
         * by value. `GalaxySeeds()` reads them out of the commander block; the six bytes go back
         * one at a time afterwards, because the block is the storage and `SystemSeeds` is a view
         * of it.
         */
        SystemSeeds galaxy = m_universe.commander.galaxySeeds;
        GalacticJump(m_universe, m_ports, galaxy, m_universe.selectedSeeds, jump, chart);

        for (int byte = 0; byte < 6; ++byte)
        {
          m_universe.commander.galaxySeeds.bytes[byte] = galaxy.bytes[static_cast<std::size_t>(byte)];
        }

        m_universe.status.hyperspaceCountdown = jump.countdown;
        m_universe.status.hyperspaceCounter = jump.counter; // 6502: `Ghy` falls into `wW`, which stores QQ22 as well
        m_universe.jumpTarget = jump.target;
        m_universe.jumpDistance = jump.distance;
        m_universe.crosshairX = chart.cursorX;
        m_universe.crosshairY = chart.cursorY;
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
    case KeyAction::ShowDistance:
    case KeyAction::SearchBySystemName:
    case KeyAction::Nothing:
      return;
    }
  }

  /// 6502: TT102 -- decide, then do. The two are separate because the start sequence FORCES a key
  /// and hands back what the dispatch made of it, so it has already decided by the time it returns.
  void Game::PressKey(std::uint8_t _key)
  {
    /*
     * 6502: the dispatch tests whether H is HELD on the matrix, not whether H is the
     * key that arrived, and `RDKEY` has just filled the logger from the matrix in both loops. So it
     * is read live off the window here, the way `JumpOf` reads CTRL for the galactic drive.
     *
     * THIS WAS A CONSTANT FALSE until 2026-09-06, so no key the player pressed could ever reach
     * `hyp`: H arrived as key &23, the dispatch discarded it as the original does, and the flag that
     * should have carried it said nobody was holding anything (§6.159).
     */
    const bool hyperspaceHeld = m_ports.keyboard.Held(KEY_HYPERSPACE);
    Perform(ActionForKey(_key, m_universe.dockedFlag, m_universe.view, m_universe.status.hyperspaceCountdown, hyperspaceHeld));
  }

  /*
   * The original's `TITLE` ends by testing the fire key and stepping `JSTK` for anything else --
   * the fire key leaves `JSTK` set, and
   * that is the joystick question answered "yes" (Design/InputTimer.md §5.1, slice I-3).
   *
   * THE ROUTINE IS THE ORIGINAL'S AND STAYS SO; this runs AFTER it, outside anything the oracle
   * compares. The port has no CIA port A to read a stick from, so a `JSTK` the platform cannot
   * honour put `DOKEY` into its joystick branch -- both rates snapped to centre whenever their keys
   * were up, and the damping a keyboard player gets never ran (InputTimer.md I-4). Until the
   * platform answers `HasJoystick`, the fire key on the title screen is a key like any other; when
   * it does, nothing here runs and the original's rule returns unchanged. The byte itself stays in
   * `Universe` and in the digest.
   */
  void Game::SettleJoystick() noexcept
  {
    if (!m_ports.keyboard.HasJoystick())
    {
      m_universe.options.joystick = 0u; // JSTK: keyboard
    }
  }

  /*
   * 6502: the six jump targets `DOENTRY` chooses between, and `EN6`'s jump to `BAY`.
   *
   * A function rather than six lines in the switch because `BRIEF` needs the briefing ship's slot
   * carried into the control code that spins it, and that is one line the other five do not have.
   */
  ForcedKey Game::MissionOf(DockingOutcome _outcome)
  {
    Universe& universe = m_universe;
    Ports& ports = m_ports;

    switch (_outcome)
    {
    case DockingOutcome::BriefMission1:
    {
      /*
       * `BRIEF` creates the Constrictor and then prints a token containing two `{22}`s, which spin
       * it -- so the slot has to reach the control code. It travels in `Universe::shipSlot` since
       * M3-a, where it went out through the shell and came back in.
       */
      const std::uint8_t token = RunConstrictorBriefing(universe, ports, false);
      return PrintAndEnterBay(universe, ports, false, token);
    }

    case DockingOutcome::DebriefMission1:
      return DebriefMission1(universe, ports, false);
    case DockingOutcome::BriefMission2:
      return BriefMission2(universe, ports, false);
    case DockingOutcome::CollectPlans:
      return CollectPlans(universe, ports, false);
    case DockingOutcome::DebriefMission2:
      return DebriefMission2(universe, ports, false);
    case DockingOutcome::OfferTrumbles:
      return OfferTrumble(universe, ports, false, m_ports.keyboard);

    case DockingOutcome::DockingBay:
    default:
      // 6502: EN6 -- straight to `BAY`, and nothing happened.
      return EnterDockingBay(universe, universe.view, universe.status.hyperspaceCountdown, false);
    }
  }

  /*
   * 6502: the three jumps that leave `M%` and do not come back -- into `DOENTRY`, `DEATH` and
   * `ESCAPE` (§6.82).
   *
   * The port hands them back as a `LoopOutcome` because none of them returns; this is where the
   * jump is actually taken. Two of the three are wired and one is refused, and which is which is
   * decided by what exists rather than by what is convenient.
   */
  void Game::Leave(LoopOutcome _outcome)
  {
    switch (_outcome)
    {
    case LoopOutcome::Docked:
    {
      /*
       * 6502: DOENTRY -- ported in slice 2d, so this is the whole arrival.
       *
       * IT CANNOT BE REACHED TODAY. Part 9's docking check reads `SSPR`, and the only thing that
       * sets `SSPR` is `NWSPS`, which is phase 4's and is the stub in `FlightSession`. The wiring
       * is here anyway because the routine is built and the alternative is a hole that looks like
       * a decision.
       */
      const DockingResult arrival = DockAtStation(m_universe, m_ports, m_universe.view, false);

      /*
       * 6502: the seven exits, and six of them are a briefing (slice 4d-c).
       *
       * `EN6` goes straight to `BAY` and the other six are tail calls into `BRIEF`, `DEBRIEF`,
       * `BRIEF2`,
       * `BRIEF3`, `DEBRIEF2` and `TBRIEF`, each of which ends at `BAY` in its own turn. Until this
       * slice the port took the tail they share and skipped the briefings themselves, which is why
       * a docking that had earned one went straight to the status screen.
       */
      const ForcedKey bay = MissionOf(arrival.outcome);
      Perform(bay.outcome);
      return;
    }

    case LoopOutcome::Died:
    {
      /*
       * 6502: DEATH, then DEATH2 -- and the port now takes both.
       *
       * `DEATH` is built (§6.117), so what a player sees on dying is the sequence rather than an immediate restart:
       * the sound, FOUR TIMES the speed, the border rubbed off with its own EOR, a new stardust field, "GAME OVER",
       * five pieces of wreckage and sixty-four iterations of the flight loop to fly them past. `DEATH2` is the tail
       * -- `RES2` and a fall into `BR1` -- which this already did and still does.
       *
       * It said "a quarter-turn of the speed" until 2026-09-05, as did `Flight.h`. Two doublings are a multiply,
       * the port and its test have always had it right, and four times your last speed is what the debris looks like.
       *
       * Neither routine restores the energy banks. That is the game's behaviour and not an omission here: `RESET`
       * fills them and only the COLD start calls it (ADR-003).
       */
      // 6502: DEATH's D2 loop -- a frame, the counter down, round again -- and
      // `Presenter::HoldFlightFrame` is what shows each of the sixty-five frames for as long as
      // the next takes, which is §6.149's bug and the reason it is not `Present`. The library
      // counts the ships; `FRIN` is its byte.
      Die(m_universe, m_ports);

      ResetShipAndBubble(m_universe, m_ports); // 6502: DEATH2's call to RES2

      const ForcedKey begun = StartGame(m_universe, m_ports, false);
      SettleJoystick();
      Perform(begun.outcome);
      return;
    }

    case LoopOutcome::Escaped:
    {
      /*
       * 6502: ESCAPE -- built in slice 4b-a, and this is the last of the three jumps that leave
       * `M%` to be wired (§6.82 named all three; `DOENTRY` and `DEATH` have been wired since 3d).
       *
       * The routine ends by jumping to `GOIN`, which is the docking -- so the arrival is the
       * caller's, the
       * way `TT18`'s fall into `TT110` was. A default commander cannot reach here at all: `KY13` is
       * ANDed with `ESCP`, so it needs one that has bought a pod.
       */
      AbandonShip(m_universe, m_ports);

      // 6502: GOIN -- `stopbd` and then `DOENTRY`, which is the arrival slice 2d built.
      StopDockingMusic(m_universe.music, m_universe.status.titleReset, m_universe.sound, m_universe.memoryMap, m_ports.sid);
      Leave(LoopOutcome::Docked);
      return;
    }

    case LoopOutcome::Continued:
      return;
    }
  }
  /*
   * 6502: TT100 -- one pass of the flight half of the main loop, and then `MLOOP` under it.
   *
   * ONE PASS AND ONE KEY, which is the whole shape of §2.1's `Step(InputFrame)`. It was `Advance`
   * in the executable until M3-c and it counted its own passes, over a `double` accumulator that
   * turned wall-clock seconds into steps -- and the plan's row said that would move here, which it
   * cannot: the determinism guard forbids this library a float (AGENTS.md §5), and ADR-005 §3's
   * accumulator is floating point by construction. So the counting stays where the clock is and
   * this is what it counts.
   *
   * `_key` IS `thiskey` AND ZERO IS A KEY. A key nothing matches falls through `HME1` into `TT107`,
   * which is how the hyperspace countdown ticks: once per pass, whether or not the player touched
   * anything (§6.159).
   */
  bool Game::Step(std::uint8_t _key) noexcept
  {
    const LoopOutcome outcome = MainFlightLoop(m_universe, m_ports); // 6502: M%
    m_lastOutcome = outcome;
    if (outcome != LoopOutcome::Continued)
    {
      Leave(outcome);
      return false;
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
    /*
     * 6502: `.MTT4` falls into `.TT100`, so a trader costs a SECOND flight frame (M6-a-1).
     *
     * The loop is written as a loop because that is what the original is -- part 1's tail lands on
     * the top of part 2, and the top of part 2 is a call to `M%` -- but it runs at most twice: `MCNT` was
     * zero when the head above decremented it, so the second head takes it to 255 and answers
     * `SkipSpawning`. The second frame can end the flight like any other, and it is left through
     * the same door.
     */
    while (RunLoopHead(m_universe, m_ports) == LoopHead::Spawn && RunSpawning(m_universe, false) == SpawnOutcome::Restarted)
    {
      const LoopOutcome again = MainFlightLoop(m_universe, m_ports); // 6502: TT100 -- M% again
      m_lastOutcome = again;
      if (again != LoopOutcome::Continued)
      {
        Leave(again);
        return false;
      }
    }

    /*
     * The frames part 5 asks to wait for are DROPPED here, and honestly: `DELAY` is two vertical
     * syncs on a docked screen, and this is the FLIGHT pass -- `QQ11` is zero on every call that
     * reaches here, so the option's branch is never the one that waits. `StepDocked` is where it
     * matters, and it returns them to the caller since InputTimer.md T-2.
     */
    static_cast<void>(RunLoopTail(m_universe, m_ports, m_universe.commander, m_universe.options.authorNames, false));

    /*
     * 6502: and then `MLOOP`'s second half, which the flight loop falls into -- `TT17` and
     * `TT102`, once per frame and AFTER it.
     *
     * The key the caller hands in is a different thing from the key logger `ScanKeyboard` fills:
     * `TT102` wants the key that was PRESSED and the flight loop wants the keys being HELD, which
     * is why the game reads the hardware twice per frame and so does this.
     *
     * AND `TT17` IS THE HALF THAT WAS MISSING. The comment above described both reads from the
     * day this loop was written and only one of them was here, so `DOKEY` -- ported, swept and
     * green -- was never called by anything but its own test: no key the player HELD reached the
     * game, which is every flight control there is (§6.111).
     */
    (void)ScanFlightControls(m_universe, m_ports, m_universe.view);

    /*
     * 6502: `DOKEY` FALLS INTO `DK4`. The key that arrived is stored into byte 0 of the logger,
     * which nothing on this build reads back but the image compares (M6-0-e). The test for
     * INST/DEL that follows it IS NOT KEPT: the pause screen it opened was removed by owner ruling
     * on 2026-09-08 (InputTimer.md I-0, §5.9), so INST/DEL carries on to the dispatch like every
     * other key, where `TT102` matches nothing and falls through to the countdown.
     */
    m_universe.keys[0] = _key;

    PressKey(_key);
    return true;
  }

  /*
   * 6502: MLOOP's tail on a DOCKED pass -- the countdowns, `TT17` and then `TT102`.
   *
   * EVERY PASS AND NOT ONLY WHEN A KEY WAS PRESSED. The port dispatched on key EVENTS, which is
   * right for every docked screen except the two that read the keyboard as a state: a chart moves
   * its crosshairs while a cursor key is HELD, and `TT102` reaches that through `TT17`'s X and Y
   * rather than through the key it was handed. So a pass with no key is `MoveCrosshairs` on a chart
   * and `CountdownOnly` everywhere else, through `ActionForKey`'s own fall-through (§6.115).
   */
  std::uint8_t Game::StepDocked(std::uint8_t _key) noexcept
  {
    /*
     * 6502: MLOOP -- part 5 WHOLE, on a docked pass as on a flying one (InputTimer.md T-2).
     *
     * Until T-2 this ran `CoolTheGuns` alone and named the rest as a gap: the author-names delay
     * and the Trumble breeding are below the two countdowns and the original reaches both while
     * docked. `RunLoopTail` is the routine, compared against `MLOOP` on docked views with Trumbles
     * aboard, and what it answers is the syncs the pass asked `DELAY` for -- which the executable
     * waits, now that it is told. `DIALS` is skipped inside it because the view is not the space
     * view, which is the original's own gate.
     */
    const std::uint8_t syncs = RunLoopTail(m_universe, m_ports, m_universe.commander, m_universe.options.authorNames, false); // docked

    m_universe.crosshairStep = ScanFlightControls(m_universe, m_ports, m_universe.view);

    PressKey(_key); // 6502: `thiskey`, which is zero when nothing is held
    return syncs;
  }

} // namespace Elite
