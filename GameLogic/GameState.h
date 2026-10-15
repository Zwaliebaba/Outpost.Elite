// GameLogic/GameState.h
#pragma once

#include "Cga.h"
#include "GameData.h"
#include "SpaceObject.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace Elite
{

class Game;

/// The game's memory (ADR-012, D19, D38): the game's state as its struct (GameData), by member (Data()), and the CGA's video memory. It
/// owns both, and it has no registers: a routine that takes a GameState takes its inputs as parameters and gives its results back.
///
/// The host makes one for the whole run, inside a Game, which starts it (Game::Start). Since change 3c of D38
/// nothing reaches the state by the original's addresses: native code names a member, a run of members that followed one another in the
/// original (BytesThrough, Text.h), or a byte within either. The code segment and the PSP are not here, nor what the game
/// used of the vector table and the BIOS's data area, which is the host's (Host, ADR-041); the segments the game was
/// loaded at are kept for the values that still carry them.
///
/// Until the waits were declared (ADR-039), every write counted in a change count that paced time compared at a loop's turn (ADR-015);
/// nothing counts now. GameData is on the heap, so a GameState can be anywhere, on the stack too.
class GameState
{
public:
  /// The CGA's video memory, which the reference writes directly.
  static constexpr std::uint16_t VIDEO_SEGMENT = 0xB800;

  /// Allocates GameData, zeroed.
  GameState();
  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  /// The game's state by member: `_state.Data().commander.fuel`.
  [[nodiscard]] GameData& Data() noexcept
  {
    return *m_data;
  }

  [[nodiscard]] const GameData& Data() const noexcept
  {
    return *m_data;
  }

  /// The slot of shipSlots numbered _slot (SpaceObject.h). A number past the last slot throws std::out_of_range.
  [[nodiscard]] SpaceObject& Slot(SlotIndex _slot)
  {
    return Data().world.shipSlots[Number(CheckedSlot(_slot))];
  }

  [[nodiscard]] const SpaceObject& Slot(SlotIndex _slot) const
  {
    return Data().world.shipSlots[Number(CheckedSlot(_slot))];
  }

  /// _object's number, a slot of shipSlots; an object that is not one of them throws std::out_of_range.
  [[nodiscard]] SlotIndex IndexOf(const SpaceObject& _object) const
  {
    const auto slots = std::span<const SpaceObject>(Data().world.shipSlots);
    const auto address = reinterpret_cast<std::uintptr_t>(&_object);
    const auto first = reinterpret_cast<std::uintptr_t>(slots.data());
    if (address < first || address - first >= slots.size_bytes() || (address - first) % sizeof(SpaceObject) != 0)
    {
      RefuseSlot();
    }
    return static_cast<SlotIndex>((address - first) / sizeof(SpaceObject));
  }

  /// The byte at B800:_offset, in the CGA's video memory, which must be in its 16 KiB; any other throws std::out_of_range.
  [[nodiscard]] std::uint8_t VideoByte(std::uint16_t _offset) const
  {
    return m_video[CheckedVideo(_offset)];
  }

  /// _value into the byte at B800:_offset.
  void SetVideoByte(std::uint16_t _offset, std::uint8_t _value)
  {
    m_video[CheckedVideo(_offset)] = _value;
  }

  /// _value into the word at B800:_offset, low byte first; at offset FFFFh its high byte is at offset 0, as on the 8088.
  void SetVideoWord(std::uint16_t _offset, std::uint16_t _value)
  {
    SetVideoByte(_offset, static_cast<std::uint8_t>(_value & 0xFF));
    SetVideoByte(static_cast<std::uint16_t>(_offset + 1), static_cast<std::uint8_t>(_value >> 8));
  }

  /// The CGA's 16 KiB of video memory.
  [[nodiscard]] Cga::VideoMemory VideoBytes() const noexcept
  {
    return m_video;
  }

private:
  // Only a Game starts a GameState.
  friend class Game;

  /// Takes the game's state as the program starts (SetStartState). Video memory is left as it is.
  void Start();

  /// _offset, when it is in video memory; otherwise throws std::out_of_range.
  [[nodiscard]] static std::uint16_t CheckedVideo(std::uint16_t _offset)
  {
    if (_offset >= Cga::VIDEO_MEMORY_BYTES)
    {
      RefuseVideo();
    }
    return _offset;
  }

  /// Throws the std::out_of_range of an offset past video memory's end.
  [[noreturn]] static void RefuseVideo();

  /// _slot, when it is one of shipSlots' 36; otherwise throws std::out_of_range.
  [[nodiscard]] static SlotIndex CheckedSlot(SlotIndex _slot)
  {
    if (Number(_slot) >= Number(SlotIndex::End))
    {
      RefuseSlot();
    }
    return _slot;
  }

  /// Throws the std::out_of_range of a slot number past the last slot.
  [[noreturn]] static void RefuseSlot();

  std::unique_ptr<GameData> m_data;
  std::array<std::uint8_t, Cga::VIDEO_MEMORY_BYTES> m_video{};
};

/// The little-endian word at _bytes[_at] and _bytes[_at + 1].
[[nodiscard]] constexpr std::uint16_t WordIn(std::span<const std::uint8_t> _bytes, std::size_t _at) noexcept
{
  return static_cast<std::uint16_t>(_bytes[_at] | (_bytes[_at + 1] << 8));
}

/// _value into _bytes[_at] and _bytes[_at + 1], low byte first, as GameState::SetVideoWord writes a word.
inline void SetWordIn(std::span<std::uint8_t> _bytes, std::size_t _at, std::uint16_t _value) noexcept
{
  _bytes[_at] = static_cast<std::uint8_t>(_value & 0xFF);
  _bytes[_at + 1] = static_cast<std::uint8_t>(_value >> 8);
}

/// The text in _bytes up to its first NUL, or all of them when none is: a buffer's text as a text routine reads it.
[[nodiscard]] inline std::string_view TextIn(std::span<const std::uint8_t> _bytes) noexcept
{
  const auto* const first = reinterpret_cast<const char*>(_bytes.data());
  const std::string_view all(first, _bytes.size());
  return all.substr(0, all.find('\0'));
}

/// _value added to the byte at _field: the sum written, and FFh over it on a carry. Those are two writes, the sum and then
/// FFh, as the original's are.
inline void AddSaturating(std::uint8_t& _field, std::uint8_t _value)
{
  const unsigned sum = _field + unsigned{_value};
  _field = static_cast<std::uint8_t>(sum);
  if (sum > 0xFF)
  {
    _field = 0xFF;
  }
}

} // namespace Elite
