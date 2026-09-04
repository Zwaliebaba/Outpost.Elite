#include "pch.h"

#include "CanvasPresenter.h"
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

#include <array>
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
 * WHAT THIS DOES NOT DO YET. There is no flight model, so `Elite::MainLoop::InSpace` is reached
 * and refused rather than run, and `PlanSteps` -- the fixed-timestep accumulator ADR-005 section 3
 * asks for -- has no caller. That is not an oversight: a DOCKED game has nothing to step. Its
 * outer loop is `MLOOP`'s second half, which polls the keyboard and dispatches, and every screen
 * it reaches ends by blocking in `TT217`. The accumulator's first caller is phase 3's `TT100`,
 * where there is finally something that advances whether or not a key was pressed.
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
      : shell(window, presenter, canvas),
        screen(canvas, text, &shell),
        characters(screen),
        recursive(characters),
        values(recursive, text, commander, name, currentSeeds, selectedSeeds, false),
        extended(characters, recursive, rng, &shell),
        trade{recursive, characters, extended, text, shell, shell, rng},
        save{recursive, characters, extended, screen, text, shell, shell, store, numbers}
    {
      recursive.SetValueTokens(&values);
      recursive.SetCursor(&text);
      shell.Attach(recursive, text, characters.state, message);
      shell.AttachExtended(extended);

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
  };

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
      const Elite::ShipCondition condition{_game.dockedFlag, 0, 0, _game.status.energy};
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

    /*
     * The rest belong to phases the port has not reached. They are listed rather than defaulted
     * so that adding a phase-3 screen is a compiler error here instead of a key that does nothing.
     */
    case Elite::KeyAction::Launch:
    case Elite::KeyAction::ChangeView:
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

  int Run(HINSTANCE _instance)
  {
    auto game = std::make_unique<Game>();

    game->window.Create(_instance, INITIAL_SCALE);
    game->presenter.Create(game->window.Handle());

    // 6502: NA% -- the commander the disk menu's "load" compares against, and the one SVE writes.
    Elite::SaveCommander(game->commander, game->name, game->image);

    Elite::GameStart start{game->shell,      game->save,       game->text,           game->commander, game->name,
                           game->image,      game->buffer,     game->useDisk,        game->current,   game->selectedSeeds,
                           game->crosshairX, game->crosshairY, game->explosionCount, game->dockedFlag};

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
     * 6502: MLOOP's second half -- poll the keyboard, dispatch, repeat.
     *
     * The POSITION goes to the dispatch and not the character, which is the whole reason `KeyMap`
     * maps a Windows key to a C64 matrix position: `TT102` compares against 37 for "8" and never
     * against `'8'`. A shell that handed it the translated character would find that no docked
     * screen key worked at all.
     */
    while (game->shell.Turn())
    {
      std::uint8_t key = 0;
      if (game->window.TakeKey(key))
      {
        PressKey(*game, key);
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
