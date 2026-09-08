#include "pch.h"

#include "Canvas.h"
#include "Picture.h"
#include "TextPrint.h"
#include "TextPrint2x.h"
#include "Tokens.h"

#include <array>
#include <cstdint>
#include <set>
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
    std::wstring TheGlyphsAgree(const Canvas& _canvas, const Picture& _picture, TextLayout _layout)
    {
      std::set<int> occupied;

      for (int row = 0; row < Canvas::CELL_ROWS; ++row)
      {
        for (int column = 0; column < Canvas::CELL_COLUMNS; ++column)
        {
          const std::array<std::uint8_t, 8> ink = CanvasCell(_canvas, column, row);
          if (Blank(ink))
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
          if (Blank(PictureCell(_picture, column, row)))
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

    /// A printer wired to both surfaces, as `Game` wires its own.
    struct Printers
    {
      Canvas canvas;
      Picture picture;
      Elite::TextState text;
      std::uint8_t view = 0;
      Elite::TextPrinter printer{canvas, text};

      explicit Printers(std::uint8_t _view)
        : view(_view)
      {
        text.palette = Elite::TEXT_COLOUR_WHITE;
        printer.AttachPicture(&picture, &view);
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
