// GameLogic/Hyperspace.h
#pragma once

#include "GameState.h"
#include "NativeEntry.h"
#include "Ships.h"

#include <cstdint>
#include <optional>
#include <span>

namespace Elite
{

// The reference's hyperspace routines, ported (plan §5 Phase 3, ADR-010): hyperspace, galactic hyperspace and witchspace. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> HyperspaceEntries() noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// Where IsMassLocked's look at the ship slots stopped.
struct ShipScan
{
  std::uint16_t slot;      ///< the ship that locks the jump drive, or past the last slot looked at: what the original leaves in DI
  std::uint16_t slotsLeft; ///< the count LOOP had there, CX
  std::optional<std::uint16_t> lastLooked; ///< the last slot it looked at, whose type byte the original leaves in AL
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
  std::uint16_t message;        ///< the message it posted
  std::optional<MassLock> lock; ///< what IsMassLocked found, once it was asked
};

/// IsMassLocked (CS:4144): whether the jump drive is mass-locked: in the station's safe zone, near the sun or the planet
/// (IsObjectNear, which erases the blip of one that is far), or with an active ship from firstShipSlot that is not an
/// asteroid, boulder, barrel or splinter and has its blip drawn. With objectSlotCount 3 or less, or above 127, it looks at no
/// ship, and is locked when the count is below 3, the borrow of its SUB.
MassLock IsMassLocked(GameState& _state);

/// ResetHyperspaceRings (CS:48AB): hyperspaceRings from hyperspaceRingStart, fifteen words copied up through the
/// data segment, or down from each start when _backward (REP MOVSW with the direction flag set).
void ResetHyperspaceRings(GameState& _state, bool _backward);

/// EnterWitchSpace (CS:4917): the witch-space countdown, and the player halfway to the destination on the charts.
void EnterWitchSpace(GameState& _state);

/// UpdateMissionSchedule (CS:4953): the mask ship's and the missions' jump counts, on an arrival outside witch space.
/// Returns whether the jump counted towards the missions: outside the first galaxy, or with data7629 set.
bool UpdateMissionSchedule(GameState& _state);

/// LatchHyperspaceTarget (CS:49F6): the selected system becomes the hyperspace target: its index, and systemRecordBytes
/// of its record, 65536 for a count of 0.
void LatchHyperspaceTarget(GameState& _state);

/// ShowHyperspaceCountdown (CS:8C62): a beep (StartBeep), and the countdown on the message line for ten frames: its two
/// digits, "10" or a space and the digit, written into hyperspaceCountdownMessage.
void ShowHyperspaceCountdown(GameState& _state);

/// ArriveInSystem (CS:2B5A): SetUpLocalSpace (its copies backwards when _backward), then, out of witch space, the sun, the planet
/// and the station moved by one random offset, 200h-3FFh either way in the top 16 bits of x and y and a random word in z's low
/// word, and the player turned to the station (ComputeAnglesToObject) with a random roll. Returns what ShowCockpitScreen did.
ScreenChange ArriveInSystem(GameState& _state, Hardware& _hardware, bool _backward);

/// CompleteHyperspaceJump (CS:4707): the jump, galactic when galacticJumpPending (GalacticJump, whose search takes _countIfNone, the
/// BP it is called with): the compass and the blips erased, 'Hyperspace Motors Engaged', the fuel spent and 5 off legalStatus,
/// the destination made current, its seeds loaded; witch space on a mis-jump, else the charts' cursors on it; then the tunnel
/// (PlayHyperspaceTunnel, its first message line by _backward, the direction flag), the arrival (ArriveInSystem), the missions'
/// schedule and the arrival's message. Waits, in the tunnel's frames.
void CompleteHyperspaceJump(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward);

/// DrawHyperspaceRings (CS:48C0): one frame of the hyperspace rings: each counts its delay down, then grows by an eighth, at
/// least 1, below 150, and from 20 on is drawn (DrawCircle) in its colour about the space view's centre. Returns whether a chord's
/// DrawLine filled a horizontal line's bytes.
bool DrawHyperspaceRings(GameState& _state);

/// PlayHyperspaceTunnel (CS:4906): fifty frames of the hyperspace rings: UpdateMessageLine (the first by _backward, the
/// direction flag), DrawHyperspaceRings and FinishSpaceViewFrame. Waits, a turn of its loop for each frame after the first.
void PlayHyperspaceTunnel(GameState& _state, Hardware& _hardware, bool _backward);

/// TickHyperspaceCountdown (CS:7F79): the galactic drive's ready frames, and the hyperspace countdown, a step every ten frames
/// (ShowHyperspaceCountdown), which jumps (CompleteHyperspaceJump, with _countIfNone and _backward) when it runs out. Returns
/// whether it jumped. Waits then.
bool TickHyperspaceCountdown(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

/// The registers IsMassLocked's original leaves once it found _lock: AL as InSafeZone leaves it; then what each IsObjectNear
/// leaves (IsObjectNearOut), with DI on its slot; then DI and CX where the look at the ships stopped, and AL the last slot's type
/// byte, SHR AL,1, and AND AL,1Fh once it is active. For its entry, and for EngageJumpDrive's, whose contract compares them.
void MassLockOut(Guest& _guest, const MassLock& _lock);

void IsMassLockedEntry(Guest& _guest);            ///< Out: CF set when locked; every register as the original leaves it.
void ResetHyperspaceRingsEntry(Guest& _guest);    ///< Out: ES=B800h; AX, CX, SI and DI clobbered.
void EnterWitchSpaceEntry(Guest& _guest);         ///< AX and BX clobbered.
void UpdateMissionScheduleEntry(Guest& _guest);   ///< Out: AL the mission picked, once the jump counts.
void LatchHyperspaceTargetEntry(Guest& _guest);   ///< Out: AL the last byte copied; CX, SI and DI clobbered.
void ShowHyperspaceCountdownEntry(Guest& _guest); ///< AX clobbered.
void ArriveInSystemEntry(Guest& _guest);          ///< Out: DF clear once the cockpit was drawn. Clobbers all.
/// BP, as GalacticJump's search takes it. Out: ES = B800h, DF clear. AX, BX, CX, DX, SI, DI and BP clobbered.
void CompleteHyperspaceJumpEntry(Guest& _guest);
void DrawHyperspaceRingsEntry(Guest& _guest);     ///< Out: DF clear once a chord was a horizontal line. Clobbers all.
void PlayHyperspaceTunnelEntry(Guest& _guest);    ///< Out: DF clear. Clobbers all.
void TickHyperspaceCountdownEntry(Guest& _guest); ///< BP, as CompleteHyperspaceJumpEntry. Out: once it jumps, as it. Clobbers all but DS.

} // namespace Elite
