// GameLogic/Docking.h
#pragma once

#include "GameState.h"
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

/// DrawTunnelRectangle (CS:1AA0): the four edges between the five points at SI (x, y signed bytes about the view's
/// centre), by DrawLine in drawColor. Clobbers all.
void DrawTunnelRectangle(Guest& _guest);

/// PlayStationTunnel (CS:2D5B): the tunnel between the station and space, twenty frames, leaving or
/// docking as playerDocked says. Waits. Clobbers all.
void PlayStationTunnel(Guest& _guest);

/// ToggleDockingComputer (CS:83B2): D, released: the docking computer off, or on when inside the station's
/// safe zone and the station has not been shot. AX comes back the message posted.
void ToggleDockingComputer(Guest& _guest);

/// RunDockingComputer (CS:8622): one frame of the docking computer's flight, by dockingComputerState.
/// Out: dockingComputerSteering and rollRate, the steering for the frame.
void RunDockingComputer(Guest& _guest);

/// CancelDockingComputer (CS:8BAA): the docking computer off, if it is on.
void CancelDockingComputer(Guest& _guest);

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// CheckDockingAlignment (CS:2D0F): whether the player's angles are within _tolerance of those that dock with the
/// station in _station: pitch near 0 with yaw near a half turn, or the other way round, and roll near the station's
/// spin or half a turn from it.
[[nodiscard]] bool CheckDockingAlignment(const GameState& _state, const ObjectSlot& _station, std::uint16_t _tolerance);

/// MaskOutsideTunnel (CS:2E0A): zeroes the space-view buffer outside the centred rectangle whose top-left corner is at
/// DS:_rectangle: the rows above it and its left margins each forwards or, _backwards, down (the direction flag it is
/// called with), then the rows below it and its right margins from the end of the buffer down.
void MaskOutsideTunnel(GameState& _state, std::uint16_t _rectangle, bool _backwards);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void CheckDockingAlignmentEntry(Guest& _guest); ///< In: BX the tolerance, DI the station's slot. Out: CF; AX, CX and DX clobbered.
void MaskOutsideTunnelEntry(Guest& _guest);     ///< In: SI the rectangle. Out: ES=B800, DF=0; clobbers all but ES.

} // namespace Elite
