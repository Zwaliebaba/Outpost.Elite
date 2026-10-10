// GameLogic/Input.h
#pragma once

#include "GameState.h"
#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's input routines, ported (plan §5 Phase 3, ADR-010): the keyboard, the joystick and the mouse. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> InputEntries() noexcept;

/// KeyboardInterrupt (CS:0201): int 9's handler, without the IRET: ReadScanCode with DS and ES set.
void KeyboardInterrupt(Guest& _guest);

/// WaitForKeyPress (CS:6DEC): GetKey until a key comes, the loop's turns ended where the original's are. Out: AH = its scan code.
/// Waits as a rule.
void WaitForKeyPress(Guest& _guest);

/// ReadScanCode (CS:7443): one scan code from the keyboard into keyDown and keyBuffer, then the end of the interrupt.
/// AX clobbered.
void ReadScanCode(Guest& _guest);

/// ReadFireButton (CS:74E0): CF=1 while fire is pressed on the selected device. AL clobbered (and BX, CX and DX by
/// the mouse, DX by the IBM stick).
void ReadFireButton(Guest& _guest);

/// ReadSteering (CS:7536): AL=roll and AH=pitch input from the selected device, +-23. BX clobbered (and CX and DX by
/// the joystick and the mouse). Waits sometimes, in ReadJoystickAxes.
void ReadSteering(Guest& _guest);

/// GetKey (CS:7616): the next key from keyBuffer: ZF=0, AH=scan code, AL=AL<<1 | Shift; or ZF=1, AH=0 when there is
/// none. Interrupts on.
void GetKey(Guest& _guest);

/// ReadJoystickAxes (CS:777E): the IBM stick's two axes as counted polls: CF clear, BX = X and CX = Y; CF set when the stick does
/// not answer or a count times out, with interrupts then left off. DX = 201h, AX clobbered; fireLatch set while button 1 is down.
/// Waits when X's one-shot drops before Y's, for Y's.
void ReadJoystickAxes(Guest& _guest);

/// ReadJoystickSteering (CS:77C1): AL = roll and AH = pitch from the joystick: the Amstrad's keys ramped, or the IBM stick about its
/// centre, within -23..23; AX = 0 when the IBM stick does not answer. BX, CX and DX clobbered. Waits sometimes, in
/// ReadJoystickAxes.
void ReadJoystickSteering(Guest& _guest);

/// ReadMouseSteering (CS:797E): AL = roll and AH = pitch from the mouse's motion (int 33h), added to the rates the last frame
/// left, within -23..23, with -1 and 1 snapped to 0; the buttons into mouseButtons and fireLatch. BX, CX and DX clobbered.
void ReadMouseSteering(Guest& _guest);

/// PollScreenDumpKey (CS:7F3D): SaveScreenshot while Alt and PrtSc are held.
void PollScreenDumpKey(Guest& _guest);

/// ResetMouseIfSelected (CS:7F5D): int 33h AX=0 when the mouse is the input device. AX and BX clobbered.
void ResetMouseIfSelected(Guest& _guest);

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

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

/// IsMouseDriverInstalled (CS:02D4): the int 33h vector in the interrupt table, and whether a driver is behind it.
[[nodiscard]] MouseDriver IsMouseDriverInstalled(const GameState& _state);

/// ResetKeyboard (CS:7668): every key up, keyBuffer empty, rollRate zero.
void ResetKeyboard(GameState& _state);

/// ReadKeyboardSteering (CS:78EF): the cursor and QAOP keys ramped into keyboardRollRamp and keyboardPitchRamp. Returns
/// the roll ramp and the pitch ramp negated.
[[nodiscard]] Steering ReadKeyboardSteering(GameState& _state);

/// ApplyReverseControls (CS:8EA5): the reversing options on _steering: reverseYControl negates the pitch, and
/// reverseXAndY then both. ApplyReverseControlsToDx (CS:8EBA) is the same routine on DL and DH: its entry calls this.
[[nodiscard]] Steering ApplyReverseControls(const GameState& _state, Steering _steering);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void IsMouseDriverInstalledEntry(Guest& _guest);   ///< Out: ZF=0 if installed; ES the vector's segment.
void ResetKeyboardEntry(Guest& _guest);            ///< Out: IF=1.
void ReadKeyboardSteeringEntry(Guest& _guest);     ///< Out: AL=roll, AH=pitch; BX clobbered.
void ApplyReverseControlsEntry(Guest& _guest);     ///< In/out: AL=roll, AH=pitch.
void ApplyReverseControlsToDxEntry(Guest& _guest); ///< In/out: DL=roll, DH=pitch.

} // namespace Elite
