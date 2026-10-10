#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"

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
constexpr std::uint16_t READ_FIRE_BUTTON = 0x74E0;
constexpr std::uint16_t READ_STEERING = 0x7536;
constexpr std::uint16_t GET_KEY = 0x7616;
constexpr std::uint16_t APPLY_REVERSE_CONTROLS = 0x8EA5;
constexpr std::uint16_t APPLY_REVERSE_CONTROLS_TO_DX = 0x8EBA;

constexpr std::uint8_t ALT = 0x38;
constexpr std::uint8_t LEFT_SHIFT = 0x2A;
constexpr std::uint8_t RIGHT_SHIFT = 0x36;
constexpr std::uint8_t BREAK = 0x80;
constexpr std::uint16_t LAST_BUFFER_BYTE = 0xA0BF; // keyBuffer's 16th
constexpr std::uint16_t MIDDLE_COUNT = 60100;      // ReadJoystickAxes counts up from 60000
constexpr std::uint16_t SMALL_CENTER = 100;

void Poke(ComparisonRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  _rig.Host().Ram().Write8(Elite::DataSegment(_rig.Program()), _field.offset, _value);
}

void Poke(ComparisonRig& _rig, Elite::DataField<std::uint16_t> _field, std::uint16_t _value)
{
  _rig.Host().Ram().Write16(Elite::DataSegment(_rig.Program()), _field.offset, _value);
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

  // The Amstrad's stick and mouse by their key codes, and the IBM stick's two buttons.
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
    rig.AssertAllAgreed(READ_FIRE_BUTTON, calls);
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
};

} // namespace GameLogicTests
