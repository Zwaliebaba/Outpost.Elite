// GameLogic/Combat.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's combat routines, ported (plan §5 Phase 3, ADR-010): lasers, missiles, hits and explosions. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> CombatEntries() noexcept;

/// DrawLaserSights (CS:0630): the sights of the current view's laser, ANDed and ORed into the centre of the space view.
void DrawLaserSights(Guest& _guest);

/// GetViewLaser (CS:066C): CF set and AL = the laser type 0-3 when the current view's mount has a laser.
void GetViewLaser(Guest& _guest);

/// DrawLaserBeams (CS:0A9A): the player's beams, from the bottom of the view to near its centre, in firingLaserType's pattern.
void DrawLaserBeams(Guest& _guest);

/// ExplodeObject (CS:4FC1): the object at DI removed, and when it was drawn, its fragments and its cargo barrels spawned.
void ExplodeObject(Guest& _guest);

/// TallyMaskMissionKill (CS:50FE): counts a mask-mission Asp off, and notes the mask ship's.
void TallyMaskMissionKill(Guest& _guest);

/// TryFireLaserAtPlayer (CS:518B): the ship at DI fires at the player when it may, with AX, BX its aim errors.
void TryFireLaserAtPlayer(Guest& _guest);

/// TryLaunchMissileAtPlayer (CS:543A): the ship at DI launches a missile when it may, with odds BX out of 65536.
void TryLaunchMissileAtPlayer(Guest& _guest);

/// TryLaunchThargon (CS:5471): a Thargoid at DI launches a Thargon, at odds of 300 in 65536.
void TryLaunchThargon(Guest& _guest);

/// FindShipInCrosshairs (CS:8A46): CF set and DI = the nearest drawn object over the view's centre; CF clear when none is.
void FindShipInCrosshairs(Guest& _guest);

/// ResolveLaserFire (CS:8AC2): a shot of the player's laser: the hit, its damage and its consequences, then the beams.
void ResolveLaserFire(Guest& _guest);

/// CheckMissileTargetDestroyed (CS:8B8B): unlocks a missile locked on DI, with its message.
void CheckMissileTargetDestroyed(Guest& _guest);

/// CreditKill (CS:8BC6): the kill of DI paid for, or held against the player's legal status.
void CreditKill(Guest& _guest);

/// ApplyEnemyLaserHit (CS:8C8E): a pending hit on the player: its beam, then the damage to a shield and the energy.
void ApplyEnemyLaserHit(Guest& _guest);

} // namespace Elite
