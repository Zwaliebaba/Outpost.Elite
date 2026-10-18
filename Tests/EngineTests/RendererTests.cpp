#include "pch.h"

#include "Renderer.h"

#include <array>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace EngineTests
{

namespace
{

// Two backgrounds, a frame drawn in each in turn.
constexpr std::array<std::uint32_t, 2> BACKGROUNDS = {0x102030, 0xC08040};

// The target the tests draw on.
constexpr std::uint32_t TARGET_WIDTH = 20;
constexpr std::uint32_t TARGET_HEIGHT = 10;

// That _pixels, a capture of TARGET_WIDTH x TARGET_HEIGHT, is _background throughout.
void AssertBackground(const std::vector<std::uint32_t>& _pixels, std::uint32_t _background)
{
  Assert::AreEqual(static_cast<std::uint32_t>(TARGET_WIDTH * TARGET_HEIGHT), static_cast<std::uint32_t>(_pixels.size()),
                   L"a pixel for each of the target's");
  for (std::uint32_t row = 0; row < TARGET_HEIGHT; ++row)
  {
    for (std::uint32_t column = 0; column < TARGET_WIDTH; ++column)
    {
      const std::wstring where = L"(" + std::to_wstring(column) + L", " + std::to_wstring(row) + L")";
      Assert::AreEqual(_background, _pixels[std::size_t{row} * TARGET_WIDTH + column], (L"the background at " + where).c_str());
    }
  }
}

} // namespace

// The renderer (ADR-031, ADR-070): what it draws with nothing over its background, on WARP, Direct3D 12's software device, so
// that CI sees what a player would.
TEST_CLASS(RendererTests)
{
public:
  // A frame with no scene and no canvas is its background, read back as it was given.
  TEST_METHOD(DrawsTheBackgroundOnWarp)
  {
    Engine::Renderer renderer;
    Assert::IsTrue(renderer.Create(Engine::Renderer::Desc{.warp = true}), L"Direct3D 12 on WARP");
    AssertBackground(renderer.Capture(BACKGROUNDS[0], TARGET_WIDTH, TARGET_HEIGHT), BACKGROUNDS[0]);
  }

  // Frames one after another, through both frames in flight and round again: each draws the background it was given.
  TEST_METHOD(EachFrameDrawsItsOwnBackground)
  {
    Engine::Renderer renderer;
    Assert::IsTrue(renderer.Create(Engine::Renderer::Desc{.warp = true}), L"Direct3D 12 on WARP");
    for (std::size_t frame = 0; frame < 2 * std::size_t{Engine::Renderer::FRAME_COUNT} + 1; ++frame)
    {
      const std::uint32_t background = BACKGROUNDS[frame % BACKGROUNDS.size()];
      AssertBackground(renderer.Capture(background, TARGET_WIDTH, TARGET_HEIGHT), background);
    }
  }

  // Nothing is drawn on a target of no size.
  TEST_METHOD(NothingIsCapturedOfNoSize)
  {
    Engine::Renderer renderer;
    Assert::IsTrue(renderer.Create(Engine::Renderer::Desc{.warp = true}), L"Direct3D 12 on WARP");
    Assert::IsTrue(renderer.Capture(BACKGROUNDS[0], 0, TARGET_HEIGHT).empty(), L"no width");
    Assert::IsTrue(renderer.Capture(BACKGROUNDS[0], TARGET_WIDTH, 0).empty(), L"no height");
  }
};

} // namespace EngineTests
