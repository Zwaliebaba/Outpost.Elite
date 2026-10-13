// GameLogic/GameState.h
#pragma once

#include "Cga.h"
#include "Counted.h"
#include "GameData.h"
#include "Reference.h"
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
/// The host makes one for the whole run, inside a Game, which loads the reference's image into it (Game::Load). Since change 3c of D38
/// nothing reaches the state by the original's addresses: native code names a member, a run of members that followed one another in 1987
/// (BytesThrough, Text.h), or a byte within either. The code segment, the vector table, the BIOS's data area and the PSP are
/// not here: they are the platform's (Platform, Hardware), and the segments the game was loaded at are kept for the devices that still take
/// far addresses.
///
/// Every write counts in one change count (ChangeCount) when it changes what it writes, as the emulated PC's memory did until D33: paced
/// time compared the count at each turn of a loop, and a turn that changed nothing idled (ADR-015). Since the waits are declared
/// (ADR-039), nothing reads it. A member of GameData counts its own
/// writes (Counted): the data segment is in a block of its own, aligned to COUNTED_BLOCK_ALIGNMENT, with the count first, where a member
/// finds it from its own address. Video memory's writes count in the same count. A read changes nothing and counts nothing.
class GameState
{
public:
  /// The CGA's video memory, which the reference writes directly.
  static constexpr std::uint16_t VIDEO_SEGMENT = 0xB800;

  /// Allocates the data segment's block, zeroed, with a count of 0. The block is on the heap, so a GameState can be anywhere,
  /// on the stack too.
  GameState();
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

  /// The game's state by member: `_state.Data().commander.fuel`. Every member counts its own changes (Counted).
  [[nodiscard]] GameData& Data() noexcept
  {
    return m_block->data;
  }

  [[nodiscard]] const GameData& Data() const noexcept
  {
    return m_block->data;
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

  /// _value into the byte at B800:_offset, counted when it changes it.
  void SetVideoByte(std::uint16_t _offset, std::uint8_t _value)
  {
    Store(m_video[CheckedVideo(_offset)], _value);
  }

  /// _value into the word at B800:_offset, low byte first; at offset FFFFh its high byte is at offset 0, as on the 8088.
  void SetVideoWord(std::uint16_t _offset, std::uint16_t _value)
  {
    SetVideoByte(_offset, static_cast<std::uint8_t>(_value & 0xFF));
    SetVideoByte(static_cast<std::uint16_t>(_offset + 1), static_cast<std::uint8_t>(_value >> 8));
  }

  /// GameData's bytes, the game's state: what the state digest hashes (StateDigest.h).
  [[nodiscard]] std::span<const std::uint8_t, sizeof(GameData)> DataBytes() const noexcept
  {
    return std::span<const std::uint8_t, sizeof(GameData)>(reinterpret_cast<const std::uint8_t*>(&m_block->data), sizeof(GameData));
  }

  /// The CGA's 16 KiB of video memory.
  [[nodiscard]] Cga::VideoMemory VideoBytes() const noexcept
  {
    return m_video;
  }

  /// Writes that changed a byte, counted since construction. A write of the value already there is not counted, so a loop
  /// that only stores what it found leaves the count alone: paced time uses that to tell a program that is waiting from one
  /// that is working (ADR-015).
  [[nodiscard]] std::uint64_t ChangeCount() const noexcept
  {
    return m_block->changes;
  }

private:
  // Only a Game loads a GameState.
  friend class Game;

  /// Takes the game's state as DOS would have loaded the reference with its code segment at _codeSegment, uncounted: _state,
  /// GameData's bytes as ReferenceLoadImage lays them out, from the data segment's image. Video memory is left as it is. The
  /// data segment is paragraph DATA_SEGMENT_PARAGRAPH on from _codeSegment.
  void Load(std::uint16_t _codeSegment, std::span<const std::uint8_t> _state) noexcept;

  [[nodiscard]] std::span<std::uint8_t, sizeof(GameData)> DataStorage() noexcept
  {
    return std::span<std::uint8_t, sizeof(GameData)>(reinterpret_cast<std::uint8_t*>(&m_block->data), sizeof(GameData));
  }

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

  /// The game's state and its change count, in storage aligned to COUNTED_BLOCK_ALIGNMENT, so that a member of GameData finds
  /// the count by rounding its own address down (ChangeCountAt).
  struct Block
  {
    std::uint64_t changes;
    GameData data;
  };

  static_assert(sizeof(Block) <= COUNTED_BLOCK_ALIGNMENT, "a member's address rounded down must find its own block's start");

  /// Frees a Block from its aligned storage.
  struct FreeBlock
  {
    void operator()(Block* _block) const noexcept;
  };

  void Store(std::uint8_t& _byte, std::uint8_t _value) noexcept
  {
    if (_byte != _value)
    {
      _byte = _value;
      ++m_block->changes;
    }
  }

  std::unique_ptr<Block, FreeBlock> m_block;
  std::array<std::uint8_t, Cga::VIDEO_MEMORY_BYTES> m_video{};
  std::uint16_t m_codeSegment = 0;
  std::uint16_t m_dataSegment = 0;
};

/// The little-endian word at _bytes[_at] and _bytes[_at + 1].
[[nodiscard]] inline std::uint16_t WordIn(std::span<const Counted<std::uint8_t>> _bytes, std::size_t _at) noexcept
{
  return static_cast<std::uint16_t>(_bytes[_at] | (_bytes[_at + 1] << 8));
}

/// The little-endian word at _bytes[_at] and _bytes[_at + 1] of a constant's table (D38).
[[nodiscard]] constexpr std::uint16_t WordIn(std::span<const std::uint8_t> _bytes, std::size_t _at) noexcept
{
  return static_cast<std::uint16_t>(_bytes[_at] | (_bytes[_at + 1] << 8));
}

/// _value into _bytes[_at] and _bytes[_at + 1], low byte first, each counted when it changes, as GameState::SetWord writes a
/// word.
inline void SetWordIn(std::span<Counted<std::uint8_t>> _bytes, std::size_t _at, std::uint16_t _value) noexcept
{
  _bytes[_at] = static_cast<std::uint8_t>(_value & 0xFF);
  _bytes[_at + 1] = static_cast<std::uint8_t>(_value >> 8);
}

/// The text in _bytes up to its first NUL, or all of them when none is: a buffer's text as a text routine reads it.
[[nodiscard]] inline std::string_view TextIn(std::span<const Counted<std::uint8_t>> _bytes) noexcept
{
  const auto* const first = reinterpret_cast<const char*>(_bytes.data());
  const std::string_view all(first, _bytes.size());
  return all.substr(0, all.find('\0'));
}

/// ADD byte,_value / JAE / MOV byte,0FFh on _field: the sum written, and FFh over it on a carry. Those are two writes, as the
/// original's ADD and MOV are, each counted when it changes the byte (ADR-015).
inline void AddSaturating(Counted<std::uint8_t>& _field, std::uint8_t _value)
{
  const unsigned sum = _field + unsigned{_value};
  _field = static_cast<std::uint8_t>(sum);
  if (sum > 0xFF)
  {
    _field = 0xFF;
  }
}

} // namespace Elite
