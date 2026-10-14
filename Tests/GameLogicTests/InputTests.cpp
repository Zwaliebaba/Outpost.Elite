#include "pch.h"

#include "Text.h"
#include "TwinRig.h"

#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// A key's release: its code with bit 7 set.
constexpr std::uint8_t BREAK = 0x80;

// inputDevice as the disc menu sets it.
constexpr std::uint8_t MOUSE_DEVICE = 2;

// The twin's mouse, through the machine's own driver (Elite::Mouse, a twin made with mousePresent): moved _acrossMickeys right
// and _downMickeys down, which the next read of its motion (int 33h function 0Bh, once a frame) takes all at once, and with
// _buttons held (Elite::Mouse::BUTTON_LEFT, BUTTON_RIGHT).
void MoveTwinMouse(TwinRig& _rig, std::int32_t _acrossMickeys, std::int32_t _downMickeys, std::uint8_t _buttons = 0)
{
  _rig.Both(
    [=](Elite::Platform& _platform, Elite::GameState&)
    {
      Elite::Mouse& mouse = _platform.MouseDriver();
      mouse.Move(_acrossMickeys, _downMickeys);
      mouse.SetButtons(_buttons);
    });
}

// The rates the steering left flight, roll and pitch (rollRate's two bytes), as the twin has them.
[[nodiscard]] std::pair<std::int8_t, std::int8_t> FlightRates(TwinRig& _rig)
{
  std::uint16_t rates = 0;
  _rig.Both([&rates](Elite::Platform&, Elite::GameState& _state) { rates = _state.Data().playerShip.rollRate; });
  return {static_cast<std::int8_t>(rates & 0xFF), static_cast<std::int8_t>(rates >> 8)};
}

// The byte _member names, as the twin holds it.
template <typename Struct> [[nodiscard]] std::uint8_t TwinByte(TwinRig& _rig, DataByte<Struct> _member)
{
  std::uint8_t value = 0;
  _rig.Both([&value, _member](Elite::Platform&, Elite::GameState& _state) { value = DataByteIn(_state, _member); });
  return value;
}

} // namespace

// Twins for the mouse and a briefing's wait, which no replay uses (ADR-016). The joysticks' went with D81.
TEST_CLASS(InputTests)
{
public:
  // The mask mission's briefing, which the docked status screen shows first when F9 brings it back from another screen, waits in
  // WaitForKeyPress until space: the routine that waits gives the original's digests while it waits and after.
  TEST_METHOD(WaitForKeyPressWaitsThroughABriefing)
  {
    TwinRig rig("TwinBriefing");
    rig.Play("key space; wait 4");
    rig.Both(
      [](Elite::Platform&, Elite::GameState& _state)
      {
        _state.Data().commander.missionNumber = 2;
        _state.Data().commander.missionStage = 0;
      });
    rig.Play("key F8; wait 0.5\nkey F9; wait 1\ndigest briefing\nkey space; wait 1\ndigest status");
  }

  // Flight steered by the mouse, with the machine's own driver loaded (ADR-008 item 8, D20): M at the disc menu, which takes the
  // mouse only with a driver; the launch; the mouse moved right and down, then further left and up, which ReadMouseSteering turns
  // into rates that stay until it moves again; and the left button held, which fires the laser.
  TEST_METHOD(MouseSteersFlight)
  {
    TwinRig rig("TwinMouseFlight", {.mousePresent = true});
    rig.Play("key space; wait 4\nkey Escape; wait 0.3\nkey m; wait 0.3\ndigest mouse");
    Assert::AreEqual(MOUSE_DEVICE, TwinByte(rig, &Elite::SaveLoadState::inputDevice), L"the mouse selected");
    rig.Play("key F1; wait 1\ndigest launched");
    MoveTwinMouse(rig, 0x100, 0x100);
    rig.Play("wait 0.5\ndigest right-and-down");
    const std::pair<std::int8_t, std::int8_t> rightAndDown = FlightRates(rig);
    MoveTwinMouse(rig, -0x200, -0x200);
    rig.Play("wait 0.5\ndigest left-and-up");
    const std::pair<std::int8_t, std::int8_t> leftAndUp = FlightRates(rig);
    MoveTwinMouse(rig, 0, 0, Elite::Mouse::BUTTON_LEFT);
    rig.Play("wait 0.5\ndigest firing");
    Assert::IsTrue(TwinByte(rig, &Elite::FlightState::laserTemperature) != 0, L"the laser fired");
    MoveTwinMouse(rig, 0, 0);
    rig.Play("wait 0.1");
    Assert::IsTrue(rightAndDown.first > 0 && leftAndUp.first < 0, L"the mouse rolled the ship either way");
    Assert::IsTrue(rightAndDown.second < 0 && leftAndUp.second > 0, L"and pitched it either way");
  }
};

} // namespace GameLogicTests
