// GameLogic/Ships.h
#pragma once

#include "Flight.h"
#include "GameState.h"
#include "Maths.h"
#include "ObjectSlot.h"

#include <cstdint>
#include <optional>

namespace Elite
{

// The reference's ships routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the ship slots, blueprints and their vertex
// programs. Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// The 64-byte object record of shipSlots: the fields the ported routines use, as offsets into it. Combat and Ai use them too.
inline constexpr std::uint16_t SLOT_BYTES = 0x40;
inline constexpr std::uint16_t SLOT_TYPE = 0x00;   ///< bit 0 active, bits 1-5 the type, bit 7 drawn this frame
inline constexpr std::uint16_t SLOT_X_HIGH = 0x01; ///< the high bytes of the 24-bit position: x, then y and z
inline constexpr std::uint16_t SLOT_X = 0x04;      ///< the low words of the position: x, then y (+6) and z (+8)
inline constexpr std::uint16_t SLOT_Y = 0x06;
inline constexpr std::uint16_t SLOT_Z = 0x08;
inline constexpr std::uint16_t SLOT_PITCH = 0x0A; ///< the heading, as ConvertVectorToAngles gives it: AX
inline constexpr std::uint16_t SLOT_YAW = 0x0C;   ///< and BX
inline constexpr std::uint16_t SLOT_ROLL = 0x0E;
inline constexpr std::uint16_t SLOT_VIEW_X = 0x10; ///< the position in the camera's frame: x, y (+12h), z (+14h)
inline constexpr std::uint16_t SLOT_VIEW_Y = 0x12;
inline constexpr std::uint16_t SLOT_VIEW_Z = 0x14;
inline constexpr std::uint16_t SLOT_PASSES = 0x16; ///< attack passes left
inline constexpr std::uint16_t SLOT_STATE = 0x17;  ///< the AI state
inline constexpr std::uint16_t SLOT_SPEED = 0x18;
inline constexpr std::uint16_t SLOT_VELOCITY = 0x19; ///< three signed bytes: x, y, z
inline constexpr std::uint16_t SLOT_RANGE = 0x1C;    ///< the high byte of the range a ship turns back at
inline constexpr std::uint16_t SLOT_TURN_RATE = 0x1D;
inline constexpr std::uint16_t SLOT_FLAGS = 0x1E;     ///< bit 0 hostile, 1 blip drawn, 2 indestructible, 5 carries the device
inline constexpr std::uint16_t SLOT_THARGONS = 0x1F;  ///< Thargons a Thargoid has left to launch
inline constexpr std::uint16_t SLOT_SPIN_ROLL = 0x26; ///< a fragment's spin, added to the roll and the pitch each frame
inline constexpr std::uint16_t SLOT_SPIN_PITCH = 0x27;
inline constexpr std::uint16_t SLOT_TARGET = 0x29; ///< word: a missile's target, a hunter's pack mate
inline constexpr std::uint16_t SLOT_ENERGY = 0x2B;
inline constexpr std::uint16_t SLOT_CARGO = 0x2C;
inline constexpr std::uint16_t SLOT_FRAGMENTS = 0x2D;
inline constexpr std::uint16_t SLOT_LIFETIME = 0x2E; ///< frames a fragment has left
inline constexpr std::uint16_t SLOT_AGE = 0x2F;
inline constexpr std::uint16_t SLOT_AGGRESSION = 0x30;
inline constexpr std::uint16_t SLOT_BOUNTY = 0x31;
inline constexpr std::uint16_t SLOT_MISSILES = 0x32;
inline constexpr std::uint16_t SLOT_CLASS = 0x33; ///< the behaviour class, behaviorHandlers' index
inline constexpr std::uint16_t SLOT_SCANNED = 0x34;
inline constexpr std::uint16_t SLOT_JINK_FRAMES = 0x35; ///< frames to the next evasive jink
inline constexpr std::uint16_t SLOT_JINK_PITCH = 0x36;  ///< words: the jink added to the wanted heading
inline constexpr std::uint16_t SLOT_JINK_YAW = 0x38;
inline constexpr std::uint16_t SLOT_OWNER = 0x3A;       ///< word: a Thargon's mother; 1 for a police Viper
inline constexpr std::uint16_t SLOT_VIEW_Z_HIGH = 0x3C; ///< the high byte of the camera-frame z: bit 7 set behind
inline constexpr std::uint16_t SLOT_DETAIL = 0x3F;      ///< the level-of-detail threshold, from the template

// SLOT_TYPE: the active bit, and the types the ported routines name (bits 1-5).
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
// A slot that a routine reads or writes by its fields is an ObjectSlot; one it searches for, copies or clears whole is the
// offset in the data segment the original holds in SI or DI.

/// Where a search of the object slots stopped.
struct SlotSearch
{
  bool found;         ///< a slot was found
  std::uint16_t slot; ///< its offset; when none was, the offset past the last one looked at, where the original leaves SI
};

/// What IsObjectNear finds of a slot.
struct NearTest
{
  bool nearby;                              ///< each 24-bit coordinate fits a signed word
  std::optional<DashboardPixel> erasedBlip; ///< when one does not, the last pixel of the blip EraseScannerBlip erased, if it erased one
  std::uint8_t lastHigh;                    ///< the last high byte the test looked at, INC'd and DEC'd back: what the original leaves in AL
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
  /// What the original leaves in DX: the z velocity's CWD, or the last blip erased, its row in the high byte and its column in
  /// the low. UpdateObjectsAndSpawn's next handler takes DL of it for its range's box.
  std::uint16_t dx;
};

/// What ComputeVelocity gives.
struct Velocity
{
  Vector words; ///< the velocity as the rotations give it, in words; the slot keeps their low bytes
  /// What the original leaves in DX: RotateBySinCos' last product, z turned by the pitch's sine, doubled and rounded.
  /// UpdateObjectsAndSpawn's next handler takes DL of it after the station launches a ship.
  std::uint16_t dx;
};

/// The slot ReclaimShipSlot found.
struct ReclaimedSlot
{
  std::uint16_t slot;                       ///< its offset, which the original leaves in SI
  bool evicted;                             ///< every ship slot had a blip, so this one was removed to make room
  std::optional<DashboardPixel> erasedBlip; ///< then, what RemoveObject erased of its blip
};

/// ADD [slot+4+2*axis],_value / ADC [slot+1+axis],DL with DL from CWD: _value added to the 24-bit coordinate _axis (0 x, 1 y,
/// 2 z) of _slot, the low word, then the high byte. Not an entry: the idiom several routines share.
void AddToCoordinate(ObjectSlot _slot, int _axis, std::int16_t _value);

/// ClearObjectSlot (CS:2FD8): the 64 bytes of the slot at _slot zeroed, byte by byte.
void ClearObjectSlot(GameState& _state, std::uint16_t _slot);

/// IsObjectNear (CS:3B9A): whether each 24-bit coordinate of _slot fits a signed word; when one does not, EraseScannerBlip.
NearTest IsObjectNear(GameState& _state, ObjectSlot _slot);

/// IsSunOrPlanet (CS:3F2A): whether _slot is the sun (type 1Eh) or the planet (1Fh).
[[nodiscard]] bool IsSunOrPlanet(const ObjectSlot& _slot);

/// IsPlanet (CS:3F37): whether _slot is the planet (type 1Fh).
[[nodiscard]] bool IsPlanet(const ObjectSlot& _slot);

/// IsStation (CS:3F40): whether _slot is a station, and whether a Dodo.
[[nodiscard]] StationTest IsStation(const ObjectSlot& _slot);

/// IsObjectNearKeepBlip (CS:460E): IsObjectNear's test alone: whether each 24-bit coordinate of _slot fits a signed word.
[[nodiscard]] bool IsObjectNearKeepBlip(const ObjectSlot& _slot);

/// InitPoliceViper (CS:4C76): _slot made a Viper (spawnTemplates entry 14), class 4, police (+3Ah = 1), aggression 64h.
void InitPoliceViper(GameState& _state, ObjectSlot _slot);

/// InitCargoBarrel (CS:4C99): _slot made a barrel (spawnTemplates entry 3), class 3.
void InitCargoBarrel(GameState& _state, ObjectSlot _slot);

/// InitAbandonedCobra (CS:4CA6): _slot made a Cobra (entry 9) of class 0: the ship an escape pod leaves.
void InitAbandonedCobra(GameState& _state, ObjectSlot _slot);

/// InitShuttle (CS:4CC0): _slot made a shuttle (entry 7), class 3.
void InitShuttle(GameState& _state, ObjectSlot _slot);

/// InitThargon (CS:4CDA): _slot made a Thargon (entry 28), class 5.
void InitThargon(GameState& _state, ObjectSlot _slot);

/// SpawnRandomDrifter (CS:4CE7): _slot, a free one, made one of the eight drifters from spawnTemplates' entry 1 by a random
/// word rotated right and its two bytes XORed, placed at the spawn point and turned to the player, of class 3 with turn rate
/// 1Eh, and its velocity set.
void SpawnRandomDrifter(GameState& _state, ObjectSlot _slot);

/// SpawnRandomTrader (CS:4D08): _slot, a free one, made one of the six traders from spawnTemplates' entry 9 by a random byte /
/// 43, placed and turned as SpawnRandomDrifter does, of class 4, and its velocity set. A Viper gets a random bit for +3Ah,
/// police when 1, and then a word write of legalStatus at +30h, which zeroes the bounty above it.
void SpawnRandomTrader(GameState& _state, ObjectSlot _slot);

/// SpawnMaskMissionShip (CS:4DAE): _slot, a free one, from the Asp's record, placed and turned as SpawnRandomDrifter does: the
/// Asp when _maskShip, otherwise type 12h or 13h by the sign of a random word; class 5, a random aggression below 80h, energy
/// 96h, no cargo, 6 missiles and a bounty of C8h.
void SpawnMaskMissionShip(GameState& _state, ObjectSlot _slot, bool _maskShip);

/// SpawnInvasionThargoid (CS:4DF0): _slot, a free one, made the Thargoid of an invasion (spawnTemplates entry 27), placed and
/// turned as SpawnRandomDrifter does; class 5, a random aggression below 80h, energy 32h, no cargo, 6 missiles and 8
/// Thargons.
void SpawnInvasionThargoid(GameState& _state, ObjectSlot _slot);

/// SpawnRandomHunter (CS:4D3B): _slot, a free one, made one of the seven hunters from spawnTemplates' entry 15 by a random
/// byte / 37, placed at the spawn point and turned to the player, of class 6 with a random aggression below 20h, and its velocity
/// set.
void SpawnRandomHunter(GameState& _state, ObjectSlot _slot);

/// SpawnRandomWolf (CS:4D60): _slot, a free one, made one of the five wolves from spawnTemplates' entry 22 by a random byte / 52,
/// or in witch space the sixth, the Thargoid; placed and turned as SpawnRandomHunter does, of class 5 with a random aggression
/// below 40h, 20h more in an anarchy, its velocity set, and a Thargoid given 2 to 5 Thargons.
void SpawnRandomWolf(GameState& _state, ObjectSlot _slot);

/// InitObjectFromTemplate (CS:4E1B): _slot from the 10-byte record _index of the table at _table: its state, flags and
/// aggression cleared, its scanned byte 1, its type active, then the record's nine fields.
void InitObjectFromTemplate(GameState& _state, ObjectSlot _slot, std::uint16_t _table, std::uint8_t _index);

/// PlaceAtSpawnPoint (CS:4E75): _slot placed at a random point 10000 out, by rotation pairs 7 and 6: (a scatter of +-1023,
/// 10000) turned by pair 7, then (another scatter, what that left of the 10000) by pair 6. Its y, x and z, in that order.
void PlaceAtSpawnPoint(GameState& _state, ObjectSlot _slot);

/// FacePlayerWithRandomRoll (CS:4EC5): FacePlayer, then a random word for _slot's roll.
void FacePlayerWithRandomRoll(GameState& _state, ObjectSlot _slot);

/// GetObjectPosition (CS:4EF4): the low words of _slot's position.
[[nodiscard]] Vector GetObjectPosition(const ObjectSlot& _slot);

/// GetVectorToPlayer (CS:4EFE): the low words of _slot's position, negated.
[[nodiscard]] Vector GetVectorToPlayer(const ObjectSlot& _slot);

/// ComputeVelocity (CS:4F48): _slot's velocity bytes from its speed and heading: the pitch into rotation pair 7 and the yaw
/// into pair 6, then (0, speed) turned by pair 6 and (0, what that left) by pair 7. Returns the velocity as those rotations
/// give it, in words, of which the slot keeps the low bytes, and what the second rotation leaves in DX.
Velocity ComputeVelocity(GameState& _state, ObjectSlot _slot);

/// MoveObject (CS:4F6E): _slot's velocity bytes, sign-extended, added to its 24-bit position, x, y then z, each as ADD on the
/// low word and ADC on the high byte; then IsObjectNear, and RemoveObject when it is not near.
MovedObject MoveObject(GameState& _state, ObjectSlot _slot);

/// RemoveObject (CS:4F98): _slot's active bit cleared, then EraseScannerBlip. Returns the blip's last pixel, when it erased one.
std::optional<DashboardPixel> RemoveObject(GameState& _state, ObjectSlot _slot);

/// FacePlayer (CS:513E): _slot's heading turned to the player: ConvertVectorToAngles of GetVectorToPlayer, as its pitch, then
/// its yaw.
void FacePlayer(GameState& _state, ObjectSlot _slot);

/// FindFreeShipSlot (CS:51E0): the first inactive ship slot, from firstShipSlot.
[[nodiscard]] SlotSearch FindFreeShipSlot(GameState& _state);

/// ReclaimShipSlot (CS:51FD): the first ship slot from firstShipSlot whose blip is not drawn; when every one has a blip, one of
/// slots 4-19 at random, removed (RemoveObject) to make room.
ReclaimedSlot ReclaimShipSlot(GameState& _state);

/// ClearAllObjects (CS:52B2): shipSlotCount slots from shipSlots zeroed, a word at a time as REP STOSW does, backwards when
/// _backward (the direction flag) is set.
void ClearAllObjects(GameState& _state, bool _backward);

/// FindDebrisSlot (CS:52EC): the first inactive debris slot, or, when every one is active, the oldest, the last of equals.
[[nodiscard]] std::uint16_t FindDebrisSlot(GameState& _state);

/// CopyObject (CS:5320): the 64 bytes at _source copied to _destination a word at a time as REP MOVSW does, backwards when
/// _backward (the direction flag) is set.
void CopyObject(GameState& _state, std::uint16_t _source, std::uint16_t _destination, bool _backward);

/// UpdateDebrisAi (CS:5330): a fragment's frame: its lifetime counted down, and at 0 its active bit cleared; otherwise its age
/// counted up, its spin bytes, sign-extended, added to its roll and then its pitch, and MoveObject. Returns what MoveObject
/// did, when it ran.
std::optional<MovedObject> UpdateDebrisAi(GameState& _state, ObjectSlot _slot);

/// IsDebrisType (CS:53FE): whether _slot is a rock: a plate (0Ch), a boulder (6), an asteroid (5) or a splinter (0Bh).
[[nodiscard]] bool IsDebrisType(const ObjectSlot& _slot);

/// IsViperType (CS:5413): whether _slot is a Viper (type 1Ch).
[[nodiscard]] bool IsViperType(const ObjectSlot& _slot);

/// IsPoliceViper (CS:541E): whether _slot is a Viper with word +3Ah = 1.
[[nodiscard]] bool IsPoliceViper(const ObjectSlot& _slot);

/// IsThargoidType (CS:5428): whether _slot is a Thargoid (type 16h).
[[nodiscard]] bool IsThargoidType(const ObjectSlot& _slot);

/// IsThargonType (CS:5431): whether _slot is a Thargon (type 7).
[[nodiscard]] bool IsThargonType(const ObjectSlot& _slot);

} // namespace Elite
