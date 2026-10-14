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

constexpr std::uint8_t MOST_MOUSE_STEP = 0x3F; // mickeys / 8, either way
constexpr std::uint16_t MOUSE_STEP_LIMIT = 0x40;

constexpr std::uint8_t SHIFT_BIT = 0x80; // on a code in keyBuffer
constexpr std::uint8_t W_CODE = 0x11;
constexpr std::uint8_t D_CODE = 0x20;
constexpr std::uint8_t KEY_BUFFER_CODES = 0x10;
constexpr std::uint16_t KEY_BUFFER_WRAP = 0x0F; // keyBuffer is a ring of 16: an index past its last is its first

// keyDown's 128 bytes, a byte a scan code: 1 while its key is held.
[[nodiscard]] std::span<std::uint8_t> KeyDown(GameState& _state)
{
  return _state.Data().keyboard.keyDown;
}

constexpr std::uint8_t KEYBOARD_DEVICE = 0;
constexpr std::uint8_t MOUSE_DEVICE = 2;

constexpr std::int8_t MOST_RATE = 23;             // either way
constexpr std::uint8_t MOST_NEGATIVE_RATE = 0xE9; // -23
constexpr std::uint8_t RATE_DECAY_STEPS = 3;

// The mouse's buttons, either of which fires: the left and the right.
constexpr std::uint8_t MOUSE_LEFT_AND_RIGHT = 0x03;

// A key's keyDown byte, or two ORed, as SHR AL,1 tests it: bit 0, which the shift leaves in CF.
[[nodiscard]] bool Pressed(std::uint8_t _keys) noexcept
{
  return (_keys & 1) != 0;
}

// Whether IntegrateRate tests _input against the rate at _rate for a reversal: XOR BL,[rate], which leaves BL the key
// XOR the rate.
[[nodiscard]] bool TestsReversal(const GameState& _state, std::uint8_t _input, const std::uint8_t& _rate)
{
  return _input != 0 && _state.Data().flight.keyboardRecenter == 1 && _rate != 0;
}

// One axis of ApplyKeyboardRates: a new key adds to the rate, within +-23, or with keyboardRecenter snaps
// an opposite rate to 0, which it stores at once; no key lets keyboardDamping take up to 3 off it.
[[nodiscard]] std::uint8_t IntegrateRate(GameState& _state, std::uint8_t _input, std::uint8_t& _rate)
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

// ApplyKeyboardRates: the keys _keys integrated into keyboardRollRate and keyboardPitchRate, written as one
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
void RampAxis(std::uint8_t _key, std::uint8_t& _lastKey, std::uint8_t& _ramp)
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

void KeyboardInterrupt(GameState& _state, const KeyEvent& _event)
{
  ReadScanCode(_state, _event);
}

bool IsMouseDriverInstalled(Host& _host)
{
  return _host.MouseDevice().Present();
}

[[nodiscard]] Task<void> WaitForKeyPress(GameState& _state, Host& _host)
{
  // GetKey until a key comes, the JE back to the entry.
  for (;;)
  {
    const KeyPress key = GetKey(_state);
    if (key.scanCode != 0)
    {
      co_return;
    }
    co_await AwaitNextKey(_host, key);
  }
}

[[nodiscard]] Task<void> AwaitNextKey(Host& _host, const KeyPress& _key)
{
  if (_key.taken)
  {
    _host.Spin();
    co_return;
  }
  co_await _host.Wait();
}

Steering ReadSteering(GameState& _state, Host& _host)
{
  const std::uint8_t device = _state.Data().saveLoad.inputDevice;
  if (device == KEYBOARD_DEVICE)
  {
    return ApplyKeyboardRates(_state, ReadKeyboardSteering(_state));
  }
  return ReadMouseSteering(_state, _host);
}

KeyPress GetKey(GameState& _state)
{
  KeyPress key{false, 0, false};
  const std::uint8_t count = _state.Data().input.keyBufferCount;
  if (count != 0)
  {
    const std::uint16_t read = _state.Data().input.keyBufferRead;
    const std::uint8_t code = _state.Data().input.keyBuffer[read];
    _state.Data().input.keyBufferRead = static_cast<std::uint16_t>((read + 1) & KEY_BUFFER_WRAP);
    _state.Data().input.keyBufferCount = static_cast<std::uint8_t>(count - 1);
    key = KeyPress{true, static_cast<std::uint8_t>(code & ~SHIFT_BIT), (code & SHIFT_BIT) != 0};
  }
  return key;
}

void ResetKeyboard(GameState& _state)
{
  // REP STOSW: every key released, two at a time.
  for (std::uint8_t& key : KeyDown(_state))
  {
    key = 0;
  }
  _state.Data().input.keyBufferCount = 0;
  _state.Data().input.keyBufferWrite = 0;
  _state.Data().input.keyBufferRead = 0;
  _state.Data().playerShip.rollRate = 0;
}

void ReadScanCode(GameState& _state, const KeyEvent& _event)
{
  const KeyEvent event = _event;
  const auto key = static_cast<std::uint8_t>(event.key & ~SHIFT_BIT);
  if (!event.down)
  {
    KeyDown(_state)[key] = 0;
    if (key == D_CODE && _state.Data().flow.inFlight == 1)
    {
      _state.Data().playerShip.dockingKeyReleased = 1;
    }
  }
  else
  {
    if (key == W_CODE && _state.Data().keyboard.keyDown[Keys::ALT] == 1)
    {
      _state.Data().jump.forceMisjump = 1;
    }
    KeyDown(_state)[key] = 1;
    std::uint8_t code = key;
    if (_state.Data().keyboard.keyDown[Keys::LEFT_SHIFT] == 1 || _state.Data().keyboard.keyDown[Keys::RIGHT_SHIFT] == 1)
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

bool ReadFireButton(const GameState& _state, Host& _host)
{
  const std::uint8_t device = _state.Data().saveLoad.inputDevice;
  if (device == KEYBOARD_DEVICE)
  {
    return Pressed(_state.Data().keyboard.keyDown[Keys::SPACE]);
  }
  return (_host.MouseDevice().Buttons() & MOUSE_LEFT_AND_RIGHT) != 0;
}

Steering ReadKeyboardSteering(GameState& _state)
{
  std::uint8_t roll = 0;
  std::uint8_t pitch = 0;
  const auto held = [](const std::uint8_t& _first, const std::uint8_t& _second) { return (_first | _second) != 0; };
  if (held(_state.Data().keyboard.keyDown[Keys::UP], _state.Data().keyboard.keyDown[Keys::Q]))
  {
    --pitch;
  }
  if (held(_state.Data().keyboard.keyDown[Keys::DOWN], _state.Data().keyboard.keyDown[Keys::A]))
  {
    ++pitch;
  }
  if (held(_state.Data().keyboard.keyDown[Keys::LEFT], _state.Data().keyboard.keyDown[Keys::O]))
  {
    --roll;
  }
  if (held(_state.Data().keyboard.keyDown[Keys::RIGHT], _state.Data().keyboard.keyDown[Keys::P]))
  {
    ++roll;
  }
  RampAxis(roll, _state.Data().input.keyboardLastRollKey, _state.Data().input.keyboardRollRamp);
  RampAxis(pitch, _state.Data().input.keyboardLastPitchKey, _state.Data().input.keyboardPitchRamp);
  return Steering{_state.Data().input.keyboardRollRamp, Negate(_state.Data().input.keyboardPitchRamp)};
}

Steering ReadMouseSteering(GameState& _state, Host& _host)
{
  const MouseMotion motion = _host.MouseDevice().Motion();
  const std::uint8_t rollStep = MouseAxis(motion.acrossMickeys);
  const std::uint8_t pitchStep = MouseAxis(motion.downMickeys);
  // NEG AH / SAR AL,1 / SAR AH,1: half of each step, the pitch negated, added to the rates the last frame left.
  return Steering{MouseRate(rollStep, Low(_state.Data().playerShip.rollRate)),
                  MouseRate(Negate(pitchStep), High(_state.Data().playerShip.rollRate))};
}

void ResetMouseIfSelected(const GameState& _state, Host& _host)
{
  if (_state.Data().saveLoad.inputDevice != MOUSE_DEVICE)
  {
    return;
  }
  _host.MouseDevice().Reset();
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

} // namespace Elite
