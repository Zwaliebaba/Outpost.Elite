#include "pch.h"

#include "NullSeams.h"

#include "Canvas.h"
#include "Commander.h"
#include "Equipment.h"
#include "Galaxy.h"
#include "ExtendedTokens.h"
#include "MarketScreen.h"
#include "Picture.h"
#include "Ports.h"
#include "StateTokens.h"
#include "StatusScreen.h"
#include "SystemScreen.h"
#include "TextPrint.h"
#include "TextPrint2x.h"
#include "Tokens.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The text layer of the 640x400 picture (Design/Resolution.md section 6, slice RS-1).
 *
 * THE REGION IS NOT SHOWN YET, which is what makes these tests the whole of the slice's evidence.
 * The space view's region flips at RS-3, so every glyph RS-1 draws goes onto a surface nobody
 * presents; there is nothing to look at and no screenshot to sign off. What there is instead is the
 * SHADOW TEST of section 8.1: resolve nothing, read the two surfaces' planes directly, and require
 * that what the faithful printer put on the canvas is on the picture too, once, on the cell the
 * layout names.
 *
 * That property is stronger than it sounds, because the glyph does not change size. The eight bytes
 * on the canvas and the eight on the picture are the SAME eight bytes -- so the test is an equality
 * and not a resemblance, and a twin that dropped a glyph, drew it twice, or put it one cell out
 * fails on the first character printed.
 */
namespace GameLogicTests
{

  namespace
  {
    using Elite::Canvas;
    using Elite::Picture;
    using Elite::TextLayout;

    /// The eight bitmap bytes of one canvas cell.
    std::array<std::uint8_t, 8> CanvasCell(const Canvas& _canvas, int _column, int _row)
    {
      std::array<std::uint8_t, 8> bytes{};
      for (int line = 0; line < 8; ++line)
      {
        bytes[static_cast<std::size_t>(line)] =
          _canvas.Read(static_cast<std::uint16_t>(_row * Canvas::ROW_BYTES + _column * 8 + line));
      }
      return bytes;
    }

    /// And of one wide cell.
    std::array<std::uint8_t, 8> PictureCell(const Picture& _picture, int _column, int _row)
    {
      std::array<std::uint8_t, 8> bytes{};
      for (int line = 0; line < 8; ++line)
      {
        bytes[static_cast<std::size_t>(line)] =
          _picture.ReadBitmap(static_cast<std::size_t>(_row) * Picture::ROW_BYTES + static_cast<std::size_t>(_column) * 8u + line);
      }
      return bytes;
    }

    [[nodiscard]] bool Blank(const std::array<std::uint8_t, 8>& _cell)
    {
      for (const std::uint8_t byte : _cell)
      {
        if (byte != 0u)
        {
          return false;
        }
      }
      return true;
    }

    /*
     * Section 8.1's text property, over whatever the two surfaces currently hold.
     *
     * Three clauses, and the third is the one a re-flow table breaks (Risk R24): every cell the
     * canvas has ink on is on the picture at the mapped cell with the SAME bytes; every cell the
     * picture has ink on came from one the canvas has ink on; and no two canvas cells map onto one
     * wide cell. Returns the first failure, because a mapping that is wrong is wrong everywhere and
     * a test that printed every case would print nothing anybody reads.
     */
    /*
     * `_before` is a screen with its FURNITURE on it and no text -- the border, the rules, whatever
     * `TT66` drew -- and every cell that has not changed since is skipped by both walks.
     *
     * It is needed the moment a real screen is driven rather than a printer: the frame is inked on
     * both surfaces by twins that work in wide coordinates and not through a layout, so a walk that
     * treated every inked canvas cell as a glyph would fail on cell (0, 0) of any screen at all.
     * Passing the same surfaces twice compares everything, which is what a printer-only fixture
     * wants.
     */
    std::wstring TheGlyphsAgree(const Canvas& _canvas, const Picture& _picture, TextLayout _layout, const Canvas& _before,
                                const Picture& _beforePicture)
    {
      std::set<int> occupied;

      const auto canvasChanged = [&](int _column, int _row) {
        return CanvasCell(_canvas, _column, _row) != CanvasCell(_before, _column, _row);
      };
      const auto pictureChanged = [&](int _column, int _row) {
        return PictureCell(_picture, _column, _row) != PictureCell(_beforePicture, _column, _row);
      };

      for (int row = 0; row < Canvas::CELL_ROWS; ++row)
      {
        for (int column = 0; column < Canvas::CELL_COLUMNS; ++column)
        {
          const std::array<std::uint8_t, 8> ink = CanvasCell(_canvas, column, row);
          if (Blank(ink) || (&_before != &_canvas && !canvasChanged(column, row)))
          {
            continue;
          }

          const Elite::WideCell wide =
            _layout.Map(static_cast<std::uint8_t>(column), static_cast<std::uint8_t>(row));
          const std::wstring where = L"canvas cell (" + std::to_wstring(column) + L", " + std::to_wstring(row) + L") -> wide (" +
                                     std::to_wstring(wide.column) + L", " + std::to_wstring(wide.row) + L")";

          if (wide.column < 0 || wide.column >= Picture::CELL_COLUMNS || wide.row < 0 || wide.row >= Picture::CELL_ROWS)
          {
            return where + L": the layout puts it off the wide grid";
          }
          if (PictureCell(_picture, wide.column, wide.row) != ink)
          {
            return where + L": the picture does not carry the glyph the canvas has";
          }

          const int key = wide.row * Picture::CELL_COLUMNS + wide.column;
          if (!occupied.insert(key).second)
          {
            return where + L": two canvas cells were mapped onto one wide cell";
          }
        }
      }

      // And nothing on the picture that the canvas does not account for -- a twin that drew where
      // the faithful printer did not is as wrong as one that failed to draw where it did.
      for (int row = 0; row < Picture::CELL_ROWS; ++row)
      {
        for (int column = 0; column < Picture::CELL_COLUMNS; ++column)
        {
          if (Blank(PictureCell(_picture, column, row)) || (&_beforePicture != &_picture && !pictureChanged(column, row)))
          {
            continue;
          }
          if (occupied.find(row * Picture::CELL_COLUMNS + column) == occupied.end())
          {
            return L"wide cell (" + std::to_wstring(column) + L", " + std::to_wstring(row) +
                   L") has ink the canvas has nowhere";
          }
        }
      }

      return {};
    }

    /*
     * NO ANCHOR BOUNDARY FALLS INSIDE A WORD -- the clause `TheGlyphsAgree` cannot see, and the
     * reason it is here is that the absence of it let two sheared screens through (slice RS-5-d).
     *
     * An anchor covers a RANGE of canvas columns. A range that ends in the middle of a run of ink
     * sends the first half of a word to the anchor's column and the rest to the offsets, and the
     * result -- "Radioacti   ve  s" -- is a mapping that is still perfectly INJECTIVE, still puts
     * every canvas glyph on the picture, and still has nothing on the picture the canvas lacks. All
     * three clauses of the shadow test hold on a screen that is unreadable.
     *
     * So this asks the question those cannot: two canvas cells side by side, both with ink, must
     * land side by side. It is the property a table is FOR, and a sketch is how a person checks it;
     * this is how the suite does.
     */
    std::wstring NothingIsSheared(const Canvas& _canvas, TextLayout _layout, const Canvas& _before)
    {
      for (int row = 0; row < Canvas::CELL_ROWS; ++row)
      {
        for (int column = 0; column + 1 < Canvas::CELL_COLUMNS; ++column)
        {
          const bool here = !Blank(CanvasCell(_canvas, column, row)) && CanvasCell(_canvas, column, row) != CanvasCell(_before, column, row);
          const bool next = !Blank(CanvasCell(_canvas, column + 1, row)) &&
                            CanvasCell(_canvas, column + 1, row) != CanvasCell(_before, column + 1, row);
          if (!here || !next)
          {
            continue;
          }

          const Elite::WideCell left = _layout.Map(static_cast<std::uint8_t>(column), static_cast<std::uint8_t>(row));
          const Elite::WideCell right = _layout.Map(static_cast<std::uint8_t>(column + 1), static_cast<std::uint8_t>(row));
          if (left.row != right.row || right.column != left.column + 1)
          {
            return L"canvas cells (" + std::to_wstring(column) + L", " + std::to_wstring(row) + L") and (" +
                   std::to_wstring(column + 1) + L", " + std::to_wstring(row) +
                   L") both carry ink and are side by side, but the table sends them to (" + std::to_wstring(left.column) + L", " +
                   std::to_wstring(left.row) + L") and (" + std::to_wstring(right.column) + L", " + std::to_wstring(right.row) +
                   L") -- an anchor boundary falls inside a word";
          }
        }
      }
      return {};
    }

    /// The whole of both surfaces, for a fixture that drew nothing but text.
    std::wstring TheGlyphsAgree(const Canvas& _canvas, const Picture& _picture, TextLayout _layout)
    {
      return TheGlyphsAgree(_canvas, _picture, _layout, _canvas, _picture);
    }

    /// A printer wired to both surfaces, as `Game` wires its own.
    struct Printers
    {
      Canvas canvas;
      Picture picture;
      Elite::TextState text;
      Elite::TextLayout layout;
      Elite::TextPrinter printer{canvas, text};

      explicit Printers(std::uint8_t _view)
        : layout(Elite::LayoutForView(_view))
      {
        text.palette = Elite::TEXT_COLOUR_WHITE;
        printer.AttachPicture(&picture, &layout);
      }

      explicit Printers(Elite::TextLayout _layout)
        : layout(_layout)
      {
        text.palette = Elite::TEXT_COLOUR_WHITE;
        printer.AttachPicture(&picture, &layout);
      }

      void Print(const std::string& _text)
      {
        for (const char character : _text)
        {
          (void)printer.Print(static_cast<std::uint8_t>(character));
        }
      }
    };
  } // namespace

  TEST_CLASS(ThePictureText)
  {
  public:
    /// The mapping, stated as the numbers rather than as the formula that produces them.
    TEST_METHOD(TheLayoutPutsACellWhereTheDesignSaysItDoes)
    {
      // A text screen packs the faithful rows and centres the columns: canvas cell 4 -- which is
      // `XC` zero, four cells of margin in -- is wide cell 24, and row 1 is wide row 13.
      Assert::AreEqual(24, Elite::CENTRED_LAYOUT.Map(4, 1).column, L"a text screen's first text cell");
      Assert::AreEqual(13, Elite::CENTRED_LAYOUT.Map(4, 1).row);
      Assert::AreEqual(55, Elite::CENTRED_LAYOUT.Map(35, 1).column, L"and its last, so the 32 used cells are centred");

      // The space view spreads the rows instead, so the message row lands where the original put it.
      Assert::AreEqual(24, Elite::SPACE_VIEW_LAYOUT.Map(4, 16).column, L"the same column");
      Assert::AreEqual(32, Elite::SPACE_VIEW_LAYOUT.Map(4, 16).row, L"and twice the row");

      // And the view byte chooses between them by the game's own test.
      Assert::AreEqual<std::uint32_t>(Elite::SPACE_VIEW_LAYOUT.rowStride, Elite::LayoutForView(0).rowStride, L"view 0 is the space view");
      Assert::AreEqual<std::uint32_t>(Elite::SPACE_VIEW_LAYOUT.rowStride, Elite::LayoutForView(13).rowStride, L"and so is view 13");
      Assert::AreEqual<std::uint32_t>(Elite::CENTRED_LAYOUT.rowStride, Elite::LayoutForView(1).rowStride, L"everything else is a text screen");
      Assert::AreEqual<std::uint32_t>(Elite::CENTRED_LAYOUT.rowStride, Elite::LayoutForView(128).rowStride, L"the short-range chart too");
    }

    /*
     * The anchor, which is the whole of a re-flow (slice RS-5-0).
     *
     * A layout's three offsets move a screen and cannot re-arrange one, and re-arranging is what
     * Resolution.md section 6.3 asks of every docked screen. An anchor is the exception: a
     * RECTANGLE of faithful cells goes where it says, spread by a stride of its own, and every
     * other cell follows the offsets -- so a table can put the status screen's equipment list in a
     * second column without `STATUS` knowing anything about it.
     */
    TEST_METHOD(AnAnchorMovesTheBlockItCoversAndNothingElse)
    {
      // The status screen's equipment list, measured off the screen the port prints: the heading
      // is on canvas row 12 and the items run to row 23, and the whole block goes to the right-hand
      // half at twice the row spacing. ONE anchor, which is the point of it being a rectangle.
      static constexpr std::array<Elite::Anchor, 1> ANCHORS{{{0, 39, 12, 23, 46, 16, 2}}};

      // The offsets of a screen re-flowed for the wider grid rather than centred on it: a re-flow
      // that kept the centred +20 would have nowhere to anchor TO, since a 40-column screen placed
      // at column 20 already covers columns 20 to 59. `NoLayoutSends...` below is what says so.
      constexpr Elite::TextLayout REFLOWED{4, 8, 2, ANCHORS};

      // The block's own origin, and then the cell beside it: a run of glyphs stays a run, which a
      // per-cell anchor could not have done -- the second letter of "EQUIPMENT:" would have been
      // left behind at the offsets.
      Assert::AreEqual(47, REFLOWED.Map(1, 12).column, L"the E of the heading");
      Assert::AreEqual(16, REFLOWED.Map(1, 12).row);
      Assert::AreEqual(48, REFLOWED.Map(2, 12).column, L"and its Q, one cell along as it was");
      Assert::AreEqual(16, REFLOWED.Map(2, 12).row);

      // The anchor's own stride, which is not the layout's: eleven packed rows become twenty-two.
      Assert::AreEqual(52, REFLOWED.Map(6, 13).column, L"the first item, at its indent");
      Assert::AreEqual(18, REFLOWED.Map(6, 13).row);
      Assert::AreEqual(52, REFLOWED.Map(6, 23).column, L"and the last one the list can reach");
      Assert::AreEqual(38, REFLOWED.Map(6, 23).row);

      // Everything outside the rectangle is the offsets, including the row directly above it.
      Assert::AreEqual(5, REFLOWED.Map(1, 11).column, L"a cell above the block keeps the offsets");
      Assert::AreEqual(30, REFLOWED.Map(1, 11).row);

      // A layout with no anchors is the layout it was before this field existed.
      Assert::AreEqual(26, Elite::CENTRED_LAYOUT.Map(6, 13).column, L"an empty table changes nothing");
      Assert::AreEqual(25, Elite::CENTRED_LAYOUT.Map(6, 13).row);
    }

    /*
     * A universe wired to both surfaces, as `Game` wires its own -- what a whole SCREEN needs, as
     * against the printer `Printers` above gives a glyph test.
     */
    struct Docked
    {
      Elite::Universe universe;
      Elite::TextPrinter screen{universe.canvas, universe.text};
      Elite::CharacterPrinter characters{screen, universe.sentences};
      Elite::TokenPrinter printer{characters, universe.text};
      std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
      Elite::StateTokens values;
      Elite::ExtendedTokenPrinter extended;
      NullSeams nulls;
      Elite::SidWriteLog sid;
      Elite::Ports ports;

      Docked()
        : values(printer, universe.text, universe.commander, std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name),
                 universe.current.seeds, universe.selectedSeeds, false),
          extended(characters, printer, universe.rng),
          ports{printer, characters, screen, sid, extended, nulls, nulls, nulls}
      {
        universe.commander = Elite::DefaultCommander();
        universe.text.palette = Elite::TEXT_COLOUR_WHITE;
        universe.current.seeds = universe.commander.galaxySeeds;
        universe.selectedSeeds = universe.commander.galaxySeeds;
        characters.State().sentenceStart = 0xFF;
        printer.SetCaseFlags(0);
        printer.SetValueTokens(&values);
        screen.AttachPicture(&universe.picture, &universe.screenLayout);
      }

      /// Everything in the hold, so a cargo list is as long as it can be.
      void FillTheHold()
      {
        for (std::size_t item = 0; item < universe.commander.cargoHold.size(); ++item)
        {
          universe.commander.cargoHold[item] = static_cast<std::uint8_t>(item + 1u);
        }
      }
    };

    /*
     * THE STATUS SCREEN, re-flowed, against the screen the game prints (slice RS-5-a).
     *
     * The whole re-flow is a table, so what has to be shown is that the table is the ONLY thing
     * that moved: the faithful screen is drawn on the canvas exactly as it always was, every glyph
     * of it is on the wide surface at the cell `STATUS_LAYOUT` names, and the wide surface has
     * nothing the canvas does not account for. `TheGlyphsAgree` is all three clauses at once.
     *
     * A fully-fitted commander, because that is the worst case for the anchored block: seven pieces
     * of equipment and four lasers is eleven lines, which is as far down as the list can reach.
     */
    TEST_METHOD(TheStatusScreenLandsWhereItsTableSaysAndNowhereElse)
    {
      Docked docked;
      Elite::Commander& commander = docked.universe.commander;
      commander.legalStatus = 60; // a fugitive, the longest of the three
      commander.kills.lo = 100;
      commander.escapePod = 1;
      commander.fuelScoops = 1;
      commander.ecm = 1;
      commander.energyBomb = 1;
      commander.energyUnit = 1;
      commander.dockingComputer = 1;
      commander.galacticDrive = 1;
      commander.lasers[0].byte = 15;  // pulse
      commander.lasers[1].byte = 143; // beam
      commander.lasers[2].byte = 151; // military
      commander.lasers[3].byte = 50;  // mining

      /*
       * The frame first, on its own, so that the comparison below is over the TEXT alone. Every
       * docked screen opens with `TRADEMODE`, which redraws the same border and the same rules;
       * running it once here gives a baseline those cells match exactly, and everything that
       * differs afterwards is a glyph.
       */
      Elite::SetUpTradeScreen(docked.universe, docked.ports, Elite::INVENTORY_VIEW, Elite::STATUS_LAYOUT);
      const Canvas frame = docked.universe.canvas;
      const Picture framePicture = docked.universe.picture;

      const Elite::ShipCondition condition{1, 0, 0, 255}; // docked, so the condition line is "Docked"
      Elite::StatusScreen(docked.universe, docked.ports, condition);

      // The screen chose its own table, which is the whole point of the layout parameter: the view
      // byte says 8, and 8 is also the inventory screen.
      Assert::AreEqual<std::uint32_t>(Elite::INVENTORY_VIEW, docked.universe.view, L"the view is still the game's");
      Assert::AreEqual<std::uint32_t>(Elite::STATUS_LAYOUT.rowOffset, docked.universe.screenLayout.rowOffset,
                                      L"and the layout is the status screen's, not LayoutForView's");

      const std::wstring wrong =
        TheGlyphsAgree(docked.universe.canvas, docked.universe.picture, Elite::STATUS_LAYOUT, frame, framePicture);
      Assert::IsTrue(wrong.empty(), wrong.c_str());

      const std::wstring sheared = NothingIsSheared(docked.universe.canvas, Elite::STATUS_LAYOUT, frame);
      Assert::IsTrue(sheared.empty(), sheared.c_str());

      // And the three cells the sketch is made of, stated as numbers so that a table edited by
      // accident fails here rather than on somebody's screen.
      const Elite::WideCell title = Elite::STATUS_LAYOUT.Map(11, 1); // XC 7, which is canvas cell 11
      Assert::AreEqual(31, title.column, L"the title, over both columns");
      Assert::AreEqual(3, title.row, L"above where NLIN3's rule would fall");

      const Elite::WideCell heading = Elite::STATUS_LAYOUT.Map(5, 12);
      Assert::AreEqual(47, heading.column, L"EQUIPMENT: in the right-hand column");
      Assert::AreEqual(12, heading.row, L"level with the first label line");

      const Elite::WideCell lastItem = Elite::STATUS_LAYOUT.Map(10, 23);
      Assert::AreEqual(52, lastItem.column, L"the last equipment line, at its indent");
      Assert::AreEqual(34, lastItem.row, L"and two wide rows below the one before it");
    }

    /*
     * THE THREE TRADE SCREENS, each against the screen the game prints (slice RS-5-b).
     *
     * The same three clauses as the status screen, over the three tables accepted with it: the
     * market list, the inventory and the equipment shop. The market screen is the one worth having
     * a test for beyond the sweep, because its heading is TWO faithful rows folded onto ONE wide
     * one -- the arrangement that would be silently wrong if an anchor were edited, since both rows
     * would still be on the grid, just no longer reading as one phrase.
     */
    TEST_METHOD(TheMarketScreenFoldsItsTwoHeadingRowsOntoOne)
    {
      Docked docked;

      Elite::SetUpTradeScreen(docked.universe, docked.ports, Elite::BUY_CARGO_VIEW, Elite::BUY_LAYOUT);
      const Canvas frame = docked.universe.canvas;
      const Picture framePicture = docked.universe.picture;

      Elite::BuyScreen(docked.universe, docked.ports, false);

      Assert::AreEqual<std::uint32_t>(Elite::BUY_LAYOUT.rowOffset, docked.universe.screenLayout.rowOffset,
                                      L"the buy screen names its own table");

      const std::wstring wrong =
        TheGlyphsAgree(docked.universe.canvas, docked.universe.picture, Elite::BUY_LAYOUT, frame, framePicture);
      Assert::IsTrue(wrong.empty(), wrong.c_str());

      const std::wstring sheared = NothingIsSheared(docked.universe.canvas, Elite::BUY_LAYOUT, frame);
      Assert::IsTrue(sheared.empty(), sheared.c_str());

      // The fold: canvas row 1's "UNIT" and canvas row 2's "PRICE" are on ONE wide row, four cells
      // apart, so they read as the phrase the forty-column screen had to split.
      const Elite::WideCell unit = Elite::BUY_LAYOUT.Map(21, 1);
      const Elite::WideCell price = Elite::BUY_LAYOUT.Map(21, 2);
      Assert::AreEqual(unit.row, price.row, L"the two heading rows land on one");
      Assert::AreEqual(40, unit.column, L"UNIT ...");
      Assert::AreEqual(45, price.column, L"... PRICE, one space along");

      // And each heading sits over the column it heads.
      Assert::AreEqual(Elite::BUY_LAYOUT.Map(5, 4).column, Elite::BUY_LAYOUT.Map(6, 2).column, L"PRODUCT over the names");
      Assert::AreEqual(48, Elite::BUY_LAYOUT.Map(24, 4).column, L"and the price still ends where PrintNumber left it");
    }

    TEST_METHOD(TheInventoryScreenPutsTheHoldInItsOwnColumn)
    {
      Docked docked;
      docked.FillTheHold();

      Elite::SetUpTradeScreen(docked.universe, docked.ports, Elite::INVENTORY_VIEW, Elite::INVENTORY_LAYOUT);
      const Canvas frame = docked.universe.canvas;
      const Picture framePicture = docked.universe.picture;

      Elite::InventoryScreen(docked.universe, docked.ports);

      const std::wstring wrong =
        TheGlyphsAgree(docked.universe.canvas, docked.universe.picture, Elite::INVENTORY_LAYOUT, frame, framePicture);
      Assert::IsTrue(wrong.empty(), wrong.c_str());

      const std::wstring sheared = NothingIsSheared(docked.universe.canvas, Elite::INVENTORY_LAYOUT, frame);
      Assert::IsTrue(sheared.empty(), sheared.c_str());

      // The first item is level with the fuel line and in the other column, which is the sketch.
      const Elite::WideCell fuel = Elite::INVENTORY_LAYOUT.Map(5, 4);
      const Elite::WideCell first = Elite::INVENTORY_LAYOUT.Map(5, 7);
      Assert::AreEqual(fuel.row, first.row, L"the hold starts level with the fuel");
      Assert::AreEqual(6, fuel.column, L"the fuel line on the left");
      Assert::AreEqual(47, first.column, L"and the hold on the right");
    }

    TEST_METHOD(TheEquipScreenBringsItsPromptBackUnderTheList)
    {
      Docked docked;

      Elite::SetUpTradeScreen(docked.universe, docked.ports, Elite::EQUIP_SHIP_VIEW, Elite::EQUIP_LAYOUT);
      const Canvas frame = docked.universe.canvas;
      const Picture framePicture = docked.universe.picture;

      Elite::EquipShipScreen(docked.universe, docked.ports);

      const std::wstring wrong =
        TheGlyphsAgree(docked.universe.canvas, docked.universe.picture, Elite::EQUIP_LAYOUT, frame, framePicture);
      Assert::IsTrue(wrong.empty(), wrong.c_str());

      const std::wstring sheared = NothingIsSheared(docked.universe.canvas, Elite::EQUIP_LAYOUT, frame);
      Assert::IsTrue(sheared.empty(), sheared.c_str());

      /*
       * `CLYNS`'s row 21 is the point of the fourth anchor. At twice the row spacing the offsets
       * would put it on wide row 43, thirty rows below a list that ends at 34; the anchor brings it
       * to 40. A row number that meant "just under the text" at 25 rows does not at 50.
       */
      const Elite::WideCell lastItem = Elite::EQUIP_LAYOUT.Map(7, 16);
      const Elite::WideCell prompt = Elite::EQUIP_LAYOUT.Map(5, 21);
      Assert::AreEqual(34, lastItem.row, L"the thirteenth item, which is as many as any station sells");
      Assert::AreEqual(40, prompt.row, L"and the prompt six rows under it rather than at 43");
      Assert::IsTrue(prompt.row > lastItem.row, L"under the list and not through it");
    }

    /*
     * THE DATA ON SYSTEM SCREEN (slice RS-5-d), whose table is the one that is NOT stride 2.
     *
     * `TT25` double-spaces itself, so the offsets keep the game's own spacing and the anchors do
     * the re-flow. The assertion that matters beyond the sweep is the one about the description:
     * it is the text `DA11` justified to thirty columns, placed in a column of its own, and the
     * picture carries it CHARACTER FOR CHARACTER -- which is what section 6.2's `wrapWidth`
     * measurement gave up a wider paragraph to keep.
     */
    TEST_METHOD(TheDataScreenStandsItsDescriptionBesideThePairs)
    {
      Docked docked;

      Elite::SetUpScreen(docked.universe, docked.ports, Elite::DATA_ON_SYSTEM_VIEW, Elite::DATA_LAYOUT);
      const Canvas frame = docked.universe.canvas;
      const Picture framePicture = docked.universe.picture;

      const Elite::SystemData data = Elite::GenerateSystemData(docked.universe.selectedSeeds);
      Elite::SystemDataScreen(docked.universe, docked.ports, data, 60);

      const std::wstring wrong =
        TheGlyphsAgree(docked.universe.canvas, docked.universe.picture, Elite::DATA_LAYOUT, frame, framePicture);
      Assert::IsTrue(wrong.empty(), wrong.c_str());

      const std::wstring sheared = NothingIsSheared(docked.universe.canvas, Elite::DATA_LAYOUT, frame);
      Assert::IsTrue(sheared.empty(), sheared.c_str());

      // The first pair and the description's first line are level, in two columns.
      const Elite::WideCell pair = Elite::DATA_LAYOUT.Map(5, 3);
      const Elite::WideCell blurb = Elite::DATA_LAYOUT.Map(5, 19);
      Assert::AreEqual(pair.row, blurb.row, L"the description starts level with the first pair");
      Assert::AreEqual(6, pair.column, L"the pairs on the left");
      Assert::AreEqual(47, blurb.column, L"and the description on the right");

      // Stride one, because the screen already spaces itself: a pair and the pair after it are two
      // wide rows apart and not four.
      Assert::AreEqual(2, Elite::DATA_LAYOUT.Map(5, 5).row - Elite::DATA_LAYOUT.Map(5, 3).row,
                       L"the game's own spacing, not the layout's on top of it");
    }

    /*
     * THE PROPERTY EVERY PER-SCREEN TABLE HAS TO KEEP -- Risk R24's tripwire, and the reason it is
     * a test rather than an assertion in the header: nothing about an `Anchor` stops it naming a
     * wide cell the offsets already reach, and two faithful cells landing on one wide cell would
     * print two glyphs into the same eight bytes, exclusive-ored together, which is neither of them.
     *
     * It sweeps the whole 40x25 grid rather than the anchors alone, because the collision that
     * matters is between an ANCHORED cell and an offset one, which no amount of looking at the
     * table by itself would find. Every layout section 6.3 adds joins the list below.
     */
    TEST_METHOD(NoLayoutSendsTwoFaithfulCellsToOneWideCell)
    {
      struct Named
      {
        const wchar_t* what;
        Elite::TextLayout layout;
      };

      // The one anchored table there is until a screen's sketch is accepted, so that the sweep is
      // exercising the anchor path and not two rigid transforms.
      static constexpr std::array<Elite::Anchor, 1> ANCHORS{{{0, 39, 12, 23, 46, 16, 2}}};

      const std::array<Named, 8> LAYOUTS{{{L"the centred layout", Elite::CENTRED_LAYOUT},
                                          {L"the space view", Elite::SPACE_VIEW_LAYOUT},
                                          {L"the status screen", Elite::STATUS_LAYOUT},
                                          {L"the market screens", Elite::BUY_LAYOUT},
                                          {L"the inventory screen", Elite::INVENTORY_LAYOUT},
                                          {L"the equip ship screen", Elite::EQUIP_LAYOUT},
                                          {L"the data on system screen", Elite::DATA_LAYOUT},
                                          {L"an anchored table", Elite::TextLayout{4, 8, 2, ANCHORS}}}};

      for (const Named& named : LAYOUTS)
      {
        std::array<std::uint16_t, 80u * 50u> seen{};
        seen.fill(0xFFFFu);

        for (std::uint8_t row = 0; row < 25u; ++row)
        {
          for (std::uint8_t column = 0; column < 40u; ++column)
          {
            const Elite::WideCell cell = named.layout.Map(column, row);
            if (cell.column < 0 || cell.column >= 80 || cell.row < 0 || cell.row >= 50)
            {
              continue; // off the grid is allowed and is dropped by `PrintGlyph2x`
            }

            const std::size_t index = static_cast<std::size_t>(cell.row) * 80u + static_cast<std::size_t>(cell.column);
            const std::uint16_t here = static_cast<std::uint16_t>(row * 40u + column);
            if (seen[index] != 0xFFFFu)
            {
              const std::wstring message = std::wstring(named.what) + L": faithful cells " + std::to_wstring(seen[index]) +
                                           L" and " + std::to_wstring(here) + L" both land on wide cell " +
                                           std::to_wstring(cell.column) + L"," + std::to_wstring(cell.row);
              Assert::Fail(message.c_str());
            }
            seen[index] = here;
          }
        }
      }
    }

    /// The shadow test, on a text screen and on the space view.
    TEST_METHOD(EveryGlyphTheCanvasHasThePictureHasOnce)
    {
      for (const std::uint8_t view : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{13}, std::uint8_t{128}})
      {
        Printers both(view);
        both.text.row = 3;
        both.text.column = 0;
        both.Print("COMMANDER JAMESON");
        both.text.row = 5;
        both.text.column = 2;
        both.Print("PRESENT SYSTEM : LAVE");

        const std::wstring wrong = TheGlyphsAgree(both.canvas, both.picture, Elite::LayoutForView(view));
        Assert::IsTrue(wrong.empty(), (L"view " + std::to_wstring(view) + L": " + wrong).c_str());
      }
    }

    /// The palette travels with the glyph, because a cell with no palette draws black on black --
    /// which is what `COL2` does before `RES2`, and what RS-0's hash test found the hard way.
    TEST_METHOD(TheCellPaletteTravelsWithTheGlyph)
    {
      Printers both(1);
      both.text.palette = Elite::TEXT_COLOUR_PURPLE;
      both.text.row = 4;
      both.text.column = 1;
      both.Print("A");

      const Elite::WideCell wide = Elite::CENTRED_LAYOUT.Map(Elite::TEXT_FIRST_COLUMN + 1, 4);
      Assert::IsTrue(Elite::TEXT_COLOUR_PURPLE == both.picture.Cell(wide.column, wide.row),
                     L"the wide cell did not take the colour the faithful cell did");
    }

    /*
     * Printing a glyph twice takes it off both surfaces, which is how every message in the game is
     * erased. The picture is a plane of bits precisely so that this holds (Resolution.md §3.2).
     */
    TEST_METHOD(PrintingAGlyphTwiceRemovesItFromBoth)
    {
      Printers both(0);
      both.text.row = 16;
      both.text.column = 6;
      both.Print("INCOMING MISSILE");

      const std::uint64_t drawn = both.picture.Hash(both.canvas);

      both.text.row = 16;
      both.text.column = 6;
      both.Print("INCOMING MISSILE");

      Assert::AreNotEqual(drawn, both.picture.Hash(both.canvas), L"the second print changed nothing");
      Assert::IsTrue(TheGlyphsAgree(both.canvas, both.picture, Elite::SPACE_VIEW_LAYOUT).empty(), L"the erase left the two disagreeing");

      for (const std::uint8_t byte : both.picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"a message printed twice left ink on the picture");
      }
    }

    /// Character 127 -- the one place the text code blanks a cell rather than drawing over it.
    TEST_METHOD(DeleteBlanksTheSameCellOnBoth)
    {
      Printers both(1);
      both.text.row = 6;
      both.text.column = 0;
      both.Print("AB");
      both.Print(std::string(1, static_cast<char>(127)));

      Assert::IsTrue(TheGlyphsAgree(both.canvas, both.picture, Elite::CENTRED_LAYOUT).empty(), L"delete left the two disagreeing");

      const Elite::WideCell wide = Elite::CENTRED_LAYOUT.Map(Elite::TEXT_FIRST_COLUMN + 1, 6);
      Assert::IsTrue(Blank(PictureCell(both.picture, wide.column, wide.row)), L"the deleted cell still has ink on the picture");
    }

    /// `TT66simp` -- a screen change wipes rows 1 to 23 of both surfaces, or the picture keeps the
    /// last screen's text under the next one's.
    TEST_METHOD(AScreenClearWipesBothSurfaces)
    {
      Printers both(1);
      both.text.row = 8;
      both.text.column = 3;
      both.Print("GALACTIC CHART");

      Elite::ClearTextArea(both.canvas, both.text);
      Elite::ClearTextArea2x(both.picture, Elite::CENTRED_LAYOUT);

      Assert::IsTrue(TheGlyphsAgree(both.canvas, both.picture, Elite::CENTRED_LAYOUT).empty(), L"the clear left the two disagreeing");
      for (const std::uint8_t byte : both.picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"the clear left ink on the picture");
      }
    }

    /*
     * The form feed reaches the clear through the printer, which is the path the game actually
     * takes: `CHPR` sees character 12, calls `TT66simp` and prints the character again.
     */
    TEST_METHOD(TheFormFeedClearsThePictureThroughThePrinter)
    {
      Printers both(1);
      both.text.row = 9;
      both.text.column = 4;
      both.Print("STATUS");

      both.Print(std::string(1, static_cast<char>(12)));

      Assert::IsTrue(TheGlyphsAgree(both.canvas, both.picture, Elite::CENTRED_LAYOUT).empty(),
                     L"the form feed left the two disagreeing");
    }

    /// A printer with nothing attached draws the canvas alone, which is what every fixture that
    /// builds one without a universe is comparing.
    TEST_METHOD(AnUnattachedPrinterLeavesThePictureAlone)
    {
      Canvas canvas;
      Picture picture;
      Elite::TextState text;
      text.palette = Elite::TEXT_COLOUR_WHITE;
      Elite::TextPrinter printer(canvas, text);

      text.row = 2;
      for (const char character : std::string("ELITE"))
      {
        (void)printer.Print(static_cast<std::uint8_t>(character));
      }

      Assert::IsTrue(canvas.Hash() != Canvas{}.Hash(), L"the canvas was not drawn on at all");
      for (const std::uint8_t byte : picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"an unattached printer drew on the picture");
      }
    }
  };

} // namespace GameLogicTests
