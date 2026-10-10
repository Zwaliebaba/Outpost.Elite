// GameLogic/ObjectSlot.h
#pragma once

#include "GameState.h"

#include <cstdint>

namespace Elite
{

/// The byte fields of an object slot, by what they hold (Reference-Map.md). An offset that holds different
/// things for different kinds of object has a name for each.
enum class SlotByte : std::uint16_t
{
  Type = 0x00,  ///< bit 0 active, bits 1-5 the type, bit 7 drawn this frame
  XHigh = 0x01, ///< the high bytes of the 24-bit position
  YHigh = 0x02,
  ZHigh = 0x03,
  DiscScale = 0x0A, ///< the sun's or planet's scale shift; for a ship, the low byte of its pitch
  Color = 0x0B,     ///< the sun's or planet's colour
  Collided = 0x0C,  ///< the station's: bit 0 set while the player is inside it; for a ship, the low byte of its yaw
  ViewZHigh = 0x15, ///< the high byte of the view-space z
  Passes = 0x16,    ///< attack passes left
  State = 0x17,     ///< the AI state
  Speed = 0x18,
  VelocityX = 0x19, ///< signed bytes: x, y, z
  VelocityY = 0x1A,
  VelocityZ = 0x1B,
  Range = 0x1C, ///< the high byte of the range a ship turns back at
  TurnRate = 0x1D,
  Flags = 0x1E,        ///< bit 0 hostile, 1 blip drawn, 2 indestructible, 5 carries the device
  Thargons = 0x1F,     ///< Thargons a Thargoid has left to launch
  StationDepth = 0x25, ///< the station's
  SpinRoll = 0x26,     ///< a fragment's spin, added to the roll and the pitch each frame
  SpinPitch = 0x27,
  BlipX = 0x26, ///< the scanner blip, or the compass dot: x, y, z
  BlipY = 0x27,
  BlipZ = 0x28,
  Energy = 0x2B,
  Cargo = 0x2C,
  FlashFrames = 0x2C, ///< a ship's that carries the device (Flags bit 5): frames to its next flash on or off
  Fragments = 0x2D,
  Lifetime = 0x2E, ///< frames a fragment has left
  Age = 0x2F,
  Aggression = 0x30,
  Bounty = 0x31,
  Missiles = 0x32,
  Class = 0x33, ///< the behaviour class, behaviorHandlers' index
  Scanned = 0x34,
  FramesAway = 0x34,  ///< the sun's or planet's
  JinkFrames = 0x35,  ///< frames to the next evasive jink
  CameraZHigh = 0x3C, ///< the high byte of the camera-frame z: bit 7 set behind
  Depth = 0x3D,       ///< the drawing order; for the sun and planet, their scale shift again
  Size = 0x3E,
  Detail = 0x3F, ///< the level-of-detail threshold, from the template
};

/// The word fields of an object slot.
enum class SlotWord : std::uint16_t
{
  X = 0x04, ///< the low words of the 24-bit position
  Y = 0x06,
  Z = 0x08,
  Pitch = 0x0A, ///< the heading, as ConvertVectorToAngles gives it
  Yaw = 0x0C,
  Roll = 0x0E,
  ViewX = 0x10, ///< the position in the camera's frame
  ViewY = 0x12,
  ViewZ = 0x14,
  CompassX = 0x20, ///< the station's direction for the compass
  CompassY = 0x22,
  CompassZ = 0x24,
  Target = 0x29,    ///< a missile's target, a hunter's pack mate
  JinkPitch = 0x36, ///< the jink added to the wanted heading
  JinkYaw = 0x38,
  Owner = 0x3A, ///< a Thargon's mother; 1 for a police Viper
};

/// One of the records of 64 bytes, from shipSlots (DS:6930) and debrisSlots, that hold the objects in
/// space (ADR-012). A view: it names the bytes the original reads and writes at a slot's offset, and reads
/// and writes them through the GameState, as the original does.
class ObjectSlot
{
public:
  /// A slot's size, and the step from one to the next.
  static constexpr std::uint16_t BYTES = 0x40;

  /// Bit 0 of the type byte: the slot is in use.
  static constexpr std::uint8_t ACTIVE = 0x01;

  ObjectSlot(GameState& _state, std::uint16_t _offset) noexcept
    : m_state(&_state),
      m_offset(_offset)
  {
  }

  /// The slot's offset in the data segment: what the original holds in DI or SI.
  [[nodiscard]] std::uint16_t Offset() const noexcept
  {
    return m_offset;
  }

  [[nodiscard]] std::uint8_t Get(SlotByte _field) const noexcept
  {
    return m_state->Byte(Field(static_cast<std::uint16_t>(_field)));
  }

  [[nodiscard]] std::uint16_t Get(SlotWord _field) const noexcept
  {
    return m_state->Word(Field(static_cast<std::uint16_t>(_field)));
  }

  void Set(SlotByte _field, std::uint8_t _value) noexcept
  {
    m_state->SetByte(Field(static_cast<std::uint16_t>(_field)), _value);
  }

  void Set(SlotWord _field, std::uint16_t _value) noexcept
  {
    m_state->SetWord(Field(static_cast<std::uint16_t>(_field)), _value);
  }

  /// x, y and z of the position relative to the player, 24 bits each: the high byte and the low word.
  /// The value is the 24-bit two's complement number, held in the low 24 bits.
  [[nodiscard]] std::uint32_t PositionX() const noexcept
  {
    return Position(SlotByte::XHigh, SlotWord::X);
  }

  [[nodiscard]] std::uint32_t PositionY() const noexcept
  {
    return Position(SlotByte::YHigh, SlotWord::Y);
  }

  [[nodiscard]] std::uint32_t PositionZ() const noexcept
  {
    return Position(SlotByte::ZHigh, SlotWord::Z);
  }

private:
  // The offset _field bytes into the slot, wrapping at 64K as [DI+disp] does.
  [[nodiscard]] std::uint16_t Field(std::uint16_t _field) const noexcept
  {
    return static_cast<std::uint16_t>(m_offset + _field);
  }

  [[nodiscard]] std::uint32_t Position(SlotByte _high, SlotWord _low) const noexcept
  {
    return (std::uint32_t{Get(_high)} << 16) | Get(_low);
  }

  GameState* m_state;
  std::uint16_t m_offset;
};

} // namespace Elite
