// GameLogic/Input.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's input routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the keyboard and the mouse.
// Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014). The keyboard's interrupt
// handler also has its entry, at the end, which int 9 reaches (ADR-010).

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> InputEntries() noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState and the devices (ADR-014) ──

/// What IsMouseDriverInstalled finds at the int 33h vector.
struct MouseDriver
{
  bool installed; ///< the vector is set and does not point at an IRET
};

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
  bool screenshot;       ///< Alt and PrtSc were held, and it saved a screenshot
};

/// KeyboardInterrupt (CS:0201): int 9's handler, without the IRET: ReadScanCode.
void KeyboardInterrupt(GameState& _state, Hardware& _hardware);

/// IsMouseDriverInstalled (CS:02D4): the int 33h vector in the interrupt table, and whether a driver is behind it: whether
/// the code it names is not an IRET.
[[nodiscard]] MouseDriver IsMouseDriverInstalled(Hardware& _hardware);

/// ResetKeyboard (CS:7668): every key up, keyBuffer empty, rollRate zero, with interrupts off; then interrupts on.
void ResetKeyboard(GameState& _state, Hardware& _hardware);

/// ReadScanCode (CS:7443): one scan code from the keyboard, acknowledged, into keyDown and keyBuffer, then the end of the
/// interrupt.
void ReadScanCode(GameState& _state, Hardware& _hardware);

/// ReadFireButton (CS:74E0): whether fire is pressed on the selected device: the keyboard's space, or the mouse's buttons
/// (int 33h AX=5). The joystick went with D81.
[[nodiscard]] bool ReadFireButton(const GameState& _state, Hardware& _hardware);

/// ReadMouseSteering (CS:797E): the roll and the pitch from the mouse's motion (int 33h AX=0Bh), added to the rates the last
/// frame left, within -23..23, with -1 and 1 snapped to 0; the buttons (int 33h AX=5) into
/// mouseButtons, and the left one into fireLatch.
[[nodiscard]] Steering ReadMouseSteering(GameState& _state, Hardware& _hardware);

/// ResetMouseIfSelected (CS:7F5D): the mouse driver reset (int 33h AX=0) when the mouse is the input device.
void ResetMouseIfSelected(const GameState& _state, Hardware& _hardware);

/// ReadKeyboardSteering (CS:78EF): the cursor and QAOP keys ramped into keyboardRollRamp and keyboardPitchRamp. Returns
/// the roll ramp and the pitch ramp negated.
[[nodiscard]] Steering ReadKeyboardSteering(GameState& _state);

/// WaitForKeyPress (CS:6DEC): GetKey until it gives a key. Waits as a rule, a turn of its loop for each time GetKey finds none
/// (ADR-015).
void WaitForKeyPress(GameState& _state, Hardware& _hardware);

/// After a wait's GetKey that brought no key the wait wants (ADR-039): when it took no code and saved no screenshot, nothing
/// changed and the wait idles (Hardware::Wait); when it took one or saved one, the original's next poll comes at once
/// (Hardware::Spin).
void AwaitNextKey(Hardware& _hardware, const KeyPress& _key);

/// ReadSteering (CS:7536): the roll and the pitch from the selected device, within -23..23: the keyboard's (ReadKeyboardSteering)
/// integrated into keyboardRollRate and keyboardPitchRate, or the mouse's (ReadMouseSteering).
[[nodiscard]] Steering ReadSteering(GameState& _state, Hardware& _hardware);

/// GetKey (CS:7616): the next code from keyBuffer, taken with interrupts off; then interrupts on, and SaveScreenshot while Alt and
/// PrtSc are held.
[[nodiscard]] KeyPress GetKey(GameState& _state, Hardware& _hardware);

/// PollScreenDumpKey (CS:7F3D): SaveScreenshot while Alt and PrtSc are held.
void PollScreenDumpKey(GameState& _state, Hardware& _hardware);

/// ApplyReverseControls (CS:8EA5): the reversing options on _steering: reverseYControl negates the pitch, and
/// reverseXAndY then both. ApplyReverseControlsToDx (CS:8EBA) is the same routine on DL and DH, and its callers call this.
[[nodiscard]] Steering ApplyReverseControls(const GameState& _state, Steering _steering);

// ── The entry of the keyboard's interrupt handler ──

void KeyboardInterruptEntry(Guest& _guest); ///< Preserves every register.

} // namespace Elite
