// GameLogic/Ai.h
#pragma once

#include "Flight.h"
#include "GameState.h"
#include "Maths.h"
#include "NativeEntry.h"
#include "ObjectSlot.h"
#include "Ships.h"

#include <cstdint>
#include <optional>
#include <span>

namespace Elite
{

// The reference's ai routines, ported (plan §5 Phase 3, ADR-010): the ships' behaviour: traders, police, pirates, the station.
// Each body is declared here once it is ported, on the registers of its contract in Symbols.tsv; those de-assembled (ADR-012)
// take values and give values back, below the register bodies, and their entries keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> AiEntries() noexcept;

/// UpdateObjectsAndSpawn (CS:4A10): every active slot counted and run by its class's handler, then new ships spawned.
void UpdateObjectsAndSpawn(Guest& _guest);

/// SkipInertObjectAi (CS:5594): class 0's handler, which does nothing.
void SkipInertObjectAi(Guest& _guest);

/// UpdateStationAi (CS:5595): class 1: the station spins, launches police or traders at an offender, and answers missiles.
void UpdateStationAi(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──

/// ClampTurnStep's step and the error it was taken from.
struct TurnStep
{
  std::int16_t step;            ///< the error, or the turn rate with its sign when the error is not below that
  std::uint16_t errorMagnitude; ///< |error|
};

/// TurnTowardAngles' two steps: the pitch's, then the yaw's.
struct Turn
{
  TurnStep pitch;
  TurnStep yaw;
};

/// GetVectorToObject's vector, and whether it is within the box.
struct VectorToObject
{
  Vector vector;
  bool within;
};

/// What CountOtherHuntersOnScanner counts.
struct HunterCount
{
  std::uint8_t count;                ///< as a byte, wrapping
  std::optional<std::uint16_t> last; ///< the last slot counted, if any
};

/// What CheckSafeZoneHoldFire finds.
struct HoldFire
{
  bool hold;                    ///< the ship must not fire
  std::optional<SafeZone> zone; ///< what InSafeZone read, when it was asked: neither in an invasion nor for a police Viper
};

/// What UpdateDriftingObjectAi did with a slot.
struct DriftingObject
{
  MovedObject moved;          ///< what MoveObject did
  std::optional<Pair> tumble; ///< for a rock, what it added to the pitch, then to the roll
};

/// ScaleSpawnOdds (CS:4C0E): _odds multiplied by 32 (shifted left 5) while jumpDriveEngaged is 1.
[[nodiscard]] std::uint16_t ScaleSpawnOdds(const GameState& _state, std::uint16_t _odds);

/// IsMaskShipPresent (CS:4C20): the first of objectSlotCount slots from shipSlots, active or not, that carries the masking
/// device (+1Eh bit 5).
[[nodiscard]] SlotSearch IsMaskShipPresent(GameState& _state);

/// PlaceEscortNear (CS:4C38): _escort given the first 16 bytes of the leader at _leader, byte by byte, then moved a random
/// -1024..1023 on each axis.
void PlaceEscortNear(GameState& _state, ObjectSlot _escort, std::uint16_t _leader);

/// TurnTowardAngles (CS:514B): _slot's pitch, then its yaw, turned toward _wanted by at most its turn rate (ClampTurnStep).
Turn TurnTowardAngles(ObjectSlot _slot, Angles _wanted);

/// ClampTurnStep (CS:5166): the step from angle _current toward angle _wanted within _slot's turn rate. The error is
/// (_wanted & 7FFh) - (_current & 7FFh), not wrapped at 2048, so a turn across 0 goes the long way.
[[nodiscard]] TurnStep ClampTurnStep(const ObjectSlot& _slot, std::uint16_t _wanted, std::uint16_t _current);

/// CountOtherHuntersOnScanner (CS:548F): the class-6 ships among objectSlotCount slots from shipSlots with their blips drawn,
/// other than the one at _slot.
[[nodiscard]] HunterCount CountOtherHuntersOnScanner(GameState& _state, std::uint16_t _slot);

/// GetVectorToObject (CS:54B4): (_object's position - _slot's) / 4, each coordinate quartered first, and whether it is within
/// _halfSize / 4 on every axis (VectorWithinBox).
[[nodiscard]] VectorToObject GetVectorToObject(const ObjectSlot& _slot, const ObjectSlot& _object, std::uint16_t _halfSize);

/// UpdateDriftingObjectAi (CS:5681): class 3: _slot moved (MoveObject), and a rock (IsDebrisType) tumbled: 37h added to its
/// pitch and FFDFh to its roll when bit 1 of its type byte is set, the other way round when it is clear.
DriftingObject UpdateDriftingObjectAi(GameState& _state, ObjectSlot _slot);

/// UpdateTraderOrPoliceAi (CS:569C): class 4: traders and the police at _slot: deciding, attacking, fleeing with jinks, breaking
/// off; rocks only spin. _rangeLow is DL, the low byte of the box a ship breaking off measures its range by; _backward the
/// direction flag a launch's CopyObject runs by. Every path ends with MoveObject: returns what it did.
MovedObject UpdateTraderOrPoliceAi(GameState& _state, ObjectSlot _slot, std::uint8_t _rangeLow, bool _backward);

/// UpdateWolfAi (CS:57E8): class 5: attack passes at the player, a Thargoid launching Thargons and setting off its ECM now and
/// then; _rangeLow and _backward as UpdateTraderOrPoliceAi's, for turning away. Returns what its MoveObject did.
MovedObject UpdateWolfAi(GameState& _state, ObjectSlot _slot, std::uint8_t _rangeLow, bool _backward);

/// UpdateHunterAi (CS:58DE): class 6: pack hunting; _rangeLow and _backward as UpdateTraderOrPoliceAi's, for evading. Returns
/// what its MoveObject did.
MovedObject UpdateHunterAi(GameState& _state, ObjectSlot _slot, std::uint8_t _rangeLow, bool _backward);

/// CheckSafeZoneHoldFire (CS:5A10): whether _slot must hold its fire: while the player is in the station's safe zone
/// (InSafeZone), unless a Thargoid invasion is on or _slot is a police Viper.
[[nodiscard]] HoldFire CheckSafeZoneHoldFire(const GameState& _state, const ObjectSlot& _slot);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void ScaleSpawnOddsEntry(Guest& _guest);    ///< BX = the odds, in and out.
void IsMaskShipPresentEntry(Guest& _guest); ///< Out: CF set and SI = the slot when found, else SI past the slots; CX clobbered.
void PlaceEscortNearEntry(Guest& _guest);   ///< DI = the escort, SI = the leader. AX, CX, DX, SI clobbered.
/// AX, BX = the angles wanted, DI = the slot. Out: AX, BX the errors' magnitudes, BP = AX, CX the turn rate, negated when it is
/// the yaw's step; DX clobbered.
void TurnTowardAnglesEntry(Guest& _guest);
/// AX = wanted, CX = current, DI = the slot. Out: AX the step, BX |error|, CX the turn rate, negated when the step is.
void ClampTurnStepEntry(Guest& _guest);
void CountOtherHuntersOnScannerEntry(Guest& _guest); ///< DI = the slot. Out: AL the count, BP the last found; CX, SI clobbered.
/// DI = the slot, SI = the object, DX = the box's half size. Out: AX, BX, CX the vector; CF set within the box; DX, BP clobbered.
void GetVectorToObjectEntry(Guest& _guest);
/// DI = the slot, DL the low byte of a range's box. Out: DI kept; AX, BX, CX, DX, SI, BP and ES clobbered.
void UpdateTraderOrPoliceAiEntry(Guest& _guest);
void UpdateWolfAiEntry(Guest& _guest);   ///< As UpdateTraderOrPoliceAiEntry.
void UpdateHunterAiEntry(Guest& _guest); ///< As UpdateTraderOrPoliceAiEntry.
/// DI = the slot. Out: every register as the original leaves it: MoveObject's, AL the type, and a rock's tumble in AX and BX.
void UpdateDriftingObjectAiEntry(Guest& _guest);
/// DI = the slot. Out: CF set when it must hold its fire; AL what InSafeZone leaves there, when it is asked.
void CheckSafeZoneHoldFireEntry(Guest& _guest);

} // namespace Elite
