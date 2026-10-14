// GameLogic/Docking.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "SpaceObject.h"

#include <cstdint>

namespace Elite
{

// The reference's docking routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): docking, by hand and by computer. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// PlayStationTunnel (CS:2D5B): the tunnel between the station and space, leaving or docking as playerDocked says: the message
/// for a frame (UpdateMessageLine) and the dashboard, then twenty frames of tunnelRectangles, the first ten one to ten of them and
/// the last ten ten to one, from one further along each time, each presented (FinishSpaceViewFrame) and the first ten followed by
/// 20 timer ticks. Leaving, the world moves on behind the tunnel (TransformAndDrawObjects, MoveObjectsByVelocity,
/// UpdateStardust), masked outside it (MaskOutsideTunnel); docking, each frame waits for the timer first. Waits as a rule (ADR-015).
[[nodiscard]] Task<void> PlayStationTunnel(GameState& _state, Hardware& _hardware);

/// RunDockingComputer (CS:8622): one frame of the docking computer's flight, by dockingComputerState: 0 slow to a stop; 1 to 3
/// roll and then pitch towards the approach point, 2000 out from the slot, twice; 4 fly to it; 5 to 7 the same towards the
/// station; 8 close in until within 650, then the front view, locked; 9 to 11 roll to match the station's spin. Its steering for
/// the frame is in dockingComputerSteering and rollRate, cleared first.
void RunDockingComputer(GameState& _state);

/// DrawTunnelRectangle (CS:1AA0): the four edges between the five points of rectangle _rectangle of tunnelRectangles, x and y
/// signed bytes about the view's centre, by DrawLine in drawColor.
void DrawTunnelRectangle(GameState& _state, std::size_t _rectangle);

/// CheckDockingAlignment (CS:2D0F): whether the player's angles are within _tolerance of those that dock with the
/// station in _station: pitch near 0 with yaw near a half turn, or the other way round, and roll near the station's
/// spin or half a turn from it.
[[nodiscard]] bool CheckDockingAlignment(const GameState& _state, const SpaceObject& _station, std::uint16_t _tolerance);

/// MaskOutsideTunnel (CS:2E0A): zeroes the space-view buffer outside the centred rectangle whose top-left corner is the first point of
/// rectangle _rectangle of tunnelRectangles: the rows above it and its left margins each forwards, as the direction flag is clear wherever
/// the game calls it (change 3c), then, after its STD, the rows below it and its right margins from the end of the buffer down, and its
/// CLD.
void MaskOutsideTunnel(GameState& _state, std::size_t _rectangle);

/// ToggleDockingComputer (CS:83B2): D, released: dockingKeyReleased cleared, and the docking computer off (all sound stopped,
/// the view unlocked, a stopped ship given speed 4, the low beep), or on (state 0, the music and the beep) when inside the
/// station's safe zone and the station has not been shot; refused with the low beep otherwise. Each way posts its message for 25
/// frames.
void ToggleDockingComputer(GameState& _state, Hardware& _hardware);

/// CancelDockingComputer (CS:8BAA): the docking computer off, if it is on: dataA137 cleared, the view unlocked and all sound
/// stopped (StopAllSound).
void CancelDockingComputer(GameState& _state, Hardware& _hardware);

} // namespace Elite
