// Engine/SceneSnapshot.h
#pragma once

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

/// What one update of a game left to draw (ADR-032, D49): the camera and the objects, in the game's units and frame. The engine
/// draws between the last two (Interpolate); it knows nothing of the game that filled them (R9).
struct SceneSnapshot
{
  std::uint64_t time = 0; ///< when the update was made, in the game's ticks
  Vector3 cameraPosition;
  Quaternion cameraOrientation;
  bool cameraContinues = false; ///< the camera moved from the snapshot before's as it can in one update
  std::vector<SceneObject> objects;
};

/// The rotation _t of the way from _from to _to, the short way round: _from at 0, _to at 1.
[[nodiscard]] Quaternion Slerp(const Quaternion& _from, const Quaternion& _to, float _t) noexcept;

/// How far between _before and _after a frame drawn at _now is, 0 to 1, when the frame shows the game one update late: at _after's
/// time it shows _before, and an update later _after. _before's time is not after _after's.
[[nodiscard]] float BlendFactor(std::uint64_t _before, std::uint64_t _after, std::uint64_t _now) noexcept;

/// The scene _t of the way from _before to _after. A continuing camera and each continuing object whose key _before also holds
/// are interpolated, positions linearly and orientations by Slerp; everything else is as _after has it, and what only _before
/// holds is gone.
[[nodiscard]] SceneSnapshot Interpolate(const SceneSnapshot& _before, const SceneSnapshot& _after, float _t);

} // namespace Engine
