// GameLogic/Hyperspace.h
#pragma once

#include "FlightSnapshot.h"
#include "GameState.h"
#include "Host.h"
#include "Ships.h"

#include <cstdint>
#include <optional>

namespace Elite
{

// The reference's hyperspace routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): hyperspace, galactic hyperspace and
// witchspace. Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// Where IsMassLocked's look at the ship slots stopped.
struct ShipScan
{
  SlotIndex slot;          ///< the ship that locks the jump drive, or past the last slot looked at
  std::uint16_t slotsLeft; ///< the slots the loop had still to look at there
};

/// What IsMassLocked found, and what it looked at on the way, which the original leaves in the registers.
struct MassLock
{
  bool locked;
  SafeZone zone;                  ///< what InSafeZone read
  std::optional<NearTest> sun;    ///< what IsObjectNear found of the sun's slot, outside the safe zone
  std::optional<NearTest> planet; ///< and of the planet's, once the sun is far
  std::optional<ShipScan> ships;  ///< once both are far
};

/// What EngageJumpDrive (Flight.h) did.
struct JumpDriveRequest
{
  std::optional<MassLock> lock; ///< what IsMassLocked found, once it was asked
};

/// IsMassLocked: whether the jump drive is mass-locked: in the station's safe zone, near the sun or the planet
/// (IsObjectNear, which erases the blip of one that is far), or with an active ship from firstShipSlot that is not an
/// asteroid, boulder, barrel or splinter and has its blip drawn. With objectSlotCount 3 or less, or above 127, it looks at no
/// ship, and is locked when the count is below 3, which borrows when 3 is taken from it.
MassLock IsMassLocked(GameState& _state);

/// ResetHyperspaceRings: hyperspaceRings from hyperspaceRingStart, fifteen words copied upwards, as the direction
/// flag is clear wherever the game calls it (change 3c).
void ResetHyperspaceRings(GameState& _state);

/// EnterWitchSpace: the witch-space countdown, and the player halfway to the destination on the charts.
void EnterWitchSpace(GameState& _state);

/// UpdateMissionSchedule: the mask ship's and the missions' jump counts, on an arrival outside witch space; the
/// missions' only outside the first galaxy, or with missionsInFirstGalaxy set.
void UpdateMissionSchedule(GameState& _state);

/// LatchHyperspaceTarget: the selected system becomes the hyperspace target: its index, and systemRecordBytes
/// of its record, 65536 for a count of 0.
void LatchHyperspaceTarget(GameState& _state);

/// ShowHyperspaceCountdown: a beep (StartBeep), and the countdown on the message line for ten frames: its two
/// digits, "10" or a space and the digit, written into hyperspaceCountdownMessage.
void ShowHyperspaceCountdown(GameState& _state);

/// ArriveInSystem: SetUpLocalSpace, then, out of witch space, the sun, the planet
/// and the station moved by one random offset, 200h-3FFh either way in the top 16 bits of x and y and a random word in z's low
/// word, and the player turned to the station (ComputeAnglesToObject) with a random roll.
void ArriveInSystem(GameState& _state, Host& _host);

/// CompleteHyperspaceJump: the jump, galactic when galacticJumpPending (GalacticJump): the compass and the blips erased,
/// 'Hyperspace Motors Engaged', the fuel spent and 5 off legalStatus,
/// the destination made current, its seeds loaded; witch space on a mis-jump, else the charts' cursors on it; then the tunnel
/// (PlayHyperspaceTunnel), the arrival (ArriveInSystem), the missions'
/// schedule and the arrival's message. Waits, in the tunnel's frames.
[[nodiscard]] Task<void> CompleteHyperspaceJump(GameState& _state, Host& _host);

/// DrawHyperspaceRings: one frame of the hyperspace rings: each counts its delay down, then grows by an eighth, at
/// least 1, below 150, and from 20 on is drawn (DrawCircle) in its colour about the space view's centre. Returns the rings it
/// drew, for the renderer (ADR-069).
SnapshotTunnel DrawHyperspaceRings(GameState& _state);

/// PlayHyperspaceTunnel: fifty frames of the hyperspace rings: UpdateMessageLine, DrawHyperspaceRings and
/// FinishSpaceViewFrame. Waits, a turn of its loop for each frame after the first.
[[nodiscard]] Task<void> PlayHyperspaceTunnel(GameState& _state, Host& _host);

/// TickHyperspaceCountdown: the galactic drive's ready frames, and the hyperspace countdown, a step every ten frames
/// (ShowHyperspaceCountdown), which jumps (CompleteHyperspaceJump) when it runs out. Returns
/// whether it jumped. Waits then.
[[nodiscard]] Task<bool> TickHyperspaceCountdown(GameState& _state, Host& _host);

} // namespace Elite
