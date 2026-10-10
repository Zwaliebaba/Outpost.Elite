// GameLogic/Docking.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "NativeEntry.h"
#include "ObjectSlot.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's docking routines, ported (plan §5 Phase 3, ADR-010): docking, by hand and by computer. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv. The routines de-assembled so far (ADR-012) take
// values and give values back, and their entries, at the end, keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> DockingEntries() noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// PlayStationTunnel (CS:2D5B): the tunnel between the station and space, leaving or docking as playerDocked says: the message
/// for a frame (UpdateMessageLine) and the dashboard, then twenty frames of tunnelRectangles, the first ten one to ten of them and
/// the last ten ten to one, from one further along each time, each presented (FinishSpaceViewFrame) and the first ten followed by
/// 20 timer ticks. Leaving, the world moves on behind the tunnel (TransformAndDrawObjects, MoveObjectsByVelocity,
/// UpdateStardust), masked outside it (MaskOutsideTunnel); docking, each frame waits for the timer first. _backward is the
/// direction flag; it leaves it clear. Waits as a rule (ADR-015).
void PlayStationTunnel(GameState& _state, Hardware& _hardware, bool _backward);

/// What a frame of the docking computer did.
struct DockingStep
{
  std::uint8_t state; ///< dockingComputerState as the frame found it
  /// What the state leaves in AX: 1 and 5 the roll they aim for; 2 and 6 the roll angle once within tolerance, else the steering;
  /// 3, 7, 10 and 11 the steering; 9 the station's spin; 4 and 8 playerVelocityZ, or, once state 8 stops, the last random
  /// number ResetStardust drew with the last lifetime over its low byte. Nothing for states 0 and 12 and up.
  std::uint16_t result;
  bool stopped; ///< state 8 came within 650 of the station and scattered the stardust
};

/// RunDockingComputer (CS:8622): one frame of the docking computer's flight, by dockingComputerState: 0 slow to a stop; 1 to 3
/// roll and then pitch towards the approach point, 2000 out from the slot, twice; 4 fly to it; 5 to 7 the same towards the
/// station; 8 close in until within 650, then the front view, locked; 9 to 11 roll to match the station's spin. Its steering for
/// the frame is in dockingComputerSteering and rollRate, cleared first.
DockingStep RunDockingComputer(GameState& _state);

/// DrawTunnelRectangle (CS:1AA0): the four edges between the five points at DS:_points, x and y signed bytes about the view's
/// centre, by DrawLine in drawColor. Returns whether any DrawLine filled bytes with REP STOSB (DrawLineOut).
bool DrawTunnelRectangle(GameState& _state, std::uint16_t _points);

/// CheckDockingAlignment (CS:2D0F): whether the player's angles are within _tolerance of those that dock with the
/// station in _station: pitch near 0 with yaw near a half turn, or the other way round, and roll near the station's
/// spin or half a turn from it.
[[nodiscard]] bool CheckDockingAlignment(const GameState& _state, const ObjectSlot& _station, std::uint16_t _tolerance);

/// MaskOutsideTunnel (CS:2E0A): zeroes the space-view buffer outside the centred rectangle whose top-left corner is at
/// DS:_rectangle: the rows above it and its left margins each forwards or, _backwards, down (the direction flag it is
/// called with), then the rows below it and its right margins from the end of the buffer down.
void MaskOutsideTunnel(GameState& _state, std::uint16_t _rectangle, bool _backwards);

/// ToggleDockingComputer (CS:83B2): D, released: dockingKeyReleased cleared, and the docking computer off (all sound stopped,
/// the view unlocked, a stopped ship given speed 4, the low beep), or on (state 0, the music and the beep) when inside the
/// station's safe zone and the station has not been shot; refused with the low beep otherwise. Returns the message it posts for
/// 25 frames.
std::uint16_t ToggleDockingComputer(GameState& _state, Hardware& _hardware);

/// CancelDockingComputer (CS:8BAA): the docking computer off, if it is on: dataA137 cleared, the view unlocked and all sound
/// stopped (StopAllSound).
void CancelDockingComputer(GameState& _state, Hardware& _hardware);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void DrawTunnelRectangleEntry(Guest& _guest);   ///< In: SI the points. Out: ES=DS and DF=0 after a REP STOSB; clobbers all.
void PlayStationTunnelEntry(Guest& _guest);     ///< Out: DF=0, BP=20h. Clobbers all but DS and BP.
void CheckDockingAlignmentEntry(Guest& _guest); ///< In: BX the tolerance, DI the station's slot. Out: CF; AX, CX and DX clobbered.
void MaskOutsideTunnelEntry(Guest& _guest);     ///< In: SI the rectangle. Out: ES=B800, DF=0; clobbers all but ES.
void ToggleDockingComputerEntry(Guest& _guest); ///< Out: AX the message, and BX the station's slot once it looks at it.
void CancelDockingComputerEntry(Guest& _guest); ///< Preserves every register.
/// Out: AX, SI and DI as each state leaves them (DockingStep); BX, CX and DX clobbered.
void RunDockingComputerEntry(Guest& _guest);

} // namespace Elite
