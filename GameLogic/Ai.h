// GameLogic/Ai.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's ai routines, ported (plan §5 Phase 3, ADR-010): the ships' behaviour: traders, police, pirates, the station.
// Each body is declared here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> AiEntries() noexcept;

/// UpdateObjectsAndSpawn (CS:4A10): every active slot counted and run by its class's handler, then new ships spawned.
void UpdateObjectsAndSpawn(Guest& _guest);

/// ScaleSpawnOdds (CS:4C0E): BX multiplied by 32 while jumpDriveEngaged is 1.
void ScaleSpawnOdds(Guest& _guest);

/// TurnTowardAngles (CS:514B): the heading of the slot at DI turned toward AX, BX by at most its turn rate. Out: AX, BX the
/// errors' magnitudes.
void TurnTowardAngles(Guest& _guest);

/// ClampTurnStep (CS:5166): AX = the step from CX toward AX within the turn rate of DI; BX = the error's magnitude.
void ClampTurnStep(Guest& _guest);

/// CountOtherHuntersOnScanner (CS:548F): AL = the other class-6 ships with blips; BP = the last of them.
void CountOtherHuntersOnScanner(Guest& _guest);

/// SkipInertObjectAi (CS:5594): class 0's handler, which does nothing.
void SkipInertObjectAi(Guest& _guest);

/// UpdateStationAi (CS:5595): class 1: the station spins, launches police or traders at an offender, and answers missiles.
void UpdateStationAi(Guest& _guest);

/// UpdateDriftingObjectAi (CS:5681): class 3: moves, and tumbles if a rock.
void UpdateDriftingObjectAi(Guest& _guest);

/// UpdateWolfAi (CS:57E8): class 5: attack passes at the player.
void UpdateWolfAi(Guest& _guest);

/// UpdateHunterAi (CS:58DE): class 6: pack hunting.
void UpdateHunterAi(Guest& _guest);

/// CheckSafeZoneHoldFire (CS:5A10): CF set when the ship at DI must not fire.
void CheckSafeZoneHoldFire(Guest& _guest);

} // namespace Elite
