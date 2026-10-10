#include "pch.h"

#include "ComparisonRig.h"
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
using Bytes = std::initializer_list<std::uint8_t>;

constexpr std::uint16_t KEYBOARD_INTERRUPT = 0x0201;
constexpr std::uint16_t IS_MOUSE_DRIVER_INSTALLED = 0x02D4;
constexpr std::uint16_t READ_FIRE_BUTTON = 0x74E0;
constexpr std::uint16_t READ_STEERING = 0x7536;
constexpr std::uint16_t GET_KEY = 0x7616;
constexpr std::uint16_t READ_JOYSTICK_AXES = 0x777E;
constexpr std::uint16_t READ_JOYSTICK_STEERING = 0x77C1;
constexpr std::uint16_t READ_MOUSE_STEERING = 0x797E;
constexpr std::uint16_t RESET_MOUSE_IF_SELECTED = 0x7F5D;
constexpr std::uint16_t APPLY_REVERSE_CONTROLS = 0x8EA5;
constexpr std::uint16_t APPLY_REVERSE_CONTROLS_TO_DX = 0x8EBA;

constexpr std::uint8_t ALT = 0x38;
constexpr std::uint8_t LEFT_SHIFT = 0x2A;
constexpr std::uint8_t RIGHT_SHIFT = 0x36;
constexpr std::uint8_t BREAK = 0x80;
constexpr std::uint16_t LAST_BUFFER_BYTE = 0xA0BF; // keyBuffer's 16th
constexpr std::uint16_t MIDDLE_COUNT = 60100;      // ReadJoystickAxes counts up from 60000
constexpr std::uint16_t SMALL_CENTER = 100;
constexpr std::uint16_t MOUSE_VECTOR_OFFSET = 0x33 * 4; // int 33h's, in the table at 0000:0000
constexpr std::uint16_t MOUSE_VECTOR_SEGMENT = MOUSE_VECTOR_OFFSET + 2;
// A stand-in mouse driver's code, in the BIOS's inter-application area at 0000:04F0, which nothing else uses.
constexpr std::uint16_t MOUSE_DRIVER = 0x04F0;
constexpr std::uint8_t MOV_AX = 0xB8;
constexpr std::uint8_t IRET = 0xCF;

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

void Poke(ComparisonRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  _rig.Host().Ram().Write8(Elite::DataSegment(_rig.Program()), _field.offset, _value);
}

void Poke(ComparisonRig& _rig, Elite::DataField<std::uint16_t> _field, std::uint16_t _value)
{
  _rig.Host().Ram().Write16(Elite::DataSegment(_rig.Program()), _field.offset, _value);
}

// Points int 33h at a stand-in driver that answers every call with AX = _ax: MOV AX,_ax; IRET.
void InstallMouseDriver(ComparisonRig& _rig, std::uint16_t _ax)
{
  Machine::Memory& memory = _rig.Host().Ram();
  memory.Write8(0, MOUSE_DRIVER, MOV_AX);
  memory.Write16(0, MOUSE_DRIVER + 1, _ax);
  memory.Write8(0, MOUSE_DRIVER + 3, IRET);
  memory.Write16(0, MOUSE_VECTOR_OFFSET, MOUSE_DRIVER);
  memory.Write16(0, MOUSE_VECTOR_SEGMENT, 0);
}

// Calls an interrupt handler as the CPU enters one: the flags, with interrupts off, then CS and the return
// offset on the stack.
void CallHandler(ComparisonRig& _rig, std::uint16_t _entry)
{
  Machine::Pc& pc = _rig.Host();
  Machine::Registers& regs = pc.Processor().Regs();
  const Machine::Registers saved = regs;
  regs.flags = static_cast<std::uint16_t>(regs.flags & ~Machine::FLAG_INTERRUPT);
  regs.cs = _rig.Program().loadSegment;
  for (const std::uint16_t value : {regs.flags, regs.cs})
  {
    regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
    pc.Ram().Write16(regs.ss, regs.sp, value);
  }
  pc.CallNear(_entry);
  regs = saved;
}

// The keyboard sends _code; time passes until it is latched, and int 9's handler takes it.
void Deliver(ComparisonRig& _rig, std::uint8_t _code)
{
  Machine::Pc& pc = _rig.Host();
  pc.KeyboardController().Inject(_code);
  while (!pc.KeyboardController().LatchFull())
    pc.Idle(Machine::NO_EVENT);
  CallHandler(_rig, KEYBOARD_INTERRUPT);
}

// The IBM stick attached to both twins and selected, centred on the count 50 kOhm gives, steering the galactic chart's cursor:
// the same resistance on both axes, which ends both counts together, and X's below Y's, which makes the read wait for Y's
// one-shot. (With X's above Y's, X's one-shot would outlast the read, and the next read would depend on the cycles of everything
// run between the two, which native code does not count.) Every digest of the twins must agree, and the cursor must move.
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
  // The interpreted twin's chart cursor, x and y (Both calls it first).
  const auto cursor = [&]()
  {
    std::uint16_t position = 0;
    bool first = true;
    _rig.Both(
      [&](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
      {
        if (first)
          position = _pc.Ram().Read16(Elite::DataSegment(_program), DS.chartCursorX.offset);
        first = false;
      });
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

// Both twins' mice, through the machine's own driver (Machine::Mouse, a twin made with mousePresent): moved _acrossMickeys right
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

// The IBM stick in the game port of both twins: each axis's resistance, and the first button.
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

// _codes sent by both twins' keyboards, in order: how a twin sends what an Amstrad's stick sends, codes 77h-7Ch that no replay
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

// The rates the steering left flight, roll and pitch (rollRate's two bytes), as the native twin has them; the interpreted
// twin's are the same, or the digests would differ.
[[nodiscard]] std::pair<std::int8_t, std::int8_t> FlightRates(TwinRig& _rig)
{
  std::uint16_t rates = 0;
  _rig.Both([&rates](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { rates = _pc.Ram().Read16(Elite::DataSegment(_program), DS.rollRate.offset); });
  return {static_cast<std::int8_t>(rates & 0xFF), static_cast<std::int8_t>(rates >> 8)};
}

// The byte _field as the native twin holds it; the interpreted twin's is the same, or the digests would differ.
[[nodiscard]] std::uint8_t TwinByte(TwinRig& _rig, Elite::DataField<std::uint8_t> _field)
{
  std::uint8_t value = 0;
  _rig.Both([&value, _field](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { value = _pc.Ram().Read8(Elite::DataSegment(_program), _field.offset); });
  return value;
}

} // namespace

// Constructed inputs for the keyboard, joystick and mouse routines (plan §6.3): the keys, devices and options no
// replay uses.
TEST_CLASS(InputTests)
{
public:
  // D released in flight, Alt-W, both Shifts, a full buffer and its wrap.
  TEST_METHOD(KeyboardInterruptAgreesOnEveryKindOfCode)
  {
    ComparisonRig rig("KeyboardInterrupt");
    Assert::IsFalse(rig.Host().KeyboardController().LatchFull(), L"no code waiting");
    std::uint64_t calls = 0;
    const auto deliver = [&](std::uint8_t _code)
    {
      Deliver(rig, _code);
      ++calls;
    };
    Poke(rig, DS.keyBufferCount, 0);
    Poke(rig, DS.inFlight, 1);
    for (const std::uint8_t code : Bytes{0x20, 0x20 | BREAK, ALT, 0x11, 0x11 | BREAK, ALT | BREAK})
      deliver(code);
    for (const std::uint8_t shift : Bytes{LEFT_SHIFT, RIGHT_SHIFT})
    {
      for (const std::uint8_t code : Bytes{shift, 0x1E, 0x1E | BREAK, static_cast<std::uint8_t>(shift | BREAK)})
        deliver(code);
    }
    Poke(rig, DS.keyBufferCount, 0x10);
    deliver(0x30);
    Poke(rig, DS.keyBufferCount, 0);
    Poke(rig, DS.keyBufferWrite, LAST_BUFFER_BYTE);
    deliver(0x31);
    rig.AssertAllAgreed(KEYBOARD_INTERRUPT, calls);
  }

  // The Amstrad's stick and mouse by their key codes, the IBM stick's two buttons, and the mouse through int 33h: with no
  // driver, the ROM's IRET, and with one that reports neither button, the left or the right.
  TEST_METHOD(ReadFireButtonAgreesOnEveryDevice)
  {
    ComparisonRig rig("ReadFireButton");
    std::uint64_t calls = 0;
    const auto read = [&]()
    {
      rig.Call(READ_FIRE_BUTTON, {});
      ++calls;
    };
    Poke(rig, DS.inputDevice, 1);
    Poke(rig, DS.joystickIsAmstrad, 1);
    for (const std::uint8_t pressed : Bytes{0, 1})
    {
      Poke(rig, DS.keyDownAmstradFire2, pressed);
      read();
    }
    Poke(rig, DS.joystickIsAmstrad, 0);
    Machine::GamePort& stick = rig.Host().Joystick();
    read();
    for (const std::size_t button : {std::size_t{0}, std::size_t{1}})
    {
      stick.SetButton(button, true);
      read();
      stick.SetButton(button, false);
    }
    Poke(rig, DS.inputDevice, 2);
    Poke(rig, DS.amstradPresent, 1);
    for (const std::uint8_t pressed : Bytes{0, 1})
    {
      Poke(rig, DS.keyDownAmstradMouseLeft, pressed);
      read();
    }
    Poke(rig, DS.amstradPresent, 0);
    read();
    for (const std::uint8_t buttons : Bytes{0, 1, 2})
    {
      InstallMouseDriver(rig, buttons);
      read();
    }
    rig.AssertAllAgreed(READ_FIRE_BUTTON, calls);
  }

  // Another device, and the mouse with no driver (the ROM's IRET) and with one, which answers a reset with FFFFh.
  TEST_METHOD(ResetMouseIfSelectedAgreesWithAndWithoutTheMouse)
  {
    ComparisonRig rig("ResetMouseIfSelected");
    Poke(rig, DS.inputDevice, 1);
    rig.Call(RESET_MOUSE_IF_SELECTED, {.ax = 0x1111, .bx = 0x2222});
    Poke(rig, DS.inputDevice, 2);
    rig.Call(RESET_MOUSE_IF_SELECTED, {.ax = 0x1111, .bx = 0x2222});
    InstallMouseDriver(rig, 0xFFFF);
    rig.Call(RESET_MOUSE_IF_SELECTED, {.ax = 0x1111, .bx = 0x2222});
    rig.AssertAllAgreed(RESET_MOUSE_IF_SELECTED, 3);
  }

  // The Amstrad's stick, whose keys go through the keyboard's rates, and the IBM stick: absent, at either end of
  // both axes about a centre near the middle, and far off a small centre.
  TEST_METHOD(ReadSteeringAgreesWithTheJoysticks)
  {
    ComparisonRig rig("ReadSteering");
    std::uint64_t calls = 0;
    const auto read = [&]()
    {
      rig.Call(READ_STEERING, {});
      ++calls;
    };
    Poke(rig, DS.inputDevice, 1);
    Poke(rig, DS.joystickIsAmstrad, 1);
    Poke(rig, DS.keyDownAmstradUp, 1);
    read();
    // An unplugged axis's one-shot never ends, and a one-shot that has not ended cannot fire again: the absent stick
    // comes last.
    Poke(rig, DS.joystickIsAmstrad, 0);
    Machine::GamePort& stick = rig.Host().Joystick();
    const std::initializer_list<std::pair<std::uint16_t, std::uint32_t>> positions = {{MIDDLE_COUNT, 0},
                                                                                      {MIDDLE_COUNT, Machine::GamePort::MAXIMUM_OHMS},
                                                                                      {SMALL_CENTER, Machine::GamePort::MAXIMUM_OHMS / 2},
                                                                                      {MIDDLE_COUNT, Machine::GamePort::AXIS_DISCONNECTED}};
    for (const auto& [center, ohms] : positions)
    {
      Poke(rig, DS.joystickCenterX, center);
      Poke(rig, DS.joystickCenterY, center);
      stick.SetAxisResistance(0, ohms);
      stick.SetAxisResistance(1, ohms);
      read();
    }
    rig.AssertAllAgreed(READ_STEERING, calls);
  }

  // The keyboard's rates: a key that changes starts its ramp again, and a key held on ramps on; the rates stop at 23 either way;
  // with recentring a key against the rate stops it at once, and with damping an idle axis comes back towards 0 by 3 a frame.
  TEST_METHOD(ReadSteeringAgreesOnTheKeyboardsRates)
  {
    ComparisonRig rig("ReadSteeringKeyboard");
    struct Frame
    {
      bool up;
      bool down;
      bool left;
      bool right;
      std::uint8_t lastRoll; // the key the ramps went on from: -1, 0 or 1
      std::uint8_t lastPitch;
      std::uint8_t rollRamp;
      std::uint8_t pitchRamp;
      std::uint8_t rollRate;
      std::uint8_t pitchRate;
      std::uint8_t recenter;
      std::uint8_t damping;
    };
    const Frame frames[] = {
      {true, false, true, false, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0, 0},
      {false, true, false, true, 0x01, 0x01, 0x05, 0x05, 0x14, 0xEC, 0, 0},
      {true, false, true, false, 0xFF, 0xFF, 0xFB, 0xFB, 0xEC, 0x14, 0, 0},
      {false, true, true, false, 0xFF, 0x01, 0xFB, 0x05, 0x05, 0x05, 1, 0},
      {false, false, false, false, 0x00, 0x00, 0x00, 0x00, 0xFB, 0xFB, 0, 1},
    };
    Poke(rig, DS.inputDevice, 0);
    for (const Frame& frame : frames)
    {
      Poke(rig, DS.keyDownUp, frame.up ? 1 : 0);
      Poke(rig, DS.keyDownDown, frame.down ? 1 : 0);
      Poke(rig, DS.keyDownLeft, frame.left ? 1 : 0);
      Poke(rig, DS.keyDownRight, frame.right ? 1 : 0);
      Poke(rig, DS.keyboardLastRollKey, frame.lastRoll);
      Poke(rig, DS.keyboardLastPitchKey, frame.lastPitch);
      Poke(rig, DS.keyboardRollRamp, frame.rollRamp);
      Poke(rig, DS.keyboardPitchRamp, frame.pitchRamp);
      Poke(rig, DS.keyboardRollRate, frame.rollRate);
      Poke(rig, DS.keyboardPitchRate, frame.pitchRate);
      Poke(rig, DS.keyboardRecenter, frame.recenter);
      Poke(rig, DS.keyboardDamping, frame.damping);
      rig.Call(READ_STEERING, {});
    }
    rig.AssertAllAgreed(READ_STEERING, std::size(frames));
  }

  // The last code in the buffer, with Shift, and the read pointer's wrap.
  TEST_METHOD(GetKeyAgreesAcrossTheBufferWrap)
  {
    ComparisonRig rig("GetKey");
    Poke(rig, DS.keyBufferCount, 1);
    Poke(rig, DS.keyBufferRead, LAST_BUFFER_BYTE);
    rig.Host().Ram().Write8(Elite::DataSegment(rig.Program()), LAST_BUFFER_BYTE, 0x9E);
    rig.Call(GET_KEY, {.ax = 0x1281});
    rig.AssertAllAgreed(GET_KEY, 1);
  }

  // The int 33h vector unset, at the ROM's IRET (no driver), and at code that is not an IRET.
  TEST_METHOD(IsMouseDriverInstalledAgreesOnTheVector)
  {
    ComparisonRig rig("IsMouseDriverInstalled");
    Machine::Memory& memory = rig.Host().Ram();
    const std::uint16_t offset = memory.Read16(0, MOUSE_VECTOR_OFFSET);
    const std::uint16_t segment = memory.Read16(0, MOUSE_VECTOR_SEGMENT);
    rig.Call(IS_MOUSE_DRIVER_INSTALLED, {.ax = 0x1111, .bx = 0x2222});
    memory.Write16(0, MOUSE_VECTOR_OFFSET, 0);
    memory.Write16(0, MOUSE_VECTOR_SEGMENT, 0);
    rig.Call(IS_MOUSE_DRIVER_INSTALLED, {.ax = 0x1111, .bx = 0x2222});
    memory.Write16(0, MOUSE_VECTOR_OFFSET, IS_MOUSE_DRIVER_INSTALLED);
    memory.Write16(0, MOUSE_VECTOR_SEGMENT, rig.Program().loadSegment);
    rig.Call(IS_MOUSE_DRIVER_INSTALLED, {.ax = 0x1111, .bx = 0x2222});
    memory.Write16(0, MOUSE_VECTOR_OFFSET, offset);
    memory.Write16(0, MOUSE_VECTOR_SEGMENT, segment);
    rig.AssertAllAgreed(IS_MOUSE_DRIVER_INSTALLED, 3);
  }

  // The IBM stick counted about the middle and at one end, with its first button down, and absent: a time-out.
  TEST_METHOD(ReadJoystickAxesAgreesOnCountsAndTimeOut)
  {
    ComparisonRig rig("ReadJoystickAxes");
    Machine::GamePort& stick = rig.Host().Joystick();
    Poke(rig, DS.fireLatch, 0);
    const std::initializer_list<std::uint32_t> positions = {Machine::GamePort::MAXIMUM_OHMS / 2, 0, Machine::GamePort::AXIS_DISCONNECTED};
    for (const std::uint32_t ohms : positions)
    {
      // Both axes alike: with X shorter, the wait for Y would idle, and that call could not be compared.
      stick.SetAxisResistance(0, ohms);
      stick.SetAxisResistance(1, ohms);
      stick.SetButton(0, ohms == 0);
      rig.Call(READ_JOYSTICK_AXES, {.ax = 0x00A5});
    }
    stick.SetButton(0, false);
    rig.AssertAllAgreed(READ_JOYSTICK_AXES, positions.size());
  }

  // The Amstrad's keys ramped, held and changed, at either limit; then the IBM stick about centres that put it on either side,
  // in the dead zone, saturated, and dividing by a centre of 0 into the trap; then absent.
  TEST_METHOD(ReadJoystickSteeringAgreesOnBothSticks)
  {
    ComparisonRig rig("ReadJoystickSteering");
    std::uint64_t calls = 0;
    const auto read = [&]()
    {
      rig.Call(READ_JOYSTICK_STEERING, {.bx = 0x1234, .cx = 0x5678, .dx = 0x9ABC});
      ++calls;
    };
    Poke(rig, DS.joystickIsAmstrad, 1);
    struct Keys
    {
      std::uint8_t up;
      std::uint8_t down;
      std::uint8_t left;
      std::uint8_t right;
      std::uint8_t rollRamp;
      std::uint8_t pitchRamp;
    };
    const Keys keys[] = {
      {1, 0, 1, 0, 5, 5},       {1, 0, 1, 0, 5, 5},       {1, 0, 1, 0, 0xE9, 0xE9},
      {0, 1, 0, 1, 0x10, 0x10}, {0, 1, 0, 1, 0x17, 0x17}, {0, 0, 0, 0, 3, 3},
    };
    for (const Keys& held : keys)
    {
      Poke(rig, DS.keyDownAmstradUp, held.up);
      Poke(rig, DS.keyDownAmstradDown, held.down);
      Poke(rig, DS.keyDownAmstradLeft, held.left);
      Poke(rig, DS.keyDownAmstradRight, held.right);
      Poke(rig, DS.amstradStickRollRamp, held.rollRamp);
      Poke(rig, DS.amstradStickPitchRamp, held.pitchRamp);
      read();
    }
    Poke(rig, DS.joystickIsAmstrad, 0);
    Machine::GamePort& stick = rig.Host().Joystick();
    stick.SetAxisResistance(0, Machine::GamePort::MAXIMUM_OHMS / 2);
    stick.SetAxisResistance(1, Machine::GamePort::MAXIMUM_OHMS / 2);
    const std::uint16_t centers[][2] = {{40, 2000}, {2000, 40}, {1, 0}, {0, 1}, {70, 74}, {78, 0x7FFF}, {0xFFFF, 0x8000}};
    for (const auto& center : centers)
    {
      Poke(rig, DS.joystickCenterX, center[0]);
      Poke(rig, DS.joystickCenterY, center[1]);
      read();
    }
    stick.SetAxisResistance(0, Machine::GamePort::AXIS_DISCONNECTED);
    stick.SetAxisResistance(1, Machine::GamePort::AXIS_DISCONNECTED);
    read();
    rig.AssertAllAgreed(READ_JOYSTICK_STEERING, calls);
  }

  // With no driver loaded int 33h is the ROM's IRET, so the motion is what CX and DX held: either way, saturated, at the
  // limits of the rates and snapped from 1 and -1; and the Amstrad's mouse buttons by their key codes.
  TEST_METHOD(ReadMouseSteeringAgreesOnMotionAndButtons)
  {
    ComparisonRig rig("ReadMouseSteering");
    struct Motion
    {
      std::uint16_t across;
      std::uint16_t down;
      std::uint8_t rollRate;
      std::uint8_t pitchRate;
      std::uint8_t amstrad;
      std::uint8_t left;
      std::uint8_t right;
    };
    const Motion motions[] = {
      {0x0100, 0xFF00, 0, 0, 0, 0, 0},       {0x0300, 0x0300, 0x10, 0xF0, 0, 0, 0}, {0xFD00, 0x0000, 0x1F, 0x01, 0, 0, 0},
      {0x8000, 0x7FFF, 0xEC, 0x14, 0, 0, 0}, {0x0010, 0xFFF0, 0, 0, 1, 1, 0},       {0x0008, 0xFFF8, 0xFF, 0x01, 1, 0, 1},
      {0x0000, 0xFD00, 0, 0, 0, 0, 0},
    };
    for (const Motion& motion : motions)
    {
      Poke(rig, DS.rollRate, static_cast<std::uint16_t>((motion.pitchRate << 8) | motion.rollRate));
      Poke(rig, DS.amstradPresent, motion.amstrad);
      Poke(rig, DS.keyDownAmstradMouseLeft, motion.left);
      Poke(rig, DS.keyDownAmstradMouseRight, motion.right);
      Poke(rig, DS.fireLatch, 0);
      rig.Call(READ_MOUSE_STEERING, {.ax = 0x4444, .bx = 0x5555, .cx = motion.across, .dx = motion.down});
    }
    Poke(rig, DS.amstradPresent, 0);
    rig.AssertAllAgreed(READ_MOUSE_STEERING, std::size(motions));
  }

  // The mouse through ReadSteering, its one caller: with no driver loaded int 33h is the ROM's IRET, so the call is compared.
  TEST_METHOD(ReadSteeringAgreesWithTheMouse)
  {
    ComparisonRig rig("ReadSteeringMouse");
    Poke(rig, DS.inputDevice, 2);
    Poke(rig, DS.amstradPresent, 0);
    rig.Call(READ_STEERING, {.cx = 0x0040, .dx = 0xFFC0});
    Poke(rig, DS.inputDevice, 0);
    rig.AssertAllAgreed(READ_STEERING, 1);
  }

  // The stick read natively, the native twin's calls not compared: its read counts the polls the interpreted one does, by the
  // cycles it counts for the original's instructions, and waits where the interpreted one waits.
  TEST_METHOD(IbmStickSteersTheChartNatively)
  {
    TwinRig rig("TwinStickNative", {.compared = false});
    SteerTheChartWithTheStick(rig);
  }

  // The same with every call of a work routine compared, ReadSteering's among them.
  TEST_METHOD(IbmStickSteersTheChartCompared)
  {
    TwinRig rig("TwinStickCompared");
    SteerTheChartWithTheStick(rig);
  }

  // The mask mission's briefing, which the docked status screen shows first when F9 brings it back from another screen, waits in
  // WaitForKeyPress until space: the routine that waits, run natively, gives the interpreted run's digests while it waits and
  // after.
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

  TEST_METHOD(ApplyReverseControlsAgreesWithEveryOption)
  {
    ComparisonRig rig("ApplyReverseControls");
    std::uint64_t calls = 0;
    for (const std::uint8_t reverseY : Bytes{0, 1})
    {
      for (const std::uint8_t reverseBoth : Bytes{0, 1})
      {
        Poke(rig, DS.reverseYControl, reverseY);
        Poke(rig, DS.reverseXAndY, reverseBoth);
        rig.Call(APPLY_REVERSE_CONTROLS, {.ax = 0x05FB});
        rig.Call(APPLY_REVERSE_CONTROLS_TO_DX, {.dx = 0x7F80});
        ++calls;
      }
    }
    rig.AssertAllAgreed(APPLY_REVERSE_CONTROLS, calls);
    rig.AssertAllAgreed(APPLY_REVERSE_CONTROLS_TO_DX, calls);
  }

  // Flight steered by the mouse, with the machine's own driver loaded (ADR-008 item 8, D20): M at the disc menu, which takes the
  // mouse only with a driver; the launch; the mouse moved right and down, then further left and up, which ReadMouseSteering turns
  // into rates that stay until it moves again; and the left button held, which fires the laser. Uncompared: each read of the
  // mouse calls int 33h, which leaves a compared call the original's outcome (ADR-010 item 4), so only here does the native
  // code of the reads run.
  TEST_METHOD(MouseSteersFlight)
  {
    TwinRig rig("TwinMouseFlight", {.compared = false, .mousePresent = true});
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
