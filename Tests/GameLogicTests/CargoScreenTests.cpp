#include "pch.h"

#include "Canvas.h"
#include "CargoScreens.h"
#include "Commander.h"
#include "DockedKeys.h"
#include "ExtendedTokens.h"
#include "LookupTables.h"
#include "Market.h"
#include "Rng.h"
#include "StateTokens.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using Elite::Canvas;
using Elite::ListKey;
using Elite::ListKeyPress;
using Elite::MarketState;
using Elite::QuantityEntry;
using Elite::QuantityStep;

/*
 * The modernised trading screens (ADR-006).
 *
 * These are NOT oracle tests. There is no original to compare a screen the original never had, so
 * what is asserted is behaviour: what the screen prints, where the bar is, what a purchase does to
 * the hold, the market and the cash -- and those last three are computed by the routines the
 * oracle suites already prove, so the numbers here are checked against `MarketPrice`, `TotalPrice`
 * and `CargoFits` rather than against constants.
 *
 * The screen is READ BACK FROM THE CANVAS. The ported screens are compared as a character stream
 * with the cursor stamped on each; these print some of their text straight through CHPR, which no
 * sink sees, so the test decodes cells against the font instead. That also lets it see the bar,
 * which is not a character at all.
 */
namespace GameLogicTests
{

  /// Named rather than anonymous, because the tests declare local classes that hold a `Screen&`,
  /// and a local class with a member of anonymous-namespace type is a linkage warning on GCC.
  namespace CargoFixture
  {
    std::wstring Widen(const std::string& _text)
    {
      return std::wstring(_text.begin(), _text.end());
    }

    /// Everything the screens reach through a seam, recorded rather than performed.
    class NullEffects final : public Elite::TradeScreenEffects, public Elite::ControlCodes, public Elite::KeySource
    {
    public:
      void SetUpTradeScreen(std::uint8_t _view) override
      {
        ClearToView(_view);
        ++setUps;
      }
      void ClearBottomRows() override
      {
        ++clears;
      }
      void ClearToView(std::uint8_t _view) override
      {
        view = _view;
        if (canvas != nullptr && text != nullptr && printer != nullptr && extended != nullptr)
        {
          Elite::ResetCellColours(*canvas);
          Elite::ClearTextArea(*canvas, *text);
          Elite::SetUpTextScreen(*printer, *text, *extended);
        }
      }
      void ResetMissileIndicators() override {}
      void BeepAndPause() override
      {
        ++beeps;
      }
      void Run(std::uint8_t) override {}
      std::uint8_t NextKey() override
      {
        return 'C';
      }

      Canvas* canvas = nullptr;
      Elite::TextState* text = nullptr;
      Elite::TokenPrinter* printer = nullptr;
      Elite::ExtendedTextState* extended = nullptr;
      std::uint8_t view = 0;
      int setUps = 0;
      int clears = 0;
      int beeps = 0;
    };

    /// The scripted keyboard. Running off the end LEAVES, through a key the dispatch acts on, so a
    /// script that expected fewer keys than the screen asked for fails by name rather than hanging.
    class ScriptedListKeys final : public Elite::ListKeySource
    {
    public:
      ListKeyPress NextListKey() override
      {
        if (taken >= keys.size())
        {
          overran = true;
          return {ListKey::Other, 0, Elite::KEY_INVENTORY};
        }
        return keys[taken++];
      }

      std::vector<ListKeyPress> keys;
      std::size_t taken = 0;
      bool overran = false;
    };

    ListKeyPress Up()
    {
      return {ListKey::Up, 0, 0};
    }
    ListKeyPress Down()
    {
      return {ListKey::Down, 0, 0};
    }
    ListKeyPress Buy()
    {
      return {ListKey::Buy, 'B', 0};
    }
    /// A digit, with the position it shares with a screen key when it does: "7" is F4's position.
    ListKeyPress Digit(char _digit, std::uint8_t _position = 0)
    {
      return {ListKey::Digit, static_cast<std::uint8_t>(_digit), _position};
    }
    ListKeyPress Delete()
    {
      return {ListKey::Delete, 127, 0};
    }
    ListKeyPress Return()
    {
      return {ListKey::Return, 13, 0};
    }
    ListKeyPress Escape()
    {
      return {ListKey::Escape, 27, 0};
    }
    ListKeyPress Other(std::uint8_t _position)
    {
      return {ListKey::Other, 0, _position};
    }

    /// Lave's seeds: the system at (20, 173) in the first galaxy, found the way TT111 finds it.
    Elite::SystemSeeds Lave()
    {
      return Elite::FindNearestSystem(Elite::DefaultCommander().GalaxySeeds(), 20, 173, 20, 173).seeds;
    }

    /// A market with ten of everything except slaves, and a randomiser of zero so the prices are
    /// whatever `MarketPrice` says for the economy alone.
    MarketState TenOfEverything()
    {
      MarketState market;
      market.randomiser = 0;
      for (std::size_t item = 0; item < static_cast<std::size_t>(Elite::MARKET_ITEM_COUNT); ++item)
      {
        market.availability[item] = 10;
      }
      market.availability[3] = 0; // slaves: a dash
      return market;
    }

    constexpr std::uint8_t ECONOMY = 5;

    /*
     * The whole text system over a real canvas, wired the way the composition root wires it.
     */
    struct Screen
    {
      Screen()
        : screen(canvas, text, nullptr),
          characters(screen),
          recursive(characters),
          values(recursive, text, commander, name, currentSeeds, selectedSeeds, false),
          extended(characters, recursive, rng, &effects),
          trade{recursive, characters, extended, text, effects, effects, rng}
      {
        recursive.SetValueTokens(&values);
        recursive.SetCursor(&text);
        effects.canvas = &canvas;
        effects.text = &text;
        effects.printer = &recursive;
        effects.extended = &characters.state;
        characters.state.sentenceStart = 0xFF;
        text.cellColour = Elite::TEXT_COLOUR_WHITE;
      }

      Screen(const Screen&) = delete;
      Screen& operator=(const Screen&) = delete;

      std::uint8_t RunBuyScreen()
      {
        return Elite::BuyCargoScreen(trade, keys, canvas, commander, market, ECONOMY, 0xFF);
      }

      /// Decode one cell against the font. A cell that is nothing is a space; a cell that matches
      /// no glyph, or matches only after flipping when it should not, is '?'.
      char Cell(std::uint8_t _row, std::uint8_t _column, bool _inverted = false) const
      {
        const std::uint16_t base = static_cast<std::uint16_t>(_row * Canvas::ROW_BYTES + Canvas::SPACE_VIEW_MARGIN + _column * 8);
        std::array<std::uint8_t, 8> bytes{};
        for (std::uint8_t line = 0; line < 8; ++line)
        {
          bytes[line] = canvas.Read(static_cast<std::uint16_t>(base + line));
          if (_inverted)
          {
            bytes[line] = static_cast<std::uint8_t>(~bytes[line]);
          }
        }
        for (int character = 32; character < 127; ++character)
        {
          const std::size_t glyph = static_cast<std::size_t>(character - 32) * 8u;
          bool same = true;
          for (std::size_t line = 0; line < 8; ++line)
          {
            same = same && (bytes[line] == Elite::FONT_DATA[glyph + line]);
          }
          if (same)
          {
            return static_cast<char>(character);
          }
        }
        return '?';
      }

      std::string Row(std::uint8_t _row, bool _inverted = false) const
      {
        std::string text;
        for (std::uint8_t column = 0; column < 32; ++column)
        {
          text += Cell(_row, column, _inverted);
        }
        while (!text.empty() && text.back() == ' ')
        {
          text.pop_back();
        }
        return text;
      }

      /// Column 31 is never printed -- CHPR wraps at it -- so its cell says whether the row is inverted.
      bool RowIsInverted(std::uint8_t _row) const
      {
        const std::uint16_t base = static_cast<std::uint16_t>(_row * Canvas::ROW_BYTES + Canvas::SPACE_VIEW_MARGIN + 31 * 8);
        return canvas.Read(base) == 0xFF;
      }

      /// The line `PrintMarketItem` prints for an item, on a scratch canvas, for comparison.
      std::string ExpectedItemLine(int _item)
      {
        Canvas scratch;
        Elite::TextState scratchText;
        scratchText.cellColour = Elite::TEXT_COLOUR_WHITE;
        Elite::TextPrinter scratchScreen(scratch, scratchText, nullptr);
        Elite::CharacterPrinter scratchCharacters(scratchScreen);
        Elite::TokenPrinter scratchRecursive(scratchCharacters);
        scratchRecursive.SetCursor(&scratchText);
        scratchText.row = 10;
        scratchRecursive.SetCaseFlags(0x80);
        MarketState copy = market;
        Elite::PrintMarketItem(scratchRecursive, scratchCharacters, scratchText, _item, ECONOMY, copy, false);

        std::string text;
        for (std::uint8_t column = 0; column < 32; ++column)
        {
          const std::uint16_t base = static_cast<std::uint16_t>(10 * Canvas::ROW_BYTES + Canvas::SPACE_VIEW_MARGIN + column * 8);
          std::array<std::uint8_t, 8> bytes{};
          for (std::uint8_t line = 0; line < 8; ++line)
          {
            bytes[line] = scratch.Read(static_cast<std::uint16_t>(base + line));
          }
          char found = '?';
          for (int character = 32; character < 127; ++character)
          {
            const std::size_t glyph = static_cast<std::size_t>(character - 32) * 8u;
            bool same = true;
            for (std::size_t line = 0; line < 8; ++line)
            {
              same = same && (bytes[line] == Elite::FONT_DATA[glyph + line]);
            }
            if (same)
            {
              found = static_cast<char>(character);
              break;
            }
          }
          text += found;
        }
        while (!text.empty() && text.back() == ' ')
        {
          text.pop_back();
        }
        return text;
      }

      NullEffects effects;
      ScriptedListKeys keys;

      Canvas canvas;
      Elite::TextState text;
      Elite::TextPrinter screen;
      Elite::CharacterPrinter characters;
      Elite::TokenPrinter recursive;
      Elite::Rng rng;

      Elite::CommanderBlock commander = Elite::DefaultCommander();
      std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
      Elite::SystemSeeds currentSeeds = Lave();
      Elite::SystemSeeds selectedSeeds = Lave();
      MarketState market = TenOfEverything();

      Elite::StateTokens values;
      Elite::ExtendedTokenPrinter extended;
      Elite::TradeScreen trade;
    };
  } // namespace CargoFixture

  using namespace CargoFixture;

  TEST_CLASS(TheQuantityPromptTakesDigitsAndBackspace)
  {
  public:
    TEST_METHOD(DigitsBuildTheValueAndDeleteTakesThemBack)
    {
      QuantityEntry entry;
      Assert::IsTrue(QuantityStep::Typed == Elite::EditQuantity(entry, Digit('1')));
      Assert::IsTrue(QuantityStep::Typed == Elite::EditQuantity(entry, Digit('2')));
      Assert::AreEqual<std::uint32_t>(12u, entry.value);
      Assert::AreEqual<std::uint32_t>(2u, entry.digits);

      Assert::IsTrue(QuantityStep::Deleted == Elite::EditQuantity(entry, Delete()));
      Assert::AreEqual<std::uint32_t>(1u, entry.value);
      Assert::AreEqual<std::uint32_t>(1u, entry.digits);

      Assert::IsTrue(QuantityStep::Deleted == Elite::EditQuantity(entry, Delete()));
      Assert::AreEqual<std::uint32_t>(0u, entry.value);
      Assert::AreEqual<std::uint32_t>(0u, entry.digits);

      Assert::IsTrue(QuantityStep::Refused == Elite::EditQuantity(entry, Delete()), L"backspace on an empty prompt beeps");
    }

    TEST_METHOD(ThreeDigitsAndAByteAreTheLimits)
    {
      QuantityEntry entry;
      (void)Elite::EditQuantity(entry, Digit('1'));
      (void)Elite::EditQuantity(entry, Digit('0'));
      (void)Elite::EditQuantity(entry, Digit('0'));
      Assert::IsTrue(QuantityStep::Refused == Elite::EditQuantity(entry, Digit('0')), L"a fourth digit is refused");
      Assert::AreEqual<std::uint32_t>(100u, entry.value);

      QuantityEntry big;
      (void)Elite::EditQuantity(big, Digit('2'));
      (void)Elite::EditQuantity(big, Digit('5'));
      Assert::IsTrue(QuantityStep::Refused == Elite::EditQuantity(big, Digit('6')), L"256 does not fit a byte");
      Assert::IsTrue(QuantityStep::Typed == Elite::EditQuantity(big, Digit('5')), L"255 does");
      Assert::AreEqual<std::uint32_t>(255u, big.value);

      QuantityEntry zeros;
      (void)Elite::EditQuantity(zeros, Digit('0'));
      (void)Elite::EditQuantity(zeros, Digit('0'));
      Assert::AreEqual<std::uint32_t>(2u, zeros.digits, L"zeros count as digits, so they can be deleted");
      Assert::AreEqual<std::uint32_t>(0u, zeros.value);
    }

    TEST_METHOD(ReturnCommitsAValueAndCancelsNothingOrZero)
    {
      QuantityEntry empty;
      Assert::IsTrue(QuantityStep::Cancelled == Elite::EditQuantity(empty, Return()));

      QuantityEntry zero;
      (void)Elite::EditQuantity(zero, Digit('0'));
      Assert::IsTrue(QuantityStep::Cancelled == Elite::EditQuantity(zero, Return()));

      QuantityEntry some;
      (void)Elite::EditQuantity(some, Digit('7'));
      Assert::IsTrue(QuantityStep::Committed == Elite::EditQuantity(some, Return()));
      Assert::AreEqual<std::uint32_t>(7u, some.value);

      QuantityEntry escaped;
      (void)Elite::EditQuantity(escaped, Digit('7'));
      Assert::IsTrue(QuantityStep::Cancelled == Elite::EditQuantity(escaped, Escape()));

      QuantityEntry untouched;
      (void)Elite::EditQuantity(untouched, Digit('3'));
      Assert::IsTrue(QuantityStep::Ignored == Elite::EditQuantity(untouched, Up()));
      Assert::IsTrue(QuantityStep::Ignored == Elite::EditQuantity(untouched, Down()));
      Assert::IsTrue(QuantityStep::Ignored == Elite::EditQuantity(untouched, Buy()));
      Assert::IsTrue(QuantityStep::Ignored == Elite::EditQuantity(untouched, Other(0x28)));
      Assert::AreEqual<std::uint32_t>(3u, untouched.value, L"an ignored key changes nothing");
    }
  };

  TEST_CLASS(TheBuyCargoScreen)
  {
  public:
    TEST_METHOD(DrawsTheMarketWithTheBarOnTheFirstItem)
    {
      Screen fixture;
      fixture.keys.keys = {Other(Elite::KEY_INVENTORY)};

      const std::uint8_t left = fixture.RunBuyScreen();

      Assert::AreEqual<std::uint32_t>(Elite::KEY_INVENTORY, left, L"left on the key that was pressed");
      Assert::AreEqual<std::uint32_t>(Elite::BUY_CARGO_VIEW, fixture.effects.view);
      Assert::AreEqual(std::string(" BUY CARGO"), fixture.Row(Elite::TRADE_TITLE_ROW), L"the title, and no prompt yet");
      Assert::AreEqual(std::string(" Use the cursor keys to select"), fixture.Row(2));
      Assert::AreEqual(std::string(" an item. Press B to buy it."), fixture.Row(3));
      Assert::AreEqual(std::string(" PRODUCT    UNIT PRICE QTY"), fixture.Row(Elite::BUY_CARGO_HEADING_ROW));

      for (int item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        const std::uint8_t row = static_cast<std::uint8_t>(Elite::BUY_CARGO_FIRST_ITEM_ROW + item);
        const bool inverted = (item == 0);
        Assert::AreEqual(inverted, fixture.RowIsInverted(row), Widen("bar on item " + std::to_string(item)).c_str());
        Assert::AreEqual(fixture.ExpectedItemLine(item), fixture.Row(row, inverted),
                         Widen("line for item " + std::to_string(item)).c_str());
      }

      Assert::AreEqual(std::string(" Food         t   3.6   10t"), fixture.Row(Elite::BUY_CARGO_FIRST_ITEM_ROW, true),
                       L"and the first line reads as the market line it is");
      Assert::AreEqual('-', fixture.Row(Elite::BUY_CARGO_FIRST_ITEM_ROW + 3).back(), L"a dash for nothing");
      Assert::AreEqual(std::string(" CASH:    100.0 CR"), fixture.Row(Elite::BUY_CARGO_CASH_ROW), L"dn's line: the token pads the amount");
      Assert::AreEqual(1, fixture.effects.setUps, L"one TRADEMODE");
      Assert::AreEqual(0, fixture.effects.beeps);
      Assert::IsFalse(fixture.keys.overran);
    }

    TEST_METHOD(TheCursorKeysMoveTheBarAndWrap)
    {
      Screen fixture;
      fixture.keys.keys = {Down(), Down(), Up(), Up(), Up(), Other(Elite::KEY_STATUS)};

      const std::uint8_t left = fixture.RunBuyScreen();

      Assert::AreEqual<std::uint32_t>(Elite::KEY_STATUS, left);
      for (int item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        const std::uint8_t row = static_cast<std::uint8_t>(Elite::BUY_CARGO_FIRST_ITEM_ROW + item);
        Assert::AreEqual(item == Elite::MARKET_ITEM_COUNT - 1, fixture.RowIsInverted(row),
                         Widen("after two down and three up the bar is on the last item, not " + std::to_string(item)).c_str());
      }
      Assert::AreEqual(fixture.ExpectedItemLine(Elite::MARKET_ITEM_COUNT - 1), fixture.Row(Elite::BUY_CARGO_FIRST_ITEM_ROW + 16, true),
                       L"the line under the bar is intact");
      Assert::AreEqual(fixture.ExpectedItemLine(0), fixture.Row(Elite::BUY_CARGO_FIRST_ITEM_ROW), L"and so is the one it left");

      Screen wrapDown;
      std::vector<ListKeyPress> script(Elite::MARKET_ITEM_COUNT, Down());
      script.push_back(Other(Elite::KEY_STATUS));
      wrapDown.keys.keys = script;
      (void)wrapDown.RunBuyScreen();
      Assert::IsTrue(wrapDown.RowIsInverted(Elite::BUY_CARGO_FIRST_ITEM_ROW), L"seventeen downs come back to the top");
    }

    TEST_METHOD(BuyingMovesTheHoldTheMarketAndTheCash)
    {
      Screen fixture;
      fixture.keys.keys = {Down(), Buy(), Digit('3'), Return(), Other(Elite::KEY_INVENTORY)};

      const std::uint32_t cashBefore = fixture.commander.Cash();
      const std::uint8_t price = Elite::MarketPrice(1, ECONOMY, 0);
      (void)fixture.RunBuyScreen();

      const std::size_t hold = static_cast<std::size_t>(Elite::Field::CargoHold);
      Assert::AreEqual<std::uint32_t>(3u, fixture.commander.bytes[hold + 1], L"three tonnes of textiles in the hold");
      Assert::AreEqual<std::uint32_t>(7u, fixture.market.availability[1], L"and seven left on the shelf");
      Assert::AreEqual<std::uint32_t>(cashBefore - Elite::TotalPrice(price, 3), fixture.commander.Cash(), L"paid GCASH's price");
      Assert::AreEqual(1, fixture.effects.beeps, L"dn2 once");

      Assert::AreEqual(std::string(" BUY CARGO"), fixture.Row(Elite::TRADE_TITLE_ROW), L"the prompt is gone");
      Assert::IsTrue(fixture.RowIsInverted(Elite::BUY_CARGO_FIRST_ITEM_ROW + 1), L"the bar is still on textiles");
      Assert::AreEqual(fixture.ExpectedItemLine(1), fixture.Row(Elite::BUY_CARGO_FIRST_ITEM_ROW + 1, true),
                       L"the line shows the new quantity");
      Assert::IsTrue(fixture.Row(Elite::BUY_CARGO_FIRST_ITEM_ROW + 1, true).find("7t") != std::string::npos);
      const std::string cashLine = fixture.Row(Elite::BUY_CARGO_CASH_ROW);
      const std::string amount =
        std::to_string(fixture.commander.Cash() / 10) + "." + std::to_string(fixture.commander.Cash() % 10) + " CR";
      Assert::AreEqual(std::string(" CASH:"), cashLine.substr(0, 6), L"the cash line was reprinted");
      Assert::AreEqual(amount, cashLine.substr(cashLine.size() - amount.size()), L"with the new amount, and only once");
      Assert::IsFalse(fixture.keys.overran);
    }

    TEST_METHOD(ThePromptEchoesDigitsAndBackspace)
    {
      Screen fixture;
      fixture.keys.keys = {
        Buy(), Digit('1'), Digit('2'), Delete(), Digit('0'), Other(Elite::KEY_STATUS), Escape(), Other(Elite::KEY_STATUS)};

      // The Other in the middle is IGNORED while the prompt is open, which is why the script needs
      // the Escape and a second one to get out.
      const std::uint8_t left = fixture.RunBuyScreen();

      Assert::AreEqual<std::uint32_t>(Elite::KEY_STATUS, left);
      Assert::AreEqual<std::size_t>(8u, fixture.keys.taken, L"every key was read, including the ignored one");
      Assert::AreEqual(std::string(" BUY CARGO"), fixture.Row(Elite::TRADE_TITLE_ROW), L"Escape cleared the prompt");
      Assert::AreEqual<std::uint32_t>(0u, fixture.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold)], L"nothing bought");
      Assert::AreEqual<std::uint32_t>(10u, fixture.market.availability[0]);
    }

    TEST_METHOD(ThePromptShowsWhatIsTyped)
    {
      // Stop INSIDE the prompt: the script runs out there, and the overrun key is Other, which the
      // prompt ignores... so give it an Escape after looking. The look is done by a key source that
      // snapshots the canvas when asked for the key after "11".
      class Peeking final : public Elite::ListKeySource
      {
      public:
        explicit Peeking(Screen& _fixture)
          : fixture(_fixture)
        {
        }
        ListKeyPress NextListKey() override
        {
          switch (step++)
          {
          case 0:
            return Buy();
          case 1:
            return Digit('1');
          case 2:
            return Digit('1');
          case 3:
            title = fixture.Row(Elite::TRADE_TITLE_ROW);
            return Delete();
          case 4:
            afterDelete = fixture.Row(Elite::TRADE_TITLE_ROW);
            return Return();
          default:
            return Other(Elite::KEY_STATUS);
          }
        }
        Screen& fixture;
        int step = 0;
        std::string title;
        std::string afterDelete;
      };

      Screen fixture;
      Peeking keys(fixture);
      const std::uint8_t left =
        Elite::BuyCargoScreen(fixture.trade, keys, fixture.canvas, fixture.commander, fixture.market, ECONOMY, 0xFF);

      Assert::AreEqual<std::uint32_t>(Elite::KEY_STATUS, left);
      Assert::AreEqual(std::string(" BUY CARGO       QUANTITY? 11"), keys.title, L"the prompt and the digits, on the title row");
      Assert::AreEqual(std::string(" BUY CARGO       QUANTITY? 1"), keys.afterDelete, L"backspace rubbed one out");
      Assert::AreEqual<std::uint32_t>(1u, fixture.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold)],
                                      L"and Return bought the one that was left");
    }

    TEST_METHOD(ReturnOnNothingOrZeroCancels)
    {
      Screen fixture;
      fixture.keys.keys = {Buy(), Return(), Buy(), Digit('0'), Return(), Other(Elite::KEY_STATUS)};

      const std::uint32_t cashBefore = fixture.commander.Cash();
      (void)fixture.RunBuyScreen();

      Assert::AreEqual<std::uint32_t>(cashBefore, fixture.commander.Cash());
      Assert::AreEqual<std::uint32_t>(10u, fixture.market.availability[0]);
      Assert::AreEqual(0, fixture.effects.beeps, L"cancelling is silent");
      Assert::AreEqual(std::string(" BUY CARGO"), fixture.Row(Elite::TRADE_TITLE_ROW));
    }

    TEST_METHOD(BOnADashRingsTheBellAndOpensNothing)
    {
      Screen fixture;
      // Slaves is item 3 and has none. The digits that follow are read as SELECTION keys and ignored,
      // which is the proof that no prompt opened.
      fixture.keys.keys = {Down(), Down(), Down(), Buy(), Digit('5'), Return(), Other(Elite::KEY_STATUS)};

      const std::uint32_t cashBefore = fixture.commander.Cash();
      (void)fixture.RunBuyScreen();

      Assert::AreEqual<std::uint32_t>(cashBefore, fixture.commander.Cash());
      Assert::AreEqual(std::string(" BUY CARGO"), fixture.Row(Elite::TRADE_TITLE_ROW), L"no prompt");
      Assert::IsTrue(fixture.RowIsInverted(Elite::BUY_CARGO_FIRST_ITEM_ROW + 3));
    }

    TEST_METHOD(TooManyIsRefusedWithTheOriginalsWord)
    {
      class Peeking final : public Elite::ListKeySource
      {
      public:
        explicit Peeking(Screen& _fixture)
          : fixture(_fixture)
        {
        }
        ListKeyPress NextListKey() override
        {
          switch (step++)
          {
          case 0:
            return Buy();
          case 1:
            return Digit('1');
          case 2:
            return Digit('1');
          case 3:
            return Return();
          default:
            afterwards = fixture.Row(Elite::TRADE_TITLE_ROW);
            return Other(Elite::KEY_STATUS);
          }
        }
        Screen& fixture;
        int step = 0;
        std::string afterwards;
      };

      // The complaint is on screen only during BeepAndPause, so the effects record it.
      class Watching final : public Elite::TradeScreenEffects
      {
      public:
        explicit Watching(Screen& _fixture)
          : fixture(_fixture)
        {
        }
        void SetUpTradeScreen(std::uint8_t _view) override
        {
          fixture.effects.SetUpTradeScreen(_view);
        }
        void ClearBottomRows() override {}
        void ClearToView(std::uint8_t _view) override
        {
          fixture.effects.ClearToView(_view);
        }
        void ResetMissileIndicators() override {}
        void BeepAndPause() override
        {
          duringPause = fixture.Row(Elite::TRADE_TITLE_ROW);
          ++beeps;
        }
        Screen& fixture;
        std::string duringPause;
        int beeps = 0;
      };

      Screen fixture;
      Peeking keys(fixture);
      Watching effects(fixture);
      Elite::TradeScreen trade{fixture.recursive, fixture.characters, fixture.extended, fixture.text, fixture.effects,
                               effects,           fixture.rng};

      const std::uint32_t cashBefore = fixture.commander.Cash();
      (void)Elite::BuyCargoScreen(trade, keys, fixture.canvas, fixture.commander, fixture.market, ECONOMY, 0xFF);

      Assert::AreEqual(1, effects.beeps);
      Assert::AreEqual(std::string(" BUY CARGO       QUANTITY?"), effects.duringPause, L"TQ4's word, where the prompt was");
      Assert::AreEqual(std::string(" BUY CARGO"), keys.afterwards, L"and gone again after the pause");
      Assert::AreEqual<std::uint32_t>(cashBefore, fixture.commander.Cash());
      Assert::AreEqual<std::uint32_t>(10u, fixture.market.availability[0]);
      Assert::AreEqual<std::uint32_t>(0u, fixture.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold)]);
    }

    TEST_METHOD(NoRoomAndNoCashAreRefusedInTheOriginalsOrder)
    {
      // No room: fill the hold. CRGO is the capacity plus two, so 22 means twenty tonnes, and the
      // first thirteen items share it.
      Screen full;
      full.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold)] = 20;
      full.keys.keys = {Buy(), Digit('1'), Return(), Other(Elite::KEY_STATUS)};
      Assert::IsFalse(Elite::CargoFits(full.commander, 0, 1), L"the premise: one more tonne does not fit");

      const std::uint32_t fullCash = full.commander.Cash();
      (void)full.RunBuyScreen();
      Assert::AreEqual(1, full.effects.beeps, L"CARGO? beeped");
      Assert::AreEqual<std::uint32_t>(fullCash, full.commander.Cash());
      Assert::AreEqual<std::uint32_t>(20u, full.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold)]);
      Assert::AreEqual<std::uint32_t>(10u, full.market.availability[0]);

      // No cash: a commander with nothing.
      Screen broke;
      broke.commander.SetCash(0);
      broke.keys.keys = {Buy(), Digit('1'), Return(), Other(Elite::KEY_STATUS)};

      (void)broke.RunBuyScreen();
      Assert::AreEqual(1, broke.effects.beeps, L"CASH? beeped");
      Assert::AreEqual<std::uint32_t>(0u, broke.commander.Cash(), L"LCASH put it back");
      Assert::AreEqual<std::uint32_t>(0u, broke.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold)]);
      Assert::AreEqual<std::uint32_t>(10u, broke.market.availability[0]);
    }

    TEST_METHOD(OnlyAKeyTheDispatchActsOnLeaves)
    {
      Screen fixture;
      // Nothing pressed, a chart letter, a flight control and a digit-less letter: all stay. Then
      // the market key leaves.
      fixture.keys.keys = {Other(0), Other(Elite::KEY_DISTANCE), Other(Elite::KEY_FIND_SYSTEM), Other(0x27),
                           Other(Elite::KEY_MARKET_PRICE)};

      const std::uint8_t left = fixture.RunBuyScreen();

      Assert::AreEqual<std::uint32_t>(Elite::KEY_MARKET_PRICE, left);
      Assert::AreEqual<std::size_t>(5u, fixture.keys.taken);
      Assert::IsFalse(fixture.keys.overran);

      // A digit is a screen key while nothing is being typed: "7" is F4's position, and "0" is
      // nobody's. And inside the prompt, the same "7" is seven.
      Screen seven;
      seven.keys.keys = {Digit('0', 0x1D), Digit('7', Elite::KEY_MARKET_PRICE)};
      Assert::AreEqual<std::uint32_t>(Elite::KEY_MARKET_PRICE, seven.RunBuyScreen(), L"7 on the list is the market screen");
      Assert::AreEqual<std::size_t>(2u, seven.keys.taken, L"and 0 on the list is nothing");

      Screen typed;
      typed.keys.keys = {Buy(), Digit('7', Elite::KEY_MARKET_PRICE), Return(), Other(Elite::KEY_STATUS)};
      Assert::AreEqual<std::uint32_t>(Elite::KEY_STATUS, typed.RunBuyScreen());
      Assert::AreEqual<std::uint32_t>(7u, typed.commander.bytes[static_cast<std::size_t>(Elite::Field::CargoHold)],
                                      L"7 in the prompt is seven");

      // And every screen key TT102 knows leaves, including the one for this screen.
      const std::uint8_t SCREEN_KEYS[] = {Elite::KEY_LAUNCH,     Elite::KEY_BUY_CARGO,   Elite::KEY_SELL_CARGO,     Elite::KEY_EQUIP_SHIP,
                                          Elite::KEY_LONG_RANGE, Elite::KEY_SHORT_RANGE, Elite::KEY_DATA_ON_SYSTEM, Elite::KEY_STATUS,
                                          Elite::KEY_INVENTORY,  Elite::KEY_DISK_ACCESS};
      for (const std::uint8_t key : SCREEN_KEYS)
      {
        Screen each;
        each.keys.keys = {Other(key)};
        Assert::AreEqual<std::uint32_t>(key, each.RunBuyScreen(), Widen("key " + std::to_string(key) + " leaves").c_str());
      }
    }
  };

  TEST_CLASS(TheMarketPricesScreen)
  {
  public:
    TEST_METHOD(NamesTheSystemAndListsBuyPricesOnly)
    {
      Screen fixture;
      Elite::MarketPricesScreen(fixture.trade, fixture.canvas, fixture.currentSeeds, ECONOMY, fixture.market, false);

      Assert::AreEqual<std::uint32_t>(Elite::BUY_CARGO_VIEW, fixture.effects.view, L"the view TT167 has always been given here");
      Assert::AreEqual(std::string(" LAVE MARKET PRICES"), fixture.Row(Elite::TRADE_TITLE_ROW));
      Assert::AreEqual(std::string(""), fixture.Row(2));
      Assert::AreEqual(std::string(" PRODUCT    UNIT BUY  / SELL"), fixture.Row(Elite::MARKET_PRICES_HEADING_ROW));
      Assert::AreEqual(std::string(""), fixture.Row(4));

      for (int item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        const std::uint8_t row = static_cast<std::uint8_t>(Elite::MARKET_PRICES_FIRST_ITEM_ROW + item);
        const std::string line = fixture.Row(row);
        const std::string expected = fixture.ExpectedItemLine(item);

        // The line is the market line up to and including the price -- the first 21 cells -- and
        // nothing after it: no quantity, and nothing in the sell column.
        Assert::AreEqual(expected.substr(0, 21), line.substr(0, 21),
                         Widen("name, units and price for item " + std::to_string(item)).c_str());
        Assert::AreEqual<std::size_t>(21u, line.size(), Widen("and nothing past the price for item " + std::to_string(item)).c_str());
        Assert::IsFalse(fixture.RowIsInverted(row));
      }

      Assert::AreEqual(std::string(" Food         t   3.6"), fixture.Row(Elite::MARKET_PRICES_FIRST_ITEM_ROW));
      Assert::AreEqual(std::string(" Gold         kg"), fixture.Row(Elite::MARKET_PRICES_FIRST_ITEM_ROW + 13).substr(0, 16),
                       L"kilos, and TT161's missing space before the price");
      Assert::AreEqual<std::uint32_t>(0u, fixture.market.availability[16], L"var's side effect: printing prices zeroes Alien Items");
      Assert::AreEqual(std::string(""), fixture.Row(Elite::BUY_CARGO_CASH_ROW), L"no cash line on a read-only screen");
    }

    TEST_METHOD(InWitchspaceThereAreNoLines)
    {
      Screen fixture;
      Elite::MarketPricesScreen(fixture.trade, fixture.canvas, fixture.currentSeeds, ECONOMY, fixture.market, true);

      Assert::AreEqual(std::string(" LAVE MARKET PRICES"), fixture.Row(Elite::TRADE_TITLE_ROW));
      for (int item = 0; item < Elite::MARKET_ITEM_COUNT; ++item)
      {
        Assert::AreEqual(std::string(""), fixture.Row(static_cast<std::uint8_t>(Elite::MARKET_PRICES_FIRST_ITEM_ROW + item)));
      }
    }
  };

} // namespace GameLogicTests
