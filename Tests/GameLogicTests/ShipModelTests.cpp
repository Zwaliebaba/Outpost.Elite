#include "pch.h"

#include "FlightSnapshot.h"
#include "GameState.h"
#include "Maths.h"
#include "Scene.h"
#include "ShipModel.h"
#include "Text.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::size_t VERTEX_BYTES = 6;

// The game's rounding: each plane turn's, the arithmetic shifts of the program's averages, which round down and pile up to 4 units
// on a vertex built from several, and the Dodo's ring sines, high bytes only, which are out by up to 6 on a vertex 250 from the
// centre. A vertex is within this many units, plus this fraction of its distance from the centre, of where the model puts it.
constexpr double MOST_ERROR_UNITS = 5.0;
constexpr double MOST_ERROR_FRACTION = 0.01;

// Angles to try, 800h per turn: none, and mixes round every axis.
struct Angles
{
  std::uint16_t pitch;
  std::uint16_t yaw;
  std::uint16_t roll;
};
constexpr std::array<Angles, 5> OBJECT_ANGLES = {{{0, 0, 0}, {0x123, 0, 0}, {0, 0x2B0, 0}, {0, 0, 0x5C1}, {0x3F7, 0x111, 0x6A2}}};
constexpr std::array<Angles, 3> PLAYER_ANGLES = {{{0, 0, 0}, {0x0C8, 0x7A0, 0x044}, {0x6E1, 0x205, 0x399}}};
constexpr std::array<std::uint16_t, 4> VIEWS = {0, 0x200, 0x400, 0x600}; // front, left, rear and right
constexpr std::array<std::int16_t, 3> CENTER = {120, -75, 3000};

[[nodiscard]] std::array<double, 3> Rotate(const Elite::Orientation& _rotation, const std::array<double, 3>& _vector)
{
  const double x = _rotation.x;
  const double y = _rotation.y;
  const double z = _rotation.z;
  const double w = _rotation.w;
  const std::array<double, 3> t = {2.0 * (y * _vector[2] - z * _vector[1]), 2.0 * (z * _vector[0] - x * _vector[2]),
                                   2.0 * (x * _vector[1] - y * _vector[0])};
  return {_vector[0] + w * t[0] + (y * t[2] - z * t[1]), _vector[1] + w * t[1] + (z * t[0] - x * t[2]),
          _vector[2] + w * t[2] + (x * t[1] - y * t[0])};
}

[[nodiscard]] Elite::Orientation Product(const Elite::Orientation& _a, const Elite::Orientation& _b) noexcept
{
  return Elite::Orientation{.x = _a.w * _b.x + _a.x * _b.w + _a.y * _b.z - _a.z * _b.y,
                            .y = _a.w * _b.y - _a.x * _b.z + _a.y * _b.w + _a.z * _b.x,
                            .z = _a.w * _b.z + _a.x * _b.y - _a.y * _b.x + _a.z * _b.w,
                            .w = _a.w * _b.w - _a.x * _b.x - _a.y * _b.y - _a.z * _b.z};
}

// The game's own vertices of _blueprint drawn at _object's angles, the player at _player's, in view _view, centred on CENTER.
[[nodiscard]] std::vector<std::array<double, 3>> GameVertices(const Elite::Blueprint& _blueprint, Angles _object, Angles _player,
                                                              std::uint16_t _view)
{
  const auto state = std::make_unique<Elite::GameState>();
  state->Data().playerShip.viewAngle = _view;
  (void)Elite::SetSinCos(*state, 0, _player.pitch);
  (void)Elite::SetSinCos(*state, 1, _player.yaw);
  (void)Elite::SetSinCos(*state, 2, _player.roll);
  state->Data().playerShip.playerPitchAngle = _player.pitch;
  state->Data().scene.drawPitchAngle = _object.pitch;
  state->Data().scene.drawYawAngle = _object.yaw;
  state->Data().scene.drawRollAngle = _object.roll;
  state->Data().scene.drawCenterX = static_cast<std::uint16_t>(CENTER[0]);
  state->Data().scene.drawCenterY = static_cast<std::uint16_t>(CENTER[1]);
  state->Data().scene.drawCenterZ = static_cast<std::uint16_t>(CENTER[2]);
  state->Data().scene.boxHalfWidth = _blueprint.halfWidth;
  if (_blueprint.shape == Elite::BlueprintShape::Box)
  {
    Elite::BuildBoxCornerVertices(*state, _blueprint.extents);
  }
  else
  {
    Elite::BuildDodoVertices(*state);
  }
  Elite::RunVertexProgram(*state, _blueprint.vertexProgram, Elite::Vector{});
  const std::span<const Elite::Counted<std::uint8_t>> buffer = Elite::BytesOf(std::as_const(*state).Data().scene.vertexBuffer);
  std::vector<std::array<double, 3>> vertices;
  for (std::size_t vertex = 0; vertex < _blueprint.projectedVertices; ++vertex)
  {
    std::array<double, 3> at{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      at[axis] = static_cast<std::int16_t>(Elite::WordIn(buffer, vertex * VERTEX_BYTES + 2 * axis));
    }
    vertices.push_back(at);
  }
  return vertices;
}

// The game's test of whether a face is drawn, on its test triangle _a, _b, _c of the game's own vertices, projected as
// ProjectVertices projects them, 256x/z and 256y/z: TriangleWindingSign's (x0-x1)(y2-y1) - (y0-y1)(x2-x1), negative when it is.
struct Winding
{
  double value;
};

[[nodiscard]] Winding ProjectedWinding(const std::array<double, 3>& _a, const std::array<double, 3>& _b, const std::array<double, 3>& _c)
{
  const auto project = [](const std::array<double, 3>& _v) { return std::array<double, 2>{256.0 * _v[0] / _v[2], 256.0 * _v[1] / _v[2]}; };
  const std::array<double, 2> p0 = project(_a);
  const std::array<double, 2> p1 = project(_b);
  const std::array<double, 2> p2 = project(_c);
  return Winding{.value = (p0[0] - p1[0]) * (p2[1] - p1[1]) - (p0[1] - p1[1]) * (p2[0] - p1[0])};
}

[[nodiscard]] double Distance(const std::array<double, 3>& _a, const std::array<double, 3>& _b)
{
  return std::hypot(_a[0] - _b[0], _a[1] - _b[1], _a[2] - _b[2]);
}

} // namespace

// Ship models (ADR-033): each blueprint's model, turned and moved, is where the game draws it, and its faces are read whole.
TEST_CLASS(ShipModelTests)
{
public:
  // Every blueprint's model, turned by the object's rotation, the player's and the view's and moved to the centre, puts every
  // vertex where the game's own handler and program do, at every pair of angles tried, in each of the four views.
  TEST_METHOD(ModelsAreWhereTheGameDrawsThem)
  {
    double worst = 0.0;
    for (std::uint8_t kind = 0; kind < Elite::SHIP_MODELS; ++kind)
    {
      const Elite::ShipModel model = Elite::ShipModelOf(kind);
      const Elite::Blueprint& blueprint = Elite::BlueprintOf(kind);
      Assert::AreEqual(std::uint32_t{blueprint.projectedVertices}, static_cast<std::uint32_t>(model.vertices.size()));
      for (const Angles object : OBJECT_ANGLES)
      {
        for (const Angles player : PLAYER_ANGLES)
        {
          for (const std::uint16_t view : VIEWS)
          {
            const Elite::Orientation rotation =
              Product(Elite::ViewRotation(view), Product(Elite::PlayerRotation(player.pitch, player.yaw, player.roll),
                                                         Elite::ModelRotation(model, object.pitch, object.yaw, object.roll)));
            const std::vector<std::array<double, 3>> game = GameVertices(blueprint, object, player, view);
            for (std::size_t vertex = 0; vertex < game.size(); ++vertex)
            {
              const Elite::ModelVertex& own = model.vertices[vertex];
              const std::array<double, 3> turned = Rotate(rotation, {own.x, own.y, own.z});
              const double reach = std::hypot(double{own.x}, double{own.y}, double{own.z});
              double error = 0.0;
              for (std::size_t axis = 0; axis < 3; ++axis)
              {
                error = std::max(error, std::abs(turned[axis] + CENTER[axis] - game[vertex][axis]));
              }
              worst = std::max(worst, error - MOST_ERROR_FRACTION * reach);
              Assert::IsTrue(
                error <= MOST_ERROR_UNITS + MOST_ERROR_FRACTION * reach,
                (L"type " + std::to_wstring(kind) + L", vertex " + std::to_wstring(vertex) + L", error " + std::to_wstring(error)).c_str());
            }
          }
        }
      }
    }
    Logger::WriteMessage(("worst excess over the fraction: " + std::to_string(worst)).c_str());
  }

  // Each face is seen, by its normal turned with the model, exactly when the game's own test on its own vertices draws it, at
  // every pair of angles tried, in each of the four views. The game's vertices are out by up to 5 units (ModelsAreWhereTheGameDrawsThem), which decides a
  // face seen nearly edge on or a small one: only faces whose test triangle's shortest edge is MOST_ROUNDED_EDGE units or more,
  // seen at a sine of MOST_EDGE_ON or more from edge on, are compared.
  TEST_METHOD(FacesAreSeenAsTheGameSeesThem)
  {
    constexpr double MOST_EDGE_ON = 0.15; // 5 units across 40 is 0.125 rad
    constexpr double MOST_ROUNDED_EDGE = 40.0;
    constexpr double UNIT_ERROR = 1e-5;
    std::size_t compared = 0;
    for (std::uint8_t kind = 0; kind < Elite::SHIP_MODELS; ++kind)
    {
      const Elite::ShipModel model = Elite::ShipModelOf(kind);
      for (const Elite::ModelFace& face : model.faces)
      {
        const double length = std::hypot(double{face.normal.x}, double{face.normal.y}, double{face.normal.z});
        Assert::IsTrue(std::abs(length - 1.0) <= UNIT_ERROR, L"a unit normal");
      }
      const Elite::Blueprint& blueprint = Elite::BlueprintOf(kind);
      for (const Angles object : OBJECT_ANGLES)
      {
        for (const Angles player : PLAYER_ANGLES)
        {
          for (const std::uint16_t view : VIEWS)
          {
            const Elite::Orientation rotation =
              Product(Elite::ViewRotation(view), Product(Elite::PlayerRotation(player.pitch, player.yaw, player.roll),
                                                         Elite::ModelRotation(model, object.pitch, object.yaw, object.roll)));
            const std::vector<std::array<double, 3>> game = GameVertices(blueprint, object, player, view);
            for (std::size_t index = 0; index < model.faces.size(); ++index)
            {
              const Elite::ModelFace& face = model.faces[index];
              const std::array<double, 3>& t0 = game[face.test[0]];
              const std::array<double, 3>& t1 = game[face.test[1]];
              const std::array<double, 3>& t2 = game[face.test[2]];
              const Elite::ModelVertex& corner = model.vertices[face.test[1]];
              const std::array<double, 3> turned = Rotate(rotation, {corner.x, corner.y, corner.z});
              const std::array<double, 3> at = {turned[0] + CENTER[0], turned[1] + CENTER[1], turned[2] + CENTER[2]};
              const std::array<double, 3> normal = Rotate(rotation, {face.normal.x, face.normal.y, face.normal.z});
              const double facing = (normal[0] * at[0] + normal[1] * at[1] + normal[2] * at[2]) / std::hypot(at[0], at[1], at[2]);
              if (std::abs(facing) < MOST_EDGE_ON || std::min({Distance(t0, t1), Distance(t1, t2), Distance(t2, t0)}) < MOST_ROUNDED_EDGE)
              {
                continue;
              }
              const Winding drawn = ProjectedWinding(t0, t1, t2);
              ++compared;
              Assert::IsTrue((drawn.value < 0.0) == (facing < 0.0),
                             (L"type " + std::to_wstring(kind) + L", face " + std::to_wstring(index)).c_str());
            }
          }
        }
      }
    }
    Logger::WriteMessage(("faces compared: " + std::to_string(compared)).c_str());
    Assert::IsTrue(compared > 4000, L"enough faces compared");
  }

  // Every face's test triangle, triangles and edges name vertices the model has, every pattern is one of the sixteen, and every
  // blueprint with faces has at least one triangle.
  TEST_METHOD(FacesAreReadWhole)
  {
    for (std::uint8_t kind = 0; kind < Elite::SHIP_MODELS; ++kind)
    {
      const Elite::ShipModel model = Elite::ShipModelOf(kind);
      const auto valid = [&model](std::uint16_t _vertex) { return _vertex < model.vertices.size(); };
      Assert::AreEqual(std::uint32_t{Elite::BlueprintOf(kind).faces}, static_cast<std::uint32_t>(model.faces.size()));
      for (const Elite::ModelFace& face : model.faces)
      {
        Assert::IsTrue(std::ranges::all_of(face.test, valid), (L"a face of type " + std::to_wstring(kind)).c_str());
      }
      for (const Elite::ModelTriangle& triangle : model.triangles)
      {
        Assert::IsTrue(std::ranges::all_of(triangle.vertices, valid) && triangle.pattern < 16 && triangle.face < model.faces.size(),
                       (L"a triangle of type " + std::to_wstring(kind)).c_str());
      }
      for (const Elite::ModelEdge& edge : model.edges)
      {
        Assert::IsTrue(std::ranges::all_of(edge.vertices, valid) && edge.color < 4 && edge.face < model.faces.size(),
                       (L"an edge of type " + std::to_wstring(kind)).c_str());
      }
      Assert::IsTrue(model.faces.empty() || !model.triangles.empty(), (L"type " + std::to_wstring(kind) + L" is filled").c_str());
    }
  }

  // A fill pattern's mix: a solid colour is all of one, a checkerboard half of each of two, and every mix sums to one.
  TEST_METHOD(PatternMixCountsThePixels)
  {
    Assert::AreEqual(1.0f, Elite::PatternMix(0)[0], L"solid 0");
    Assert::AreEqual(1.0f, Elite::PatternMix(9)[3], L"solid 3");
    const std::array<float, 4> checker = Elite::PatternMix(6); // 6699h: colours 2 and 1
    Assert::IsTrue(checker[1] == 0.5f && checker[2] == 0.5f, L"a checkerboard of 2 and 1");
    for (std::uint8_t pattern = 0; pattern < 16; ++pattern)
    {
      const std::array<float, 4> mix = Elite::PatternMix(pattern);
      Assert::AreEqual(1.0f, mix[0] + mix[1] + mix[2] + mix[3]);
    }
  }
};

} // namespace GameLogicTests
