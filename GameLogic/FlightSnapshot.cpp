#include "pch.h"

#include "FlightSnapshot.h"

#include "GameState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace Elite
{

namespace
{

constexpr std::uint8_t TYPE_BITS = 0x3E; // bits 1-5 of a slot's type byte
constexpr std::int32_t SIGN_24 = 0x800000;
constexpr std::int32_t WRAP_24 = 0x1000000;
constexpr std::uint16_t TURN = 0x800; // a whole turn, in the game's angles

// The 24-bit two's complement number of a coordinate's high byte and low word.
[[nodiscard]] constexpr std::int32_t Coordinate(std::uint8_t _high, std::uint16_t _low) noexcept
{
  const std::int32_t value = (std::int32_t{_high} << 16) | _low;
  return value >= SIGN_24 ? value - WRAP_24 : value;
}

using Matrix = std::array<std::array<double, 3>, 3>; // rows: where each axis goes, by column

// The plane of axes _a and _b turned by _angle, 800h per turn, as RotateBySinCos turns (a, b): a' = a cos - b sin, b' = b cos + a sin.
[[nodiscard]] Matrix PlaneRotation(std::size_t _a, std::size_t _b, std::uint16_t _angle) noexcept
{
  const double radians = static_cast<double>(_angle % TURN) * 2.0 * std::numbers::pi / TURN;
  Matrix matrix{};
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    matrix[axis][axis] = 1.0;
  }
  matrix[_a][_a] = std::cos(radians);
  matrix[_a][_b] = -std::sin(radians);
  matrix[_b][_a] = std::sin(radians);
  matrix[_b][_b] = std::cos(radians);
  return matrix;
}

[[nodiscard]] Matrix Product(const Matrix& _left, const Matrix& _right) noexcept
{
  Matrix product{};
  for (std::size_t row = 0; row < 3; ++row)
  {
    for (std::size_t column = 0; column < 3; ++column)
    {
      for (std::size_t inner = 0; inner < 3; ++inner)
      {
        product[row][column] += _left[row][inner] * _right[inner][column];
      }
    }
  }
  return product;
}

// RotatePitchYawRoll's turn: (y, z) by the pitch, then (x, z) by the yaw, then (x, y) by the roll; the pitch acts first, so it
// is rightmost.
[[nodiscard]] Matrix PlayerMatrix(std::uint16_t _pitch, std::uint16_t _yaw, std::uint16_t _roll) noexcept
{
  constexpr std::size_t X = 0;
  constexpr std::size_t Y = 1;
  constexpr std::size_t Z = 2;
  return Product(PlaneRotation(X, Y, _roll), Product(PlaneRotation(X, Z, _yaw), PlaneRotation(Y, Z, _pitch)));
}

// RotateToViewDirection's turn: (x, z) by -_viewAngle.
[[nodiscard]] Matrix ViewMatrix(std::uint16_t _viewAngle) noexcept
{
  constexpr std::size_t X = 0;
  constexpr std::size_t Z = 2;
  return PlaneRotation(X, Z, static_cast<std::uint16_t>(TURN - _viewAngle % TURN));
}

// The unit quaternion of a rotation matrix, by its largest diagonal term for precision.
[[nodiscard]] Orientation QuaternionOf(const Matrix& _m) noexcept
{
  const double trace = _m[0][0] + _m[1][1] + _m[2][2];
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double w = 0.0;
  if (trace > 0.0)
  {
    const double s = std::sqrt(trace + 1.0) * 2.0;
    w = s / 4.0;
    x = (_m[2][1] - _m[1][2]) / s;
    y = (_m[0][2] - _m[2][0]) / s;
    z = (_m[1][0] - _m[0][1]) / s;
  }
  else if (_m[0][0] > _m[1][1] && _m[0][0] > _m[2][2])
  {
    const double s = std::sqrt(1.0 + _m[0][0] - _m[1][1] - _m[2][2]) * 2.0;
    w = (_m[2][1] - _m[1][2]) / s;
    x = s / 4.0;
    y = (_m[0][1] + _m[1][0]) / s;
    z = (_m[0][2] + _m[2][0]) / s;
  }
  else if (_m[1][1] > _m[2][2])
  {
    const double s = std::sqrt(1.0 + _m[1][1] - _m[0][0] - _m[2][2]) * 2.0;
    w = (_m[0][2] - _m[2][0]) / s;
    x = (_m[0][1] + _m[1][0]) / s;
    y = s / 4.0;
    z = (_m[1][2] + _m[2][1]) / s;
  }
  else
  {
    const double s = std::sqrt(1.0 + _m[2][2] - _m[0][0] - _m[1][1]) * 2.0;
    w = (_m[1][0] - _m[0][1]) / s;
    x = (_m[0][2] + _m[2][0]) / s;
    y = (_m[1][2] + _m[2][1]) / s;
    z = s / 4.0;
  }
  return Orientation{.x = static_cast<float>(x), .y = static_cast<float>(y), .z = static_cast<float>(z), .w = static_cast<float>(w)};
}

} // namespace

FlightSnapshot TakeFlightSnapshot(const GameState& _state)
{
  FlightSnapshot snapshot;
  snapshot.playerPitch = _state.Data().playerShip.playerPitchAngle;
  snapshot.playerYaw = _state.Data().playerShip.playerYawAngle;
  snapshot.playerRoll = _state.Data().playerShip.playerRollAngle;
  snapshot.viewAngle = _state.Data().playerShip.viewAngle;
  snapshot.player = PlayerRotation(snapshot.playerPitch, snapshot.playerYaw, snapshot.playerRoll);
  for (std::uint16_t number = 0; number < Number(SlotIndex::End); ++number)
  {
    const SpaceObject& slot = _state.Slot(SlotAfter(SlotIndex::Sun, number));
    if ((slot.type & SpaceObject::ACTIVE) == 0)
    {
      continue;
    }
    snapshot.objects[snapshot.objectCount] = SnapshotObject{.slot = static_cast<std::uint8_t>(number),
                                                            .kind = static_cast<std::uint8_t>((slot.type & TYPE_BITS) >> 1),
                                                            .x = Coordinate(slot.xHigh, slot.x),
                                                            .y = Coordinate(slot.yHigh, slot.y),
                                                            .z = Coordinate(slot.zHigh, slot.z),
                                                            .pitch = slot.pitch,
                                                            .yaw = slot.yaw,
                                                            .roll = slot.roll,
                                                            .viewX = static_cast<std::int16_t>(slot.viewX),
                                                            .viewY = static_cast<std::int16_t>(slot.viewY),
                                                            .viewZ = static_cast<std::int16_t>(slot.viewZ),
                                                            .continues = false,
                                                            .model = false};
    ++snapshot.objectCount;
  }
  return snapshot;
}

Orientation PlayerRotation(std::uint16_t _pitch, std::uint16_t _yaw, std::uint16_t _roll) noexcept
{
  return QuaternionOf(PlayerMatrix(_pitch, _yaw, _roll));
}

Orientation CameraRotation(const FlightSnapshot& _snapshot) noexcept
{
  return QuaternionOf(
    Product(ViewMatrix(_snapshot.viewAngle), PlayerMatrix(_snapshot.playerPitch, _snapshot.playerYaw, _snapshot.playerRoll)));
}

std::array<float, 3> ScenePosition(const FlightSnapshot& _snapshot, const SnapshotObject& _object) noexcept
{
  // The camera's rotation undone: its matrix transposed.
  const Matrix camera =
    Product(ViewMatrix(_snapshot.viewAngle), PlayerMatrix(_snapshot.playerPitch, _snapshot.playerYaw, _snapshot.playerRoll));
  const std::array<double, 3> view = {static_cast<double>(_object.viewX), static_cast<double>(_object.viewY),
                                      static_cast<double>(_object.viewZ)};
  std::array<float, 3> position{};
  for (std::size_t axis = 0; axis < 3; ++axis)
  {
    position[axis] = static_cast<float>(camera[0][axis] * view[0] + camera[1][axis] * view[1] + camera[2][axis] * view[2]);
  }
  return position;
}

Orientation ObjectRotation(std::uint16_t _pitch, std::uint16_t _yaw, std::uint16_t _roll) noexcept
{
  constexpr std::size_t X = 0;
  constexpr std::size_t Y = 1;
  constexpr std::size_t Z = 2;
  // The roll acts first, negated, so it is rightmost.
  return QuaternionOf(Product(PlaneRotation(Y, Z, _pitch),
                              Product(PlaneRotation(X, Z, _yaw), PlaneRotation(X, Y, static_cast<std::uint16_t>(TURN - _roll % TURN)))));
}

Orientation ViewRotation(std::uint16_t _viewAngle) noexcept
{
  return QuaternionOf(ViewMatrix(_viewAngle));
}

float AngleBetween(const Orientation& _a, const Orientation& _b) noexcept
{
  const float dot = std::abs(_a.x * _b.x + _a.y * _b.y + _a.z * _b.z + _a.w * _b.w);
  return 2.0f * std::acos(std::min(dot, 1.0f));
}

bool SameObject(const SnapshotObject& _before, const SnapshotObject& _after) noexcept
{
  return _before.slot == _after.slot && _before.kind == _after.kind && std::abs(_after.x - _before.x) <= MOST_STEP_PER_FRAME &&
         std::abs(_after.y - _before.y) <= MOST_STEP_PER_FRAME && std::abs(_after.z - _before.z) <= MOST_STEP_PER_FRAME;
}

void ContinueFrom(const FlightSnapshot& _before, FlightSnapshot& _after) noexcept
{
  _after.cameraContinues =
    _before.viewAngle == _after.viewAngle && AngleBetween(_before.player, _after.player) <= MOST_TURN_PER_FRAME_RADIANS;
  // Both lists are in slot order, so one pass pairs them.
  std::size_t earlier = 0;
  for (std::size_t index = 0; index < _after.objectCount; ++index)
  {
    SnapshotObject& object = _after.objects[index];
    while (earlier < _before.objectCount && _before.objects[earlier].slot < object.slot)
    {
      ++earlier;
    }
    object.continues = earlier < _before.objectCount && SameObject(_before.objects[earlier], object);
  }
}

} // namespace Elite
