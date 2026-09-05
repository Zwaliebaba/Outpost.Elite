#include "pch.h"

#include "Canvas.h"
#include "Charts.h"
#include "Commander.h"
#include "Docking.h"
#include "DockedKeys.h"
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
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * A whole docked session, driven by a key script (slice 2e).
 *
 * Every routine reached here is compared against the shipped game somewhere else in this suite,
 * one at a time. What no per-routine test can say is whether they FIT: whether the market screen
 * reads the economy the start sequence cached, whether the equipment shop is handed the tech level
 * the chart selected, whether the cursor one screen leaves is the cursor the next one starts from.
 * That is what this is for.
 *
 * It is also the CI half of slice 2e's acceptance criterion. The plan splits that verification in
 * two: a replay through a null presenter, which needs no window and no GPU and therefore runs on
 * the Ubuntu leg, and a human sign-off on legibility and cadence which does not. This is the first
 * half. The second still needs a machine that can show the game to somebody.
 *
 * The expectation is a TRANSCRIPT rather than a hash. A hash would catch drift and tell you
 * nothing about what drifted; a transcript of what each screen printed is diffable, and reading it
 * is how you notice that a screen has gone blank rather than merely different.
 *
 * ONE THING A NULL PRESENTER CANNOT CHECK, and it is worth being explicit because the plan's
 * acceptance criterion leans on this test for half of 2e's verification: LAYOUT. `CHPR` advances
 * the cursor, and `CHPR` is the presenter -- so with a null one the cursor only moves where a
 * routine moves it deliberately, through `INCYC` or `DOXC`, and every character in this
 * transcript is stamped at wherever that left it. The per-screen oracle tests DO compare the
 * cursor, because there the shipped CHPR is trapped on both sides and neither advances it. So
 * layout is verified per screen and not across a session, and the human half of the criterion --
 * "is every docked screen legible" -- is the only thing that covers a session's layout at all.
 */
namespace GameLogicTests
{

  namespace
  {
    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    /*
     * The null presenter: everything the game reaches for outside GameLogic, doing nothing and
     * remembering that it was asked.
     *
     * One object satisfying five interfaces, which is what ADR-004 says the executable does -- the
     * screens declare what they need separately and the shell answers all of it. Building the session
     * this way is the cheapest available check that those five declarations are consistent.
     */
    class NullShell final : public Elite::TradeScreenEffects,
                            public Elite::ChartEffects,
                            public Elite::LineEntryEffects,
                            public Elite::StartUpEffects,
                            public Elite::ControlCodes
    {
    public:
      // 6502: TRADEMODE, CLYNS, TT66, msblob and dn2.
      /*
       * 6502: TRADEMODE -- TT66, a keyboard flush and a palette write.
       *
       * The text state is `SetUpTextScreen`, which is TT66's own and is compared against the shipped
       * routine by `TheScreenSeamsMatchTheShippedRoutines`. It used to be four lines written here
       * from a comment, and they were nearly right: XC, YC and QQ17 were correct and DTW1, DTW2 and
       * DTW6 were not set at all, which no assertion in this file could have noticed.
       */
      void SetUpTradeScreen(std::uint8_t _view) override
      {
        ClearToView(_view);
        FlushKeyboard();
        Note("view " + std::to_string(_view));
      }
      void ClearBottomRows() override
      {
        Note("clyns");
      }
      void BeepAndPause() override
      {
        Note("beep");
      }
      void ClearToView(std::uint8_t _view) override
      {
        view = _view;
        if (cursor != nullptr && printer != nullptr && extended != nullptr)
        {
          Elite::SetUpTextScreen(*printer, *cursor, *extended);
        }
        Note("clear " + std::to_string(_view));
      }
      void ResetMissileIndicators() override
      {
        Note("missiles");
      }

      // 6502: DELAY and FLKB, from two interfaces that both want them.
      void WaitFrames(std::uint8_t _frames) override
      {
        Note("wait " + std::to_string(_frames));
      }
      void FlushKeyboard() override
      {
        Note("flush");
      }

      // 6502: RESET, RES2, ZEKTRAN, startat, stopat, LAUN and TITLE.
      void ResetUniverse() override
      {
        Note("reset");
      }
      void ResetShip() override
      {
        Note("res2");
      }
      void ClearKeyLogger() override
      {
        Note("zektran");
      }
      void StartTheme() override
      {
        Note("music on");
      }
      void StopTheme() override
      {
        Note("music off");
      }
      /// 6502: JSR RDKEY inside `TLL2`. Nothing here rotates a ship, so the first scan dismisses it.
      [[nodiscard]] Elite::TitleKey ScanTitleKeys(Elite::KeyLogger& _keys) override
      {
        (void)_keys;
        return {true, 0u};
      }

      void ShowDockingTunnel() override
      {
        Note("tunnel");
      }
      std::uint8_t ShowTitleScreen(std::uint8_t _token, std::uint8_t _ship, std::uint8_t) override
      {
        Note("title " + std::to_string(_token) + "/" + std::to_string(_ship));
        return titleAnswer;
      }

      /// The control codes that leave the text system, which a null presenter simply does not draw.
      void Run(std::uint8_t _code) override
      {
        Note("code " + std::to_string(_code));
      }

      Elite::TextState* cursor = nullptr;
      Elite::TokenPrinter* printer = nullptr;
      Elite::ExtendedTextState* extended = nullptr;
      std::uint8_t view = 0;
      std::uint8_t titleAnswer = 'N';
      std::vector<std::string> log;

    private:
      void Note(std::string _what)
      {
        log.push_back(std::move(_what));
      }
    };

    /// A store that keeps one commander in memory, so a save and a load in the same session agree.
    class MemoryStore final : public Elite::CommanderStore
    {
    public:
      bool Write(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>,
                 std::span<const std::uint8_t, Elite::COMMANDER_FILE_SIZE> _file) override
      {
        for (std::size_t index = 0; index < _file.size(); ++index)
        {
          file[index] = _file[index];
        }
        written = true;
        return true;
      }
      bool Read(std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>,
                std::span<std::uint8_t, Elite::COMMANDER_FILE_SIZE> _outFile) override
      {
        if (!written)
        {
          return false;
        }
        for (std::size_t index = 0; index < _outFile.size(); ++index)
        {
          _outFile[index] = file[index];
        }
        return true;
      }
      std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE> file{};
      bool written = false;
    };

    class ScriptedKeys final : public Elite::KeySource
    {
    public:
      explicit ScriptedKeys(std::vector<std::uint8_t> _keys) noexcept
        : m_keys(std::move(_keys))
      {
      }
      std::uint8_t NextKey() override
      {
        if (m_taken >= m_keys.size())
        {
          overran = true;
          // RETURN ends a typed line and "N" leaves both the disk menu and a yes/no question, so
          // alternating the two gets out of every loop the docked screens have.
          return ((m_extra++ % 2u) == 0u) ? static_cast<std::uint8_t>(13) : static_cast<std::uint8_t>('N');
        }
        return m_keys[m_taken++];
      }
      [[nodiscard]] std::size_t Taken() const noexcept
      {
        return m_taken;
      }
      bool overran = false;

    private:
      std::vector<std::uint8_t> m_keys;
      std::size_t m_taken = 0;
      std::size_t m_extra = 0;
    };

    /// Every character the session printed, with the cursor it was printed at, as the screens compare.
    struct TranscriptSink final : public Elite::TextSink
    {
      void Put(std::uint8_t _character) override
      {
        ++characters;
        if (_character >= 32 && _character < 127)
        {
          text += static_cast<char>(_character);
        }
        else if (_character == 12 || _character == 13)
        {
          text += '/';
        }
        else
        {
          text += '.';
        }
      }

      void Reset()
      {
        characters = 0;
        text.clear();
      }

      std::uint32_t characters = 0;
      std::string text;
    };

    /*
     * Everything a docked game is, wired together once.
     *
     * The declaration order matters and is not arbitrary: the character printer needs the sink, the
     * token printer needs the character printer, the state tokens need the token printer AND the
     * commander, and the token printer needs the state tokens back -- which is the cycle SetValueTokens
     * exists to break, and the reason ADR-004 has the executable own the composition rather than any
     * one screen.
     */
    struct Session
    {
      Session()
        : characters(sink),
          recursive(characters),
          values(recursive, text, commander, name, currentSeeds, selectedSeeds, false),
          extended(characters, recursive, rng, &shell),
          trade{recursive, characters, extended, text, keys, shell, rng},
          save{recursive, characters, extended, sink, text, keys, shell, store, numbers}
      {
        recursive.SetValueTokens(&values);
        recursive.SetCursor(&text);
        shell.cursor = &text;
        shell.printer = &recursive;
        shell.extended = &characters.state;
        characters.state.sentenceStart = 0xFF;
      }

      Session(const Session&) = delete;
      Session& operator=(const Session&) = delete;

      // ---- the shell ---------------------------------------------------------------------------
      NullShell shell;
      MemoryStore store;
      ScriptedKeys keys{{}};

      // ---- the text system ---------------------------------------------------------------------
      TranscriptSink sink;
      Elite::TextState text;
      Elite::CharacterPrinter characters;
      Elite::TokenPrinter recursive;
      Elite::Rng rng;
      Elite::NumberWorkspace numbers;

      // ---- the commander and the universe -------------------------------------------------------
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

      std::uint8_t crosshairX = 0;
      std::uint8_t crosshairY = 0;
      std::uint8_t explosionCount = 0;
      std::uint8_t dockedFlag = 0;
      std::uint8_t view = 0; ///< 6502: QQ11

      Elite::StateTokens values;
      Elite::ExtendedTokenPrinter extended;
      Elite::TradeScreen trade;
      Elite::SaveScreen save;
    };

    /*
     * One key, and whatever screen it reaches.
     *
     * The dispatch is `ActionForKey`'s, compared against TT102 over 16,384 states; this is the other
     * half -- what a caller DOES with the answer. The actions that reach phase 3 (a launch, a view
     * change, a hyperspace jump) are recorded and not performed, which is what makes a docked session
     * runnable at all before the flight model exists.
     */
    std::string PressKey(Session& _game, std::uint8_t _key)
    {
      const Elite::KeyOutcome outcome = Elite::ActionForKey(_key, _game.dockedFlag, _game.view, _game.status.hyperspaceCountdown, false);

      _game.sink.Reset();

      switch (outcome.action)
      {
      case Elite::KeyAction::StatusMode:
      {
        const Elite::ShipCondition condition{_game.dockedFlag, 0, 0, _game.status.energy};
        Elite::StatusScreen(_game.trade, _game.commander, condition, _game.crosshairX, _game.crosshairY, _game.selectedSeeds);
        return "status";
      }

      case Elite::KeyAction::DataOnSystem:
      {
        // 6502: JSR TT111 / JMP TT25 -- the screen reads what the search leaves behind.
        const Elite::NearestSystem found =
          Elite::FindNearestSystem(_game.commander.GalaxySeeds(), _game.crosshairX, _game.crosshairY,
                                   _game.commander.At(Elite::Field::SystemX), _game.commander.At(Elite::Field::SystemY));
        _game.selectedSeeds = found.seeds;
        Elite::SystemDataScreen(_game.trade, _game.selectedSeeds, found.data, found.distance);
        return "data on system";
      }

      case Elite::KeyAction::MarketPrice:
        // 6502: TT167 -- and the screen reset above it is TRADEMODE, which the caller does. That is
        // the one place the port's split between a screen and its seam is visible from here.
        _game.shell.SetUpTradeScreen(Elite::BUY_CARGO_VIEW);
        Elite::PrintMarketScreen(_game.recursive, _game.characters, _game.text, _game.current.economy, _game.market, false);
        return "market";

      case Elite::KeyAction::BuyCargo:
        Elite::BuyScreen(_game.trade, _game.commander, _game.market, _game.current.economy, false);
        return "buy";

      case Elite::KeyAction::SellCargo:
        Elite::ListCargo(_game.trade, _game.commander, _game.market, _game.current.economy, Elite::SELL_CARGO_VIEW);
        return "sell";

      case Elite::KeyAction::Inventory:
        Elite::InventoryScreen(_game.trade, _game.commander, _game.market, _game.current.economy);
        return "inventory";

      case Elite::KeyAction::EquipShip:
        Elite::EquipShipScreen(_game.trade, _game.commander, _game.current.techLevel);
        return "equip";

      case Elite::KeyAction::DiskAccess:
      {
        const Elite::DiskMenuResult menu =
          Elite::DiskAccessMenu(_game.save, _game.commander, _game.name, _game.image, _game.buffer, _game.useDisk);
        // 6502: BCC P%+5 / JMP QU5 / JMP BAY -- and QU5 is DFAULT, which installs the image.
        if (menu.newCommander)
        {
          (void)Elite::LoadCommander(_game.image, _game.commander, _game.name);
          return "disk menu, new commander";
        }
        return "disk menu";
      }

      /*
       * The rest are phase 3's, and saying so is the point: a docked session cannot launch, change
       * view or jump, and a test that quietly did nothing for these would look the same as one that
       * had wired them up.
       */
      case Elite::KeyAction::Launch:
        return "[launch: phase 3]";
      case Elite::KeyAction::ChangeView:
        return "[view " + std::to_string(outcome.view) + ": phase 3]";
      case Elite::KeyAction::Hyperspace:
        return "[hyperspace: phase 3]";
      case Elite::KeyAction::LongRangeChart:
        return "[long-range chart: needs a canvas]";
      case Elite::KeyAction::ShortRangeChart:
        return "[short-range chart: needs a canvas]";
      case Elite::KeyAction::ShowDistance:
        return "[distance: needs a chart]";
      case Elite::KeyAction::SearchBySystemName:
        return "[search: needs a chart]";
      case Elite::KeyAction::HomeCrosshairs:
        return "[home: needs a chart]";
      case Elite::KeyAction::MoveCrosshairs:
        return "[move: needs a chart]";
      case Elite::KeyAction::CountdownOnly:
        return "[countdown: phase 3]";
      case Elite::KeyAction::Nothing:
        return "nothing";
      }

      return "?";
    }
  } // namespace

  TEST_CLASS(AWholeDockedSessionRuns)
  {
  public:
    /*
     * Start a game and walk every docked screen the port has, in one session.
     *
     * The assertion is the transcript: for each key, which screen it reached and how much that
     * screen printed. What it is really checking is that the pieces compose -- that no screen throws
     * away the state the last one left, that the seams are satisfied by one object, and that a
     * session started by BR1 can be driven by TT102's dispatch into every screen built so far.
     */
    TEST_METHOD(EveryDockedScreenIsReachableInOneSession)
    {
      auto game = std::make_unique<Session>();

      /*
       * The commander the session starts from, with cargo in the hold -- otherwise the sell screen
       * has nothing to offer and prints four words, which would look like a working screen.
       */
      game->commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + 0u] = 5; // food
      game->commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + 3u] = 2; // radioactives
      Elite::SaveCommander(game->commander, game->name, game->image);

      Elite::GameStart start{game->shell,      game->save,       game->text,           game->commander, game->name,
                             game->image,      game->buffer,     game->useDisk,        game->current,   game->selectedSeeds,
                             game->crosshairX, game->crosshairY, game->explosionCount, game->dockedFlag};

      // 6502: TT170 -- the cold start, which ends by pressing "8" on the player's behalf.
      game->shell.titleAnswer = 'N';
      const Elite::ForcedKey begun = Elite::ResetAndStartGame(start);

      Assert::AreEqual(static_cast<int>(Elite::KeyAction::StatusMode), static_cast<int>(begun.outcome.action),
                       L"a new game opens on the status screen");
      Assert::AreEqual(static_cast<int>(Elite::MainLoop::Docked), static_cast<int>(begun.loop), L"and in the docked half of the main loop");
      Assert::AreEqual<std::uint8_t>(0xFF, game->dockedFlag, L"docked");

      /*
       * 6502: the market is rolled on arrival, not by the start sequence -- but the market screen
       * reads it, so a session that never generated one would print a table of zeroes and pass.
       */
      Elite::GenerateMarket(game->rng, game->current.economy, game->market);

      /*
       * What the market says food costs, before anything is bought. `MarketPrice` is the routine
       * the screen prints through, so asking it here is asking the same question the player reads.
       */
      const std::uint32_t quotedFoodPrice = Elite::TotalPrice(Elite::MarketPrice(0, game->current.economy, game->market.randomiser), 1);
      const std::uint32_t startingCash = game->commander.Cash();

      struct Step
      {
        std::uint8_t key;
        const char* expected;
        bool printsSomething;
      };

      // Every key TT102 acts on while docked, in the order a player would try them.
      const std::vector<Step> SCRIPT = {
        {Elite::KEY_STATUS, "status", true},
        {Elite::KEY_MARKET_PRICE, "market", true},
        {Elite::KEY_BUY_CARGO, "buy", true},
        {Elite::KEY_SELL_CARGO, "sell", true},
        {Elite::KEY_INVENTORY, "inventory", true},
        {Elite::KEY_EQUIP_SHIP, "equip", true},
        {Elite::KEY_DATA_ON_SYSTEM, "data on system", true},
        {Elite::KEY_LONG_RANGE, "[long-range chart: needs a canvas]", false},
        {Elite::KEY_SHORT_RANGE, "[short-range chart: needs a canvas]", false},
        {Elite::KEY_LAUNCH, "[launch: phase 3]", false},
        {Elite::KEY_DISK_ACCESS, "disk menu", true},
        /*
         * Not one of the dispatch's keys -- and the answer is NOT "nothing". Docked and not on a
         * chart, TT102 falls all the way through to TT107, the hyperspace countdown, which ticks on
         * every key press whether or not the key meant anything. The only way to reach `t95`'s RTS
         * is to be on a chart with a jump already counting down.
         */
        {0x7F, "[countdown: phase 3]", false},
        {Elite::KEY_STATUS, "status", true},
      };

      /*
       * What each screen has to be answered with. The buy and sell screens read a quantity, the
       * equipment shop a menu choice, and the disk menu a key -- so the script for the KEYBOARD is
       * not the script for the dispatch, and the two have to line up.
       */
      /*
       * The keys each screen asks for, and they are not one apiece.
       *
       * The buy screen asks for a quantity for EVERY item with stock and re-asks after a refusal,
       * so the only bounded way out is gnum's letter exit; the sell screen asks once per item in
       * the hold; the equipment shop takes one number; the disk menu one key. Getting this wrong is
       * how a session test silently runs on fallback keys, which is why the overrun is an assertion
       * rather than a convenience.
       */
      game->keys = ScriptedKeys({
        '2',
        13, // buy: two tonnes of the first item with any stock
        13, 13, 13, 13,
        13,  // then nothing of the next five, which the routine accepts silently
        'Q', // and then a letter, which is gnum's only way out of the buy loop
        'N',
        'N', // sell: refuse both items in the hold
        13,  // equip: nothing entered, which leaves the shop quietly
        '5', // the disk menu: exit
      });

      std::vector<std::string> transcript;
      std::uint32_t screensThatPrinted = 0;

      for (const Step& step : SCRIPT)
      {
        const std::size_t before = game->keys.Taken();
        const std::string reached = PressKey(*game, step.key);
        transcript.push_back(reached + " (" + std::to_string(game->keys.Taken() - before) + " keys, " +
                             std::to_string(game->sink.characters) + " chars)");

        const std::wstring where = Widen("key " + std::to_string(step.key));
        Assert::AreEqual(std::string(step.expected), reached, (where + L": which screen").c_str());
        (void)before;

        if (step.printsSomething)
        {
          Assert::IsTrue(game->sink.characters > 0, (where + L": " + Widen(reached) + L" printed nothing at all").c_str());
          ++screensThatPrinted;
        }
      }

      Logger::WriteMessage("Session: ");
      for (const std::string& entry : transcript)
      {
        Logger::WriteMessage((entry + "; ").c_str());
      }
      Logger::WriteMessage("\n");

      Assert::IsFalse(game->keys.overran, L"the session asked for more keys than the script holds");

      /*
       * The purchase reached the commander, and the commander reached the inventory.
       *
       * This is the assertion the whole harness exists for. Two tonnes of food bought on the buy
       * screen have to be two tonnes in the hold when the inventory screen prints it and two tonnes
       * fewer in the market's stock, and the cash has to have moved by the price the market screen
       * quoted -- three routines, three tests of their own, and nothing that checks they are talking
       * about the same tonne of food.
       */
      Assert::AreEqual<std::uint8_t>(7, game->commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold)],
                                     L"five tonnes of food in the hold, plus the two just bought");
      Assert::IsTrue(game->commander.Cash() < startingCash, L"and the money for them has left the commander");
      Assert::AreEqual<std::uint32_t>(startingCash - game->commander.Cash(), 2u * quotedFoodPrice,
                                      L"exactly twice the price the market quoted");

      /*
       * Seven screens drew, and they are not each other. A session where every screen printed the
       * same thing would satisfy every assertion above.
       */
      Assert::AreEqual<std::uint32_t>(9, screensThatPrinted, L"how many screens drew");

      Logger::WriteMessage("Session: ");
      for (const std::string& entry : transcript)
      {
        Logger::WriteMessage((entry + "; ").c_str());
      }
      Logger::WriteMessage("\n");
    }

    /*
     * The same session, checked for the thing a transcript cannot show: that the screens print
     * DIFFERENT text, and that each one leaves the cursor where the next one expects to find it.
     */
    TEST_METHOD(TheScreensDoNotPrintTheSameThingAsEachOther)
    {
      auto game = std::make_unique<Session>();
      game->commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + 0u] = 5;
      game->commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold) + 3u] = 2;
      Elite::SaveCommander(game->commander, game->name, game->image);

      Elite::GameStart start{game->shell,      game->save,       game->text,           game->commander, game->name,
                             game->image,      game->buffer,     game->useDisk,        game->current,   game->selectedSeeds,
                             game->crosshairX, game->crosshairY, game->explosionCount, game->dockedFlag};
      (void)Elite::ResetAndStartGame(start);
      Elite::GenerateMarket(game->rng, game->current.economy, game->market);

      game->keys = ScriptedKeys({'2', 13, 13, 13, 13, 13, 13, 'Q', 'N', 'N', 13});

      struct Drawn
      {
        std::string what;
        std::string text;
      };
      std::vector<Drawn> drawn;

      for (const std::uint8_t key : {Elite::KEY_STATUS, Elite::KEY_MARKET_PRICE, Elite::KEY_BUY_CARGO, Elite::KEY_SELL_CARGO,
                                     Elite::KEY_INVENTORY, Elite::KEY_EQUIP_SHIP, Elite::KEY_DATA_ON_SYSTEM})
      {
        const std::string what = PressKey(*game, key);
        drawn.push_back({what, game->sink.text});
      }

      for (std::size_t left = 0; left < drawn.size(); ++left)
      {
        Assert::IsTrue(drawn[left].text.size() > 20, (Widen(drawn[left].what) + L" printed suspiciously little").c_str());
        for (std::size_t right = left + 1; right < drawn.size(); ++right)
        {
          Assert::AreNotEqual(drawn[left].text, drawn[right].text,
                              (Widen(drawn[left].what) + L" and " + Widen(drawn[right].what) + L" printed the same thing").c_str());
        }
      }

      Assert::IsFalse(game->keys.overran, L"the session asked for more keys than the script holds");

      for (const Drawn& entry : drawn)
      {
        Logger::WriteMessage((entry.what + ": \"" + entry.text.substr(0, 72) + "\"\n").c_str());
      }
    }
  };

} // namespace GameLogicTests
