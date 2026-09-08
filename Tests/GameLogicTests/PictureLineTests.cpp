#include "pch.h"

#include "Arith.h"
#include "Canvas.h"
#include "Picture.h"
#include "ShipBlueprint.h"
#include "ShipDraw.h"
#include "Lines2x.h"
#include "LookupTables.h"
#include "ShipDraw2x.h"
#include "StateHash.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The 640x400 picture's lines (Design/Resolution.md section 4.1, slices RS-2 and RS-3).
 *
 * TWO KINDS OF EVIDENCE, and they answer different questions. The SHADOW test asks whether the two
 * surfaces show the same picture, by downsampling the wide one and comparing; it would catch a twin
 * that dropped a line, drew it twice or put it somewhere else. It would NOT catch a twin that is
 * one wide pixel out everywhere, because a two-pixel block absorbs that -- and being one pixel out
 * everywhere is exactly what a wrong scale looks like.
 *
 * So the SWEEP is the other half, and RS-3 is where it earned its keep. The ship shadow test was
 * green with a twin that drew a DIFFERENT LINE: `LOIN` is not a Bresenham but a DDA over a
 * logarithm-table slope, and an exact drawer wandered up to three canvas pixels from it -- and drew
 * 510 pixels on each of the ninety-six lines the game refuses outright. A ship's edges are short
 * enough to hide all of that; a sweep over 18,432 lines is not. The sweep below is what says the
 * wide line is the faithful line and not a plausible one.
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

    /// And the picture, in the same view coordinates -- the twins add the doubled margin at the plot.
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
          // NOT `near`: `minwindef.h` defines `near` and `far` as empty macros, so a variable of
          // either name compiles on every other toolchain and fails on the one this project builds
          // with. It cost a red CI run at RS-2, and `check_outpost.py` now refuses both.
          bool alongside = false;
          for (int down = -1; down <= 1 && !alongside; ++down)
          {
            for (int across = -1; across <= 1 && !alongside; ++across)
            {
              const int cx = x / 2 + across;
              const int cy = y / 2 + down;
              alongside = cx >= 0 && cx < 256 && cy >= 0 && cy < Canvas::SPACE_VIEW_HEIGHT && CanvasLit(_canvas, cx, cy);
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
     * The sweep that says the wide line is the faithful line, and the one RS-2 did not have.
     *
     * Over 18,432 lines across the whole view, every lit wide pixel must lie within one canvas pixel
     * of a lit canvas pixel, in BOTH directions -- and no line the game refuses to draw may be drawn
     * here. RS-2's exact Bresenham failed both clauses: 431 wide pixels three canvas pixels away,
     * one four away, and 96 lines drawn on a canvas the game left blank.
     *
     * The residue this allows is named rather than absorbed: `LOIN` threads the carry out of its
     * SCREEN POINTER arithmetic into the accumulator, and a surface with its own geometry has no
     * address to take that from (Lines2x.h). It moves one pixel of some lines by one canvas pixel,
     * which is inside the slack, and nothing else does.
     */
    TEST_METHOD(TheWideLineIsTheFaithfulLineAtTwiceTheScale)
    {
      int lines = 0;
      int strayLines = 0;
      int strayPixels = 0;
      std::wstring first;

      for (int x1 = 0; x1 < 256; x1 += 17)
      {
        for (int y1 = 0; y1 < Canvas::SPACE_VIEW_HEIGHT; y1 += 13)
        {
          for (int x2 = 0; x2 < 256; x2 += 23)
          {
            for (int y2 = 0; y2 < Canvas::SPACE_VIEW_HEIGHT; y2 += 19)
            {
              Canvas canvas;
              Picture picture;
              const Elite::Line line{static_cast<std::uint8_t>(x1), static_cast<std::uint8_t>(y1), static_cast<std::uint8_t>(x2),
                                     static_cast<std::uint8_t>(y2)};
              (void)Elite::DrawLine(canvas, line);
              Elite::DrawLine2x(picture, line);
              ++lines;

              bool canvasHasInk = false;
              for (int y = 0; y < Canvas::SPACE_VIEW_HEIGHT && !canvasHasInk; ++y)
              {
                for (int x = 0; x < 256 && !canvasHasInk; ++x)
                {
                  canvasHasInk = CanvasLit(canvas, x, y);
                }
              }

              bool pictureHasInk = false;
              for (const std::uint8_t byte : picture.Bitmap())
              {
                pictureHasInk = pictureHasInk || byte != 0u;
              }

              if (!canvasHasInk && pictureHasInk)
              {
                if (strayLines == 0)
                {
                  first = L"the game drew nothing for (" + std::to_wstring(x1) + L", " + std::to_wstring(y1) + L")-(" +
                          std::to_wstring(x2) + L", " + std::to_wstring(y2) + L") and the picture did";
                }
                ++strayLines;
                continue;
              }

              for (int y = 0; y < Picture::SPACE_VIEW_HEIGHT; ++y)
              {
                for (int x = 0; x < 512; ++x)
                {
                  if (!PictureLit(picture, x, y))
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
                      alongside = cx >= 0 && cx < 256 && cy >= 0 && cy < Canvas::SPACE_VIEW_HEIGHT && CanvasLit(canvas, cx, cy);
                    }
                  }
                  if (!alongside)
                  {
                    if (strayPixels == 0)
                    {
                      first = L"(" + std::to_wstring(x1) + L", " + std::to_wstring(y1) + L")-(" + std::to_wstring(x2) + L", " +
                              std::to_wstring(y2) + L"): wide pixel (" + std::to_wstring(x) + L", " + std::to_wstring(y) +
                              L") is not beside the faithful line";
                    }
                    ++strayPixels;
                  }
                }
              }
            }
          }
        }
      }

      Assert::AreEqual(18432, lines, L"the sweep is not the one the measurement was taken over");
      Assert::AreEqual(0, strayLines, (L"lines the game refuses were drawn wide: " + first).c_str());
      Assert::AreEqual(0, strayPixels, (L"wide pixels away from the faithful line: " + first).c_str());
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

    /*
     * And the measurement that decided the line drawer, which is the same finding a third time.
     *
     * `LOIN`'s step is `LineSlope`, a logarithm-table lookup, so the line it draws is not the
     * straight line between its endpoints. If that ever became exact the twin could be a Bresenham
     * again -- and until it does, one must not be.
     */
    TEST_METHOD(TheFaithfulLineSlopeIsALogTableAndNotExact)
    {
      int worst = 0;
      int cases = 0;

      for (int denominator = 1; denominator < 256; ++denominator)
      {
        for (int numerator = 0; numerator <= denominator; ++numerator)
        {
          const int got = Elite::LineSlope(static_cast<std::uint8_t>(numerator), static_cast<std::uint8_t>(denominator));
          const int exact = (256 * numerator) / denominator;
          const int error = (got > exact) ? (got - exact) : (exact - got);
          worst = (error > worst) ? error : worst;
          ++cases;
        }
      }

      Assert::AreEqual(32895, cases, L"the sweep is not the whole space");
      Assert::IsTrue(worst >= 2, L"the slope has become exact, which would change what a twin may do");
    }

    /// The line drawer erases by drawing again, which is the whole of how a ship leaves the screen.
    TEST_METHOD(ALineDrawnTwiceIsGone)
    {
      Picture picture;
      const std::array<Elite::Line, 5> LINES = {
        {{0, 0, 255, 143}, {255, 0, 1, 143}, {10, 10, 10, 100}, {10, 10, 200, 10}, {127, 71, 128, 72}}};

      for (const Elite::Line& line : LINES)
      {
        Elite::DrawLine2x(picture, line);
      }
      bool anything = false;
      for (const std::uint8_t byte : picture.Bitmap())
      {
        anything = anything || byte != 0u;
      }
      Assert::IsTrue(anything, L"nothing was drawn at all");

      for (const Elite::Line& line : LINES)
      {
        Elite::DrawLine2x(picture, line);
      }
      for (const std::uint8_t byte : picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"a line drawn twice left ink behind");
      }
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

      Elite::DrawShipLines(universe->canvas, universe->heap, universe->work.heap, &universe->picture);

      for (const std::uint8_t byte : universe->picture.Bitmap())
      {
        Assert::AreEqual<std::uint32_t>(0u, byte, L"the second draw left ink on the picture");
      }
    }

    /*
     * The wide heap is GONE, and this is the test that says so (Resolution.md section 13).
     *
     * RS-2 kept a parallel record of every line at twice the scale, 13,824 bytes of it, excluded
     * from the state hash for the picture's reason. RS-3 deleted it: the twin reads the faithful
     * heap and doubles it, so there is nothing to keep in step and nothing to exclude. What is left
     * to assert is that the ship drawing still moves no game state it should not -- the heap the
     * game keeps still hashes, and the surface beside it still does not.
     */
    TEST_METHOD(TheShipDrawingMovesTheFaithfulHeapAndNotTheSurface)
    {
      Elite::Universe universe;
      const std::uint64_t base = Elite::HashState(universe);

      universe.picture.PlotPoint(11, 13);
      Assert::AreEqual(base, Elite::HashState(universe), L"the picture moved the state hash");

      universe.heap.Write(Elite::HeapOffset::FromAddress(0xFF00u), 0x5Au);
      Assert::AreNotEqual(base, Elite::HashState(universe), L"the faithful heap stopped moving it");
    }

  };

} // namespace GameLogicTests
