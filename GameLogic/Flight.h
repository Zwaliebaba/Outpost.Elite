// GameLogic/Flight.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "Maths.h"
#include "NativeEntry.h"
#include "ObjectSlot.h"
#include "Video.h"

#include <cstdint>
#include <optional>
#include <span>

namespace Elite
{

// The reference's flight routines, ported (plan §5 Phase 3, ADR-010): flying: the player's ship, the local bubble, the dashboard's logic. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv. Those de-assembled so far (ADR-012) follow
// the bodies: they take values and give values back, and their entries keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> FlightEntries() noexcept;

/// HandleFlightFunctionKeys (CS:0BB3): F1-F10 in flight. Out: AH = the F1-F4 scan code or 0; BX and CX
/// come back as the scan leaves them.
void HandleFlightFunctionKeys(Guest& _guest);

/// RunFlight (CS:7E9B): the station tunnel, then a frame at a time until the player docks or is 40
/// frames dead; when the escape pod arrives TickEscapePod returns past it. Waits.
void RunFlight(Guest& _guest);

/// ProcessFlightKeys (CS:7FA8): the flight controls other than steering.
void ProcessFlightKeys(Guest& _guest);

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

/// The last pixel a dashboard routine XORed through XorDashboardPixel: where it is from the dashboard's origin, and
/// the mask it XORed.
struct DashboardPixel
{
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t mask;
};

/// What the warning checks find: the check they stopped at, the count LOOP leaves, and the text of the warning that check
/// posted, if it posted one.
struct WarningChecks
{
  std::uint8_t check;
  std::uint16_t checksLeft;
  std::optional<std::uint16_t> text;
};

/// GetPreviousDustScreenPosition (CS:08A1): where the particle at DS:_particle was in the drawing buffer last frame,
/// from its entry in stardustPrevious, when it was on screen then.
[[nodiscard]] std::optional<DustScreenPosition> GetPreviousDustScreenPosition(const GameState& _state, std::uint16_t _particle);

/// ComputeDustStripMask (CS:08B9): 2^n - 1 just covering |_step| / 2, the strip a respawned particle lands in.
[[nodiscard]] std::uint16_t ComputeDustStripMask(std::int16_t _step);

/// ShiftStardustSideways (CS:08CB): _step added to x of every particle on screen, a particle that leaves it born again on
/// the edge it moves away from, in the strip ComputeDustStripMask gives for _step.
void ShiftStardustSideways(GameState& _state, std::int16_t _step);

/// RespawnDustAtSideEdge (CS:08F2): the particle at DS:_particle born again, its lifetime and its copy's at random
/// and the copy marked new, at a random y on the edge a sideways _step moves away from, its x in the strip
/// _stripMask covers. Returns where.
[[nodiscard]] DustPosition RespawnDustAtSideEdge(GameState& _state, std::uint16_t _particle, std::int16_t _step, std::uint16_t _stripMask);

/// RollStardust (CS:0927): every particle turned by rotationSinCos[7].
void RollStardust(GameState& _state);

/// ShiftStardustVertically (CS:0940): ShiftStardustSideways for y.
void ShiftStardustVertically(GameState& _state, std::int16_t _step);

/// RespawnDustAtVerticalEdge (CS:0969): RespawnDustAtSideEdge for a vertical _step: a random x, and y in the strip on
/// the edge the step moves away from.
[[nodiscard]] DustPosition RespawnDustAtVerticalEdge(GameState& _state, std::uint16_t _particle, std::int16_t _step,
                                                     std::uint16_t _stripMask);

/// RespawnDustAnywhere (CS:09A3): the particle at DS:_particle born again anywhere on screen.
[[nodiscard]] DustPosition RespawnDustAnywhere(GameState& _state, std::uint16_t _particle);

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

/// ResetStardust (CS:0A43): all 30 particles scattered at random, within 23h of the centre, with random lifetimes;
/// messageFrames cleared first when its low byte is 0, so that the new view's title prints. Returns the last random number
/// it drew, whose low byte gave the last particle's lifetime.
std::uint16_t ResetStardust(GameState& _state);

/// SaveStardustPositions (CS:0A88): stardust copied over stardustPrevious a byte at a time, for the jump drive's
/// streaks.
void SaveStardustPositions(GameState& _state);

/// UpdateStardust (CS:068F): the 30 stardust particles moved and drawn for viewAngle, once a frame, in stardustColor; streaks
/// from where they were while the jump drive is engaged (SaveStardustPositions first). The front and rear views shift them by
/// the pitch, roll them by the roll and move them by the speed (ComputeStardustShift), the rear inwards with lifetimes; the side
/// views shift them sideways by the speed (UpdateSideStardust). _bx is the BX it is called with, which reaches
/// ComputeStardustShift's divide when neither the pitch nor the roll has turned the dust. Returns whether a DrawLine filled bytes
/// with REP STOSB.
bool UpdateStardust(GameState& _state, std::uint16_t _bx);

/// ComputeStardustShift (CS:0887): stardustShift = (34h - playerSpeed) / 12 + 4, one less while jumpDriveEngaged, and returned.
/// A speed above 34h, which the game never sets, divides into the game's trap, which saves BX: _bh over 12.
std::uint8_t ComputeStardustShift(GameState& _state, std::uint8_t _bh);

/// RestoreFlightScreen (CS:15CF): the cockpit back after a function key's screen (ShowCockpitScreen, its copies backwards when
/// _backward), every gauge marked stale (InvalidateDashboard), a beep, and messageShown cleared. Returns what ShowCockpitScreen
/// did.
ScreenChange RestoreFlightScreen(GameState& _state, Hardware& _hardware, bool _backward);

/// UpdateDashboard (CS:254F): once a frame, in the CGA's video memory: the condition light, the energy and the laser's heat
/// (UpdateEnergyAndLaserHeat), the safe zone's S and the ECM's E, the energy banks, the missiles, the pitch and roll
/// indicators, then the bars for the laser's temperature, the altitude, the cabin's temperature, the fuel, the shields and the
/// speed, each redrawn only when its value changed, the shields every frame.
void UpdateDashboard(GameState& _state);

/// DrawFiveLineBar (CS:2645): _value * 12 / 63, 0-48 pixels, as a bar 5 scanlines high on the dashboard line at B800:_line:
/// colour 1, a partial byte from barPartialBytes, then colour 2 to the 48th pixel. For a value of fewer than 4 pixels the second
/// run counts _countHigh, CH as the caller leaves it, in its high byte.
void DrawFiveLineBar(GameState& _state, std::uint16_t _line, std::uint8_t _value, std::uint8_t _countHigh);

/// DrawThreeLineBar (CS:273E): DrawFiveLineBar's bar 3 scanlines high.
void DrawThreeLineBar(GameState& _state, std::uint16_t _line, std::uint8_t _value, std::uint8_t _countHigh);

/// DrawEnergyBanks (CS:283B): playerEnergy split into the four bank bytes of energyBankFill, FFh full, the rest, then 0, and
/// each bank that changed redrawn (DrawFiveLineBar) from B800:3E38 upwards.
void DrawEnergyBanks(GameState& _state);

/// UpdateEnergyAndLaserHeat (CS:2882): unless the game is over or the escape pod flies, the laser cooled by 2 to 0, and the
/// shields up by 1 to FFh at full energy, or the energy up by 1, or 3 with the energy unit, to 3FFh, each written and then
/// floored or capped over it; then below one bank, at odds of 50 in 65536, one of the 13 equipment bytes from missileCount lost
/// and its message posted.
void UpdateEnergyAndLaserHeat(GameState& _state);

/// SetUpLocalSpace (CS:29D0): the current system's seeds (LoadSystemSeeds), every gauge stale, the slot counts, the slots cleared
/// (ClearAllObjects, backwards when _backward), the cockpit shown (ShowCockpitScreen), the stardust scattered, the flight's
/// variables reset; then out of witch space the sun, the planet and the station, and spawnGovernment, else spawnGovernment 0.
/// Returns what ShowCockpitScreen did.
ScreenChange SetUpLocalSpace(GameState& _state, Hardware& _hardware, bool _backward);

/// UpdatePlayerMotion (CS:8472): skipped during GAME OVER after its first frame; with the escape pod flying, only the world moves
/// (MoveObjectsByVelocity). Otherwise the steering for the frame, the docking computer's (RunDockingComputer) or the selected
/// device's (ReadSteering, then ApplyReverseControls) after . and , change the speed; the roll, -23 to 23, turns the roll angle
/// by twice as much, and the pitch the camera's frame (ApplyPitch); then, unless the docking computer flies, the velocity
/// (UpdatePlayerVelocity) and the world moved (MoveObjectsByVelocity). Waits sometimes, in the stick's read.
void UpdatePlayerMotion(GameState& _state, Hardware& _hardware);

/// InvalidateDashboard (CS:2540): the 22 dashboard cache bytes from missileCountShown filled with 0x80, so that
/// UpdateDashboard redraws every gauge.
void InvalidateDashboard(GameState& _state);

/// DrawSignedIndicator (CS:26BE): the roll or pitch indicator for _value, held to -23..23, on the five scanlines of the
/// dashboard line at B800:_line: the strip in colour 2, then the marker from indicatorMarkers.
void DrawSignedIndicator(GameState& _state, std::uint16_t _line, std::int8_t _value);

/// DrawMissileIcons (CS:2799): when missileCount changed, that many missileIcon cells at B800:3E0C and the background
/// up to four. Returns whether it drew.
bool DrawMissileIcons(GameState& _state);

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

/// UpdateSafeZone (CS:2E69): safeZoneFlags from the station in stationSlot: bit 0 set while it is an active station, IsObjectNear
/// and nearer than 32C8h, over what the original's AL held: IsObjectNear's last high byte, or the low byte of the distance.
void UpdateSafeZone(GameState& _state);

/// ComputeDeathDebrisVector (CS:2F8B): (0, 40, 0) turned by the view and, off the front view, by the roll, then by the yaw
/// and the pitch, through rotationSinCos[8], [7] and [6], which it sets.
[[nodiscard]] Vector ComputeDeathDebrisVector(GameState& _state);

/// CheckCollisions (CS:2BC5): the player against each active one of objectSlotCount slots from shipSlots: inside its type's
/// collisionRanges box, a ship's damage, or the station's docking, scrape or crash, the impact sound's STI on each.
void CheckCollisions(GameState& _state, Hardware& _hardware);

/// UpdateWarnings (CS:36B6): unless the game is over, the current warning posted again while warningFrames lasts, else the
/// four warning checks from the one after the last warning.
void UpdateWarnings(GameState& _state);

/// CheckMissileWarning (CS:36FD), CheckAltitudeWarning (CS:370E), CheckTemperatureWarning (CS:371A) and CheckEnergyWarning
/// (CS:3726): that warning check, then the next ones round while _checks lasts as LOOP counts it, until one posts its
/// warning. The missile check clears its alert.
WarningChecks CheckMissileWarning(GameState& _state, std::uint16_t _checks);
WarningChecks CheckAltitudeWarning(GameState& _state, std::uint16_t _checks);
WarningChecks CheckTemperatureWarning(GameState& _state, std::uint16_t _checks);
WarningChecks CheckEnergyWarning(GameState& _state, std::uint16_t _checks);

/// UpdateScannerBlip (CS:40EC): unless _slot is debris, a station, the sun or the planet, its scanner blip moved to the
/// camera-frame position _camera, y and z scaled by 1.25: the slot marked scanned, the old blip XORed out if one is drawn
/// and the new one in. Returns the last pixel of the new blip, when it drew one.
std::optional<DashboardPixel> UpdateScannerBlip(GameState& _state, ObjectSlot _slot, Vector _camera);

/// What UpdateCompass leaves for its entry.
struct CompassUpdate
{
  DashboardPixel last;  ///< the last pixel of the dot it drew, one right of and one above its centre
  std::uint16_t range;  ///< |z| + 1000 of the target in the camera's frame, the dot's divisor, which CX keeps
  std::uint8_t inFront; ///< the station slot's BlipZ: 20h while the target is in front, else 0, which BP keeps sign-extended
};

/// UpdateCompass (CS:418F): while titleShown, the compass dot moved to the station when the planet is near
/// (IsObjectNearKeepBlip), else to the planet (compassTargetIsStation 1 or 0). The target's and the station's scale shifts go to
/// their depth bytes (+3Dh); the target's scaled position turned to the view goes to the station's compass words and, turned to
/// the camera's frame, gives the dot: x and y as 8 * |c| / (|z| + 1000), at most 7, normalised by sqrtTable when their squares
/// reach 41h, at (CFh + x, 27h + y). The old dot, kept in the station's blip bytes, is XORed out if drawn and the new one in.
std::optional<CompassUpdate> UpdateCompass(GameState& _state);

/// XorCompassDot (CS:42A4): the compass dot at _x, _y from the dashboard's origin XORed into video memory: its eight
/// neighbours, a ring, and the centre too while _inFront, solid. Returns the last pixel, one right of and one above
/// the centre.
DashboardPixel XorCompassDot(GameState& _state, std::uint8_t _x, std::uint8_t _y, bool _inFront);

/// A dashboard pixel's place as the original holds it in DX: x in DL, y in DH. What a routine that ends by XORing a pixel
/// leaves there.
[[nodiscard]] std::uint16_t PixelPlace(DashboardPixel _pixel) noexcept;

/// EraseScannerBlip (CS:42D6): unless _slot is a station's, its scanner blip XORed out when one is drawn, and the flag that
/// says so cleared. Returns the blip's last pixel, when it erased one.
std::optional<DashboardPixel> EraseScannerBlip(GameState& _state, ObjectSlot _slot);

/// EraseCompassAndBlips (CS:4594): the compass dot XORed out of video memory when stationSlot's blip flag says it is drawn, the
/// flag cleared first, and then each ship slot's scanner blip, EraseScannerBlip. Returns whether it erased anything.
bool EraseCompassAndBlips(GameState& _state);

/// XorScannerBlip (CS:42F6): the scanner blip for the scanner bytes _x, _y and _z XORed into video memory: a stick from
/// (3Dh + _x, 1Fh - _z / 4) of |_y / 4| pixels, up or down, and a pixel right of its end. Returns that last pixel.
DashboardPixel XorScannerBlip(GameState& _state, std::uint8_t _x, std::uint8_t _y, std::uint8_t _z);

/// XorDashboardPixel (CS:43C4): the colour-2 pixel at _x, _y from the dashboard's origin (96, 144) XORed into
/// video memory. Returns the mask, from dashboardPixelMasks.
std::uint8_t XorDashboardPixel(GameState& _state, std::uint8_t _x, std::uint8_t _y);

/// UpdateFuelLeak (CS:499F): a fuel leak's delay counted down, then its frames, each taking 5 fuel and posting fuelLeakText;
/// the border red while it leaks and maskingBackgroundColor otherwise, with the bright palette, unless the delay has just run out.
void UpdateFuelLeak(GameState& _state, Hardware& _hardware);

/// TickEscapePod (CS:7F69): escapePodFrames counted down while it runs. Returns whether the pod has arrived, when the original
/// drops its own return address so that its RET leaves RunFlight.
bool TickEscapePod(GameState& _state);

/// DrainEnergy (CS:839F): playerEnergy less _amount, sign-extended; on a borrow it is 0 and the player dead.
void DrainEnergy(GameState& _state, std::int8_t _amount);

/// What EngageJumpDrive did: in Hyperspace.h, beside the MassLock it holds, which this header cannot include (Hyperspace.h
/// includes Ships.h, which includes this).
struct JumpDriveRequest;

/// EngageJumpDrive (CS:8430): J: the jump drive engaged (jumpDriveEngaged, velocityDirty) at full speed, 48, unless the docking
/// computer is on or IsMassLocked says it is mass-locked; otherwise disengaged. Its message is posted for 5 frames.
JumpDriveRequest EngageJumpDrive(GameState& _state);

/// UpdatePlayerVelocity (CS:8599): when velocityDirty, the velocity from playerSpeed along the pitch and the yaw, through
/// rotationSinCos[6] and [7], which it sets. While jumpDriveEngaged it is 32 times the speed, for one frame.
void UpdatePlayerVelocity(GameState& _state);

/// MoveObjectsByVelocity (CS:85EC): the player's velocity, sign-extended, taken from the 24-bit position of each of
/// shipSlotCount slots from shipSlots, and from its compass words: the player stays at the origin.
void MoveObjectsByVelocity(GameState& _state);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes its results
// back there. The registers the contract leaves to the routine it hands to Guest::Clobber, unless a caller reads what
// the original leaves in one: then the entry leaves that, and the contract compares it (InvalidateDashboardEntry,
// DrawMissileLockIndicatorEntry's, DrawSignedIndicatorEntry's and DrawMissileIconsEntry's CX, XorDashboardPixelEntry's
// AX, BX and ES, ResetStardustEntry's AX and DI, XorCompassDotEntry's AX and BX, XorScannerBlipEntry's BX, CX, DX and
// ES, EraseScannerBlipEntry's AX, BX, CX, DX and ES, UpdateScannerBlipEntry's DX and ES, and MoveObjectsByVelocityEntry's
// AX and SI).

void GetPreviousDustScreenPositionEntry(Guest& _guest); ///< SI = the particle. Out: CF clear and CL, CH, or CF set.
void ComputeDustStripMaskEntry(Guest& _guest);
void ShiftStardustSidewaysEntry(Guest& _guest); ///< DX = the step.
void RespawnDustAtSideEdgeEntry(Guest& _guest); ///< SI = the particle, DX = the step, BP = the strip. Out: AX, BX.
void RollStardustEntry(Guest& _guest);
void ShiftStardustVerticallyEntry(Guest& _guest);   ///< DX = the step.
void RespawnDustAtVerticalEdgeEntry(Guest& _guest); ///< SI = the particle, DX = the step, BP = the strip. Out: AX, BX.
void RespawnDustAnywhereEntry(Guest& _guest);       ///< SI = the particle. Out: AX, BX.
void IsDustOnScreenEntry(Guest& _guest);
void IsDustNearCenterEntry(Guest& _guest);
void ScaleDustStepEntry(Guest& _guest);
void LoadDustPositionEntry(Guest& _guest);
void StoreDustPositionEntry(Guest& _guest);
void StorePreviousDustPositionEntry(Guest& _guest);
void DustToScreenEntry(Guest& _guest);
void ResetStardustEntry(Guest& _guest); ///< Out: AX = the last random, its low byte the last lifetime; DI past the particles.
void SaveStardustPositionsEntry(Guest& _guest);
void InvalidateDashboardEntry(Guest& _guest);
void UpdateStardustEntry(Guest& _guest);       ///< Out: ES = DS and DF clear once a streak was horizontal.
void ComputeStardustShiftEntry(Guest& _guest); ///< Out: BL = 12; AX clobbered.
/// Out: every register as the original leaves it: InvalidateDashboard's AX, CX, DI and ES, and ShowCockpitScreen's SI, and its BX
/// and DX once it set the mode.
void RestoreFlightScreenEntry(Guest& _guest);
void UpdateDashboardEntry(Guest& _guest);          ///< Out: ES = B800h. Every other register clobbered.
void DrawFiveLineBarEntry(Guest& _guest);          ///< AL = the value, DI = the line, CH = the count's high byte, ES = B800h.
void DrawThreeLineBarEntry(Guest& _guest);         ///< As DrawFiveLineBarEntry.
void DrawEnergyBanksEntry(Guest& _guest);          ///< ES = B800h. Clobbers all.
void UpdateEnergyAndLaserHeatEntry(Guest& _guest); ///< AX, BX clobbered.
void SetUpLocalSpaceEntry(Guest& _guest);          ///< Out: DF clear once the cockpit was drawn. Clobbers all but DS.
void UpdatePlayerMotionEntry(Guest& _guest);       ///< Clobbers all.
void DrawSignedIndicatorEntry(Guest& _guest);      ///< AL = the value, DI = the line, ES = B800h. Out: CX = 0.
void DrawMissileIconsEntry(Guest& _guest);         ///< ES = B800h. Out: CX = 0 once it draws, else CL = missileCount.
void DrawMissileLockIndicatorEntry(Guest& _guest);
void DrawConditionLightEntry(Guest& _guest);
void UpdateConditionColorEntry(Guest& _guest);
void InSafeZoneEntry(Guest& _guest);
void ComputeDeathDebrisVectorEntry(Guest& _guest); ///< Out: AX, BX, CX.
void CheckCollisionsEntry(Guest& _guest);          ///< Out: DI past the slots. Clobbers all but DS.
void UpdateWarningsEntry(Guest& _guest);
void CheckMissileWarningEntry(Guest& _guest); ///< CX = the checks. Out: AL = the check, CX as LOOP leaves it; BX, AX once one posts.
void CheckAltitudeWarningEntry(Guest& _guest);
void CheckTemperatureWarningEntry(Guest& _guest);
void CheckEnergyWarningEntry(Guest& _guest);
void UpdateScannerBlipEntry(Guest& _guest); ///< DI = the slot, AX, BX, CX = the camera position. Out: DX, ES.
void XorCompassDotEntry(Guest& _guest);     ///< DL, DH = the dot, BP = 0 behind. Out: AX = DX one right and one up, BX the mask.
void EraseScannerBlipEntry(Guest& _guest);  ///< DI = the slot. Out, once it erases: AX = DX the last pixel, BX its mask, CX, ES.
void XorScannerBlipEntry(Guest& _guest);    ///< AH, BH, CH = the scanner bytes. Out: AX = DX the last pixel, BX its mask, CX.
void XorDashboardPixelEntry(Guest& _guest);
/// Out: every register as the original leaves it once titleShown: XorCompassDotEntry's AX, BX, DX and ES, CX the dot's divisor, BP
/// the in-front byte sign-extended, and DI = stationSlot, which TransformAndDrawObjects goes on with.
void UpdateCompassEntry(Guest& _guest);
void EraseCompassAndBlipsEntry(Guest& _guest); ///< Out: ES = B800h once it erases anything.
void DrainEnergyEntry(Guest& _guest);
void EngageJumpDriveEntry(Guest& _guest); ///< Out: AX the message, and every register IsMassLocked leaves once it is asked.
void UpdatePlayerVelocityEntry(Guest& _guest);
void MoveObjectsByVelocityEntry(Guest& _guest); ///< Out: AX = playerVelocityZ, SI past the last slot.
void UpdateSafeZoneEntry(Guest& _guest);        ///< Out: DI = stationSlot. AX, BX, CX, DX clobbered.
void UpdateFuelLeakEntry(Guest& _guest);        ///< AX, DX clobbered.
void TickEscapePodEntry(Guest& _guest);         ///< When the pod arrives, pops the return address into AX: the RET leaves RunFlight.

/// What EraseScannerBlip leaves in the registers for _slot once it has _erased its blip: AX = DX the last pixel, BX its mask, CX
/// the stick's step and 0, and ES the video segment. For the entries of the routines that end with it, whose callers go on
/// with them.
void EraseScannerBlipOut(Guest& _guest, const ObjectSlot& _slot, const std::optional<DashboardPixel>& _erased);

} // namespace Elite
