#include "pch.h"

#include "SceneSnapshot.h"

#include <cmath>
#include <numbers>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace EngineTests
{

namespace
{

constexpr float TOLERANCE = 1e-4f;

// A turn of _radians about z.
[[nodiscard]] Engine::Quaternion AboutZ(float _radians) noexcept
{
  return Engine::Quaternion{.x = 0.0f, .y = 0.0f, .z = std::sin(_radians / 2.0f), .w = std::cos(_radians / 2.0f)};
}

// Whether _a and _b are one rotation: equal, or each the other's negation.
[[nodiscard]] bool SameRotation(const Engine::Quaternion& _a, const Engine::Quaternion& _b) noexcept
{
  const float dot = _a.x * _b.x + _a.y * _b.y + _a.z * _b.z + _a.w * _b.w;
  return std::abs(std::abs(dot) - 1.0f) < TOLERANCE;
}

[[nodiscard]] bool Near(const Engine::Vector3& _a, const Engine::Vector3& _b) noexcept
{
  return std::abs(_a.x - _b.x) < TOLERANCE && std::abs(_a.y - _b.y) < TOLERANCE && std::abs(_a.z - _b.z) < TOLERANCE;
}

[[nodiscard]] Engine::SceneObject Object(std::uint32_t _key, Engine::Vector3 _position, Engine::Quaternion _orientation, bool _continues)
{
  return Engine::SceneObject{.key = _key, .model = 7, .position = _position, .orientation = _orientation, .continues = _continues};
}

} // namespace

// Drawing between two updates (ADR-032, D49): the rotations between two, the blend for a frame's time, and the scene between.
TEST_CLASS(SceneSnapshotTests)
{
public:
  // Slerp's ends are its arguments; its middle is half the turn; it goes the short way when the second is given negated.
  TEST_METHOD(SlerpTurnsTheShortWayAtAnEvenRate)
  {
    const Engine::Quaternion from = AboutZ(0.0f);
    const Engine::Quaternion to = AboutZ(std::numbers::pi_v<float> / 2.0f);
    Assert::IsTrue(SameRotation(Engine::Slerp(from, to, 0.0f), from), L"the start");
    Assert::IsTrue(SameRotation(Engine::Slerp(from, to, 1.0f), to), L"the end");
    Assert::IsTrue(SameRotation(Engine::Slerp(from, to, 0.5f), AboutZ(std::numbers::pi_v<float> / 4.0f)), L"half way: an eighth turn");
    Assert::IsTrue(SameRotation(Engine::Slerp(from, to, 0.25f), AboutZ(std::numbers::pi_v<float> / 8.0f)), L"a quarter of the way");
    const Engine::Quaternion negated{.x = -to.x, .y = -to.y, .z = -to.z, .w = -to.w};
    Assert::IsTrue(SameRotation(Engine::Slerp(from, negated, 0.5f), AboutZ(std::numbers::pi_v<float> / 4.0f)), L"the short way");
    Assert::IsTrue(SameRotation(Engine::Slerp(from, from, 0.5f), from), L"no turn at all");
  }

  // A frame shows the game one update late: at the later update's time the earlier one, an update on the later, and between,
  // the fraction of the way; before or after that, the nearer end.
  TEST_METHOD(BlendFactorShowsTheGameOneUpdateLate)
  {
    Assert::AreEqual(0.0f, Engine::BlendFactor(100, 200, 200));
    Assert::AreEqual(0.25f, Engine::BlendFactor(100, 200, 225));
    Assert::AreEqual(1.0f, Engine::BlendFactor(100, 200, 300));
    Assert::AreEqual(1.0f, Engine::BlendFactor(100, 200, 450), L"the next update is late");
    Assert::AreEqual(0.0f, Engine::BlendFactor(100, 200, 150), L"the frame is before the later update");
    Assert::AreEqual(0.0f, Engine::BlendFactor(200, 200, 300), L"one update, twice");
  }

  // Interpolate moves a continuing camera and each continuing object part of the way, and draws the rest as the later snapshot has
  // them: an object that does not continue, one whose key the earlier snapshot lacks, and none of what only the earlier holds.
  TEST_METHOD(InterpolateMovesOnlyWhatContinues)
  {
    Engine::SceneSnapshot before;
    before.time = 1000;
    before.cameraPosition = Engine::Vector3{.x = 0.0f, .y = 0.0f, .z = 0.0f};
    before.cameraOrientation = AboutZ(0.0f);
    before.objects = {Object(1, {.x = 0.0f, .y = 0.0f, .z = 0.0f}, AboutZ(0.0f), false),
                      Object(2, {.x = 10.0f, .y = 0.0f, .z = 0.0f}, AboutZ(0.0f), false),
                      Object(9, {.x = 5.0f, .y = 5.0f, .z = 5.0f}, AboutZ(0.0f), false)};
    Engine::SceneSnapshot after;
    after.time = 2000;
    after.cameraPosition = Engine::Vector3{.x = 4.0f, .y = 0.0f, .z = 0.0f};
    after.cameraOrientation = AboutZ(1.0f);
    after.cameraContinues = true;
    after.objects = {Object(1, {.x = 8.0f, .y = 0.0f, .z = 0.0f}, AboutZ(1.0f), true),
                     Object(2, {.x = 99.0f, .y = 0.0f, .z = 0.0f}, AboutZ(1.0f), false),
                     Object(3, {.x = 0.0f, .y = 7.0f, .z = 0.0f}, AboutZ(1.0f), true)};

    const Engine::SceneSnapshot scene = Engine::Interpolate(before, after, 0.25f);
    Assert::AreEqual(std::uint64_t{1250}, scene.time, L"a quarter of the way in time");
    Assert::IsTrue(Near(scene.cameraPosition, Engine::Vector3{.x = 1.0f, .y = 0.0f, .z = 0.0f}), L"the camera moved a quarter");
    Assert::IsTrue(SameRotation(scene.cameraOrientation, AboutZ(0.25f)), L"and turned a quarter");
    Assert::AreEqual(std::uint32_t{3}, static_cast<std::uint32_t>(scene.objects.size()), L"the later snapshot's objects");
    Assert::IsTrue(Near(scene.objects[0].position, Engine::Vector3{.x = 2.0f, .y = 0.0f, .z = 0.0f}), L"1 continues: a quarter");
    Assert::IsTrue(SameRotation(scene.objects[0].orientation, AboutZ(0.25f)), L"1 turned a quarter");
    Assert::IsTrue(Near(scene.objects[1].position, Engine::Vector3{.x = 99.0f, .y = 0.0f, .z = 0.0f}), L"2 is new: where it is");
    Assert::IsTrue(Near(scene.objects[2].position, Engine::Vector3{.x = 0.0f, .y = 7.0f, .z = 0.0f}), L"3 has no earlier: where it is");

    after.cameraContinues = false;
    const Engine::SceneSnapshot cut = Engine::Interpolate(before, after, 0.25f);
    Assert::IsTrue(Near(cut.cameraPosition, after.cameraPosition) && SameRotation(cut.cameraOrientation, after.cameraOrientation),
                   L"a camera that does not continue is where the later snapshot puts it");
  }
};

} // namespace EngineTests
