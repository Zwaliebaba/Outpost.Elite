// Engine/SceneSnapshot.h
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Engine
{

struct Vector3
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  friend bool operator==(const Vector3&, const Vector3&) = default;
};

/// A rotation as a unit quaternion: (x, y, z) its axis times the sine of half its angle, w the cosine.
struct Quaternion
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;

  friend bool operator==(const Quaternion&, const Quaternion&) = default;
};

/// One thing in a scene, in the game's units: which it is, what it looks like, where it is and which way it faces.
struct SceneObject
{
  std::uint32_t key = 0;   ///< the same key in two snapshots names the same thing when continues says so
  std::uint32_t model = 0; ///< what it looks like, a number the game gives its models
  Vector3 position;
  Quaternion orientation;
  bool continues = false; ///< the same thing as the snapshot before's of this key, which may be drawn between the two
};

/// What a sphere's surface is, as a scene shades it (ADR-035).
enum class SphereSurface : std::uint8_t
{
  Star,    ///< glowing, white at its middle and colors[0] at its rim, with a corona round it
  Terrain, ///< seas below level and lands above, under cloud: colors[0] the deep sea, [1] the shallows, [2] the low land, [3] the high
  Bands    ///< bands across its latitudes, colors[0] to [3] in turn, stirred by turbulence
};

/// A sphere in a scene (ADR-035): which it is, where it is and how large, in the game's units and frame, and what its surface is.
/// Its surface is fixed in the scene's frame, and lit by the scene's light.
struct SceneSphere
{
  std::uint32_t key = 0; ///< as an object's
  Vector3 position;
  float radius = 0.0f;
  SphereSurface surface = SphereSurface::Star;
  std::array<std::uint32_t, 4> colors{}; ///< 0x00RRGGBB, display-encoded
  std::uint32_t atmosphere = 0;          ///< the haze at its rim, 0x00RRGGBB; 0 for none
  std::uint32_t seed = 0;                ///< which surface of its kind: the noise's offset
  float level = 0.5f;                    ///< Terrain: the share of it under the sea, 0 to 1
  float cover = 0.0f;                    ///< Terrain: the share of it under cloud, 0 to 1
  float bands = 8.0f;                    ///< Bands: how many pairs of bands from pole to pole
  float turbulence = 0.5f;               ///< how far the noise stirs its pattern, 0 to 1
  bool continues = false;                ///< as an object's
};

/// What one update of a game left to draw (ADR-032, D49): the camera and the objects, in the game's units and frame. The engine
/// draws between the last two (Interpolate); it knows nothing of the game that filled them (R9).
struct SceneSnapshot
{
  std::uint64_t time = 0; ///< when the update was made, in the game's ticks
  Vector3 cameraPosition;
  Quaternion cameraOrientation;
  bool cameraContinues = false; ///< the camera moved from the snapshot before's as it can in one update
  std::vector<SceneObject> objects;
  std::vector<SceneSphere> spheres;
};

/// The rotation _t of the way from _from to _to, the short way round: _from at 0, _to at 1.
[[nodiscard]] Quaternion Slerp(const Quaternion& _from, const Quaternion& _to, float _t) noexcept;

/// How far between _before and _after a frame drawn at _now is, 0 to 1, when the frame shows the game one update late: at _after's
/// time it shows _before, and an update later _after. _before's time is not after _after's.
[[nodiscard]] float BlendFactor(std::uint64_t _before, std::uint64_t _after, std::uint64_t _now) noexcept;

/// The scene _t of the way from _before to _after. A continuing camera and each continuing object or sphere whose key _before
/// also holds are interpolated, positions linearly and orientations by Slerp; everything else is as _after has it, and what only
/// _before holds is gone.
[[nodiscard]] SceneSnapshot Interpolate(const SceneSnapshot& _before, const SceneSnapshot& _after, float _t);

} // namespace Engine
