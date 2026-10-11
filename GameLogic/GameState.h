// GameLogic/GameState.h
#pragma once

#include "Cga.h"
#include "DataField.h"
#include "GameData.h"
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
///
/// The host makes one for the whole run, inside a Game, which loads the reference into it (Game::Load) at the segments DOS
/// would have put it at, so that the original's far addresses, segment and offset, still name its bytes: the code segment
/// at CS:0000, the data segment and the stack in it at DS:0000, and video memory at B800:0000. A far access resolves its
/// address to one of the three; an address outside them is not the game's memory, and throws std::out_of_range. The vector
/// table, the BIOS's data area and the PSP are not here: they are the platform's (Platform, Hardware).
///
/// Every write goes through one path that counts a byte whose value it changes (ChangeCount), as the emulated PC's memory did
/// until D33: paced time compares the count at each turn of a loop, and a turn that changed nothing idles (ADR-015). A read
/// changes nothing and counts nothing.
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
    Store(DataStorage()[_offset], _value);
  }

  void SetWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    SetByte(_offset, static_cast<std::uint8_t>(_value & 0xFF));
    SetByte(static_cast<std::uint16_t>(_offset + 1), static_cast<std::uint8_t>(_value >> 8));
  }

  /// The byte at _segment:_offset, which must be in the game's memory.
  [[nodiscard]] std::uint8_t FarByte(std::uint16_t _segment, std::uint16_t _offset) const
  {
    return *At(Linear(_segment, _offset));
  }

  /// The word at _segment:_offset; at offset FFFFh its high byte is at offset 0 of the same segment, as on the 8088.
  [[nodiscard]] std::uint16_t FarWord(std::uint16_t _segment, std::uint16_t _offset) const
  {
    return static_cast<std::uint16_t>(FarByte(_segment, _offset) | (FarByte(_segment, static_cast<std::uint16_t>(_offset + 1)) << 8));
  }

  void SetFarByte(std::uint16_t _segment, std::uint16_t _offset, std::uint8_t _value)
  {
    Store(*At(Linear(_segment, _offset)), _value);
  }

  void SetFarWord(std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _value)
  {
    SetFarByte(_segment, _offset, static_cast<std::uint8_t>(_value & 0xFF));
    SetFarByte(_segment, static_cast<std::uint16_t>(_offset + 1), static_cast<std::uint8_t>(_value >> 8));
  }

  /// The byte at the 20-bit address _linear, which must be in the game's memory: what DOS's reads and writes by linear address
  /// reach (DosFiles).
  [[nodiscard]] std::uint8_t LinearByte(std::uint32_t _linear) const
  {
    return *At(_linear);
  }

  void SetLinearByte(std::uint32_t _linear, std::uint8_t _value)
  {
    Store(*At(_linear), _value);
  }

  /// The byte at CS:_offset: data the original keeps in its code segment, and the code it patches. Past the code segment's
  /// image, CS:_offset is in the data segment, which follows it.
  [[nodiscard]] std::uint8_t CodeByte(std::uint16_t _offset) const
  {
    return _offset < m_code.size() ? m_code[_offset] : FarByte(m_codeSegment, _offset);
  }

  [[nodiscard]] std::uint16_t CodeWord(std::uint16_t _offset) const
  {
    return static_cast<std::uint16_t>(CodeByte(_offset) | (CodeByte(static_cast<std::uint16_t>(_offset + 1)) << 8));
  }

  void SetCodeByte(std::uint16_t _offset, std::uint8_t _value)
  {
    SetFarByte(m_codeSegment, _offset, _value);
  }

  void SetCodeWord(std::uint16_t _offset, std::uint16_t _value)
  {
    SetFarWord(m_codeSegment, _offset, _value);
  }

  /// The byte at B800:_offset, in the CGA's video memory, which must be in its 16 KiB.
  [[nodiscard]] std::uint8_t VideoByte(std::uint16_t _offset) const
  {
    return _offset < m_video.size() ? m_video[_offset] : FarByte(VIDEO_SEGMENT, _offset);
  }

  void SetVideoByte(std::uint16_t _offset, std::uint8_t _value)
  {
    SetFarByte(VIDEO_SEGMENT, _offset, _value);
  }

  void SetVideoWord(std::uint16_t _offset, std::uint16_t _value)
  {
    SetFarWord(VIDEO_SEGMENT, _offset, _value);
  }

  /// The data segment's 64 KiB, GameData's bytes: what the digest hashes, but for the stack (StateDigest.h).
  [[nodiscard]] std::span<const std::uint8_t, sizeof(GameData)> DataBytes() const noexcept
  {
    return std::span<const std::uint8_t, sizeof(GameData)>(reinterpret_cast<const std::uint8_t*>(&m_data), sizeof(GameData));
  }

  /// The CGA's 16 KiB of video memory.
  [[nodiscard]] Cga::VideoMemory VideoBytes() const noexcept
  {
    return m_video;
  }

  /// The code segment's image.
  [[nodiscard]] std::span<const std::uint8_t, CODE_SEGMENT_BYTES> CodeBytes() const noexcept
  {
    return m_code;
  }

  /// Writes that changed a byte, counted since construction. A write of the value already there is not counted, so a loop
  /// that only stores what it found leaves the count alone: paced time uses that to tell a program that is waiting from one
  /// that is working (ADR-015).
  [[nodiscard]] std::uint64_t ChangeCount() const noexcept
  {
    return m_changes;
  }

  /// The 20-bit address of _segment:_offset.
  [[nodiscard]] static constexpr std::uint32_t Linear(std::uint16_t _segment, std::uint16_t _offset) noexcept
  {
    return ((static_cast<std::uint32_t>(_segment) << 4) + _offset) & ADDRESS_MASK;
  }

private:
  // Only a Game loads a GameState.
  friend class Game;

  static constexpr std::uint32_t ADDRESS_MASK = 0xFFFFF;
  static constexpr std::uint32_t VIDEO_LINEAR = std::uint32_t{VIDEO_SEGMENT} << 4;

  /// Takes the reference's load image, the code segment and then the data segment's image, as DOS would have put it at
  /// _codeSegment, uncounted: the code segment's image from _image's start, and the data segment, from paragraph
  /// DATA_SEGMENT_PARAGRAPH of it, with zeros past the image's end. Video memory is left as it is.
  void Load(std::uint16_t _codeSegment, std::span<const std::uint8_t> _image) noexcept;

  [[nodiscard]] std::span<std::uint8_t, sizeof(GameData)> DataStorage() noexcept
  {
    return std::span<std::uint8_t, sizeof(GameData)>(reinterpret_cast<std::uint8_t*>(&m_data), sizeof(GameData));
  }

  enum class Region : std::uint8_t
  {
    Code,
    Data,
    Video
  };

  struct Place
  {
    Region region;
    std::uint32_t index;
  };

  /// Which region holds the byte at _linear, and where in it. Throws std::out_of_range for an address in none of them. Inline,
  /// and a subtraction and one comparison a region, the data segment first: every far access of the game's comes here, the
  /// copy of the picture to video memory most of all.
  [[nodiscard]] Place Locate(std::uint32_t _linear) const
  {
    const std::uint32_t linear = _linear & ADDRESS_MASK;
    if (const std::uint32_t index = linear - Linear(m_dataSegment, 0); index < sizeof(GameData))
    {
      return Place{Region::Data, index};
    }
    if (const std::uint32_t index = linear - VIDEO_LINEAR; index < Cga::VIDEO_MEMORY_BYTES)
    {
      return Place{Region::Video, index};
    }
    if (const std::uint32_t index = linear - Linear(m_codeSegment, 0); index < CODE_SEGMENT_BYTES)
    {
      return Place{Region::Code, index};
    }
    RefuseAddress();
  }

  /// Throws the std::out_of_range of an address outside the game's memory.
  [[noreturn]] static void RefuseAddress();

  /// Where the byte at _linear is: in the code segment's image, the data segment or video memory. Throws std::out_of_range
  /// for any other address.
  [[nodiscard]] std::uint8_t* At(std::uint32_t _linear)
  {
    const Place place = Locate(_linear);
    switch (place.region)
    {
    case Region::Data:
      return &DataStorage()[place.index];
    case Region::Video:
      return &m_video[place.index];
    case Region::Code:
      break;
    }
    return &m_code[place.index];
  }

  [[nodiscard]] const std::uint8_t* At(std::uint32_t _linear) const
  {
    const Place place = Locate(_linear);
    switch (place.region)
    {
    case Region::Data:
      return &DataBytes()[place.index];
    case Region::Video:
      return &m_video[place.index];
    case Region::Code:
      break;
    }
    return &m_code[place.index];
  }

  void Store(std::uint8_t& _byte, std::uint8_t _value) noexcept
  {
    if (_byte != _value)
    {
      _byte = _value;
      ++m_changes;
    }
  }

  GameData m_data{};
  std::array<std::uint8_t, CODE_SEGMENT_BYTES> m_code{};
  std::array<std::uint8_t, Cga::VIDEO_MEMORY_BYTES> m_video{};
  std::uint16_t m_codeSegment = 0;
  std::uint16_t m_dataSegment = 0;
  std::uint64_t m_changes = 0;
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
