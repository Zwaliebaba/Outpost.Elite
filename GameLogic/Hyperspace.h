// GameLogic/Hyperspace.h
#pragma once

#include "GameState.h"
#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's hyperspace routines, ported (plan §5 Phase 3, ADR-010): hyperspace, galactic hyperspace and witchspace. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> HyperspaceEntries() noexcept;

/// ArriveInSystem (CS:2B5A): SetUpLocalSpace, then, unless in witch space, the sun, planet and station
/// moved by one random offset and the player turned towards the station.
void ArriveInSystem(Guest& _guest);

/// IsMassLocked (CS:4144): CF set when the jump drive is mass-locked: in the station's safe zone, near
/// the sun or the planet, or with a ship other than a rock, barrel or splinter on the scanner.
void IsMassLocked(Guest& _guest);

/// CompleteHyperspaceJump (CS:4707): the jump, galactic when galacticJumpPending: the destination made
/// current, or witch space on a mis-jump, the tunnel, and the arrival. Waits. Out: ES = B800h.
void CompleteHyperspaceJump(Guest& _guest);

/// DrawHyperspaceRings (CS:48C0): one frame of the hyperspace rings: each counts down, then grows and is
/// drawn.
void DrawHyperspaceRings(Guest& _guest);

/// PlayHyperspaceTunnel (CS:4906): fifty frames of the hyperspace rings. Waits. Clobbers all.
void PlayHyperspaceTunnel(Guest& _guest);

/// TickHyperspaceCountdown (CS:7F79): the galactic drive's ready frames, and the hyperspace countdown, a
/// step every ten frames, which jumps when it runs out. Waits then. Clobbers all.
void TickHyperspaceCountdown(Guest& _guest);

/// ShowHyperspaceCountdown (CS:8C62): a beep, and the countdown on the message line.
void ShowHyperspaceCountdown(Guest& _guest);

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

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

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void ResetHyperspaceRingsEntry(Guest& _guest);  ///< Out: ES=B800h; AX, CX, SI and DI clobbered.
void EnterWitchSpaceEntry(Guest& _guest);       ///< AX and BX clobbered.
void UpdateMissionScheduleEntry(Guest& _guest); ///< Out: AL the mission picked, once the jump counts.
void LatchHyperspaceTargetEntry(Guest& _guest); ///< Out: AL the last byte copied; CX, SI and DI clobbered.

} // namespace Elite
