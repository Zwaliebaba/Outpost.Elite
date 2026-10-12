#include "pch.h"

#include "SpaceView.h"

#include "FlightSnapshot.h"
#include "PlanetLook.h"
#include "Platform.h"
#include "SpaceViewLayers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace Outpost
{

namespace
{

// ProjectVertices' projection: 256 pixels across per unit of x / z and 256 lines down per unit of y / z, from pixel 128 of the
// view and buffer row 64, the view's row 63. Each rounds its coordinate towards zero or to the nearest, so a pixel's middle is
// where its coordinate falls: the axis is at the middle of pixel 128 and of row 63.
constexpr float FOCAL = 256.0f;
constexpr float AXIS_PIXEL = 128.5f;
constexpr float AXIS_ROW = 63.5f;
constexpr float NEAR_UNITS = 16.0f;

constexpr std::uint8_t SUN_SLOT = 0;
constexpr std::size_t PALETTE_COLORS = 4;

// The spheres (ADR-035): the title's planet's key, past every slot's, and how much of a share of 255 is.
constexpr std::uint32_t TITLE_PLANET_KEY = 0x100;
constexpr float SHARE = 255.0f;
constexpr float GAS_GIANT_LEVEL = 0.5f; // a gas giant has no sea
// The strokes, in the view's rows: a dot of dust, and a beam at the gun and at its end; the beam's light, which blooms; and how far a
// particle moves in a frame and is the same one, past which it was born again elsewhere.
constexpr float DUST_WIDTH_ROWS = 0.45f;
constexpr float BEAM_GUN_ROWS = 1.8f;
constexpr float BEAM_END_ROWS = 0.5f;
constexpr float BEAM_INTENSITY = 2.5f;
constexpr std::int16_t MOST_DUST_STEP_PIXELS = 12;
// A pixel's middle, and the drawing buffer's row over the view's: row 1 of the buffer is the view's first.
constexpr float MIDDLE = 0.5f;
constexpr float FIRST_VIEW_ROW = 1.0f;
// How much of the scene's light past 1 blooms.
constexpr float BLOOM_SHARE = 1.0f;

[[nodiscard]] float LayerX(std::int16_t _x) noexcept
{
  return static_cast<float>(_x) + MIDDLE;
}

[[nodiscard]] float LayerY(std::int16_t _row) noexcept
{
  return static_cast<float>(_row) - FIRST_VIEW_ROW + MIDDLE;
}

// A system's planet as the scene's sphere: its look's surface, colours and shares.
[[nodiscard]] Engine::SceneSphere PlanetSphere(std::uint32_t _key, const Elite::SnapshotSphere& _sphere, const Elite::PlanetLook& _look,
                                               bool _continues) noexcept
{
  const bool gasGiant = _look.kind == Elite::PlanetKind::GasGiant;
  return Engine::SceneSphere{.key = _key,
                             .position = {.x = _sphere.position[0], .y = _sphere.position[1], .z = _sphere.position[2]},
                             .radius = _sphere.radius,
                             .surface = gasGiant ? Engine::SphereSurface::Bands : Engine::SphereSurface::Terrain,
                             .colors = _look.colors,
                             .atmosphere = _look.atmosphere,
                             .seed = _look.seed,
                             .level = gasGiant ? GAS_GIANT_LEVEL : static_cast<float>(_look.seaLevel) / SHARE,
                             .cover = gasGiant ? 0.0f : static_cast<float>(_look.cloudCover) / SHARE,
                             .bands = static_cast<float>(_look.bands),
                             .turbulence = static_cast<float>(_look.turbulence) / SHARE,
                             .continues = _continues};
}

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

Engine::SceneSnapshot SpaceView::SceneOf(const Elite::FlightSnapshot& _snapshot, const std::array<std::uint32_t, 4>& _palette) const
{
  Engine::SceneSnapshot scene;
  scene.time = _snapshot.pacedTime;
  scene.cameraOrientation = QuaternionOf(Elite::CameraRotation(_snapshot));
  scene.cameraContinues = _snapshot.cameraContinues;
  const Elite::PlanetLook look = Elite::PlanetLookOf(_snapshot.galaxy, _snapshot.system);
  for (std::size_t index = 0; index < _snapshot.objectCount; ++index)
  {
    const Elite::SnapshotObject& object = _snapshot.objects[index];
    if (Elite::IsPlanet(object))
    {
      scene.spheres.push_back(PlanetSphere(object.slot, Elite::SphereOf(_snapshot, object), look, object.continues));
      continue;
    }
    if (Elite::IsSun(object))
    {
      // White hot in the colour its disc is drawn in, the high byte of its pitch (DrawSunOrPlanetDisc).
      const Elite::SnapshotSphere sphere = Elite::SphereOf(_snapshot, object);
      const std::uint32_t color = _palette.at((object.pitch >> 8) & (PALETTE_COLORS - 1));
      scene.spheres.push_back(Engine::SceneSphere{.key = object.slot,
                                                  .position = {.x = sphere.position[0], .y = sphere.position[1], .z = sphere.position[2]},
                                                  .radius = sphere.radius,
                                                  .surface = Engine::SphereSurface::Star,
                                                  .colors = {color, color, color, color},
                                                  .atmosphere = 0,
                                                  .seed = look.seed,
                                                  .level = 0.0f,
                                                  .cover = 0.0f,
                                                  .bands = 0.0f,
                                                  .turbulence = 0.0f,
                                                  .continues = object.continues});
      continue;
    }
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
  if (_snapshot.titlePlanet.shown)
  {
    scene.spheres.push_back(PlanetSphere(TITLE_PLANET_KEY, Elite::TitlePlanetSphere(_snapshot), look, true));
  }
  return scene;
}

void SpaceView::TakeStrokes(const Elite::FlightSnapshot& _before, const Elite::FlightSnapshot& _after, float _t,
                            const std::array<std::uint32_t, 4>& _palette)
{
  // The dust a frame drew, where it was, moved toward where the next frame drew the same particle, unless it was born again there.
  m_dust.clear();
  const std::uint32_t dustColor = _palette.at(_before.dustColor & (PALETTE_COLORS - 1));
  for (std::size_t index = 0; index < _before.dustCount; ++index)
  {
    const Elite::SnapshotDust& dust = _before.dust[index];
    Engine::SceneStroke stroke{.fromX = LayerX(dust.x),
                               .fromY = LayerY(dust.row),
                               .toX = LayerX(dust.x),
                               .toY = LayerY(dust.row),
                               .fromWidth = DUST_WIDTH_ROWS,
                               .toWidth = DUST_WIDTH_ROWS,
                               .color = dustColor,
                               .intensity = 1.0f};
    if (dust.streak)
    {
      stroke.fromX = LayerX(dust.fromX);
      stroke.fromY = LayerY(dust.fromRow);
    }
    else
    {
      const auto later = std::ranges::find(_after.dust.begin(), _after.dust.begin() + static_cast<std::ptrdiff_t>(_after.dustCount),
                                           dust.particle, &Elite::SnapshotDust::particle);
      const bool moved = later != _after.dust.begin() + static_cast<std::ptrdiff_t>(_after.dustCount) && !later->streak &&
                         std::abs(later->x - dust.x) <= MOST_DUST_STEP_PIXELS && std::abs(later->row - dust.row) <= MOST_DUST_STEP_PIXELS;
      if (moved)
      {
        stroke.fromX += (LayerX(later->x) - stroke.fromX) * _t;
        stroke.fromY += (LayerY(later->row) - stroke.fromY) * _t;
        stroke.toX = stroke.fromX;
        stroke.toY = stroke.fromY;
      }
    }
    m_dust.push_back(stroke);
  }
  // The frame's beams, for the update it is shown, from the gun's end, wide, to the target's, narrow.
  m_beams.clear();
  for (std::size_t index = 0; index < _before.beamCount; ++index)
  {
    const Elite::SnapshotBeam& beam = _before.beams[index];
    m_beams.push_back(Engine::SceneStroke{.fromX = LayerX(beam.fromX),
                                          .fromY = LayerY(beam.fromRow),
                                          .toX = LayerX(beam.toX),
                                          .toY = LayerY(beam.toRow),
                                          .fromWidth = BEAM_GUN_ROWS,
                                          .toWidth = BEAM_END_ROWS,
                                          .color = _palette.at(beam.color & (PALETTE_COLORS - 1)),
                                          .intensity = BEAM_INTENSITY});
  }
}

const Engine::SceneFrame* SpaceView::Frame(const Elite::Platform& _platform, const Engine::Rect& _place)
{
  const Elite::FlightSnapshot& latest = _platform.LatestFlightSnapshot();
  if (latest.sequence == 0 || !_platform.SpaceViewShown())
  {
    return nullptr;
  }
  const std::array<std::uint8_t, 4> colors = _platform.GraphicsColors();
  const auto palette = Elite::CgaPalette();
  std::array<std::uint32_t, PALETTE_COLORS> rgb{};
  for (std::size_t color = 0; color < PALETTE_COLORS; ++color)
  {
    rgb.at(color) = Rgb(palette[colors[color] & 0x0F]);
  }
  if (latest.sequence != m_afterSequence)
  {
    m_before = SceneOf(_platform.PreviousFlightSnapshot(), rgb);
    m_after = SceneOf(latest, rgb);
    m_afterSequence = latest.sequence;
  }
  const float blend = Engine::BlendFactor(m_before.time, m_after.time, _platform.Clock());
  m_drawn = Engine::Interpolate(m_before, m_after, blend);
  // The strokes of the frame before, unless the latest starts a flight or a view, which continues nothing before it.
  TakeStrokes(latest.cameraContinues ? _platform.PreviousFlightSnapshot() : latest, latest, blend, rgb);

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

  Engine::SceneInset& inset = m_frame.inset;
  inset.palette = rgb;
  // The view's 256 x 126 pixels stretched over _place, and the projection with them.
  const float across = _place.width / static_cast<float>(Elite::SPACE_VIEW_WIDTH_PIXELS);
  const float down = _place.height / static_cast<float>(Elite::SPACE_VIEW_HEIGHT_PIXELS);
  inset.left = _place.left;
  inset.top = _place.top;
  inset.width = _place.width;
  inset.height = _place.height;
  inset.focalX = FOCAL * across;
  inset.focalY = FOCAL * down;
  inset.centerX = AXIS_PIXEL * across;
  inset.centerY = AXIS_ROW * down;
  inset.nearUnits = NEAR_UNITS;
  inset.towardsLight = towardsLight;
  inset.layerWidth = Elite::SPACE_VIEW_WIDTH_PIXELS;
  inset.layerHeight = Elite::SPACE_VIEW_HEIGHT_PIXELS;
  inset.under = _platform.SpaceView().Under();
  inset.over = _platform.SpaceView().Over();
  inset.dust = m_dust;
  inset.beams = m_beams;
  const Elite::SkyLook sky = Elite::SkyLookOf(latest.galaxy, latest.system);
  inset.sky = Engine::SceneSky{.shown = true,
                               .seed = sky.seed,
                               .nebula = sky.nebula,
                               .nebulaDensity = static_cast<float>(sky.nebulaDensity) / SHARE,
                               .starDensity = static_cast<float>(sky.starDensity) / SHARE};
  inset.bloom = BLOOM_SHARE;
  m_frame.scene = &m_drawn;
  return &m_frame;
}

} // namespace Outpost
