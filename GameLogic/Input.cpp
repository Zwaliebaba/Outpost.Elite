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

constexpr std::uint16_t GAME_PORT = 0x201;
constexpr std::uint8_t MOUSE_VECTOR = 0x33;
// The button int 33h AX=5 is asked about: BX=1, the right. The game reads only AX, the buttons held, and discards the
// right button's presses.
constexpr std::uint16_t MOUSE_RIGHT_BUTTON = 1;
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

// ReadJoystickAxes's backward jumps: the X count's, the wait for both bits, and the Y count's.
constexpr std::uint16_t X_COUNT_TURN = 0x778F;
constexpr std::uint16_t BOTH_DOWN_TURN = 0x7797;
constexpr std::uint16_t Y_COUNT_TURN = 0x779D;

// The cycles the 8088 model (Machine/Cpu.cpp) charges for ReadJoystickAxes's instructions: the game port times its one-shots by
// them, and the stick is read by counting turns of the polling loops, so native code counts them where the original runs them.
constexpr Machine::Cycles BEFORE_FIRING_CYCLES = 12;    // CLI 2, MOV BX,imm16 4, MOV CX,BX 2, MOV DX,imm16 4
constexpr Machine::Cycles OUT_CYCLES = 8;               // OUT DX,AL
constexpr Machine::Cycles IN_CYCLES = 8;                // IN AL,DX
constexpr Machine::Cycles AND_CYCLES = 4;               // AND AL,imm8
constexpr Machine::Cycles CMP_CYCLES = 4;               // CMP AL,imm8
constexpr Machine::Cycles INC_CYCLES = 2;               // INC r16
constexpr Machine::Cycles TAKEN_CYCLES = 16;            // a conditional short jump taken
constexpr Machine::Cycles NOT_TAKEN_CYCLES = 4;         // and not taken
constexpr Machine::Cycles TIME_OUT_CYCLES = 14;         // STC 2, RET 12
constexpr Machine::Cycles BEFORE_LAST_READ_CYCLES = 14; // STI 2, SUB CX,imm16 4, SUB BX,imm16 4, MOV DX,imm16 4
constexpr Machine::Cycles STORE_CYCLES = 10;            // MOV [joystickPortByte],AL
constexpr Machine::Cycles LATCH_CYCLES = 16;            // MOV byte [fireLatch],1: 10, and 6 for the direct address
constexpr Machine::Cycles RET_CYCLES = 12;              // RET: 8, and 4 for the word popped over the 8-bit bus

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

// The mouse's buttons in what int 33h AX=5 leaves in AL: ReadFireButton shifts out two, the left and then the right.
constexpr std::uint8_t MOUSE_LEFT_AND_RIGHT = 0x03;

// A key's keyDown byte, or two ORed, as SHR AL,1 tests it: bit 0, which the shift leaves in CF.
[[nodiscard]] bool Pressed(std::uint8_t _keys) noexcept
{
  return (_keys & 1) != 0;
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

// Whether IntegrateRate tests _input against the rate at _rate for a reversal: XOR BL,[rate], which leaves BL the key
// XOR the rate.
[[nodiscard]] bool TestsReversal(const GameState& _state, std::uint8_t _input, DataField<std::uint8_t> _rate)
{
  return _input != 0 && _state.Get(DS.keyboardRecenter) == 1 && _state.Get(_rate) != 0;
}

// One axis of ApplyKeyboardRates (0x7543): a new key adds to the rate, within +-23, or with keyboardRecenter snaps
// an opposite rate to 0, which it stores at once; no key lets keyboardDamping take up to 3 off it.
[[nodiscard]] std::uint8_t IntegrateRate(GameState& _state, std::uint8_t _input, DataField<std::uint8_t> _rate)
{
  const std::uint8_t rate = _state.Get(_rate);
  if (_input == 0)
  {
    std::uint8_t value = rate;
    if (_state.Get(DS.keyboardDamping) != 1 || value == 0)
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
  if (TestsReversal(_state, _input, _rate) && ((_input ^ rate) & 0x80) != 0)
  {
    _state.Set(_rate, 0);
    return 0;
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

// ApplyKeyboardRates (0x7543): the keys _keys integrated into keyboardRollRate and keyboardPitchRate, written as one
// word. Returns the rates.
Steering ApplyKeyboardRates(GameState& _state, Steering _keys)
{
  const Steering rates{IntegrateRate(_state, _keys.roll, DS.keyboardRollRate), IntegrateRate(_state, _keys.pitch, DS.keyboardPitchRate)};
  _state.SetWord(DS.keyboardRollRate.offset, Join(rates.pitch, rates.roll));
  return rates;
}

// ApplyKeyboardRates on AL and AH, for ReadSteering's register code: AX the rates, and BL the key XOR its rate where the
// original tests one for a reversal, the pitch's over the roll's. Neither test reads what the other axis writes.
void ApplyKeyboardRatesToRegisters(Machine::Registers& _regs, GameState& _state)
{
  const Steering keys{Low(_regs.ax), High(_regs.ax)};
  if (TestsReversal(_state, keys.roll, DS.keyboardRollRate))
  {
    SetLow(_regs.bx, static_cast<std::uint8_t>(keys.roll ^ _state.Get(DS.keyboardRollRate)));
  }
  if (TestsReversal(_state, keys.pitch, DS.keyboardPitchRate))
  {
    SetLow(_regs.bx, static_cast<std::uint8_t>(keys.pitch ^ _state.Get(DS.keyboardPitchRate)));
  }
  const Steering rates = ApplyKeyboardRates(_state, keys);
  _regs.ax = Join(rates.pitch, rates.roll);
}

// One axis of ReadKeyboardSteering: the ramp grows while the same key is held, inside +-23, and restarts on a change.
void RampAxis(GameState& _state, std::uint8_t _key, DataField<std::uint8_t> _lastKey, DataField<std::uint8_t> _ramp)
{
  const bool held = _key == _state.Get(_lastKey);
  _state.Set(_lastKey, _key);
  if (!held)
  {
    _state.Set(_ramp, 0);
    return;
  }
  const auto value = static_cast<std::uint8_t>(_key + _state.Get(_ramp));
  const auto signedValue = static_cast<std::int8_t>(value);
  if (signedValue > -MOST_RATE - 1 && signedValue < MOST_RATE + 1)
  {
    _state.Set(_ramp, value);
  }
}

// ReadJoystickSteering's Amstrad stick (787D): its four key codes ramped as the keyboard's cursor keys are. Returns the
// roll ramp and the pitch ramp negated.
Steering ReadAmstradStick(GameState& _state)
{
  std::uint8_t roll = 0;
  std::uint8_t pitch = 0;
  if (_state.Get(DS.keyDownAmstradUp) == 1)
  {
    --pitch;
  }
  if (_state.Get(DS.keyDownAmstradDown) == 1)
  {
    ++pitch;
  }
  if (_state.Get(DS.keyDownAmstradLeft) == 1)
  {
    --roll;
  }
  if (_state.Get(DS.keyDownAmstradRight) == 1)
  {
    ++roll;
  }
  RampAxis(_state, roll, DS.amstradStickLastRoll, DS.amstradStickRollRamp);
  RampAxis(_state, pitch, DS.amstradStickLastPitch, DS.amstradStickPitchRamp);
  return Steering{_state.Get(DS.amstradStickRollRamp), Negate(_state.Get(DS.amstradStickPitchRamp))};
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

// One axis of ReadMouseSteering: a motion counter over 8, saturated at 63 either way.
[[nodiscard]] std::uint8_t MouseAxis(std::uint16_t _mickeys) noexcept
{
  const bool negative = (_mickeys & 0x8000) != 0;
  const auto magnitude = static_cast<std::uint16_t>((negative ? Negate(_mickeys) : _mickeys) >> 3);
  const std::uint8_t step = magnitude >= MOUSE_STEP_LIMIT ? MOST_MOUSE_STEP : Low(magnitude);
  return negative ? Negate(step) : step;
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
  // What ReadScanCode leaves in AX goes no further: AX is popped below.
  ReadScanCode(_guest.State(), _guest.Devices());
  regs.es = es;
  regs.ds = ds;
  regs.ax = ax;
}

MouseDriver IsMouseDriverInstalled(const GameState& _state)
{
  // The int 33h vector: installed when it is set and does not point at an IRET.
  const std::uint16_t offset = _state.FarWord(0, MOUSE_VECTOR_OFFSET);
  const std::uint16_t segment = _state.FarWord(0, static_cast<std::uint16_t>(MOUSE_VECTOR_OFFSET + 2));
  return MouseDriver{(offset | segment) != 0 && _state.FarByte(segment, offset) != IRET_OPCODE, segment};
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
    _guest.JumpBack(WAIT_FOR_KEY_PRESS);
  }
}

void ReadSteering(Guest& _guest)
{
  const std::uint8_t device = _guest.Get(DS.inputDevice);
  if (device == KEYBOARD_DEVICE)
  {
    ReadKeyboardSteeringEntry(_guest);
    ApplyKeyboardRatesToRegisters(_guest.Regs(), _guest.State());
    return;
  }
  if (device == JOYSTICK_DEVICE)
  {
    ReadJoystickSteering(_guest);
    if (_guest.Get(DS.joystickIsAmstrad) == 1)
    {
      ApplyKeyboardRatesToRegisters(_guest.Regs(), _guest.State());
    }
    return;
  }
  ReadMouseSteeringEntry(_guest);
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

void ResetKeyboard(GameState& _state, Hardware& _hardware)
{
  _hardware.DisableInterrupts();
  for (std::size_t word = 0; word < KEY_DOWN_WORDS; ++word)
  {
    _state.SetWord(DS.keyDown.At(word * 2), 0);
  }
  _state.Set(DS.keyBufferCount, 0);
  _state.Set(DS.keyBufferWrite, DS.keyBuffer.offset);
  _state.Set(DS.keyBufferRead, DS.keyBuffer.offset);
  _state.Set(DS.rollRate, 0);
  _hardware.EnableInterrupts();
}

void ReadScanCode(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t scan = _hardware.KeyboardData();
  _hardware.AcknowledgeKeyboard();
  if (scan != OVERRUN_CODE)
  {
    if ((scan & BREAK_BIT) != 0)
    {
      const auto code = static_cast<std::uint8_t>(scan & ~BREAK_BIT);
      _state.SetByte(DS.keyDown.At(code), 0);
      if (code == D_CODE && _state.Get(DS.inFlight) == 1)
      {
        _state.Set(DS.dockingKeyReleased, 1);
      }
    }
    else
    {
      _state.Set(DS.anyKeyLatch, 1);
      if (scan == ESC_CODE)
      {
        _state.Set(DS.escKeyLatch, 1);
      }
      if (scan == W_CODE && _state.Get(DS.keyDownAlt) == 1)
      {
        _state.Set(DS.forceMisjump, 1);
      }
      _state.SetByte(DS.keyDown.At(scan), 1);
      std::uint8_t code = scan;
      if (_state.Get(DS.keyDownLeftShift) == 1 || _state.Get(DS.keyDownRightShift) == 1)
      {
        code |= SHIFT_BIT;
      }
      const std::uint8_t count = _state.Get(DS.keyBufferCount);
      if (count != KEY_BUFFER_CODES)
      {
        const std::uint16_t write = _state.Get(DS.keyBufferWrite);
        _state.SetByte(write, code);
        const auto next = static_cast<std::uint16_t>(write + 1);
        _state.Set(DS.keyBufferWrite, (next & 0x0F) == 0 ? DS.keyBuffer.offset : next);
        _state.Set(DS.keyBufferCount, static_cast<std::uint8_t>(count + 1));
      }
    }
  }
  _hardware.EndOfInterrupt();
}

bool ReadFireButton(const GameState& _state, Hardware& _hardware)
{
  const std::uint8_t device = _state.Get(DS.inputDevice);
  if (device == KEYBOARD_DEVICE)
  {
    return Pressed(_state.Get(DS.keyDownSpace));
  }
  if (device == JOYSTICK_DEVICE)
  {
    if (_state.Get(DS.joystickIsAmstrad) == 1)
    {
      return Pressed(static_cast<std::uint8_t>(_state.Get(DS.keyDownAmstradFire1) | _state.Get(DS.keyDownAmstradFire2)));
    }
    // Each button shifted left into CF and inverted, bit 5 and then bit 4: pressed reads 0.
    const std::uint8_t buttons = _hardware.GamePortButtons();
    return (buttons & STICK_BUTTON_A) == 0 || (buttons & STICK_BUTTON_B) == 0;
  }
  if (_state.Get(DS.amstradPresent) == 1)
  {
    return Pressed(static_cast<std::uint8_t>(_state.Get(DS.keyDownAmstradMouseRight) | _state.Get(DS.keyDownAmstradMouseLeft)));
  }
  // SHR AL,1 twice: the left button, then the right.
  return (Low(_hardware.ReadMousePresses(MOUSE_RIGHT_BUTTON).buttons) & MOUSE_LEFT_AND_RIGHT) != 0;
}

void ReadJoystickAxes(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // With interrupts off, the one-shots fired, and each axis's polls counted until its bit drops. The game port times its one-shots
  // by the instructions executed, so each of the original's is counted (CountCycles) before the port access after it, and each of
  // its backward jumps ends a turn (JumpBack): the wait for both bits idles as the original's does.
  _guest.SetFlag(Machine::FLAG_INTERRUPT, false);
  regs.bx = AXIS_COUNT_START;
  regs.cx = regs.bx;
  regs.dx = GAME_PORT;
  _guest.CountCycles(BEFORE_FIRING_CYCLES);
  _guest.Out8(GAME_PORT, Low(regs.ax));
  _guest.CountCycles(OUT_CYCLES);
  SetLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(GAME_PORT) & BOTH_AXES));
  _guest.CountCycles(IN_CYCLES + AND_CYCLES + CMP_CYCLES);
  // A time-out leaves interrupts off, as the original does.
  const auto timeOut = [&]()
  {
    _guest.CountCycles(TAKEN_CYCLES + TIME_OUT_CYCLES);
    _guest.SetFlag(Machine::FLAG_CARRY, true);
  };
  if (Low(regs.ax) != BOTH_AXES)
  {
    timeOut();
    return;
  }
  _guest.CountCycles(NOT_TAKEN_CYCLES);
  for (;;)
  {
    regs.bx = static_cast<std::uint16_t>(regs.bx + 1);
    _guest.CountCycles(INC_CYCLES);
    if (regs.bx == 0)
    {
      timeOut();
      return;
    }
    _guest.CountCycles(NOT_TAKEN_CYCLES);
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(GAME_PORT) & X_AXIS));
    _guest.CountCycles(IN_CYCLES + AND_CYCLES);
    if (Low(regs.ax) == 0)
    {
      break;
    }
    _guest.CountCycles(TAKEN_CYCLES);
    _guest.JumpBack(X_COUNT_TURN);
  }
  _guest.CountCycles(NOT_TAKEN_CYCLES);
  // Both bits down before the one-shots fire again: a wait when X's drops first.
  for (;;)
  {
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(GAME_PORT) & BOTH_AXES));
    _guest.CountCycles(IN_CYCLES + AND_CYCLES);
    if (Low(regs.ax) == 0)
    {
      break;
    }
    _guest.CountCycles(TAKEN_CYCLES);
    _guest.JumpBack(BOTH_DOWN_TURN);
  }
  _guest.CountCycles(NOT_TAKEN_CYCLES);
  _guest.Out8(GAME_PORT, Low(regs.ax));
  _guest.CountCycles(OUT_CYCLES);
  for (;;)
  {
    regs.cx = static_cast<std::uint16_t>(regs.cx + 1);
    _guest.CountCycles(INC_CYCLES);
    if (regs.cx == 0)
    {
      timeOut();
      return;
    }
    _guest.CountCycles(NOT_TAKEN_CYCLES);
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.In8(GAME_PORT) & Y_AXIS));
    _guest.CountCycles(IN_CYCLES + AND_CYCLES);
    if (Low(regs.ax) == 0)
    {
      break;
    }
    _guest.CountCycles(TAKEN_CYCLES);
    _guest.JumpBack(Y_COUNT_TURN);
  }
  _guest.CountCycles(NOT_TAKEN_CYCLES);
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);
  regs.cx = static_cast<std::uint16_t>(regs.cx - AXIS_COUNT_START);
  regs.bx = static_cast<std::uint16_t>(regs.bx - AXIS_COUNT_START);
  regs.dx = GAME_PORT;
  _guest.CountCycles(BEFORE_LAST_READ_CYCLES);
  SetLow(regs.ax, _guest.In8(GAME_PORT));
  _guest.Set(DS.joystickPortByte, Low(regs.ax));
  // Bit 4, the stick's first button, low while pressed.
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & STICK_BUTTON_B));
  _guest.CountCycles(IN_CYCLES + STORE_CYCLES + AND_CYCLES);
  if (Low(regs.ax) == 0)
  {
    _guest.Set(DS.fireLatch, 1);
    _guest.CountCycles(NOT_TAKEN_CYCLES + LATCH_CYCLES);
  }
  else
  {
    _guest.CountCycles(TAKEN_CYCLES);
  }
  _guest.CountCycles(RET_CYCLES);
  _guest.SetFlag(Machine::FLAG_CARRY, false);
}

void ReadJoystickSteering(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.joystickIsAmstrad) == 1)
  {
    const Steering stick = ReadAmstradStick(_guest.State());
    regs.ax = Join(stick.pitch, stick.roll);
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

Steering ReadKeyboardSteering(GameState& _state)
{
  std::uint8_t roll = 0;
  std::uint8_t pitch = 0;
  const auto held = [&_state](DataField<std::uint8_t> _first, DataField<std::uint8_t> _second)
  { return (_state.Get(_first) | _state.Get(_second)) != 0; };
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
  RampAxis(_state, roll, DS.keyboardLastRollKey, DS.keyboardRollRamp);
  RampAxis(_state, pitch, DS.keyboardLastPitchKey, DS.keyboardPitchRamp);
  return Steering{_state.Get(DS.keyboardRollRamp), Negate(_state.Get(DS.keyboardPitchRamp))};
}

Steering ReadMouseSteering(GameState& _state, Hardware& _hardware)
{
  const MouseMotion motion = _hardware.ReadMouseMotion();
  const std::uint8_t rollStep = MouseAxis(motion.acrossMickeys);
  const std::uint8_t pitchStep = MouseAxis(motion.downMickeys);
  std::uint8_t buttons = 0;
  if (_state.Get(DS.amstradPresent) == 1)
  {
    buttons = static_cast<std::uint8_t>((_state.Get(DS.keyDownAmstradMouseRight) << 1) | _state.Get(DS.keyDownAmstradMouseLeft));
  }
  else
  {
    buttons = Low(_hardware.ReadMousePresses(MOUSE_RIGHT_BUTTON).buttons);
  }
  _state.Set(DS.mouseButtons, buttons);
  if ((buttons & 1) != 0)
  {
    _state.Set(DS.fireLatch, 1);
  }
  // NEG AH / SAR AL,1 / SAR AH,1: half of each step, the pitch negated, added to the rates the last frame left.
  return Steering{MouseRate(rollStep, _state.Byte(DS.rollRate.offset)), MouseRate(Negate(pitchStep), _state.Get(DS.pitchRate))};
}

void PollScreenDumpKey(Guest& _guest)
{
  SaveScreenshotIfAsked(_guest);
}

void ResetMouseIfSelected(const GameState& _state, Hardware& _hardware)
{
  if (_state.Get(DS.inputDevice) != MOUSE_DEVICE)
  {
    return;
  }
  // What the driver answers, whether it is installed and how many buttons it has, the game does not read.
  _hardware.ResetMouse();
}

Steering ApplyReverseControls(const GameState& _state, Steering _steering)
{
  Steering steering = _steering;
  if (_state.Get(DS.reverseYControl) == 1)
  {
    steering.pitch = Negate(steering.pitch);
  }
  if (_state.Get(DS.reverseXAndY) == 1)
  {
    steering.roll = Negate(steering.roll);
    steering.pitch = Negate(steering.pitch);
  }
  return steering;
}

// ── The entries of the de-assembled routines ──

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_INTERRUPT;
using Machine::FLAG_ZERO;
using Machine::REGISTER_AX;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DX;

constexpr Machine::NativeContract RETURNS_ZERO{0, FLAG_ZERO};
constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX{REGISTER_AX | REGISTER_BX, 0};
constexpr Machine::NativeContract CLOBBERS_BX{REGISTER_BX, 0};
constexpr Machine::NativeContract CLOBBERS_BX_CX_DX{REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract FIRE_BUTTON{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, FLAG_CARRY};

// The steering bytes of a register, roll in the low byte and pitch in the high, and back.
[[nodiscard]] Steering SteeringIn(std::uint16_t _register) noexcept
{
  return Steering{Low(_register), High(_register)};
}

[[nodiscard]] std::uint16_t SteeringOut(Steering _steering) noexcept
{
  return Join(_steering.pitch, _steering.roll);
}

} // namespace

void IsMouseDriverInstalledEntry(Guest& _guest)
{
  const MouseDriver driver = IsMouseDriverInstalled(_guest.State());
  _guest.Regs().es = driver.segment;
  _guest.SetFlag(FLAG_ZERO, !driver.installed);
  _guest.Clobber(RETURNS_ZERO);
}

void ResetKeyboardEntry(Guest& _guest)
{
  ResetKeyboard(_guest.State(), _guest.Devices());
  _guest.Clobber(PRESERVES_ALL);
}

void ApplyReverseControlsEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = SteeringOut(ApplyReverseControls(_guest.State(), SteeringIn(regs.ax)));
  _guest.Clobber(PRESERVES_ALL);
}

void ApplyReverseControlsToDxEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = SteeringOut(ApplyReverseControls(_guest.State(), SteeringIn(regs.dx)));
  _guest.Clobber(PRESERVES_ALL);
}

void ReadKeyboardSteeringEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = SteeringOut(ReadKeyboardSteering(_guest.State()));
  _guest.Clobber(CLOBBERS_BX);
}

void ReadScanCodeEntry(Guest& _guest)
{
  ReadScanCode(_guest.State(), _guest.Devices());
  _guest.Clobber(CLOBBERS_AX);
}

void ReadFireButtonEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_CARRY, ReadFireButton(_guest.State(), _guest.Devices()));
  _guest.Clobber(FIRE_BUTTON);
}

void ReadMouseSteeringEntry(Guest& _guest)
{
  _guest.Regs().ax = SteeringOut(ReadMouseSteering(_guest.State(), _guest.Devices()));
  _guest.Clobber(CLOBBERS_BX_CX_DX);
}

void ResetMouseIfSelectedEntry(Guest& _guest)
{
  ResetMouseIfSelected(_guest.State(), _guest.Devices());
  _guest.Clobber(CLOBBERS_AX_BX);
}

namespace
{

constexpr Machine::NativeContract KEY{0, FLAG_ZERO | FLAG_INTERRUPT};
// The stick's routines leave interrupts off on a time-out, which their callers live with: compared too. They wait when the stick's
// X one-shot drops before its Y one-shot (ReadJoystickAxes's loop at 7797), and so does ReadSteering through them.
constexpr Machine::NativeContract STICK_AXES{REGISTER_AX, FLAG_CARRY | FLAG_INTERRUPT};
constexpr Machine::NativeContract STICK_STEERING{REGISTER_BX | REGISTER_CX | REGISTER_DX, FLAG_INTERRUPT};

constexpr std::array ENTRIES = {
  NativeEntry{0x0201, "KeyboardInterrupt", &KeyboardInterrupt, PRESERVES_ALL, Machine::NativeReturn::Interrupt},
  NativeEntry{0x02D4, "IsMouseDriverInstalled", &IsMouseDriverInstalledEntry, RETURNS_ZERO},
  // It waits as a rule: the mission briefings call it for the key that ends them.
  NativeEntry{0x6DEC, "WaitForKeyPress", &WaitForKeyPress, PRESERVES_ALL, Machine::NativeReturn::Near, 0, Machine::NativeWait::Always},
  NativeEntry{0x7443, "ReadScanCode", &ReadScanCodeEntry, CLOBBERS_AX},
  NativeEntry{0x74E0, "ReadFireButton", &ReadFireButtonEntry, FIRE_BUTTON},
  NativeEntry{0x7536, "ReadSteering", &ReadSteering, CLOBBERS_BX_CX_DX, Machine::NativeReturn::Near, 0, Machine::NativeWait::Sometimes},
  NativeEntry{0x7616, "GetKey", &GetKey, KEY},
  NativeEntry{0x7668, "ResetKeyboard", &ResetKeyboardEntry, PRESERVES_ALL},
  NativeEntry{0x777E, "ReadJoystickAxes", &ReadJoystickAxes, STICK_AXES, Machine::NativeReturn::Near, 0, Machine::NativeWait::Sometimes},
  NativeEntry{0x77C1, "ReadJoystickSteering", &ReadJoystickSteering, STICK_STEERING, Machine::NativeReturn::Near, 0,
              Machine::NativeWait::Sometimes},
  NativeEntry{0x78EF, "ReadKeyboardSteering", &ReadKeyboardSteeringEntry, CLOBBERS_BX},
  NativeEntry{0x797E, "ReadMouseSteering", &ReadMouseSteeringEntry, CLOBBERS_BX_CX_DX},
  NativeEntry{0x7F3D, "PollScreenDumpKey", &PollScreenDumpKey, PRESERVES_ALL},
  NativeEntry{0x7F5D, "ResetMouseIfSelected", &ResetMouseIfSelectedEntry, CLOBBERS_AX_BX},
  NativeEntry{0x8EA5, "ApplyReverseControls", &ApplyReverseControlsEntry, PRESERVES_ALL},
  NativeEntry{0x8EBA, "ApplyReverseControlsToDx", &ApplyReverseControlsToDxEntry, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> InputEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
