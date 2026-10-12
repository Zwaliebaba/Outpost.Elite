#include "pch.h"

#include "SpaceView.h"

#include "FlightSnapshot.h"
#include "Platform.h"
#include "SpaceViewLayers.h"

#include <cmath>
#include <vector>

namespace Outpost
{

namespace
{

// The space view in the picture's grid of 640 dots by 200 lines (FrameImage): the 320x200 mode's pixels are two dots each.
constexpr float DOTS_PER_PIXEL = 2.0f;

// ProjectVertices' projection: 256 pixels across per unit of x / z and 256 lines down per unit of y / z, from pixel 128 of the
// view and buffer row 64, the view's row 63. Each rounds its coordinate towards zero or to the nearest, so a pixel's middle is
// where its coordinate falls: the axis is at the middle of pixel 128 and of row 63.
constexpr float FOCAL = 256.0f;
constexpr float AXIS_PIXEL = 128.5f;
constexpr float AXIS_ROW = 63.5f;
constexpr float NEAR_UNITS = 16.0f;

constexpr std::uint8_t SUN_SLOT = 0;
constexpr std::size_t PALETTE_COLORS = 4;

static_assert(Engine::SCENE_LAYER_CLEAR == Elite::LAYER_CLEAR, "the game's layers are the renderer's");

[[nodiscard]] Engine::Vector3 VectorOf(const Elite::ModelVertex& _vertex) noexcept
{
  return Engine::Vector3{.x = _vertex.x, .y = _vertex.y, .z = _vertex.z};
}

[[nodiscard]] Engine::Quaternion QuaternionOf(const Elite::Orientation& _orientation) noexcept
{
  return Engine::Quaternion{.x = _orientation.x, .y = _orientation.y, .z = _orientation.z, .w = _orientation.w};
}

// A unit vector along (_x, _y, _z), or none for none.
[[nodiscard]] Engine::Vector3 Unit(float _x, float _y, float _z) noexcept
{
  const float length = std::sqrt(_x * _x + _y * _y + _z * _z);
  return length == 0.0f ? Engine::Vector3{} : Engine::Vector3{.x = _x / length, .y = _y / length, .z = _z / length};
}

// _vector turned by _rotation, a unit quaternion.
[[nodiscard]] Engine::Vector3 Rotate(const Elite::Orientation& _rotation, const Engine::Vector3& _vector) noexcept
{
  const Engine::Vector3 twice{.x = 2.0f * (_rotation.y * _vector.z - _rotation.z * _vector.y),
                              .y = 2.0f * (_rotation.z * _vector.x - _rotation.x * _vector.z),
                              .z = 2.0f * (_rotation.x * _vector.y - _rotation.y * _vector.x)};
  return Engine::Vector3{.x = _vector.x + _rotation.w * twice.x + (_rotation.y * twice.z - _rotation.z * twice.y),
                         .y = _vector.y + _rotation.w * twice.y + (_rotation.z * twice.x - _rotation.x * twice.z),
                         .z = _vector.z + _rotation.w * twice.z + (_rotation.x * twice.y - _rotation.y * twice.x)};
}

[[nodiscard]] std::uint32_t Rgb(const Elite::RgbColor& _color) noexcept
{
  return (std::uint32_t{_color.red} << 16) | (std::uint32_t{_color.green} << 8) | _color.blue;
}

} // namespace

bool SpaceView::AddModels(Engine::Renderer& _renderer)
{
  m_models.clear();
  for (std::uint8_t kind = 0; kind < Elite::SHIP_MODELS; ++kind)
  {
    m_models.push_back(Elite::ShipModelOf(kind));
    const Elite::ShipModel& model = m_models.back();
    // Each vertex carries its face's normal and the face's test corner, so the renderer sees or culls the face as the game does.
    // Each vertex carries its item's layer too, so that what the game paints later over its face, the Dodo's slot, stays over it.
    const auto vertex = [&model](std::uint16_t _vertex, std::uint16_t _face, const std::array<float, 4>& _weights, std::uint8_t _layer)
    {
      const Elite::ModelFace& face = model.faces.at(_face);
      return Engine::SceneVertex{.position = VectorOf(model.vertices.at(_vertex)),
                                 .normal = VectorOf(face.normal),
                                 .facePoint = VectorOf(model.vertices.at(face.test[1])),
                                 .paletteWeights = _weights,
                                 .layer = static_cast<float>(_layer)};
    };
    std::vector<Engine::SceneVertex> triangles;
    for (const Elite::ModelTriangle& triangle : model.triangles)
    {
      const std::array<float, 4> weights = Elite::PatternMix(triangle.pattern);
      for (const std::uint16_t corner : triangle.vertices)
      {
        triangles.push_back(vertex(corner, triangle.face, weights, triangle.layer));
      }
    }
    std::vector<Engine::SceneVertex> lines;
    for (const Elite::ModelEdge& edge : model.edges)
    {
      std::array<float, 4> weights{};
      weights.at(edge.color) = 1.0f;
      for (const std::uint16_t end : edge.vertices)
      {
        lines.push_back(vertex(end, edge.face, weights, edge.layer));
      }
    }
    if (!_renderer.AddModel(kind, Engine::SceneModelDesc{.triangles = triangles, .lines = lines}))
    {
      return false;
    }
  }
  return true;
}

Engine::SceneSnapshot SpaceView::SceneOf(const Elite::FlightSnapshot& _snapshot) const
{
  Engine::SceneSnapshot scene;
  scene.time = _snapshot.pacedTime;
  scene.cameraOrientation = QuaternionOf(Elite::CameraRotation(_snapshot));
  scene.cameraContinues = _snapshot.cameraContinues;
  for (std::size_t index = 0; index < _snapshot.objectCount; ++index)
  {
    const Elite::SnapshotObject& object = _snapshot.objects[index];
    if (!object.model || object.kind >= m_models.size())
    {
      continue;
    }
    const std::array<float, 3> position = Elite::ScenePosition(_snapshot, object);
    scene.objects.push_back(
      Engine::SceneObject{.key = object.slot,
                          .model = object.kind,
                          .position = {.x = position[0], .y = position[1], .z = position[2]},
                          .orientation = QuaternionOf(Elite::ModelRotation(m_models[object.kind], object.pitch, object.yaw, object.roll)),
                          .continues = object.continues});
  }
  return scene;
}

const Engine::SceneFrame* SpaceView::Frame(const Elite::Platform& _platform)
{
  const Elite::FlightSnapshot& latest = _platform.LatestFlightSnapshot();
  if (latest.sequence == 0 || !_platform.SpaceViewShown())
  {
    return nullptr;
  }
  if (latest.sequence != m_afterSequence)
  {
    m_before = SceneOf(_platform.PreviousFlightSnapshot());
    m_after = SceneOf(latest);
    m_afterSequence = latest.sequence;
  }
  m_drawn = Engine::Interpolate(m_before, m_after, Engine::BlendFactor(m_before.time, m_after.time, _platform.Clock()));

  // The sun's way, in the game's axes, which the scene's are; with no sun, from behind the camera.
  Engine::Vector3 towardsLight = Engine::Vector3{};
  for (std::size_t index = 0; index < latest.objectCount; ++index)
  {
    const Elite::SnapshotObject& object = latest.objects[index];
    if (object.slot == SUN_SLOT)
    {
      towardsLight = Unit(static_cast<float>(object.x), static_cast<float>(object.y), static_cast<float>(object.z));
    }
  }
  if (towardsLight == Engine::Vector3{})
  {
    // The camera's way back, its -z, turned into the game's axes by the camera's rotation undone.
    const Elite::Orientation camera = Elite::CameraRotation(latest);
    towardsLight = Rotate(Elite::Orientation{.x = -camera.x, .y = -camera.y, .z = -camera.z, .w = camera.w},
                          Engine::Vector3{.x = 0.0f, .y = 0.0f, .z = -1.0f});
  }

  const std::array<std::uint8_t, 4> colors = _platform.GraphicsColors();
  const auto palette = Elite::CgaPalette();
  Engine::SceneInset& inset = m_frame.inset;
  for (std::size_t color = 0; color < PALETTE_COLORS; ++color)
  {
    inset.palette[color] = Rgb(palette[colors[color] & 0x0F]);
  }
  inset.left = static_cast<float>(Elite::SPACE_VIEW_LEFT_PIXELS) * DOTS_PER_PIXEL;
  inset.top = static_cast<float>(Elite::SPACE_VIEW_TOP_LINES);
  inset.width = static_cast<float>(Elite::SPACE_VIEW_WIDTH_PIXELS) * DOTS_PER_PIXEL;
  inset.height = static_cast<float>(Elite::SPACE_VIEW_HEIGHT_PIXELS);
  inset.focalX = FOCAL * DOTS_PER_PIXEL;
  inset.focalY = FOCAL;
  inset.centerX = AXIS_PIXEL * DOTS_PER_PIXEL;
  inset.centerY = AXIS_ROW;
  inset.nearUnits = NEAR_UNITS;
  inset.towardsLight = towardsLight;
  inset.layerWidth = Elite::SPACE_VIEW_WIDTH_PIXELS;
  inset.layerHeight = Elite::SPACE_VIEW_HEIGHT_PIXELS;
  inset.under = _platform.SpaceView().Under();
  inset.over = _platform.SpaceView().Over();
  m_frame.scene = &m_drawn;
  return &m_frame;
}

} // namespace Outpost
