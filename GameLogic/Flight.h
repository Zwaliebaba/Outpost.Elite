// GameLogic/Flight.h
#pragma once

#include "GameState.h"
#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's flight routines, ported (plan §5 Phase 3, ADR-010): flying: the player's ship, the local bubble, the dashboard's logic. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv. Those de-assembled so far (ADR-012) follow
// the bodies: they take values and give values back, and their entries keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> FlightEntries() noexcept;

/// UpdateStardust (CS:068F): moves and draws the 30 stardust particles for viewAngle, once a frame.
void UpdateStardust(Guest& _guest);

/// ComputeStardustShift (CS:0887): stardustShift from playerSpeed, one less while jumpDriveEngaged.
/// AX clobbered, BL = 12.
void ComputeStardustShift(Guest& _guest);

/// GetPreviousDustScreenPosition (CS:08A1): the particle at SI's position last frame. Out: CF clear and
/// CL, CH = its draw buffer position, or CF set when it was off screen. AX, BX clobbered.
void GetPreviousDustScreenPosition(Guest& _guest);

/// ShiftStardustSideways (CS:08CB): adds DX to x of every on-screen particle, respawning those that
/// leave on the entering edge.
void ShiftStardustSideways(Guest& _guest);

/// RespawnDustAtSideEdge (CS:08F2): a new particle at SI on the edge DX moves away from, in the strip BP
/// covers. Out: AX = x, BX = y.
void RespawnDustAtSideEdge(Guest& _guest);

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

/// ResetStardust (CS:0A43): scatters all 30 particles at random.
void ResetStardust(Guest& _guest);

/// HandleFlightFunctionKeys (CS:0BB3): F1-F10 in flight. Out: AH = the F1-F4 scan code or 0; BX and CX
/// come back as the scan leaves them.
void HandleFlightFunctionKeys(Guest& _guest);

/// RestoreFlightScreen (CS:15CF): the cockpit back after a function key's screen, with a beep.
void RestoreFlightScreen(Guest& _guest);

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

/// DrawEnergyBanks (CS:283B): splits playerEnergy into the four banks and redraws those that changed.
void DrawEnergyBanks(Guest& _guest);

/// UpdateEnergyAndLaserHeat (CS:2882): cools the laser, recharges energy and shields, and at low energy
/// may lose a piece of equipment.
void UpdateEnergyAndLaserHeat(Guest& _guest);

/// SetUpLocalSpace (CS:29D0): the current system's sun, planet and station, and the flight variables
/// reset.
void SetUpLocalSpace(Guest& _guest);

/// CheckCollisions (CS:2BC5): the player against every object slot: damage, or docking.
void CheckCollisions(Guest& _guest);

/// UpdateSafeZone (CS:2E69): safeZoneFlags from the station's distance. DI comes back stationSlot.
void UpdateSafeZone(Guest& _guest);

/// ComputeDeathDebrisVector (CS:2F8B): (0, 40, 0) turned by the view, the roll off the front view, the
/// yaw and the pitch. Out: AX, BX, CX.
void ComputeDeathDebrisVector(Guest& _guest);

/// UpdateWarnings (CS:36B6): re-posts the current warning, or tries the four warning checks.
void UpdateWarnings(Guest& _guest);

/// CheckMissileWarning (CS:36FD), CheckAltitudeWarning (CS:370E), CheckTemperatureWarning (CS:371A) and
/// CheckEnergyWarning (CS:3726): that warning check, then the next ones round while CX lasts, until one
/// posts its warning.
void CheckMissileWarning(Guest& _guest);
void CheckAltitudeWarning(Guest& _guest);
void CheckTemperatureWarning(Guest& _guest);
void CheckEnergyWarning(Guest& _guest);

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

/// EraseCompassAndBlips (CS:4594): erases the compass dot and every scanner blip.
void EraseCompassAndBlips(Guest& _guest);

/// UpdateFuelLeak (CS:499F): runs a fuel leak, and sets the border colour.
void UpdateFuelLeak(Guest& _guest);

/// RunFlight (CS:7E9B): the station tunnel, then a frame at a time until the player docks or is 40
/// frames dead; when the escape pod arrives TickEscapePod returns past it. Waits.
void RunFlight(Guest& _guest);

/// TickEscapePod (CS:7F69): counts the escape pod down; when it arrives, pops the return address
/// into AX so that the caller's caller is returned to.
void TickEscapePod(Guest& _guest);

/// ProcessFlightKeys (CS:7FA8): the flight controls other than steering.
void ProcessFlightKeys(Guest& _guest);

/// EngageJumpDrive (CS:8430): J: the jump drive engaged at full speed unless mass-locked.
void EngageJumpDrive(Guest& _guest);

/// UpdatePlayerMotion (CS:8472): speed, roll and pitch for this frame, then the world moves.
void UpdatePlayerMotion(Guest& _guest);

/// UpdatePlayerVelocity (CS:8599): playerVelocity from playerSpeed and the angles, when velocityDirty.
void UpdatePlayerVelocity(Guest& _guest);

/// MoveObjectsByVelocity (CS:85EC): moves every slot by the player's velocity.
void MoveObjectsByVelocity(Guest& _guest);

/// RunPauseScreen (CS:8D6A): the pause menu, until space resumes; its keys toggle the options and set
/// the frame time, and A drops two return addresses to leave RunFlight for the title. Waits.
void RunPauseScreen(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight, and every byte it writes is
// written as the original writes it, in the same order and at the same width. The dashboard's routines draw into
// the CGA's video memory, where their contracts have ES.

/// A stardust particle's position: signed 8.8 words, whose high bytes are its offsets from the centre of the
/// view, x -20h to 1Fh and y -10h to 0Fh while it is on screen.
struct DustPosition
{
  std::int16_t x;
  std::int16_t y;
};

/// ScaleDustStep's result: a particle's position shifted arithmetically right by stardustShift, and the shift.
struct DustStep
{
  std::int16_t x;
  std::int16_t y;
  std::uint8_t shift; ///< stardustShift
};

/// Where DustToScreen puts a particle in the 256x128 drawing buffer, as words: bits 14-6 of x and of y, signed
/// from bit 14, plus 128 and 64. On screen, the low bytes are the column and the row.
struct DustScreenPosition
{
  std::int16_t x;
  std::int16_t row;
};

/// What InSafeZone reads of safeZoneFlags.
struct SafeZone
{
  bool inside;       ///< bit 0: inside the station's safe zone
  std::uint8_t rest; ///< the other bits, shifted down: what UpdateSafeZone's RCL kept of AL
};

/// ComputeDustStripMask (CS:08B9): 2^n - 1 just covering |_step| / 2, the strip a respawned particle lands in.
[[nodiscard]] std::uint16_t ComputeDustStripMask(std::int16_t _step);

/// IsDustOnScreen (CS:09D6): whether the high byte of x is in -20h..1Fh and that of y in -10h..0Fh.
[[nodiscard]] bool IsDustOnScreen(DustPosition _position);

/// IsDustNearCenter (CS:09EE): whether the high byte of x is in -6..6 and that of y in -3..3, where the rear
/// view respawns a particle.
[[nodiscard]] bool IsDustNearCenter(DustPosition _position);

/// ScaleDustStep (CS:0A06): _position shifted right by stardustShift, the step a particle moves this frame.
[[nodiscard]] DustStep ScaleDustStep(const GameState& _state, DustPosition _position);

/// LoadDustPosition (CS:0A13): the position of the particle at DS:_particle, an entry of stardust.
[[nodiscard]] DustPosition LoadDustPosition(const GameState& _state, std::uint16_t _particle);

/// StoreDustPosition (CS:0A19): the particle at DS:_particle moved to _position.
void StoreDustPosition(GameState& _state, std::uint16_t _particle, DustPosition _position);

/// StorePreviousDustPosition (CS:0A1F): the particle's entry in stardustPrevious, 0xB4 bytes on, set to
/// _position.
void StorePreviousDustPosition(GameState& _state, std::uint16_t _particle, DustPosition _position);

/// DustToScreen (CS:0A28): where _position is in the drawing buffer.
[[nodiscard]] DustScreenPosition DustToScreen(DustPosition _position);

/// SaveStardustPositions (CS:0A88): stardust copied over stardustPrevious a byte at a time, for the jump drive's
/// streaks.
void SaveStardustPositions(GameState& _state);

/// InvalidateDashboard (CS:2540): the 22 dashboard cache bytes from missileCountShown filled with 0x80, so that
/// UpdateDashboard redraws every gauge.
void InvalidateDashboard(GameState& _state);

/// DrawMissileLockIndicator (CS:2804): when missileState changed, the 12x6-pixel block at B800:1E15 in
/// colorFillBytes[missileState]. Returns whether it drew.
bool DrawMissileLockIndicator(GameState& _state);

/// DrawConditionLight (CS:290C): when it changed, conditionLightBitmap at B800:176C in conditionColor, which
/// flashes black and red on bit 9 of millisecondCounter while it is 0.
void DrawConditionLight(GameState& _state);

/// UpdateConditionColor (CS:2959): conditionColor from the ship's state, and the colour: 0 flashing, 2 red, 3
/// yellow or 1 green.
std::uint8_t UpdateConditionColor(GameState& _state);

/// InSafeZone (CS:2E63): safeZoneFlags, read.
[[nodiscard]] SafeZone InSafeZone(const GameState& _state);

/// XorDashboardPixel (CS:43C4): the colour-2 pixel at _x, _y from the dashboard's origin (96, 144) XORed into
/// video memory. Returns the mask, from dashboardPixelMasks.
std::uint8_t XorDashboardPixel(GameState& _state, std::uint8_t _x, std::uint8_t _y);

/// DrainEnergy (CS:839F): playerEnergy less _amount, sign-extended; on a borrow it is 0 and the player dead.
void DrainEnergy(GameState& _state, std::int8_t _amount);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes its results
// back there. The registers the contract leaves to the routine it hands to Guest::Clobber, unless a caller reads what
// the original leaves in one: then the entry leaves that, and the contract compares it (InvalidateDashboardEntry,
// DrawMissileLockIndicatorEntry's CX, and XorDashboardPixelEntry's AX, BX and ES).

void ComputeDustStripMaskEntry(Guest& _guest);
void IsDustOnScreenEntry(Guest& _guest);
void IsDustNearCenterEntry(Guest& _guest);
void ScaleDustStepEntry(Guest& _guest);
void LoadDustPositionEntry(Guest& _guest);
void StoreDustPositionEntry(Guest& _guest);
void StorePreviousDustPositionEntry(Guest& _guest);
void DustToScreenEntry(Guest& _guest);
void SaveStardustPositionsEntry(Guest& _guest);
void InvalidateDashboardEntry(Guest& _guest);
void DrawMissileLockIndicatorEntry(Guest& _guest);
void DrawConditionLightEntry(Guest& _guest);
void UpdateConditionColorEntry(Guest& _guest);
void InSafeZoneEntry(Guest& _guest);
void XorDashboardPixelEntry(Guest& _guest);
void DrainEnergyEntry(Guest& _guest);

} // namespace Elite
