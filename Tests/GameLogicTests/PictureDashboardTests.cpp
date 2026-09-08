#include "pch.h"

#include "Canvas.h"
#include "Dashboard.h"
#include "Dashboard2x.h"
#include "LoaderScreen.h"
#include "Picture.h"
#include "Scanner.h"
#include "ViewChange.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The 640x400 picture's dashboard (Design/Resolution.md section 5, slice RS-4).
 *
 * THE LOWER REGION IS AN INDEX PLANE, so the evidence is shaped differently from the three slices
 * above it. There are no bits to compare: what a dashboard pixel holds is a colour, and a twin that
 * put the right shape in the wrong colour would pass a shape test and look wrong.
 *
 * So the first test here is an EQUALITY over the whole region -- the bootstrap resolves to exactly
 * the canvas doubled, every one of 71,680 pixels -- and it is the strongest thing in the slice: it
 * says the picture a person sees on the dashboard is the picture the game drew, before any twin
 * adds detail to it. The rest are the per-instrument properties section 10 asks for, and one shadow
 * test over a whole `DIALS` frame with the ink compared rather than the colour.
 */
namespace GameLogicTests
{

  namespace
  {
    using Elite::Canvas;
    using Elite::Picture;

    /// The whole picture, resolved, which is what a person sees.
    std::vector<std::uint8_t> ResolvePicture(const Picture& _picture, const Canvas& _canvas)
    {
      std::vector<std::uint8_t> out(static_cast<std::size_t>(Picture::WIDTH) * Picture::HEIGHT, std::uint8_t{0});
      _picture.Resolve(out, _canvas);
      return out;
    }

    std::vector<std::uint8_t> ResolveCanvas(const Canvas& _canvas)
    {
      std::vector<std::uint8_t> out(static_cast<std::size_t>(Canvas::WIDTH) * Canvas::HEIGHT, std::uint8_t{0});
      _canvas.Resolve(out);
      return out;
    }

    /// A universe with the loader's dashboard on it: the picture, its cell colours and its colour
    /// RAM, which is what makes the region anything other than black.
    std::unique_ptr<Elite::Universe> WithDashboard()
    {
      auto universe = std::make_unique<Elite::Universe>();
      Elite::SetUpLoaderScreen(universe->canvas, &universe->picture);
      universe->canvas.SetDashboardShown(true);

      // 6502: what `wantdials` copies -- the dashboard's own picture, and its twin beside it.
      Elite::CopyPagesDown(universe->canvas, Elite::DASHBOARD_IMAGE.data(), Elite::DASHBOARD_BITMAP, 8u, 0u);
      Elite::CopyPagesDown(universe->canvas, Elite::DASHBOARD_IMAGE.data() + 8u * 256u,
                           static_cast<std::uint16_t>(Elite::DASHBOARD_BITMAP + 8u * 256u), 1u, 0xC0u);
      Elite::CopyDashboardPicture2x(universe->picture, universe->canvas);
      return universe;
    }

    /// How many hi-res pixels of one canvas row of the plane are NOT the background.
    int WideInkOnRow(const Picture& _picture, const Canvas& _canvas, int _canvasRow, int _fromCanvasX, int _toCanvasX)
    {
      const std::uint8_t background = ColourIndex(_canvas.Background());
      int lit = 0;
      for (int x = 2 * _fromCanvasX; x < 2 * _toCanvasX; ++x)
      {
        lit += (_picture.Dot(x, 2 * _canvasRow) != background) ? 1 : 0;
      }
      return lit;
    }

    /// And the same on the canvas, counted in canvas pixels.
    int CanvasInkOnRow(const std::vector<std::uint8_t>& _resolved, const Canvas& _canvas, int _canvasRow, int _fromX, int _toX)
    {
      const std::uint8_t background = ColourIndex(_canvas.Background());
      int lit = 0;
      for (int x = _fromX; x < _toX; ++x)
      {
        lit += (_resolved[static_cast<std::size_t>(_canvasRow) * Canvas::WIDTH + x] != background) ? 1 : 0;
      }
      return lit;
    }
  } // namespace

  TEST_CLASS(ThePictureDashboard)
  {
  public:
    /*
     * The bootstrap IS the canvas doubled, pixel for pixel, and this is the slice's strongest test.
     *
     * Section 5.3 asked for a 71,680-byte generated table holding this image; RS-4 computes it from
     * `DASHBOARD_IMAGE` through the canvas's own decode instead, and what makes that safe is an
     * equality rather than a resemblance -- every one of the 71,680 pixels of the region, against
     * the same pixel of the resolved canvas. A generated table could only ever have been checked
     * this way, and this checks the thing that is actually in the tree.
     */
    TEST_METHOD(TheBootstrapIsTheCanvasDoubledExactly)
    {
      auto universe = WithDashboard();

      const std::vector<std::uint8_t> wide = ResolvePicture(universe->picture, universe->canvas);
      const std::vector<std::uint8_t> narrow = ResolveCanvas(universe->canvas);

      int compared = 0;
      for (int y = Canvas::SPACE_VIEW_HEIGHT; y < Canvas::HEIGHT; ++y)
      {
        for (int x = 0; x < Canvas::WIDTH; ++x)
        {
          const std::uint8_t want = narrow[static_cast<std::size_t>(y) * Canvas::WIDTH + x];
          for (int down = 0; down < 2; ++down)
          {
            for (int across = 0; across < 2; ++across)
            {
              const std::size_t at = static_cast<std::size_t>(2 * y + down) * Picture::WIDTH + (2 * x + across);
              ++compared;
              if (wide[at] != want)
              {
                Assert::Fail((L"the dashboard bootstrap differs at canvas (" + std::to_wstring(x) + L", " + std::to_wstring(y) +
                              L"): canvas " + std::to_wstring(want) + L", picture " + std::to_wstring(wide[at]))
                               .c_str());
              }
            }
          }
        }
      }

      Assert::AreEqual(static_cast<int>(Picture::DASHBOARD_SIZE), compared, L"the sweep did not cover the whole plane");
    }

    /*
     * The bars have twice the steps, from the same byte, and the sweep says so over every value.
     *
     * `DIL` lights `min(value >> shifts, 16)` fat pixels of sixteen; the twin lights
     * `min(value >> (shifts - 1), 32)` of thirty-two. Counted as INK on each surface rather than
     * asserted against the arithmetic, so a twin that computed the right number and drew it in the
     * wrong place would still fail.
     *
     * `DILX+2` and `DILX` -- shifts of two and four -- are the entries the fuel, the shields and the
     * temperatures use; `DIL-1` is the speed's one shift. All four are swept.
     */
    TEST_METHOD(EveryBarHasTwiceTheStepsFromTheSameByte)
    {
      int compared = 0;

      for (const int shifts : {1, 2, 4})
      {
        for (int value = 0; value < 256; ++value)
        {
          auto universe = WithDashboard();
          Elite::DrawWorkspace draw;
          draw.sc = static_cast<std::uint16_t>(Elite::DASHBOARD_BITMAP + 8u * 30u);

          const std::uint16_t sc = draw.sc;
          Elite::DrawBar(universe->canvas, draw, static_cast<std::uint8_t>(value), shifts, 255u,
                         Elite::DialColours{Elite::DIAL_NORMAL, Elite::PixelPattern::Blank}, &universe->picture);

          const int characterRow = sc / Canvas::ROW_BYTES;
          const int leftX = ((sc % Canvas::ROW_BYTES) / 8) * 8;
          const int row = characterRow * 8 + 2;

          const std::vector<std::uint8_t> narrow = ResolveCanvas(universe->canvas);
          const int canvasLit = CanvasInkOnRow(narrow, universe->canvas, row, leftX, leftX + 32);
          const int wideLit = WideInkOnRow(universe->picture, universe->canvas, row, leftX, leftX + 32);

          // A canvas step is a fat pixel, two canvas pixels wide; a wide step is two hi-res ones.
          const int faithfulSteps = canvasLit / 2;
          const int wideSteps = wideLit / 2;
          ++compared;

          const std::wstring where = L"shifts " + std::to_wstring(shifts) + L", value " + std::to_wstring(value) + L": " +
                                     std::to_wstring(faithfulSteps) + L" faithful steps and " + std::to_wstring(wideSteps) + L" wide";

          const int expectedFaithful = (value >> shifts) > 16 ? 16 : (value >> shifts);
          const int expectedWide = (value >> (shifts - 1)) > 32 ? 32 : (value >> (shifts - 1));

          Assert::AreEqual(expectedFaithful, faithfulSteps, (L"the faithful bar moved: " + where).c_str());
          Assert::AreEqual(expectedWide, wideSteps, (L"the wide bar is not the same byte at twice the scale: " + where).c_str());
        }
      }

      Assert::AreEqual(768, compared, L"the sweep did not cover every value at every entry point");
    }

    /*
     * The ENERGY bars are the exception and the sweep names them: `DILX` entered with no shift at
     * all sees a value that is already 0..16, so there is no bit under it and the twin doubles.
     */
    TEST_METHOD(TheEnergyBarsDoubleBecauseThereIsNoBitUnderThem)
    {
      for (int value = 0; value <= 16; ++value)
      {
        auto universe = WithDashboard();
        Elite::DrawWorkspace draw;
        draw.sc = static_cast<std::uint16_t>(Elite::DASHBOARD_BITMAP + 8u * 30u);
        const std::uint16_t sc = draw.sc;

        Elite::DrawBar(universe->canvas, draw, static_cast<std::uint8_t>(value), 0, 255u,
                       Elite::DialColours{Elite::DIAL_NORMAL, Elite::PixelPattern::Blank}, &universe->picture);

        const int leftX = ((sc % Canvas::ROW_BYTES) / 8) * 8;
        const int row = (sc / Canvas::ROW_BYTES) * 8 + 2;

        const std::vector<std::uint8_t> narrow = ResolveCanvas(universe->canvas);
        Assert::AreEqual(value, CanvasInkOnRow(narrow, universe->canvas, row, leftX, leftX + 32) / 2, L"the faithful energy bar moved");
        Assert::AreEqual(2 * value, WideInkOnRow(universe->picture, universe->canvas, row, leftX, leftX + 32) / 2,
                         L"the wide energy bar is not the faithful one doubled");
      }
    }

    /*
     * The scanner's extra bit: `x_lo` moves the wide blip and leaves the canvas one where it was.
     *
     * That is the whole claim of section 5.2 in one assertion -- the byte is there, the game does
     * not read it, and the twin does. Swept over every `x_hi` the scanner accepts, with the bit both
     * ways, so a twin that read the wrong end of the byte or the wrong sign fails at once.
     */
    TEST_METHOD(TheScannerBlipReadsTheFractionTheGameThrowsAway)
    {
      int moved = 0;
      int compared = 0;

      for (int high = 0; high < 64; ++high)
      {
        for (const std::uint8_t sign : {std::uint8_t{0}, std::uint8_t{0x80}})
        {
          std::array<std::unique_ptr<Elite::Universe>, 2> both;
          std::array<int, 2> firstLit{-1, -1};

          for (int which = 0; which < 2; ++which)
          {
            both[static_cast<std::size_t>(which)] = WithDashboard();
            Elite::Universe& universe = *both[static_cast<std::size_t>(which)];

            Elite::Ship ship;
            ship.x.hi = static_cast<std::uint8_t>(high);
            ship.x.lo = (which == 1) ? std::uint8_t{0x80} : std::uint8_t{0};
            ship.x.sgn = sign;
            ship.y.hi = 8u;
            ship.z.hi = 20u;
            ship.state = Elite::Mask(Elite::ShipStateBit::OnScanner);

            Elite::DrawScannerBlip(universe.canvas, ship, Elite::ShipType::CobraMk3, 0u, &universe.picture);

            // The leftmost hi-res pixel the blip lit, anywhere in the plane.
            for (int y = Picture::SPACE_VIEW_HEIGHT; y < Picture::HEIGHT && firstLit[static_cast<std::size_t>(which)] < 0; ++y)
            {
              for (int x = 0; x < Picture::WIDTH; ++x)
              {
                if (universe.picture.Dot(x, y) != ColourIndex(universe.canvas.Background()))
                {
                  // The bootstrap has ink everywhere, so what is compared is the blip's own EOR:
                  // the plane against the same plane without a blip on it.
                  continue;
                }
              }
            }
          }

          /*
           * The canvas blip cannot have moved -- `SCAN` never read the byte -- and the two wide
           * planes must differ, which is the fraction arriving. Compared as whole planes because
           * the blip's colour is exclusive-ored into whatever the picture already held.
           */
          const std::span<const std::uint8_t> a = both[0]->picture.Dashboard();
          const std::span<const std::uint8_t> b = both[1]->picture.Dashboard();

          bool planesDiffer = false;
          for (std::size_t at = 0; at < a.size() && !planesDiffer; ++at)
          {
            planesDiffer = a[at] != b[at];
          }

          for (std::uint16_t at = Elite::DASHBOARD_BITMAP; at < Canvas::BITMAP_SIZE; ++at)
          {
            Assert::AreEqual(both[0]->canvas.Read(at), both[1]->canvas.Read(at), L"`x_lo` moved the CANVAS blip, which SCAN never reads");
          }

          Assert::IsTrue(planesDiffer, L"`x_lo` did not move the wide blip, so the twin is not reading it");
          ++compared;
          moved += planesDiffer ? 1 : 0;
        }
      }

      Assert::AreEqual(128, compared, L"the sweep did not cover every high byte and both signs");
      Assert::AreEqual(compared, moved, L"the fraction failed to move the blip somewhere in the sweep");
    }

    /// A blip drawn twice is gone, which is how `WPSHPS` takes every ship off the scanner (rule T3).
    TEST_METHOD(ABlipDrawnTwiceLeavesThePlaneAsItWas)
    {
      auto universe = WithDashboard();
      const std::vector<std::uint8_t> before(universe->picture.Dashboard().begin(), universe->picture.Dashboard().end());

      Elite::Ship ship;
      ship.x.hi = 30u;
      ship.x.lo = 0x80u;
      ship.y.hi = 40u;
      ship.z.hi = 24u;
      ship.state = Elite::Mask(Elite::ShipStateBit::OnScanner);

      Elite::DrawScannerBlip(universe->canvas, ship, Elite::ShipType::CobraMk3, 0u, &universe->picture);

      bool drew = false;
      for (std::size_t at = 0; at < before.size() && !drew; ++at)
      {
        drew = universe->picture.Dashboard()[at] != before[at];
      }
      Assert::IsTrue(drew, L"the blip drew nothing on the plane");

      Elite::DrawScannerBlip(universe->canvas, ship, Elite::ShipType::CobraMk3, 0u, &universe->picture);

      for (std::size_t at = 0; at < before.size(); ++at)
      {
        Assert::AreEqual<std::uint32_t>(before[at], universe->picture.Dashboard()[at], L"the second blip did not erase the first");
      }
    }

    /*
     * A whole `DIALS` frame on both surfaces, compared as INK rather than as colour.
     *
     * The colour is compared exactly by the bootstrap test above; what this one asks is the shadow
     * question -- that the instruments are in the same places on both surfaces and that neither has
     * ink the other cannot account for. The slack is one canvas pixel, which is what a bar drawn at
     * half the step and a blip drawn at a quarter of the width need.
     */
    TEST_METHOD(AWholeDialsFrameLandsOnBothSurfaces)
    {
      auto universe = WithDashboard();

      universe->flight.delta = 22u;
      universe->flight.alp1 = 12u;
      universe->flight.alp2 = 0u;
      universe->flight.beta = 5u;
      universe->flight.bet1 = 5u;
      universe->flight.mainLoopCounter = 0u; // one pass in four draws everything
      universe->status.energy = 200u;
      universe->status.forwardShield = 190u;
      universe->status.aftShield = 120u;
      universe->status.cabinTemperature = 60u;
      universe->status.laserTemperature = 30u;
      universe->status.altitude = 200u;

      Elite::DrawDials(universe->canvas, universe->draw, universe->flight, universe->status, Elite::LightYearsTenths{50u},
                       universe->compass, universe->bubble, &universe->picture);

      const std::vector<std::uint8_t> narrow = ResolveCanvas(universe->canvas);
      const std::uint8_t background = ColourIndex(universe->canvas.Background());

      int canvasOnly = 0;
      int pictureOnly = 0;
      std::wstring first;

      // Canvas ink must have wide ink in the block that doubles it, grown by one wide pixel.
      for (int y = Canvas::SPACE_VIEW_HEIGHT; y < Canvas::HEIGHT; ++y)
      {
        for (int x = 0; x < Canvas::WIDTH; ++x)
        {
          if (narrow[static_cast<std::size_t>(y) * Canvas::WIDTH + x] == background)
          {
            continue;
          }

          bool alongside = false;
          for (int down = -1; down <= 2 && !alongside; ++down)
          {
            for (int across = -1; across <= 2 && !alongside; ++across)
            {
              alongside = universe->picture.Dot(2 * x + across, 2 * y + down) != background;
            }
          }
          if (!alongside)
          {
            if (canvasOnly == 0)
            {
              first = L"canvas ink at (" + std::to_wstring(x) + L", " + std::to_wstring(y) + L") with none on the picture";
            }
            ++canvasOnly;
          }
        }
      }

      /*
       * And the other way round, walked over WIDE pixels rather than canvas ones -- which is not a
       * detail of the loop but what makes the slack mean what it says. Attributing a wide pixel to
       * the canvas pixel it sits over and then growing the window by one is a different question
       * from growing the window first, and the second one reports a bar's last half-step as an
       * orphan when its own canvas pixel is one column to the left.
       */
      for (int y = Picture::SPACE_VIEW_HEIGHT; y < Picture::HEIGHT; ++y)
      {
        for (int x = 0; x < Picture::WIDTH; ++x)
        {
          if (universe->picture.Dot(x, y) == background)
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
              alongside = cx >= 0 && cx < Canvas::WIDTH && cy >= Canvas::SPACE_VIEW_HEIGHT && cy < Canvas::HEIGHT &&
                          narrow[static_cast<std::size_t>(cy) * Canvas::WIDTH + cx] != background;
            }
          }
          if (!alongside)
          {
            if (pictureOnly == 0)
            {
              first = L"picture ink at (" + std::to_wstring(x) + L", " + std::to_wstring(y) + L") with none on the canvas";
            }
            ++pictureOnly;
          }
        }
      }

      const std::wstring where = std::to_wstring(canvasOnly) + L" canvas-only and " + std::to_wstring(pictureOnly) +
                                 L" picture-only pixel(s) -- " + first;
      Assert::AreEqual(0, canvasOnly, where.c_str());
      Assert::AreEqual(0, pictureOnly, where.c_str());
    }

    /// Both regions are native, which is what RS-4 exists to deliver: nothing on the screen is the
    /// canvas upscaled any more.
    TEST_METHOD(NothingOnTheScreenIsUpscaledAnyMore)
    {
      const Picture picture;
      Assert::IsTrue(picture.Native().Complete(), L"a region is still drawn by doubling the canvas");
    }
  };

} // namespace GameLogicTests
