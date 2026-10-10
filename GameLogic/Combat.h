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

/// TakeDamage (CS:2C9B): AX of damage, taken by the fore shield and then the energy, which KillPlayer follows below 0. Nothing
/// while the escape pod flies. AX and BX clobbered.
void TakeDamage(Guest& _guest);

/// DetonateEnergyBomb (CS:2ED6): every active ship with a blip exploded, its cargo emptied first; a crime in the safe zone.
void DetonateEnergyBomb(Guest& _guest);

/// SpawnPlayerWreckage (CS:2FE3): the player's death: the view stopped, six splinters drifting along the nose, and a barrel of
/// the cargo if there is any.
void SpawnPlayerWreckage(Guest& _guest);

/// KillPlayer (CS:3115): playerDead and its sound, unless the escape pod flies.
void KillPlayer(Guest& _guest);

/// InitMissile (CS:4C8C): the slot at DI made a missile (spawnTemplates entry 0), class 2.
void InitMissile(Guest& _guest);

/// RemoveAllMissiles (CS:4F9F): every active missile among the object slots removed.
void RemoveAllMissiles(Guest& _guest);

/// ExplodeObject (CS:4FC1): the object at DI removed, and when it was drawn, its fragments and its cargo barrels spawned.
void ExplodeObject(Guest& _guest);

/// TallyMaskMissionKill (CS:50FE): counts a mask-mission Asp off, and notes the mask ship's.
void TallyMaskMissionKill(Guest& _guest);

/// TryFireLaserAtPlayer (CS:518B): the ship at DI fires at the player when it may, with AX, BX its aim errors.
void TryFireLaserAtPlayer(Guest& _guest);

/// LaunchPlayerMissile (CS:5242): the 64 bytes at DI copied into a slot, free or reclaimed, and made a missile 100 along the
/// player's nose, locked on missileTarget.
void LaunchPlayerMissile(Guest& _guest);

/// LaunchShipFromObject (CS:534E): CF set when a copy of the object at DI went into a free slot as what DL names: 14h a missile at
/// the player, 15h an escape pod, 7 a Thargon, 5 a Krait (which returns to CS:DI). DI kept.
void LaunchShipFromObject(Guest& _guest);

/// TryLaunchMissileAtPlayer (CS:543A): the ship at DI launches a missile when it may, with odds BX out of 65536.
void TryLaunchMissileAtPlayer(Guest& _guest);

/// TryLaunchThargon (CS:5471): a Thargoid at DI launches a Thargon, at odds of 300 in 65536.
void TryLaunchThargon(Guest& _guest);

/// UpdateMissileAi (CS:54F2): class 2: the missile at DI flies at its target, or the player, and explodes on it.
void UpdateMissileAi(Guest& _guest);

/// FindShipInCrosshairs (CS:8A46): CF set and DI = the nearest drawn object over the view's centre; CF clear when none is.
void FindShipInCrosshairs(Guest& _guest);

/// ResolveLaserFire (CS:8AC2): a shot of the player's laser: the hit, its damage and its consequences, then the beams.
void ResolveLaserFire(Guest& _guest);

/// CheckMissileTargetDestroyed (CS:8B8B): unlocks a missile locked on DI, with its message.
void CheckMissileTargetDestroyed(Guest& _guest);

/// CreditKill (CS:8BC6): the kill of DI paid for, or held against the player's legal status.
void CreditKill(Guest& _guest);

/// Routine8C51 (CS:8C51): AL = the type of the slot at DI; ZF set for a Thargon (7) or a Thargoid (16h), CF for the Thargoid.
void Routine8C51(Guest& _guest);

/// ApplyEnemyLaserHit (CS:8C8E): a pending hit on the player: its beam, then the damage to a shield and the energy.
void ApplyEnemyLaserHit(Guest& _guest);

/// UseMaskingDevice (CS:8ECF): 12 off the energy, the background blue, and every ship's state and hostility cleared, its
/// aggression less 2. SI and CX are left past the slots.
void UseMaskingDevice(Guest& _guest);

} // namespace Elite
