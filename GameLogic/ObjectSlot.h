// GameLogic/ObjectSlot.h
#pragma once

#include "GameState.h"

#include <cstdint>

namespace Elite
{

/// One of the 36 records of 64 bytes from shipSlots (DS:6930) that hold the objects in space, by the
/// fields Reference-Map.md lays out (ADR-012). A view: it names the bytes the original reads and writes
/// at a slot's offset, and reads and writes them through the GameState, as the original does.
class ObjectSlot
{
public:
  ObjectSlot(GameState& _state, std::uint16_t _offset) noexcept
    : m_state(&_state),
      m_offset(_offset)
  {
  }

  /// The slot's offset in the data segment: what the original holds in DI.
  [[nodiscard]] std::uint16_t Offset() const noexcept
  {
    return m_offset;
  }

  /// x, y and z of the position relative to the player, 24 bits each: the high byte at +01, +02 or
  /// +03, the low word at +04, +06 or +08. The value is the 24-bit two's complement number, held in
  /// the low 24 bits.
  [[nodiscard]] std::uint32_t PositionX() const noexcept
  {
    return Position(POSITION_X_HIGH, POSITION_X_LOW);
  }

  [[nodiscard]] std::uint32_t PositionY() const noexcept
  {
    return Position(POSITION_Y_HIGH, POSITION_Y_LOW);
  }

  [[nodiscard]] std::uint32_t PositionZ() const noexcept
  {
    return Position(POSITION_Z_HIGH, POSITION_Z_LOW);
  }

  /// The low words of the position, +04, +06 and +08.
  [[nodiscard]] std::uint16_t PositionLowX() const noexcept
  {
    return m_state->Word(Field(POSITION_X_LOW));
  }

  [[nodiscard]] std::uint16_t PositionLowY() const noexcept
  {
    return m_state->Word(Field(POSITION_Y_LOW));
  }

  [[nodiscard]] std::uint16_t PositionLowZ() const noexcept
  {
    return m_state->Word(Field(POSITION_Z_LOW));
  }

  /// The sun's or planet's scale, +0A: the shift its distance is taken down by. Other objects keep
  /// their pitch there.
  [[nodiscard]] std::uint8_t DiscScale() const noexcept
  {
    return m_state->Byte(Field(DISC_SCALE));
  }

  /// The view-space position, +10, +12 and +14.
  [[nodiscard]] std::uint16_t ViewX() const noexcept
  {
    return m_state->Word(Field(VIEW_X));
  }

  [[nodiscard]] std::uint16_t ViewY() const noexcept
  {
    return m_state->Word(Field(VIEW_Y));
  }

  [[nodiscard]] std::uint16_t ViewZ() const noexcept
  {
    return m_state->Word(Field(VIEW_Z));
  }

private:
  static constexpr std::uint16_t POSITION_X_HIGH = 0x01;
  static constexpr std::uint16_t POSITION_Y_HIGH = 0x02;
  static constexpr std::uint16_t POSITION_Z_HIGH = 0x03;
  static constexpr std::uint16_t POSITION_X_LOW = 0x04;
  static constexpr std::uint16_t POSITION_Y_LOW = 0x06;
  static constexpr std::uint16_t POSITION_Z_LOW = 0x08;
  static constexpr std::uint16_t DISC_SCALE = 0x0A;
  static constexpr std::uint16_t VIEW_X = 0x10;
  static constexpr std::uint16_t VIEW_Y = 0x12;
  static constexpr std::uint16_t VIEW_Z = 0x14;

  // The offset _field bytes into the slot, wrapping at 64K as [DI+disp] does.
  [[nodiscard]] std::uint16_t Field(std::uint16_t _field) const noexcept
  {
    return static_cast<std::uint16_t>(m_offset + _field);
  }

  [[nodiscard]] std::uint32_t Position(std::uint16_t _high, std::uint16_t _low) const noexcept
  {
    return (std::uint32_t{m_state->Byte(Field(_high))} << 16) | m_state->Word(Field(_low));
  }

  GameState* m_state;
  std::uint16_t m_offset;
};

} // namespace Elite
