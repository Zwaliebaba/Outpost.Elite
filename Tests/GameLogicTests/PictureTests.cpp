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

    /*
     * An empty backdrop, for the fixtures below that build ONE surface (RN-0).
     *
     * `Resolve` composites a frame over a backdrop by exclusive-or, so an empty one resolves to the
     * frame exactly. It is spelled out rather than defaulted so that a test which should be passing
     * a real backdrop cannot do it by omission.
     */
    const Elite::Picture NO_BACKDROP;

    /// Every pixel of both, with the sprites composited when `_video` is given.
    Pictures ResolveBoth(const Canvas& _canvas, const Picture& _picture, const Elite::VideoState* _video)
    {
      Pictures out;
      if (_video != nullptr)
      {
        _canvas.Resolve(out.canvas, *_video);
        _picture.Resolve(out.picture, _canvas, NO_BACKDROP, *_video);
      }
      else
      {
        _canvas.Resolve(out.canvas);
        _picture.Resolve(out.picture, _canvas, NO_BACKDROP);
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

    /*
     * The same with the hardware sprites over it, which is what the presenter actually shows.
     *
     * Sprites are the part of the doubling most likely to be one pixel out, because a sprite's
     * position, its expand flag and the output's scale all multiply -- so this is the equation
     * again over a scene with an ordinary sprite, an expanded one and one hanging off each edge.
     */
    TEST_METHOD(TheSpritesDoubleWithIt)
    {
      /*
       * BOTH SURFACES BLANK UNDER THE SPRITES, which is what RS-6 left available and is enough: the
       * subject is the sprite blit, and an empty canvas and an empty picture resolve to the same
       * background everywhere, so any difference `TheCanvasDoubled` finds is a sprite pixel in the
       * wrong place. Before RS-6 this test drew a scene and leaned on the upscale to put it on both;
       * there is no upscale now, and mirroring the scene by hand would test the mirror.
       */
      Canvas canvas;
      canvas.SetDashboardShown(true);
      Picture picture;

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

    /*
     * The energy bomb reinterprets the same bytes as two-bit pairs, and it reaches THIS surface
     * because the flag it reads is the canvas's (Resolution.md section 3.2).
     *
     * IT CANNOT BE TESTED AGAINST THE CANVAS DOUBLED, and the reason is the bomb itself. Doubling a
     * bit and then reading the result in pairs is not the same as reading in pairs and then
     * doubling: `01` doubled is `0011`, which as pairs is `00` and `11` -- neither of them `01`. The
     * upscale never had this problem because it doubled RESOLVED PIXELS rather than bits, and it is
     * gone. So the property is stated directly: the picture's own bits change colour when the flag
     * is set, and the colours they change to are the two-bit reading of those same bits.
     */
    TEST_METHOD(TheEnergyBombReachesThePictureThroughTheCanvasFlag)
    {
      Canvas canvas;
      Picture picture;

      // %01 %10 %11 %00 read as pairs, or 0 1 0 1 1 0 0 1 read as bits.
      picture.WriteBitmap(0, 0b0110'1100u);
      picture.SetCell(0, 0, Elite::CellPalette{Elite::Colour::White, Elite::Colour::Red});
      canvas.SetCellColour(0, Elite::ColourIndex(Elite::Colour::Cyan));
      canvas.SetSpaceViewBackground(Elite::ColourIndex(Elite::Colour::Green));

      std::vector<std::uint8_t> plain(static_cast<std::size_t>(Picture::WIDTH) * Picture::HEIGHT);
      picture.Resolve(plain, canvas, NO_BACKDROP);

      canvas.SetSpaceViewMulticolour(true);
      std::vector<std::uint8_t> bombed(static_cast<std::size_t>(Picture::WIDTH) * Picture::HEIGHT);
      picture.Resolve(bombed, canvas, NO_BACKDROP);

      Assert::IsTrue(plain != bombed, L"the bomb's flag did not reach the picture at all");

      // The four pairs of `0b01101100`, in order, through the cell and the canvas's own registers.
      const std::array<Elite::Colour, 4> expected{Elite::Colour::White, Elite::Colour::Red, Elite::Colour::Cyan, Elite::Colour::Green};
      for (int pair = 0; pair < 4; ++pair)
      {
        const std::uint8_t want = Elite::ColourIndex(expected[static_cast<std::size_t>(pair)]);
        for (int repeat = 0; repeat < 2; ++repeat)
        {
          const std::size_t at = static_cast<std::size_t>(pair) * 2u + static_cast<std::size_t>(repeat);
          Assert::AreEqual<std::uint32_t>(want, bombed[at], (L"pixel " + std::to_wstring(at) + L" of the bombed cell").c_str());
        }
      }
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
      picture.SetCell(3, 2, Elite::CellPalette{Elite::Colour::White, Elite::Colour::Blue});
      picture.PlotPoint(25, 17); // cell (3, 2), pixel (1, 1) within it

      std::vector<std::uint8_t> out(static_cast<std::size_t>(Picture::WIDTH) * Picture::HEIGHT, std::uint8_t{0});
      picture.Resolve(out, canvas, NO_BACKDROP);

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
      picture.SetDot(100, 300, 5u);
      picture.SetDot(101, 300, 7u);

      std::vector<std::uint8_t> out(static_cast<std::size_t>(Picture::WIDTH) * Picture::HEIGHT, std::uint8_t{0});
      picture.Resolve(out, canvas, NO_BACKDROP);

      const auto at = [&out](int _x, int _y) { return out[static_cast<std::size_t>(_y) * Picture::WIDTH + _x]; };
      Assert::AreEqual<std::uint32_t>(5u, at(100, 300), L"an index written to the plane is the index shown");
      Assert::AreEqual<std::uint32_t>(7u, at(101, 300), L"and its neighbour is its own colour, which two per cell could not be");

      // With the dashboard off, all fifty rows are the space view's region and the plane is unread.
      canvas.SetDashboardShown(false);
      picture.Resolve(out, canvas, NO_BACKDROP);
      Assert::AreNotEqual<std::uint32_t>(5u, at(100, 300), L"a docked screen has no dashboard region to read the plane from");
    }

    /*
     * Erase-by-redraw is exact here, which is the whole reason the upper surface is a plane of bits
     * rather than an array of colours (Resolution.md section 3.2, ADR-002 section 7).
     */
    TEST_METHOD(PlottingAPointTwiceLeavesTheSurfaceAsItWas)
    {
      Picture picture;

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
      picture.SetDot(10, 10, 9u); // above the dashboard region
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

      /*
       * WITH BOTH REGIONS NATIVE THE HASH IS THE SURFACE'S, and the canvas reaches it only through
       * the handful of raster bytes `Resolve` reads from there rather than keeping twice (section
       * 3.2). A bitmap byte no longer moves it -- that is what "native" means -- and the flag that
       * decides which region the lower rows belong to still does.
       */
      canvas.SetDashboardShown(true);
      const std::uint64_t base = picture.Hash(canvas, NO_BACKDROP);
      Assert::AreEqual(base, picture.Hash(canvas, NO_BACKDROP), L"the same surface hashed differently twice");

      canvas.Write(0x1D00u, static_cast<std::uint8_t>(canvas.Read(0x1D00u) ^ 0xFFu));
      Assert::AreEqual(base, picture.Hash(canvas, NO_BACKDROP), L"a canvas bitmap byte still reaches a native picture");

      /*
       * The raster split still reaches the picture, and showing it needs ink in the plane: with the
       * dashboard off, all fifty rows belong to the upper region and the index plane is not read at
       * all. An empty surface hashes the same either way, because black is black.
       */
      picture.SetDot(100, 300, 5u);
      const std::uint64_t withDashboard = picture.Hash(canvas, NO_BACKDROP);
      canvas.SetDashboardShown(false);
      Assert::AreNotEqual(withDashboard, picture.Hash(canvas, NO_BACKDROP), L"the raster split stopped reaching the picture");
      canvas.SetDashboardShown(true);

      /*
       * In the NATIVE region the hash follows the SURFACE -- and the cell has to be painted first,
       * which is not a detail of this test but the behaviour it pins. A cell nobody has coloured is
       * `CellPalette{}`, black over black, so a lit bit and a clear one are both colour 0 and the
       * point is invisible: exactly what `COL2` does on the canvas before `RES2` writes it, where a
       * screen printed without it prints invisibly. This test failed that way when it was written.
       */
      picture.SetCell(1, 1, Elite::CellPalette{Elite::Colour::White, Elite::Colour::Black});
      const std::uint64_t native = picture.Hash(canvas, NO_BACKDROP);
      picture.PlotPoint(11, 13);
      Assert::AreNotEqual(native, picture.Hash(canvas, NO_BACKDROP), L"a plotted point did not move the hash");
    }

    /*
     * THE CANVAS IS NOT A SOURCE OF PIXELS ANY MORE, which is the whole of RS-6 stated as a test.
     *
     * Between RS-0 and RS-4 a region with no twins yet was drawn from the canvas doubled, and Risk
     * R27's tripwire watched for the day that stopped being true. It fired at RS-4; this slice took
     * the scaffolding down. What replaces the tripwire is the opposite assertion: a picture nobody
     * has drawn on resolves BLANK, however busy the canvas beside it is. Before RS-6 this test
     * would have failed on every pixel of the scene.
     *
     * `_canvas` is still a parameter of `Resolve`, and still has to be: the dashboard flag, the
     * energy bomb's mode and background, the colour RAM and the sprite pointers are raster state
     * this surface does not hold. Pixels are the thing it no longer takes.
     */
    TEST_METHOD(AnUndrawnPictureIsBlankHoweverBusyTheCanvasIs)
    {
      Canvas canvas;
      DrawAScene(canvas);

      const Picture picture; // nothing drawn on it at all
      const Pictures both = ResolveBoth(canvas, picture, nullptr);

      bool canvasHasInk = false;
      for (int y = 0; y < Canvas::HEIGHT; ++y)
      {
        for (int x = 0; x < Canvas::WIDTH; ++x)
        {
          if (both.OnCanvas(x, y) != both.OnCanvas(0, 0))
          {
            canvasHasInk = true;
          }
        }
      }
      Assert::IsTrue(canvasHasInk, L"the scene drew nothing, so the test proves nothing");

      const std::uint8_t blank = both.OnPicture(0, 0);
      for (int y = 0; y < Picture::HEIGHT; ++y)
      {
        for (int x = 0; x < Picture::WIDTH; ++x)
        {
          if (both.OnPicture(x, y) != blank)
          {
            Assert::Fail(
              (L"picture (" + std::to_wstring(x) + L", " + std::to_wstring(y) + L") took a pixel from the canvas, which RS-6 removed")
                .c_str());
          }
        }
      }
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

      universe.picture.PlotPoint(3, 4);
      universe.picture.SetDot(5, 300, 9u);
      universe.picture.SetCell(1, 1, Elite::CellPalette{Elite::Colour::White, Elite::Colour::Red});

      Assert::AreEqual(base, Elite::HashState(universe), L"the picture moved the state hash, which would re-record every replay");

      // And the BACKDROP beside it, which RN-0 added under exactly the same exclusion: it is the
      // other half of one rendering, not a second thing the game does.
      universe.backdrop.PlotPoint(9, 10);
      universe.backdrop.SetDot(11, 320, 4u);
      universe.backdrop.SetCell(2, 2, Elite::CellPalette{Elite::Colour::Cyan, Elite::Colour::Blue});

      Assert::AreEqual(base, Elite::HashState(universe), L"the backdrop moved the state hash, which would re-record every replay");

      // And the canvas beside it still does, so the exclusion is narrow rather than a hole.
      universe.canvas.Write(0x1234u, 0x5Au);
      Assert::AreNotEqual(base, Elite::HashState(universe), L"a canvas byte stopped moving the state hash");
    }

    /*
     * Every mutator steps `Generation`, and nothing that hashes can see it (T-3).
     *
     * The presenter skips the 256,000-pixel resolve when this number has not moved, so a mutator
     * that forgot to step it is a frame the player never sees -- and the failure would be
     * intermittent, because whether it shows depends on what else wrote that frame. It is checked
     * here rather than trusted, one mutator at a time, so that a new one added without the
     * increment fails on the line that names it.
     *
     * The other half is that it must remain invisible to everything that compares pictures: it is
     * on the field `HashState` and `StateCells` skip whole, and `Picture::Hash` folds resolved
     * PIXELS, so neither can see it. A counter that moved a digest would re-record five tables.
     */
    TEST_METHOD(EveryWriteStepsTheGenerationAndNothingHashesIt)
    {
      Elite::Canvas canvas;
      Elite::Picture picture;

      std::uint32_t last = picture.Generation();
      const auto moved = [&](const wchar_t* _what)
      {
        const std::uint32_t now = picture.Generation();
        Assert::IsTrue(now != last, (std::wstring(_what) + L" did not step the picture's generation").c_str());
        last = now;
      };

      picture.WriteBitmap(0u, 0xFFu);
      moved(L"WriteBitmap");
      picture.ExclusiveOrBitmap(0u, 0x0Fu);
      moved(L"ExclusiveOrBitmap");
      picture.PlotPoint(11, 12);
      moved(L"PlotPoint");
      picture.SetCell(2, 3, Elite::CellPalette{Elite::Colour::White, Elite::Colour::Red});
      moved(L"SetCell");
      picture.SetDot(7, 300, 9u);
      moved(L"SetDot");
      picture.ExclusiveOrDot(7, 300, 3u);
      moved(L"ExclusiveOrDot");
      picture.Clear();
      moved(L"Clear");

      /*
       * A write that lands twice with the same value steps it twice, and that is the intended
       * bias: a redundant resolve costs a frame's work, a missed one costs the frame.
       */
      picture.WriteBitmap(4u, 0x11u);
      const std::uint32_t once = picture.Generation();
      picture.WriteBitmap(4u, 0x11u);
      Assert::IsTrue(picture.Generation() != once, L"an unchanged value stopped stepping the generation");

      // Out of range writes nothing, so it must step nothing either.
      const std::uint32_t before = picture.Generation();
      picture.WriteBitmap(Elite::Picture::BITMAP_SIZE + 5u, 0x77u);
      picture.SetCell(-1, -1, Elite::CellPalette{});
      picture.SetDot(-1, 0, 1u);
      Assert::AreEqual(before, picture.Generation(), L"a write that landed nowhere stepped the generation");

      // And no digest moves with it.
      const std::uint64_t pixels = picture.Hash(canvas, NO_BACKDROP);
      picture.WriteBitmap(9u, picture.ReadBitmap(9u)); // same value: generation moves, pixels do not
      Assert::AreEqual(pixels, picture.Hash(canvas, NO_BACKDROP), L"the generation reached Picture::Hash");
    }

    /*
     * `ResolveSignature` sees everything `Resolve` reads -- the property the presenter rests on.
     *
     * `ScreenPresenter` resolves only when this number moves, so a read `Resolve` makes and the
     * signature does not is a screen that stops updating: silent, total, and on a path neither CI
     * leg can see, because the Ubuntu leg does not compile the presenter and the Windows leg has no
     * display. This is where that is caught instead.
     *
     * EACH CASE ASSERTS BOTH HALVES, and the first half is the one that took two attempts to get
     * right: the change must actually move PIXELS. A first draft ran against a default picture --
     * every cell palette black on black, the colour RAM zero -- where flipping bitmap bits changed
     * nothing a person could see, so every case passed while proving nothing. `Scene` below is
     * therefore not decoration: four distinct colours, a bit pattern holding all four multicolour
     * pairs, and one sprite switched on over the space view, so that each of the twelve reads has
     * something to change.
     *
     * The converse is deliberately NOT asserted: the signature may move when the pixels would not
     * (`Generation` steps on a write of the byte that was already there). A redundant resolve costs
     * a frame's work; a missed one costs the frame.
     */
    TEST_METHOD(TheResolveSignatureSeesEverythingTheResolveReads)
    {
      Elite::Canvas canvas;
      Elite::Picture picture;
      Elite::VideoState video;

      std::vector<std::uint8_t> before(static_cast<std::size_t>(Picture::WIDTH) * Picture::HEIGHT, 0u);
      std::vector<std::uint8_t> after(before.size(), 0u);

      const auto scene = [&]
      {
        canvas = Elite::Canvas{};
        picture = Elite::Picture{};
        video = Elite::VideoState{};

        // Four colours that are all different, so that every multicolour pair resolves to its own
        // index: %00 the background, %01 the cell's high, %10 its low, %11 the colour RAM.
        canvas.SetSpaceViewMulticolour(true);
        canvas.SetSpaceViewBackground(0x06u); // blue
        canvas.SetBackground(0x00u);
        for (int cell = 0; cell < Elite::Canvas::CELL_COLUMNS * Elite::Canvas::CELL_ROWS; ++cell)
        {
          canvas.SetCellColour(cell, 0x02u); // red
        }
        for (int row = 0; row < Picture::CELL_ROWS; ++row)
        {
          for (int column = 0; column < Picture::CELL_COLUMNS; ++column)
          {
            picture.SetCell(column, row, Elite::CellPalette{Elite::Colour::White, Elite::Colour::Yellow});
          }
        }

        // %00 %01 %10 %11 across every byte of the space view, so all four pairs are present in
        // every cell and each of the four colours above is on the screen somewhere.
        for (std::size_t offset = 0; offset < Picture::BITMAP_SIZE; ++offset)
        {
          picture.WriteBitmap(offset, 0x1Bu);
        }
        /*
         * And the dashboard's own index plane, SHOWN -- `Resolve` reads the lower rows from the
         * bitmap while `DFLAG` is clear, so a dashboard dot moves no pixel until this is set. That
         * is what the first version of this test got wrong and what the case below now proves.
         */
        canvas.SetDashboardShown(true);
        for (int y = Picture::SPACE_VIEW_HEIGHT; y < Picture::HEIGHT; ++y)
        {
          for (int x = 0; x < Picture::WIDTH; ++x)
          {
            picture.SetDot(x, y, static_cast<std::uint8_t>((x + y) & 0x0Fu));
          }
        }

        // One laser sight, on, over the middle of the space view.
        video.enabled = 0x01u;
        video.x[0] = static_cast<std::uint16_t>(Elite::SPRITE_ORIGIN_X + 100);
        video.y[0] = static_cast<std::uint8_t>(Elite::SPRITE_ORIGIN_Y + 60);
        video.colour[0] = Elite::Colour::White;
        canvas.Write(Elite::Canvas::SPRITE_POINTERS, Elite::SPRITE_POINTER_BASE);
      };

      const auto check = [&](const wchar_t* _what, auto&& _change)
      {
        scene();
        picture.Resolve(before, canvas, NO_BACKDROP, video);
        const std::uint64_t signature = picture.ResolveSignature(canvas, NO_BACKDROP, &video);

        _change();

        picture.Resolve(after, canvas, NO_BACKDROP, video);

        Assert::IsTrue(after != before, (std::wstring(_what) + L" moved no pixel, so this case proves nothing").c_str());
        Assert::IsTrue(
          picture.ResolveSignature(canvas, NO_BACKDROP, &video) != signature,
          (std::wstring(_what) + L" changed the picture and not the signature, so the presenter would never resolve it").c_str());
      };

      check(L"a bitmap write", [&] { picture.ExclusiveOrBitmap(Picture::BitmapOffset(80, 40), 0xFFu); });
      check(L"a cell palette", [&] { picture.SetCell(10, 5, Elite::CellPalette{Elite::Colour::Red, Elite::Colour::Green}); });
      check(L"a dashboard dot", [&] { picture.SetDot(4, Picture::SPACE_VIEW_HEIGHT + 1, 0x0Du); });
      check(L"the colour RAM", [&] { canvas.SetCellColour(0, 0x0Bu); });
      check(L"the space view background", [&] { canvas.SetSpaceViewBackground(0x0Au); });
      check(L"the space view multicolour bit", [&] { canvas.SetSpaceViewMulticolour(false); });
      check(L"the dashboard-shown flag", [&] { canvas.SetDashboardShown(false); });
      check(L"a sprite pointer",
            [&] { canvas.Write(Elite::Canvas::SPRITE_POINTERS, static_cast<std::uint8_t>(Elite::SPRITE_POINTER_BASE + 4u)); });
      check(L"a sprite's colour", [&] { video.colour[0] = Elite::Colour::Red; });
      check(L"a sprite's column", [&] { video.x[0] = static_cast<std::uint16_t>(video.x[0] + 8u); });
      check(L"a sprite's row", [&] { video.y[0] = static_cast<std::uint8_t>(video.y[0] + 8u); });
      check(L"the sprite expansion", [&] { video.expanded = 0x01u; });
      check(L"the sprites enabled", [&] { video.enabled = 0x00u; });

      /*
       * And "no registers at all" is its own answer. The title and every docked screen present
       * through the overload that takes none, and the two are different pictures.
       */
      scene();
      picture.Resolve(before, canvas, NO_BACKDROP, video);
      picture.Resolve(after, canvas, NO_BACKDROP);
      Assert::IsTrue(after != before, L"a sprite that is switched on resolved the same with and without the registers");
      Assert::IsTrue(picture.ResolveSignature(canvas, NO_BACKDROP, &video) != picture.ResolveSignature(canvas, NO_BACKDROP, nullptr),
                     L"a picture with sprites signs the same as one without");
    }
  };

} // namespace GameLogicTests
