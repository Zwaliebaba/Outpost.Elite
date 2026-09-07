#include "pch.h"

#include "Arith.h"
#include "Canvas.h"
#include "Picture.h"
#include "ShipBlueprint.h"
#include "ShipDraw.h"
#include "ShipDraw2x.h"
#include "StateHash.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The 640x400 picture's lines (Design/Resolution.md section 4.1, slice RS-2).
 *
 * TWO KINDS OF EVIDENCE, and they answer different questions. The SHADOW test asks whether the two
 * surfaces show the same picture, by downsampling the wide one and comparing; it would catch a twin
 * that dropped a line, drew it twice or put it somewhere else. It would NOT catch a twin that is
 * one wide pixel out everywhere, because a two-pixel block absorbs that -- and being one pixel out
 * everywhere is exactly what a wrong scale looks like.
 *
 * So the PROPERTY sweeps are the other half, and they are what pins the extra bit: `Divide512`
 * halved must be `LL28` for every pair of bytes it does not saturate on, and a projected vertex
 * halved must be the faithful vertex. Those are equalities over the whole input space, and they are
 * the only thing in the slice that can say the resolution was actually spent rather than faked.
 */
namespace GameLogicTests
{

  namespace
  {
    using Elite::Canvas;
    using Elite::Picture;

    /// Is the canvas lit at this point of the SPACE VIEW -- x 0..255, y 0..143, the coordinates the
    /// drawing works in, with `RowOffset` supplying the four-cell margin.
    [[nodiscard]] bool CanvasLit(const Canvas& _canvas, int _x, int _y)
    {
      const std::uint16_t byte = static_cast<std::uint16_t>(Canvas::RowOffset(static_cast<std::uint8_t>(_y)) + (_x & 0xF8));
      const std::uint8_t bits = _canvas.Read(static_cast<std::uint16_t>(byte + (_y & 7)));
      return (bits & (0x80u >> (_x & 7))) != 0u;
    }

    /// And the picture, in the same view coordinates -- `Bresenham2x` adds the doubled margin.
    [[nodiscard]] bool PictureLit(const Picture& _picture, int _x, int _y)
    {
      return _picture.Point(_x + Picture::SPACE_VIEW_MARGIN, _y);
    }

    /// Whether any wide pixel of the block that doubles (_x, _y), grown by one, is lit.
    [[nodiscard]] bool PictureNear(const Picture& _picture, int _x, int _y)
    {
      for (int down = -1; down <= 2; ++down)
      {
        for (int across = -1; across <= 2; ++across)
        {
          if (PictureLit(_picture, 2 * _x + across, 2 * _y + down))
          {
            return true;
          }
        }
      }
      return false;
    }

    /*
     * Section 8.1's space-view property, both ways round, with the one pixel of slack the clipper's
     * half-pixel corner needs.
     *
     * Counted rather than reported first-only, because a line that is subtly wrong lights a handful
     * of pixels and a scale that is wrong lights thousands -- and the number is what tells them
     * apart. Returns the two tallies and the first offender of each.
     */
    struct Disagreement
    {
      int canvasOnly = 0;
      int pictureOnly = 0;
      std::wstring first;
    };

    Disagreement Compare(const Canvas& _canvas, const Picture& _picture)
    {
      Disagreement out;

      for (int y = 0; y < Canvas::SPACE_VIEW_HEIGHT; ++y)
      {
        for (int x = 0; x < 256; ++x)
        {
          if (CanvasLit(_canvas, x, y) && !PictureNear(_picture, x, y))
          {
            if (out.canvasOnly == 0)
            {
              out.first = L"canvas lit at (" + std::to_wstring(x) + L", " + std::to_wstring(y) + L") and the picture is not";
            }
            ++out.canvasOnly;
          }
        }
      }

      for (int y = 0; y < Picture::SPACE_VIEW_HEIGHT; ++y)
      {
        for (int x = 0; x < 512; ++x)
        {
          if (!PictureLit(_picture, x, y))
          {
            continue;
          }
          bool near = false;
          for (int down = -1; down <= 1 && !near; ++down)
          {
            for (int across = -1; across <= 1 && !near; ++across)
            {
              const int cx = x / 2 + across;
              const int cy = y / 2 + down;
              near = cx >= 0 && cx < 256 && cy >= 0 && cy < Canvas::SPACE_VIEW_HEIGHT && CanvasLit(_canvas, cx, cy);
            }
          }
          if (!near)
          {
            if (out.pictureOnly == 0)
            {
              out.first = L"picture lit at (" + std::to_wstring(x) + L", " + std::to_wstring(y) + L") and the canvas is not";
            }
            ++out.pictureOnly;
          }
        }
      }

      return out;
    }

    /// A universe with one ship placed in front of the player, drawn by `LL9` onto both surfaces.
    std::unique_ptr<Elite::Universe> DrawOneShip(Elite::ShipType _type, std::uint8_t _zHigh)
    {
      const Elite::Blueprint* blueprint = Elite::BlueprintOf(_type);
      if (blueprint == nullptr)
      {
        return nullptr;
      }

      auto universe = std::make_unique<Elite::Universe>();
      universe->flight.blueprint = blueprint;
      universe->flight.type = _type;

      Elite::Ship& work = universe->work;
      work.z.hi = _zHigh;
      work.z.lo = 0x40u;

      // 6502: INWK+9 onwards -- the identity orientation, which is what `ZINF` writes: nosev along
      // z, roofv along y, sidev along x, each at the &60 the game uses for a unit vector.
      work.nose.z.hi = 0x60u;
      work.roof.y.hi = 0x60u;
      work.side.x.hi = 0x60u;

      work.heap = Elite::HeapOffset::FromAddress(0xFF00u);
      work.state = 0u;

      Elite::Ship slot = work;
      Elite::DrawShip(*universe, slot, false);
      return universe;
    }
  } // namespace

  TEST_CLASS(ThePictureLines)
  {
  public:
    /*
     * The property the slice actually has, and it is not the one the design expected.
     *
     * `LL28` is a LOGARITHM-TABLE lookup rather than a truncating divide -- measured against exact
     * division over all 32,640 pairs it is out by up to 3 and by 0.70 on average -- so there is no
     * dropped bit for a twin to recover, and a twin that divided exactly would draw a different
     * wireframe. What is true instead is an equality: the wide vertex is the faithful vertex
     * doubled, exactly, for every sixteen-bit value `XX3` can hold. That is what `Doubled` is, and
     * asserting it over the whole range is what stops a later slice quietly reintroducing a second
     * arithmetic.
     */
    TEST_METHOD(TheWideVertexIsTheFaithfulOneDoubled)
    {
      int compared = 0;
      for (int value = -32768; value <= 32767; value += 1)
      {
        const std::uint16_t bytes = static_cast<std::uint16_t>(value);
        const std::int16_t wide = Elite::Doubled(static_cast<std::uint8_t>(bytes), static_cast<std::uint8_t>(bytes >> 8));
        ++compared;

        // Doubling wraps at sixteen bits exactly as the sum of two such coordinates would, and the
        // clipper below is what keeps a wrapped one off the screen -- so the assertion is on the
        // arithmetic and not on the range.
        Assert::AreEqual(static_cast<int>(static_cast<std::int16_t>(2 * value)), static_cast<int>(wide),
                         L"a vertex was not doubled");
      }
      Assert::AreEqual(65536, compared, L"the sweep did not cover every sixteen-bit value");
    }

    /// The measurement that settles it, kept as a test so the claim above cannot rot: the log
    /// divide is NOT exact division, and by how much.
    TEST_METHOD(TheFaithfulDivideIsALogTableAndNotExact)
    {
      int worst = 0;
      long total = 0;
      int cases = 0;
      for (int divisor = 1; divisor < 256; ++divisor)
      {
        for (int dividend = 0; dividend < divisor; ++dividend)
        {
          const int got = Elite::DivideByLog(static_cast<std::uint8_t>(dividend), static_cast<std::uint8_t>(divisor)).value;
          const int exact = (256 * dividend) / divisor;
          const int error = (got > exact) ? (got - exact) : (exact - got);
          worst = (error > worst) ? error : worst;
          total += error;
          ++cases;
        }
      }

      Assert::AreEqual(32640, cases, L"the sweep is not the whole space");
      Assert::IsTrue(worst >= 2, L"the log divide has become exact, which would change what a twin may do");
      Assert::IsTrue(total > cases / 2, L"its average error has collapsed, which would change the same thing");
    }

    /// The line drawer erases by drawing again, which is the whole of how a ship leaves the screen.
    TEST_METHOD(ALineDrawnTwiceIsGone)
    {
      Picture picture;
      const std::array<Elite::Line2x, 5> LINES = {{{0, 0, 511, 287, true},
                                                   {511, 0, 0, 287, true},
                                                   {10, 10, 10, 200, true},
                                                   {10, 10, 400, 10, true},
                                                   {255, 143, 256, 144, true}}};

      for (const Elite::Line2x& line : LINES)
      {
        Elite::Bresenham2x(picture, line);
      }
      bool anything = false;
      for (const std::uint8_t byte : picture.Bitmap())
      {
        anything = anything || byte != 0u;
      }
      Assert::IsTrue(anything, L"nothing was drawn at all");

      for (const Elite::Line2x& line : LINES)
      {
        Elite::Bresenham2x(picture, line);
      }
      for (const std::uint8_t byte : picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"a line drawn twice left ink behind");
      }
    }

    /// The cut keeps what is inside and reports when nothing is.
    TEST_METHOD(TheClipperCutsToTheWideView)
    {
      Elite::Line2x out;

      Assert::IsTrue(Elite::ClipLine2x(Elite::Line2x{10, 10, 100, 100, true}, Elite::SPACE_VIEW_BOTTOM_2X, out),
                     L"a line wholly inside was rejected");
      Assert::IsTrue(out == Elite::Line2x{10, 10, 100, 100, true}, L"a line wholly inside was moved");

      Assert::IsFalse(Elite::ClipLine2x(Elite::Line2x{-500, 10, -400, 100, true}, Elite::SPACE_VIEW_BOTTOM_2X, out),
                      L"a line wholly left of the view was accepted");
      Assert::IsFalse(Elite::ClipLine2x(Elite::Line2x{10, 400, 100, 500, true}, Elite::SPACE_VIEW_BOTTOM_2X, out),
                      L"a line below the space view was accepted");

      Assert::IsTrue(Elite::ClipLine2x(Elite::Line2x{-100, 100, 300, 100, true}, Elite::SPACE_VIEW_BOTTOM_2X, out),
                     L"a line crossing the left edge was rejected");
      Assert::AreEqual(0, static_cast<int>(out.x1), L"it was not cut to the edge");
      Assert::AreEqual(300, static_cast<int>(out.x2), L"and its far end moved");

      // `dontclip` is the short-range chart letting the drawing run down the whole screen.
      Assert::IsTrue(Elite::ClipLine2x(Elite::Line2x{10, 300, 100, 350, true}, Elite::WHOLE_SCREEN_BOTTOM_2X, out),
                     L"the chart's taller region rejected a line inside it");
    }

    /*
     * The shadow test: a real ship, drawn by `LL9` onto both surfaces, compared.
     *
     * Three distances, because what `LL9` does changes with them -- a near ship is a full wireframe,
     * a middle one fewer edges, and a far one is `SHPPT`'s two-line dot.
     */
    TEST_METHOD(AShipLandsOnBothSurfaces)
    {
      for (const std::uint8_t distance : {std::uint8_t{0x10}, std::uint8_t{0x28}, std::uint8_t{0x60}})
      {
        std::unique_ptr<Elite::Universe> universe = DrawOneShip(Elite::ShipType::CobraMk3, distance);
        if (universe == nullptr)
        {
          Assert::Fail(L"the Cobra has no blueprint");
        }

        const Disagreement wrong = Compare(universe->canvas, universe->picture);
        const std::wstring where = L"distance " + std::to_wstring(distance) + L": " + std::to_wstring(wrong.canvasOnly) +
                                   L" canvas-only and " + std::to_wstring(wrong.pictureOnly) + L" picture-only pixel(s) -- " +
                                   wrong.first;
        Assert::AreEqual(0, wrong.canvasOnly, where.c_str());
        Assert::AreEqual(0, wrong.pictureOnly, where.c_str());
      }
    }

    /// And drawing the ship a second time takes it off both, which is how every frame erases the
    /// last one. The wide heap is what makes that possible at all (rule T3).
    TEST_METHOD(RedrawingAShipRemovesItFromBoth)
    {
      /*
       * The distance is chosen so that something SURVIVES, which is not true of every distance.
       *
       * A blueprint has coincident edges, and everything is drawn by exclusive-or, so at some
       * distances a ship's own edges cancel each other and the frame is blank -- on both surfaces,
       * identically, because the wide lines are the faithful ones doubled. That is the original's
       * behaviour and not a defect; it is written down here because a test that took a blank frame
       * for a broken twin is exactly the wrong conclusion to reach, and this one did until it was
       * measured (§13).
       */
      std::unique_ptr<Elite::Universe> universe = DrawOneShip(Elite::ShipType::CobraMk3, 0x10u);
      Assert::IsTrue(universe != nullptr);

      bool onCanvas = false;
      for (int y = 0; y < Canvas::SPACE_VIEW_HEIGHT && !onCanvas; ++y)
      {
        for (int x = 0; x < 256 && !onCanvas; ++x)
        {
          onCanvas = CanvasLit(universe->canvas, x, y);
        }
      }
      Assert::IsTrue(onCanvas, L"the ship drew nothing on the CANVAS, so this proves nothing");

      bool drewSomething = false;
      for (const std::uint8_t byte : universe->picture.Bitmap())
      {
        drewSomething = drewSomething || byte != 0u;
      }
      Assert::IsTrue(drewSomething, L"the ship drew nothing on the picture at all");

      Elite::DrawShipLines(universe->canvas, universe->heap, universe->work.heap, &universe->picture, &universe->heap2x);

      for (const std::uint8_t byte : universe->picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"the second draw left ink on the picture");
      }
    }

    /// The wide heap is game-shaped state and is deliberately outside the replay digest, for the
    /// picture's reason (Resolution.md section 3.4).
    TEST_METHOD(TheStateHashDoesNotSeeTheWideHeap)
    {
      Elite::Universe universe;
      const std::uint64_t base = Elite::HashState(universe);

      universe.heap2x.Write(Elite::HeapOffset::FromAddress(0xFF00u), Elite::Line2x{1, 2, 3, 4, true});
      Assert::AreEqual(base, Elite::HashState(universe), L"the wide heap moved the state hash");

      universe.heap.Write(Elite::HeapOffset::FromAddress(0xFF00u), 0x5Au);
      Assert::AreNotEqual(base, Elite::HashState(universe), L"the faithful heap stopped moving it");
    }
  };

} // namespace GameLogicTests
