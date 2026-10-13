#include "pch.h"

#include "Renderer.h"
#include "SceneSnapshot.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace EngineTests
{

namespace
{

// A picture of 128 x 128 source pixels at scale 2 on a target of 256 x 256, the inset the whole of it, with layers of 128 x 128
// pixels of its own: a layer pixel is 2 x 2 on screen. The camera's axis meets the inset's middle, screen (128, 128), and a point
// at x / z = 1 is at its right edge.
constexpr std::uint32_t SIDE = 128;
constexpr std::uint32_t TARGET_SIDE = 256;
constexpr std::uint32_t MIDDLE = TARGET_SIDE / 2;
constexpr float FOCAL = 64.0f;
constexpr std::array<std::uint32_t, 4> PALETTE = {0x000000, 0x00AAAA, 0xAA00AA, 0xFFFFFF};

// A planet of radius 2, 10 units ahead: it spans 64 x 2 / sqrt(96) = 13.06 source pixels, 26.1 screen pixels, about the middle.
constexpr float PLANET_RADIUS = 2.0f;
constexpr float PLANET_DEPTH = 10.0f;
constexpr std::uint32_t PLANET_COLOR = 0x3060C0;
constexpr std::uint32_t WITHIN_PIXELS = 22;
constexpr std::uint32_t BEYOND_PIXELS = 31;

// A white square, its side 1, facing the camera.
constexpr std::uint32_t SQUARE = 1;
constexpr float HALF_SIDE = 0.5f;

constexpr int MOST_CHANNEL_ERROR = 3;

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

[[nodiscard]] int Channel(std::uint32_t _color, int _shift) noexcept
{
  return static_cast<int>((_color >> _shift) & 0xFFu);
}

[[nodiscard]] int Brightest(std::uint32_t _color) noexcept
{
  return std::max({Channel(_color, 16), Channel(_color, 8), Channel(_color, 0)});
}

[[nodiscard]] Engine::SceneVertex Vertex(float _x, float _y) noexcept
{
  return Engine::SceneVertex{.position = {.x = _x, .y = _y, .z = 0.0f},
                             .normal = {.x = 0.0f, .y = 0.0f, .z = -1.0f},
                             .facePoint = {},
                             .paletteWeights = {0.0f, 0.0f, 0.0f, 1.0f},
                             .layer = 0.0f};
}

// The renderer on WARP, with the white square, and what it draws.
struct Rig
{
  Engine::Renderer renderer;
  std::vector<std::uint8_t> under = std::vector<std::uint8_t>(std::size_t{SIDE} * SIDE, Engine::SCENE_LAYER_CLEAR);
  std::vector<std::uint8_t> over = std::vector<std::uint8_t>(std::size_t{SIDE} * SIDE, Engine::SCENE_LAYER_CLEAR);
  std::vector<std::uint32_t> picture = std::vector<std::uint32_t>(std::size_t{SIDE} * SIDE, 0x808080);

  Rig()
  {
    Assert::IsTrue(renderer.Create(Engine::Renderer::Desc{
                     .warp = true, .sourceWidth = SIDE, .sourceHeight = SIDE, .logicalWidth = SIDE, .logicalHeight = SIDE}),
                   L"Direct3D 12 on WARP");
    const Engine::SceneVertex a = Vertex(-HALF_SIDE, -HALF_SIDE);
    const Engine::SceneVertex b = Vertex(HALF_SIDE, -HALF_SIDE);
    const Engine::SceneVertex c = Vertex(HALF_SIDE, HALF_SIDE);
    const Engine::SceneVertex d = Vertex(-HALF_SIDE, HALF_SIDE);
    const std::vector<Engine::SceneVertex> square = {a, b, c, a, c, d};
    Assert::IsTrue(renderer.AddModel(SQUARE, Engine::SceneModelDesc{.triangles = square, .lines = {}}), L"the square");
  }

  [[nodiscard]] Engine::SceneFrame Frame(const Engine::SceneSnapshot& _scene) const
  {
    return Engine::SceneFrame{.scene = &_scene,
                              .inset = Engine::SceneInset{.left = 0.0f,
                                                          .top = 0.0f,
                                                          .width = static_cast<float>(SIDE),
                                                          .height = static_cast<float>(SIDE),
                                                          .focalX = FOCAL,
                                                          .focalY = FOCAL,
                                                          .centerX = static_cast<float>(SIDE) / 2.0f,
                                                          .centerY = static_cast<float>(SIDE) / 2.0f,
                                                          .nearUnits = 1.0f,
                                                          .towardsLight = {.x = 0.0f, .y = 0.0f, .z = -1.0f},
                                                          .palette = PALETTE,
                                                          .layerWidth = SIDE,
                                                          .layerHeight = SIDE,
                                                          .under = under,
                                                          .over = over,
                                                          .dust = {},
                                                          .beams = {},
                                                          .sky = {},
                                                          .bloom = 0.0f}};
  }

  [[nodiscard]] std::vector<std::uint32_t> Capture(const Engine::SceneFrame& _frame)
  {
    std::vector<std::uint32_t> pixels = renderer.Capture(picture, 0, TARGET_SIDE, TARGET_SIDE, &_frame);
    Assert::AreEqual(std::size_t{TARGET_SIDE} * TARGET_SIDE, pixels.size(), L"a pixel for each of the target's");
    return pixels;
  }
};

[[nodiscard]] std::uint32_t At(const std::vector<std::uint32_t>& _pixels, std::uint32_t _x, std::uint32_t _y)
{
  return _pixels[std::size_t{_y} * TARGET_SIDE + _x];
}

[[nodiscard]] Engine::SceneSphere Planet()
{
  return Engine::SceneSphere{.key = 1,
                             .position = {.x = 0.0f, .y = 0.0f, .z = PLANET_DEPTH},
                             .radius = PLANET_RADIUS,
                             .surface = Engine::SphereSurface::Terrain,
                             .colors = {PLANET_COLOR, PLANET_COLOR, PLANET_COLOR, PLANET_COLOR},
                             .atmosphere = 0,
                             .seed = 7,
                             .level = 0.5f,
                             .cover = 0.0f,
                             .bands = 8.0f,
                             .turbulence = 0.5f,
                             .continues = false};
}

[[nodiscard]] Engine::SceneObject Square(float _depth) noexcept
{
  return Engine::SceneObject{
    .key = 9, .model = SQUARE, .position = {.x = 0.0f, .y = 0.0f, .z = _depth}, .orientation = {}, .continues = false};
}

} // namespace

// The scene's spheres, strokes, bloom and sky (ADR-035), drawn on WARP.
TEST_CLASS(SceneSphereTests)
{
public:
  // A sphere is drawn where the camera's projection puts it, as large, lit full on where it faces the light, and at its depth: a
  // square nearer covers it, and it covers one further off.
  TEST_METHOD(SpheresStandWhereTheyProject)
  {
    Rig rig;
    Engine::SceneSnapshot scene;
    scene.spheres = {Planet()};
    const Engine::SceneFrame frame = rig.Frame(scene);
    const std::vector<std::uint32_t> pixels = rig.Capture(frame);
    Assert::IsTrue(CloseTo(At(pixels, MIDDLE, MIDDLE), PLANET_COLOR), L"its middle, lit full on");
    Assert::IsTrue(Brightest(At(pixels, MIDDLE + WITHIN_PIXELS, MIDDLE)) > 0x10, L"within its radius, right");
    Assert::IsTrue(Brightest(At(pixels, MIDDLE, MIDDLE - WITHIN_PIXELS)) > 0x10, L"within its radius, up");
    Assert::AreEqual(PALETTE[0], At(pixels, MIDDLE + BEYOND_PIXELS, MIDDLE), L"beyond its radius, right");
    Assert::AreEqual(PALETTE[0], At(pixels, MIDDLE, MIDDLE + BEYOND_PIXELS), L"beyond its radius, down");

    scene.objects = {Square(PLANET_DEPTH - PLANET_RADIUS - 1.0f)};
    Assert::IsTrue(CloseTo(At(rig.Capture(rig.Frame(scene)), MIDDLE, MIDDLE), PALETTE[3]), L"a square nearer covers it");
    scene.objects = {Square(PLANET_DEPTH)};
    Assert::IsTrue(CloseTo(At(rig.Capture(rig.Frame(scene)), MIDDLE, MIDDLE), PLANET_COLOR), L"it covers a square at its middle");
  }

  // A star is white hot at its middle and glows past its rim, which a planet does not; far from either, space is dark.
  TEST_METHOD(StarsGlowPastTheirRim)
  {
    Rig rig;
    Engine::SceneSnapshot scene;
    Engine::SceneSphere star = Planet();
    star.surface = Engine::SphereSurface::Star;
    star.colors[0] = 0xFFAA00;
    scene.spheres = {star};
    const std::vector<std::uint32_t> pixels = rig.Capture(rig.Frame(scene));
    Assert::IsTrue(CloseTo(At(pixels, MIDDLE, MIDDLE), 0xFFFFFF), L"white at its middle");
    Assert::IsTrue(Channel(At(pixels, MIDDLE + BEYOND_PIXELS, MIDDLE), 16) > 0x20, L"its corona, past its rim");
    Assert::IsTrue(Brightest(At(pixels, 4, 4)) < 0x08, L"dark far from it");
    scene.spheres = {Planet()};
    Assert::AreEqual(PALETTE[0], At(rig.Capture(rig.Frame(scene)), MIDDLE + BEYOND_PIXELS, MIDDLE), L"a planet does not glow");
  }

  // A stroke is drawn along its line in its colour, over a model in front of it; and light past 1 blooms round it only while the
  // inset asks for bloom.
  TEST_METHOD(StrokesAndTheirBloom)
  {
    Rig rig;
    Engine::SceneSnapshot scene;
    scene.objects = {Square(2.0f)}; // covering screen 96 to 160 each way
    const std::array<Engine::SceneStroke, 1> beam = {Engine::SceneStroke{.fromX = 10.0f,
                                                                         .fromY = 64.0f,
                                                                         .toX = 118.0f,
                                                                         .toY = 64.0f,
                                                                         .fromWidth = 2.0f,
                                                                         .toWidth = 2.0f,
                                                                         .color = 0xFF0000,
                                                                         .intensity = 1.0f}};
    Engine::SceneFrame frame = rig.Frame(scene);
    frame.inset.beams = beam;
    std::vector<std::uint32_t> pixels = rig.Capture(frame);
    Assert::IsTrue(CloseTo(At(pixels, 40, MIDDLE), 0xFF0000), L"the beam, on its line");
    Assert::IsTrue(CloseTo(At(pixels, MIDDLE, MIDDLE), 0xFF0000), L"the beam, over the square");
    Assert::AreEqual(PALETTE[0], At(pixels, 40, MIDDLE + 12), L"off its line");

    const std::array<Engine::SceneStroke, 1> dust = {Engine::SceneStroke{.fromX = 20.5f,
                                                                         .fromY = 20.5f,
                                                                         .toX = 20.5f,
                                                                         .toY = 20.5f,
                                                                         .fromWidth = 1.0f,
                                                                         .toWidth = 1.0f,
                                                                         .color = 0xFFFFFF,
                                                                         .intensity = 1.0f}};
    frame.inset.beams = {};
    frame.inset.dust = dust;
    pixels = rig.Capture(frame);
    Assert::IsTrue(Brightest(At(pixels, 41, 41)) > 0xC0, L"a dot of dust, at its layer pixel");
    Assert::AreEqual(PALETTE[0], At(pixels, 50, 41), L"and nowhere else");

    const std::array<Engine::SceneStroke, 1> bright = {Engine::SceneStroke{.fromX = 10.0f,
                                                                           .fromY = 32.0f,
                                                                           .toX = 118.0f,
                                                                           .toY = 32.0f,
                                                                           .fromWidth = 2.0f,
                                                                           .toWidth = 2.0f,
                                                                           .color = 0xFFFFFF,
                                                                           .intensity = 8.0f}};
    frame.inset.dust = {};
    frame.inset.beams = bright;
    constexpr std::uint32_t NEAR_ROW = 64 + 10; // ten screen pixels below the stroke's middle row, 65
    Assert::IsTrue(Brightest(At(rig.Capture(frame), 60, NEAR_ROW)) < 0x04, L"no bloom unless asked for");
    frame.inset.bloom = 1.0f;
    Assert::IsTrue(Brightest(At(rig.Capture(frame), 60, NEAR_ROW)) > 0x10, L"the light past 1 blooms round it");
  }

  // The sky is behind the scene and the under layer, fixed in the scene's frame: stars where its seed puts them while it is shown,
  // none while it is not, and the under layer's colour over it.
  TEST_METHOD(TheSkyIsBehindEverything)
  {
    Rig rig;
    Engine::SceneSnapshot scene;
    rig.under[0] = 1;
    Engine::SceneFrame frame = rig.Frame(scene);
    frame.inset.sky =
      Engine::SceneSky{.shown = true, .seed = 3, .nebula = {0x402060, 0x206080}, .nebulaDensity = 0.0f, .starDensity = 1.0f};
    std::vector<std::uint32_t> pixels = rig.Capture(frame);
    std::size_t lit = 0;
    for (std::uint32_t y = 0; y < TARGET_SIDE; ++y)
    {
      for (std::uint32_t x = 0; x < TARGET_SIDE; ++x)
      {
        lit += Brightest(At(pixels, x, y)) > 0x20 ? 1 : 0;
      }
    }
    Assert::IsTrue(lit > 100, (L"stars: " + std::to_wstring(lit) + L" pixels lit").c_str());
    Assert::AreEqual(PALETTE[1], At(pixels, 0, 0), L"the under layer over the sky");
    frame.inset.sky.shown = false;
    pixels = rig.Capture(frame);
    Assert::AreEqual(PALETTE[0], At(pixels, 200, 200), L"no sky, no stars");
  }
};

} // namespace EngineTests
