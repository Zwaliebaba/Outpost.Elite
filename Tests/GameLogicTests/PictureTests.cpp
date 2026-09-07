#include "pch.h"

#include "Canvas.h"
#include "Picture.h"
#include "StateHash.h"
#include "Universe.h"
#include "VideoState.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The 640x400 picture (Design/Resolution.md slice RS-0).
 *
 * IT HAS NO ORACLE AND WILL NEVER HAVE ONE. The game never rendered a picture into memory at any
 * resolution, so there is nothing to compare one against -- which is the same position `Canvas::
 * Resolve`'s sprite overlay has been in since ADR-005 section 1, and the answer is the same shape:
 * properties that must hold, not a recorded image somebody accepted by eye.
 *
 * The strongest of them is the one this slice can state exactly. Until a region draws itself, its
 * pixels ARE the canvas's doubled -- so `picture[y][x] == canvas[y / 2][x / 2]` everywhere, for
 * every scene, sprites and all. That single equation is what makes "the game plays at 640x400
 * looking exactly as today" a test rather than a claim, and it is what the later slices retire one
 * region at a time.
 */
namespace GameLogicTests
{

  namespace
  {
    using Elite::Canvas;
    using Elite::Picture;

    /// The two surfaces resolved side by side, so a test can compare them pixel for pixel.
    struct Pictures
    {
      std::array<std::uint8_t, static_cast<std::size_t>(Canvas::WIDTH) * Canvas::HEIGHT> canvas{};
      std::vector<std::uint8_t> picture;

      Pictures()
        : picture(static_cast<std::size_t>(Picture::WIDTH) * Picture::HEIGHT, std::uint8_t{0})
      {
      }

      [[nodiscard]] std::uint8_t OnCanvas(int _x, int _y) const
      {
        return canvas[static_cast<std::size_t>(_y) * Canvas::WIDTH + _x];
      }
      [[nodiscard]] std::uint8_t OnPicture(int _x, int _y) const
      {
        return picture[static_cast<std::size_t>(_y) * Picture::WIDTH + _x];
      }
    };

    /// Every pixel of both, with the sprites composited when `_video` is given.
    Pictures ResolveBoth(const Canvas& _canvas, const Picture& _picture, const Elite::VideoState* _video)
    {
      Pictures out;
      if (_video != nullptr)
      {
        _canvas.Resolve(out.canvas, *_video);
        _picture.Resolve(out.picture, _canvas, *_video);
      }
      else
      {
        _canvas.Resolve(out.canvas);
        _picture.Resolve(out.picture, _canvas);
      }
      return out;
    }

    /// The property RS-0 exists to make true. Returns an empty string, or the first pixel that
    /// breaks it -- the first, because a broken doubling breaks tens of thousands and a test that
    /// printed them all would print nothing anybody reads.
    std::wstring TheCanvasDoubled(const Pictures& _both)
    {
      for (int y = 0; y < Picture::HEIGHT; ++y)
      {
        for (int x = 0; x < Picture::WIDTH; ++x)
        {
          const std::uint8_t want = _both.OnCanvas(x / 2, y / 2);
          const std::uint8_t got = _both.OnPicture(x, y);
          if (want != got)
          {
            return L"picture (" + std::to_wstring(x) + L", " + std::to_wstring(y) + L") is " + std::to_wstring(got) +
                   L" and the canvas pixel it doubles is " + std::to_wstring(want);
          }
        }
      }
      return {};
    }

    /// A scene with something in every part of the screen: bitmap bytes across the whole plane,
    /// a different palette in every cell, colour RAM under it, and the dashboard switched on.
    void DrawAScene(Canvas& _canvas)
    {
      for (std::uint16_t offset = 0; offset < Canvas::BITMAP_SIZE; ++offset)
      {
        _canvas.Write(offset, static_cast<std::uint8_t>((offset * 37u) ^ (offset >> 5)));
      }
      for (int cell = 0; cell < Canvas::CELL_COLUMNS * Canvas::CELL_ROWS; ++cell)
      {
        _canvas.Write(static_cast<std::uint16_t>(Canvas::SCREEN_CELLS + cell), static_cast<std::uint8_t>(0x10u + (cell % 0xEFu)));
        _canvas.Write(static_cast<std::uint16_t>(Canvas::DASHBOARD_CELLS + cell), static_cast<std::uint8_t>(0x21u + (cell % 0xDEu)));
        _canvas.SetCellColour(cell, static_cast<std::uint8_t>(cell % 16));
      }
    }
  } // namespace

  TEST_CLASS(ThePicture)
  {
  public:
    TEST_METHOD(TheGeometryIsTheCanvasDoubled)
    {
      Assert::AreEqual(640, Picture::WIDTH, L"the picture is twice the canvas across");
      Assert::AreEqual(400, Picture::HEIGHT, L"and twice down");
      Assert::AreEqual(Canvas::WIDTH * 2, Picture::WIDTH);
      Assert::AreEqual(Canvas::HEIGHT * 2, Picture::HEIGHT);
      Assert::AreEqual(80, Picture::CELL_COLUMNS, L"a glyph is still 8x8, so there are twice as many cells across");
      Assert::AreEqual(50, Picture::CELL_ROWS);
      Assert::AreEqual(288, Picture::SPACE_VIEW_HEIGHT, L"the space view is 2 * 144");
      Assert::AreEqual(112, Picture::DASHBOARD_HEIGHT, L"and the dashboard 2 * 56");
      Assert::AreEqual(36, Picture::DASHBOARD_CELL_ROW);
      Assert::AreEqual(64, Picture::SPACE_VIEW_MARGIN, L"eight cells of margin, which is the canvas's four doubled");

      // The margin is a MARGIN. 512 columns of view inside 640 leaves 64 either side, exactly as
      // 256 inside 320 leaves 32 -- so the field of view is unchanged and nothing new is visible.
      Assert::AreEqual(Picture::WIDTH - 2 * Picture::SPACE_VIEW_MARGIN, 2 * (Canvas::WIDTH - 2 * Canvas::SPACE_VIEW_MARGIN),
                       L"the view inside the margins is the canvas's, doubled");
    }

    /// The acceptance of RS-0, over a scene with something everywhere in it.
    TEST_METHOD(WithNoRegionOfItsOwnItIsTheCanvasDoubled)
    {
      Canvas canvas;
      DrawAScene(canvas);
      const Picture picture;

      for (const bool dashboard : {false, true})
      {
        canvas.SetDashboardShown(dashboard);
        const std::wstring wrong = TheCanvasDoubled(ResolveBoth(canvas, picture, nullptr));
        Assert::IsTrue(wrong.empty(), (std::wstring(dashboard ? L"dashboard shown: " : L"docked screen: ") + wrong).c_str());
      }
    }

    /*
     * The same with the hardware sprites over it, which is what the presenter actually shows.
     *
     * Sprites are the part of the doubling most likely to be one pixel out, because a sprite's
     * position, its expand flag and the output's scale all multiply -- so this is the equation
     * again over a scene with an ordinary sprite, an expanded one and one hanging off each edge.
     */
    TEST_METHOD(TheSpritesDoubleWithIt)
    {
      Canvas canvas;
      DrawAScene(canvas);
      canvas.SetDashboardShown(true);
      const Picture picture;

      Elite::VideoState video;
      video.enabled = 0b0000'1111u;
      video.expanded = 0b0000'0010u; // one of them double size, which multiplies with the doubling
      for (std::size_t sprite = 0; sprite < 4; ++sprite)
      {
        canvas.Write(static_cast<std::uint16_t>(Canvas::SCREEN_CELLS + 0x3F8u + sprite),
                     static_cast<std::uint8_t>(Elite::SPRITE_POINTER_ORIGIN + sprite));
        video.colour[sprite] = Elite::ColourOf(static_cast<std::uint8_t>(3u + sprite));
      }

      // One in the middle, one over the raster split, one off the left edge and one off the right.
      video.x = {130u, 200u, 8u, 330u, 0u, 0u, 0u, 0u};
      video.y = {80u, 190u, 120u, 60u, 0u, 0u, 0u, 0u};

      const std::wstring wrong = TheCanvasDoubled(ResolveBoth(canvas, picture, &video));
      Assert::IsTrue(wrong.empty(), (L"with sprites: " + wrong).c_str());
    }

    /// The energy bomb reinterprets the same bytes as two-bit pairs, and it has to do that to both
    /// surfaces at once because it is one flag on one canvas (Resolution.md section 3.2).
    TEST_METHOD(TheEnergyBombReinterpretsBothSurfacesTogether)
    {
      Canvas canvas;
      DrawAScene(canvas);
      canvas.SetSpaceViewMulticolour(true);
      canvas.SetSpaceViewBackground(6u);
      const Picture picture;

      const std::wstring wrong = TheCanvasDoubled(ResolveBoth(canvas, picture, nullptr));
      Assert::IsTrue(wrong.empty(), (L"with the bomb burning: " + wrong).c_str());
    }

    /*
     * A native region stops reading the canvas and draws its own bits.
     *
     * This is the mechanism every later slice turns on, so it is tested before anything uses it:
     * with the space view native, the canvas underneath is ignored entirely and what appears is the
     * bitmap plane through the cell palette.
     */
    TEST_METHOD(ANativeRegionDrawsItsOwnBitsAndIgnoresTheCanvas)
    {
      Canvas canvas;
      DrawAScene(canvas); // deliberately busy, and none of it should appear

      Picture picture;
      picture.SetNative({true, false});
      picture.SetCell(3, 2, Elite::CellPalette{Elite::Colour::White, Elite::Colour::Blue});
      picture.PlotPoint(25, 17); // cell (3, 2), pixel (1, 1) within it

      std::vector<std::uint8_t> out(static_cast<std::size_t>(Picture::WIDTH) * Picture::HEIGHT, std::uint8_t{0});
      picture.Resolve(out, canvas);

      const auto at = [&out](int _x, int _y) { return out[static_cast<std::size_t>(_y) * Picture::WIDTH + _x]; };
      Assert::AreEqual<std::uint32_t>(Elite::ColourIndex(Elite::Colour::White), at(25, 17), L"the lit bit takes the cell's high nibble");
      Assert::AreEqual<std::uint32_t>(Elite::ColourIndex(Elite::Colour::Blue), at(26, 17), L"and a clear one takes the low nibble");
      Assert::AreEqual<std::uint32_t>(0u, at(9, 9), L"a cell nothing wrote is its default palette, not the canvas");
    }

    /// The dashboard is an index plane, because two colours a cell cannot hold a blip, a stick and
    /// the ellipse at once. It is native separately, and only while the dashboard is on screen.
    TEST_METHOD(TheDashboardRegionIsItsOwnIndexPlane)
    {
      Canvas canvas;
      canvas.SetDashboardShown(true);

      Picture picture;
      picture.SetNative({false, true});
      picture.SetDot(100, 300, 5u);
      picture.SetDot(101, 300, 7u);

      std::vector<std::uint8_t> out(static_cast<std::size_t>(Picture::WIDTH) * Picture::HEIGHT, std::uint8_t{0});
      picture.Resolve(out, canvas);

      const auto at = [&out](int _x, int _y) { return out[static_cast<std::size_t>(_y) * Picture::WIDTH + _x]; };
      Assert::AreEqual<std::uint32_t>(5u, at(100, 300), L"an index written to the plane is the index shown");
      Assert::AreEqual<std::uint32_t>(7u, at(101, 300), L"and its neighbour is its own colour, which two per cell could not be");

      // With the dashboard off, all fifty rows are the space view's region and the plane is unread.
      canvas.SetDashboardShown(false);
      picture.Resolve(out, canvas);
      Assert::AreNotEqual<std::uint32_t>(5u, at(100, 300), L"a docked screen has no dashboard region to read the plane from");
    }

    /*
     * Erase-by-redraw is exact here, which is the whole reason the upper surface is a plane of bits
     * rather than an array of colours (Resolution.md section 3.2, ADR-002 section 7).
     */
    TEST_METHOD(PlottingAPointTwiceLeavesTheSurfaceAsItWas)
    {
      Picture picture;
      picture.SetNative({true, false});

      const std::array<std::pair<int, int>, 5> points = {{{0, 0}, {639, 399}, {320, 200}, {7, 8}, {8, 7}}};
      for (const auto& [x, y] : points)
      {
        picture.PlotPoint(x, y);
        Assert::IsTrue(picture.Point(x, y), L"the point did not go on");
        picture.PlotPoint(x, y);
        Assert::IsFalse(picture.Point(x, y), L"and drawing it again did not take it off");
      }

      // And nothing else moved: a blank surface plotted twice is a blank surface.
      for (const std::uint8_t byte : picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"a point drawn twice left something behind");
      }
    }

    /// Out of range reads zero and writes nowhere, rather than wrapping into the wrong row -- the
    /// guard `Canvas` has, for the same reason: a twin that is wrong should draw nothing, not
    /// something plausible eight rows away.
    TEST_METHOD(OutOfRangeIsRefusedRatherThanWrapped)
    {
      Picture picture;
      picture.PlotPoint(-1, 10);
      picture.PlotPoint(Picture::WIDTH, 10);
      picture.PlotPoint(10, -1);
      picture.PlotPoint(10, Picture::HEIGHT);
      picture.SetDot(-1, 300, 9u);
      picture.SetDot(10, 10, 9u);  // above the dashboard region
      picture.SetCell(-1, 0, Elite::CellPalette{Elite::Colour::White, Elite::Colour::White});
      picture.SetCell(0, Picture::CELL_ROWS, Elite::CellPalette{Elite::Colour::White, Elite::Colour::White});

      for (const std::uint8_t byte : picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"an out-of-range plot reached the bitmap");
      }
      for (const std::uint8_t index : picture.Dashboard())
      {
        Assert::AreEqual<std::uint32_t>(0u, index, L"an out-of-range dot reached the dashboard plane");
      }
      Assert::AreEqual<std::uint32_t>(0u, picture.Dot(-1, 300), L"an out-of-range read invented a value");
      Assert::AreEqual<std::uint32_t>(0u, picture.ReadBitmap(Picture::BITMAP_SIZE));
    }

    /// A hash over what `Resolve` produces, for the goldens the later slices record.
    TEST_METHOD(TheHashIsStableAndNoticesOnePixel)
    {
      Canvas canvas;
      DrawAScene(canvas);
      Picture picture;

      const std::uint64_t base = picture.Hash(canvas);
      Assert::AreEqual(base, picture.Hash(canvas), L"the same surface hashed differently twice");

      // While no region is native the hash follows the CANVAS, which is what makes a golden of it
      // meaningful this slice: it is a hash of the doubling.
      canvas.Write(0x0123u, static_cast<std::uint8_t>(canvas.Read(0x0123u) ^ 0xFFu));
      Assert::AreNotEqual(base, picture.Hash(canvas), L"a canvas byte did not move the picture's hash");

      /*
       * With a region native the hash follows the SURFACE -- and the cell has to be painted first,
       * which is not a detail of this test but the behaviour it pins. A cell nobody has coloured is
       * `CellPalette{}`, black over black, so a lit bit and a clear one are both colour 0 and the
       * point is invisible: exactly what `COL2` does on the canvas before `RES2` writes it, where a
       * screen printed without it prints invisibly. This test failed that way when it was written.
       */
      picture.SetNative({true, false});
      picture.SetCell(1, 1, Elite::CellPalette{Elite::Colour::White, Elite::Colour::Black});
      const std::uint64_t native = picture.Hash(canvas);
      picture.PlotPoint(11, 13);
      Assert::AreNotEqual(native, picture.Hash(canvas), L"a plotted point did not move the hash");
    }

    /*
     * The regions are incomplete, and saying so is the point (Risk R27).
     *
     * The last resolution slice deletes the upscale, and what licenses that is `Complete()` -- so
     * this test fails the day every region is native and the fallback is still there, which is the
     * reminder nobody would otherwise get.
     */
    TEST_METHOD(TheRegionsSayWhichSlicesHaveLanded)
    {
      const Picture picture;
      Assert::IsFalse(picture.Native().spaceView, L"RS-1 to RS-3 have not landed");
      Assert::IsFalse(picture.Native().dashboard, L"RS-4 has not landed");
      Assert::IsFalse(picture.Native().Complete(), L"the canvas fallback is still what draws the picture");

      Picture done;
      done.SetNative({true, true});
      Assert::IsTrue(done.Native().Complete(), L"Complete() is what RS-6 asserts before deleting the upscale");
    }

    /*
     * The picture is NOT in the state hash, and that is a decision rather than an omission
     * (Resolution.md section 3.4, and the comment beside the fold in StateHash.cpp).
     *
     * It is asserted here so that adding it later is a test failure with the reason attached,
     * rather than a silent re-recording of five replay tables.
     */
    TEST_METHOD(TheStateHashDeliberatelyDoesNotSeeIt)
    {
      Elite::Universe universe;
      const std::uint64_t base = Elite::HashState(universe);

      universe.picture.SetNative({true, true});
      universe.picture.PlotPoint(3, 4);
      universe.picture.SetDot(5, 300, 9u);
      universe.picture.SetCell(1, 1, Elite::CellPalette{Elite::Colour::White, Elite::Colour::Red});

      Assert::AreEqual(base, Elite::HashState(universe), L"the picture moved the state hash, which would re-record every replay");

      // And the canvas beside it still does, so the exclusion is narrow rather than a hole.
      universe.canvas.Write(0x1234u, 0x5Au);
      Assert::AreNotEqual(base, Elite::HashState(universe), L"a canvas byte stopped moving the state hash");
    }
  };

} // namespace GameLogicTests
