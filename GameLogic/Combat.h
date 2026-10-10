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

// ── The routines (ADR-012): values in, values out, on the GameState ──

/// What Routine8C51 finds of a slot.
struct ThargoidTest
{
  std::uint8_t type;      ///< bits 1-5 of the type byte
  bool thargoidOrThargon; ///< a Thargon (7) or a Thargoid (16h)
  bool thargoid;
};

/// What ExplodeObject leaves of the registers its callers go on with.
struct Explosion
{
  std::uint16_t slot; ///< DI: the object, or past the slots FindFreeShipSlot looks at once a barrel's search for one failed
  std::uint16_t bx;   ///< BX, which the barrel count's divide of a later explosion saves, should it trap
  /// DX, when it writes DX: what the last blip it erased or the last object it moved leaves there (MovedObject). UpdateMissileAi
  /// hands it on to UpdateObjectsAndSpawn's next handler.
  std::optional<std::uint16_t> dx;
  std::optional<std::uint16_t> si; ///< SI, when it writes SI: the object once fragments flew, else what the last barrel was copied from
  /// ES, when it writes ES: the data segment after a copy (CopyObject), the video segment after an erased blip.
  std::optional<std::uint16_t> es;
};

/// What DetonateEnergyBomb's explosions leave in SI and ES, the last of them to write each, which DrawSunOrPlanet goes on with.
struct Detonation
{
  std::optional<std::uint16_t> si;
  std::optional<std::uint16_t> es;
};

/// What FindShipInCrosshairs finds.
struct CrosshairTarget
{
  std::optional<std::uint16_t> slot; ///< the nearest drawn object over the view's centre, if there is one
  /// What the original leaves in BX: BL the type byte of the last slot it looked at, BH what the last slot it tested left there,
  /// which is all of BX when that was the last slot. ExplodeObject's barrel count saves it, should its divide trap.
  std::uint16_t bx;
  /// What the original leaves in DI: the slot found, or past the slots it looked at, which HandleMissileKeys' launch copies from.
  std::uint16_t di;
};

/// What UpdateMissileAi leaves that UpdateObjectsAndSpawn's next handler can read.
struct MissileFlight
{
  std::uint16_t slot; ///< DI: the missile, its target, or where an explosion left DI
  std::uint16_t dx;   ///< DX, of which the next handler takes DL for its range's box
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

/// DrawLaserBeams (CS:0A9A): the player's beams, DrawLine from the bottom row of the view to a random point within 3 pixels of
/// (7Eh, 3Eh), in firingLaserType's pattern: with bit 0 clear, one side's pair a shot, the sides alternating by laserBeamSide
/// and the colour counting down past 0 for type 0 and up for type 2; type 1 both outer beams, the colour counting up; type 3 all
/// four, the inner pair in laserBeamColorToggle's colour and the outer in the other. Returns whether a beam was a horizontal
/// line, which DrawLine fills by REP STOSB.
bool DrawLaserBeams(GameState& _state);

/// TakeDamage (CS:2C9B): _damage taken by the fore shield and what it cannot take by the energy: from 100h on, the shield takes all
/// it holds; below, the low byte goes against the shield, written back, and 0 over it on a borrow. An energy that goes below 0
/// kills the player (KillPlayer) and is then 0. Nothing while the escape pod flies. Returns the step length the death sound
/// starts at, when it kills.
std::optional<std::uint8_t> TakeDamage(GameState& _state, std::uint16_t _damage);

/// DetonateEnergyBomb (CS:2ED6): in the safe zone a crime, 28h added to legalStatus up to FFh (the sum written, then FFh over
/// it on a carry); then every active ship from firstShipSlot with its blip drawn has its cargo emptied and explodes
/// (ExplodeObject, its copies backwards when _backward). Returns what the explosions leave in SI and ES.
Detonation DetonateEnergyBomb(GameState& _state, Hardware& _hardware, bool _backward);

/// SpawnPlayerWreckage (CS:2FE3): the player's death: the drift, 40 along the nose (ComputeDeathDebrisVector), kept in
/// wreckDrift; the player stopped at speed 8 with the view locked; six splinters in debris slots drifting with it and a
/// random -15..16 on each axis, each run for a frame (UpdateDebrisAi); and with cargo aboard, a barrel in a ship slot, free or
/// reclaimed, four drifts ahead, drifting at a quarter of the drift and a little, turned along it and moved a random -31..32 on
/// x and y.
void SpawnPlayerWreckage(GameState& _state);

/// KillPlayer (CS:3115): unless the escape pod flies, playerDead and StartPlayerDeathSound. Returns the step length that sound
/// starts at, when it starts.
std::optional<std::uint8_t> KillPlayer(GameState& _state);

/// InitMissile (CS:4C8C): _slot made a missile (spawnTemplates entry 0), class 2.
void InitMissile(GameState& _state, ObjectSlot _slot);

/// RemoveAllMissiles (CS:4F9F): every active missile among objectSlotCount slots from shipSlots removed (RemoveObject). Returns
/// the last pixel of the last scanner blip that erased, if it erased one.
std::optional<DashboardPixel> RemoveAllMissiles(GameState& _state);

/// ExplodeObject (CS:4FC1): _object removed (TallyMaskMissionKill first, then RemoveObject); when it was drawn this frame, the
/// explosion's sound, its fragments in debris slots (copied from it, backwards when _backward), and its cargo's barrels in free
/// ship slots: one for a ship that carries the device, else a random byte / (255 / (cargo + 1) + 1). _bx is the BX it is called
/// with, which the barrel count's first divide saves when a cargo of FFh makes it divide by 0. Returns what it leaves in DI, BX
/// and DX.
Explosion ExplodeObject(GameState& _state, Hardware& _hardware, ObjectSlot _object, bool _backward, std::uint16_t _bx);

/// TallyMaskMissionKill (CS:50FE): while maskMissionShipsLeft is not 0, an Asp at _slot with the mission's bounty counts it down,
/// and one that carries the device sets maskShipDestroyed.
void TallyMaskMissionKill(GameState& _state, const ObjectSlot& _slot);

/// TryFireLaserAtPlayer (CS:518B): _slot fires at the player when it may: its blip drawn, a random byte below its aggression, not
/// holding fire (CheckSafeZoneHoldFire) and nothing blocking it; each shot calms it by 5 down to 20. With _pitchError and
/// _yawError, the magnitudes TurnTowardAngles leaves, within 200 the player is grazed by it, and within 70, the slot's camera-z
/// high byte below 70 too, hit squarely.
void TryFireLaserAtPlayer(GameState& _state, ObjectSlot _slot, std::uint16_t _pitchError, std::uint16_t _yawError);

/// LaunchPlayerMissile (CS:5242): the 64 bytes at _source copied (CopyObject, backwards when _backward) into a ship slot, free
/// or reclaimed, and made a missile (InitMissile) 100 along the player's nose, locked on missileTarget, aimed at where that is,
/// its velocity set and moved twice. A slot ReclaimShipSlot evicts is copied onto itself, as the original's XCHG SI,DI leaves it.
void LaunchPlayerMissile(GameState& _state, std::uint16_t _source, bool _backward);

/// LaunchShipFromObject (CS:534E): a copy of _launcher (CopyObject, backwards when _backward) in a free ship slot, made what
/// _kind names and moved clear of it: 14h a missile at the player, moved three times; 15h an escape pod, its heading random,
/// moved three times; 7 a Thargon whose mother is _launcher, and 5 a Krait, each moved twice. Returns the slot, or nothing
/// when no slot is free or _kind is none of these.
std::optional<std::uint16_t> LaunchShipFromObject(GameState& _state, const ObjectSlot& _launcher, std::uint8_t _kind, bool _backward);

/// TryLaunchMissileAtPlayer (CS:543A): _slot launches a missile at the player (LaunchShipFromObject, the copy backwards when
/// _backward) when it may: once the player has three kills, while it is hostile, does not hold its fire (CheckSafeZoneHoldFire),
/// has missiles and nothing blocks firing, at odds of _odds in 65536; one launched is one missile fewer.
void TryLaunchMissileAtPlayer(GameState& _state, ObjectSlot _slot, std::uint16_t _odds, bool _backward);

/// TryLaunchThargon (CS:5471): a Thargoid at _slot with Thargons left launches one (LaunchShipFromObject, the copy backwards
/// when _backward), at odds of 300 in 65536; one launched is one Thargon fewer.
void TryLaunchThargon(GameState& _state, ObjectSlot _slot, bool _backward);

/// UpdateMissileAi (CS:54F2): class 2: the missile at _missile spins, and flies at its target, or at the player, raising the
/// alert; when its target is gone it explodes. Within C8h of it on every axis it explodes (ExplodeObject) on it: the player
/// takes 320h damage (TakeDamage); a station is a crime of 5 on legalStatus, or in an invasion loses 10 of its energy and
/// explodes at the last; a ship is credited (CreditKill) and explodes unless indestructible. _backward is the direction flag the
/// explosions copy by; _bx and _dx the BX and DX it is called with, which an explosion passes on. Returns what it leaves in DI
/// and DX.
MissileFlight UpdateMissileAi(GameState& _state, Hardware& _hardware, ObjectSlot _missile, bool _backward, std::uint16_t _bx,
                              std::uint16_t _dx);

/// FindShipInCrosshairs (CS:8A46): the nearest of objectSlotCount - 2 slots from stationSlot that is active, drawn this frame,
/// not passed over (+1Eh bits 5 and 6 both set), and over the view's centre: |x| * 256 / z and |y| * 256 / z both below its
/// type's shipTargetRadius * 256 / z, plus 2. The divides overflow into the game's trap for a near or off-axis object.
[[nodiscard]] CrosshairTarget FindShipInCrosshairs(GameState& _state);

/// ResolveLaserFire (CS:8AC2): while laserFiring, a shot of the player's laser: the ship in the crosshairs (FindShipInCrosshairs)
/// is hit, marked hostile, and takes the laser's type + 1 in damage, added to its aggression up to FFh; a station's hit cancels
/// the docking computer and is a crime of 28h, or in an invasion does half damage; a ship that runs out of energy is destroyed
/// (CreditKill, CheckMissileTargetDestroyed, ExplodeObject, its copies backwards when _backward) unless indestructible, where a
/// station stays at 0 and falls only in an invasion. Then the beams (DrawLaserBeams), the laser's sound unless the target was
/// destroyed, and laserFiring cleared. Returns whether a beam was a horizontal line, which DrawLine fills by REP STOSB.
bool ResolveLaserFire(GameState& _state, Hardware& _hardware, bool _backward);

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

/// ApplyEnemyLaserHit (CS:8C8E): a pending hit on the player (playerHitPending): StartPlayerHitSound and its STI; when the attacker
/// at playerHitBy was drawn this frame, its beam in colour 3 from where it is on screen (ProjectToScreen) to a random point on an
/// edge of the view (DrawClippedLine); then 15 off the fore shield, or the aft while the attacker is behind (playerHitByDepth
/// bit 7), written back and 0 over it on a borrow, and what it could not take off playerEnergy likewise, where a borrow sets
/// playerDead. Returns whether the beam was a horizontal line, which DrawLine fills by REP STOSB.
bool ApplyEnemyLaserHit(GameState& _state, Hardware& _hardware);

/// UseMaskingDevice (CS:8ECF): 12 off the energy, at least 0, the background blue, and for each of objectSlotCount slots from
/// shipSlots, its state and hostility cleared and 2 off its aggression, at least 0.
void UseMaskingDevice(GameState& _state);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

/// The registers RemoveAllMissiles' original leaves, _erased saying whether it erased a scanner blip: DI past the slots it looked
/// at, and ES = B800h once it erased one. For its entry, and for UpdateStationAi's, whose contract compares them.
void RemoveAllMissilesOut(Guest& _guest, bool _erased);

void DrawLaserSightsEntry(Guest& _guest);      ///< AX, BX, CX, SI, DI clobbered.
void GetViewLaserEntry(Guest& _guest);         ///< Out: CF set and AL = the type when the mount has a laser; AX, BX, CX clobbered.
void DrawLaserBeamsEntry(Guest& _guest);       ///< Out: ES = DS once a beam was horizontal; AX-DX, BP, DI clobbered.
void TakeDamageEntry(Guest& _guest);           ///< AX = the damage. Out: AX and BX as the original leaves them; IF=1 when it kills.
void SpawnPlayerWreckageEntry(Guest& _guest);  ///< Clobbers all but DS.
void KillPlayerEntry(Guest& _guest);           ///< Out: AL the sound's step length and IF=1, when it starts.
void InitMissileEntry(Guest& _guest);          ///< DI = the slot. AX, BX clobbered.
void RemoveAllMissilesEntry(Guest& _guest);    ///< Out: DI past the slots, ES = B800h once a blip is erased; AX-DX clobbered.
void TallyMaskMissionKillEntry(Guest& _guest); ///< DI = the slot. Out: AL = its type while the mission runs.
void TryFireLaserAtPlayerEntry(Guest& _guest); ///< DI = the slot, AX, BX = the aim errors. AX, BX, CX, DX clobbered.
void LaunchPlayerMissileEntry(Guest& _guest);  ///< DI = the 64 bytes to copy. Clobbers all but DS.
/// DI = the launcher, DL = what to launch. Out: CF set when launched, DI kept; a Krait returns to CS:DI. AX-DX, SI, BP, ES clobbered.
void LaunchShipFromObjectEntry(Guest& _guest);
void TryLaunchMissileAtPlayerEntry(Guest& _guest);    ///< DI = the slot, BX = the odds. Out: DI kept; AX-DX, SI, BP, ES clobbered.
void TryLaunchThargonEntry(Guest& _guest);            ///< DI = the slot. Out: DI kept; AX-DX, SI, BP, ES clobbered.
void CheckMissileTargetDestroyedEntry(Guest& _guest); ///< DI = the slot. Out: AX = the message, when unlocked.
void CreditKillEntry(Guest& _guest);                  ///< DI = the slot. Out: AX, BX, CX and SI as the original leaves them.
void Routine8C51Entry(Guest& _guest);                 ///< DI = the slot. Out: AL = its type; ZF for either, CF for the Thargoid.
void UseMaskingDeviceEntry(Guest& _guest);            ///< Out: SI past the slots, CX = 0.
void DetonateEnergyBombEntry(Guest& _guest);          ///< Clobbers all.
void ExplodeObjectEntry(Guest& _guest);               ///< DI = the object. Clobbers all.
/// DI = the missile. Out: DI and DX as the original leaves them, which UpdateObjectsAndSpawn goes on with; AX, BX, CX, SI, BP and
/// ES clobbered.
void UpdateMissileAiEntry(Guest& _guest);
/// Out: CF set and DI = the slot when one is in the crosshairs, else DI past the slots. AX, BX, CX, DX, SI, BP clobbered.
void FindShipInCrosshairsEntry(Guest& _guest);
void ResolveLaserFireEntry(Guest& _guest);   ///< Out: DF clear once a beam was horizontal. Clobbers all.
void ApplyEnemyLaserHitEntry(Guest& _guest); ///< Out: DF clear once the beam was horizontal; IF=1 after a hit. Clobbers all but DS.

} // namespace Elite
