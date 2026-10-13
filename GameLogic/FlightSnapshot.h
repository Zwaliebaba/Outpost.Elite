// GameLogic/FlightSnapshot.h
#pragma once

#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

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

/// A blip on the scanner (UpdateScannerBlip): its slot, and its x, its height y and its depth z as the slot keeps them, the
/// camera's frame's high bytes, y and z times 1.25. XorScannerBlip draws it (3Dh + x, 1Fh - z / 4) from the scanner's corner,
/// with a stick of y / 4 pixels, down for a positive y.
struct SnapshotBlip
{
  std::uint8_t slot = 0;
  std::int8_t x = 0;
  std::int8_t y = 0;
  std::int8_t z = 0;
  bool hostile = false; ///< the slot's +1Eh bit 0

  friend bool operator==(const SnapshotBlip&, const SnapshotBlip&) = default;
};

/// What the frame's dashboard showed (ADR-034, D51): the values UpdateDashboard draws as it left them, the scanner's blips, the
/// compass's dot and the message line, in the game's units.
struct SnapshotDashboard
{
  static constexpr std::size_t MESSAGE_BYTES = 48; ///< the message line's text, NUL-terminated

  std::uint8_t foreShield = 0; ///< 0-FFh
  std::uint8_t aftShield = 0;
  std::uint8_t fuel = 0; ///< FFh full; light years are fuel / 36
  std::uint8_t cabinTemperature = 0;
  std::uint8_t laserTemperature = 0;
  std::uint8_t altitude = 0;
  std::uint8_t speed = 0;        ///< 0-30h
  std::int8_t roll = 0;          ///< rollRate's low byte, which the indicator holds to -23..23
  std::int8_t pitch = 0;         ///< rollRate's high byte, likewise
  std::uint16_t energy = 0;      ///< 0-3FFh, four banks of FFh
  std::uint8_t missiles = 0;     ///< 0-4
  std::uint8_t missileState = 0; ///< 0 idle, 1 armed and looking, 2 locked
  std::uint8_t condition = 0;    ///< conditionColor: 0 flashing, 1 green, 2 red, 3 yellow
  bool safeZone = false;         ///< the S shown
  bool ecm = false;              ///< the E shown, for the frame the ECM fired
  bool compass = false;          ///< the compass dot shown
  std::int8_t compassX = 0;      ///< the dot, -7..7 steps right of the compass's centre
  std::int8_t compassY = 0;      ///< and below it
  bool compassInFront = false;   ///< its target ahead: the dot solid, else a ring
  std::array<SnapshotBlip, 36> blips{};
  std::size_t blipCount = 0;
  std::array<char, MESSAGE_BYTES> message{}; ///< what the message line shows, the view's name when no message is up
  bool live = true; ///< the values drawn; false on the title, whose dashboard is the cockpit's picture, never updated (D64)
};

/// A line the game drew across the view in a frame of flight, a laser's beam (DrawLaserBeams, ApplyEnemyLaserHit): from
/// (fromX, fromRow) to (toX, toRow) of the drawing buffer's 256 x 128 pixels, whose row 1 is the view's first, in pixel value
/// color, 0-3.
struct SnapshotBeam
{
  std::int16_t fromX = 0;
  std::int16_t fromRow = 0;
  std::int16_t toX = 0;
  std::int16_t toRow = 0;
  std::uint8_t color = 0;

  friend bool operator==(const SnapshotBeam&, const SnapshotBeam&) = default;
};

/// A stardust particle as UpdateStardust drew it (DrawnStardust): a point at (x, row) of the drawing buffer, or, while the jump
/// drive streaks it, a line to there from (fromX, fromRow).
struct SnapshotDust
{
  std::uint8_t particle = 0; ///< its index of stardust
  std::int16_t x = 0;
  std::int16_t row = 0;
  bool streak = false;
  std::int16_t fromX = 0;
  std::int16_t fromRow = 0;

  friend bool operator==(const SnapshotDust&, const SnapshotDust&) = default;
};

/// A disc the game drew in the view as DrawDisc draws one: centred on (x, row) of the drawing buffer, rows of it each side.
struct SnapshotDisc
{
  bool shown = false;
  std::int16_t x = 0;
  std::int16_t row = 0;
  std::uint16_t rows = 0;

  friend bool operator==(const SnapshotDisc&, const SnapshotDisc&) = default;
};

/// What one frame of flight drew (ADR-032, D49): the camera and every active object, taken where RunFlight's frame is finished,
/// so that a renderer can draw between the last two. It is the game's, in the game's units; the space view (Phase 6, piece 3)
/// makes the engine's scene of it. It is taken outside the game's counted state, and changes nothing the digests or turns see.
struct FlightSnapshot
{
  static constexpr std::size_t MOST_OBJECTS = 36; ///< shipSlots' slots
  static constexpr std::size_t DUST_PARTICLES = 30;
  static constexpr std::size_t MOST_BEAMS = 8; ///< four of the player's and the enemy's one, a frame, with room
  static constexpr std::uint8_t NO_SIGHT = 0xFF;

  std::uint64_t sequence = 0;    ///< its number: 1 for the first published, 0 for none
  Cycles pacedTime = 0;          ///< the platform's clock when it was published
  std::uint16_t playerPitch = 0; ///< the player's orientation, 800h per turn
  std::uint16_t playerYaw = 0;
  std::uint16_t playerRoll = 0;
  std::uint16_t viewAngle = 0;  ///< the view: 0 front, 200h left, 400h rear, 600h right
  Orientation player;           ///< the player's angles as the rotation RotatePitchYawRoll makes of them (PlayerRotation)
  bool cameraContinues = false; ///< the camera turned from the snapshot before's, as the player can in one frame (ContinueFrom)
  bool title = false;           ///< a frame of the title's turning ship (RunTitle), not of flight
  std::array<SnapshotObject, MOST_OBJECTS> objects{};
  std::size_t objectCount = 0; ///< the active slots, in slot order, at the start of objects
  SnapshotDashboard dashboard;
  // What the renderer draws in place of the game's pixels besides the models (ADR-035, D65).
  std::array<SnapshotDust, DUST_PARTICLES> dust{};
  std::size_t dustCount = 0;
  std::uint8_t dustColor = 0; ///< the pixel value the stardust is drawn in
  std::array<SnapshotBeam, MOST_BEAMS> beams{};
  std::size_t beamCount = 0;      ///< the beams the frame drew, which the platform hands it (Platform::PublishFlightSnapshot)
  std::uint8_t sight = NO_SIGHT;  ///< the view's laser type, 0-3, whose sights FinishSpaceViewFrame draws, or NO_SIGHT for none
  std::uint8_t supernovaHeat = 0; ///< while a supernova burns (SupernovaHeat), the sun's radius in rows; else 0
  std::uint8_t galaxy = 0;        ///< the system the planet is: galaxyNumber
  std::uint8_t system = 0;        ///< and currentSystemIndex
  SnapshotDisc titlePlanet;       ///< the planet the title draws behind its ship (DrawTitlePlanet)
};

/// A sun or a planet as the renderer draws it (ADR-035, D65): a sphere, its centre in the game's axes as ScenePosition gives a
/// model's, and its radius, both in the game's units, the slot's position unscaled.
struct SnapshotSphere
{
  std::array<float, 3> position{};
  float radius = 0.0f;
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

/// Whether _object is the sun, or the planet (TYPE_SUN, TYPE_PLANET).
[[nodiscard]] bool IsSun(const SnapshotObject& _object) noexcept;
[[nodiscard]] bool IsPlanet(const SnapshotObject& _object) noexcept;

/// The sun or the planet _object of _snapshot as a sphere (D65): where the frame put it, its view position times its scale
/// shift's power of 2 (ClassifySunOrPlanet), turned back by the camera; and the radius whose disc SizePlanet and SizeSun size,
/// 50 x 256 and 100 x 256 of the game's units, or, while a supernova burns, the heat's rows at its distance.
[[nodiscard]] SnapshotSphere SphereOf(const FlightSnapshot& _snapshot, const SnapshotObject& _object) noexcept;

/// The title's planet of _snapshot as a sphere (D65), where its disc is in the view, at a depth of TITLE_PLANET_DEPTH.
[[nodiscard]] SnapshotSphere TitlePlanetSphere(const FlightSnapshot& _snapshot) noexcept;

/// The depth the title's planet is put at: any would do, as the title's camera does not move.
inline constexpr float TITLE_PLANET_DEPTH = 65536.0f;

/// Where the space view's projection puts the axis (ProjectVertices): pixel 128 of the drawing buffer and its row 64, each at
/// the middle of its pixel, and 256 of them a unit of x / z and of y / z.
inline constexpr float VIEW_AXIS_X = 128.5f;
inline constexpr float VIEW_AXIS_ROW = 64.5f;
inline constexpr float VIEW_FOCAL = 256.0f;

/// The angle between two rotations, 0 to pi radians.
[[nodiscard]] float AngleBetween(const Orientation& _a, const Orientation& _b) noexcept;

/// The snapshot of _state as the frame just drawn left it: the player's angles and view, and each active slot. Its sequence,
/// paced time and continuity are the publisher's (Platform::PublishFlightSnapshot).
[[nodiscard]] FlightSnapshot TakeFlightSnapshot(const GameState& _state);

/// The snapshot of the title's frame (RunTitle, D64, D65): TakeFlightSnapshot's, marked the title's, with _message on its
/// dashboard (TitleDashboard), _planet the disc it draws behind its ship, and no stardust, which the title does not draw.
[[nodiscard]] FlightSnapshot TakeTitleSnapshot(const GameState& _state, std::string_view _message, const SnapshotDisc& _planet);

/// _snapshot of a frame of the view that holds only the game's words, the pause screen's or the credits' (D66): no object, no
/// stardust, no sights and no title's planet, which neither draws over the cleared view; the dashboard and the system stay.
[[nodiscard]] FlightSnapshot WordsOnly(const FlightSnapshot& _snapshot) noexcept;

/// The title's dashboard (RunTitle, D64): the cockpit's picture as ShowCockpitScreen copies it, which UpdateDashboard never draws
/// over on the title, so nothing on it is live; and _message, the title's ---- E L I T E ----, on the message line.
[[nodiscard]] SnapshotDashboard TitleDashboard(std::string_view _message);

/// Whether _after is the object _before was, one frame on: the same slot, the same kind, and no further than MOST_STEP_PER_FRAME
/// on any axis. A slot freed and filled again within a frame by an object of the same kind within that distance is taken for the
/// same; ADR-032 records how rarely the corpus does it.
[[nodiscard]] bool SameObject(const SnapshotObject& _before, const SnapshotObject& _after) noexcept;

/// _after's continuity from _before, the snapshot one frame earlier: the camera continues when the view is the same and the
/// player turned no further than MOST_TURN_PER_FRAME_RADIANS, and each object when SameObject finds its slot's object in _before.
void ContinueFrom(const FlightSnapshot& _before, FlightSnapshot& _after) noexcept;

} // namespace Elite
