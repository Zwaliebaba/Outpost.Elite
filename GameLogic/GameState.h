// GameLogic/GameState.h
#pragma once

#include "Cga.h"
#include "DataField.h"
#include "GameData.h"
#include "Memory.h"
#include "Reference.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

class Game;

/// The game's memory (ADR-012, D19): the reference's data segment as its struct (GameData), by name (DataOverlay.h) or by
/// offset; the code segment's image, which the original keeps data and patches in; and the CGA's video memory. It owns all
/// three, and it has no registers: a routine that takes a GameState takes its inputs as parameters and gives its results back.
/// Only the entries the Dispatcher reaches see the registers (Guest).
///
/// The host makes one for the whole run, inside a Game, whose Pc maps the three regions into its address space (Game::Load,
/// Machine::Memory::Map): the devices, DOS and the stack read and write them there. Every write the GameState makes goes
/// through Machine::Memory too, so paced time's change count sees each byte as before (ADR-015). A read changes nothing, so
/// it reads the GameState's own bytes; only a read through Far*, which can land anywhere in the 1 MiB, or past the end of the
/// code or video region, goes through Machine::Memory.
class GameState
{
public:
  /// The CGA's video memory, which the reference writes directly.
  static constexpr std::uint16_t VIDEO_SEGMENT = 0xB800;

  GameState() noexcept = default;
  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

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
    return DataBytes()[_offset];
  }

  /// The word at DS:_offset; at offset FFFFh its high byte is at offset 0, as on the 8088.
  [[nodiscard]] std::uint16_t Word(std::uint16_t _offset) const noexcept
  {
    return static_cast<std::uint16_t>(Byte(_offset) | (Byte(static_cast<std::uint16_t>(_offset + 1)) << 8));
  }

  void SetByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    m_memory->Write8(m_dataSegment, _offset, _value);
  }

  void SetWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    m_memory->Write16(m_dataSegment, _offset, _value);
  }

  /// The byte at _segment:_offset, in any segment.
  [[nodiscard]] std::uint8_t FarByte(std::uint16_t _segment, std::uint16_t _offset) const noexcept
  {
    return m_memory->Read8(_segment, _offset);
  }

  [[nodiscard]] std::uint16_t FarWord(std::uint16_t _segment, std::uint16_t _offset) const noexcept
  {
    return m_memory->Read16(_segment, _offset);
  }

  void SetFarByte(std::uint16_t _segment, std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    m_memory->Write8(_segment, _offset, _value);
  }

  void SetFarWord(std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    m_memory->Write16(_segment, _offset, _value);
  }

  /// The byte at CS:_offset: data the original keeps in its code segment, and the code it patches. Past the code segment's
  /// image, CS:_offset is in the data segment, which follows it, and the byte is read as any far byte is.
  [[nodiscard]] std::uint8_t CodeByte(std::uint16_t _offset) const noexcept
  {
    return _offset < m_code.size() ? m_code[_offset] : FarByte(m_codeSegment, _offset);
  }

  [[nodiscard]] std::uint16_t CodeWord(std::uint16_t _offset) const noexcept
  {
    return static_cast<std::uint16_t>(CodeByte(_offset) | (CodeByte(static_cast<std::uint16_t>(_offset + 1)) << 8));
  }

  void SetCodeByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    SetFarByte(m_codeSegment, _offset, _value);
  }

  void SetCodeWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    SetFarWord(m_codeSegment, _offset, _value);
  }

  /// The byte at B800:_offset, in the CGA's video memory. Past its 16 KiB the byte is read as any far byte is.
  [[nodiscard]] std::uint8_t VideoByte(std::uint16_t _offset) const noexcept
  {
    return _offset < m_video.size() ? m_video[_offset] : FarByte(VIDEO_SEGMENT, _offset);
  }

  void SetVideoByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    SetFarByte(VIDEO_SEGMENT, _offset, _value);
  }

  void SetVideoWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    SetFarWord(VIDEO_SEGMENT, _offset, _value);
  }

  /// The data segment's 64 KiB, GameData's bytes: what the digest hashes, but for the stack (StateDigest.h).
  [[nodiscard]] std::span<const std::uint8_t, sizeof(GameData)> DataBytes() const noexcept
  {
    return std::span<const std::uint8_t, sizeof(GameData)>(reinterpret_cast<const std::uint8_t*>(&m_data), sizeof(GameData));
  }

  /// The CGA's 16 KiB of video memory.
  [[nodiscard]] std::span<const std::uint8_t, Machine::Cga::VIDEO_MEMORY_BYTES> VideoBytes() const noexcept
  {
    return m_video;
  }

private:
  // Only a Game maps a GameState, so the Pc that maps it never outlives it.
  friend class Game;

  /// Maps the three regions into _memory, at the segments the reference was loaded at and at the CGA's video memory, taking
  /// over the bytes the load put there; from then on every write goes through _memory. Throws std::invalid_argument, as
  /// Machine::Memory::Map does, if a region cannot be mapped.
  void Map(Machine::Memory& _memory, std::uint16_t _codeSegment, std::uint16_t _dataSegment)
  {
    static_assert(Machine::Memory::Linear(VIDEO_SEGMENT, 0) == Machine::Cga::VIDEO_MEMORY_LINEAR);
    _memory.Map(Machine::Memory::Linear(_codeSegment, 0), m_code);
    _memory.Map(Machine::Memory::Linear(_dataSegment, 0),
                std::span<std::uint8_t>(reinterpret_cast<std::uint8_t*>(&m_data), sizeof(GameData)));
    _memory.Map(Machine::Memory::Linear(VIDEO_SEGMENT, 0), m_video);
    m_memory = &_memory;
    m_codeSegment = _codeSegment;
    m_dataSegment = _dataSegment;
  }

  GameData m_data{};
  std::array<std::uint8_t, CODE_SEGMENT_BYTES> m_code{};
  std::array<std::uint8_t, Machine::Cga::VIDEO_MEMORY_BYTES> m_video{};
  Machine::Memory* m_memory = nullptr;
  std::uint16_t m_codeSegment = 0;
  std::uint16_t m_dataSegment = 0;
};

/// ADD byte,_value / JAE / MOV byte,0FFh on _field: the sum written, and FFh over it on a carry. Those are two writes, as the
/// original's ADD and MOV are, so paced time's change count sees both (ADR-015).
inline void AddSaturating(GameState& _state, DataField<std::uint8_t> _field, std::uint8_t _value)
{
  const unsigned sum = _state.Get(_field) + unsigned{_value};
  _state.Set(_field, static_cast<std::uint8_t>(sum));
  if (sum > 0xFF)
  {
    _state.Set(_field, 0xFF);
  }
}

} // namespace Elite
