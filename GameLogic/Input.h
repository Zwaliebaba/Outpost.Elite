// GameLogic/Input.h
#pragma once

#include "GameState.h"
#include "Host.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's input routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the keyboard and the mouse.
// Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014). The game installs
// KeyboardInterrupt as its keys' handler (Game).

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState and the devices (ADR-014) ──

/// A roll and a pitch, signed bytes, as the steering routines give them.
struct Steering
{
  std::uint8_t roll;
  std::uint8_t pitch;
};

/// What GetKey takes from keyBuffer.
struct KeyPress
{
  bool taken;            ///< keyBuffer held a code, and GetKey took it
  std::uint8_t scanCode; ///< the code's scan code, 01h-7Fh; 0 when it took none, which its callers read as no key
  bool shift;            ///< Shift was held when the key went down
};

/// KeyboardInterrupt (CS:0201): the game's handler of a key event, which the host runs with each (D91): ReadScanCode.
void KeyboardInterrupt(GameState& _state, const KeyEvent& _event);

/// IsMouseDriverInstalled (CS:02D4): whether a mouse is attached (Mouse::Present), where the original looked for a driver
/// behind int 33h's vector.
[[nodiscard]] bool IsMouseDriverInstalled(Host& _host);

/// ResetKeyboard (CS:7668): every key up, keyBuffer empty, rollRate zero.
void ResetKeyboard(GameState& _state);

/// ReadScanCode (CS:7443): _event into keyDown and keyBuffer (D88).
void ReadScanCode(GameState& _state, const KeyEvent& _event);

/// ReadFireButton (CS:74E0): whether fire is pressed on the selected device: the keyboard's space, or either of the mouse's
/// buttons. The joystick went with D81.
[[nodiscard]] bool ReadFireButton(const GameState& _state, Host& _host);

/// ReadMouseSteering (CS:797E): the roll and the pitch from the mouse's motion since the last frame, added to the rates the
/// last frame left, within -23..23, with -1 and 1 snapped to 0.
[[nodiscard]] Steering ReadMouseSteering(GameState& _state, Host& _host);

/// ResetMouseIfSelected (CS:7F5D): the mouse's motion so far forgotten (Mouse::Reset) when the mouse is the input device.
void ResetMouseIfSelected(const GameState& _state, Host& _host);

/// ReadKeyboardSteering (CS:78EF): the cursor and QAOP keys ramped into keyboardRollRamp and keyboardPitchRamp. Returns
/// the roll ramp and the pitch ramp negated.
[[nodiscard]] Steering ReadKeyboardSteering(GameState& _state);

/// WaitForKeyPress (CS:6DEC): GetKey until it gives a key. Waits as a rule, a turn of its loop for each time GetKey finds none
/// (ADR-015).
[[nodiscard]] Task<void> WaitForKeyPress(GameState& _state, Host& _host);

/// After a wait's GetKey that brought no key the wait wants (ADR-039): when it took no code, nothing changed and the wait idles
/// (Host::Wait); when it took one, the original's next poll comes at once (Host::Spin).
[[nodiscard]] Task<void> AwaitNextKey(Host& _host, const KeyPress& _key);

/// ReadSteering (CS:7536): the roll and the pitch from the selected device, within -23..23: the keyboard's (ReadKeyboardSteering)
/// integrated into keyboardRollRate and keyboardPitchRate, or the mouse's (ReadMouseSteering).
[[nodiscard]] Steering ReadSteering(GameState& _state, Host& _host);

/// GetKey (CS:7616): the next code from keyBuffer, taken. Its screenshot while Alt and PrtSc
/// are held went with D86.
[[nodiscard]] KeyPress GetKey(GameState& _state);

/// ApplyReverseControls (CS:8EA5): the reversing options on _steering: reverseYControl negates the pitch, and
/// reverseXAndY then both. ApplyReverseControlsToDx (CS:8EBA) is the same routine on DL and DH, and its callers call this.
[[nodiscard]] Steering ApplyReverseControls(const GameState& _state, Steering _steering);

} // namespace Elite
