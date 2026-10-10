// GameLogic/Flight.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's flight routines, ported (plan §5 Phase 3, ADR-010): flying: the player's ship, the local bubble, the dashboard's logic. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> FlightEntries() noexcept;

/// UpdateStardust (CS:068F): moves and draws the 30 stardust particles for viewAngle, once a frame.
void UpdateStardust(Guest& _guest);

/// ComputeStardustShift (CS:0887): stardustShift from playerSpeed, one less while jumpDriveEngaged.
/// AX clobbered, BL = 12.
void ComputeStardustShift(Guest& _guest);

/// ComputeDustStripMask (CS:08B9): BP = 2^n - 1 just covering |DX|/2. AX comes back 0.
void ComputeDustStripMask(Guest& _guest);

/// RollStardust (CS:0927): rotates every particle by rotationSinCos[7].
void RollStardust(Guest& _guest);

/// ShiftStardustVertically (CS:0940): adds DX to y of every on-screen particle, respawning those that
/// leave on the entering edge.
void ShiftStardustVertically(Guest& _guest);

/// RespawnDustAtVerticalEdge (CS:0969): a new particle at SI on the edge DX moves away from, in the
/// strip BP covers. Out: AX = x, BX = y.
void RespawnDustAtVerticalEdge(Guest& _guest);

/// RespawnDustAnywhere (CS:09A3): a new particle at SI anywhere on screen. Out: AX = x, BX = y.
void RespawnDustAnywhere(Guest& _guest);

/// IsDustOnScreen (CS:09D6): CF set when AH is in -0x20..0x1F and BH in -0x10..0x0F.
void IsDustOnScreen(Guest& _guest);

/// ScaleDustStep (CS:0A06): CL = stardustShift, BP = AX >> CL and DX = BX >> CL, arithmetic.
void ScaleDustStep(Guest& _guest);

/// LoadDustPosition (CS:0A13): AX, BX = the particle at SI.
void LoadDustPosition(Guest& _guest);

/// StoreDustPosition (CS:0A19): the particle at SI = AX, BX.
void StoreDustPosition(Guest& _guest);

/// StorePreviousDustPosition (CS:0A1F): the particle's entry in stardustPrevious = AX, BX.
void StorePreviousDustPosition(Guest& _guest);

/// DustToScreen (CS:0A28): AL, BL = the draw buffer position of the particle at AX, BX.
void DustToScreen(Guest& _guest);

/// ResetStardust (CS:0A43): scatters all 30 particles at random.
void ResetStardust(Guest& _guest);

/// HandleFlightFunctionKeys (CS:0BB3): F1-F10 in flight. Out: AH = the F1-F4 scan code or 0; BX and CX
/// come back as the scan leaves them.
void HandleFlightFunctionKeys(Guest& _guest);

/// InvalidateDashboard (CS:2540): fills the 22 dashboard cache bytes with 0x80. Out: ES = DS.
void InvalidateDashboard(Guest& _guest);

/// UpdateDashboard (CS:254F): redraws every gauge whose value changed. Out: ES = B800h.
void UpdateDashboard(Guest& _guest);

/// DrawFiveLineBar (CS:2645): a bar 5 scanlines high for AL at ES:DI.
void DrawFiveLineBar(Guest& _guest);

/// DrawSignedIndicator (CS:26BE): the roll or pitch indicator for the signed AL at ES:DI.
void DrawSignedIndicator(Guest& _guest);

/// DrawThreeLineBar (CS:273E): a bar 3 scanlines high for AL at ES:DI.
void DrawThreeLineBar(Guest& _guest);

/// DrawMissileIcons (CS:2799): the missile icons, when missileCount changed.
void DrawMissileIcons(Guest& _guest);

/// DrawMissileLockIndicator (CS:2804): the lock block, when missileState changed.
void DrawMissileLockIndicator(Guest& _guest);

/// DrawEnergyBanks (CS:283B): splits playerEnergy into the four banks and redraws those that changed.
void DrawEnergyBanks(Guest& _guest);

/// UpdateEnergyAndLaserHeat (CS:2882): cools the laser, recharges energy and shields, and at low energy
/// may lose a piece of equipment.
void UpdateEnergyAndLaserHeat(Guest& _guest);

/// DrawConditionLight (CS:290C): the condition light in conditionColor, when it changed.
void DrawConditionLight(Guest& _guest);

/// UpdateConditionColor (CS:2959): conditionColor from the ship's state. AL = the colour.
void UpdateConditionColor(Guest& _guest);

/// SetUpLocalSpace (CS:29D0): the current system's sun, planet and station, and the flight variables
/// reset.
void SetUpLocalSpace(Guest& _guest);

/// CheckCollisions (CS:2BC5): the player against every object slot: damage, or docking.
void CheckCollisions(Guest& _guest);

/// InSafeZone (CS:2E63): CF = bit 0 of safeZoneFlags, AL = the rest.
void InSafeZone(Guest& _guest);

/// UpdateSafeZone (CS:2E69): safeZoneFlags from the station's distance. DI comes back stationSlot.
void UpdateSafeZone(Guest& _guest);

/// UpdateWarnings (CS:36B6): re-posts the current warning, or tries the four warning checks.
void UpdateWarnings(Guest& _guest);

/// UpdateScannerBlip (CS:40EC): moves slot DI's scanner blip to camera-frame AX, BX, CX.
void UpdateScannerBlip(Guest& _guest);

/// UpdateCompass (CS:418F): moves the compass dot to the planet or the station. Every register comes
/// back as the original leaves it; DI = stationSlot.
void UpdateCompass(Guest& _guest);

/// XorCompassDot (CS:42A4): the compass dot at DL, DH, solid when BP is non-zero. DX comes back one
/// right and one up.
void XorCompassDot(Guest& _guest);

/// EraseScannerBlip (CS:42D6): erases slot DI's scanner blip, if it has one.
void EraseScannerBlip(Guest& _guest);

/// XorScannerBlip (CS:42F6): the scanner blip for the scanner bytes AH, BH, CH.
void XorScannerBlip(Guest& _guest);

/// XorDashboardPixel (CS:43C4): XORs the dashboard pixel at DL, DH. AX comes back DX.
void XorDashboardPixel(Guest& _guest);

/// EraseCompassAndBlips (CS:4594): erases the compass dot and every scanner blip.
void EraseCompassAndBlips(Guest& _guest);

/// UpdateFuelLeak (CS:499F): runs a fuel leak, and sets the border colour.
void UpdateFuelLeak(Guest& _guest);

/// TickEscapePod (CS:7F69): counts the escape pod down; when it arrives, pops the return address
/// into AX so that the caller's caller is returned to.
void TickEscapePod(Guest& _guest);

/// ProcessFlightKeys (CS:7FA8): the flight controls other than steering.
void ProcessFlightKeys(Guest& _guest);

/// UpdatePlayerMotion (CS:8472): speed, roll and pitch for this frame, then the world moves.
void UpdatePlayerMotion(Guest& _guest);

/// UpdatePlayerVelocity (CS:8599): playerVelocity from playerSpeed and the angles, when velocityDirty.
void UpdatePlayerVelocity(Guest& _guest);

/// MoveObjectsByVelocity (CS:85EC): moves every slot by the player's velocity.
void MoveObjectsByVelocity(Guest& _guest);

} // namespace Elite
