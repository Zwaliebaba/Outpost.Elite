#include "pch.h"

#include "NullSeams.h"

#include "Canvas.h"
#include "Picture.h"
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
#include "Galaxy.h"

#include <array>
#include <cstdint>
#include <memory>
#include <sstream>
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
 * transcript is stamped at wherever that left it. The per-screen comparisons DID check the
 * cursor, because there the shipped `CHPR` was trapped on both sides and neither advanced it, and
 * they went with the original (Modernize.md M6-b-5). Nothing covers a session's layout now except
 * the human half of the criterion: is every docked screen legible.
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
     * One object satisfying four interfaces, which is what ADR-004 says the executable does -- the
     * screens declare what they need separately and the shell answers all of it. Building the session
     * this way is the cheapest available check that those declarations are consistent.
     */
    class NullShell final : public Elite::Presenter
    {
    public:
      /*
       * `TRADEMODE`, `CLYNS`, `TT66` and `dn2` WERE ANSWERED HERE AND ARE NOT ANY MORE (M3-b-3b).
       *
       * Four overrides, and the shape of them is why the seams went: `SetUpTradeScreen` called
       * `ClearToView` and `FlushKeyboard`, and `ClearToView` called `Elite::SetUpTextScreen`. The
       * library does all of that itself now -- `SetUpScreen`, `ClearMessageRows` and `Beep` -- and
       * only the flush and the wait were left for the platform to answer. The flush went too in
       * M3-b-3d: it is `Keyboard::Flush`, so `ScriptedKeys` notes it and this does not.
       */

      // 6502: DELAY, which is `Presenter`'s.
      void WaitFrames(std::uint8_t _frames) override
      {
        Note("wait " + std::to_string(_frames));
      }
      void Present() override
      {
        Note("present");
      }
      void HoldFlightFrame(std::uint8_t _ships) override
      {
        Note("hold " + std::to_string(_ships));
      }
      void HoldTitleFrame(std::uint8_t _distance) override
      {
        Note("spin " + std::to_string(_distance));
      }

      // `RESET`, `RES2` and `msblob` were answered here until M3-b-1e, `startat` and `stopat`
      // until M3-b-2b, `ZEKTRAN` until M6-0-h-1 and `TITLE` until M6-0-h-2; all six are the
      // library's now, and the title screen ends on the key `ScriptedKeys` holds for it.

      /*
       * `Run` WAS HERE AND IS NOT ANY MORE (M3-b-4b).
       *
       * It noted `"code N"` into the transcript and nothing asserted on the note. A docked session
       * is the fixture that most nearly IS the executable, so the codes run for real here now --
       * `SetGame` in the constructor -- and what they do lands in the state and the transcript this
       * suite already compares.
       */

      Elite::TextState* cursor = nullptr;
      Elite::TokenPrinter* printer = nullptr;
      Elite::ExtendedTextState* extended = nullptr;
      std::uint8_t view = 0;
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

    /*
     * The keyboard, which answers three questions and used to answer one (M3-b-3d).
     *
     * It was a `KeySource` -- `TT217` and nothing else -- because `RDKEY` was a seam on the shell
     * and `FLKB` was one on the line editor. Both are this port's now, so the script gained a set
     * of held keys (which nothing docked reads: `RDKEY`'s callers are the title screen and the
     * flight loop) and the flush the null shell used to note.
     */
    class ScriptedKeys final : public Elite::Keyboard
    {
    public:
      explicit ScriptedKeys(std::vector<std::uint8_t> _keys) noexcept
        : m_keys(std::move(_keys))
      {
      }
      /// 6502: the matrix walk -- Space, held for exactly as long as the title screens need a key
      /// to end them (M6-0-h-2), and nothing otherwise: the docked half reads `NextKey`.
      [[nodiscard]] bool Held(std::size_t _key) override
      {
        return titleHeld && _key == Elite::KEY_SPEED_UP;
      }
      bool titleHeld = false;
      void Flush() override
      {
        ++flushes;
      }
      [[nodiscard]] std::uint8_t NextKey() override
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
      std::uint32_t flushes = 0; ///< 6502: FLKB, which was the null shell's note until M3-b-3d

    private:
      std::vector<std::uint8_t> m_keys;
      std::size_t m_taken = 0;
      std::size_t m_extra = 0;
    };

    /// Every character the session printed, with the cursor it was printed at, as the screens compare.
    struct TranscriptSink final : public Elite::TextSink
    {
      /*
       * It RECORDS and then PASSES ON, and the passing on is RN-0's (`TheDockedScreensAreAsRecorded`).
       *
       * This was a terminal sink: characters went into a string and stopped, which is what the
       * comment at the top of this file means by "a null presenter" and why it says LAYOUT is the
       * one thing a session could not check. `CHPR` is the presenter, and with none of it the
       * cursor only moved where a routine moved it deliberately -- and nothing was drawn, on either
       * surface, so a session had no picture to record.
       *
       * Passing the character on to a real `TextPrinter` gives the session both: the transcript the
       * older tests read, and the canvas and picture a person would see. `_next` is a pointer
       * rather than a reference so that a sink can still be terminal, which nothing needs today and
       * which is one line to keep.
       */
      explicit TranscriptSink(Elite::TextSink* _next = nullptr) noexcept
        : next(_next)
      {
      }

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

        // And on to `CHPR`, which is what puts it on the canvas and the picture.
        if (next != nullptr)
        {
          next->Put(_character);
        }
      }

      void Reset()
      {
        characters = 0;
        text.clear();
      }

      Elite::TextSink* next = nullptr;

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
        : characters(sink, universe.sentences),
          recursive(characters, text),
          values(recursive, text, commander, name, currentSeeds, selectedSeeds, false),
          extended(characters, recursive, rng),
          ports{recursive, characters, sink, sid,
                extended,  shell, keys, store}
      {
        extended.SetGame(universe, ports); // 6502: DT3 -- the codes that leave run in the library
        commander = Elite::DefaultCommander();
        name = Elite::DefaultCommanderName();
        recursive.SetValueTokens(&values);
        shell.cursor = &text;
        shell.printer = &recursive;
        shell.extended = &characters.State();
        characters.State().sentenceStart = 0xFF;

        // The wide surface, so the glyph twins draw beside the faithful ones (Resolution.md §6) --
        // and the BACKDROP of the two since RN-0, exactly as `Game` wires the real game.
        screen.AttachPicture(&universe.backdrop, &universe.screenLayout);
      }

      Session(const Session&) = delete;
      Session& operator=(const Session&) = delete;

      // ---- the shell ---------------------------------------------------------------------------
      NullShell shell;
      MemoryStore store;
      ScriptedKeys keys{{}};

      /*
       * Every byte of game state, in one object, and the names under it are ALIASES INTO IT.
       *
       * This struct is the docked half's `Outpost::Game`, and `Game` owns one `Elite::Universe`
       * since M3-a; the screens take `(Universe&, Ports&)` since M3-a-3, so a session that kept
       * its own commander beside the universe's would be driving the screens from different bytes
       * from the ones it asserts on -- which is the defect M3-a-2's Windows build found in the app.
       */
      Elite::Universe universe;

      // ---- the text system ---------------------------------------------------------------------

      /*
       * `CHPR` ITSELF, over the universe's own canvas, and the sink below feeds it (RN-0).
       *
       * A session printed through a terminal transcript until 2026-09-09 and drew nothing, so the
       * two things a person actually looks at -- the canvas and the 640x400 picture beside it --
       * were empty on every docked screen and no test could record them. This is the same wiring
       * `PictureTextTests`' `Docked` fixture has and the same wiring `Game` gives the real game.
       */
      Elite::TextPrinter screen{universe.canvas, universe.text};

      TranscriptSink sink{&screen};
      Elite::TextState& text = universe.text;
      Elite::CharacterPrinter characters;
      Elite::TokenPrinter recursive;
      Elite::Rng& rng = universe.rng;
      std::uint8_t& numberWidth = universe.numberWidth; // 6502: U as the last BPRNT left it (M2-c)

      // ---- the commander and the universe -------------------------------------------------------
      Elite::Commander& commander = universe.commander;
      std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE>& name = universe.commanderName;
      std::array<std::uint8_t, Elite::COMMANDER_FILE_SIZE>& image = universe.commanderFile;
      std::array<std::uint8_t, 16>& buffer = universe.lineBuffer;
      std::uint8_t& useDisk = universe.useDisk;

      Elite::SystemSeeds& currentSeeds = universe.current.seeds;
      Elite::SystemSeeds& selectedSeeds = universe.selectedSeeds;
      Elite::CurrentSystem& current = universe.current;
      Elite::MarketState& market = universe.market;
      Elite::FlightStatus& status = universe.status;

      std::uint8_t& crosshairX = universe.crosshairX;
      std::uint8_t& crosshairY = universe.crosshairY;
      std::uint8_t& explosionCount = universe.explosions;
      std::uint8_t& dockedFlag = universe.dockedFlag;
      std::uint8_t& view = universe.view; ///< 6502: QQ11

      Elite::StateTokens values;
      Elite::ExtendedTokenPrinter extended;
      NullSeams nulls;
      Elite::SidWriteLog sid; ///< 6502: SID -- the docked half's own writes, which are none

      /// The seams, over the shell and the store. Last, because every reference in it is bound at
      /// construction. `NullShell` answers four of them, which is what it is for.
      Elite::Ports ports;
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
        Elite::StatusScreen(_game.universe, _game.ports, condition);
        return "status";
      }

      case Elite::KeyAction::DataOnSystem:
      {
        // 6502: JSR TT111 / JMP TT25 -- the screen reads what the search leaves behind.
        const Elite::NearestSystem found =
          Elite::FindNearestSystem(_game.commander.galaxySeeds, _game.crosshairX, _game.crosshairY,
                                   _game.commander.systemX, _game.commander.systemY);
        _game.selectedSeeds = found.seeds;
        Elite::SystemDataScreen(_game.universe, _game.ports, found.data, found.distance);
        return "data on system";
      }

      case Elite::KeyAction::MarketPrice:
        // 6502: TT167 -- and the screen reset above it is TRADEMODE, which the caller does. It was
        // a seam on the shell until M3-b-3b and is `TT66` and a keyboard flush.
        Elite::SetUpScreen(_game.universe, _game.ports, Elite::BUY_CARGO_VIEW);
        _game.keys.Flush();
        Elite::PrintMarketScreen(_game.recursive, _game.characters, _game.text, _game.current.economy, _game.market, false);
        return "market";

      case Elite::KeyAction::BuyCargo:
        Elite::BuyScreen(_game.universe, _game.ports, false);
        return "buy";

      case Elite::KeyAction::SellCargo:
        Elite::ListCargo(_game.universe, _game.ports, Elite::SELL_CARGO_VIEW);
        return "sell";

      case Elite::KeyAction::Inventory:
        Elite::InventoryScreen(_game.universe, _game.ports);
        return "inventory";

      case Elite::KeyAction::EquipShip:
        Elite::EquipShipScreen(_game.universe, _game.ports);
        return "equip";

      case Elite::KeyAction::DiskAccess:
      {
        const Elite::DiskMenuResult menu =
          Elite::DiskAccessMenu(_game.universe, _game.ports);
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
      game->commander.cargoHold[0u] = 5; // food
      game->commander.cargoHold[3u] = 2; // radioactives
      Elite::SaveCommander(game->commander, game->name, game->image);

      // 6502: TT170 -- the cold start, which ends by pressing "8" on the player's behalf.
      game->keys.titleHeld = true; // Space at both title screens, which is not "Y", so no disk menu
      const Elite::ForcedKey begun = Elite::ResetAndStartGame(game->universe, game->ports, false);
      game->keys.titleHeld = false;

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
      const std::uint32_t startingCash = game->commander.cash.tenths;

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
      Assert::AreEqual<std::uint8_t>(7, game->commander.cargoHold[0],
                                     L"five tonnes of food in the hold, plus the two just bought");
      Assert::IsTrue(game->commander.cash.tenths < startingCash, L"and the money for them has left the commander");
      Assert::AreEqual<std::uint32_t>(startingCash - game->commander.cash.tenths, 2u * quotedFoodPrice,
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
      game->commander.cargoHold[0u] = 5;
      game->commander.cargoHold[3u] = 2;
      Elite::SaveCommander(game->commander, game->name, game->image);

      game->keys.titleHeld = true; // Space at both title screens (M6-0-h-2)
      (void)Elite::ResetAndStartGame(game->universe, game->ports, false);
      game->keys.titleHeld = false;
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

    /*
     * WHAT EACH DOCKED SCREEN LOOKS LIKE, one whole-frame digest apiece (RN-0).
     *
     * `ThePictureIsAsRecorded` does this for a scripted FLIGHT and is the gate every rendering
     * slice runs against. It sees no docked screen at all, and until this test the docked half had
     * no picture gate of any kind: this file printed through a terminal transcript, so the canvas
     * and the 640x400 surface were blank on every screen and there was nothing to hash. Twenty-seven
     * of RN-0's eighty-one draw sites are on these screens.
     *
     * SO THE NUMBERS BELOW ARE A BEFORE, recorded on the tree with the backdrop present and no site
     * moved onto it, and RN-0's remaining commits are accepted on their not moving. A chart or a
     * market glyph misclassified as transient would blank between one screen and the next, and
     * nothing else in this suite would say so -- the transcript would be identical, because the
     * text is printed either way; it is only the surface it lands on that changes.
     *
     * They are IDENTITY ACROSS A REFACTOR and not fidelity. No oracle ever saw this surface
     * (ADR-008 §3), and `Picture::Hash` folds the resolved pixels, so what a moved value means is
     * "a person is shown something different", never "the game does something different" -- the
     * state digests answer that, separately, in `FlightReplayTests`.
     *
     * Recorded 2026-09-09 by running the session and printing what it drew. Every value came out of
     * the port and none was chosen.
     */
    static constexpr std::uint64_t RECORDED_DOCKED_PICTURES[] = {
      0x97D04EFDD6A9BD2Dull, ///< status
      0x580D1001003EE1A7ull, ///< market
      0x64E4880A35A8215Full, ///< buy
      0xCB40E184D014E420ull, ///< sell
      0x5DA37B99B8E7D606ull, ///< inventory
      0x7A140263D565AD3Bull, ///< equip
      0xCB1B15DE2595159Dull, ///< data on system
    };

    TEST_METHOD(TheDockedScreensAreAsRecorded)
    {
      auto game = std::make_unique<Session>();
      game->commander.cargoHold[0u] = 5;
      game->commander.cargoHold[3u] = 2;
      Elite::SaveCommander(game->commander, game->name, game->image);

      game->keys.titleHeld = true; // Space at both title screens (M6-0-h-2)
      (void)Elite::ResetAndStartGame(game->universe, game->ports, false);
      game->keys.titleHeld = false;
      Elite::GenerateMarket(game->rng, game->current.economy, game->market);

      game->keys = ScriptedKeys({'2', 13, 13, 13, 13, 13, 13, 'Q', 'N', 'N', 13});

      // The same seven `TheScreensDoNotPrintTheSameThingAsEachOther` walks, in the same order and
      // off the same script, so a change to one test's script is a visible change to the other's.
      const std::vector<std::uint8_t> SCREENS = {Elite::KEY_STATUS,    Elite::KEY_MARKET_PRICE, Elite::KEY_BUY_CARGO,
                                                 Elite::KEY_SELL_CARGO, Elite::KEY_INVENTORY,   Elite::KEY_EQUIP_SHIP,
                                                 Elite::KEY_DATA_ON_SYSTEM};

      std::vector<std::string> names;
      std::vector<std::uint64_t> digests;

      for (const std::uint8_t key : SCREENS)
      {
        names.push_back(PressKey(*game, key));
        digests.push_back(game->universe.picture.Hash(game->universe.canvas, game->universe.backdrop));
      }

      // The table, printed on every run so that re-recording is a copy rather than a transcription.
      {
        std::wstringstream table;
        table << L"RECORDED_DOCKED_PICTURES[] = {";
        for (const std::uint64_t digest : digests)
        {
          table << L"0x" << std::hex << std::uppercase << digest << L"ull, ";
        }
        table << L"};\n";
        Logger::WriteMessage(table.str().c_str());
      }

      Assert::IsFalse(game->keys.overran, L"the session asked for more keys than the script holds");

      /*
       * The seven are DIFFERENT PICTURES, and this clause is what stops the table above passing
       * vacuously. A session that drew nothing would give seven identical digests and go on
       * matching a table re-recorded from it for ever; so would one where every screen blanked.
       */
      for (std::size_t left = 0; left < digests.size(); ++left)
      {
        for (std::size_t right = left + 1; right < digests.size(); ++right)
        {
          if (digests[left] == digests[right])
          {
            Assert::Fail((Widen(names[left]) + L" and " + Widen(names[right]) + L" resolved to the same picture").c_str());
          }
        }
      }

      Assert::AreEqual(sizeof(RECORDED_DOCKED_PICTURES) / sizeof(RECORDED_DOCKED_PICTURES[0]), digests.size(),
                       L"a different number of screens was walked, so the table cannot line up");

      for (std::size_t at = 0; at < digests.size(); ++at)
      {
        if (digests[at] != RECORDED_DOCKED_PICTURES[at])
        {
          std::wstringstream message;
          message << L"the " << Widen(names[at]) << L" screen changed: recorded 0x" << std::hex << std::uppercase
                  << RECORDED_DOCKED_PICTURES[at] << L", drew 0x" << digests[at]
                  << L". Nothing about the GAME moved -- the transcript tests beside this one assert that -- so this is what a"
                  << L" person is shown changing. If a slice meant to change it, re-record with the reason in the journal"
                  << L" (Design/Platform-Build.md RN-0).";
          Assert::Fail(message.str().c_str());
        }
      }
    }
  };

} // namespace GameLogicTests
