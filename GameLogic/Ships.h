// GameLogic/Ships.h
#pragma once

#include "GameState.h"
#include "Maths.h"
#include "NativeEntry.h"
#include "ObjectSlot.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's ships routines, ported (plan §5 Phase 3, ADR-010): the ship slots, blueprints and their vertex programs. Each body
// is declared here once it is ported, on the registers of its contract in Symbols.tsv; those de-assembled (ADR-012) take values and
// give values back, below the register bodies, and their entries keep the register contracts.

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

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> ShipsEntries() noexcept;

/// IsObjectNear (CS:3B9A): CF set when each 24-bit coordinate of the slot at DI fits a signed word; otherwise
/// EraseScannerBlip, and CF clear. AL is the last high byte looked at (0 for FFh).
void IsObjectNear(Guest& _guest);

/// IsSunOrPlanet (CS:3F2A): AL = the type of the slot at DI; ZF set for 1Eh or 1Fh, the flags of the last CMP.
void IsSunOrPlanet(Guest& _guest);

/// IsPlanet (CS:3F37): AL = the type of the slot at DI; ZF set for 1Fh.
void IsPlanet(Guest& _guest);

/// IsObjectNearKeepBlip (CS:460E): IsObjectNear's test alone.
void IsObjectNearKeepBlip(Guest& _guest);

/// InitPoliceViper (CS:4C76): the slot at DI made a Viper (spawnTemplates entry 14), class 4, police (+3Ah = 1), aggression 64h.
void InitPoliceViper(Guest& _guest);

/// InitCargoBarrel (CS:4C99): the slot at DI made a barrel (spawnTemplates entry 2), class 3.
void InitCargoBarrel(Guest& _guest);

/// InitAbandonedCobra (CS:4CA6): the slot at DI made a Cobra (entry 9) of class 0: the ship an escape pod leaves.
void InitAbandonedCobra(Guest& _guest);

/// InitEscapePod (CS:4CB3): the slot at DI made an escape pod (entry 1), class 3.
void InitEscapePod(Guest& _guest);

/// InitShuttle (CS:4CC0): the slot at DI made a shuttle (entry 7), class 3.
void InitShuttle(Guest& _guest);

/// InitKraitHunter (CS:4CCD): the slot at DI made a Krait (entry 17), class 6.
void InitKraitHunter(Guest& _guest);

/// InitThargon (CS:4CDA): the slot at DI made a Thargon (entry 28), class 5.
void InitThargon(Guest& _guest);

/// SpawnRandomDrifter (CS:4CE7): one of the eight drifters from entry 1 in the free slot at DI, class 3, turn rate 1Eh.
void SpawnRandomDrifter(Guest& _guest);

/// SpawnRandomTrader (CS:4D08): one of the six traders from entry 9 in the free slot at DI, class 4; a Viper is police half the time.
void SpawnRandomTrader(Guest& _guest);

/// SpawnRandomHunter (CS:4D3B): a random class-6 ship in the free slot at DI.
void SpawnRandomHunter(Guest& _guest);

/// SpawnRandomWolf (CS:4D60): a random class-5 ship in the free slot at DI, a Thargoid in witch space.
void SpawnRandomWolf(Guest& _guest);

/// SpawnMaskMissionShip (CS:4DAE): a mask-mission ship in the free slot at DI from the Asp's record: the Asp when CF is set,
/// otherwise type 12h or 13h by a random sign.
void SpawnMaskMissionShip(Guest& _guest);

/// SpawnInvasionThargoid (CS:4DF0): an invasion's Thargoid in the free slot at DI, with 8 Thargons.
void SpawnInvasionThargoid(Guest& _guest);

/// PlaceAtSpawnPoint (CS:4E75): the slot at DI placed at a random point 10000 out along the rotations in DS:41B8 and DS:41BC.
void PlaceAtSpawnPoint(Guest& _guest);

/// FacePlayerWithRandomRoll (CS:4EC5): FacePlayer, then a random roll.
void FacePlayerWithRandomRoll(Guest& _guest);

/// GetVectorToPlayer (CS:4EFE): AX, BX, CX = the position of the slot at DI, negated.
void GetVectorToPlayer(Guest& _guest);

/// RandomizeOrientation (CS:4F35): the pitch, yaw and roll of the slot at DI, a random word each.
void RandomizeOrientation(Guest& _guest);

/// ComputeVelocity (CS:4F48): the velocity bytes of the slot at DI from its speed and heading, through rotation pairs 7 and 6.
void ComputeVelocity(Guest& _guest);

/// MoveObject (CS:4F6E): adds the velocity of the slot at DI to its position, and removes it once IsObjectNear fails.
void MoveObject(Guest& _guest);

/// RemoveObject (CS:4F98): clears the active bit of the slot at DI and erases its blip.
void RemoveObject(Guest& _guest);

/// FacePlayer (CS:513E): the heading of the slot at DI turned to the player.
void FacePlayer(Guest& _guest);

/// ReclaimShipSlot (CS:51FD): SI = the first ship slot whose blip is not drawn; when every one has a blip, one of slots 4-19 at
/// random, removed (DI = SI).
void ReclaimShipSlot(Guest& _guest);

/// UpdateDebrisAi (CS:5330): a fragment's frame: its lifetime counted down, its spin, MoveObject.
void UpdateDebrisAi(Guest& _guest);

/// IsDebrisType (CS:53FE): AL = the type of the slot at DI; ZF set for 5, 6, 0Bh or 0Ch.
void IsDebrisType(Guest& _guest);

/// IsPoliceViper (CS:541E): ZF set when the slot at DI is a Viper with word +3Ah = 1.
void IsPoliceViper(Guest& _guest);

/// IsThargoidType (CS:5428): AL = the type of the slot at DI; ZF set for 16h.
void IsThargoidType(Guest& _guest);

/// IsThargonType (CS:5431): AL = the type of the slot at DI; ZF set for 7.
void IsThargonType(Guest& _guest);

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

/// What IsStation finds of a slot.
struct StationTest
{
  std::uint8_t type; ///< bits 1-5 of the type byte
  bool station;      ///< a Dodo (type 0) or a Coriolis (type 1)
  bool dodo;
};

/// ADD [slot+4+2*axis],_value / ADC [slot+1+axis],DL with DL from CWD: _value added to the 24-bit coordinate _axis (0 x, 1 y,
/// 2 z) of _slot, the low word, then the high byte. Not an entry: the idiom several routines share.
void AddToCoordinate(ObjectSlot _slot, int _axis, std::int16_t _value);

/// ClearObjectSlot (CS:2FD8): the 64 bytes of the slot at _slot zeroed, byte by byte.
void ClearObjectSlot(GameState& _state, std::uint16_t _slot);

/// IsStation (CS:3F40): _slot's type, and whether it is a station's.
[[nodiscard]] StationTest IsStation(const ObjectSlot& _slot);

/// InitObjectFromTemplate (CS:4E1B): _slot from the 10-byte record _index of the table at _table: its state, flags and
/// aggression cleared, its scanned byte 1, its type active, then the record's nine fields.
void InitObjectFromTemplate(GameState& _state, ObjectSlot _slot, std::uint16_t _table, std::uint8_t _index);

/// GetObjectPosition (CS:4EF4): the low words of _slot's position.
[[nodiscard]] Vector GetObjectPosition(const ObjectSlot& _slot);

/// FindFreeShipSlot (CS:51E0): the first inactive ship slot, from firstShipSlot.
[[nodiscard]] SlotSearch FindFreeShipSlot(GameState& _state);

/// ClearAllObjects (CS:52B2): shipSlotCount slots from shipSlots zeroed, a word at a time as REP STOSW does, backwards when
/// _backward (the direction flag) is set.
void ClearAllObjects(GameState& _state, bool _backward);

/// FindDebrisSlot (CS:52EC): the first inactive debris slot, or, when every one is active, the oldest, the last of equals.
[[nodiscard]] std::uint16_t FindDebrisSlot(GameState& _state);

/// CopyObject (CS:5320): the 64 bytes at _source copied to _destination a word at a time as REP MOVSW does, backwards when
/// _backward (the direction flag) is set.
void CopyObject(GameState& _state, std::uint16_t _source, std::uint16_t _destination, bool _backward);

/// IsViperType (CS:5413): whether _slot is a Viper (type 1Ch).
[[nodiscard]] bool IsViperType(const ObjectSlot& _slot);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void ClearObjectSlotEntry(Guest& _guest);        ///< SI = the slot. Out: DI = the slot, SI = the slot + 40h, CX = 0.
void IsStationEntry(Guest& _guest);              ///< DI = the slot. Out: AL = the type; ZF set for a station, CF for the Dodo.
void InitObjectFromTemplateEntry(Guest& _guest); ///< BX = the table, AL = the record, DI = the slot. AX, BX clobbered.
void GetObjectPositionEntry(Guest& _guest);      ///< DI = the slot. Out: AX, BX, CX.
void FindFreeShipSlotEntry(Guest& _guest);       ///< Out: CF set and SI = the slot when one is free, else SI past the slots.
void ClearAllObjectsEntry(Guest& _guest);        ///< Out: ES = DS. AX, CX, DI clobbered.
void FindDebrisSlotEntry(Guest& _guest);         ///< Out: SI = the slot.
void CopyObjectEntry(Guest& _guest);             ///< SI = the source, DI = the destination. Out: ES = DS.
void IsViperTypeEntry(Guest& _guest);            ///< DI = the slot. Out: ZF set for a Viper; AX kept.

} // namespace Elite
