#include "pch.h"

#include "Input.h"

#include "Arithmetic.h"
#include "Maths.h"
#include "Text.h"
#include "Video.h"

namespace Elite
{

namespace
{

// WaitForKeyPress's loop: GetKey, and JE back to it while there is no key (Hardware::LoopTurn).
constexpr std::uint16_t WAIT_FOR_KEY_PRESS = 0x6DEC;

constexpr std::uint8_t MOUSE_VECTOR = 0x33;
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

// ReadJoystickAxes's backward jumps: the X count's, the wait for both bits, and the Y count's (Hardware::LoopTurn).
constexpr std::uint16_t X_COUNT_TURN = 0x778F;
constexpr std::uint16_t BOTH_DOWN_TURN = 0x7797;
constexpr std::uint16_t Y_COUNT_TURN = 0x779D;

// The cycles the 8088 model (the interpreter, until D7) charged for ReadJoystickAxes's instructions: the game port times its one-shots by
// them, and the stick is read by counting turns of the polling loops, so native code counts them where the original runs them.
constexpr Cycles BEFORE_FIRING_CYCLES = 12;    // CLI 2, MOV BX,imm16 4, MOV CX,BX 2, MOV DX,imm16 4
constexpr Cycles OUT_CYCLES = 8;               // OUT DX,AL
constexpr Cycles IN_CYCLES = 8;                // IN AL,DX
constexpr Cycles AND_CYCLES = 4;               // AND AL,imm8
constexpr Cycles CMP_CYCLES = 4;               // CMP AL,imm8
constexpr Cycles INC_CYCLES = 2;               // INC r16
constexpr Cycles TAKEN_CYCLES = 16;            // a conditional short jump taken
constexpr Cycles NOT_TAKEN_CYCLES = 4;         // and not taken
constexpr Cycles TIME_OUT_CYCLES = 14;         // STC 2, RET 12
constexpr Cycles BEFORE_LAST_READ_CYCLES = 14; // STI 2, SUB CX,imm16 4, SUB BX,imm16 4, MOV DX,imm16 4
constexpr Cycles STORE_CYCLES = 10;            // MOV [joystickPortByte],AL
constexpr Cycles LATCH_CYCLES = 16;            // MOV byte [fireLatch],1: 10, and 6 for the direct address
constexpr Cycles RET_CYCLES = 12;              // RET: 8, and 4 for the word popped over the 8-bit bus

constexpr std::uint8_t OVERRUN_CODE = 0xFF;
constexpr std::uint8_t BREAK_BIT = 0x80;
constexpr std::uint8_t SHIFT_BIT = 0x80; // on a code in keyBuffer
constexpr std::uint8_t W_CODE = 0x11;
constexpr std::uint8_t D_CODE = 0x20;
constexpr std::uint8_t KEY_BUFFER_CODES = 0x10;
constexpr std::uint16_t KEY_BUFFER_WRAP = 0x0F; // keyBuffer is a ring of 16: an index past its last is its first
constexpr std::size_t KEY_DOWN_WORDS = 0x40;

// keyDown's 128 bytes, a byte a scan code: 1 while its key is held.
[[nodiscard]] std::span<Counted<std::uint8_t>> KeyDown(GameState& _state)
{
  return BytesOf(_state.Data().keyboard.keyDown);
}

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

// SaveScreenshot (CS:01B7) while Alt and PrtSc are held. Returns whether it saved one. The original pushes and pops every
// register but ES around the call, so that a screenshot leaves only ES, on the CGA's memory.
bool SaveScreenshotIfAsked(GameState& _state, Hardware& _hardware)
{
  if (_state.Data().keyboard.keyDown.keyDownPrintScreen != 1 || _state.Data().keyboard.keyDown.keyDownAlt != 1)
  {
    return false;
  }
  SaveScreenshot(_state, _hardware);
  return true;
}

// Whether IntegrateRate tests _input against the rate at _rate for a reversal: XOR BL,[rate], which leaves BL the key
// XOR the rate.
[[nodiscard]] bool TestsReversal(const GameState& _state, std::uint8_t _input, const Counted<std::uint8_t>& _rate)
{
  return _input != 0 && _state.Data().flight.keyboardRecenter == 1 && _rate != 0;
}

// One axis of ApplyKeyboardRates (0x7543): a new key adds to the rate, within +-23, or with keyboardRecenter snaps
// an opposite rate to 0, which it stores at once; no key lets keyboardDamping take up to 3 off it.
[[nodiscard]] std::uint8_t IntegrateRate(GameState& _state, std::uint8_t _input, Counted<std::uint8_t>& _rate)
{
  const std::uint8_t rate = _rate;
  if (_input == 0)
  {
    std::uint8_t value = rate;
    if (_state.Data().flight.keyboardDamping != 1 || value == 0)
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
    _rate = 0;
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
  const Steering rates{IntegrateRate(_state, _keys.roll, _state.Data().input.keyboardRollRate),
                       IntegrateRate(_state, _keys.pitch, _state.Data().input.keyboardPitchRate)};
  // Written as one word, the roll's byte and then the pitch's.
  _state.Data().input.keyboardRollRate = rates.roll;
  _state.Data().input.keyboardPitchRate = rates.pitch;
  return rates;
}

// One axis of ReadKeyboardSteering: the ramp grows while the same key is held, inside +-23, and restarts on a change.
void RampAxis(std::uint8_t _key, Counted<std::uint8_t>& _lastKey, Counted<std::uint8_t>& _ramp)
{
  const bool held = _key == _lastKey;
  _lastKey = _key;
  if (!held)
  {
    _ramp = 0;
    return;
  }
  const auto value = static_cast<std::uint8_t>(_key + _ramp);
  const auto signedValue = static_cast<std::int8_t>(value);
  if (signedValue > -MOST_RATE - 1 && signedValue < MOST_RATE + 1)
  {
    _ramp = value;
  }
}

// ReadJoystickSteering's Amstrad stick (787D): its four key codes ramped as the keyboard's cursor keys are. Returns the
// roll ramp and the pitch ramp negated.
Steering ReadAmstradStick(GameState& _state)
{
  std::uint8_t roll = 0;
  std::uint8_t pitch = 0;
  if (_state.Data().keyboard.keyDown.keyDownAmstradUp == 1)
  {
    --pitch;
  }
  if (_state.Data().keyboard.keyDown.keyDownAmstradDown == 1)
  {
    ++pitch;
  }
  if (_state.Data().keyboard.keyDown.keyDownAmstradLeft == 1)
  {
    --roll;
  }
  if (_state.Data().keyboard.keyDown.keyDownAmstradRight == 1)
  {
    ++roll;
  }
  RampAxis(roll, _state.Data().input.amstradStickLastRoll, _state.Data().input.amstradStickRollRamp);
  RampAxis(pitch, _state.Data().input.amstradStickLastPitch, _state.Data().input.amstradStickPitchRamp);
  return Steering{_state.Data().input.amstradStickRollRamp, Negate(_state.Data().input.amstradStickPitchRamp)};
}

// One axis of ReadJoystickSteering: (_count - centre) * 256 / centre by a DIV that can overflow into the game's trap; halved and
// saturated at 127, over 8, less a dead zone of 4, with the sign put back.
[[nodiscard]] std::uint8_t StickAxis(std::uint16_t _count, const Counted<std::uint16_t>& _center)
{
  auto difference = static_cast<std::uint16_t>(_count - _center);
  const bool negative = (difference & 0x8000) != 0;
  if (negative)
  {
    difference = Negate(difference);
  }
  // XOR DH,DH / MOV DL,AH / MOV AH,AL / XOR AL,AL: DX:AX = the difference * 256.
  const std::uint32_t dividend = std::uint32_t{difference} << 8;
  const auto halved = static_cast<std::uint16_t>(DivideWord(dividend, _center).quotient >> 1);
  const std::uint8_t saturated = halved >= STICK_STEP_LIMIT ? MOST_STICK_STEP : Low(halved);
  const auto step = static_cast<std::uint8_t>(saturated >> 3);
  const std::uint8_t value = step >= STICK_DEAD_ZONE ? static_cast<std::uint8_t>(step - STICK_DEAD_ZONE) : std::uint8_t{0};
  return negative ? Negate(value) : value;
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

void KeyboardInterrupt(GameState& _state, Hardware& _hardware)
{
  ReadScanCode(_state, _hardware);
}

MouseDriver IsMouseDriverInstalled(Hardware& _hardware)
{
  // The int 33h vector: installed when it is set and does not point at an IRET.
  const FarAddress vector = _hardware.InterruptVector(MOUSE_VECTOR);
  const bool set = (vector.offset | vector.segment) != 0;
  return MouseDriver{set && _hardware.HandlerOpcode(vector) != IRET_OPCODE};
}

void WaitForKeyPress(GameState& _state, Hardware& _hardware)
{
  // GetKey until a key comes: each empty turn ends at the original's JE back to the entry. The turn carries nothing: GetKey
  // writes AH before it reads it and keeps BX, and it reads AL only when it takes a code, which changes keyBuffer's count.
  for (;;)
  {
    if (GetKey(_state, _hardware).scanCode != 0)
    {
      return;
    }
    _hardware.LoopTurn(WAIT_FOR_KEY_PRESS, {});
  }
}

Steering ReadSteering(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t device = _state.Data().saveLoad.inputDevice;
  if (device == KEYBOARD_DEVICE)
  {
    return ApplyKeyboardRates(_state, ReadKeyboardSteering(_state));
  }
  if (device == JOYSTICK_DEVICE)
  {
    const Steering stick = ReadJoystickSteering(_state, _hardware);
    return _state.Data().saveLoad.joystickIsAmstrad == 1 ? ApplyKeyboardRates(_state, stick) : stick;
  }
  return ReadMouseSteering(_state, _hardware);
}

KeyPress GetKey(GameState& _state, Hardware& _hardware)
{
  _hardware.DisableInterrupts();
  KeyPress key{false, 0, false, false};
  const std::uint8_t count = _state.Data().input.keyBufferCount;
  if (count != 0)
  {
    const std::uint16_t read = _state.Data().input.keyBufferRead;
    const std::uint8_t code = _state.Data().input.keyBuffer[read];
    _state.Data().input.keyBufferRead = static_cast<std::uint16_t>((read + 1) & KEY_BUFFER_WRAP);
    _state.Data().input.keyBufferCount = static_cast<std::uint8_t>(count - 1);
    key = KeyPress{true, static_cast<std::uint8_t>(code & ~SHIFT_BIT), (code & SHIFT_BIT) != 0, false};
  }
  _hardware.EnableInterrupts();
  key.screenshot = SaveScreenshotIfAsked(_state, _hardware);
  return key;
}

void ResetKeyboard(GameState& _state, Hardware& _hardware)
{
  _hardware.DisableInterrupts();
  for (std::size_t word = 0; word < KEY_DOWN_WORDS; ++word)
  {
    SetWordIn(KeyDown(_state), word * 2, 0);
  }
  _state.Data().input.keyBufferCount = 0;
  _state.Data().input.keyBufferWrite = 0;
  _state.Data().input.keyBufferRead = 0;
  _state.Data().playerShip.rollRate = 0;
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
      KeyDown(_state)[code] = 0;
      if (code == D_CODE && _state.Data().flow.inFlight == 1)
      {
        _state.Data().playerShip.dockingKeyReleased = 1;
      }
    }
    else
    {
      if (scan == W_CODE && _state.Data().keyboard.keyDown.keyDownAlt == 1)
      {
        _state.Data().jump.forceMisjump = 1;
      }
      KeyDown(_state)[scan] = 1;
      std::uint8_t code = scan;
      if (_state.Data().keyboard.keyDown.keyDownLeftShift == 1 || _state.Data().keyboard.keyDown.keyDownRightShift == 1)
      {
        code |= SHIFT_BIT;
      }
      const std::uint8_t count = _state.Data().input.keyBufferCount;
      if (count != KEY_BUFFER_CODES)
      {
        const std::uint16_t write = _state.Data().input.keyBufferWrite;
        _state.Data().input.keyBuffer[write] = code;
        _state.Data().input.keyBufferWrite = static_cast<std::uint16_t>((write + 1) & KEY_BUFFER_WRAP);
        _state.Data().input.keyBufferCount = static_cast<std::uint8_t>(count + 1);
      }
    }
  }
  _hardware.EndOfInterrupt();
}

bool ReadFireButton(const GameState& _state, Hardware& _hardware)
{
  const std::uint8_t device = _state.Data().saveLoad.inputDevice;
  if (device == KEYBOARD_DEVICE)
  {
    return Pressed(_state.Data().keyboard.keyDown.keyDownSpace);
  }
  if (device == JOYSTICK_DEVICE)
  {
    if (_state.Data().saveLoad.joystickIsAmstrad == 1)
    {
      return Pressed(
        static_cast<std::uint8_t>(_state.Data().keyboard.keyDown.keyDownAmstradFire1 | _state.Data().keyboard.keyDown.keyDownAmstradFire2));
    }
    // Each button shifted left into CF and inverted, bit 5 and then bit 4: pressed reads 0.
    const std::uint8_t buttons = _hardware.GamePortButtons();
    return (buttons & STICK_BUTTON_A) == 0 || (buttons & STICK_BUTTON_B) == 0;
  }
  // SHR AL,1 twice: the left button, then the right.
  return (Low(_hardware.ReadMousePresses()) & MOUSE_LEFT_AND_RIGHT) != 0;
}

StickAxes ReadJoystickAxes(Hardware& _hardware)
{
  // With interrupts off, the one-shots fired, and each axis's polls counted until its bit drops. The game port times its one-shots
  // by the instructions executed, so each of the original's is counted (CountInstructionCycles) before the port access after it.
  // At each of its backward jumps the original holds in BX or CX the count the next turn goes on with, and in AL the bits it
  // read; the wait for both bits, which reads no other register before it writes it but DX, the port, idles as the original's
  // does.
  _hardware.DisableInterrupts();
  std::uint16_t x = AXIS_COUNT_START;
  std::uint16_t y = x;
  _hardware.CountInstructionCycles(BEFORE_FIRING_CYCLES);
  _hardware.FireGamePort();
  _hardware.CountInstructionCycles(OUT_CYCLES);
  std::uint8_t bits = static_cast<std::uint8_t>(_hardware.GamePortOneShots() & BOTH_AXES);
  _hardware.CountInstructionCycles(IN_CYCLES + AND_CYCLES + CMP_CYCLES);
  // A time-out leaves interrupts off, and the counts as they stand.
  const auto timeOut = [&]()
  {
    _hardware.CountInstructionCycles(TAKEN_CYCLES + TIME_OUT_CYCLES);
    return StickAxes{true, x, y};
  };
  if (bits != BOTH_AXES)
  {
    return timeOut();
  }
  _hardware.CountInstructionCycles(NOT_TAKEN_CYCLES);
  for (;;)
  {
    x = static_cast<std::uint16_t>(x + 1);
    _hardware.CountInstructionCycles(INC_CYCLES);
    if (x == 0)
    {
      return timeOut();
    }
    _hardware.CountInstructionCycles(NOT_TAKEN_CYCLES);
    bits = static_cast<std::uint8_t>(_hardware.GamePortOneShots() & X_AXIS);
    _hardware.CountInstructionCycles(IN_CYCLES + AND_CYCLES);
    if (bits == 0)
    {
      break;
    }
    _hardware.CountInstructionCycles(TAKEN_CYCLES);
    _hardware.LoopTurn(X_COUNT_TURN, {x, bits});
  }
  _hardware.CountInstructionCycles(NOT_TAKEN_CYCLES);
  // Both bits down before the one-shots fire again: a wait when X's drops first.
  for (;;)
  {
    bits = static_cast<std::uint8_t>(_hardware.GamePortOneShots() & BOTH_AXES);
    _hardware.CountInstructionCycles(IN_CYCLES + AND_CYCLES);
    if (bits == 0)
    {
      break;
    }
    _hardware.CountInstructionCycles(TAKEN_CYCLES);
    _hardware.LoopTurn(BOTH_DOWN_TURN, {bits});
  }
  _hardware.CountInstructionCycles(NOT_TAKEN_CYCLES);
  _hardware.FireGamePort();
  _hardware.CountInstructionCycles(OUT_CYCLES);
  for (;;)
  {
    y = static_cast<std::uint16_t>(y + 1);
    _hardware.CountInstructionCycles(INC_CYCLES);
    if (y == 0)
    {
      return timeOut();
    }
    _hardware.CountInstructionCycles(NOT_TAKEN_CYCLES);
    bits = static_cast<std::uint8_t>(_hardware.GamePortOneShots() & Y_AXIS);
    _hardware.CountInstructionCycles(IN_CYCLES + AND_CYCLES);
    if (bits == 0)
    {
      break;
    }
    _hardware.CountInstructionCycles(TAKEN_CYCLES);
    _hardware.LoopTurn(Y_COUNT_TURN, {y, bits});
  }
  _hardware.CountInstructionCycles(NOT_TAKEN_CYCLES);
  _hardware.EnableInterrupts();
  y = static_cast<std::uint16_t>(y - AXIS_COUNT_START);
  x = static_cast<std::uint16_t>(x - AXIS_COUNT_START);
  _hardware.CountInstructionCycles(BEFORE_LAST_READ_CYCLES);
  const std::uint8_t port = _hardware.GamePortButtons();
  // Bit 4, the stick's first button, low while pressed.
  _hardware.CountInstructionCycles(IN_CYCLES + STORE_CYCLES + AND_CYCLES);
  if ((port & STICK_BUTTON_B) == 0)
  {
    _hardware.CountInstructionCycles(NOT_TAKEN_CYCLES + LATCH_CYCLES);
  }
  else
  {
    _hardware.CountInstructionCycles(TAKEN_CYCLES);
  }
  _hardware.CountInstructionCycles(RET_CYCLES);
  return StickAxes{false, x, y};
}

Steering ReadJoystickSteering(GameState& _state, Hardware& _hardware)
{
  if (_state.Data().saveLoad.joystickIsAmstrad == 1)
  {
    return ReadAmstradStick(_state);
  }
  const StickAxes axes = ReadJoystickAxes(_hardware);
  if (axes.timedOut)
  {
    return Steering{0, 0};
  }
  const std::uint8_t roll = StickAxis(axes.x, _state.Data().saveLoad.joystickCenterX);
  const std::uint8_t pitch = StickAxis(axes.y, _state.Data().saveLoad.joystickCenterY);
  // ClampJoystickSteering (7A3C): the roll, then the pitch negated.
  return Steering{ClampRate(roll), ClampRate(Negate(pitch))};
}

Steering ReadKeyboardSteering(GameState& _state)
{
  std::uint8_t roll = 0;
  std::uint8_t pitch = 0;
  const auto held = [](const Counted<std::uint8_t>& _first, const Counted<std::uint8_t>& _second) { return (_first | _second) != 0; };
  if (held(_state.Data().keyboard.keyDown.keyDownUp, _state.Data().keyboard.keyDown.keyDownQ))
  {
    --pitch;
  }
  if (held(_state.Data().keyboard.keyDown.keyDownDown, _state.Data().keyboard.keyDown.keyDownA))
  {
    ++pitch;
  }
  if (held(_state.Data().keyboard.keyDown.keyDownLeft, _state.Data().keyboard.keyDown.keyDownO))
  {
    --roll;
  }
  if (held(_state.Data().keyboard.keyDown.keyDownRight, _state.Data().keyboard.keyDown.keyDownP))
  {
    ++roll;
  }
  RampAxis(roll, _state.Data().input.keyboardLastRollKey, _state.Data().input.keyboardRollRamp);
  RampAxis(pitch, _state.Data().input.keyboardLastPitchKey, _state.Data().input.keyboardPitchRamp);
  return Steering{_state.Data().input.keyboardRollRamp, Negate(_state.Data().input.keyboardPitchRamp)};
}

Steering ReadMouseSteering(GameState& _state, Hardware& _hardware)
{
  const MouseMotion motion = _hardware.ReadMouseMotion();
  const std::uint8_t rollStep = MouseAxis(motion.acrossMickeys);
  const std::uint8_t pitchStep = MouseAxis(motion.downMickeys);
  // The presses, which the original keeps in mouseButtons and latches into fireLatch: nothing reads either (change 3c).
  (void)_hardware.ReadMousePresses();
  // NEG AH / SAR AL,1 / SAR AH,1: half of each step, the pitch negated, added to the rates the last frame left.
  return Steering{MouseRate(rollStep, Low(_state.Data().playerShip.rollRate)),
                  MouseRate(Negate(pitchStep), High(_state.Data().playerShip.rollRate))};
}

void PollScreenDumpKey(GameState& _state, Hardware& _hardware)
{
  SaveScreenshotIfAsked(_state, _hardware);
}

void ResetMouseIfSelected(const GameState& _state, Hardware& _hardware)
{
  if (_state.Data().saveLoad.inputDevice != MOUSE_DEVICE)
  {
    return;
  }
  // What the driver answers, whether it is installed and how many buttons it has, the game does not read.
  _hardware.ResetMouse();
}

Steering ApplyReverseControls(const GameState& _state, Steering _steering)
{
  Steering steering = _steering;
  if (_state.Data().flight.reverseYControl == 1)
  {
    steering.pitch = Negate(steering.pitch);
  }
  if (_state.Data().flight.reverseXAndY == 1)
  {
    steering.roll = Negate(steering.roll);
    steering.pitch = Negate(steering.pitch);
  }
  return steering;
}

// ── The entry of the keyboard's interrupt handler ──

void KeyboardInterruptEntry(Guest& _guest)
{
  // AX, DS and ES pushed round MOV ES,B800h / MOV DS,data and the call, and popped: every register is kept.
  KeyboardInterrupt(_guest.State(), _guest.Devices());
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x0201, &KeyboardInterruptEntry, EntryReturn::Interrupt},
};

} // namespace

std::span<const NativeEntry> InputEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
