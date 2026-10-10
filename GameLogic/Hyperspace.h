// GameLogic/Hyperspace.h
#pragma once

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

/// ResetHyperspaceRings (CS:48AB): hyperspaceRings from hyperspaceRingStart. Out: ES = B800h.
void ResetHyperspaceRings(Guest& _guest);

/// DrawHyperspaceRings (CS:48C0): one frame of the hyperspace rings: each counts down, then grows and is
/// drawn.
void DrawHyperspaceRings(Guest& _guest);

/// PlayHyperspaceTunnel (CS:4906): fifty frames of the hyperspace rings. Waits. Clobbers all.
void PlayHyperspaceTunnel(Guest& _guest);

/// EnterWitchSpace (CS:4917): the witch-space countdown, and the player halfway to the destination on
/// the charts. AX, BX clobbered.
void EnterWitchSpace(Guest& _guest);

/// UpdateMissionSchedule (CS:4953): the mask ship's and the missions' jump counts, on an arrival outside
/// witch space.
void UpdateMissionSchedule(Guest& _guest);

/// LatchHyperspaceTarget (CS:49F6): the selected system becomes the hyperspace target.
void LatchHyperspaceTarget(Guest& _guest);

/// TickHyperspaceCountdown (CS:7F79): the galactic drive's ready frames, and the hyperspace countdown, a
/// step every ten frames, which jumps when it runs out. Waits then. Clobbers all.
void TickHyperspaceCountdown(Guest& _guest);

/// ShowHyperspaceCountdown (CS:8C62): a beep, and the countdown on the message line.
void ShowHyperspaceCountdown(Guest& _guest);

} // namespace Elite
