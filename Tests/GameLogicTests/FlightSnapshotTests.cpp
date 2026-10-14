#include "pch.h"

#include "Flight.h"
#include "FlightSnapshot.h"
#include "GameState.h"
#include "Maths.h"
#include "HostRig.h"
#include "Ships.h"
#include "SpaceObject.h"

#include <cmath>
#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// A vector of this length on each axis in turn, long enough that RotatePitchYawRoll's rounding is far below the tolerance.
constexpr std::int16_t PROBE_LENGTH = 0x2000;

// RotatePitchYawRoll's result within this fraction of the probe's length of the rotation's: its sine table and its rounded products
// are not exact.
constexpr double MOST_RELATIVE_ERROR = 0.002;

// _vector turned by _rotation, the quaternion's sandwich product.
[[nodiscard]] std::array<double, 3> Rotate(const Elite::Orientation& _rotation, const std::array<double, 3>& _vector)
{
  const double x = _rotation.x;
  const double y = _rotation.y;
  const double z = _rotation.z;
  const double w = _rotation.w;
  // t = 2 (q.xyz x v); v' = v + w t + q.xyz x t.
  const std::array<double, 3> t = {2.0 * (y * _vector[2] - z * _vector[1]), 2.0 * (z * _vector[0] - x * _vector[2]),
                                   2.0 * (x * _vector[1] - y * _vector[0])};
  return {_vector[0] + w * t[0] + (y * t[2] - z * t[1]), _vector[1] + w * t[1] + (z * t[0] - x * t[2]),
          _vector[2] + w * t[2] + (x * t[1] - y * t[0])};
}

// A slot of _state made active as _kind at (_x, _y, _z), 24 bits each.
void Place(Elite::GameState& _state, std::uint16_t _slot, std::uint8_t _kind, std::int32_t _x, std::int32_t _y, std::int32_t _z)
{
  Elite::SpaceObject& object = _state.Slot(Elite::SlotAfter(Elite::SlotIndex::Sun, _slot));
  object.type = static_cast<std::uint8_t>((_kind << 1) | Elite::SpaceObject::ACTIVE);
  const auto high = [](std::int32_t _value) { return static_cast<std::uint8_t>((static_cast<std::uint32_t>(_value) >> 16) & 0xFF); };
  const auto low = [](std::int32_t _value) { return static_cast<std::uint16_t>(static_cast<std::uint32_t>(_value) & 0xFFFF); };
  object.xHigh = high(_x);
  object.x = low(_x);
  object.yHigh = high(_y);
  object.y = low(_y);
  object.zHigh = high(_z);
  object.z = low(_z);
}

} // namespace

// The flight snapshot (ADR-032): what it takes of a frame, how it tells one object from the next, and the player's rotation.
TEST_CLASS(FlightSnapshotTests)
{
public:
  // PlayerRotation turns a vector as RotatePitchYawRoll does, at angles round the whole turn on each axis and mixed.
  TEST_METHOD(PlayerRotationIsTheGamesRotation)
  {
    const auto state = std::make_unique<Elite::GameState>();
    for (std::uint16_t pitch = 0; pitch < 0x800; pitch += 0x0B3)
    {
      for (std::uint16_t yaw = 0x20; yaw < 0x800; yaw += 0x0E5)
      {
        for (std::uint16_t roll = 0x40; roll < 0x800; roll += 0x127)
        {
          (void)Elite::SetSinCos(*state, 0, pitch);
          (void)Elite::SetSinCos(*state, 1, yaw);
          (void)Elite::SetSinCos(*state, 2, roll);
          const Elite::Orientation rotation = Elite::PlayerRotation(pitch, yaw, roll);
          for (std::size_t axis = 0; axis < 3; ++axis)
          {
            std::array<double, 3> unit{};
            unit[axis] = PROBE_LENGTH;
            const Elite::Vector game =
              Elite::RotatePitchYawRoll(*state, Elite::Vector{static_cast<std::int16_t>(unit[0]), static_cast<std::int16_t>(unit[1]),
                                                              static_cast<std::int16_t>(unit[2])});
            const std::array<double, 3> turned = Rotate(rotation, unit);
            const double error = std::hypot(turned[0] - game.x, turned[1] - game.y, turned[2] - game.z) / PROBE_LENGTH;
            Assert::IsTrue(error < MOST_RELATIVE_ERROR, (L"pitch " + std::to_wstring(pitch) + L", yaw " + std::to_wstring(yaw) +
                                                         L", roll " + std::to_wstring(roll) + L", axis " + std::to_wstring(axis))
                                                          .c_str());
          }
        }
      }
    }
  }

  // PlayerHeading is the way UpdatePlayerVelocity turns the speed, exactly, at angles round the whole turn and speeds up to the jump
  // drive's (D67): what the stardust follows while the velocity itself is stale, after a jump, until the player steers.
  TEST_METHOD(TheHeadingIsTheWayTheVelocityGoes)
  {
    const auto state = std::make_unique<Elite::GameState>();
    for (std::uint16_t pitch = 0; pitch < 0x800; pitch += 0x05B)
    {
      for (std::uint16_t yaw = 0x11; yaw < 0x800; yaw += 0x067)
      {
        for (const std::uint16_t speed : {std::uint16_t{1}, std::uint16_t{0x14}, std::uint16_t{0x28}, std::uint16_t{0x28 << 5}})
        {
          state->Data().playerShip.playerPitchAngle = pitch;
          state->Data().playerShip.playerYawAngle = yaw;
          state->Data().playerShip.playerSpeed = speed;
          state->Data().playerShip.velocityDirty = 1;
          Elite::UpdatePlayerVelocity(*state);
          const std::array<std::int16_t, 3> heading = Elite::PlayerHeading(pitch, yaw, static_cast<std::int16_t>(speed));
          const auto velocity = [&state](std::uint16_t _word) { return static_cast<std::int16_t>(_word); };
          Assert::IsTrue(
            heading[0] == velocity(state->Data().playerShip.playerVelocityX) &&
              heading[1] == velocity(state->Data().playerShip.playerVelocityY) &&
              heading[2] == velocity(state->Data().playerShip.playerVelocityZ),
            (L"pitch " + std::to_wstring(pitch) + L", yaw " + std::to_wstring(yaw) + L", speed " + std::to_wstring(speed)).c_str());
        }
      }
    }
  }

  // The angle between two rotations: none between one and itself or its negation, and a quarter turn of pitch is a quarter turn.
  // The camera (ADR-033) is the player's rotation and then the view's, which turns (x, z) by minus the view: the left view, 200h,
  // sees ahead of the player on its right. A scene position turned by the camera is the view position the frame drew, at angles
  // round every axis and in every view.
  TEST_METHOD(TheCameraSeesWhereTheFrameDrew)
  {
    constexpr double MOST_UNITS = 0.01;
    const std::array<double, 3> ahead = Rotate(Elite::ViewRotation(0x200), {0.0, 0.0, 1000.0});
    Assert::IsTrue(std::abs(ahead[0] - 1000.0) < MOST_UNITS && std::abs(ahead[2]) < MOST_UNITS, L"the left view sees ahead on its right");
    for (const std::uint16_t view : std::array<std::uint16_t, 4>{0x000, 0x200, 0x400, 0x600})
    {
      for (std::uint16_t angle = 0; angle < 0x800; angle += 0x0D3)
      {
        Elite::FlightSnapshot snapshot;
        snapshot.playerPitch = angle;
        snapshot.playerYaw = static_cast<std::uint16_t>(0x800 - angle);
        snapshot.playerRoll = static_cast<std::uint16_t>(angle * 3);
        snapshot.viewAngle = view;
        const Elite::Orientation camera = Elite::CameraRotation(snapshot);
        const auto expected = [&](const std::array<double, 3>& _v)
        { return Rotate(Elite::ViewRotation(view), Rotate(Elite::PlayerRotation(angle, snapshot.playerYaw, snapshot.playerRoll), _v)); };
        const std::array<double, 3> probe = {300.0, -1200.0, 4000.0};
        const std::array<double, 3> turned = Rotate(camera, probe);
        const std::array<double, 3> composed = expected(probe);
        const Elite::SnapshotObject object{.slot = 3,
                                           .kind = 9,
                                           .x = 0,
                                           .y = 0,
                                           .z = 0,
                                           .pitch = 0,
                                           .yaw = 0,
                                           .roll = 0,
                                           .viewX = -2500,
                                           .viewY = 700,
                                           .viewZ = 9000,
                                           .continues = false,
                                           .model = true};
        const std::array<float, 3> position = Elite::ScenePosition(snapshot, object);
        const std::array<double, 3> seen = Rotate(camera, {position[0], position[1], position[2]});
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
          Assert::IsTrue(std::abs(turned[axis] - composed[axis]) < MOST_UNITS * 10, L"the view after the player");
        }
        Assert::IsTrue(std::abs(seen[0] + 2500.0) < 1.0 && std::abs(seen[1] - 700.0) < 1.0 && std::abs(seen[2] - 9000.0) < 1.0,
                       (L"seen where drawn, view " + std::to_wstring(view) + L", angle " + std::to_wstring(angle)).c_str());
      }
    }
  }

  TEST_METHOD(AngleBetweenIsTheTurnFromOneToTheOther)
  {
    const Elite::Orientation start = Elite::PlayerRotation(0x123, 0x456, 0x789);
    const Elite::Orientation negated{.x = -start.x, .y = -start.y, .z = -start.z, .w = -start.w};
    Assert::IsTrue(Elite::AngleBetween(start, start) < 1e-3f, L"itself");
    Assert::IsTrue(Elite::AngleBetween(start, negated) < 1e-3f, L"its negation, the same rotation");
    const float quarter = Elite::AngleBetween(Elite::PlayerRotation(0, 0, 0), Elite::PlayerRotation(0x200, 0, 0));
    Assert::IsTrue(std::abs(quarter - 1.5707963f) < 1e-3f, L"a quarter turn");
  }

  // The active slots in slot order, each 24-bit coordinate sign-extended, and the player's angles and view.
  TEST_METHOD(TakesEveryActiveSlotAndThePlayer)
  {
    const auto state = std::make_unique<Elite::GameState>();
    Place(*state, 0, 0, 0x7FFFFF, -0x800000, 0);
    Place(*state, 2, 3, -1, 0x1234, -0x5678);
    Place(*state, 35, 9, 0x10000, -0x10000, 0xFFFF);
    state->Data().playerShip.playerPitchAngle = 0x100;
    state->Data().playerShip.viewAngle = 0x400;
    const Elite::FlightSnapshot snapshot = Elite::TakeFlightSnapshot(*state);
    Assert::AreEqual(std::uint32_t{3}, static_cast<std::uint32_t>(snapshot.objectCount), L"three slots are active");
    const Elite::SnapshotObject& sun = snapshot.objects[0];
    Assert::IsTrue(sun.slot == 0 && sun.kind == 0 && sun.x == 0x7FFFFF && sun.y == -0x800000 && sun.z == 0, L"the sun");
    const Elite::SnapshotObject& station = snapshot.objects[1];
    Assert::IsTrue(station.slot == 2 && station.kind == 3 && station.x == -1 && station.y == 0x1234 && station.z == -0x5678, L"slot 2");
    const Elite::SnapshotObject& last = snapshot.objects[2];
    Assert::IsTrue(last.slot == 35 && last.kind == 9 && last.x == 0x10000 && last.y == -0x10000 && last.z == 0xFFFF, L"the last slot");
    Assert::IsTrue(snapshot.playerPitch == 0x100 && snapshot.viewAngle == 0x400, L"the player");
  }

  // An object continues when its slot holds the same kind within a frame's step; a new kind, a jump, or a slot that was empty
  // does not. The camera continues within a frame's turn and the same view.
  TEST_METHOD(ContinuityIsTheSameKindWithinAFramesStep)
  {
    const auto state = std::make_unique<Elite::GameState>();
    Place(*state, 3, 5, 1000, 2000, 3000);
    Place(*state, 4, 5, 1000, 2000, 3000);
    Place(*state, 5, 5, 1000, 2000, 3000);
    Elite::FlightSnapshot before = Elite::TakeFlightSnapshot(*state);
    Place(*state, 3, 5, 1000 + Elite::MOST_STEP_PER_FRAME, 2000, 3000);     // as far as a frame takes it
    Place(*state, 4, 6, 1000, 2000, 3000);                                  // another kind
    Place(*state, 5, 5, 1000, 2000 - Elite::MOST_STEP_PER_FRAME - 1, 3000); // further than a frame takes it
    Place(*state, 6, 5, 0, 0, 0);                                           // a slot that was empty
    state->Data().playerShip.playerRollAngle = 0x20;
    Elite::FlightSnapshot after = Elite::TakeFlightSnapshot(*state);
    Elite::ContinueFrom(before, after);
    Assert::AreEqual(std::uint32_t{4}, static_cast<std::uint32_t>(after.objectCount));
    Assert::IsTrue(after.objects[0].continues, L"the same kind, a frame's step on");
    Assert::IsFalse(after.objects[1].continues, L"another kind");
    Assert::IsFalse(after.objects[2].continues, L"a jump");
    Assert::IsFalse(after.objects[3].continues, L"a slot that was empty");
    Assert::IsTrue(after.cameraContinues, L"a small roll");

    before = after;
    state->Data().playerShip.viewAngle = 0x200;
    after = Elite::TakeFlightSnapshot(*state);
    Elite::ContinueFrom(before, after);
    Assert::IsFalse(after.cameraContinues, L"another view");
    before = after;
    state->Data().playerShip.playerYawAngle = 0x300;
    after = Elite::TakeFlightSnapshot(*state);
    Elite::ContinueFrom(before, after);
    Assert::IsFalse(after.cameraContinues, L"more than a frame's turn");
  }

  // The sun and the planet as spheres (D65): where the view put them, times their scale shift's power of 2, and radii of 100 x 256
  // and 50 x 256 units, which the view sees as the discs SizeSun and SizePlanet size; while a supernova burns, the sun's radius is
  // its heat's rows wherever it is. The title's planet stands where its disc is, as large.
  TEST_METHOD(SpheresAreWhereTheDiscsAre)
  {
    Elite::FlightSnapshot snapshot;
    Elite::SnapshotObject planet;
    planet.slot = 1;
    planet.kind = Elite::TYPE_PLANET;
    planet.pitch = 3; // its scale shift
    planet.viewZ = 0x1000;
    Assert::IsTrue(Elite::IsPlanet(planet) && !Elite::IsSun(planet));
    const Elite::SnapshotSphere sphere = Elite::SphereOf(snapshot, planet);
    Assert::AreEqual(32768.0f, sphere.position[2], L"the view z, unshifted");
    Assert::AreEqual(50.0f * 256.0f, sphere.radius, L"the planet's radius");
    Elite::SnapshotObject sun = planet;
    sun.slot = 0;
    sun.kind = Elite::TYPE_SUN;
    Assert::AreEqual(100.0f * 256.0f, Elite::SphereOf(snapshot, sun).radius, L"the sun's radius");
    snapshot.supernovaHeat = 0x40;
    const Elite::SnapshotSphere nova = Elite::SphereOf(snapshot, sun);
    Assert::AreEqual(64.0f * 0x8000 / 256.0f, nova.radius, L"the heat's rows, at the sun's distance");
    Assert::AreEqual(50.0f * 256.0f, Elite::SphereOf(snapshot, planet).radius, L"the planet does not burn");

    snapshot.titlePlanet = Elite::SnapshotDisc{.shown = true, .x = 200, .row = 50, .rows = 30};
    const Elite::SnapshotSphere title = Elite::TitlePlanetSphere(snapshot);
    const float x = Elite::VIEW_AXIS_X + Elite::VIEW_FOCAL * title.position[0] / title.position[2];
    const float row = Elite::VIEW_AXIS_ROW + Elite::VIEW_FOCAL * title.position[1] / title.position[2];
    Assert::IsTrue(std::abs(x - 200.5f) < 1e-3f && std::abs(row - 50.5f) < 1e-3f, L"the title's planet at its disc's middle");
    Assert::IsTrue(std::abs(Elite::VIEW_FOCAL * title.radius / title.position[2] - 30.0f) < 1e-3f, L"its rows");
  }

  // The snapshot takes a supernova's heat while SupernovaHeat holds supernovaFrames at 1, and none while it counts down; the view's
  // laser, whose sights are drawn; and the system the planet is.
  TEST_METHOD(TakesTheSunsHeatTheSightsAndTheSystem)
  {
    const auto state = std::make_unique<Elite::GameState>();
    Elite::GameData& data = state->Data();
    data.commander.supernovaFrames = 5;
    data.commander.supernovaHeat = 0x30;
    Assert::AreEqual(std::uint8_t{0}, Elite::TakeFlightSnapshot(*state).supernovaHeat, L"still counting down");
    data.commander.supernovaFrames = 1;
    Assert::AreEqual(std::uint8_t{0x30}, Elite::TakeFlightSnapshot(*state).supernovaHeat, L"burning");
    Assert::AreEqual(Elite::FlightSnapshot::NO_SIGHT, Elite::TakeFlightSnapshot(*state).sight, L"no laser, no sights");
    data.commander.laserMountsFitted = 1; // the front mount
    data.commander.laserMountTypes = 2;   // a military laser there
    Assert::AreEqual(std::uint8_t{2}, Elite::TakeFlightSnapshot(*state).sight, L"the front laser's sights");
    data.playerShip.viewAngle = 0x400; // the rear view, with no laser
    Assert::AreEqual(Elite::FlightSnapshot::NO_SIGHT, Elite::TakeFlightSnapshot(*state).sight, L"none in the rear view");
    data.commander.galaxyNumber = 3;
    data.commander.currentSystem.index = 0x81;
    const Elite::FlightSnapshot snapshot = Elite::TakeFlightSnapshot(*state);
    Assert::IsTrue(snapshot.galaxy == 3 && snapshot.system == 0x81, L"the system");
    const Elite::FlightSnapshot title = Elite::TakeTitleSnapshot(*state, "TITLE", Elite::SnapshotDisc{.shown = true});
    Assert::IsTrue(title.title && !title.dashboard.live && title.titlePlanet.shown && title.dustCount == 0, L"the title's");
  }

  // The platform numbers each snapshot, stamps it with its clock, and links it to the one before unless it starts a flight.
  TEST_METHOD(ThePlatformKeepsTheLastTwo)
  {
    HostRig rig("FlightSnapshotPublish");
    Place(rig.State(), 3, 5, 0, 0, 0);
    Elite::Host& platform = rig.Host();
    Assert::AreEqual(std::uint64_t{0}, platform.LatestFlightSnapshot().sequence, L"none yet");
    platform.PublishFlightSnapshot(Elite::TakeFlightSnapshot(rig.State()), true);
    Assert::AreEqual(std::uint64_t{1}, platform.LatestFlightSnapshot().sequence);
    Assert::IsFalse(platform.LatestFlightSnapshot().objects[0].continues, L"the first continues nothing");
    platform.PublishFlightSnapshot(Elite::TakeFlightSnapshot(rig.State()), true);
    Assert::AreEqual(std::uint64_t{2}, platform.LatestFlightSnapshot().sequence);
    Assert::AreEqual(std::uint64_t{1}, platform.PreviousFlightSnapshot().sequence);
    Assert::IsTrue(platform.LatestFlightSnapshot().objects[0].continues && platform.LatestFlightSnapshot().cameraContinues,
                   L"the second continues the first");
    Assert::AreEqual(platform.Clock(), platform.LatestFlightSnapshot().pacedTime, L"stamped with the clock");
    platform.PublishFlightSnapshot(Elite::TakeFlightSnapshot(rig.State()), false);
    Assert::IsFalse(platform.LatestFlightSnapshot().objects[0].continues || platform.LatestFlightSnapshot().cameraContinues,
                    L"a new flight continues nothing");
  }
};

} // namespace GameLogicTests
