// GameLogic/Input.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's input routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the keyboard, the joystick and
// the mouse. The routines take values and give values back, on the GameState and the devices (ADR-014), and their entries keep
// the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> InputEntries() noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState and the devices (ADR-014) ──

/// What IsMouseDriverInstalled finds at the int 33h vector.
struct MouseDriver
{
  bool installed;        ///< the vector is set and does not point at an IRET
  std::uint16_t segment; ///< the vector's segment, which the original leaves in ES
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

/// What ReadJoystickAxes counts: the polls of each of the IBM stick's axes before its one-shot drops.
struct StickAxes
{
  bool timedOut;   ///< the stick did not answer or a count wrapped; interrupts are then left off
  std::uint16_t x; ///< X's polls; on a time-out, the count as it stands, from 60000 (BX)
  std::uint16_t y; ///< Y's polls, likewise (CX)
};

/// KeyboardInterrupt (CS:0201): int 9's handler, without the IRET: ReadScanCode.
void KeyboardInterrupt(GameState& _state, Hardware& _hardware);

/// ReadJoystickAxes (CS:777E): the IBM stick's two axes as counted polls, with interrupts off from the first firing of the one-shots
/// (OUT 201h, of _trigger, which the port does not read) to the last poll; then the port's byte into joystickPortByte, and
/// fireLatch set while button 1 is down. Counts the 8088's cycles for its instructions, by which the port times its one-shots, and
/// waits when X's one-shot drops before Y's, for Y's.
[[nodiscard]] StickAxes ReadJoystickAxes(GameState& _state, Hardware& _hardware, std::uint8_t _trigger);

/// IsMouseDriverInstalled (CS:02D4): the int 33h vector in the interrupt table, and whether a driver is behind it.
[[nodiscard]] MouseDriver IsMouseDriverInstalled(const GameState& _state);

/// ResetKeyboard (CS:7668): every key up, keyBuffer empty, rollRate zero, with interrupts off; then interrupts on.
void ResetKeyboard(GameState& _state, Hardware& _hardware);

/// ReadScanCode (CS:7443): one scan code from the keyboard, acknowledged, into keyDown and keyBuffer, then the end of the
/// interrupt.
void ReadScanCode(GameState& _state, Hardware& _hardware);

/// ReadFireButton (CS:74E0): whether fire is pressed on the selected device: the keyboard's space, the Amstrad stick's fire
/// keys, the IBM stick's two buttons (IN 201h), or the mouse's buttons (int 33h AX=5) or the Amstrad mouse's keys.
[[nodiscard]] bool ReadFireButton(const GameState& _state, Hardware& _hardware);

/// ReadMouseSteering (CS:797E): the roll and the pitch from the mouse's motion (int 33h AX=0Bh), added to the rates the last
/// frame left, within -23..23, with -1 and 1 snapped to 0; the buttons (int 33h AX=5, or the Amstrad mouse's keys) into
/// mouseButtons, and the left one into fireLatch.
[[nodiscard]] Steering ReadMouseSteering(GameState& _state, Hardware& _hardware);

/// ResetMouseIfSelected (CS:7F5D): the mouse driver reset (int 33h AX=0) when the mouse is the input device.
void ResetMouseIfSelected(const GameState& _state, Hardware& _hardware);

/// ReadKeyboardSteering (CS:78EF): the cursor and QAOP keys ramped into keyboardRollRamp and keyboardPitchRamp. Returns
/// the roll ramp and the pitch ramp negated.
[[nodiscard]] Steering ReadKeyboardSteering(GameState& _state);

/// WaitForKeyPress (CS:6DEC): GetKey until it gives a key, which it returns, with screenshot set if any of its calls saved one.
/// Waits as a rule, a turn of its loop for each time GetKey finds none (ADR-015).
[[nodiscard]] KeyPress WaitForKeyPress(GameState& _state, Hardware& _hardware);

/// ReadSteering (CS:7536): the roll and the pitch from the selected device, within -23..23: the keyboard's (ReadKeyboardSteering)
/// or the Amstrad stick's integrated into keyboardRollRate and keyboardPitchRate, the IBM stick's (ReadJoystickSteering), or the
/// mouse's (ReadMouseSteering). _trigger is the byte the IBM stick's read writes to the game port, which the port does not read:
/// AL as ReadSteering's caller left it. Waits sometimes, in ReadJoystickAxes.
[[nodiscard]] Steering ReadSteering(GameState& _state, Hardware& _hardware, std::uint8_t _trigger);

/// GetKey (CS:7616): the next code from keyBuffer, taken with interrupts off; then interrupts on, and SaveScreenshot while Alt and
/// PrtSc are held.
[[nodiscard]] KeyPress GetKey(GameState& _state, Hardware& _hardware);

/// ReadJoystickSteering (CS:77C1): the roll and the pitch from the joystick: the Amstrad's keys ramped, or the IBM stick's
/// counts about joystickCenterX and joystickCenterY, each (count - centre) * 128 / centre by a divide that can trap, at most 127,
/// over 8 less a dead zone of 4, the pitch negated, within -23..23; 0 and 0 when the IBM stick does not answer. _trigger as for
/// ReadSteering. Waits sometimes, in ReadJoystickAxes.
[[nodiscard]] Steering ReadJoystickSteering(GameState& _state, Hardware& _hardware, std::uint8_t _trigger);

/// PollScreenDumpKey (CS:7F3D): SaveScreenshot while Alt and PrtSc are held. Returns whether it saved one.
bool PollScreenDumpKey(GameState& _state, Hardware& _hardware);

/// ApplyReverseControls (CS:8EA5): the reversing options on _steering: reverseYControl negates the pitch, and
/// reverseXAndY then both. ApplyReverseControlsToDx (CS:8EBA) is the same routine on DL and DH: its entry calls this.
[[nodiscard]] Steering ApplyReverseControls(const GameState& _state, Steering _steering);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void KeyboardInterruptEntry(Guest& _guest);        ///< Preserves every register.
void ReadJoystickAxesEntry(Guest& _guest);         ///< In: AL, to fire. Out: CF=1 on a time-out; BX=X, CX=Y, DX=201h; AX clobbered.
void IsMouseDriverInstalledEntry(Guest& _guest);   ///< Out: ZF=0 if installed; ES the vector's segment.
void ResetKeyboardEntry(Guest& _guest);            ///< Out: IF=1.
void ReadScanCodeEntry(Guest& _guest);             ///< AX clobbered.
void ReadFireButtonEntry(Guest& _guest);           ///< Out: CF=1 while fire is pressed; AX, BX, CX and DX clobbered.
void ReadMouseSteeringEntry(Guest& _guest);        ///< Out: AL=roll, AH=pitch; BX, CX and DX clobbered.
void ResetMouseIfSelectedEntry(Guest& _guest);     ///< AX and BX clobbered.
void ReadKeyboardSteeringEntry(Guest& _guest);     ///< Out: AL=roll, AH=pitch; BX clobbered.
void ApplyReverseControlsEntry(Guest& _guest);     ///< In/out: AL=roll, AH=pitch.
void ApplyReverseControlsToDxEntry(Guest& _guest); ///< In/out: DL=roll, DH=pitch.
void WaitForKeyPressEntry(Guest& _guest);          ///< Out: AH=scan code, AL=AL<<1 | Shift, as GetKey leaves them.
void ReadSteeringEntry(Guest& _guest);             ///< In: AL, to fire the stick. Out: AL=roll, AH=pitch; BX, CX and DX clobbered.
/// Out: ZF=0, AH=scan code and AL=AL<<1 | Shift; or ZF=1 and AH=0 when there is none. IF=1, and ES=B800h after a screenshot.
void GetKeyEntry(Guest& _guest);
void ReadJoystickSteeringEntry(Guest& _guest); ///< In: AL, to fire the stick. Out: AL=roll, AH=pitch; BX, CX and DX clobbered.
void PollScreenDumpKeyEntry(Guest& _guest);    ///< Out: ES=B800h after a screenshot.

} // namespace Elite
