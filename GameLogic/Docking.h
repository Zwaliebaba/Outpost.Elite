// GameLogic/Docking.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's docking routines, ported (plan §5 Phase 3, ADR-010): docking, by hand and by computer. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> DockingEntries() noexcept;

/// DrawTunnelRectangle (CS:1AA0): the four edges between the five points at SI (x, y signed bytes about the view's
/// centre), by DrawLine in drawColor. Clobbers all.
void DrawTunnelRectangle(Guest& _guest);

/// CheckDockingAlignment (CS:2D0F): CF=1 when the player's angles are within BX of the station's in slot DI. AX, CX
/// and DX clobbered.
void CheckDockingAlignment(Guest& _guest);

/// PlayStationTunnel (CS:2D5B): the tunnel between the station and space, twenty frames, leaving or
/// docking as playerDocked says. Waits. Clobbers all.
void PlayStationTunnel(Guest& _guest);

/// MaskOutsideTunnel (CS:2E0A): zeroes the space-view buffer outside the centred rectangle whose top-left corner is
/// at SI. Out: ES=B800; clobbers all.
void MaskOutsideTunnel(Guest& _guest);

/// ToggleDockingComputer (CS:83B2): D, released: the docking computer off, or on when inside the station's
/// safe zone and the station has not been shot. AX comes back the message posted.
void ToggleDockingComputer(Guest& _guest);

/// RunDockingComputer (CS:8622): one frame of the docking computer's flight, by dockingComputerState.
/// Out: dockingComputerSteering and rollRate, the steering for the frame.
void RunDockingComputer(Guest& _guest);

/// CancelDockingComputer (CS:8BAA): the docking computer off, if it is on.
void CancelDockingComputer(Guest& _guest);

} // namespace Elite
