// GameLogic/Ships.h
#pragma once

#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's ships routines, ported (plan §5 Phase 3, ADR-010): the ship slots, blueprints and their vertex programs. Each body
// is declared here once it is ported, on the registers of its contract in Symbols.tsv.

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

/// IsStation (CS:3F40): AL = the type of the slot at DI; ZF set for 0 or 1, CF set for 0 (the Dodo).
void IsStation(Guest& _guest);

/// IsObjectNearKeepBlip (CS:460E): IsObjectNear's test alone.
void IsObjectNearKeepBlip(Guest& _guest);

/// InitCargoBarrel (CS:4C99): the slot at DI made a barrel (spawnTemplates entry 2), class 3.
void InitCargoBarrel(Guest& _guest);

/// SpawnRandomHunter (CS:4D3B): a random class-6 ship in the free slot at DI.
void SpawnRandomHunter(Guest& _guest);

/// SpawnRandomWolf (CS:4D60): a random class-5 ship in the free slot at DI, a Thargoid in witch space.
void SpawnRandomWolf(Guest& _guest);

/// InitObjectFromTemplate (CS:4E1B): the slot at DI from the 10-byte record AL of the table at BX. Out: BX the record, AL its last byte.
void InitObjectFromTemplate(Guest& _guest);

/// PlaceAtSpawnPoint (CS:4E75): the slot at DI placed at a random point 10000 out along the rotations in DS:41B8 and DS:41BC.
void PlaceAtSpawnPoint(Guest& _guest);

/// FacePlayerWithRandomRoll (CS:4EC5): FacePlayer, then a random roll.
void FacePlayerWithRandomRoll(Guest& _guest);

/// GetObjectPosition (CS:4EF4): AX, BX, CX = the low words of the position of the slot at DI.
void GetObjectPosition(Guest& _guest);

/// GetVectorToPlayer (CS:4EFE): AX, BX, CX = the position of the slot at DI, negated.
void GetVectorToPlayer(Guest& _guest);

/// ComputeVelocity (CS:4F48): the velocity bytes of the slot at DI from its speed and heading, through rotation pairs 7 and 6.
void ComputeVelocity(Guest& _guest);

/// MoveObject (CS:4F6E): adds the velocity of the slot at DI to its position, and removes it once IsObjectNear fails.
void MoveObject(Guest& _guest);

/// RemoveObject (CS:4F98): clears the active bit of the slot at DI and erases its blip.
void RemoveObject(Guest& _guest);

/// FacePlayer (CS:513E): the heading of the slot at DI turned to the player.
void FacePlayer(Guest& _guest);

/// FindFreeShipSlot (CS:51E0): CF set and SI = the first inactive ship slot; CF clear when none is.
void FindFreeShipSlot(Guest& _guest);

/// ClearAllObjects (CS:52B2): zeroes shipSlotCount slots. Out: ES = DS, AX = CX = 0, DI past the end.
void ClearAllObjects(Guest& _guest);

/// FindDebrisSlot (CS:52EC): SI = the first inactive debris slot, or the oldest.
void FindDebrisSlot(Guest& _guest);

/// CopyObject (CS:5320): the 64 bytes at SI copied to DI. Out: ES = DS.
void CopyObject(Guest& _guest);

/// UpdateDebrisAi (CS:5330): a fragment's frame: its lifetime counted down, its spin, MoveObject.
void UpdateDebrisAi(Guest& _guest);

/// IsDebrisType (CS:53FE): AL = the type of the slot at DI; ZF set for 5, 6, 0Bh or 0Ch.
void IsDebrisType(Guest& _guest);

/// IsViperType (CS:5413): ZF set when the slot at DI is a Viper (type 1Ch). Keeps AX.
void IsViperType(Guest& _guest);

/// IsPoliceViper (CS:541E): ZF set when the slot at DI is a Viper with word +3Ah = 1.
void IsPoliceViper(Guest& _guest);

/// IsThargoidType (CS:5428): AL = the type of the slot at DI; ZF set for 16h.
void IsThargoidType(Guest& _guest);

/// IsThargonType (CS:5431): AL = the type of the slot at DI; ZF set for 7.
void IsThargonType(Guest& _guest);

} // namespace Elite
