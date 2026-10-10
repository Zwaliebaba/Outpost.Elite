// GameLogic/Guest.h
#pragma once

#include "DataField.h"
#include "Pc.h"

#include <cstdint>

namespace Elite
{

/// The machine as a native routine sees it (ADR-010): the registers, the reference's data segment by
/// name (DataOverlay.h) or by offset, and the way back into the original's code. One is made for each
/// call of a native routine, around the Pc that runs the program; everything it reads and writes goes
/// through that Pc, so the replays, the journals and paced time's wait detection see it.
class Guest
{
public:
  Guest(Machine::Pc& _pc, std::uint16_t _dataSegment) noexcept
    : m_pc(_pc),
      m_dataSegment(_dataSegment)
  {
  }

  [[nodiscard]] Machine::Registers& Regs() noexcept
  {
    return m_pc.Processor().Regs();
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return m_pc;
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
    return m_pc.Ram().Read8(m_dataSegment, _offset);
  }

  /// The word at DS:_offset; at offset FFFFh its high byte is at offset 0, as on the 8088.
  [[nodiscard]] std::uint16_t Word(std::uint16_t _offset) const noexcept
  {
    return m_pc.Ram().Read16(m_dataSegment, _offset);
  }

  void SetByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    m_pc.Ram().Write8(m_dataSegment, _offset, _value);
  }

  void SetWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    m_pc.Ram().Write16(m_dataSegment, _offset, _value);
  }

  /// Sets or clears _flag (FLAG_* in Registers.h): what a routine does for a flag its callers read.
  void SetFlag(std::uint16_t _flag, bool _set) noexcept
  {
    std::uint16_t& flags = Regs().flags;
    flags = static_cast<std::uint16_t>(_set ? (flags | _flag) : (flags & ~_flag));
  }

  /// Calls the original routine at CS:_offset and runs it to its return (Pc::CallNear).
  void Call(std::uint16_t _offset)
  {
    m_pc.CallNear(_offset);
  }

private:
  Machine::Pc& m_pc;
  std::uint16_t m_dataSegment;
};

} // namespace Elite
