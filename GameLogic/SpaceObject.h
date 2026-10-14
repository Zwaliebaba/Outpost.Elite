// GameLogic/SpaceObject.h
#pragma once

#include "Field.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

// A member's offset in SpaceObject, as a constant expression wherever this header is read; under Clang the builtin, for the
// reason GameData.h gives for GAME_DATA_OFFSET.
#if defined(__clang__)
#   define SPACE_OBJECT_OFFSET(member) __builtin_offsetof(::Elite::SpaceObject, member)
#else
#   define SPACE_OBJECT_OFFSET(member) offsetof(::Elite::SpaceObject, member)
#endif

namespace Elite
{

/// A slot of shipSlots by its number, where the original holds its offset, DS:6930 + 64 times the number, in SI or DI or in a
/// word of its state (D33, piece 3): 0 the sun, 1 the planet, 2 the station, 3-19 the ships and 20-35 the debris. A search that
/// finds none leaves the number past the last slot it looked at.
///
/// A slot stored in the game's state (missileTarget, playerHitBy, maskShipSlot, and a record's target and owner) may instead hold
/// one of the values below, which the original stores as the offsets 0 and 1, where no slot is. They are the sun's and the
/// planet's numbers, which none of those fields ever holds: no missile is aimed at the sun or the planet, no pack mate, mother or
/// attacker is either, and no search for a ship returns them. So a slot cleared to 0 still means what the original's does.
enum class SlotIndex : std::uint16_t
{
  Sun = 0,
  Planet = 1,
  Station = 2,
  FirstShip = 3,
  FirstDebris = 20,
  End = 36, ///< past the last slot

  Player = 0, ///< a target of 0: the player
  None = 0,   ///< an owner of 0: no mother, and not the police
  Police = 1, ///< an owner of 1: a police Viper
};

/// _slot's number.
[[nodiscard]] constexpr std::uint16_t Number(SlotIndex _slot) noexcept
{
  return static_cast<std::uint16_t>(_slot);
}

/// The slot _count numbers on from _slot, as the original's SI or DI steps on by 40h a slot.
[[nodiscard]] constexpr SlotIndex SlotAfter(SlotIndex _slot, std::uint16_t _count = 1) noexcept
{
  return static_cast<SlotIndex>(Number(_slot) + _count);
}

/// One of the 36 records of 64 bytes in shipSlots (DS:6930) that hold the objects in space (ADR-012): slot 0 the sun, 1 the planet, 2 the
/// station, 3-19 the ships and 20-35 the debris. Each field is at its 1987 offset, a byte or a word as the original reads it, and counts
/// its own changes (Field). The GameState gives the record of a slot's number (GameState::Slot), and a record's number
/// (GameState::IndexOf), where the original holds the record's offset in SI or DI.
///
/// Where the original reads one place by two names of one width, the record has one member and an accessor for the other name
/// (SpinRoll, SpinPitch, FlashFrames, FramesAway): no union (change 3c). Where it reads
/// a byte of a word, or two bytes as one word, the record has the field it reads most, and the rest is read and written with
/// Low, High, WithLow and WithHigh where it is used:
/// - x, y and z: their high bytes (+05, +07, +09), which the sun's and planet's set-up writes, and z's low byte (+08);
/// - xHigh and yHigh as one word (+01), which the title clears;
/// - pitch: the sun's or planet's scale shift (+0A, its low byte) and colour (+0B, its high byte);
/// - yaw: the station's collided bit (+0C, bit 0 of its low byte). The colour and the byte after it (+0B-0C) are written as
///   one word, pitch's high byte and yaw's low byte;
/// - viewZ's high byte (+15), and compassZ's (+25), the station's depth;
/// - the spin (a fragment's) and the compass dot (the station's) as one word (+26), blipX's and blipY's bytes;
/// - cargo and fragments as one word (+2C), and aggression and bounty (+30).
struct SpaceObject
{
  // Trivial, so GameData stays trivially copyable, and the copy explicit, as GameData's structs are (Field.h).
  SpaceObject() noexcept = default;
  explicit SpaceObject(const SpaceObject&) noexcept = default;

  /// A record's size, and the step from one to the next.
  static constexpr std::uint16_t BYTES = 0x40;

  /// Bit 0 of the type byte: the slot is in use.
  static constexpr std::uint8_t ACTIVE = 0x01;

  Field<std::uint8_t> type;  ///< +00: bit 0 active, bits 1-5 the type, bit 7 drawn this frame
  Field<std::uint8_t> xHigh; ///< +01: the high bytes of the 24-bit position
  Field<std::uint8_t> yHigh;
  Field<std::uint8_t> zHigh;
  Field<std::uint16_t> x; ///< +04: the low words of the 24-bit position
  Field<std::uint16_t> y;
  Field<std::uint16_t> z;
  Field<std::uint16_t> pitch; ///< +0A: the heading, as ConvertVectorToAngles gives it
  Field<std::uint16_t> yaw;
  Field<std::uint16_t> roll;
  Field<std::uint16_t> viewX; ///< +10: the position in the camera's frame
  Field<std::uint16_t> viewY;
  Field<std::uint16_t> viewZ;
  Field<std::uint8_t> passes;  ///< +16: attack passes left
  Field<std::uint8_t> aiState; ///< +17: the AI state
  Field<std::uint8_t> speed;
  Field<std::uint8_t> velocityX; ///< +19: signed bytes: x, y, z
  Field<std::uint8_t> velocityY;
  Field<std::uint8_t> velocityZ;
  Field<std::uint8_t> range; ///< +1C: the high byte of the range a ship turns back at
  Field<std::uint8_t> turnRate;
  Field<std::uint8_t> flags;     ///< +1E: bit 0 hostile, 1 blip drawn, 2 indestructible, 5 carries the device
  Field<std::uint8_t> thargons;  ///< +1F: Thargons a Thargoid has left to launch
  Field<std::uint16_t> compassX; ///< +20: the station's direction for the compass
  Field<std::uint16_t> compassY;
  Field<std::uint16_t> compassZ;
  Field<std::uint8_t> blipX; ///< +26: the scanner blip, or the station's compass dot: x, y, z; a fragment's spin (SpinRoll)
  Field<std::uint8_t> blipY; ///< +27: and a fragment's (SpinPitch)
  Field<std::uint8_t> blipZ;
  Field<SlotIndex> target; ///< +29: a missile's target, a hunter's pack mate; SlotIndex::Player for the player
  Field<std::uint8_t> energy;
  Field<std::uint8_t> cargo; ///< +2C; for a ship that carries the device, its flash's frames (FlashFrames)
  Field<std::uint8_t> fragments;
  Field<std::uint8_t> lifetime; ///< +2E: frames a fragment has left
  Field<std::uint8_t> age;
  Field<std::uint8_t> aggression;
  Field<std::uint8_t> bounty;
  Field<std::uint8_t> missiles;
  Field<std::uint8_t> behaviorClass; ///< +33: the behaviour class, behaviorHandlers' index
  Field<std::uint8_t> scanned;       ///< +34; from its 1, the frames an object spends off the scanner (FramesAway)
  Field<std::uint8_t> jinkFrames;    ///< +35: frames to the next evasive jink
  Field<std::uint16_t> jinkPitch;    ///< +36: the jink added to the wanted heading
  Field<std::uint16_t> jinkYaw;
  Field<SlotIndex> owner;          ///< +3A: a Thargon's mother; SlotIndex::Police for a police Viper, SlotIndex::None for neither
  Field<std::uint8_t> cameraZHigh; ///< +3C: the high byte of the camera-frame z: bit 7 set behind
  Field<std::uint8_t> depth;       ///< +3D: the drawing order; for the sun and planet, their scale shift again
  Field<std::uint8_t> size;
  Field<std::uint8_t> detail; ///< +3F: the level-of-detail threshold, from the template

  /// Each field, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("type", _self.type);
    _visitor("xHigh", _self.xHigh);
    _visitor("yHigh", _self.yHigh);
    _visitor("zHigh", _self.zHigh);
    _visitor("x", _self.x);
    _visitor("y", _self.y);
    _visitor("z", _self.z);
    _visitor("pitch", _self.pitch);
    _visitor("yaw", _self.yaw);
    _visitor("roll", _self.roll);
    _visitor("viewX", _self.viewX);
    _visitor("viewY", _self.viewY);
    _visitor("viewZ", _self.viewZ);
    _visitor("passes", _self.passes);
    _visitor("aiState", _self.aiState);
    _visitor("speed", _self.speed);
    _visitor("velocityX", _self.velocityX);
    _visitor("velocityY", _self.velocityY);
    _visitor("velocityZ", _self.velocityZ);
    _visitor("range", _self.range);
    _visitor("turnRate", _self.turnRate);
    _visitor("flags", _self.flags);
    _visitor("thargons", _self.thargons);
    _visitor("compassX", _self.compassX);
    _visitor("compassY", _self.compassY);
    _visitor("compassZ", _self.compassZ);
    _visitor("blipX", _self.blipX);
    _visitor("blipY", _self.blipY);
    _visitor("blipZ", _self.blipZ);
    _visitor("target", _self.target);
    _visitor("energy", _self.energy);
    _visitor("cargo", _self.cargo);
    _visitor("fragments", _self.fragments);
    _visitor("lifetime", _self.lifetime);
    _visitor("age", _self.age);
    _visitor("aggression", _self.aggression);
    _visitor("bounty", _self.bounty);
    _visitor("missiles", _self.missiles);
    _visitor("behaviorClass", _self.behaviorClass);
    _visitor("scanned", _self.scanned);
    _visitor("jinkFrames", _self.jinkFrames);
    _visitor("jinkPitch", _self.jinkPitch);
    _visitor("jinkYaw", _self.jinkYaw);
    _visitor("owner", _self.owner);
    _visitor("cameraZHigh", _self.cameraZHigh);
    _visitor("depth", _self.depth);
    _visitor("size", _self.size);
    _visitor("detail", _self.detail);
  }

  template <typename Visitor> void Visit(Visitor& _visitor)
  {
    VisitMembers(*this, _visitor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor) const
  {
    VisitMembers(*this, _visitor);
  }

  /// x, y and z of the position relative to the player, 24 bits each: the high byte and the low word. The value is the 24-bit
  /// two's complement number, held in the low 24 bits.
  [[nodiscard]] std::uint32_t PositionX() const noexcept
  {
    return Position(xHigh, x);
  }

  [[nodiscard]] std::uint32_t PositionY() const noexcept
  {
    return Position(yHigh, y);
  }

  [[nodiscard]] std::uint32_t PositionZ() const noexcept
  {
    return Position(zHigh, z);
  }

  /// +26 and +27 read as a fragment's spin, added to the roll and the pitch each frame: blipX's and blipY's bytes, which a
  /// fragment, having no blip, uses for it.
  [[nodiscard]] Field<std::uint8_t>& SpinRoll() noexcept
  {
    return blipX;
  }

  [[nodiscard]] const Field<std::uint8_t>& SpinRoll() const noexcept
  {
    return blipX;
  }

  [[nodiscard]] Field<std::uint8_t>& SpinPitch() noexcept
  {
    return blipY;
  }

  [[nodiscard]] const Field<std::uint8_t>& SpinPitch() const noexcept
  {
    return blipY;
  }

  /// +2C read for a ship that carries the device (flags bit 5): frames to its next flash on or off, cargo's byte.
  [[nodiscard]] Field<std::uint8_t>& FlashFrames() noexcept
  {
    return cargo;
  }

  [[nodiscard]] const Field<std::uint8_t>& FlashFrames() const noexcept
  {
    return cargo;
  }

  /// +34 read, from scanned's 1, as the frames an object spends off the scanner: scanned's byte.
  [[nodiscard]] Field<std::uint8_t>& FramesAway() noexcept
  {
    return scanned;
  }

  [[nodiscard]] const Field<std::uint8_t>& FramesAway() const noexcept
  {
    return scanned;
  }

private:
  [[nodiscard]] static std::uint32_t Position(std::uint8_t _high, std::uint16_t _low) noexcept
  {
    return (static_cast<std::uint32_t>(_high) << 16) | _low;
  }
};

/// A byte field of the record, or a word field, for a routine that takes the field it works on: `_slot.*_field`.
using ObjectByte = Field<std::uint8_t> SpaceObject::*;
using ObjectWord = Field<std::uint16_t> SpaceObject::*;

static_assert(sizeof(SpaceObject) == SpaceObject::BYTES);
static_assert(std::is_trivially_copyable_v<SpaceObject> && std::is_standard_layout_v<SpaceObject>);
static_assert(!std::is_convertible_v<const SpaceObject&, SpaceObject>, "a copy is only ever explicit");
static_assert(SPACE_OBJECT_OFFSET(type) == 0x00);
static_assert(SPACE_OBJECT_OFFSET(xHigh) == 0x01);
static_assert(SPACE_OBJECT_OFFSET(yHigh) == 0x02);
static_assert(SPACE_OBJECT_OFFSET(zHigh) == 0x03);
static_assert(SPACE_OBJECT_OFFSET(x) == 0x04);
static_assert(SPACE_OBJECT_OFFSET(y) == 0x06);
static_assert(SPACE_OBJECT_OFFSET(z) == 0x08);
static_assert(SPACE_OBJECT_OFFSET(pitch) == 0x0A);
static_assert(SPACE_OBJECT_OFFSET(yaw) == 0x0C);
static_assert(SPACE_OBJECT_OFFSET(roll) == 0x0E);
static_assert(SPACE_OBJECT_OFFSET(viewX) == 0x10);
static_assert(SPACE_OBJECT_OFFSET(viewY) == 0x12);
static_assert(SPACE_OBJECT_OFFSET(viewZ) == 0x14);
static_assert(SPACE_OBJECT_OFFSET(passes) == 0x16);
static_assert(SPACE_OBJECT_OFFSET(aiState) == 0x17);
static_assert(SPACE_OBJECT_OFFSET(speed) == 0x18);
static_assert(SPACE_OBJECT_OFFSET(velocityX) == 0x19);
static_assert(SPACE_OBJECT_OFFSET(velocityY) == 0x1A);
static_assert(SPACE_OBJECT_OFFSET(velocityZ) == 0x1B);
static_assert(SPACE_OBJECT_OFFSET(range) == 0x1C);
static_assert(SPACE_OBJECT_OFFSET(turnRate) == 0x1D);
static_assert(SPACE_OBJECT_OFFSET(flags) == 0x1E);
static_assert(SPACE_OBJECT_OFFSET(thargons) == 0x1F);
static_assert(SPACE_OBJECT_OFFSET(compassX) == 0x20);
static_assert(SPACE_OBJECT_OFFSET(compassY) == 0x22);
static_assert(SPACE_OBJECT_OFFSET(compassZ) == 0x24);
static_assert(SPACE_OBJECT_OFFSET(blipX) == 0x26);
static_assert(SPACE_OBJECT_OFFSET(blipY) == 0x27);
static_assert(SPACE_OBJECT_OFFSET(blipZ) == 0x28);
static_assert(SPACE_OBJECT_OFFSET(target) == 0x29);
static_assert(SPACE_OBJECT_OFFSET(energy) == 0x2B);
static_assert(SPACE_OBJECT_OFFSET(cargo) == 0x2C);
static_assert(SPACE_OBJECT_OFFSET(fragments) == 0x2D);
static_assert(SPACE_OBJECT_OFFSET(lifetime) == 0x2E);
static_assert(SPACE_OBJECT_OFFSET(age) == 0x2F);
static_assert(SPACE_OBJECT_OFFSET(aggression) == 0x30);
static_assert(SPACE_OBJECT_OFFSET(bounty) == 0x31);
static_assert(SPACE_OBJECT_OFFSET(missiles) == 0x32);
static_assert(SPACE_OBJECT_OFFSET(behaviorClass) == 0x33);
static_assert(SPACE_OBJECT_OFFSET(scanned) == 0x34);
static_assert(SPACE_OBJECT_OFFSET(jinkFrames) == 0x35);
static_assert(SPACE_OBJECT_OFFSET(jinkPitch) == 0x36);
static_assert(SPACE_OBJECT_OFFSET(jinkYaw) == 0x38);
static_assert(SPACE_OBJECT_OFFSET(owner) == 0x3A);
static_assert(SPACE_OBJECT_OFFSET(cameraZHigh) == 0x3C);
static_assert(SPACE_OBJECT_OFFSET(depth) == 0x3D);
static_assert(SPACE_OBJECT_OFFSET(size) == 0x3E);
static_assert(SPACE_OBJECT_OFFSET(detail) == 0x3F);

} // namespace Elite
