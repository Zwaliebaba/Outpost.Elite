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

/// ResetHyperspaceRings (CS:48AB): hyperspaceRings from hyperspaceRingStart. Out: ES = B800h.
void ResetHyperspaceRings(Guest& _guest);

/// DrawHyperspaceRings (CS:48C0): one frame of the hyperspace rings: each counts down, then grows and is
/// drawn.
void DrawHyperspaceRings(Guest& _guest);

/// UpdateMissionSchedule (CS:4953): the mask ship's and the missions' jump counts, on an arrival outside
/// witch space.
void UpdateMissionSchedule(Guest& _guest);

/// LatchHyperspaceTarget (CS:49F6): the selected system becomes the hyperspace target.
void LatchHyperspaceTarget(Guest& _guest);

/// ShowHyperspaceCountdown (CS:8C62): a beep, and the countdown on the message line.
void ShowHyperspaceCountdown(Guest& _guest);

} // namespace Elite
