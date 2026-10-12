// GameLogic/FlightSnapshot.h
#pragma once

#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Elite
{

class GameState;

/// A rotation as a unit quaternion, (x, y, z) its axis times the sine of half its angle and w the cosine. Presentation only: the
/// game never reads one back (ADR-032).
struct Orientation
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

/// One object of a flight snapshot: an active slot of shipSlots as the frame drew it.
struct SnapshotObject
{
  std::uint8_t slot = 0; ///< its slot number: 0 the sun, 1 the planet, 2 the station, 3-19 the ships, 20-35 the debris
  std::uint8_t kind = 0; ///< the type, bits 1-5 of the slot's type byte
  std::int32_t x = 0;    ///< the position relative to the player, the slot's 24-bit x, y and z sign-extended
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::uint16_t pitch = 0; ///< as the slot holds them: a ship's heading, 800h per turn; the sun's and the planet's scale shift
  std::uint16_t yaw = 0;   ///< and colour in pitch; the station's collided bit in yaw's bit 0
  std::uint16_t roll = 0;
  std::int16_t viewX = 0; ///< where the frame drew it, in the camera's frame and the view's (drawCenter), for a model only
  std::int16_t viewY = 0;
  std::int16_t viewZ = 0;
  bool continues = false; ///< the same object as in the snapshot before (ContinueFrom)
  bool model = false;     ///< drawn as its blueprint's model this frame, which a renderer draws in its place (SpaceViewLayers)

  friend bool operator==(const SnapshotObject&, const SnapshotObject&) = default;
};

/// What one frame of flight drew (ADR-032, D49): the camera and every active object, taken where RunFlight's frame is finished,
/// so that a renderer can draw between the last two. It is the game's, in the game's units; the space view (Phase 6, piece 3)
/// makes the engine's scene of it. It is taken outside the game's counted state, and changes nothing the digests or turns see.
struct FlightSnapshot
{
  static constexpr std::size_t MOST_OBJECTS = 36; ///< shipSlots' slots

  std::uint64_t sequence = 0;    ///< its number: 1 for the first published, 0 for none
  Cycles pacedTime = 0;          ///< the platform's clock when it was published
  std::uint16_t playerPitch = 0; ///< the player's orientation, 800h per turn
  std::uint16_t playerYaw = 0;
  std::uint16_t playerRoll = 0;
  std::uint16_t viewAngle = 0;  ///< the view: 0 front, 200h left, 400h rear, 600h right
  Orientation player;           ///< the player's angles as the rotation RotatePitchYawRoll makes of them (PlayerRotation)
  bool cameraContinues = false; ///< the camera turned from the snapshot before's, as the player can in one frame (ContinueFrom)
  std::array<SnapshotObject, MOST_OBJECTS> objects{};
  std::size_t objectCount = 0; ///< the active slots, in slot order, at the start of objects
};

/// The farthest any object moves against the player in one frame, on any axis, in the slot's units (ADR-032 measures it). A
/// slot that holds an object of the same kind further from where it was is a new object.
inline constexpr std::int32_t MOST_STEP_PER_FRAME = 0x4000;

/// The furthest the player turns in one frame, in radians, as the angle between one frame's PlayerRotation and the next's
/// (ADR-032 measures it).
inline constexpr float MOST_TURN_PER_FRAME_RADIANS = 0.5f;

/// The rotation RotatePitchYawRoll makes of _pitch, _yaw and _roll, 800h per turn: (y, z) by the pitch, then (x, z) by the yaw,
/// then (x, y) by the roll, each plane turned as RotateBySinCos turns it. It takes a position from the game's axes to the camera's.
[[nodiscard]] Orientation PlayerRotation(std::uint16_t _pitch, std::uint16_t _yaw, std::uint16_t _roll) noexcept;

/// The rotation a ship's angles make of its model (ShipModel.h), 800h per turn: its roll negated in (x, y), then its yaw in (x, z),
/// then its pitch in (y, z), as BuildBoxCornerVertices turns a box's corners before the player's rotation turns them on. The Dodo
/// turns the other way round its roll (ModelRotation, ShipModel.h).
[[nodiscard]] Orientation ObjectRotation(std::uint16_t _pitch, std::uint16_t _yaw, std::uint16_t _roll) noexcept;

/// The rotation RotateToViewDirection and RotateVerticesToView make of the view _viewAngle, 800h per turn: (x, z) by -_viewAngle.
/// It takes a position from the camera's frame, PlayerRotation's, to the view's.
[[nodiscard]] Orientation ViewRotation(std::uint16_t _viewAngle) noexcept;

/// The camera of _snapshot's frame (ADR-033): the player's rotation, then the view's, ViewRotation after PlayerRotation. It takes
/// a direction of the game's axes, in which a slot's position is held, into the view's frame, x right, y down and z ahead.
[[nodiscard]] Orientation CameraRotation(const FlightSnapshot& _snapshot) noexcept;

/// Where _object of _snapshot is in the game's axes, as a scene holds it: its view position, where the frame drew it, turned back
/// by the camera's rotation. A scene's camera, CameraRotation at the origin, sees it where the game drew it.
[[nodiscard]] std::array<float, 3> ScenePosition(const FlightSnapshot& _snapshot, const SnapshotObject& _object) noexcept;

/// The angle between two rotations, 0 to pi radians.
[[nodiscard]] float AngleBetween(const Orientation& _a, const Orientation& _b) noexcept;

/// The snapshot of _state as the frame just drawn left it: the player's angles and view, and each active slot. Its sequence,
/// paced time and continuity are the publisher's (Platform::PublishFlightSnapshot).
[[nodiscard]] FlightSnapshot TakeFlightSnapshot(const GameState& _state);

/// Whether _after is the object _before was, one frame on: the same slot, the same kind, and no further than MOST_STEP_PER_FRAME
/// on any axis. A slot freed and filled again within a frame by an object of the same kind within that distance is taken for the
/// same; ADR-032 records how rarely the corpus does it.
[[nodiscard]] bool SameObject(const SnapshotObject& _before, const SnapshotObject& _after) noexcept;

/// _after's continuity from _before, the snapshot one frame earlier: the camera continues when the view is the same and the
/// player turned no further than MOST_TURN_PER_FRAME_RADIANS, and each object when SameObject finds its slot's object in _before.
void ContinueFrom(const FlightSnapshot& _before, FlightSnapshot& _after) noexcept;

} // namespace Elite
