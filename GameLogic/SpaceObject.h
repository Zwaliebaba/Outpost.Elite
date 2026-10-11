// GameLogic/SpaceObject.h
#pragma once

#include "Counted.h"

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

/// One of the 36 records of 64 bytes in shipSlots (DS:6930) that hold the objects in space (ADR-012): slot 0 the sun, 1 the
/// planet, 2 the station, 3-19 the ships and 20-35 the debris. Each field is at its 1987 offset, a byte or a word as the
/// original reads it, and counts its own changes (Counted). The GameState gives the record at an offset the original holds in
/// SI or DI (GameState::ObjectAt), and the offset of a record (GameState::OffsetOf).
///
/// Where the original reads one place by two names of one width, the two are members of an anonymous union. Where it reads
/// a byte of a word, or two bytes as one word, the record has the field it reads most, and the rest is read and written with
/// Low, High, WithLow and WithHigh where it is used:
/// - x, y and z: their high bytes (+05, +07, +09), which the sun's and planet's set-up writes, and z's low byte (+08);
/// - xHigh and yHigh as one word (+01), which the title clears;
/// - pitch: the sun's or planet's scale shift (+0A, its low byte) and colour (+0B, its high byte);
/// - yaw: the station's collided bit (+0C, bit 0 of its low byte). The colour and the byte after it (+0B-0C) are written as
///   one word, pitch's high byte and yaw's low byte;
/// - viewZ's high byte (+15), and compassZ's (+25), the station's depth;
/// - the spin (a fragment's) and the compass dot (the station's) as one word (+26), the two bytes of the union there;
/// - cargo and fragments as one word (+2C), and aggression and bounty (+30).
struct SpaceObject
{
  /// A record's size, and the step from one to the next.
  static constexpr std::uint16_t BYTES = 0x40;

  /// Bit 0 of the type byte: the slot is in use.
  static constexpr std::uint8_t ACTIVE = 0x01;

  Counted<std::uint8_t> type;  ///< +00: bit 0 active, bits 1-5 the type, bit 7 drawn this frame
  Counted<std::uint8_t> xHigh; ///< +01: the high bytes of the 24-bit position
  Counted<std::uint8_t> yHigh;
  Counted<std::uint8_t> zHigh;
  Counted<std::uint16_t> x; ///< +04: the low words of the 24-bit position
  Counted<std::uint16_t> y;
  Counted<std::uint16_t> z;
  Counted<std::uint16_t> pitch; ///< +0A: the heading, as ConvertVectorToAngles gives it
  Counted<std::uint16_t> yaw;
  Counted<std::uint16_t> roll;
  Counted<std::uint16_t> viewX; ///< +10: the position in the camera's frame
  Counted<std::uint16_t> viewY;
  Counted<std::uint16_t> viewZ;
  Counted<std::uint8_t> passes;  ///< +16: attack passes left
  Counted<std::uint8_t> aiState; ///< +17: the AI state
  Counted<std::uint8_t> speed;
  Counted<std::uint8_t> velocityX; ///< +19: signed bytes: x, y, z
  Counted<std::uint8_t> velocityY;
  Counted<std::uint8_t> velocityZ;
  Counted<std::uint8_t> range; ///< +1C: the high byte of the range a ship turns back at
  Counted<std::uint8_t> turnRate;
  Counted<std::uint8_t> flags;     ///< +1E: bit 0 hostile, 1 blip drawn, 2 indestructible, 5 carries the device
  Counted<std::uint8_t> thargons;  ///< +1F: Thargons a Thargoid has left to launch
  Counted<std::uint16_t> compassX; ///< +20: the station's direction for the compass
  Counted<std::uint16_t> compassY;
  Counted<std::uint16_t> compassZ;
  union
  {
    Counted<std::uint8_t> blipX;    ///< +26: the scanner blip, or the station's compass dot: x, y, z
    Counted<std::uint8_t> spinRoll; ///< +26: a fragment's spin, added to the roll and the pitch each frame
  };
  union
  {
    Counted<std::uint8_t> blipY;
    Counted<std::uint8_t> spinPitch;
  };
  Counted<std::uint8_t> blipZ;
  Counted<std::uint16_t> target; ///< +29: a missile's target, a hunter's pack mate: a slot's offset
  Counted<std::uint8_t> energy;
  union
  {
    Counted<std::uint8_t> cargo;       ///< +2C
    Counted<std::uint8_t> flashFrames; ///< +2C: a ship's that carries the device (flags bit 5): frames to its next flash on or off
  };
  Counted<std::uint8_t> fragments;
  Counted<std::uint8_t> lifetime; ///< +2E: frames a fragment has left
  Counted<std::uint8_t> age;
  Counted<std::uint8_t> aggression;
  Counted<std::uint8_t> bounty;
  Counted<std::uint8_t> missiles;
  Counted<std::uint8_t> behaviorClass; ///< +33: the behaviour class, behaviorHandlers' index
  union
  {
    Counted<std::uint8_t> scanned;    ///< +34
    Counted<std::uint8_t> framesAway; ///< +34: from scanned's 1, the frames an object spends off the scanner
  };
  Counted<std::uint8_t> jinkFrames; ///< +35: frames to the next evasive jink
  Counted<std::uint16_t> jinkPitch; ///< +36: the jink added to the wanted heading
  Counted<std::uint16_t> jinkYaw;
  Counted<std::uint16_t> owner;      ///< +3A: a Thargon's mother: a slot's offset; 1 for a police Viper
  Counted<std::uint8_t> cameraZHigh; ///< +3C: the high byte of the camera-frame z: bit 7 set behind
  Counted<std::uint8_t> depth;       ///< +3D: the drawing order; for the sun and planet, their scale shift again
  Counted<std::uint8_t> size;
  Counted<std::uint8_t> detail; ///< +3F: the level-of-detail threshold, from the template

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

private:
  [[nodiscard]] static std::uint32_t Position(std::uint8_t _high, std::uint16_t _low) noexcept
  {
    return (static_cast<std::uint32_t>(_high) << 16) | _low;
  }
};

static_assert(sizeof(SpaceObject) == SpaceObject::BYTES);
static_assert(std::is_trivially_copyable_v<SpaceObject> && std::is_standard_layout_v<SpaceObject>);
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
static_assert(SPACE_OBJECT_OFFSET(blipX) == 0x26 && SPACE_OBJECT_OFFSET(spinRoll) == 0x26);
static_assert(SPACE_OBJECT_OFFSET(blipY) == 0x27 && SPACE_OBJECT_OFFSET(spinPitch) == 0x27);
static_assert(SPACE_OBJECT_OFFSET(blipZ) == 0x28);
static_assert(SPACE_OBJECT_OFFSET(target) == 0x29);
static_assert(SPACE_OBJECT_OFFSET(energy) == 0x2B);
static_assert(SPACE_OBJECT_OFFSET(cargo) == 0x2C && SPACE_OBJECT_OFFSET(flashFrames) == 0x2C);
static_assert(SPACE_OBJECT_OFFSET(fragments) == 0x2D);
static_assert(SPACE_OBJECT_OFFSET(lifetime) == 0x2E);
static_assert(SPACE_OBJECT_OFFSET(age) == 0x2F);
static_assert(SPACE_OBJECT_OFFSET(aggression) == 0x30);
static_assert(SPACE_OBJECT_OFFSET(bounty) == 0x31);
static_assert(SPACE_OBJECT_OFFSET(missiles) == 0x32);
static_assert(SPACE_OBJECT_OFFSET(behaviorClass) == 0x33);
static_assert(SPACE_OBJECT_OFFSET(scanned) == 0x34 && SPACE_OBJECT_OFFSET(framesAway) == 0x34);
static_assert(SPACE_OBJECT_OFFSET(jinkFrames) == 0x35);
static_assert(SPACE_OBJECT_OFFSET(jinkPitch) == 0x36);
static_assert(SPACE_OBJECT_OFFSET(jinkYaw) == 0x38);
static_assert(SPACE_OBJECT_OFFSET(owner) == 0x3A);
static_assert(SPACE_OBJECT_OFFSET(cameraZHigh) == 0x3C);
static_assert(SPACE_OBJECT_OFFSET(depth) == 0x3D);
static_assert(SPACE_OBJECT_OFFSET(size) == 0x3E);
static_assert(SPACE_OBJECT_OFFSET(detail) == 0x3F);

} // namespace Elite
