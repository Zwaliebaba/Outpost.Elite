#include "pch.h"

#include "Renderer.h"

#include <cstdlib>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace EngineTests
{

namespace
{

// A picture of 4 x 2 source pixels, each a colour of its own, 8 x 4 on screen at scale 1: each source pixel is a square of
// 2 x 2 there, so that at a whole-number scale it covers a square of screen pixels, where the filter is exact.
constexpr std::uint32_t SOURCE_WIDTH = 4;
constexpr std::uint32_t SOURCE_HEIGHT = 2;
constexpr std::uint32_t LOGICAL_WIDTH = 8;
constexpr std::uint32_t LOGICAL_HEIGHT = 4;
constexpr std::array<std::uint32_t, 8> PICTURE = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x808080, 0xFFFF00, 0x00FFFF, 0xFF00FF};
constexpr std::array<std::uint32_t, 8> OTHER_PICTURE = {0x000000, 0x404040, 0xC0C0C0, 0x123456, 0x654321, 0x00FF00, 0xFF0000, 0x0000FF};
constexpr std::uint32_t BORDER = 0x102030;

// The target the tests draw on: 20 x 10, where the picture's largest whole-number scale is 2, 16 x 8, at (2, 1).
constexpr std::uint32_t TARGET_WIDTH = 20;
constexpr std::uint32_t TARGET_HEIGHT = 10;
constexpr std::uint32_t PICTURE_LEFT = 2;
constexpr std::uint32_t PICTURE_TOP = 1;
constexpr std::uint32_t SCREEN_PIXELS_PER_SOURCE_PIXEL = 4;

// Each channel of a pixel inside the picture within this of the source's: the filter samples at a texel's centre, which the
// rasteriser's interpolation can miss by a rounding.
constexpr int MOST_CHANNEL_ERROR = 2;

[[nodiscard]] Engine::Renderer::Desc WarpDesc() noexcept
{
  return Engine::Renderer::Desc{.warp = true,
                                .sourceWidth = SOURCE_WIDTH,
                                .sourceHeight = SOURCE_HEIGHT,
                                .logicalWidth = LOGICAL_WIDTH,
                                .logicalHeight = LOGICAL_HEIGHT};
}

[[nodiscard]] bool CloseTo(std::uint32_t _found, std::uint32_t _expected) noexcept
{
  for (int shift = 0; shift <= 16; shift += 8)
  {
    const int found = static_cast<int>((_found >> shift) & 0xFFu);
    const int expected = static_cast<int>((_expected >> shift) & 0xFFu);
    if (std::abs(found - expected) > MOST_CHANNEL_ERROR)
    {
      return false;
    }
  }
  return true;
}

// That _pixels, a capture of TARGET_WIDTH x TARGET_HEIGHT, holds _picture where PlacePicture puts it, each source pixel over
// its square of screen pixels, and the border everywhere else.
void AssertDrawn(const std::vector<std::uint32_t>& _pixels, const std::array<std::uint32_t, 8>& _picture)
{
  Assert::AreEqual(static_cast<std::uint32_t>(TARGET_WIDTH * TARGET_HEIGHT), static_cast<std::uint32_t>(_pixels.size()),
                   L"a pixel for each of the target's");
  for (std::uint32_t row = 0; row < TARGET_HEIGHT; ++row)
  {
    for (std::uint32_t column = 0; column < TARGET_WIDTH; ++column)
    {
      const std::uint32_t found = _pixels[std::size_t{row} * TARGET_WIDTH + column];
      const std::wstring where = L"(" + std::to_wstring(column) + L", " + std::to_wstring(row) + L")";
      const bool inside = column >= PICTURE_LEFT && column < PICTURE_LEFT + SOURCE_WIDTH * SCREEN_PIXELS_PER_SOURCE_PIXEL &&
                          row >= PICTURE_TOP && row < PICTURE_TOP + SOURCE_HEIGHT * SCREEN_PIXELS_PER_SOURCE_PIXEL;
      if (!inside)
      {
        Assert::AreEqual(BORDER, found, (L"the border at " + where).c_str());
        continue;
      }
      const std::uint32_t sourceColumn = (column - PICTURE_LEFT) / SCREEN_PIXELS_PER_SOURCE_PIXEL;
      const std::uint32_t sourceRow = (row - PICTURE_TOP) / SCREEN_PIXELS_PER_SOURCE_PIXEL;
      const std::uint32_t expected = _picture[std::size_t{sourceRow} * SOURCE_WIDTH + sourceColumn];
      Assert::IsTrue(CloseTo(found, expected), (L"the picture at " + where).c_str());
    }
  }
}

} // namespace

// The renderer (ADR-031): where it puts the picture, and what it draws there, on WARP, Direct3D 12's software device, so
// that CI sees what a player would.
TEST_CLASS(RendererTests)
{
public:
  // The largest whole-number scale that fits, centred, at the sizes D46 and D48 name; and the largest fit below scale 1.
  TEST_METHOD(PlacesThePictureAtAWholeNumberScale)
  {
    Assert::IsTrue(Engine::PlacePicture(1920, 1080, 640, 480) == Engine::ScreenRect{.left = 320, .top = 60, .width = 1280, .height = 960},
                   L"1080p: scale 2");
    Assert::IsTrue(Engine::PlacePicture(2560, 1440, 640, 480) == Engine::ScreenRect{.left = 320, .top = 0, .width = 1920, .height = 1440},
                   L"1440p: scale 3");
    Assert::IsTrue(Engine::PlacePicture(3840, 2160, 640, 480) == Engine::ScreenRect{.left = 640, .top = 120, .width = 2560, .height = 1920},
                   L"4K: scale 4");
    Assert::IsTrue(Engine::PlacePicture(3440, 1440, 640, 480) == Engine::ScreenRect{.left = 760, .top = 0, .width = 1920, .height = 1440},
                   L"21:9: scale 3, set in the middle");
    Assert::IsTrue(Engine::PlacePicture(641, 481, 640, 480) == Engine::ScreenRect{.left = 0, .top = 0, .width = 640, .height = 480},
                   L"the odd pixel right of and below it");
    Assert::IsTrue(Engine::PlacePicture(320, 200, 640, 480) == Engine::ScreenRect{.left = 27, .top = 0, .width = 266, .height = 200},
                   L"smaller than scale 1: the largest fit, by the height");
    Assert::IsTrue(Engine::PlacePicture(320, 400, 640, 480) == Engine::ScreenRect{.left = 0, .top = 80, .width = 320, .height = 240},
                   L"smaller than scale 1: the largest fit, by the width");
  }

  // The picture drawn on WARP and read back: each source pixel over its square at scale 2, the border round it.
  TEST_METHOD(DrawsThePictureOnWarp)
  {
    Engine::Renderer renderer;
    Assert::IsTrue(renderer.Create(WarpDesc()), L"Direct3D 12 on WARP");
    AssertDrawn(renderer.Capture(PICTURE, BORDER, TARGET_WIDTH, TARGET_HEIGHT), PICTURE);
  }

  // Frames one after another, through both frames in flight and round again: each draws the picture it was given, not the
  // one the other frame's upload buffer held.
  TEST_METHOD(EachFrameDrawsItsOwnPicture)
  {
    Engine::Renderer renderer;
    Assert::IsTrue(renderer.Create(WarpDesc()), L"Direct3D 12 on WARP");
    for (int frame = 0; frame < 2 * static_cast<int>(Engine::Renderer::FRAME_COUNT) + 1; ++frame)
    {
      const std::array<std::uint32_t, 8>& picture = frame % 2 == 0 ? PICTURE : OTHER_PICTURE;
      AssertDrawn(renderer.Capture(picture, BORDER, TARGET_WIDTH, TARGET_HEIGHT), picture);
    }
  }
};

} // namespace EngineTests
