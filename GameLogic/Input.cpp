#include "pch.h"

#include "Input.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t SAVE_SCREENSHOT = 0x01B7;
constexpr std::uint16_t READ_JOYSTICK_STEERING = 0x77C1;
constexpr std::uint16_t READ_MOUSE_STEERING = 0x797E;

constexpr std::uint16_t KEYBOARD_DATA_PORT = 0x60;
constexpr std::uint16_t KEYBOARD_CONTROL_PORT = 0x61;
constexpr std::uint16_t PIC_COMMAND_PORT = 0x20;
constexpr std::uint16_t GAME_PORT = 0x201;
constexpr std::uint8_t KEYBOARD_ACKNOWLEDGE = 0x80; // the XT's PB7 pulse
constexpr std::uint8_t END_OF_INTERRUPT = 0x20;
constexpr std::uint8_t MOUSE_VECTOR = 0x33;
constexpr std::uint16_t MOUSE_BUTTON_PRESSES = 5;
constexpr std::uint16_t MOUSE_LEFT_BUTTON = 1;

constexpr std::uint8_t OVERRUN_CODE = 0xFF;
constexpr std::uint8_t BREAK_BIT = 0x80;
constexpr std::uint8_t SHIFT_BIT = 0x80; // on a code in keyBuffer
constexpr std::uint8_t ESC_CODE = 0x01;
constexpr std::uint8_t W_CODE = 0x11;
constexpr std::uint8_t D_CODE = 0x20;
constexpr std::uint8_t KEY_BUFFER_CODES = 0x10;
constexpr std::size_t KEY_DOWN_WORDS = 0x40;

constexpr std::uint8_t KEYBOARD_DEVICE = 0;
constexpr std::uint8_t JOYSTICK_DEVICE = 1;
constexpr std::uint8_t MOUSE_DEVICE = 2;
constexpr std::uint8_t STICK_BUTTON_A = 0x20; // low while pressed
constexpr std::uint8_t STICK_BUTTON_B = 0x10;

constexpr std::int8_t MOST_RATE = 23;             // either way
constexpr std::uint8_t MOST_NEGATIVE_RATE = 0xE9; // -23
constexpr std::uint8_t RATE_DECAY_STEPS = 3;

[[nodiscard]] std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word);
}

[[nodiscard]] std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

[[nodiscard]] std::uint16_t WithLow(std::uint16_t _word, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_word & 0xFF00) | _low);
}

[[nodiscard]] std::uint16_t WithHigh(std::uint16_t _word, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>((_word & 0x00FF) | (_high << 8));
}

[[nodiscard]] std::uint16_t Pair(std::uint8_t _high, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_high << 8) | _low);
}

[[nodiscard]] std::uint8_t Negate(std::uint8_t _value) noexcept
{
  return static_cast<std::uint8_t>(0u - _value);
}

// SHR AL,1 of a button byte: CF is the button, AL the rest.
void ShiftOutButton(Guest& _guest, std::uint8_t _buttons)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(_buttons >> 1));
  _guest.SetFlag(Machine::FLAG_CARRY, (_buttons & 1) != 0);
}

// SaveScreenshot (CS:01B7) while Alt and PrtSc are held, with every register but ES and the flags kept around it.
void SaveScreenshotIfAsked(Guest& _guest)
{
  if (_guest.Get(DS.keyDownPrintScreen) != 1 || _guest.Get(DS.keyDownAlt) != 1)
  {
    return;
  }
  Machine::Registers& regs = _guest.Regs();
  const Machine::Registers kept = regs;
  _guest.Call(SAVE_SCREENSHOT);
  regs.ax = kept.ax;
  regs.bx = kept.bx;
  regs.cx = kept.cx;
  regs.dx = kept.dx;
  regs.bp = kept.bp;
  regs.di = kept.di;
  regs.si = kept.si;
}

// One axis of ApplyKeyboardRates (0x7543): a new key adds to the rate, within +-23, or with keyboardRecenter snaps
// an opposite rate to 0; no key lets keyboardDamping take up to 3 off it.
[[nodiscard]] std::uint8_t IntegrateRate(Guest& _guest, std::uint8_t _input, DataField<std::uint8_t> _rate)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t rate = _guest.Get(_rate);
  if (_input == 0)
  {
    std::uint8_t value = rate;
    if (_guest.Get(DS.keyboardDamping) != 1 || value == 0)
    {
      return value;
    }
    const bool negative = (value & 0x80) != 0;
    for (std::uint8_t step = 0; step < RATE_DECAY_STEPS && value != 0; ++step)
    {
      value = static_cast<std::uint8_t>(negative ? value + 1 : value - 1);
    }
    return value;
  }
  if (_guest.Get(DS.keyboardRecenter) == 1 && rate != 0)
  {
    regs.bx = WithLow(regs.bx, static_cast<std::uint8_t>(_input ^ rate));
    if ((Low(regs.bx) & 0x80) != 0)
    {
      return 0;
    }
  }
  const auto value = static_cast<std::int8_t>(_input + rate);
  if (value > MOST_RATE)
  {
    return static_cast<std::uint8_t>(MOST_RATE);
  }
  if (value < -MOST_RATE)
  {
    return MOST_NEGATIVE_RATE;
  }
  return static_cast<std::uint8_t>(value);
}

// ApplyKeyboardRates (0x7543): AL and AH, as keys, integrated into keyboardRollRate and keyboardPitchRate.
void ApplyKeyboardRates(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t roll = IntegrateRate(_guest, Low(regs.ax), DS.keyboardRollRate);
  const std::uint8_t pitch = IntegrateRate(_guest, High(regs.ax), DS.keyboardPitchRate);
  regs.ax = Pair(pitch, roll);
  _guest.SetWord(DS.keyboardRollRate.offset, regs.ax);
}

// One axis of ReadKeyboardSteering: the ramp grows while the same key is held, inside +-23, and restarts on a change.
void RampAxis(Guest& _guest, std::uint8_t _key, DataField<std::uint8_t> _lastKey, DataField<std::uint8_t> _ramp)
{
  const bool held = _key == _guest.Get(_lastKey);
  _guest.Set(_lastKey, _key);
  if (!held)
  {
    _guest.Set(_ramp, 0);
    return;
  }
  const auto value = static_cast<std::uint8_t>(_key + _guest.Get(_ramp));
  const auto signedValue = static_cast<std::int8_t>(value);
  if (signedValue > -MOST_RATE - 1 && signedValue < MOST_RATE + 1)
  {
    _guest.Set(_ramp, value);
  }
}

} // namespace

void KeyboardInterrupt(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t ax = regs.ax;
  const std::uint16_t ds = regs.ds;
  const std::uint16_t es = regs.es;
  regs.es = Guest::VIDEO_SEGMENT;
  regs.ax = _guest.DataSegment();
  regs.ds = _guest.DataSegment();
  ReadScanCode(_guest);
  regs.es = es;
  regs.ds = ds;
  regs.ax = ax;
}

void ReadScanCode(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t scan = _guest.In8(KEYBOARD_DATA_PORT);
  const std::uint8_t control = _guest.In8(KEYBOARD_CONTROL_PORT);
  _guest.Out8(KEYBOARD_CONTROL_PORT, static_cast<std::uint8_t>(control | KEYBOARD_ACKNOWLEDGE));
  _guest.Out8(KEYBOARD_CONTROL_PORT, static_cast<std::uint8_t>(control & ~KEYBOARD_ACKNOWLEDGE));
  regs.ax = 0;
  if (scan != OVERRUN_CODE)
  {
    if ((scan & BREAK_BIT) != 0)
    {
      const auto code = static_cast<std::uint8_t>(scan & ~BREAK_BIT);
      _guest.SetByte(DS.keyDown.At(code), 0);
      if (code == D_CODE && _guest.Get(DS.inFlight) == 1)
      {
        _guest.Set(DS.dockingKeyReleased, 1);
      }
    }
    else
    {
      _guest.Set(DS.anyKeyLatch, 1);
      if (scan == ESC_CODE)
      {
        _guest.Set(DS.escKeyLatch, 1);
      }
      if (scan == W_CODE && _guest.Get(DS.keyDownAlt) == 1)
      {
        _guest.Set(DS.forceMisjump, 1);
      }
      _guest.SetByte(DS.keyDown.At(scan), 1);
      std::uint8_t code = scan;
      if (_guest.Get(DS.keyDownLeftShift) == 1 || _guest.Get(DS.keyDownRightShift) == 1)
      {
        code |= SHIFT_BIT;
      }
      const std::uint8_t count = _guest.Get(DS.keyBufferCount);
      regs.ax = Pair(count, code);
      if (count != KEY_BUFFER_CODES)
      {
        const std::uint16_t write = _guest.Get(DS.keyBufferWrite);
        _guest.SetByte(write, code);
        const auto next = static_cast<std::uint16_t>(write + 1);
        _guest.Set(DS.keyBufferWrite, (next & 0x0F) == 0 ? DS.keyBuffer.offset : next);
        _guest.Set(DS.keyBufferCount, static_cast<std::uint8_t>(count + 1));
      }
    }
  }
  regs.ax = WithLow(regs.ax, END_OF_INTERRUPT);
  _guest.Out8(PIC_COMMAND_PORT, END_OF_INTERRUPT);
}

void ReadFireButton(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t device = _guest.Get(DS.inputDevice);
  if (device == KEYBOARD_DEVICE)
  {
    ShiftOutButton(_guest, _guest.Get(DS.keyDownSpace));
    return;
  }
  if (device == JOYSTICK_DEVICE)
  {
    if (_guest.Get(DS.joystickIsAmstrad) == 1)
    {
      ShiftOutButton(_guest, static_cast<std::uint8_t>(_guest.Get(DS.keyDownAmstradFire1) | _guest.Get(DS.keyDownAmstradFire2)));
      return;
    }
    // Shifted left until each button falls into CF, inverted: pressed reads 0.
    regs.dx = GAME_PORT;
    const std::uint8_t buttons = _guest.In8(GAME_PORT);
    if ((buttons & STICK_BUTTON_A) == 0)
    {
      regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(buttons << 3));
      _guest.SetFlag(Machine::FLAG_CARRY, true);
      return;
    }
    regs.ax = WithLow(regs.ax, static_cast<std::uint8_t>(buttons << 4));
    _guest.SetFlag(Machine::FLAG_CARRY, (buttons & STICK_BUTTON_B) == 0);
    return;
  }
  if (_guest.Get(DS.amstradPresent) == 1)
  {
    ShiftOutButton(_guest, static_cast<std::uint8_t>(_guest.Get(DS.keyDownAmstradMouseRight) | _guest.Get(DS.keyDownAmstradMouseLeft)));
    return;
  }
  regs.ax = MOUSE_BUTTON_PRESSES;
  regs.bx = MOUSE_LEFT_BUTTON;
  _guest.Interrupt(MOUSE_VECTOR);
  const std::uint8_t buttons = Low(regs.ax);
  ShiftOutButton(_guest, buttons);
  if ((buttons & 1) == 0)
  {
    ShiftOutButton(_guest, static_cast<std::uint8_t>(buttons >> 1));
  }
}

void ReadSteering(Guest& _guest)
{
  const std::uint8_t device = _guest.Get(DS.inputDevice);
  if (device == KEYBOARD_DEVICE)
  {
    ReadKeyboardSteering(_guest);
    ApplyKeyboardRates(_guest);
    return;
  }
  if (device == JOYSTICK_DEVICE)
  {
    _guest.Call(READ_JOYSTICK_STEERING);
    if (_guest.Get(DS.joystickIsAmstrad) == 1)
    {
      ApplyKeyboardRates(_guest);
    }
    return;
  }
  _guest.Call(READ_MOUSE_STEERING);
}

void GetKey(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.SetFlag(Machine::FLAG_INTERRUPT, false);
  // PUSH BX / POP BX, for the dead stack: callers wait in loops around GetKey, and a compared run only sees such a
  // loop wait when each turn leaves memory as the last did (ADR-008).
  _guest.Push(regs.bx);
  regs.bx = _guest.Pop();
  regs.ax = WithHigh(regs.ax, 0);
  const std::uint8_t count = _guest.Get(DS.keyBufferCount);
  if (count != 0)
  {
    const std::uint16_t read = _guest.Get(DS.keyBufferRead);
    const std::uint8_t code = _guest.Byte(read);
    const auto next = static_cast<std::uint16_t>(read + 1);
    _guest.Set(DS.keyBufferRead, (next & 0x0F) == 0 ? DS.keyBuffer.offset : next);
    _guest.Set(DS.keyBufferCount, static_cast<std::uint8_t>(count - 1));
    // ROL AX,1 then SHR AH,1: the Shift bit moves into AL's bit 0.
    const std::uint16_t word = Pair(code, Low(regs.ax));
    const auto rotated = static_cast<std::uint16_t>((word << 1) | (word >> 15));
    regs.ax = Pair(static_cast<std::uint8_t>(High(rotated) >> 1), Low(rotated));
  }
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);
  SaveScreenshotIfAsked(_guest);
  _guest.SetFlag(Machine::FLAG_ZERO, High(regs.ax) == 0);
}

void ResetKeyboard(Guest& _guest)
{
  _guest.SetFlag(Machine::FLAG_INTERRUPT, false);
  for (std::size_t word = 0; word < KEY_DOWN_WORDS; ++word)
  {
    _guest.SetWord(DS.keyDown.At(word * 2), 0);
  }
  _guest.Set(DS.keyBufferCount, 0);
  _guest.Set(DS.keyBufferWrite, DS.keyBuffer.offset);
  _guest.Set(DS.keyBufferRead, DS.keyBuffer.offset);
  _guest.Set(DS.rollRate, 0);
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);
}

void ReadKeyboardSteering(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  std::uint8_t roll = 0;
  std::uint8_t pitch = 0;
  const auto held = [&](DataField<std::uint8_t> _first, DataField<std::uint8_t> _second)
  {
    regs.bx = WithLow(regs.bx, static_cast<std::uint8_t>(_guest.Get(_first) | _guest.Get(_second)));
    return Low(regs.bx) != 0;
  };
  if (held(DS.keyDownUp, DS.keyDownQ))
  {
    --pitch;
  }
  if (held(DS.keyDownDown, DS.keyDownA))
  {
    ++pitch;
  }
  if (held(DS.keyDownLeft, DS.keyDownO))
  {
    --roll;
  }
  if (held(DS.keyDownRight, DS.keyDownP))
  {
    ++roll;
  }
  RampAxis(_guest, roll, DS.keyboardLastRollKey, DS.keyboardRollRamp);
  RampAxis(_guest, pitch, DS.keyboardLastPitchKey, DS.keyboardPitchRamp);
  regs.ax = Pair(Negate(_guest.Get(DS.keyboardPitchRamp)), _guest.Get(DS.keyboardRollRamp));
}

void PollScreenDumpKey(Guest& _guest)
{
  SaveScreenshotIfAsked(_guest);
}

void ResetMouseIfSelected(Guest& _guest)
{
  if (_guest.Get(DS.inputDevice) != MOUSE_DEVICE)
  {
    return;
  }
  _guest.Regs().ax = 0;
  _guest.Interrupt(MOUSE_VECTOR);
}

void ApplyReverseControls(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.reverseYControl) == 1)
  {
    regs.ax = WithHigh(regs.ax, Negate(High(regs.ax)));
  }
  if (_guest.Get(DS.reverseXAndY) == 1)
  {
    regs.ax = Pair(Negate(High(regs.ax)), Negate(Low(regs.ax)));
  }
}

void ApplyReverseControlsToDx(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.reverseYControl) == 1)
  {
    regs.dx = WithHigh(regs.dx, Negate(High(regs.dx)));
  }
  if (_guest.Get(DS.reverseXAndY) == 1)
  {
    regs.dx = Pair(Negate(High(regs.dx)), Negate(Low(regs.dx)));
  }
}

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_INTERRUPT;
using Machine::FLAG_ZERO;
using Machine::REGISTER_AX;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DX;

constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract CLOBBERS_BX{REGISTER_BX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX{REGISTER_AX | REGISTER_BX, 0};
constexpr Machine::NativeContract CLOBBERS_BX_CX_DX{REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract FIRE_BUTTON{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, FLAG_CARRY};
constexpr Machine::NativeContract KEY{0, FLAG_ZERO | FLAG_INTERRUPT};

constexpr std::array ENTRIES = {
  NativeEntry{0x0201, "KeyboardInterrupt", &KeyboardInterrupt, PRESERVES_ALL, Machine::NativeReturn::Interrupt},
  NativeEntry{0x7443, "ReadScanCode", &ReadScanCode, CLOBBERS_AX},
  NativeEntry{0x74E0, "ReadFireButton", &ReadFireButton, FIRE_BUTTON},
  NativeEntry{0x7536, "ReadSteering", &ReadSteering, CLOBBERS_BX_CX_DX},
  NativeEntry{0x7616, "GetKey", &GetKey, KEY},
  NativeEntry{0x7668, "ResetKeyboard", &ResetKeyboard, PRESERVES_ALL},
  NativeEntry{0x78EF, "ReadKeyboardSteering", &ReadKeyboardSteering, CLOBBERS_BX},
  NativeEntry{0x7F3D, "PollScreenDumpKey", &PollScreenDumpKey, PRESERVES_ALL},
  NativeEntry{0x7F5D, "ResetMouseIfSelected", &ResetMouseIfSelected, CLOBBERS_AX_BX},
  NativeEntry{0x8EA5, "ApplyReverseControls", &ApplyReverseControls, PRESERVES_ALL},
  NativeEntry{0x8EBA, "ApplyReverseControlsToDx", &ApplyReverseControlsToDx, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> InputEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
