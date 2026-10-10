#include "pch.h"

#include "Input.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Maths.h"

#include <utility>

namespace Elite
{

namespace
{

constexpr std::uint16_t SAVE_SCREENSHOT = 0x01B7;
constexpr std::uint16_t WAIT_FOR_KEY_PRESS = 0x6DEC;
constexpr std::uint16_t GET_KEY = 0x7616;

constexpr std::uint16_t KEYBOARD_DATA_PORT = 0x60;
constexpr std::uint16_t KEYBOARD_CONTROL_PORT = 0x61;
constexpr std::uint16_t PIC_COMMAND_PORT = 0x20;
constexpr std::uint16_t GAME_PORT = 0x201;
constexpr std::uint8_t KEYBOARD_ACKNOWLEDGE = 0x80; // the XT's PB7 pulse
constexpr std::uint8_t END_OF_INTERRUPT = 0x20;
constexpr std::uint8_t MOUSE_VECTOR = 0x33;
constexpr std::uint16_t MOUSE_BUTTON_PRESSES = 5;
constexpr std::uint16_t MOUSE_MOTION = 0x0B;
constexpr std::uint16_t MOUSE_LEFT_BUTTON = 1;
constexpr std::uint16_t MOUSE_VECTOR_OFFSET = MOUSE_VECTOR * 4; // in the interrupt table at 0000:0000
constexpr std::uint8_t IRET_OPCODE = 0xCF;
constexpr std::uint8_t MOST_MOUSE_STEP = 0x3F; // mickeys / 8, either way
constexpr std::uint16_t MOUSE_STEP_LIMIT = 0x40;

// ReadJoystickAxes counts its polls up from 60000, so that a count that wraps is a time-out after 5536.
constexpr std::uint16_t AXIS_COUNT_START = 0xEA60;
constexpr std::uint8_t X_AXIS = 0x01;
constexpr std::uint8_t Y_AXIS = 0x02;
constexpr std::uint8_t BOTH_AXES = X_AXIS | Y_AXIS;
constexpr std::uint16_t STICK_STEP_LIMIT = 0x80; // (count - centre) * 128 / centre saturates at 127
constexpr std::uint8_t MOST_STICK_STEP = 0x7F;
constexpr std::uint8_t STICK_DEAD_ZONE = 4; // after the step's division by 8

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

// The end of a waiting loop's turn at the original's backward jump to CS:_target, with IP there as paced time sees it there:
// two jumps back to different places are then two turns, as they are in the original.
void JumpBack(Guest& _guest, std::uint16_t _target)
{
  _guest.Regs().ip = _target;
  _guest.LoopTurn();
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
  regs.ax = Join(pitch, roll);
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

// ReadJoystickSteering's Amstrad stick (787D): its four key codes ramped as the keyboard's cursor keys are.
void ReadAmstradStick(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  std::uint8_t roll = 0;
  std::uint8_t pitch = 0;
  if (_guest.Get(DS.keyDownAmstradUp) == 1)
  {
    --pitch;
  }
  if (_guest.Get(DS.keyDownAmstradDown) == 1)
  {
    ++pitch;
  }
  if (_guest.Get(DS.keyDownAmstradLeft) == 1)
  {
    --roll;
  }
  if (_guest.Get(DS.keyDownAmstradRight) == 1)
  {
    ++roll;
  }
  RampAxis(_guest, roll, DS.amstradStickLastRoll, DS.amstradStickRollRamp);
  RampAxis(_guest, pitch, DS.amstradStickLastPitch, DS.amstradStickPitchRamp);
  regs.ax = Join(Negate(_guest.Get(DS.amstradStickPitchRamp)), _guest.Get(DS.amstradStickRollRamp));
}

// One axis of ReadJoystickSteering, on the count in AX: (count - centre) * 256 / centre by a DIV that can overflow into the
// game's trap, halved and saturated at 127, over 8, less a dead zone of 4, with the sign put back. Out: AL; AH as the halving
// left it, DX as the DIV did.
void StickAxis(Guest& _guest, DataField<std::uint16_t> _center)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = static_cast<std::uint16_t>(regs.ax - _guest.Get(_center));
  const bool negative = (regs.ax & 0x8000) != 0;
  if (negative)
  {
    regs.ax = Negate(regs.ax);
  }
  // XOR DH,DH / MOV DL,AH / MOV AH,AL / XOR AL,AL: DX:AX = the difference * 256.
  regs.dx = High(regs.ax);
  regs.ax = Join(Low(regs.ax), 0);
  DivideWord(_guest, _guest.Get(_center));
  regs.ax = static_cast<std::uint16_t>(regs.ax >> 1);
  if (regs.ax >= STICK_STEP_LIMIT)
  {
    SetLow(regs.ax, MOST_STICK_STEP);
  }
  const auto step = static_cast<std::uint8_t>(Low(regs.ax) >> 3);
  const std::uint8_t value = step >= STICK_DEAD_ZONE ? static_cast<std::uint8_t>(step - STICK_DEAD_ZONE) : std::uint8_t{0};
  SetLow(regs.ax, negative ? Negate(value) : value);
}

// ClampJoystickSteering (7A3C) on one byte: within -23..23.
[[nodiscard]] std::uint8_t ClampRate(std::uint8_t _rate) noexcept
{
  const auto value = static_cast<std::int8_t>(_rate);
  if (value < -MOST_RATE)
  {
    return MOST_NEGATIVE_RATE;
  }
  if (value > MOST_RATE)
  {
    return static_cast<std::uint8_t>(MOST_RATE);
  }
  return _rate;
}

// One axis of ReadMouseSteering: a motion counter over 8, saturated at 63 either way. AH is as the shift left it.
[[nodiscard]] std::uint16_t MouseAxis(std::uint16_t _mickeys) noexcept
{
  const bool negative = (_mickeys & 0x8000) != 0;
  auto value = static_cast<std::uint16_t>((negative ? Negate(_mickeys) : _mickeys) >> 3);
  if (value >= MOUSE_STEP_LIMIT)
  {
    value = WithLow(value, MOST_MOUSE_STEP);
  }
  return negative ? WithLow(value, Negate(Low(value))) : value;
}

// One axis of ReadMouseSteering's rate: the step, halved, added to the rate the last frame left, within -23..23; then INC / SUB 2
// / INC, which snaps -1 and 1 to 0.
[[nodiscard]] std::uint8_t MouseRate(std::uint8_t _step, std::uint8_t _rate) noexcept
{
  auto value = static_cast<std::uint8_t>(Sar(_step, 1) + _rate);
  if (static_cast<std::int8_t>(value) > MOST_RATE)
  {
    value = static_cast<std::uint8_t>(MOST_RATE);
  }
  if (static_cast<std::int8_t>(value) < -MOST_RATE)
  {
    value = MOST_NEGATIVE_RATE;
  }
  if (value == 1 || value == 0xFF)
  {
    value = 0;
  }
  return value;
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

void IsMouseDriverInstalled(Guest& _guest)
{
  // The int 33h vector: installed when it is set and does not point at an IRET.
  const std::uint16_t offset = _guest.FarWord(0, MOUSE_VECTOR_OFFSET);
  const std::uint16_t segment = _guest.FarWord(0, static_cast<std::uint16_t>(MOUSE_VECTOR_OFFSET + 2));
  _guest.Regs().es = segment;
  const bool installed = (offset | segment) != 0 && _guest.FarByte(segment, offset) != IRET_OPCODE;
  _guest.SetFlag(Machine::FLAG_ZERO, !installed);
}

void WaitForKeyPress(Guest& _guest)
{
  // GetKey until a key comes: each empty turn ends at the original's JE back to the entry.
  for (;;)
  {
    _guest.Call(GET_KEY);
    if (!_guest.Flag(Machine::FLAG_ZERO))
    {
      return;
    }
    JumpBack(_guest, WAIT_FOR_KEY_PRESS);
  }
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
      regs.ax = Join(count, code);
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
    ReadJoystickSteering(_guest);
    if (_guest.Get(DS.joystickIsAmstrad) == 1)
    {
      ApplyKeyboardRates(_guest);
    }
    return;
  }
  ReadMouseSteering(_guest);
}

void GetKey(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.SetFlag(Machine::FLAG_INTERRUPT, false);
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
    const std::uint16_t word = Join(code, Low(regs.ax));
    const auto rotated = static_cast<std::uint16_t>((word << 1) | (word >> 15));
    regs.ax = Join(static_cast<std::uint8_t>(High(rotated) >> 1), Low(rotated));
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

void ReadJoystickAxes(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // With interrupts off, the one-shots fired, each axis's polls counted until its bit drops. The game port times its one-shots in
  // the cycles of the instructions executed, and native code executes none: run natively, a stick that answers never drops a
  // bit, and the X count times out (CF, with interrupts left off, as the original leaves them on a time-out).
  _guest.SetFlag(Machine::FLAG_INTERRUPT, false);
  regs.bx = AXIS_COUNT_START;
  regs.cx = regs.bx;
  regs.dx = GAME_PORT;
  _guest.Out8(GAME_PORT, Low(regs.ax));
  SetLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(GAME_PORT) & BOTH_AXES));
  if (Low(regs.ax) != BOTH_AXES)
  {
    _guest.SetFlag(Machine::FLAG_CARRY, true);
    return;
  }
  do
  {
    regs.bx = static_cast<std::uint16_t>(regs.bx + 1);
    if (regs.bx == 0)
    {
      _guest.SetFlag(Machine::FLAG_CARRY, true);
      return;
    }
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(GAME_PORT) & X_AXIS));
  } while (Low(regs.ax) != 0);
  // Both bits down before the one-shots fire again; this loop does not count, or time out.
  do
  {
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(GAME_PORT) & BOTH_AXES));
  } while (Low(regs.ax) != 0);
  _guest.Out8(GAME_PORT, Low(regs.ax));
  do
  {
    regs.cx = static_cast<std::uint16_t>(regs.cx + 1);
    if (regs.cx == 0)
    {
      _guest.SetFlag(Machine::FLAG_CARRY, true);
      return;
    }
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(GAME_PORT) & Y_AXIS));
  } while (Low(regs.ax) != 0);
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);
  regs.cx = static_cast<std::uint16_t>(regs.cx - AXIS_COUNT_START);
  regs.bx = static_cast<std::uint16_t>(regs.bx - AXIS_COUNT_START);
  regs.dx = GAME_PORT;
  SetLow(regs.ax, _guest.In8(GAME_PORT));
  _guest.Set(DS.joystickPortByte, Low(regs.ax));
  // Bit 4, the stick's first button, low while pressed.
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & STICK_BUTTON_B));
  if (Low(regs.ax) == 0)
  {
    _guest.Set(DS.fireLatch, 1);
  }
  _guest.SetFlag(Machine::FLAG_CARRY, false);
}

void ReadJoystickSteering(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.joystickIsAmstrad) == 1)
  {
    ReadAmstradStick(_guest);
    return;
  }
  ReadJoystickAxes(_guest);
  if (_guest.Flag(Machine::FLAG_CARRY))
  {
    regs.ax = 0;
    return;
  }
  regs.ax = regs.bx;
  StickAxis(_guest, DS.joystickCenterX);
  std::swap(regs.cx, regs.ax); // XCHG CX,AX: the roll kept, the Y count taken
  StickAxis(_guest, DS.joystickCenterY);
  regs.ax = Join(ClampRate(Negate(Low(regs.ax))), ClampRate(Low(regs.cx)));
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
  regs.ax = Join(Negate(_guest.Get(DS.keyboardPitchRamp)), _guest.Get(DS.keyboardRollRamp));
}

void ReadMouseSteering(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The motion since the last call: CX across, DX down.
  regs.ax = MOUSE_MOTION;
  _guest.Interrupt(MOUSE_VECTOR);
  regs.ax = MouseAxis(regs.cx);
  std::swap(regs.cx, regs.ax); // XCHG CX,AX
  regs.ax = MouseAxis(regs.dx);
  regs.ax = Join(Low(regs.ax), Low(regs.cx));
  const std::uint16_t steps = regs.ax; // PUSH AX / POP AX round the buttons
  if (_guest.Get(DS.amstradPresent) == 1)
  {
    SetLow(regs.ax, static_cast<std::uint8_t>((_guest.Get(DS.keyDownAmstradMouseRight) << 1) | _guest.Get(DS.keyDownAmstradMouseLeft)));
  }
  else
  {
    regs.ax = MOUSE_BUTTON_PRESSES;
    regs.bx = MOUSE_LEFT_BUTTON;
    _guest.Interrupt(MOUSE_VECTOR);
  }
  _guest.Set(DS.mouseButtons, Low(regs.ax));
  if ((Low(regs.ax) & 1) != 0)
  {
    _guest.Set(DS.fireLatch, 1);
  }
  // NEG AH / SAR AL,1 / SAR AH,1: half of each step, the pitch negated, added to the rates the last frame left.
  regs.ax = Join(MouseRate(Negate(High(steps)), _guest.Get(DS.pitchRate)), MouseRate(Low(steps), _guest.Byte(DS.rollRate.offset)));
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
    regs.ax = Join(Negate(High(regs.ax)), Negate(Low(regs.ax)));
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
    regs.dx = Join(Negate(High(regs.dx)), Negate(Low(regs.dx)));
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
// The stick's routines leave interrupts off on a time-out, which their callers live with: compared too.
constexpr Machine::NativeContract STICK_AXES{REGISTER_AX, FLAG_CARRY | FLAG_INTERRUPT};
constexpr Machine::NativeContract STICK_STEERING{REGISTER_BX | REGISTER_CX | REGISTER_DX, FLAG_INTERRUPT};

constexpr std::array ENTRIES = {
  NativeEntry{0x0201, "KeyboardInterrupt", &KeyboardInterrupt, PRESERVES_ALL, Machine::NativeReturn::Interrupt},
  NativeEntry{0x02D4, "IsMouseDriverInstalled", &IsMouseDriverInstalled, Machine::NativeContract{0, FLAG_ZERO}},
  // It waits as a rule: the mission briefings call it for the key that ends them.
  NativeEntry{0x6DEC, "WaitForKeyPress", &WaitForKeyPress, PRESERVES_ALL, Machine::NativeReturn::Near, 0, Machine::NativeWait::Always},
  NativeEntry{0x7443, "ReadScanCode", &ReadScanCode, CLOBBERS_AX},
  NativeEntry{0x74E0, "ReadFireButton", &ReadFireButton, FIRE_BUTTON},
  NativeEntry{0x7536, "ReadSteering", &ReadSteering, CLOBBERS_BX_CX_DX},
  NativeEntry{0x7616, "GetKey", &GetKey, KEY},
  NativeEntry{0x7668, "ResetKeyboard", &ResetKeyboard, PRESERVES_ALL},
  NativeEntry{0x777E, "ReadJoystickAxes", &ReadJoystickAxes, STICK_AXES},
  NativeEntry{0x77C1, "ReadJoystickSteering", &ReadJoystickSteering, STICK_STEERING},
  NativeEntry{0x78EF, "ReadKeyboardSteering", &ReadKeyboardSteering, CLOBBERS_BX},
  NativeEntry{0x797E, "ReadMouseSteering", &ReadMouseSteering, CLOBBERS_BX_CX_DX},
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
