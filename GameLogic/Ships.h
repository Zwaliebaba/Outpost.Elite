// GameLogic/Ships.h
#pragma once

#include "Flight.h"
#include "GameState.h"
#include "Maths.h"
#include "SpaceObject.h"

#include <cstdint>
#include <optional>

namespace Elite
{

// The reference's ships routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the ship slots, blueprints and their vertex
// programs. Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// The size of shipSlots' object record, SpaceObject.
inline constexpr std::uint16_t SLOT_BYTES = 0x40;

// A slot's type byte: the active bit, and the types the ported routines name (bits 1-5).
inline constexpr std::uint8_t SLOT_ACTIVE = 0x01;
inline constexpr std::uint8_t TYPE_MASK = 0x1F;
inline constexpr std::uint8_t TYPE_DODO = 0x00;
inline constexpr std::uint8_t TYPE_CORIOLIS = 0x01;
inline constexpr std::uint8_t TYPE_ASTEROID = 0x05;
inline constexpr std::uint8_t TYPE_BOULDER = 0x06;
inline constexpr std::uint8_t TYPE_THARGON = 0x07;
inline constexpr std::uint8_t TYPE_SPLINTER = 0x0B;
inline constexpr std::uint8_t TYPE_PLATE = 0x0C;
inline constexpr std::uint8_t TYPE_MISSILE = 0x14;
inline constexpr std::uint8_t TYPE_THARGOID = 0x16;
inline constexpr std::uint8_t TYPE_ASP = 0x18;
inline constexpr std::uint8_t TYPE_VIPER = 0x1C;
inline constexpr std::uint8_t TYPE_SUN = 0x1E;
inline constexpr std::uint8_t TYPE_PLANET = 0x1F;

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// A slot that a routine reads, writes, copies or clears is a SpaceObject; one it searches for is its number, a SlotIndex
// (GameState::Slot gives the record), where the original holds its offset.

/// Where a search of the object slots stopped.
struct SlotSearch
{
  bool found;     ///< a slot was found
  SlotIndex slot; ///< it; when none was, the slot past the last one looked at, where the original's search leaves off
};

/// What IsObjectNear finds of a slot.
struct NearTest
{
  bool nearby;                              ///< each 24-bit coordinate fits a signed word
  std::optional<DashboardPixel> erasedBlip; ///< when one does not, the last pixel of the blip EraseScannerBlip erased, if it erased one
  std::uint8_t lastHigh;                    ///< the last high byte the test looked at, stepped up and back down, which leaves 0 for FFh
};

/// What IsStation finds of a slot.
struct StationTest
{
  bool station; ///< a Dodo (type 0) or a Coriolis (type 1)
  bool dodo;
};

/// What MoveObject did once it had moved a slot.
struct MovedObject
{
  NearTest near;                             ///< what IsObjectNear found of it
  std::optional<DashboardPixel> removedBlip; ///< when it was not near, it was removed: what RemoveObject erased of its blip
};

/// What ComputeVelocity gives.
struct Velocity
{
  Vector words; ///< the velocity as the rotations give it, in words; the slot keeps their low bytes
};

/// The slot ReclaimShipSlot found.
struct ReclaimedSlot
{
  SlotIndex slot; ///< it, which the original leaves for its caller
  bool evicted;   ///< every ship slot had a blip, so this one was removed to make room
};

/// _value, sign-extended to 24 bits, added to the 24-bit coordinate _axis (0 x, 1 y, 2 z) of _slot: the low word, then the high
/// byte with the carry. Not an entry: the idiom several routines share.
void AddToCoordinate(SpaceObject& _slot, int _axis, std::int16_t _value);

/// ClearObjectSlot: the 64 bytes of _slot zeroed, byte by byte.
void ClearObjectSlot(SpaceObject& _slot);

/// IsObjectNear: whether each 24-bit coordinate of _slot fits a signed word; when one does not, EraseScannerBlip.
NearTest IsObjectNear(SpaceObject& _slot);

/// IsSunOrPlanet: whether _slot is the sun (type 1Eh) or the planet (1Fh).
[[nodiscard]] bool IsSunOrPlanet(const SpaceObject& _slot);

/// IsPlanet: whether _slot is the planet (type 1Fh).
[[nodiscard]] bool IsPlanet(const SpaceObject& _slot);

/// IsStation: whether _slot is a station, and whether a Dodo.
[[nodiscard]] StationTest IsStation(const SpaceObject& _slot);

/// IsObjectNearKeepBlip: IsObjectNear's test alone: whether each 24-bit coordinate of _slot fits a signed word.
[[nodiscard]] bool IsObjectNearKeepBlip(const SpaceObject& _slot);

/// InitPoliceViper: _slot made a Viper (spawnTemplates entry 14), class 4, police (+3Ah = 1), aggression 64h.
void InitPoliceViper(SpaceObject& _slot);

/// InitCargoBarrel: _slot made a barrel (spawnTemplates entry 3), class 3.
void InitCargoBarrel(SpaceObject& _slot);

/// InitAbandonedCobra: _slot made a Cobra (entry 9) of class 0: the ship an escape pod leaves.
void InitAbandonedCobra(SpaceObject& _slot);

/// InitShuttle: _slot made a shuttle (entry 7), class 3.
void InitShuttle(SpaceObject& _slot);

/// InitThargon: _slot made a Thargon (entry 28), class 5.
void InitThargon(SpaceObject& _slot);

/// SpawnRandomDrifter: _slot, a free one, made one of the eight drifters from spawnTemplates' entry 1 by a random
/// word rotated right and its two bytes XORed, placed at the spawn point and turned to the player, of class 3 with turn rate
/// 1Eh, and its velocity set.
void SpawnRandomDrifter(GameState& _state, SpaceObject& _slot);

/// SpawnRandomTrader: _slot, a free one, made one of the six traders from spawnTemplates' entry 9 by a random byte /
/// 43, placed and turned as SpawnRandomDrifter does, of class 4, and its velocity set. A Viper gets a random bit for +3Ah,
/// police when 1, and then a word write of legalStatus at +30h, which zeroes the bounty above it.
void SpawnRandomTrader(GameState& _state, SpaceObject& _slot);

/// SpawnMaskMissionShip: _slot, a free one, from the Asp's record, placed and turned as SpawnRandomDrifter does: the
/// Asp when _maskShip, otherwise type 12h or 13h by the sign of a random word; class 5, a random aggression below 80h, energy
/// 96h, no cargo, 6 missiles and a bounty of C8h.
void SpawnMaskMissionShip(GameState& _state, SpaceObject& _slot, bool _maskShip);

/// SpawnInvasionThargoid: _slot, a free one, made the Thargoid of an invasion (spawnTemplates entry 27), placed and
/// turned as SpawnRandomDrifter does; class 5, a random aggression below 80h, energy 32h, no cargo, 6 missiles and 8
/// Thargons.
void SpawnInvasionThargoid(GameState& _state, SpaceObject& _slot);

/// SpawnRandomHunter: _slot, a free one, made one of the seven hunters from spawnTemplates' entry 15 by a random
/// byte / 37, placed at the spawn point and turned to the player, of class 6 with a random aggression below 20h, and its velocity
/// set.
void SpawnRandomHunter(GameState& _state, SpaceObject& _slot);

/// SpawnRandomWolf: _slot, a free one, made one of the five wolves from spawnTemplates' entry 22 by a random byte / 52,
/// or in witch space the sixth, the Thargoid; placed and turned as SpawnRandomHunter does, of class 5 with a random aggression
/// below 40h, 20h more in an anarchy, its velocity set, and a Thargoid given 2 to 5 Thargons.
void SpawnRandomWolf(GameState& _state, SpaceObject& _slot);

/// InitObjectFromTemplate: _slot from the 10-byte record _index of spawnTemplates from _first, the first of a group of
/// them, whose address the original holds: its state, flags and
/// aggression cleared, its scanned byte 1, its type active, then the record's nine fields.
void InitObjectFromTemplate(SpaceObject& _slot, std::size_t _first, std::uint8_t _index);

/// PlaceAtSpawnPoint: _slot placed at a random point 10000 out, by rotation pairs 7 and 6: (a scatter of +-1023,
/// 10000) turned by pair 7, then (another scatter, what that left of the 10000) by pair 6. Its y, x and z, in that order.
void PlaceAtSpawnPoint(GameState& _state, SpaceObject& _slot);

/// FacePlayerWithRandomRoll: FacePlayer, then a random word for _slot's roll.
void FacePlayerWithRandomRoll(GameState& _state, SpaceObject& _slot);

/// GetObjectPosition: the low words of _slot's position.
[[nodiscard]] Vector GetObjectPosition(const SpaceObject& _slot);

/// GetVectorToPlayer: the low words of _slot's position, negated.
[[nodiscard]] Vector GetVectorToPlayer(const SpaceObject& _slot);

/// ComputeVelocity: _slot's velocity bytes from its speed and heading: the pitch into rotation pair 7 and the yaw
/// into pair 6, then (0, speed) turned by pair 6 and (0, what that left) by pair 7. Returns the velocity as those rotations
/// give it, in words, of which the slot keeps the low bytes.
Velocity ComputeVelocity(GameState& _state, SpaceObject& _slot);

/// MoveObject: _slot's velocity bytes, sign-extended, added to its 24-bit position, x, y then z, each to the low word and
/// then, with its carry, to the high byte; then IsObjectNear, and RemoveObject when it is not near.
MovedObject MoveObject(SpaceObject& _slot);

/// RemoveObject: _slot's active bit cleared, then EraseScannerBlip. Returns the blip's last pixel, when it erased one.
std::optional<DashboardPixel> RemoveObject(SpaceObject& _slot);

/// FacePlayer: _slot's heading turned to the player: ConvertVectorToAngles of GetVectorToPlayer, as its pitch, then
/// its yaw.
void FacePlayer(GameState& _state, SpaceObject& _slot);

/// FindFreeShipSlot: the first inactive ship slot, from firstShipSlot.
[[nodiscard]] SlotSearch FindFreeShipSlot(GameState& _state);

/// ReclaimShipSlot: the first ship slot from firstShipSlot whose blip is not drawn; when every one has a blip, one of
/// slots 4-19 at random, removed (RemoveObject) to make room.
ReclaimedSlot ReclaimShipSlot(GameState& _state);

/// ClearAllObjects: shipSlotCount slots from shipSlots zeroed, a word at a time, upwards: a string instruction runs forwards
/// wherever the game calls it (change 3c).
void ClearAllObjects(GameState& _state);

/// FindDebrisSlot: the first inactive debris slot, or, when every one is active, the oldest, the last of equals.
[[nodiscard]] SlotIndex FindDebrisSlot(GameState& _state);

/// CopyObject: _source's 64 bytes copied over _destination's a word at a time, upwards: a string instruction runs forwards
/// wherever the game calls it (change 3c).
void CopyObject(const SpaceObject& _source, SpaceObject& _destination);

/// UpdateDebrisAi: a fragment's frame: its lifetime counted down, and at 0 its active bit cleared; otherwise its age
/// counted up, its spin bytes, sign-extended, added to its roll and then its pitch, and MoveObject. Returns what MoveObject
/// did, when it ran.
std::optional<MovedObject> UpdateDebrisAi(SpaceObject& _slot);

/// IsDebrisType: whether _slot is a rock: a plate (0Ch), a boulder (6), an asteroid (5) or a splinter (0Bh).
[[nodiscard]] bool IsDebrisType(const SpaceObject& _slot);

/// IsViperType: whether _slot is a Viper (type 1Ch).
[[nodiscard]] bool IsViperType(const SpaceObject& _slot);

/// IsPoliceViper: whether _slot is a Viper with word +3Ah = 1.
[[nodiscard]] bool IsPoliceViper(const SpaceObject& _slot);

/// IsThargoidType: whether _slot is a Thargoid (type 16h).
[[nodiscard]] bool IsThargoidType(const SpaceObject& _slot);

/// IsThargonType: whether _slot is a Thargon (type 7).
[[nodiscard]] bool IsThargonType(const SpaceObject& _slot);

} // namespace Elite
