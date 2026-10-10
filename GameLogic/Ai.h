// GameLogic/Ai.h
#pragma once

#include "GameState.h"
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

/// PlaceEscortNear (CS:4C38): the escort at DI given the first 16 bytes of the leader at SI, then moved a random -1024..1023 on
/// each axis.
void PlaceEscortNear(Guest& _guest);

/// TurnTowardAngles (CS:514B): the heading of the slot at DI turned toward AX, BX by at most its turn rate. Out: AX, BX the
/// errors' magnitudes.
void TurnTowardAngles(Guest& _guest);

/// GetVectorToObject (CS:54B4): AX, BX, CX = (SI's position - DI's) / 4, each quartered first; CF set when all are within DX / 4.
void GetVectorToObject(Guest& _guest);

/// SkipInertObjectAi (CS:5594): class 0's handler, which does nothing.
void SkipInertObjectAi(Guest& _guest);

/// UpdateStationAi (CS:5595): class 1: the station spins, launches police or traders at an offender, and answers missiles.
void UpdateStationAi(Guest& _guest);

/// UpdateDriftingObjectAi (CS:5681): class 3: moves, and tumbles if a rock.
void UpdateDriftingObjectAi(Guest& _guest);

/// UpdateTraderOrPoliceAi (CS:569C): class 4: traders and the police: deciding, attacking, fleeing with jinks, breaking off; rocks
/// only spin.
void UpdateTraderOrPoliceAi(Guest& _guest);

/// UpdateWolfAi (CS:57E8): class 5: attack passes at the player.
void UpdateWolfAi(Guest& _guest);

/// UpdateHunterAi (CS:58DE): class 6: pack hunting.
void UpdateHunterAi(Guest& _guest);

/// CheckSafeZoneHoldFire (CS:5A10): CF set when the ship at DI must not fire.
void CheckSafeZoneHoldFire(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──

/// ClampTurnStep's step and the error it was taken from.
struct TurnStep
{
  std::int16_t step;            ///< the error, or the turn rate with its sign when the error is not below that
  std::uint16_t errorMagnitude; ///< |error|
};

/// What CountOtherHuntersOnScanner counts.
struct HunterCount
{
  std::uint8_t count;                ///< as a byte, wrapping
  std::optional<std::uint16_t> last; ///< the last slot counted, if any
};

/// ScaleSpawnOdds (CS:4C0E): _odds multiplied by 32 (shifted left 5) while jumpDriveEngaged is 1.
[[nodiscard]] std::uint16_t ScaleSpawnOdds(const GameState& _state, std::uint16_t _odds);

/// IsMaskShipPresent (CS:4C20): the first of objectSlotCount slots from shipSlots, active or not, that carries the masking
/// device (+1Eh bit 5).
[[nodiscard]] SlotSearch IsMaskShipPresent(GameState& _state);

/// ClampTurnStep (CS:5166): the step from angle _current toward angle _wanted within _slot's turn rate. The error is
/// (_wanted & 7FFh) - (_current & 7FFh), not wrapped at 2048, so a turn across 0 goes the long way.
[[nodiscard]] TurnStep ClampTurnStep(const ObjectSlot& _slot, std::uint16_t _wanted, std::uint16_t _current);

/// CountOtherHuntersOnScanner (CS:548F): the class-6 ships among objectSlotCount slots from shipSlots with their blips drawn,
/// other than the one at _slot.
[[nodiscard]] HunterCount CountOtherHuntersOnScanner(GameState& _state, std::uint16_t _slot);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void ScaleSpawnOddsEntry(Guest& _guest);    ///< BX = the odds, in and out.
void IsMaskShipPresentEntry(Guest& _guest); ///< Out: CF set and SI = the slot when found, else SI past the slots; CX clobbered.
/// AX = wanted, CX = current, DI = the slot. Out: AX the step, BX |error|, CX the turn rate, negated when the step is.
void ClampTurnStepEntry(Guest& _guest);
void CountOtherHuntersOnScannerEntry(Guest& _guest); ///< DI = the slot. Out: AL the count, BP the last found; CX, SI clobbered.

} // namespace Elite
