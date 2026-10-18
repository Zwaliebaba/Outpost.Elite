#include "pch.h"

#include "SpaceView.h"

#include "Display.h"
#include "FlightSnapshot.h"
#include "PlanetLook.h"
#include "Host.h"
#include "SpaceViewLayers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
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
constexpr std::uint32_t INK_ALPHA = 0xFF000000; // the words' ink on the canvas, opaque
// The panel each block of the words stands on (D68), in the cockpit's colours (Cockpit.cpp): its console's, nearly opaque, edged in
// its gold. Lines with no more than a blank line between them, two lines' rows, are one block, and its panel stands out from its
// ink by a margin, in view pixels and rows.
constexpr std::uint32_t WORDS_PANEL = 0xE6101418;
constexpr std::uint32_t WORDS_EDGE = 0xFFB58A36;
constexpr float WORDS_EDGE_WIDTH = 1.5f;
constexpr std::size_t WORDS_LINE_GAP_ROWS = 16;
constexpr float WORDS_MARGIN_PIXELS = 3.0f;
constexpr float WORDS_MARGIN_ROWS = 2.0f;

// The spheres (ADR-035): the title's planet's key, past every slot's, and how much of a share of 255 is.
constexpr std::uint32_t TITLE_PLANET_KEY = 0x100;
constexpr float SHARE = 255.0f;
constexpr float GAS_GIANT_LEVEL = 0.5f; // a gas giant has no sea
// The strokes, in the view's rows: a mote of dust, and a beam at the gun and at its end; and the beam's light, which blooms.
constexpr float DUST_WIDTH_ROWS = 0.45f;
constexpr float BEAM_GUN_ROWS = 1.8f;
constexpr float BEAM_END_ROWS = 0.5f;
constexpr float BEAM_INTENSITY = 2.5f;
// The stardust (D67), the renderer's own, fixed in the scene's axes in a cube round the camera that each mote wraps round, 2,048
// units from its middle to a face. The original's dust moves out from the view's middle by 1/32 of its offset a frame at speed 40, as a
// mote 32 frames off would, which at 40 units a frame is 1,280 units; the motes in the original's field lie 1,470 deep on average. Of
// 1,650, about 30 are in the original's field at once, as the original keeps 30. Each is drawn from where it is to where it was an update
// before, a dot at most speeds and a streak on the jump drive, and fades over the furthest 30% of the cube; one nearer than 64 units is
// not drawn, to keep a mote passing the camera from sweeping across the view.
constexpr float STARDUST_REACH = 2048.0f;
constexpr std::size_t STARDUST_MOTES = 1650;
constexpr float STARDUST_FADE = 0.3f;
constexpr float STARDUST_NEAREST = 64.0f;
constexpr std::uint32_t STARDUST_SEED = 0x2545F491u;
constexpr float MOST_STARDUST_UPDATES = 4.0f;
// A far object's dot beside the view (DrawObjectAsDot, D69): three pixels across, a row down, in colour 3.
constexpr float DOT_HALF_PIXELS = 1.0f;
constexpr float DOT_ROWS = 1.0f;
constexpr std::size_t DOT_COLOR = 3;
// A pixel's middle, and the drawing buffer's row over the view's: row 1 of the buffer is the view's first.
constexpr float MIDDLE = 0.5f;
constexpr float FIRST_VIEW_ROW = 1.0f;
// How much of the scene's light past 1 blooms.
constexpr float BLOOM_SHARE = 1.0f;
// The tunnels (ADR-069), drawn as strokes from the snapshot's shapes, which bloom. The station's: each rectangle's edges, wider the
// nearer it is, and rails along the corridor's corners from each rectangle to the next and on past the last to the window's edge,
// RAIL_REACH times as far out, fainter. The hyperspace's: each ring a loop of RING_SEGMENTS strokes, wider as it grows.
constexpr float TUNNEL_EDGE_ROWS = 0.6f;
constexpr float TUNNEL_EDGE_ROWS_PER_PIXEL = 1.0f / 60.0f;
constexpr float TUNNEL_EDGE_INTENSITY = 1.6f;
constexpr float TUNNEL_RAIL_ROWS = 0.5f;
constexpr float TUNNEL_RAIL_INTENSITY = 0.8f;
constexpr float RAIL_REACH = 3.0f;
constexpr std::size_t RING_SEGMENTS = 48;
constexpr float RING_ROWS = 0.8f;
constexpr float RING_ROWS_PER_PIXEL = 1.0f / 50.0f;
constexpr float RING_INTENSITY = 2.2f;

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

[[nodiscard]] Elite::Orientation OrientationOf(const Engine::Quaternion& _quaternion) noexcept
{
  return Elite::Orientation{.x = _quaternion.x, .y = _quaternion.y, .z = _quaternion.z, .w = _quaternion.w};
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

// The screen's four colours in its graphics mode, 0x00RRGGBB.
[[nodiscard]] std::array<std::uint32_t, PALETTE_COLORS> PaletteOf(const Elite::Host& _platform) noexcept
{
  const std::array<std::uint8_t, 4> colors = _platform.GraphicsColors();
  const auto palette = Elite::DisplayPalette();
  std::array<std::uint32_t, PALETTE_COLORS> rgb{};
  for (std::size_t color = 0; color < PALETTE_COLORS; ++color)
  {
    rgb.at(color) = Rgb(palette[colors[color] & 0x0F]);
  }
  return rgb;
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
    // What the game drew as a model, and beside its view's cone what it would (D69).
    const bool drawn = object.model || (!object.inView && object.drawing == Elite::SnapshotDrawing::Model);
    if (!drawn || object.kind >= m_models.size())
    {
      continue;
    }
    // Where the frame drew it; one it did not draw, by its own coordinates (D69).
    const std::array<float, 3> position = object.model ? Elite::ScenePosition(_snapshot, object) : Elite::WorldPosition(object);
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

void SpaceView::TakeSideDots(const Elite::FlightSnapshot& _snapshot, const std::array<std::uint32_t, 4>& _palette)
{
  // DrawObjectAsDot: a disc of radius 2 in colour 3 at its projection, one row of three pixels.
  const Elite::Orientation camera = Elite::CameraRotation(_snapshot);
  for (std::size_t index = 0; index < _snapshot.objectCount; ++index)
  {
    const Elite::SnapshotObject& object = _snapshot.objects[index];
    if (object.inView || object.drawing != Elite::SnapshotDrawing::Dot)
    {
      continue;
    }
    const std::array<float, 3> position = Elite::WorldPosition(object);
    const Engine::Vector3 seen = Rotate(camera, Engine::Vector3{.x = position[0], .y = position[1], .z = position[2]});
    if (seen.z <= NEAR_UNITS)
    {
      continue;
    }
    const float x = Elite::VIEW_AXIS_X + Elite::VIEW_FOCAL * seen.x / seen.z;
    const float y = Elite::VIEW_AXIS_ROW - FIRST_VIEW_ROW + Elite::VIEW_FOCAL * seen.y / seen.z;
    m_dust.push_back(Engine::SceneStroke{.fromX = x - DOT_HALF_PIXELS,
                                         .fromY = y,
                                         .toX = x + DOT_HALF_PIXELS,
                                         .toY = y,
                                         .fromWidth = DOT_ROWS,
                                         .toWidth = DOT_ROWS,
                                         .color = _palette.at(DOT_COLOR),
                                         .intensity = 1.0f});
  }
}

void SpaceView::MoveStardust(const Elite::Host& _platform, const std::array<std::uint32_t, 4>& _palette)
{
  m_dust.clear();
  const Elite::FlightSnapshot& latest = _platform.LatestFlightSnapshot();
  const Elite::FlightSnapshot& previous = _platform.PreviousFlightSnapshot();
  const std::uint64_t now = _platform.Clock();
  if (!latest.stardust)
  {
    m_stardustClock = now;
    return;
  }
  if (m_stardust.empty())
  {
    // Spread through the cube by a fixed xorshift, the same every run.
    std::uint32_t random = STARDUST_SEED;
    const auto next = [&random]
    {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      return (static_cast<float>(random) / 4294967296.0f * 2.0f - 1.0f) * STARDUST_REACH;
    };
    m_stardust.resize(STARDUST_MOTES);
    for (Engine::Vector3& mote : m_stardust)
    {
      mote = Engine::Vector3{.x = next(), .y = next(), .z = next()};
    }
  }
  // The world moves back by the velocity's length once an update, which the latest two snapshots time, and the dust with it, along
  // the player's heading, as the original's dust streams from the view's middle whatever the velocity's way: arriving from a jump leaves
  // the velocity stale until the player steers, and the docking computer flies its approach. At most a few updates at once, so a
  // stall does not sweep the dust away.
  const float speed = std::sqrt(static_cast<float>(latest.velocityX) * static_cast<float>(latest.velocityX) +
                                static_cast<float>(latest.velocityY) * static_cast<float>(latest.velocityY) +
                                static_cast<float>(latest.velocityZ) * static_cast<float>(latest.velocityZ));
  const Engine::Vector3 heading =
    Unit(static_cast<float>(latest.headingX), static_cast<float>(latest.headingY), static_cast<float>(latest.headingZ));
  const Engine::Vector3 velocity{.x = heading.x * speed, .y = heading.y * speed, .z = heading.z * speed};
  float updates = 0.0f;
  if (previous.sequence != 0 && latest.pacedTime > previous.pacedTime && m_stardustClock != 0 && now > m_stardustClock)
  {
    updates = std::min(static_cast<float>(now - m_stardustClock) / static_cast<float>(latest.pacedTime - previous.pacedTime),
                       MOST_STARDUST_UPDATES);
  }
  m_stardustClock = now;
  const auto wrap = [](float _coordinate)
  {
    const float span = 2.0f * STARDUST_REACH;
    return _coordinate - span * std::floor((_coordinate + STARDUST_REACH) / span);
  };
  for (Engine::Vector3& mote : m_stardust)
  {
    mote = Engine::Vector3{
      .x = wrap(mote.x - velocity.x * updates), .y = wrap(mote.y - velocity.y * updates), .z = wrap(mote.z - velocity.z * updates)};
  }
  // Each seen as the camera is drawn now, from where it was an update before, in the layers' pixels.
  const Elite::Orientation camera = OrientationOf(m_drawn.cameraOrientation);
  const std::uint32_t color = _palette.at(latest.dustColor & (PALETTE_COLORS - 1));
  for (const Engine::Vector3& mote : m_stardust)
  {
    const Engine::Vector3 seen = Rotate(camera, mote);
    if (seen.z < STARDUST_NEAREST)
    {
      continue;
    }
    const float distance = std::sqrt(seen.x * seen.x + seen.y * seen.y + seen.z * seen.z);
    const float fade = std::clamp((STARDUST_REACH - distance) / (STARDUST_FADE * STARDUST_REACH), 0.0f, 1.0f);
    if (fade <= 0.0f)
    {
      continue;
    }
    const Engine::Vector3 before =
      Rotate(camera, Engine::Vector3{.x = mote.x + velocity.x, .y = mote.y + velocity.y, .z = mote.z + velocity.z});
    const float x = AXIS_PIXEL + FOCAL * seen.x / seen.z;
    const float y = AXIS_ROW + FOCAL * seen.y / seen.z;
    const bool trails = before.z >= STARDUST_NEAREST;
    m_dust.push_back(Engine::SceneStroke{.fromX = trails ? AXIS_PIXEL + FOCAL * before.x / before.z : x,
                                         .fromY = trails ? AXIS_ROW + FOCAL * before.y / before.z : y,
                                         .toX = x,
                                         .toY = y,
                                         .fromWidth = DUST_WIDTH_ROWS,
                                         .toWidth = DUST_WIDTH_ROWS,
                                         .color = color,
                                         .intensity = fade});
  }
}

void SpaceView::TakeBeams(const Elite::FlightSnapshot& _before, const std::array<std::uint32_t, 4>& _palette)
{
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

void SpaceView::TakeTunnel(const Elite::FlightSnapshot& _latest, const std::array<std::uint32_t, 4>& _palette)
{
  // Tunnels continue nothing: each frame's shapes, about the tunnel's centre in the layers' pixels.
  m_beams.clear();
  const Elite::SnapshotTunnel& tunnel = _latest.tunnel;
  const float centerX = LayerX(tunnel.centerX);
  const float centerY = LayerY(tunnel.centerRow);
  const auto stroke = [this](float _fromX, float _fromY, float _toX, float _toY, float _rows, std::uint32_t _color, float _intensity)
  {
    m_beams.push_back(Engine::SceneStroke{.fromX = _fromX,
                                          .fromY = _fromY,
                                          .toX = _toX,
                                          .toY = _toY,
                                          .fromWidth = _rows,
                                          .toWidth = _rows,
                                          .color = _color,
                                          .intensity = _intensity});
  };
  for (std::size_t index = 0; index < tunnel.shapeCount; ++index)
  {
    const Elite::SnapshotTunnelShape& shape = tunnel.shapes[index];
    const float across = static_cast<float>(shape.halfWidth);
    const float down = static_cast<float>(shape.halfHeight);
    const std::uint32_t color = _palette.at(shape.color & (PALETTE_COLORS - 1));
    if (tunnel.kind == Elite::TunnelKind::Hyperspace)
    {
      const float rows = RING_ROWS + across * RING_ROWS_PER_PIXEL;
      for (std::size_t segment = 0; segment < RING_SEGMENTS; ++segment)
      {
        const float from = std::numbers::pi_v<float> * 2.0f * static_cast<float>(segment) / static_cast<float>(RING_SEGMENTS);
        const float to = std::numbers::pi_v<float> * 2.0f * static_cast<float>(segment + 1) / static_cast<float>(RING_SEGMENTS);
        stroke(centerX + across * std::cos(from), centerY + down * std::sin(from), centerX + across * std::cos(to),
               centerY + down * std::sin(to), rows, color, RING_INTENSITY);
      }
      continue;
    }
    // The rectangle's corners, clockwise from its top left, and its edges between them.
    const std::array<float, 4> cornerX = {centerX - across, centerX + across, centerX + across, centerX - across};
    const std::array<float, 4> cornerY = {centerY - down, centerY - down, centerY + down, centerY + down};
    const float rows = TUNNEL_EDGE_ROWS + across * TUNNEL_EDGE_ROWS_PER_PIXEL;
    for (std::size_t corner = 0; corner < cornerX.size(); ++corner)
    {
      const std::size_t next = (corner + 1) % cornerX.size();
      stroke(cornerX.at(corner), cornerY.at(corner), cornerX.at(next), cornerY.at(next), rows, color, TUNNEL_EDGE_INTENSITY);
    }
    // The rails, from this rectangle's corners to the next one's, or on past the last.
    const bool last = index + 1 == tunnel.shapeCount;
    const float nextAcross = last ? across * RAIL_REACH : static_cast<float>(tunnel.shapes[index + 1].halfWidth);
    const float nextDown = last ? down * RAIL_REACH : static_cast<float>(tunnel.shapes[index + 1].halfHeight);
    for (std::size_t corner = 0; corner < cornerX.size(); ++corner)
    {
      const float outX = cornerX.at(corner) < centerX ? -1.0f : 1.0f;
      const float outY = cornerY.at(corner) < centerY ? -1.0f : 1.0f;
      stroke(cornerX.at(corner), cornerY.at(corner), centerX + outX * nextAcross, centerY + outY * nextDown, TUNNEL_RAIL_ROWS, color,
             TUNNEL_RAIL_INTENSITY);
    }
  }
}

const Engine::SceneFrame* SpaceView::Frame(const Elite::Host& _platform, const Engine::Rect& _view, const Engine::Rect& _window)
{
  const Elite::FlightSnapshot& latest = _platform.LatestFlightSnapshot();
  if (latest.sequence == 0 || !_platform.SpaceViewShown())
  {
    return nullptr;
  }
  const std::array<std::uint32_t, PALETTE_COLORS> rgb = PaletteOf(_platform);
  if (latest.sequence != m_afterSequence)
  {
    m_before = SceneOf(_platform.PreviousFlightSnapshot(), rgb);
    m_after = SceneOf(latest, rgb);
    m_afterSequence = latest.sequence;
  }
  const float blend = Engine::BlendFactor(m_before.time, m_after.time, _platform.Clock());
  m_drawn = Engine::Interpolate(m_before, m_after, blend);
  // The beams of the frame before, unless the latest starts a flight or a view, which continues nothing before it; or a tunnel's.
  if (latest.tunnel.kind != Elite::TunnelKind::None)
  {
    TakeTunnel(latest, rgb);
  }
  else
  {
    TakeBeams(latest.cameraContinues ? _platform.PreviousFlightSnapshot() : latest, rgb);
  }
  MoveStardust(_platform, rgb);
  TakeSideDots(latest, rgb);

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
  // The view's 256 x 126 pixels stretched over _view, and the projection with them over the whole of _window.
  const float across = _view.width / static_cast<float>(Elite::SPACE_VIEW_WIDTH_PIXELS);
  const float down = _view.height / static_cast<float>(Elite::SPACE_VIEW_HEIGHT_PIXELS);
  inset.left = _window.left;
  inset.top = _window.top;
  inset.width = _window.width;
  inset.height = _window.height;
  inset.focalX = FOCAL * across;
  inset.focalY = FOCAL * down;
  inset.centerX = _view.left - _window.left + AXIS_PIXEL * across;
  inset.centerY = _view.top - _window.top + AXIS_ROW * down;
  inset.layersLeft = _view.left - _window.left;
  inset.layersTop = _view.top - _window.top;
  inset.layersWidth = _view.width;
  inset.layersHeight = _view.height;
  inset.nearUnits = NEAR_UNITS;
  inset.towardsLight = towardsLight;
  inset.layerWidth = Elite::SPACE_VIEW_WIDTH_PIXELS;
  inset.layerHeight = Elite::SPACE_VIEW_HEIGHT_PIXELS;
  inset.under = _platform.SpaceView().Under();
  inset.over = _platform.SpaceView().Over();
  inset.dust = m_dust;
  inset.beams = m_beams;
  const Elite::SkyLook sky = Elite::SkyLookOf(latest.galaxy, latest.system);
  // A tunnel is black round its shapes, and so is the window beside it (ADR-069).
  inset.sky = Engine::SceneSky{.shown = latest.tunnel.kind == Elite::TunnelKind::None,
                               .seed = sky.seed,
                               .nebula = sky.nebula,
                               .nebulaDensity = static_cast<float>(sky.nebulaDensity) / SHARE,
                               .starDensity = static_cast<float>(sky.starDensity) / SHARE};
  inset.bloom = BLOOM_SHARE;
  m_frame.scene = &m_drawn;
  return &m_frame;
}

void SpaceView::DrawWords(const Elite::Host& _platform, const Engine::Rect& _place, Engine::Canvas& _canvas)
{
  if (_platform.LatestFlightSnapshot().sequence == 0 || !_platform.SpaceViewShown())
  {
    return;
  }
  const std::array<std::uint32_t, PALETTE_COLORS> rgb = PaletteOf(_platform);
  const Elite::SpaceViewLayers& layers = _platform.SpaceView();
  const float across = _place.width / static_cast<float>(Elite::SPACE_VIEW_WIDTH_PIXELS);
  const float down = _place.height / static_cast<float>(Elite::SPACE_VIEW_HEIGHT_PIXELS);
  const auto ink = [&layers](std::size_t _pixel)
  {
    const std::uint8_t value = layers.Over()[_pixel];
    return layers.HoldsWords(_pixel) && value != 0 && value < PALETTE_COLORS;
  };

  // The blocks of lines first, each on its panel: the rows and pixels its ink spans.
  struct Block
  {
    std::size_t top = 0;
    std::size_t bottom = 0;
    std::size_t left = Elite::SPACE_VIEW_WIDTH_PIXELS;
    std::size_t right = 0;
  };
  std::vector<Block> blocks;
  for (std::size_t row = 0; row < Elite::SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    for (std::size_t x = 0; x < Elite::SPACE_VIEW_WIDTH_PIXELS; ++x)
    {
      if (!ink(row * Elite::SPACE_VIEW_WIDTH_PIXELS + x))
      {
        continue;
      }
      if (blocks.empty() || row > blocks.back().bottom + WORDS_LINE_GAP_ROWS)
      {
        blocks.push_back(Block{.top = row, .bottom = row});
      }
      Block& block = blocks.back();
      block.bottom = row;
      block.left = std::min(block.left, x);
      block.right = std::max(block.right, x);
    }
  }
  for (const Block& block : blocks)
  {
    const Engine::Rect panel{.left = _place.left + (static_cast<float>(block.left) - WORDS_MARGIN_PIXELS) * across,
                             .top = _place.top + (static_cast<float>(block.top) - WORDS_MARGIN_ROWS) * down,
                             .width = (static_cast<float>(block.right - block.left + 1) + 2.0f * WORDS_MARGIN_PIXELS) * across,
                             .height = (static_cast<float>(block.bottom - block.top + 1) + 2.0f * WORDS_MARGIN_ROWS) * down};
    _canvas.Box(panel, WORDS_PANEL);
    _canvas.BoxEdge(panel, WORDS_EDGE_WIDTH, WORDS_EDGE);
  }

  // Then each run of a row's ink in one colour, as one box; the paper, 0, is the panel's.
  for (std::size_t row = 0; row < Elite::SPACE_VIEW_HEIGHT_PIXELS; ++row)
  {
    std::size_t x = 0;
    while (x < Elite::SPACE_VIEW_WIDTH_PIXELS)
    {
      const std::size_t first = row * Elite::SPACE_VIEW_WIDTH_PIXELS + x;
      if (!ink(first))
      {
        ++x;
        continue;
      }
      const std::uint8_t value = layers.Over()[first];
      std::size_t end = x + 1;
      while (end < Elite::SPACE_VIEW_WIDTH_PIXELS && ink(first + end - x) && layers.Over()[first + end - x] == value)
      {
        ++end;
      }
      _canvas.Box(Engine::Rect{.left = _place.left + static_cast<float>(x) * across,
                               .top = _place.top + static_cast<float>(row) * down,
                               .width = static_cast<float>(end - x) * across,
                               .height = down},
                  INK_ALPHA | rgb.at(value));
      x = end;
    }
  }
}

} // namespace Outpost
