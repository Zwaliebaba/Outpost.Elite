#include "pch.h"

#include "DataOverlay.h"
#include "TwinRig.h"

#include <initializer_list>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

// A key's release: its code with bit 7 set.
constexpr std::uint8_t BREAK = 0x80;

// inputDevice as the disc menu sets it.
constexpr std::uint8_t JOYSTICK_DEVICE = 1;
constexpr std::uint8_t MOUSE_DEVICE = 2;
// The IBM stick's axes: centred, and pushed either way, in ohms.
constexpr std::uint32_t CENTERED_OHMS = 50'000;
constexpr std::uint32_t LOW_OHMS = 20'000;
constexpr std::uint32_t HIGH_OHMS = 80'000;
// The codes the Amstrad's stick sends through the keyboard (keyDownAmstradFire1 and the rest).
constexpr std::uint8_t AMSTRAD_FIRE = 0x77;
constexpr std::uint8_t AMSTRAD_RIGHT = 0x79;
constexpr std::uint8_t AMSTRAD_LEFT = 0x7A;
constexpr std::uint8_t AMSTRAD_DOWN = 0x7B;
constexpr std::uint8_t AMSTRAD_UP = 0x7C;

// The IBM stick attached and selected, centred on the count 50 kOhm gives, steering the galactic chart's cursor: the same
// resistance on both axes, which ends both counts together, and X's below Y's, which makes the read wait for Y's one-shot. (With
// X's above Y's, X's one-shot would outlast the read, and the next read would depend on the cycles of everything run between the
// two, which native code does not count.) Every digest must be the twin's known answer, and the cursor must move.
void SteerTheChartWithTheStick(TwinRig& _rig)
{
  _rig.Play("key space; wait 4");
  _rig.Both(
    [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      const std::uint16_t segment = Elite::DataSegment(_program);
      _pc.Ram().Write8(segment, DS.inputDevice.offset, 1);
      _pc.Ram().Write8(segment, DS.joystickIsAmstrad.offset, 0);
      _pc.Ram().Write16(segment, DS.joystickCenterX.offset, 81);
      _pc.Ram().Write16(segment, DS.joystickCenterY.offset, 81);
    });
  const auto attach = [&](std::uint32_t _xOhms, std::uint32_t _yOhms)
  {
    _rig.Both(
      [=](Machine::Pc& _pc, const Machine::LoadedProgram&)
      {
        _pc.Joystick().SetAxisResistance(0, _xOhms);
        _pc.Joystick().SetAxisResistance(1, _yOhms);
      });
  };
  // The chart cursor, x and y.
  const auto cursor = [&]()
  {
    std::uint16_t position = 0;
    _rig.Both([&](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
              { position = _pc.Ram().Read16(Elite::DataSegment(_program), DS.chartCursorX.offset); });
    return position;
  };
  attach(50'000, 50'000);
  _rig.Play("key F5; wait 0.5\ndigest centred");
  const std::uint16_t centered = cursor();
  attach(10'000, 10'000);
  _rig.Play("wait 0.5\ndigest low");
  const std::uint16_t low = cursor();
  attach(100'000, 100'000);
  _rig.Play("wait 0.5\ndigest high");
  const std::uint16_t high = cursor();
  attach(0, 0);
  _rig.Play("wait 0.5\ndigest lowest");
  attach(20'000, 80'000);
  _rig.Play("wait 0.5\ndigest left-and-down");
  Assert::IsTrue(centered != low, L"the stick moved the cursor");
  Assert::IsTrue(low != high, L"the stick moved the cursor back");
}

// The twin's mouse, through the machine's own driver (Machine::Mouse, a twin made with mousePresent): moved _acrossMickeys right
// and _downMickeys down, which the next read of its motion (int 33h function 0Bh, once a frame) takes all at once, and with
// _buttons held (Machine::Mouse::BUTTON_LEFT, BUTTON_RIGHT).
void MoveTwinMouse(TwinRig& _rig, std::int32_t _acrossMickeys, std::int32_t _downMickeys, std::uint8_t _buttons = 0)
{
  _rig.Both(
    [=](Machine::Pc& _pc, const Machine::LoadedProgram&)
    {
      Machine::Mouse& mouse = _pc.Services().MouseDriver();
      mouse.Move(_acrossMickeys, _downMickeys);
      mouse.SetButtons(_buttons);
    });
}

// The IBM stick in the twin's game port: each axis's resistance, and the first button.
void SetTwinStick(TwinRig& _rig, std::uint32_t _xOhms, std::uint32_t _yOhms, bool _firing = false)
{
  _rig.Both(
    [=](Machine::Pc& _pc, const Machine::LoadedProgram&)
    {
      _pc.Joystick().SetAxisResistance(0, _xOhms);
      _pc.Joystick().SetAxisResistance(1, _yOhms);
      _pc.Joystick().SetButton(0, _firing);
    });
}

// _codes sent by the twin's keyboard, in order: how a twin sends what an Amstrad's stick sends, codes 77h-7Ch that no replay
// can name. ReadScanCode takes each as it takes a key's.
void SendTwinCodes(TwinRig& _rig, std::initializer_list<std::uint8_t> _codes)
{
  _rig.Both(
    [_codes](Machine::Pc& _pc, const Machine::LoadedProgram&)
    {
      for (const std::uint8_t code : _codes)
        _pc.KeyboardController().Inject(code);
    });
}

// The rates the steering left flight, roll and pitch (rollRate's two bytes), as the twin has them.
[[nodiscard]] std::pair<std::int8_t, std::int8_t> FlightRates(TwinRig& _rig)
{
  std::uint16_t rates = 0;
  _rig.Both([&rates](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { rates = _pc.Ram().Read16(Elite::DataSegment(_program), DS.rollRate.offset); });
  return {static_cast<std::int8_t>(rates & 0xFF), static_cast<std::int8_t>(rates >> 8)};
}

// The byte _field as the twin holds it.
[[nodiscard]] std::uint8_t TwinByte(TwinRig& _rig, Elite::DataField<std::uint8_t> _field)
{
  std::uint8_t value = 0;
  _rig.Both([&value, _field](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { value = _pc.Ram().Read8(Elite::DataSegment(_program), _field.offset); });
  return value;
}

} // namespace

// Twins for the joysticks, the mouse and a briefing's wait, which no replay uses (ADR-016).
TEST_CLASS(InputTests)
{
public:
  // The stick read natively: its read counts the polls the original's did, by the cycles it counts for the original's
  // instructions, and waits where the original's waited. Two twins ran this scenario, one with its native calls compared
  // with the original's and one not; they gave the same answers, and this is the one D7 kept.
  TEST_METHOD(IbmStickSteersTheChart)
  {
    TwinRig rig("TwinStickNative");
    SteerTheChartWithTheStick(rig);
  }

  // The mask mission's briefing, which the docked status screen shows first when F9 brings it back from another screen, waits in
  // WaitForKeyPress until space: the routine that waits gives the original's digests while it waits and after.
  TEST_METHOD(WaitForKeyPressWaitsThroughABriefing)
  {
    TwinRig rig("TwinBriefing");
    rig.Play("key space; wait 4");
    rig.Both(
      [](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
      {
        const std::uint16_t segment = Elite::DataSegment(_program);
        _pc.Ram().Write8(segment, DS.missionNumber.offset, 2);
        _pc.Ram().Write8(segment, DS.missionStage.offset, 0);
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
    Assert::AreEqual(MOUSE_DEVICE, TwinByte(rig, DS.inputDevice), L"the mouse selected");
    rig.Play("key F1; wait 1\ndigest launched");
    MoveTwinMouse(rig, 0x100, 0x100);
    rig.Play("wait 0.5\ndigest right-and-down");
    const std::pair<std::int8_t, std::int8_t> rightAndDown = FlightRates(rig);
    MoveTwinMouse(rig, -0x200, -0x200);
    rig.Play("wait 0.5\ndigest left-and-up");
    const std::pair<std::int8_t, std::int8_t> leftAndUp = FlightRates(rig);
    MoveTwinMouse(rig, 0, 0, Machine::Mouse::BUTTON_LEFT);
    rig.Play("wait 0.5\ndigest firing");
    Assert::IsTrue(TwinByte(rig, DS.laserTemperature) != 0, L"the laser fired");
    MoveTwinMouse(rig, 0, 0);
    rig.Play("wait 0.1");
    Assert::IsTrue(rightAndDown.first > 0 && leftAndUp.first < 0, L"the mouse rolled the ship either way");
    Assert::IsTrue(rightAndDown.second < 0 && leftAndUp.second > 0, L"and pitched it either way");
  }

  // Flight steered by the IBM stick (ADR-008 item 8, D20): plugged in, centred, chosen at the disc menu, which reads its centre;
  // then, in flight, pushed left and up, right and down, and left and down, and its first button held, which fires the laser.
  // X's resistance is never above Y's, as SteerTheChartWithTheStick explains.
  TEST_METHOD(IbmStickSteersFlight)
  {
    TwinRig rig("TwinStickFlight");
    SetTwinStick(rig, CENTERED_OHMS, CENTERED_OHMS);
    rig.Play("key space; wait 4\nkey Escape; wait 0.3\nkey j; wait 0.3\nkey i; wait 0.3\nkey space; wait 0.3\ndigest stick");
    Assert::AreEqual(JOYSTICK_DEVICE, TwinByte(rig, DS.inputDevice), L"the stick selected");
    rig.Play("key F1; wait 1\ndigest launched");
    SetTwinStick(rig, LOW_OHMS, LOW_OHMS);
    rig.Play("wait 0.5\ndigest left-and-up");
    const std::pair<std::int8_t, std::int8_t> leftAndUp = FlightRates(rig);
    SetTwinStick(rig, HIGH_OHMS, HIGH_OHMS);
    rig.Play("wait 0.5\ndigest right-and-down");
    const std::pair<std::int8_t, std::int8_t> rightAndDown = FlightRates(rig);
    SetTwinStick(rig, LOW_OHMS, HIGH_OHMS);
    rig.Play("wait 0.5\ndigest left-and-down");
    SetTwinStick(rig, CENTERED_OHMS, CENTERED_OHMS, true);
    rig.Play("wait 0.5\ndigest firing");
    Assert::IsTrue(TwinByte(rig, DS.laserTemperature) != 0, L"the laser fired");
    SetTwinStick(rig, CENTERED_OHMS, CENTERED_OHMS);
    rig.Play("wait 0.1");
    Assert::IsTrue(leftAndUp.first < 0 && rightAndDown.first > 0, L"the stick rolled the ship either way");
    Assert::IsTrue(leftAndUp.second != rightAndDown.second, L"and pitched it");
  }

  // Flight steered by the Amstrad's stick (ADR-008 item 8, D20), which sends codes 77h-7Ch through the keyboard rather than
  // using the game port: chosen at the disc menu, where it must be moved before a key; then, in flight, held right and down,
  // left and up, and its fire button held. Its codes go through the keyboard's rates (ApplyKeyboardRates).
  TEST_METHOD(AmstradStickSteersFlight)
  {
    TwinRig rig("TwinAmstradFlight");
    rig.Play("key space; wait 4\nkey Escape; wait 0.3\nkey j; wait 0.3\nkey a; wait 0.3");
    SendTwinCodes(rig, {AMSTRAD_UP, AMSTRAD_UP | BREAK});
    rig.Play("wait 0.1\nkey space; wait 0.3\ndigest amstrad");
    Assert::AreEqual(JOYSTICK_DEVICE, TwinByte(rig, DS.inputDevice), L"the Amstrad's stick selected");
    Assert::AreEqual(std::uint8_t{1}, TwinByte(rig, DS.joystickIsAmstrad), L"the Amstrad's");
    rig.Play("key F1; wait 1\ndigest launched");
    SendTwinCodes(rig, {AMSTRAD_RIGHT, AMSTRAD_DOWN});
    rig.Play("wait 0.5\ndigest right-and-down");
    const std::pair<std::int8_t, std::int8_t> rightAndDown = FlightRates(rig);
    SendTwinCodes(rig, {AMSTRAD_RIGHT | BREAK, AMSTRAD_DOWN | BREAK, AMSTRAD_LEFT, AMSTRAD_UP});
    rig.Play("wait 0.5\ndigest left-and-up");
    const std::pair<std::int8_t, std::int8_t> leftAndUp = FlightRates(rig);
    SendTwinCodes(rig, {AMSTRAD_LEFT | BREAK, AMSTRAD_UP | BREAK, AMSTRAD_FIRE});
    rig.Play("wait 0.5\ndigest firing");
    Assert::IsTrue(TwinByte(rig, DS.laserTemperature) != 0, L"the laser fired");
    SendTwinCodes(rig, {AMSTRAD_FIRE | BREAK});
    rig.Play("wait 0.1");
    Assert::IsTrue(rightAndDown.first > 0 && leftAndUp.first < 0, L"the stick rolled the ship either way");
    Assert::IsTrue(rightAndDown.second != leftAndUp.second, L"and pitched it");
  }
};

} // namespace GameLogicTests
