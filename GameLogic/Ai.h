// GameLogic/Ai.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "Maths.h"
#include "SpaceObject.h"
#include "Ships.h"

#include <cstdint>
#include <optional>

namespace Elite
{

// The reference's ai routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the ships' behaviour: traders, police,
// pirates, the station. Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines (ADR-012): values in, values out, on the GameState ──

/// UpdateObjectsAndSpawn (CS:4A10): once a frame, activeObjectCount and the class counts cleared (REP STOSB, upwards: the direction flag is
/// clear wherever the game calls it, change 3c), then every active slot of shipSlotCount counted, debris apart, and run by its class's
/// handler; maskingBackgroundColor cleared; then, with fewer than 10 active objects and no more than objectSlotCount, the mask mission's
/// ships, an invasion's Thargoids, or the government's spawns.
void UpdateObjectsAndSpawn(GameState& _state, Hardware& _hardware);

/// UpdateStationAi (CS:5595): class 1: the station at _station spins; while it is hostile and the government is no anarchy, it looks for an
/// offender out of the box it guards and launches police, shuttles or traders at one (copies of itself); then it answers missiles aimed at
/// it, or at the police in the safe zone, with its ECM.
void UpdateStationAi(GameState& _state, SpaceObject& _station);

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
  std::uint8_t count;            ///< as a byte, wrapping
  std::optional<SlotIndex> last; ///< the last slot counted, if any
};

/// ScaleSpawnOdds (CS:4C0E): _odds multiplied by 32 (shifted left 5) while jumpDriveEngaged is 1.
[[nodiscard]] std::uint16_t ScaleSpawnOdds(const GameState& _state, std::uint16_t _odds);

/// IsMaskShipPresent (CS:4C20): the first of objectSlotCount slots from shipSlots, active or not, that carries the masking
/// device (+1Eh bit 5).
[[nodiscard]] SlotSearch IsMaskShipPresent(GameState& _state);

/// PlaceEscortNear (CS:4C38): _escort given the first 16 bytes of _leader, byte by byte, then moved a random -1024..1023 on
/// each axis.
void PlaceEscortNear(GameState& _state, SpaceObject& _escort, const SpaceObject& _leader);

/// TurnTowardAngles (CS:514B): _slot's pitch, then its yaw, turned toward _wanted by at most its turn rate (ClampTurnStep).
Turn TurnTowardAngles(SpaceObject& _slot, Angles _wanted);

/// ClampTurnStep (CS:5166): the step from angle _current toward angle _wanted within _slot's turn rate. The error is
/// (_wanted & 7FFh) - (_current & 7FFh), not wrapped at 2048, so a turn across 0 goes the long way.
[[nodiscard]] TurnStep ClampTurnStep(const SpaceObject& _slot, std::uint16_t _wanted, std::uint16_t _current);

/// CountOtherHuntersOnScanner (CS:548F): the class-6 ships among objectSlotCount slots from shipSlots with their blips drawn,
/// other than the one at _slot.
[[nodiscard]] HunterCount CountOtherHuntersOnScanner(GameState& _state, const SpaceObject& _slot);

/// GetVectorToObject (CS:54B4): (_object's position - _slot's) / 4, each coordinate quartered first, and whether it is within
/// _halfSize / 4 on every axis (VectorWithinBox).
[[nodiscard]] VectorToObject GetVectorToObject(const SpaceObject& _slot, const SpaceObject& _object, std::uint16_t _halfSize);

/// UpdateDriftingObjectAi (CS:5681): class 3: _slot moved (MoveObject), and a rock (IsDebrisType) tumbled: 37h added to its
/// pitch and FFDFh to its roll when bit 1 of its type byte is set, the other way round when it is clear. Returns what MoveObject
/// did.
MovedObject UpdateDriftingObjectAi(GameState& _state, SpaceObject& _slot);

/// UpdateTraderOrPoliceAi (CS:569C): class 4: traders and the police at _slot: deciding, attacking, fleeing with jinks, breaking off; rocks
/// only spin. A ship breaking off measures its range by a box of its range byte in 256ths (ADR-046). Every path ends with MoveObject:
/// returns what it did.
MovedObject UpdateTraderOrPoliceAi(GameState& _state, SpaceObject& _slot);

/// UpdateWolfAi (CS:57E8): class 5: attack passes at the player, a Thargoid launching Thargons and setting off its ECM now and then;
/// its range measured as UpdateTraderOrPoliceAi's, for turning away. Returns what its MoveObject did.
MovedObject UpdateWolfAi(GameState& _state, SpaceObject& _slot);

/// UpdateHunterAi (CS:58DE): class 6: pack hunting; its range measured as UpdateTraderOrPoliceAi's, for evading. Returns what its
/// MoveObject did.
MovedObject UpdateHunterAi(GameState& _state, SpaceObject& _slot);

/// CheckSafeZoneHoldFire (CS:5A10): whether _slot must hold its fire: while the player is in the station's safe zone
/// (InSafeZone), unless a Thargoid invasion is on or _slot is a police Viper.
[[nodiscard]] bool CheckSafeZoneHoldFire(const GameState& _state, const SpaceObject& _slot);

} // namespace Elite
