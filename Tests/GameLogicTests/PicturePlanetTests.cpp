#include "pch.h"

#include "Arith.h"
#include "Canvas.h"
#include "Explosion.h"
#include "Lasers.h"
#include "Lines2x.h"
#include "LookupTables.h"
#include "Picture.h"
#include "PlanetDraw.h"
#include "ShipDraw2x.h"
#include "Stardust.h"
#include "Universe.h"
#include "ViewChange.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The 640x400 picture's planet, sun, dust, particles, beams and frame (Resolution.md sections 4.2,
 * 4.3 and 5.3, slice RS-3).
 *
 * THE SLICE'S EVIDENCE IS IN THREE PARTS AND THEY ANSWER DIFFERENT QUESTIONS.
 *
 * The MEASUREMENTS are first, because RS-2 established that this track's design premises have to be
 * measured before they are built on -- and two of RS-3's were false. `LL5` is exact, which is what
 * lets the sun be reasoned about at all; `FMLTU2` is not, which is why the planet's circle is the
 * faithful circle doubled rather than one recomputed at twice the scale. Both are kept as tests so
 * the claims in the headers cannot rot.
 *
 * The PROPERTIES are second: equalities over whole input spaces. The stardust's half-pixel is the
 * one place in the whole track where a twin recovers real precision, and a sweep is the only thing
 * that can say it recovered the RIGHT bit rather than a plausible one. `WriteBitmapByte2x` doubles
 * a canvas byte, and a sweep says so for all 256 of them.
 *
 * The SHADOW tests are last: a real scene drawn by the faithful routines onto both surfaces, then
 * compared. They would catch a twin that dropped a shape, drew it twice or put it somewhere else;
 * they would not catch one that is half a pixel out everywhere, which is what the properties are
 * for.
 */
namespace GameLogicTests
{

  namespace
  {
    using Elite::Canvas;
    using Elite::Picture;

    /// Is the canvas lit at this point of the SPACE VIEW -- x 0..255, y 0..199, the coordinates the
    /// drawing works in, with `RowOffset` supplying the four-cell margin.
    [[nodiscard]] bool CanvasLit(const Canvas& _canvas, int _x, int _y)
    {
      const std::uint16_t byte = static_cast<std::uint16_t>(Canvas::RowOffset(static_cast<std::uint8_t>(_y)) + (_x & 0xF8));
      const std::uint8_t bits = _canvas.Read(static_cast<std::uint16_t>(byte + (_y & 7)));
      return (bits & (0x80u >> (_x & 7))) != 0u;
    }

    /// And the picture, in the same view coordinates -- the twins add the doubled margin at the plot.
    [[nodiscard]] bool PictureLit(const Picture& _picture, int _x, int _y)
    {
      return _picture.Point(_x + Picture::SPACE_VIEW_MARGIN, _y);
    }

    /*
     * Section 8.1's property, both ways round, over a chosen number of canvas rows.
     *
     * The slack is one wide pixel in each direction, which is what a twin drawing the same shape
     * half a pixel finer needs: a mark two hi-res pixels wide sits inside the four its faithful
     * counterpart covers, and a thin line runs along one edge of a thick one.
     *
     * Counted rather than reported first-only, because a shape that is subtly wrong lights a
     * handful of pixels and a scale that is wrong lights thousands -- and the number is what tells
     * them apart.
     */
    struct Disagreement
    {
      int canvasOnly = 0;
      int pictureOnly = 0;
      std::wstring first;
    };

    Disagreement Compare(const Canvas& _canvas, const Picture& _picture, int _rows)
    {
      Disagreement out;

      for (int y = 0; y < _rows; ++y)
      {
        for (int x = 0; x < 256; ++x)
        {
          if (!CanvasLit(_canvas, x, y))
          {
            continue;
          }
          bool alongside = false;
          for (int down = -1; down <= 2 && !alongside; ++down)
          {
            for (int across = -1; across <= 2 && !alongside; ++across)
            {
              alongside = PictureLit(_picture, 2 * x + across, 2 * y + down);
            }
          }
          if (!alongside)
          {
            if (out.canvasOnly == 0)
            {
              out.first = L"canvas lit at (" + std::to_wstring(x) + L", " + std::to_wstring(y) + L") and the picture is not";
            }
            ++out.canvasOnly;
          }
        }
      }

      for (int y = 0; y < 2 * _rows; ++y)
      {
        for (int x = 0; x < 512; ++x)
        {
          if (!PictureLit(_picture, x, y))
          {
            continue;
          }
          bool alongside = false;
          for (int down = -1; down <= 1 && !alongside; ++down)
          {
            for (int across = -1; across <= 1 && !alongside; ++across)
            {
              const int cx = x / 2 + across;
              const int cy = y / 2 + down;
              alongside = cx >= 0 && cx < 256 && cy >= 0 && cy < _rows && CanvasLit(_canvas, cx, cy);
            }
          }
          if (!alongside)
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

    void AssertAgrees(const Elite::Universe& _universe, int _rows, const std::wstring& _what)
    {
      const Disagreement wrong = Compare(_universe.canvas, _universe.picture, _rows);
      const std::wstring where = _what + L": " + std::to_wstring(wrong.canvasOnly) + L" canvas-only and " +
                                 std::to_wstring(wrong.pictureOnly) + L" picture-only pixel(s) -- " + wrong.first;
      Assert::AreEqual(0, wrong.canvasOnly, where.c_str());
      Assert::AreEqual(0, wrong.pictureOnly, where.c_str());
    }

    /// Anything at all on the picture, which every shadow test needs before it means something: two
    /// blank surfaces agree perfectly.
    [[nodiscard]] bool AnythingDrawn(const Picture& _picture)
    {
      for (const std::uint8_t byte : _picture.Bitmap())
      {
        if (byte != 0u)
        {
          return true;
        }
      }
      return false;
    }
  } // namespace

  TEST_CLASS(ThePicturePlanetAndDust)
  {
  public:
    /*
     * The measurement that decided the SUN, and it is the one premise of RS-3's design that held.
     *
     * `LL5` is a shift-and-subtract binary square root over eight rounds, and over every one of the
     * 65,536 radicands it can be given it equals the exact integer square root. So a twin may
     * legitimately extend it: `isqrt` at thirty-two bits is the SAME FUNCTION on a bigger domain
     * rather than a second opinion, which is what rule T2 requires and what `FMLTU2` below cannot
     * offer.
     *
     * The sun does not use that licence in the end, and the reason is measured in the test after
     * this one. It matters anyway: it is why the question could be asked at all.
     */
    TEST_METHOD(TheFaithfulSquareRootIsExact)
    {
      int compared = 0;
      for (int value = 0; value < 65536; ++value)
      {
        const int got = Elite::SquareRoot(static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)).value;

        // The exact integer square root, by search rather than by `std::sqrt` -- GameLogic bans
        // floating point (ADR-002) and a test that reached for it to check an integer routine
        // would be borrowing the very thing the port refuses.
        int root = 0;
        while ((root + 1) * (root + 1) <= value)
        {
          ++root;
        }

        Assert::AreEqual(root, got, L"LL5 disagreed with the exact integer square root");
        ++compared;
      }
      Assert::AreEqual(65536, compared, L"the sweep did not cover every radicand");
    }

    /*
     * And the measurement that decided the PLANET, which is RS-2's finding again in a second place.
     *
     * `CIRCLE2` builds each segment endpoint with `FMLTU2`, a LOGARITHM-TABLE multiply, so
     * `radius * SNE[step] / 256` is not an exact product and a twin computing one at twice the
     * scale would draw a DIFFERENT circle -- one that no longer sits concentric with the crater and
     * the meridians, which come through the same table. The design proposed exactly that (section
     * 4.2); the numbers below are why the built slice doubles the faithful endpoints instead.
     */
    TEST_METHOD(TheFaithfulSineMultiplyIsALogTableAndNotExact)
    {
      int worst = 0;
      int overOne = 0;
      int cases = 0;

      for (int radius = 0; radius < 256; ++radius)
      {
        for (int angle = 0; angle < 64; ++angle)
        {
          const int got = Elite::MultiplyBySine(static_cast<std::uint8_t>(radius), static_cast<std::uint8_t>(angle), false).value;
          const int exact = radius * Elite::SINE_TABLE[static_cast<std::size_t>(angle) & 31u] / 256;
          const int error = (got > exact) ? (got - exact) : (exact - got);

          worst = (error > worst) ? error : worst;
          overOne += (error > 1) ? 1 : 0;
          ++cases;
        }
      }

      Assert::AreEqual(16384, cases, L"the sweep is not the whole space");
      Assert::IsTrue(worst >= 2, L"the log multiply has become exact, which would change what a twin may do");
      Assert::IsTrue(overOne > cases / 100, L"its error has collapsed, which would change the same thing");
    }

    /*
     * The stardust's half-pixel: the ONE place in the track where a twin recovers real precision,
     * and the sweep that says it recovered the right bit.
     *
     * The property is an identity rather than a resemblance. `PIXEL2` turns a sign-magnitude byte
     * into a screen coordinate; the wide mark must sit at twice that coordinate, moved by one wide
     * pixel in the direction the sign names when the fraction's top bit is set, and not moved when
     * it is clear. Swept over every (across, down) pair with both fraction bits both ways, which is
     * the whole input space the movers can produce.
     */
    TEST_METHOD(TheWideSpeckIsTheFaithfulOneDoubledPlusTheFractionBit)
    {
      int compared = 0;

      for (int across = 0; across < 256; ++across)
      {
        for (int down = 0; down < 256; ++down)
        {
          const Elite::SpaceViewPoint point =
            Elite::ToSpaceViewPoint(static_cast<std::uint8_t>(across), static_cast<std::uint8_t>(down));

          for (const std::uint8_t acrossLow : {std::uint8_t{0x00}, std::uint8_t{0x80}})
          {
            for (const std::uint8_t downLow : {std::uint8_t{0x00}, std::uint8_t{0x80}})
            {
              Picture picture;
              Elite::PlotRelativePixel2x(picture, static_cast<std::uint8_t>(across), static_cast<std::uint8_t>(down), acrossLow,
                                         downLow, 200u);
              ++compared;

              if (point.offScreen)
              {
                Assert::IsFalse(AnythingDrawn(picture), L"a speck the faithful plot refused was drawn wide");
                continue;
              }

              // Where the mark must be: two hi-res pixels, starting here.
              const int wantX = 2 * static_cast<int>(point.x) + (((across & 0x80) != 0) ? -(acrossLow >> 7) : (acrossLow >> 7));
              const int wantY = 2 * static_cast<int>(point.y) + (((down & 0x80) != 0) ? (downLow >> 7) : -(downLow >> 7));

              Assert::IsTrue(picture.Point(wantX + Picture::SPACE_VIEW_MARGIN, wantY), L"the wide speck is not where the bit puts it");
              Assert::IsTrue(picture.Point(wantX + 1 + Picture::SPACE_VIEW_MARGIN, wantY), L"the wide speck is not two pixels wide");

              // And nowhere else: exactly two bits are set on the whole surface.
              int lit = 0;
              for (const std::uint8_t byte : picture.Bitmap())
              {
                for (int bit = 0; bit < 8; ++bit)
                {
                  lit += ((byte >> bit) & 1u);
                }
              }
              Assert::AreEqual(2, lit, L"a far speck lit something other than its two pixels");
            }
          }
        }
      }

      Assert::AreEqual(4 * 65536, compared, L"the sweep did not cover the whole input space");
    }

    /*
     * The frame is drawn from canvas ADDRESSES rather than coordinates, so its twin doubles the
     * BYTE -- and this is the sweep that says the doubling is the byte's own bits and nothing else.
     *
     * Every one of the 256 values, stored and then exclusive-ored, over a byte in the middle of the
     * bitmap: sixteen wide pixels, two rows of eight doubled bits, and a second exclusive-or that
     * takes them all off again.
     */
    TEST_METHOD(AWideBitmapByteIsTheCanvasByteDoubled)
    {
      // Character row 5, cell 12, pixel row 3 -- an ordinary byte with nothing special about it.
      constexpr std::uint16_t OFFSET = 5u * Canvas::WIDTH + 12u * 8u + 3u;
      constexpr int X = 12 * 8;
      constexpr int Y = 5 * 8 + 3;

      for (int value = 0; value < 256; ++value)
      {
        Picture picture;
        Elite::WriteBitmapByte2x(picture, OFFSET, static_cast<std::uint8_t>(value), false);

        for (int bit = 0; bit < 8; ++bit)
        {
          const bool want = ((value >> (7 - bit)) & 1) != 0;
          for (int down = 0; down < 2; ++down)
          {
            for (int across = 0; across < 2; ++across)
            {
              Assert::AreEqual(want, picture.Point(2 * (X + bit) + across, 2 * Y + down), L"a stored byte did not double");
            }
          }
        }

        // And the exclusive-or path, which is `BOXS2`'s: the same byte twice leaves nothing.
        Picture toggled;
        Elite::WriteBitmapByte2x(toggled, OFFSET, static_cast<std::uint8_t>(value), true);
        Elite::WriteBitmapByte2x(toggled, OFFSET, static_cast<std::uint8_t>(value), true);
        Assert::IsFalse(AnythingDrawn(toggled), L"a byte exclusive-ored twice left ink behind");
      }
    }

    /*
     * The shadow test on a PLANET: a circle at three radii, which is the whole of `CIRCLE`'s step
     * table -- eight segments for a speck, sixteen for a planet, thirty-two when you are close.
     */
    TEST_METHOD(ThePlanetLandsOnBothSurfaces)
    {
      for (const std::uint8_t radius : {std::uint8_t{6}, std::uint8_t{30}, std::uint8_t{90}})
      {
        auto universe = std::make_unique<Elite::Universe>();
        universe->heaps.lowestVisibleRow = Elite::SPACE_VIEW_BOTTOM;

        const Elite::Projection centre{Elite::SPACE_VIEW_CENTRE_X, 0u, Elite::SPACE_VIEW_CENTRE_Y, 0u};
        const bool refused = Elite::DrawCircle(universe->canvas, universe->heaps, universe->geometry, universe->math, universe->clip,
                                               centre, radius, &universe->picture);

        Assert::IsFalse(refused, L"CHKON refused a circle in the middle of the view");
        Assert::IsTrue(AnythingDrawn(universe->picture), L"the planet drew nothing on the picture");
        AssertAgrees(*universe, Canvas::SPACE_VIEW_HEIGHT, L"planet r=" + std::to_wstring(radius));
      }
    }

    /// And drawing it again takes it off both, which is how the game erases a planet: `WPLS2` walks
    /// the same heap the circle filled, and the twin reads the same bytes doubled (rule T3).
    TEST_METHOD(ErasingThePlanetClearsBothSurfaces)
    {
      auto universe = std::make_unique<Elite::Universe>();
      universe->heaps.lowestVisibleRow = Elite::SPACE_VIEW_BOTTOM;

      const Elite::Projection centre{Elite::SPACE_VIEW_CENTRE_X, 0u, Elite::SPACE_VIEW_CENTRE_Y, 0u};
      (void)Elite::DrawCircle(universe->canvas, universe->heaps, universe->geometry, universe->math, universe->clip, centre, 40u,
                              &universe->picture);
      Assert::IsTrue(AnythingDrawn(universe->picture), L"the planet drew nothing to erase");

      Elite::EraseBall(universe->canvas, universe->heaps, &universe->picture);

      for (const std::uint8_t byte : universe->picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"the erase left ink on the picture");
      }
    }

    /*
     * The shadow test on a SUN, over three frames of drift.
     *
     * One frame is not enough and the routine is why: `SUN` draws only the DIFFERENCE between last
     * frame's rows and this frame's, so a twin that is right on a fresh disc and wrong on a moving
     * one would pass a single-frame test. Three frames exercise both slivers.
     */
    TEST_METHOD(TheSunLandsOnBothSurfacesAsItDrifts)
    {
      auto universe = std::make_unique<Elite::Universe>();
      universe->heaps.lowestVisibleRow = Elite::SPACE_VIEW_BOTTOM;
      Elite::ClearSunHeap(universe->heaps);

      for (int frame = 0; frame < 3; ++frame)
      {
        const Elite::Projection centre{static_cast<std::uint8_t>(110 + 9 * frame), 0u, Elite::SPACE_VIEW_CENTRE_Y, 0u};
        Elite::DrawSun(universe->canvas, universe->heaps, universe->math, universe->rng, centre, 34u, &universe->picture);

        Assert::IsTrue(AnythingDrawn(universe->picture), L"the sun drew nothing on the picture");
        AssertAgrees(*universe, Canvas::SPACE_VIEW_HEIGHT, L"sun frame " + std::to_wstring(frame));
      }
    }

    /*
     * The shadow test on an EXPLOSION, which is the scene the design named (section 8.1) and the
     * one that exercises the particle twin: eight-bit positions rolled out of the cloud's own seeds
     * inside the drawing loop, so the twin cannot be given them -- it has to be beside the plot.
     */
    TEST_METHOD(TheExplosionCloudLandsOnBothSurfaces)
    {
      auto universe = std::make_unique<Elite::Universe>();

      Elite::Ship& work = universe->work;
      work.heap = Elite::HeapOffset::FromAddress(0xFF00u);
      work.z.hi = 0x20u;

      // The six bytes `EE55` seeds: the cloud size, the counter `DOEXP` ages, how many vertices it
      // blooms from, and three generator seeds.
      universe->heap.Write(work.heap, 24u);
      universe->heap.Write(work.heap.Byte(1u), 18u);
      universe->heap.Write(work.heap.Byte(2u), 14u);
      for (std::uint16_t byte = 3; byte < 7; ++byte)
      {
        universe->heap.Write(work.heap.Byte(byte), static_cast<std::uint8_t>(0x5Au + byte));
      }
      /*
       * The vertices, and their ORDER is the routine's rather than a coordinate's: `EXL3` reads four
       * bytes backwards into `K3`, so what the heap holds per vertex is x_lo, x_hi, y_lo, y_hi -- and
       * both high bytes have to be zero or `EXS1` rejects every particle before it plots one. That
       * is what this test's first draft got wrong, and a cloud that draws nothing agrees with a
       * picture that draws nothing.
       */
      const std::array<std::uint8_t, 8> VERTICES = {{120u, 0u, 70u, 0u, 132u, 0u, 60u, 0u}};
      for (std::size_t byte = 0; byte < VERTICES.size(); ++byte)
      {
        universe->heap.Write(work.heap.Byte(static_cast<std::uint16_t>(7u + byte)), VERTICES[byte]);
      }

      universe->math.lastDivisor = 24u;
      Elite::DrawExplosionParticles(universe->canvas, universe->math, universe->rng, work, universe->heap, universe->bubble,
                                    &universe->picture);

      Assert::IsTrue(AnythingDrawn(universe->picture), L"the cloud drew nothing on the picture");
      AssertAgrees(*universe, Canvas::SPACE_VIEW_HEIGHT, L"the explosion cloud");
    }

    /// The laser beam: four lines from the view's corners to a convergence point `FireLaser` rolls.
    TEST_METHOD(TheLaserBeamLandsOnBothSurfaces)
    {
      auto universe = std::make_unique<Elite::Universe>();
      universe->burst.x = 124u;
      universe->burst.y = 70u;

      (void)Elite::DrawLaserLines(universe->canvas, universe->burst, 0u, &universe->picture);

      Assert::IsTrue(AnythingDrawn(universe->picture), L"the beam drew nothing on the picture");
      AssertAgrees(*universe, Canvas::SPACE_VIEW_HEIGHT, L"the laser beam");

      // And `LASLI2` draws it again to take it off, which is how a burst ends.
      (void)Elite::DrawLaserLines(universe->canvas, universe->burst, 0u, &universe->picture);
      for (const std::uint8_t byte : universe->picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"the second beam left ink on the picture");
      }
    }

    /*
     * The frame: `BOX`'s rules and edges over the whole screen, which is the one thing in the slice
     * drawn in SURFACE coordinates rather than the view's.
     *
     * Compared over all 200 canvas rows rather than the space view's 144, because that is where a
     * border lives -- and because getting the coordinate space wrong is what kept this out of RS-1
     * and RS-2 (section 13).
     */
    TEST_METHOD(TheBorderLandsOnBothSurfaces)
    {
      auto universe = std::make_unique<Elite::Universe>();

      Elite::DrawFullBorder(universe->canvas, &universe->picture);

      Assert::IsTrue(AnythingDrawn(universe->picture), L"the border drew nothing on the picture");
      AssertAgrees(*universe, Canvas::HEIGHT, L"the whole-screen border");

      // `BOX2` exclusive-ors, so a second pass takes the edges and the rules off both surfaces --
      // which is exactly what `PrepareDeathScene` does with it.
      Elite::DrawFullBorder(universe->canvas, &universe->picture);
      AssertAgrees(*universe, Canvas::HEIGHT, L"the border drawn twice");
    }

    /*
     * The upper region is native, which is what makes every test above the thing a person sees
     * rather than a rehearsal. RS-4 turned the lower one over beside it.
     */
    TEST_METHOD(TheUpperRegionIsNative)
    {
      const Picture picture;
      Assert::IsTrue(picture.Native().spaceView, L"RS-3 did not turn the upper region over");
    }
  };

} // namespace GameLogicTests
