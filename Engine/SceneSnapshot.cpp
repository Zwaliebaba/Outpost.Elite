#include "pch.h"

#include "SceneSnapshot.h"

#include <algorithm>
#include <cmath>

namespace Engine
{

namespace
{

// Below this sine of the angle between two rotations, Slerp blends them linearly: the arc is too short to tell apart.
constexpr float NEARLY_PARALLEL = 1e-4f;

[[nodiscard]] Vector3 Lerp(const Vector3& _from, const Vector3& _to, float _t) noexcept
{
  return Vector3{.x = _from.x + (_to.x - _from.x) * _t, .y = _from.y + (_to.y - _from.y) * _t, .z = _from.z + (_to.z - _from.z) * _t};
}

[[nodiscard]] Quaternion Normalized(const Quaternion& _q) noexcept
{
  const float length = std::sqrt(_q.x * _q.x + _q.y * _q.y + _q.z * _q.z + _q.w * _q.w);
  return Quaternion{.x = _q.x / length, .y = _q.y / length, .z = _q.z / length, .w = _q.w / length};
}

} // namespace

Quaternion Slerp(const Quaternion& _from, const Quaternion& _to, float _t) noexcept
{
  float dot = _from.x * _to.x + _from.y * _to.y + _from.z * _to.z + _from.w * _to.w;
  // q and -q are one rotation: take the one nearer _from, so that the turn is the short way round.
  Quaternion to = _to;
  if (dot < 0.0f)
  {
    dot = -dot;
    to = Quaternion{.x = -_to.x, .y = -_to.y, .z = -_to.z, .w = -_to.w};
  }
  float fromWeight = 1.0f - _t;
  float toWeight = _t;
  const float angle = std::acos(std::min(dot, 1.0f));
  const float sine = std::sin(angle);
  if (sine > NEARLY_PARALLEL)
  {
    fromWeight = std::sin((1.0f - _t) * angle) / sine;
    toWeight = std::sin(_t * angle) / sine;
  }
  return Normalized(Quaternion{.x = _from.x * fromWeight + to.x * toWeight,
                               .y = _from.y * fromWeight + to.y * toWeight,
                               .z = _from.z * fromWeight + to.z * toWeight,
                               .w = _from.w * fromWeight + to.w * toWeight});
}

float BlendFactor(std::uint64_t _before, std::uint64_t _after, std::uint64_t _now) noexcept
{
  if (_after <= _before || _now <= _after)
  {
    return 0.0f;
  }
  const auto blend = static_cast<float>(static_cast<double>(_now - _after) / static_cast<double>(_after - _before));
  return std::min(blend, 1.0f);
}

SceneSnapshot Interpolate(const SceneSnapshot& _before, const SceneSnapshot& _after, float _t)
{
  SceneSnapshot scene = _after;
  if (_after.cameraContinues)
  {
    scene.cameraPosition = Lerp(_before.cameraPosition, _after.cameraPosition, _t);
    scene.cameraOrientation = Slerp(_before.cameraOrientation, _after.cameraOrientation, _t);
  }
  scene.time = _before.time + static_cast<std::uint64_t>(static_cast<double>(_after.time - _before.time) * _t);
  for (SceneObject& object : scene.objects)
  {
    if (!object.continues)
    {
      continue;
    }
    const auto earlier = std::ranges::find(_before.objects, object.key, &SceneObject::key);
    if (earlier == _before.objects.end())
    {
      continue;
    }
    object.position = Lerp(earlier->position, object.position, _t);
    object.orientation = Slerp(earlier->orientation, object.orientation, _t);
  }
  return scene;
}

} // namespace Engine
