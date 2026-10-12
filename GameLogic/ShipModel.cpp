#include "pch.h"

#include "ShipModel.h"

#include "GameState.h"
#include "Maths.h"
#include "Scene.h"
#include "Text.h"

#include <cmath>
#include <memory>
#include <span>
#include <utility>

namespace Elite
{

namespace
{

constexpr std::size_t VERTEX_BYTES = 6; // a vertex of vertexBuffer: x, y and z words
constexpr std::size_t POINT_BYTES = 4;  // a projected point: x and y words, where the face list's bytes point
constexpr std::uint8_t FACE_TRIANGLE = 0x80;
constexpr std::uint8_t ITEM_OFFSET_BITS = 0x7F;
constexpr std::uint16_t TRIANGLE_ITEM_BYTES = 4;
constexpr std::uint8_t EDGE_POINT_BITS = 0xFC;
constexpr std::uint8_t EDGE_COLOR_BITS = 0x03;
constexpr std::size_t PATTERN_PIXELS = 8; // four 2-bit pixels in each of its two bytes
constexpr std::uint8_t DODO = 0;
constexpr std::uint16_t TURN = 0x800;

// The point a face list's byte names, as LoadTriangle shifts it (_shift, as a byte) into the byte the point starts at.
[[nodiscard]] std::uint16_t PointOf(std::uint8_t _byte, std::uint8_t _shift) noexcept
{
  return static_cast<std::uint16_t>(static_cast<std::uint8_t>(_byte << _shift) / POINT_BYTES);
}

using Vector3 = std::array<double, 3>;

[[nodiscard]] Vector3 At(const ShipModel& _model, std::uint16_t _vertex)
{
  const ModelVertex& vertex = _model.vertices.at(_vertex);
  return {vertex.x, vertex.y, vertex.z};
}

// The unit normal of _face's test triangle, (t0 - t1) x (t2 - t1); none, 0, for a triangle of no area.
[[nodiscard]] ModelVertex NormalOf(const ShipModel& _model, const ModelFace& _face)
{
  const Vector3 middle = At(_model, _face.test[1]);
  const Vector3 first = At(_model, _face.test[0]);
  const Vector3 last = At(_model, _face.test[2]);
  const Vector3 u{first[0] - middle[0], first[1] - middle[1], first[2] - middle[2]};
  const Vector3 v{last[0] - middle[0], last[1] - middle[1], last[2] - middle[2]};
  const Vector3 normal{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
  const double length = std::hypot(normal[0], normal[1], normal[2]);
  if (length == 0.0)
  {
    return ModelVertex{};
  }
  return ModelVertex{
    .x = static_cast<float>(normal[0] / length), .y = static_cast<float>(normal[1] / length), .z = static_cast<float>(normal[2] / length)};
}

// _blueprint's vertices, as its handler and program leave them with every angle 0 and the centre at the origin.
void BuildVertices(const Blueprint& _blueprint, ShipModel& _model)
{
  const auto state = std::make_unique<GameState>(); // over 100 KiB
  // The player's rotation pairs 0-2, the drawn angles, the view and the centre all 0.
  for (std::size_t pair = 0; pair < 3; ++pair)
  {
    (void)SetSinCos(*state, pair, 0);
  }
  state->Data().scene.boxHalfWidth = _blueprint.halfWidth;
  if (_blueprint.shape == BlueprintShape::Box)
  {
    BuildBoxCornerVertices(*state, _blueprint.extents);
  }
  else
  {
    BuildDodoVertices(*state);
  }
  RunVertexProgram(*state, _blueprint.vertexProgram, Vector{});
  const std::span<const Counted<std::uint8_t>> buffer = BytesOf(std::as_const(*state).Data().scene.vertexBuffer);
  for (std::size_t vertex = 0; vertex < _blueprint.projectedVertices; ++vertex)
  {
    const auto coordinate = [&buffer, vertex](std::size_t _axis)
    { return static_cast<float>(static_cast<std::int16_t>(WordIn(buffer, vertex * VERTEX_BYTES + 2 * _axis))); };
    _model.vertices.push_back(ModelVertex{.x = coordinate(0), .y = coordinate(1), .z = coordinate(2)});
  }
}

// _blueprint's faces, as DrawVisibleFaces and DrawFaceItems walk its face list: each a test triangle, then a count of the bytes of its
// items, each an edge (one byte, its offset in the face edges) or a triangle (a byte with bit 7, its pattern's offset, then three
// vertex bytes).
void ReadFaces(const Blueprint& _blueprint, ShipModel& _model)
{
  const std::span<const std::uint8_t> list = _blueprint.faceList;
  std::size_t next = 0;
  for (std::uint16_t face = 0; face < _blueprint.faces; ++face)
  {
    ModelFace modelFace{.test = {PointOf(list[next], 0), PointOf(list[next + 1], 0), PointOf(list[next + 2], 0)}, .normal = {}};
    modelFace.normal = NormalOf(_model, modelFace);
    _model.faces.push_back(modelFace);
    next += 3;
    const std::uint8_t bytes = list[next];
    std::size_t item = next + 1;
    next = item + bytes;
    std::uint8_t layer = 0;
    while (item < next)
    {
      const std::uint8_t byte = list[item];
      const auto offset = static_cast<std::uint8_t>(byte & ITEM_OFFSET_BITS);
      if ((byte & FACE_TRIANGLE) == 0)
      {
        const std::uint8_t low = _blueprint.faceEdges[offset];
        const std::uint8_t high = _blueprint.faceEdges[offset + 1u];
        _model.edges.push_back(ModelEdge{.vertices = {PointOf(static_cast<std::uint8_t>(high & EDGE_POINT_BITS), 0),
                                                      PointOf(static_cast<std::uint8_t>(low & EDGE_POINT_BITS), 0)},
                                         .color = static_cast<std::uint8_t>(high & EDGE_COLOR_BITS),
                                         .face = face,
                                         .layer = layer});
        ++item;
      }
      else
      {
        _model.triangles.push_back(
          ModelTriangle{.vertices = {PointOf(list[item + 1], 1), PointOf(list[item + 2], 1), PointOf(list[item + 3], 1)},
                        .pattern = static_cast<std::uint8_t>(offset / 2),
                        .face = face,
                        .layer = layer});
        item += TRIANGLE_ITEM_BYTES;
      }
      ++layer;
    }
  }
}

} // namespace

ShipModel ShipModelOf(std::uint8_t _kind)
{
  const Blueprint& blueprint = BlueprintOf(_kind);
  ShipModel model;
  BuildVertices(blueprint, model);
  ReadFaces(blueprint, model);
  model.rollReversed = _kind == DODO;
  return model;
}

Orientation ModelRotation(const ShipModel& _model, std::uint16_t _pitch, std::uint16_t _yaw, std::uint16_t _roll) noexcept
{
  const std::uint16_t roll = _model.rollReversed ? static_cast<std::uint16_t>((TURN - _roll % TURN) % TURN) : _roll;
  return ObjectRotation(_pitch, _yaw, roll);
}

std::array<float, 4> PatternMix(std::uint8_t _pattern)
{
  const std::uint16_t pattern = FaceFillPattern(_pattern);
  std::array<float, 4> mix{};
  for (std::size_t pixel = 0; pixel < PATTERN_PIXELS; ++pixel)
  {
    mix[(pattern >> (2 * pixel)) & 3u] += 1.0f / PATTERN_PIXELS;
  }
  return mix;
}

} // namespace Elite
