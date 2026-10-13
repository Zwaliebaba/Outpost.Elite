#include "pch.h"

#include "Canvas.h"
#include "Renderer.h"
#include "SceneSnapshot.h"

#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace EngineTests
{

namespace
{

// A target a tenth of the design: a design unit is a tenth of a pixel, so (800, 500) in the design is pixel (80, 50).
constexpr std::uint32_t TARGET_WIDTH = 192;
constexpr std::uint32_t TARGET_HEIGHT = 108;
constexpr std::uint32_t BORDER = 0x102030;

// Each channel within this of what is expected: a pixel's middle is where its colour is measured, and the shapes' edges are
// smoothed over a pixel.
constexpr int MOST_CHANNEL_ERROR = 3;

[[nodiscard]] Engine::Renderer::Desc WarpDesc() noexcept
{
  // The picture is not drawn under a canvas; its grid is any.
  return Engine::Renderer::Desc{.warp = true, .sourceWidth = 4, .sourceHeight = 2, .logicalWidth = 8, .logicalHeight = 4};
}

[[nodiscard]] bool CloseTo(std::uint32_t _found, std::uint32_t _expected, int _error = MOST_CHANNEL_ERROR) noexcept
{
  for (int shift = 0; shift <= 16; shift += 8)
  {
    if (std::abs(static_cast<int>((_found >> shift) & 0xFFu) - static_cast<int>((_expected >> shift) & 0xFFu)) > _error)
    {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::uint32_t At(const std::vector<std::uint32_t>& _pixels, std::uint32_t _x, std::uint32_t _y)
{
  return _pixels[std::size_t{_y} * TARGET_WIDTH + _x];
}

[[nodiscard]] std::wstring Hex(std::uint32_t _color)
{
  wchar_t text[16] = {};
  std::swprintf(text, 16, L"%06X", static_cast<unsigned>(_color));
  return text;
}

} // namespace

// The 2D layer (ADR-034): shapes and text in design units, drawn on WARP through the back buffer's sRGB view.
TEST_CLASS(CanvasTests)
{
public:
  // Design units on a target (D48): the smaller scale of the two sides, the design centred.
  TEST_METHOD(PlacesTheDesignAtOneScale)
  {
    const Engine::DesignPlacement full = Engine::PlaceDesign(1920, 1080);
    Assert::IsTrue(full.scale == 1.0f && full.left == 0.0f && full.top == 0.0f, L"1080p: as designed");
    const Engine::DesignPlacement wide = Engine::PlaceDesign(2560, 1080);
    Assert::IsTrue(wide.scale == 1.0f && wide.left == 320.0f && wide.top == 0.0f, L"21:9: the height, centred across");
    const Engine::DesignPlacement tall = Engine::PlaceDesign(1280, 1024);
    Assert::IsTrue(std::abs(tall.scale - 2.0f / 3.0f) < 1e-6f && tall.left == 0.0f && tall.top == 152.0f, L"5:4: the width, centred down");
  }

  // A box, a ring and a quadrilateral shaded top to bottom, on the border. The box is its colour inside and the border outside;
  // the ring is drawn along its outline and not inside; the shading's middle is half the light, which sRGB shows as BCh.
  TEST_METHOD(DrawsShapesOnWarp)
  {
    Engine::Renderer renderer;
    Assert::IsTrue(renderer.Create(WarpDesc()), L"Direct3D 12 on WARP");
    Engine::Canvas canvas;
    canvas.Box(Engine::Rect{.left = 100.0f, .top = 100.0f, .width = 400.0f, .height = 300.0f}, 0xFFFF0000);
    canvas.Ring(Engine::Point{.x = 960.0f, .y = 540.0f}, 300.0f, 200.0f, 40.0f, 0xFF00FF00);
    canvas.Quad({Engine::Point{.x = 1400.0f, .y = 100.0f}, Engine::Point{.x = 1800.0f, .y = 100.0f},
                 Engine::Point{.x = 1800.0f, .y = 900.0f}, Engine::Point{.x = 1400.0f, .y = 900.0f}},
                0xFF000000, 0xFFFFFFFF);
    const std::vector<std::uint32_t> pixels = renderer.Capture({}, BORDER, TARGET_WIDTH, TARGET_HEIGHT, nullptr, &canvas);
    Assert::AreEqual(std::size_t{TARGET_WIDTH} * TARGET_HEIGHT, pixels.size(), L"a pixel for each of the target's");
    Assert::AreEqual(0xFF0000u, At(pixels, 30, 25), L"inside the box");
    Assert::AreEqual(BORDER, At(pixels, 5, 5), L"outside every shape: the border");
    Assert::IsTrue(CloseTo(At(pixels, 126, 54), 0x00FF00), (L"on the ring: " + Hex(At(pixels, 126, 54))).c_str());
    Assert::AreEqual(BORDER, At(pixels, 96, 54), L"inside the ring");
    const std::uint32_t middle = At(pixels, 160, 49); // its middle row, 49.5 of 10 to 90
    Assert::IsTrue(CloseTo(middle, 0xBCBCBC, 4), (L"the shading's middle, half the light: " + Hex(middle)).c_str());
  }

  // Text from DirectWrite's glyph atlas: white figures, drawn about their anchor and nowhere else. And the face is the canvas's,
  // Bahnschrift, with tabular figures, so that a number's width does not change with its digits (D39).
  TEST_METHOD(DrawsTextOnWarp)
  {
    Engine::Renderer renderer;
    Assert::IsTrue(renderer.Create(WarpDesc()), L"Direct3D 12 on WARP");
    Assert::IsTrue(renderer.HasBahnschrift(), L"Bahnschrift installed");
    const auto width = [&renderer](const wchar_t* _text)
    {
      Engine::CanvasText text;
      text.text = _text;
      text.font = Engine::Font::Number;
      text.size = 100.0f;
      return renderer.MeasureText(text);
    };
    Assert::IsTrue(width(L"1111") > 0.0f && std::abs(width(L"1111") - width(L"8888")) < 0.01f, L"tabular figures");

    Engine::Canvas canvas;
    canvas.Text(L"88", Engine::Font::Number, 400.0f, 0.0f, Engine::Point{.x = 960.0f, .y = 540.0f}, Engine::Align::Center, 0xFFFFFFFF);
    const std::vector<std::uint32_t> pixels = renderer.Capture({}, 0x000000, TARGET_WIDTH, TARGET_HEIGHT, nullptr, &canvas);
    Assert::AreEqual(std::size_t{TARGET_WIDTH} * TARGET_HEIGHT, pixels.size(), L"a pixel for each of the target's");
    std::size_t bright = 0;
    for (std::uint32_t y = 0; y < TARGET_HEIGHT; ++y)
    {
      for (std::uint32_t x = 0; x < TARGET_WIDTH; ++x)
      {
        const bool lit = (At(pixels, x, y) & 0xFF) > 0x80;
        // The two figures, 40 pixels high, stand within 40 pixels of the anchor across and 25 down.
        const bool nearAnchor = std::abs(static_cast<int>(x) - 96) <= 40 && std::abs(static_cast<int>(y) - 54) <= 25;
        Assert::IsFalse(lit && !nearAnchor,
                        (L"text away from its anchor at (" + std::to_wstring(x) + L", " + std::to_wstring(y) + L")").c_str());
        bright += lit ? 1 : 0;
      }
    }
    Assert::IsTrue(bright > 100, (L"the figures drawn: " + std::to_wstring(bright) + L" pixels").c_str());
  }

  // Under a canvas the scene's inset is in design units: an inset of the design's middle quarter, its under layer colour 1, is
  // drawn there, and the border round it.
  TEST_METHOD(PlacesTheSceneInDesignUnits)
  {
    Engine::Renderer renderer;
    Assert::IsTrue(renderer.Create(WarpDesc()), L"Direct3D 12 on WARP");
    Engine::SceneSnapshot scene;
    const std::vector<std::uint8_t> under(16, 1);
    const std::vector<std::uint8_t> over(16, Engine::SCENE_LAYER_CLEAR);
    const Engine::SceneFrame frame{.scene = &scene,
                                   .inset = Engine::SceneInset{.left = 480.0f,
                                                               .top = 270.0f,
                                                               .width = 960.0f,
                                                               .height = 540.0f,
                                                               .focalX = 960.0f,
                                                               .focalY = 540.0f,
                                                               .centerX = 480.0f,
                                                               .centerY = 270.0f,
                                                               .palette = {0x000000, 0x00AAAA, 0xAA00AA, 0xFFFFFF},
                                                               .layerWidth = 4,
                                                               .layerHeight = 4,
                                                               .layersLeft = 0.0f,
                                                               .layersTop = 0.0f,
                                                               .layersWidth = 0.0f,
                                                               .layersHeight = 0.0f,
                                                               .under = under,
                                                               .over = over,
                                                               .dust = {},
                                                               .beams = {},
                                                               .sky = {},
                                                               .bloom = 0.0f}};
    const Engine::Canvas canvas;
    const std::vector<std::uint32_t> pixels = renderer.Capture({}, BORDER, TARGET_WIDTH, TARGET_HEIGHT, &frame, &canvas);
    Assert::AreEqual(std::size_t{TARGET_WIDTH} * TARGET_HEIGHT, pixels.size(), L"a pixel for each of the target's");
    Assert::AreEqual(0x00AAAAu, At(pixels, 96, 54), L"the inset, from 48 to 144 across and 27 to 81 down");
    Assert::AreEqual(0x00AAAAu, At(pixels, 49, 28), L"its top left corner");
    Assert::AreEqual(BORDER, At(pixels, 46, 54), L"left of it");
    Assert::AreEqual(BORDER, At(pixels, 96, 83), L"below it");
  }
};

} // namespace EngineTests
