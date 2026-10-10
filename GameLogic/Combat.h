// GameLogic/Combat.h
#pragma once

#include "Flight.h"
#include "GameState.h"
#include "NativeEntry.h"
#include "ObjectSlot.h"

#include <cstdint>
#include <optional>
#include <span>

namespace Elite
{

// The reference's combat routines, ported (plan §5 Phase 3, ADR-010): lasers, missiles, hits and explosions. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv; those de-assembled (ADR-012) take values and give
// values back, below the register bodies, and their entries keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> CombatEntries() noexcept;

/// DrawLaserBeams (CS:0A9A): the player's beams, from the bottom of the view to near its centre, in firingLaserType's pattern.
void DrawLaserBeams(Guest& _guest);

/// DetonateEnergyBomb (CS:2ED6): every active ship with a blip exploded, its cargo emptied first; a crime in the safe zone.
void DetonateEnergyBomb(Guest& _guest);

/// SpawnPlayerWreckage (CS:2FE3): the player's death: the view stopped, six splinters drifting along the nose, and a barrel of
/// the cargo if there is any.
void SpawnPlayerWreckage(Guest& _guest);

/// ExplodeObject (CS:4FC1): the object at DI removed, and when it was drawn, its fragments and its cargo barrels spawned.
void ExplodeObject(Guest& _guest);

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

/// ApplyEnemyLaserHit (CS:8C8E): a pending hit on the player: its beam, then the damage to a shield and the energy.
void ApplyEnemyLaserHit(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──

/// What Routine8C51 finds of a slot.
struct ThargoidTest
{
  std::uint8_t type;      ///< bits 1-5 of the type byte
  bool thargoidOrThargon; ///< a Thargon (7) or a Thargoid (16h)
  bool thargoid;
};

/// What CreditKill did with a kill.
struct KillCredit
{
  std::uint8_t bounty;                     ///< the slot's bounty byte: 0 pays nothing, and FFh pays only for a Thargoid
  std::optional<std::uint16_t> paidTenths; ///< what it paid, in tenths of a credit, when it paid
  std::optional<ThargoidTest> killed;      ///< once it paid while a mis-jump's countdown runs: what Routine8C51 found of the slot
  bool repaired;                           ///< the countdown reached 1, and the Nav-Comp's repair was announced
  std::optional<SafeZone> zone;            ///< for a kill that paid nothing: what InSafeZone read
  std::optional<std::uint8_t> crime;       ///< what that kill added to legalStatus, when it was a crime
};

/// DrawLaserSights (CS:0630): the sights of the current view's laser, a 16x16 sprite of laserSights, ANDed and ORed into the
/// centre of the space view, a row of two words at a time. Nothing when the view's mount has no laser.
void DrawLaserSights(GameState& _state);

/// GetViewLaser (CS:066C): the laser type 0-3 of the current view's mount, or none when that mount has no laser. The mount is
/// viewLaserMount's entry for the view; whether it is fitted, bit mount-1 of laserMountsFitted, which RCR reaches a bit at a
/// time; its type, the 2-bit field mount-1 of laserMountTypes.
[[nodiscard]] std::optional<std::uint8_t> GetViewLaser(const GameState& _state);

/// TakeDamage (CS:2C9B): _damage taken by the fore shield and what it cannot take by the energy: from 100h on, the shield takes all
/// it holds; below, the low byte goes against the shield, written back, and 0 over it on a borrow. An energy that goes below 0
/// kills the player (KillPlayer) and is then 0. Nothing while the escape pod flies. Returns the step length the death sound
/// starts at, when it kills.
std::optional<std::uint8_t> TakeDamage(GameState& _state, std::uint16_t _damage);

/// KillPlayer (CS:3115): unless the escape pod flies, playerDead and StartPlayerDeathSound. Returns the step length that sound
/// starts at, when it starts.
std::optional<std::uint8_t> KillPlayer(GameState& _state);

/// InitMissile (CS:4C8C): _slot made a missile (spawnTemplates entry 0), class 2.
void InitMissile(GameState& _state, ObjectSlot _slot);

/// RemoveAllMissiles (CS:4F9F): every active missile among objectSlotCount slots from shipSlots removed (RemoveObject). Returns
/// whether that erased a scanner blip.
bool RemoveAllMissiles(GameState& _state);

/// TallyMaskMissionKill (CS:50FE): while maskMissionShipsLeft is not 0, an Asp at _slot with the mission's bounty counts it down,
/// and one that carries the device sets maskShipDestroyed.
void TallyMaskMissionKill(GameState& _state, const ObjectSlot& _slot);

/// TryFireLaserAtPlayer (CS:518B): _slot fires at the player when it may: its blip drawn, a random byte below its aggression, not
/// holding fire (CheckSafeZoneHoldFire) and nothing blocking it; each shot calms it by 5 down to 20. With _pitchError and
/// _yawError, the magnitudes TurnTowardAngles leaves, within 200 the player is grazed by it, and within 70, the slot's camera-z
/// high byte below 70 too, hit squarely.
void TryFireLaserAtPlayer(GameState& _state, ObjectSlot _slot, std::uint16_t _pitchError, std::uint16_t _yawError);

/// CheckMissileTargetDestroyed (CS:8B8B): when a missile is locked on the slot at _slot, its message and the missile unlocked.
/// Returns whether it was.
bool CheckMissileTargetDestroyed(GameState& _state, std::uint16_t _slot);

/// CreditKill (CS:8BC6): the kill of _slot paid for, or held against the player's legal status. A bounty of 0 pays nothing;
/// any other counts a kill in killCount, the INC written and then FEh over FFh; FFh pays 500 for a Thargoid and otherwise is a
/// crime, 4 for a police Viper and 2 for anything else inside the safe zone, added to legalStatus up to FFh, the sum written and
/// then FFh over it on a carry. A bounty paid goes through ShowBountyMessage and AddCredits, and while a mis-jump's countdown
/// runs, the kill of a Thargon or a Thargoid counts it down by 5 or 35, to 1 at the least, where the Nav-Comp's repair is
/// announced.
KillCredit CreditKill(GameState& _state, ObjectSlot _slot);

/// Routine8C51 (CS:8C51): _slot's type, and whether it is a Thargon's or a Thargoid's.
[[nodiscard]] ThargoidTest Routine8C51(const ObjectSlot& _slot);

/// UseMaskingDevice (CS:8ECF): 12 off the energy, at least 0, the background blue, and for each of objectSlotCount slots from
/// shipSlots, its state and hostility cleared and 2 off its aggression, at least 0.
void UseMaskingDevice(GameState& _state);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

/// The registers RemoveAllMissiles' original leaves, _erased saying whether it erased a scanner blip: DI past the slots it looked
/// at, and ES = B800h once it erased one. For its entry, and for UpdateStationAi's register code, whose contract compares them.
void RemoveAllMissilesOut(Guest& _guest, bool _erased);

void DrawLaserSightsEntry(Guest& _guest);             ///< AX, BX, CX, SI, DI clobbered.
void GetViewLaserEntry(Guest& _guest);                ///< Out: CF set and AL = the type when the mount has a laser; AX, BX, CX clobbered.
void TakeDamageEntry(Guest& _guest);                  ///< AX = the damage. Out: AX and BX as the original leaves them; IF=1 when it kills.
void KillPlayerEntry(Guest& _guest);                  ///< Out: AL the sound's step length and IF=1, when it starts.
void InitMissileEntry(Guest& _guest);                 ///< DI = the slot. AX, BX clobbered.
void RemoveAllMissilesEntry(Guest& _guest);           ///< Out: DI past the slots, ES = B800h once a blip is erased; AX-DX clobbered.
void TallyMaskMissionKillEntry(Guest& _guest);        ///< DI = the slot. Out: AL = its type while the mission runs.
void TryFireLaserAtPlayerEntry(Guest& _guest);        ///< DI = the slot, AX, BX = the aim errors. AX, BX, CX, DX clobbered.
void CheckMissileTargetDestroyedEntry(Guest& _guest); ///< DI = the slot. Out: AX = the message, when unlocked.
void CreditKillEntry(Guest& _guest);                  ///< DI = the slot. Out: AX, BX, CX and SI as the original leaves them.
void Routine8C51Entry(Guest& _guest);                 ///< DI = the slot. Out: AL = its type; ZF for either, CF for the Thargoid.
void UseMaskingDeviceEntry(Guest& _guest);            ///< Out: SI past the slots, CX = 0.

} // namespace Elite
