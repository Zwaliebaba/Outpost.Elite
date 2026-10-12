#include "pch.h"

#include "Renderer.h"
#include "SceneSnapshot.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace EngineTests
{

namespace
{

// A grey picture of 32 x 16 source pixels, square, drawn at scale 2 on a target of 64 x 32: a source pixel is 2 x 2 there.
constexpr std::uint32_t SOURCE_WIDTH = 32;
constexpr std::uint32_t SOURCE_HEIGHT = 16;
constexpr std::uint32_t TARGET_WIDTH = 64;
constexpr std::uint32_t TARGET_HEIGHT = 32;
constexpr std::uint32_t PICTURE_COLOR = 0x808080;
constexpr std::uint32_t BORDER = 0x102030;

// The inset: 16 x 8 source pixels from (8, 4), on screen from (16, 8) to (48, 24), with layers of its own 16 x 8 pixels. The
// camera's axis meets its middle, and a point at x / z = 1 is at its right edge.
constexpr float INSET_LEFT = 8.0f;
constexpr float INSET_TOP = 4.0f;
constexpr std::uint32_t LAYER_WIDTH = 16;
constexpr std::uint32_t LAYER_HEIGHT = 8;
constexpr std::uint32_t INSET_SCREEN_LEFT = 16;
constexpr std::uint32_t INSET_SCREEN_TOP = 8;
constexpr std::uint32_t INSET_SCREEN_RIGHT = 48;
constexpr std::uint32_t INSET_SCREEN_BOTTOM = 24;
constexpr float FOCAL = 8.0f;
constexpr std::array<std::uint32_t, 4> PALETTE = {0x000000, 0x00AAAA, 0xAA00AA, 0xFFFFFF};

// The models: a square of side 5 facing the camera, in colour 2, with a line across it in colour 3 a sixteenth of a unit below
// its middle and a patch on it a layer above it (Patch); and the same square facing away.
constexpr std::uint32_t FACING = 1;
constexpr std::uint32_t TURNED_AWAY = 2;
constexpr float HALF_SIDE = 2.5f;
constexpr float LINE_Y = 0.3125f; // 4.25 source pixels down the inset: the middle of screen row 16
constexpr float DEPTH = 10.0f;

// Each channel within this of what is expected: the scene is drawn in linear light and turned back to display colours.
constexpr int MOST_CHANNEL_ERROR = 2;

[[nodiscard]] bool CloseTo(std::uint32_t _found, std::uint32_t _expected) noexcept
{
  for (int shift = 0; shift <= 16; shift += 8)
  {
    if (std::abs(static_cast<int>((_found >> shift) & 0xFFu) - static_cast<int>((_expected >> shift) & 0xFFu)) > MOST_CHANNEL_ERROR)
    {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Engine::SceneVertex Vertex(float _x, float _y, float _normalZ, std::size_t _color, float _layer = 0.0f) noexcept
{
  Engine::SceneVertex vertex{.position = {.x = _x, .y = _y, .z = 0.0f},
                             .normal = {.x = 0.0f, .y = 0.0f, .z = _normalZ},
                             .facePoint = {.x = 0.0f, .y = 0.0f, .z = 0.0f},
                             .paletteWeights = {},
                             .layer = _layer};
  vertex.paletteWeights[_color] = 1.0f;
  return vertex;
}

// The square's two triangles, its normal _normalZ along z.
[[nodiscard]] std::vector<Engine::SceneVertex> Square(float _normalZ)
{
  const Engine::SceneVertex a = Vertex(-HALF_SIDE, -HALF_SIDE, _normalZ, 2);
  const Engine::SceneVertex b = Vertex(HALF_SIDE, -HALF_SIDE, _normalZ, 2);
  const Engine::SceneVertex c = Vertex(HALF_SIDE, HALF_SIDE, _normalZ, 2);
  const Engine::SceneVertex d = Vertex(-HALF_SIDE, HALF_SIDE, _normalZ, 2);
  return {a, b, c, a, c, d};
}

// A patch in colour 3 on the square, in its plane, a layer above it, as the Dodo's slot is on its front face: x 0.5 to 2 and y
// -2 to -1. It comes first, so that only its layer, not the order it is drawn in, puts it over the square.
[[nodiscard]] std::vector<Engine::SceneVertex> Patch()
{
  constexpr float LAYER = 1.0f;
  const Engine::SceneVertex a = Vertex(0.5f, -2.0f, -1.0f, 3, LAYER);
  const Engine::SceneVertex b = Vertex(2.0f, -2.0f, -1.0f, 3, LAYER);
  const Engine::SceneVertex c = Vertex(2.0f, -1.0f, -1.0f, 3, LAYER);
  const Engine::SceneVertex d = Vertex(0.5f, -1.0f, -1.0f, 3, LAYER);
  return {a, b, c, a, c, d};
}

[[nodiscard]] Engine::SceneObject Object(std::uint32_t _model, float _x) noexcept
{
  return Engine::SceneObject{
    .key = _model, .model = _model, .position = {.x = _x, .y = 0.0f, .z = DEPTH}, .orientation = {}, .continues = false};
}

[[nodiscard]] std::uint32_t At(const std::vector<std::uint32_t>& _pixels, std::uint32_t _x, std::uint32_t _y)
{
  return _pixels[std::size_t{_y} * TARGET_WIDTH + _x];
}

} // namespace

// A scene in an inset of the picture (ADR-033), drawn on WARP: its models where the camera's projection puts them, flat, lit and
// culled by their faces' normals, the layers under and over them, and the picture round the inset as it was.
TEST_CLASS(SceneInsetTests)
{
public:
  TEST_METHOD(DrawsTheSceneBetweenItsLayers)
  {
    Engine::Renderer renderer;
    Assert::IsTrue(renderer.Create(Engine::Renderer::Desc{.warp = true,
                                                          .sourceWidth = SOURCE_WIDTH,
                                                          .sourceHeight = SOURCE_HEIGHT,
                                                          .logicalWidth = SOURCE_WIDTH,
                                                          .logicalHeight = SOURCE_HEIGHT}),
                   L"Direct3D 12 on WARP");
    std::vector<Engine::SceneVertex> facing = Patch();
    std::ranges::copy(Square(-1.0f), std::back_inserter(facing));
    const std::vector<Engine::SceneVertex> line = {Vertex(-HALF_SIDE, LINE_Y, -1.0f, 3), Vertex(HALF_SIDE, LINE_Y, -1.0f, 3)};
    const std::vector<Engine::SceneVertex> turnedAway = Square(1.0f);
    Assert::IsTrue(renderer.AddModel(FACING, Engine::SceneModelDesc{.triangles = facing, .lines = line}), L"the facing square");
    Assert::IsTrue(renderer.AddModel(TURNED_AWAY, Engine::SceneModelDesc{.triangles = turnedAway, .lines = {}}), L"the square turned away");

    // The facing square in the middle, the other left of it; under, colour 1 in the top left pixel and in the two left of the
    // middle; over, colour 1 in the middle pixel, on the square.
    Engine::SceneSnapshot scene;
    scene.objects = {Object(FACING, 0.0f), Object(TURNED_AWAY, -6.0f)};
    std::vector<std::uint8_t> under(std::size_t{LAYER_WIDTH} * LAYER_HEIGHT, Engine::SCENE_LAYER_CLEAR);
    std::vector<std::uint8_t> over(under.size(), Engine::SCENE_LAYER_CLEAR);
    under[0] = 1;
    under[4 * LAYER_WIDTH + 0] = 1;
    under[4 * LAYER_WIDTH + 1] = 1;
    over[4 * LAYER_WIDTH + 8] = 1;
    Engine::SceneFrame frame{.scene = &scene,
                             .inset = Engine::SceneInset{.left = INSET_LEFT,
                                                         .top = INSET_TOP,
                                                         .width = static_cast<float>(LAYER_WIDTH),
                                                         .height = static_cast<float>(LAYER_HEIGHT),
                                                         .focalX = FOCAL,
                                                         .focalY = FOCAL,
                                                         .centerX = static_cast<float>(LAYER_WIDTH) / 2.0f,
                                                         .centerY = static_cast<float>(LAYER_HEIGHT) / 2.0f,
                                                         .nearUnits = 1.0f,
                                                         .towardsLight = {.x = 0.0f, .y = 0.0f, .z = -1.0f},
                                                         .palette = PALETTE,
                                                         .layerWidth = LAYER_WIDTH,
                                                         .layerHeight = LAYER_HEIGHT,
                                                         .under = under,
                                                         .over = over}};
    const std::vector<std::uint32_t> picture(std::size_t{SOURCE_WIDTH} * SOURCE_HEIGHT, PICTURE_COLOR);

    // Twice, through both frames in flight.
    for (std::uint32_t pass = 0; pass < Engine::Renderer::FRAME_COUNT; ++pass)
    {
      const std::vector<std::uint32_t> pixels = renderer.Capture(picture, BORDER, TARGET_WIDTH, TARGET_HEIGHT, &frame);
      Assert::AreEqual(std::size_t{TARGET_WIDTH} * TARGET_HEIGHT, pixels.size(), L"a pixel for each of the target's");
      for (std::uint32_t y = 0; y < TARGET_HEIGHT; ++y)
      {
        for (std::uint32_t x = 0; x < TARGET_WIDTH; ++x)
        {
          const bool inset = x >= INSET_SCREEN_LEFT && x < INSET_SCREEN_RIGHT && y >= INSET_SCREEN_TOP && y < INSET_SCREEN_BOTTOM;
          if (!inset && !CloseTo(At(pixels, x, y), PICTURE_COLOR))
          {
            Assert::Fail((L"the picture at (" + std::to_wstring(x) + L", " + std::to_wstring(y) + L")").c_str());
          }
        }
      }
      // The square covers screen x 28 to 36 and rows 12 to 20; its line is row 16.
      Assert::IsTrue(CloseTo(At(pixels, 29, 13), PALETTE[2]), L"the square, lit full on");
      Assert::IsTrue(CloseTo(At(pixels, 35, 19), PALETTE[2]), L"the square's far corner");
      Assert::IsTrue(CloseTo(At(pixels, 29, 16), PALETTE[3]), L"the line on the square");
      Assert::IsTrue(CloseTo(At(pixels, 34, 13), PALETTE[3]), L"the patch a layer above the square, drawn before it");
      Assert::AreEqual(PALETTE[1], At(pixels, 32, 16), L"over, on the square");
      Assert::AreEqual(PALETTE[1], At(pixels, 17, 9), L"under, where the scene draws nothing");
      Assert::AreEqual(PALETTE[1], At(pixels, 19, 17), L"under, left of the middle");
      Assert::AreEqual(PALETTE[0], At(pixels, 26, 10), L"nothing: colour 0");
      // The square turned away is centred 4.8 source pixels left of the middle, screen x 16 to 24 there: it is not drawn.
      Assert::AreEqual(PALETTE[0], At(pixels, 21, 13), L"the square turned away, culled");
    }
  }
};

} // namespace EngineTests
