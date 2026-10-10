// GameLogic/GameState.h
#pragma once

#include "DataField.h"
#include "Memory.h"

#include <cstdint>

namespace Elite
{

/// The game's state as de-assembled routines see it (ADR-012): the reference's data segment by name
/// (DataOverlay.h) or by offset, the code segment it keeps data and patches in, and the CGA's video
/// memory. It has no registers: a routine that takes a GameState takes its inputs as parameters and
/// gives its results back, and only its entry (Guest) knows the original's register contract.
///
/// It reads and writes through Machine::Memory, so paced time's change count and the comparison's
/// journals see every byte as they did, until Phase 4 puts plain structs behind it (ADR-011, D17).
class GameState
{
public:
  /// The CGA's video memory, which the reference writes directly.
  static constexpr std::uint16_t VIDEO_SEGMENT = 0xB800;

  GameState(Machine::Memory& _memory, std::uint16_t _codeSegment, std::uint16_t _dataSegment) noexcept
    : m_memory(_memory),
      m_codeSegment(_codeSegment),
      m_dataSegment(_dataSegment)
  {
  }

  [[nodiscard]] std::uint16_t CodeSegment() const noexcept
  {
    return m_codeSegment;
  }

  [[nodiscard]] std::uint16_t DataSegment() const noexcept
  {
    return m_dataSegment;
  }

  [[nodiscard]] std::uint8_t Get(DataField<std::uint8_t> _field) const noexcept
  {
    return Byte(_field.offset);
  }

  [[nodiscard]] std::uint16_t Get(DataField<std::uint16_t> _field) const noexcept
  {
    return Word(_field.offset);
  }

  void Set(DataField<std::uint8_t> _field, std::uint8_t _value) noexcept
  {
    SetByte(_field.offset, _value);
  }

  void Set(DataField<std::uint16_t> _field, std::uint16_t _value) noexcept
  {
    SetWord(_field.offset, _value);
  }

  /// The byte at DS:_offset, where DS is the reference's data segment.
  [[nodiscard]] std::uint8_t Byte(std::uint16_t _offset) const noexcept
  {
    return m_memory.Read8(m_dataSegment, _offset);
  }

  /// The word at DS:_offset; at offset FFFFh its high byte is at offset 0, as on the 8088.
  [[nodiscard]] std::uint16_t Word(std::uint16_t _offset) const noexcept
  {
    return m_memory.Read16(m_dataSegment, _offset);
  }

  void SetByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    m_memory.Write8(m_dataSegment, _offset, _value);
  }

  void SetWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    m_memory.Write16(m_dataSegment, _offset, _value);
  }

  /// The byte at _segment:_offset, in any segment.
  [[nodiscard]] std::uint8_t FarByte(std::uint16_t _segment, std::uint16_t _offset) const noexcept
  {
    return m_memory.Read8(_segment, _offset);
  }

  [[nodiscard]] std::uint16_t FarWord(std::uint16_t _segment, std::uint16_t _offset) const noexcept
  {
    return m_memory.Read16(_segment, _offset);
  }

  void SetFarByte(std::uint16_t _segment, std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    m_memory.Write8(_segment, _offset, _value);
  }

  void SetFarWord(std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    m_memory.Write16(_segment, _offset, _value);
  }

  /// The byte at CS:_offset: data the original keeps in its code segment, and the code it patches.
  [[nodiscard]] std::uint8_t CodeByte(std::uint16_t _offset) const noexcept
  {
    return FarByte(m_codeSegment, _offset);
  }

  [[nodiscard]] std::uint16_t CodeWord(std::uint16_t _offset) const noexcept
  {
    return FarWord(m_codeSegment, _offset);
  }

  void SetCodeByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    SetFarByte(m_codeSegment, _offset, _value);
  }

  void SetCodeWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    SetFarWord(m_codeSegment, _offset, _value);
  }

  /// The byte at B800:_offset, in the CGA's video memory.
  [[nodiscard]] std::uint8_t VideoByte(std::uint16_t _offset) const noexcept
  {
    return FarByte(VIDEO_SEGMENT, _offset);
  }

  [[nodiscard]] std::uint16_t VideoWord(std::uint16_t _offset) const noexcept
  {
    return FarWord(VIDEO_SEGMENT, _offset);
  }

  void SetVideoByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    SetFarByte(VIDEO_SEGMENT, _offset, _value);
  }

  void SetVideoWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    SetFarWord(VIDEO_SEGMENT, _offset, _value);
  }

private:
  Machine::Memory& m_memory;
  std::uint16_t m_codeSegment;
  std::uint16_t m_dataSegment;
};

} // namespace Elite
