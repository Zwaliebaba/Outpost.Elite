// GameLogic/Flight.h
#pragma once

#include "GameState.h"
#include "Host.h"
#include "Maths.h"
#include "SpaceObject.h"
#include "Video.h"

#include <cstdint>
#include <optional>

namespace Elite
{

// The reference's flight routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): flying: the player's ship, the local
// bubble, the dashboard's logic. Each takes values and gives values back, on the GameState and, where it drives a device, the devices
// (ADR-014).

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the original's routine of its name computes, with no register in sight, and every byte it writes is
// written as the original writes it, in the same order and at the same width. The dashboard's keep its rules and the
// cache of what each gauge shows, which the snapshot reads; the shell draws it (ADR-069).

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
  bool inside; ///< bit 0: inside the station's safe zone
};

/// The last pixel of a scanner blip (ScannerBlipEnd): where it is from the dashboard's origin, and its mask.
struct DashboardPixel
{
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t mask;
};

/// GetPreviousDustScreenPosition: where particle _particle, an index of stardust, was in the drawing buffer last
/// frame, from its entry in stardustPrevious, when it was on screen then.
[[nodiscard]] std::optional<DustScreenPosition> GetPreviousDustScreenPosition(const GameState& _state, std::size_t _particle);

/// The stardust's particles, which UpdateStardust moves and draws.
inline constexpr std::size_t STARDUST_PARTICLES = 30;

/// A particle as UpdateStardust's DrawDust drew it: a point, or while the jump drive is engaged a streak from where it was.
struct DrawnDust
{
  DustScreenPosition at;
  std::optional<DustScreenPosition> from; ///< the streak's start, while the jump drive is engaged
};

/// Particle _particle as the frame's UpdateStardust drew it, read back once it has: none when it is off screen, which it does not
/// draw, or while the jump drive is engaged when it was born again this frame or was off screen before, which DrawDust skips.
[[nodiscard]] std::optional<DrawnDust> DrawnStardust(const GameState& _state, std::size_t _particle);

/// The pixel value UpdateStardust draws the stardust in (stardustColor).
[[nodiscard]] std::uint8_t StardustColor() noexcept;

/// ComputeDustStripMask: 2^n - 1 just covering |_step| / 2, the strip a respawned particle lands in.
[[nodiscard]] std::uint16_t ComputeDustStripMask(std::int16_t _step);

/// ShiftStardustSideways: _step added to x of every particle on screen, a particle that leaves it born again on
/// the edge it moves away from, in the strip ComputeDustStripMask gives for _step.
void ShiftStardustSideways(GameState& _state, std::int16_t _step);

/// RespawnDustAtSideEdge: particle _particle born again, its lifetime and its copy's at random
/// and the copy marked new, at a random y on the edge a sideways _step moves away from, its x in the strip
/// _stripMask covers. Returns where.
[[nodiscard]] DustPosition RespawnDustAtSideEdge(GameState& _state, std::size_t _particle, std::int16_t _step, std::uint16_t _stripMask);

/// RollStardust: every particle turned by rotationSinCos[7].
void RollStardust(GameState& _state);

/// ShiftStardustVertically: ShiftStardustSideways for y.
void ShiftStardustVertically(GameState& _state, std::int16_t _step);

/// RespawnDustAtVerticalEdge: RespawnDustAtSideEdge for a vertical _step: a random x, and y in the strip on
/// the edge the step moves away from.
[[nodiscard]] DustPosition RespawnDustAtVerticalEdge(GameState& _state, std::size_t _particle, std::int16_t _step,
                                                     std::uint16_t _stripMask);

/// RespawnDustAnywhere: particle _particle born again anywhere on screen.
[[nodiscard]] DustPosition RespawnDustAnywhere(GameState& _state, std::size_t _particle);

/// IsDustOnScreen: whether the high byte of x is in -20h..1Fh and that of y in -10h..0Fh.
[[nodiscard]] bool IsDustOnScreen(DustPosition _position);

/// PressHyperspace's rule: how far _fuel reaches, in tenths of a light year: ten times it over 36, a byte divide.
[[nodiscard]] std::uint8_t FuelReachTenths(std::uint8_t _fuel);

/// PressHyperspace's rule: whether a jump of _distanceTenths tenths of a light year goes on _fuel: under 7.1 light years, and
/// the distance's low byte within the fuel's reach. A distance of 0, the current system, is none selected.
[[nodiscard]] bool WithinJumpRange(std::uint16_t _distanceTenths, std::uint8_t _fuel);

/// IsDustNearCenter: whether the high byte of x is in -6..6 and that of y in -3..3, where the rear
/// view respawns a particle.
[[nodiscard]] bool IsDustNearCenter(DustPosition _position);

/// ScaleDustStep: _position shifted right by stardustShift, the step a particle moves this frame.
[[nodiscard]] DustStep ScaleDustStep(const GameState& _state, DustPosition _position);

/// LoadDustPosition: the position of particle _particle, an index of stardust.
[[nodiscard]] DustPosition LoadDustPosition(const GameState& _state, std::size_t _particle);

/// StoreDustPosition: particle _particle moved to _position.
void StoreDustPosition(GameState& _state, std::size_t _particle, DustPosition _position);

/// StorePreviousDustPosition: the particle's entry in stardustPrevious, which follows stardust, set to _position.
void StorePreviousDustPosition(GameState& _state, std::size_t _particle, DustPosition _position);

/// DustToScreen: where _position is in the drawing buffer.
[[nodiscard]] DustScreenPosition DustToScreen(DustPosition _position);

/// ResetStardust: all 30 particles scattered at random, within 23h of the centre, with random lifetimes;
/// messageFrames cleared first when its low byte is 0, so that the new view's title prints. Returns the last random number
/// it drew, whose low byte gave the last particle's lifetime.
std::uint16_t ResetStardust(GameState& _state);

/// SaveStardustPositions: stardust copied over stardustPrevious a byte at a time, for the jump drive's
/// streaks.
void SaveStardustPositions(GameState& _state);

/// UpdateStardust: the 30 stardust particles moved and drawn for viewAngle, once a frame, in stardustColor; streaks
/// from where they were while the jump drive is engaged (SaveStardustPositions first). The front and rear views shift them by
/// the pitch, roll them by the roll and move them by the speed (ComputeStardustShift), the rear inwards with lifetimes; the side
/// views shift them sideways by the speed (UpdateSideStardust).
void UpdateStardust(GameState& _state);

/// ComputeStardustShift: stardustShift = (34h - playerSpeed) / 12 + 4, one less while jumpDriveEngaged. A speed
/// above 34h, which the game never sets, divides into the game's trap.
void ComputeStardustShift(GameState& _state);

/// RestoreFlightScreen: the cockpit back after a function key's screen (ShowCockpitScreen), every gauge marked stale
/// (InvalidateDashboard), a beep, and messageShown cleared. Returns what ShowCockpitScreen did.
ScreenChange RestoreFlightScreen(GameState& _state, Host& _host);

/// UpdateDashboard: once a frame, the dashboard's rules: the condition colour, the energy and the laser's heat
/// (UpdateEnergyAndLaserHeat), the safe zone, the ECM's E cleared after its frame and the energy banks, with the cache of what
/// each gauge shows, which the snapshot reads for the S and the E.
void UpdateDashboard(GameState& _state);

/// SplitEnergyBanks: playerEnergy split into the four bank bytes of energyBankFill, FFh full, the rest, then 0, and
/// copied to energyBanksShown.
void SplitEnergyBanks(GameState& _state);

/// UpdateEnergyAndLaserHeat: unless the game is over or the escape pod flies, the laser cooled by 2 to 0, and the
/// shields up by 1 to FFh at full energy, or the energy up by 1, or 3 with the energy unit, to 3FFh, each written and then
/// floored or capped over it; then below one bank, at odds of 50 in 65536, one of the 13 equipment bytes from missileCount lost
/// and its message posted.
void UpdateEnergyAndLaserHeat(GameState& _state);

/// SetUpLocalSpace: the current system's seeds (LoadSystemSeeds), every gauge stale, the slot counts, the slots cleared
/// (ClearAllObjects), the cockpit shown (ShowCockpitScreen), the stardust scattered, the flight's
/// variables reset; then out of witch space the sun, the planet and the station, and spawnGovernment, else spawnGovernment 0.
/// Returns what ShowCockpitScreen did.
ScreenChange SetUpLocalSpace(GameState& _state, Host& _host);

/// UpdatePlayerMotion: skipped during GAME OVER after its first frame; with the escape pod flying, only the world moves
/// (MoveObjectsByVelocity). Otherwise the steering for the frame, the docking computer's (RunDockingComputer) or the selected
/// device's (ReadSteering, then ApplyReverseControls) after . and , change the speed; the roll, -23 to 23, turns the roll angle
/// by twice as much, and the pitch the camera's frame (ApplyPitch); then, unless the docking computer flies, the velocity
/// (UpdatePlayerVelocity) and the world moved (MoveObjectsByVelocity). Waits sometimes, in the stick's read.
void UpdatePlayerMotion(GameState& _state, Host& _host);

/// InvalidateDashboard: the 22 dashboard cache bytes from missileCountShown filled with 0x80, so that
/// UpdateDashboard redraws every gauge.
void InvalidateDashboard(GameState& _state);

/// UpdateConditionColor: conditionColor from the ship's state: 0 flashing, 2 red, 3 yellow or 1 green.
void UpdateConditionColor(GameState& _state);

/// InSafeZone: safeZoneFlags, read.
[[nodiscard]] SafeZone InSafeZone(const GameState& _state);

/// UpdateSafeZone: safeZoneFlags from the station in stationSlot: bit 0 set while it is an active station, IsObjectNear
/// and nearer than 32C8h, over what the original's byte held: IsObjectNear's last high byte, or the low byte of the distance.
void UpdateSafeZone(GameState& _state);

/// ComputeDeathDebrisVector: (0, 40, 0) turned by the view and, off the front view, by the roll, then by the yaw
/// and the pitch, through rotationSinCos[8], [7] and [6], which it sets.
[[nodiscard]] Vector ComputeDeathDebrisVector(GameState& _state);

/// CheckCollisions: the player against each active one of objectSlotCount slots from shipSlots: inside its type's collisionRanges
/// box, a ship's damage, or the station's docking, scrape or crash, the impact sound on each.
void CheckCollisions(GameState& _state);

/// UpdateWarnings: unless the game is over, the current warning posted again while warningFrames lasts, else the
/// four warning checks from the one after the last warning.
void UpdateWarnings(GameState& _state);

/// UpdateScannerBlip: unless _slot is debris, a station, the sun or the planet, its scanner blip moved to the
/// camera-frame position _camera, y and z scaled by 1.25: the slot marked scanned and its blip drawn.
void UpdateScannerBlip(GameState& _state, SpaceObject& _slot, Vector _camera);

/// UpdateCompass: while titleShown, the compass dot moved to the station when the planet is near
/// (IsObjectNearKeepBlip), else to the planet (compassTargetIsStation 1 or 0). The target's and the station's scale shifts go to
/// their depth bytes (+3Dh); the target's scaled position turned to the view goes to the station's compass words and, turned to
/// the camera's frame, gives the dot: x and y as 8 * |c| / (|z| + 1000), at most 7, normalised by sqrtTable when their squares
/// reach 41h, at (CFh + x, 27h + y). The dot goes to the station's blip bytes, marked drawn.
/// Returns whether it ran: false, doing nothing, unless titleShown.
bool UpdateCompass(GameState& _state);

/// EraseScannerBlip: unless _slot is a station's, its scanner blip taken off when one is drawn, and the flag that
/// says so cleared. Returns the blip's last pixel, when it erased one.
std::optional<DashboardPixel> EraseScannerBlip(SpaceObject& _slot);

/// EraseCompassAndBlips: the compass dot taken off when stationSlot's blip flag says it is drawn, the flag cleared, and then each ship slot's scanner blip, EraseScannerBlip. Returns whether it erased anything.
bool EraseCompassAndBlips(GameState& _state);

/// ScannerBlipEnd: the last pixel of the scanner blip for the scanner bytes _x, _y and _z: a stick from
/// (3Dh + _x, 1Fh - _z / 4) of |_y / 4| pixels, up or down, and a pixel right of its end, with its mask.
[[nodiscard]] DashboardPixel ScannerBlipEnd(std::uint8_t _x, std::uint8_t _y, std::uint8_t _z) noexcept;

/// DashboardPixelMask: the mask of the colour-2 pixel at column _x in a byte of four, from dashboardPixelMasks.
[[nodiscard]] std::uint8_t DashboardPixelMask(std::uint8_t _x) noexcept;

/// UpdateFuelLeak: a fuel leak's delay counted down, then its frames, each taking 5 fuel and posting fuelLeakText;
/// the border red while it leaks and maskingBackgroundColor otherwise, with the bright palette, unless the delay has just run out.
void UpdateFuelLeak(GameState& _state, Host& _host);

/// TickEscapePod: escapePodFrames counted down while it runs. Returns whether the pod has arrived, when the original
/// drops its own return address so that its return leaves RunFlight.
bool TickEscapePod(GameState& _state);

/// DrainEnergy: playerEnergy less _amount, sign-extended; on a borrow it is 0 and the player dead.
void DrainEnergy(GameState& _state, std::int8_t _amount);

/// What EngageJumpDrive did: in Hyperspace.h, beside the MassLock it holds, which this header cannot include (Hyperspace.h
/// includes Ships.h, which includes this).
struct JumpDriveRequest;

/// EngageJumpDrive: J: the jump drive engaged (jumpDriveEngaged, velocityDirty) at full speed, 48, unless the docking
/// computer is on or IsMassLocked says it is mass-locked; otherwise disengaged. Its message is posted for 5 frames.
JumpDriveRequest EngageJumpDrive(GameState& _state);

/// UpdatePlayerVelocity: when velocityDirty, the velocity from playerSpeed along the pitch and the yaw, through
/// rotationSinCos[6] and [7], which it sets. While jumpDriveEngaged it is 32 times the speed, for one frame.
void UpdatePlayerVelocity(GameState& _state);

/// MoveObjectsByVelocity: the player's velocity, sign-extended, taken from the 24-bit position of each of
/// shipSlotCount slots from shipSlots, and from its compass words: the player stays at the origin.
void MoveObjectsByVelocity(GameState& _state);

/// What HandleFlightFunctionKeys leaves that ProcessFlightKeys reads.
struct FlightScreens
{
  std::uint8_t view;                    ///< the F1-F4 that ended it, or 0: no key, or the navigation computer's error
  std::optional<ScreenChange> restored; ///< what RestoreFlightScreen did, once a screen was shown
};

/// HandleFlightFunctionKeys: F1-F10 in flight, scanned in keyDown. A key held is flushed (ResetKeyboard), and the screens
/// shown from it (FlightScreenDispatch): F5 the galactic chart, F6 the short-range chart, F7 the system's data, F8 the market
/// prices, F9 the commander's status and F10 the inventory, each from the key the last returned, any other key waited past, until
/// F1-F4; in witch space F5, F7, F8, and F6 until its countdown is down to 1, post the navigation computer's error instead. A screen
/// shown first sets flightScreenShown and erases the compass and the blips, and the cockpit comes back as it leaves
/// (RestoreFlightScreen). inFlight is 0 throughout, but for the status screen. Waits for keys on the screens' paths (ADR-015).
[[nodiscard]] Task<FlightScreens> HandleFlightFunctionKeys(GameState& _state, Host& _host);

/// How ProcessFlightKeys ends.
struct FlightKeysExit
{
  /// The pause screen's A: the original drops its own return address and ProcessFlightKeys', so that the return leaves RunFlight for
  /// the title (RunPauseScreen).
  bool aborted;
};

/// ProcessFlightKeys: the flight controls other than steering, unless the game is over or the escape pod flies: the function
/// keys' screens and views (HandleFlightFunctionKeys, then ChangeView); G arms the galactic drive; H starts the hyperspace countdown, which
/// ends it; D toggles the docking computer; J the jump drive; Esc pauses (RunPauseScreen), or with Ctrl freezes until a key; T, U and M the
/// missile; fire; E the ECM; B the energy bomb; C the escape pod; I identifies; N the masking device; L the anti-ECM emulator.
/// Waits
/// for keys on the screens', the pause's and the freeze's paths (ADR-015).
[[nodiscard]] Task<FlightKeysExit> ProcessFlightKeys(GameState& _state, Host& _host);

/// RunPauseScreen: the speaker silenced (SilenceSpeakerTimer), then the pause menu drawn and its options presented until
/// a key ends it: space resumes; R, D, Y, B and S toggle the options; F1-F10 set the frame time and draw the menu again; A leaves
/// for the title, titleShown cleared. Returns whether A left, when the
/// original drops its own return address and its caller's. Waits for keys as a rule (ADR-015).
[[nodiscard]] Task<bool> RunPauseScreen(GameState& _state, Host& _host);

/// RunFlight: the mouse reset if it steers, local space set up (SetUpLocalSpace) and the
/// station's tunnel played; then a frame at a time: the dashboard, the objects drawn, the lasers, the stardust, the fuel leak, the
/// message line, the screen dump key, the frame presented, the objects moved and spawned, the player's motion and the collisions;
/// docked, the tunnel again and the keyboard and the sound reset; otherwise the flight keys, the hyperspace countdown and the escape
/// pod. While the player is dead, unless cheatEnabled, GAME OVER for 40 frames, then ELITE on the message line. Waits as a rule
/// (ADR-015).
[[nodiscard]] Task<void> RunFlight(GameState& _state, Host& _host);

} // namespace Elite
