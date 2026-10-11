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

/// The game's memory (ADR-012, D19): the reference's data segment as its struct (GameData), by member (Data()) or by offset;
/// and the CGA's video memory. It owns both, and it has no registers: a routine that takes a GameState takes its inputs as
/// parameters and gives its results back.
///
/// The host makes one for the whole run, inside a Game, which loads the reference's data segment into it (Game::Load) at the
/// segment DOS would have put it at, so that the original's far addresses, segment and offset, still name its bytes: the data
/// segment and the stack in it at DS:0000, and video memory at B800:0000. A far access resolves its address to one of the two;
/// an address outside them is not the game's memory, and throws std::out_of_range. The code segment, which the original keeps
/// data and patches in, is not here (D33, piece 3): native code reads the only bytes of it it needs from generated tables, and
/// its segment is where the interrupt vectors the game installs point. The vector table, the BIOS's data area and the PSP are not
/// here either: they are the platform's (Platform, Hardware).
///
/// Every write counts in one change count (ChangeCount) when it changes what it writes, as the emulated PC's memory did until
/// D33: paced time compares the count at each turn of a loop, and a turn that changed nothing idles (ADR-015). A member of
/// GameData counts its own writes (Counted): the data segment is in a block of its own, aligned to COUNTED_BLOCK_ALIGNMENT,
/// with the count first, where a member finds it from its own address. The accessors by offset count each byte whose value
/// they change. A read changes nothing and counts nothing.
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

  /// The data segment by member: `_state.Data().commanderBlock.fuel`. Every member counts its own changes (Counted).
  [[nodiscard]] GameData& Data() noexcept
  {
    return m_block->data;
  }

  [[nodiscard]] const GameData& Data() const noexcept
  {
    return m_block->data;
  }

  /// The object record at DS:_offset, where the original's SI or DI points: one of shipSlots' 36 slots, or wherever else it
  /// reads 64 bytes as one. A record that would run past the data segment's end, which the original would wrap at 64 KiB,
  /// throws std::out_of_range.
  [[nodiscard]] SpaceObject& ObjectAt(std::uint16_t _offset)
  {
    return *reinterpret_cast<SpaceObject*>(&DataStorage()[ObjectOffset(_offset)]);
  }

  [[nodiscard]] const SpaceObject& ObjectAt(std::uint16_t _offset) const
  {
    return *reinterpret_cast<const SpaceObject*>(&DataBytes()[ObjectOffset(_offset)]);
  }

  /// The offset in the data segment of _object, a record ObjectAt gave: what the original holds in SI or DI for it.
  [[nodiscard]] std::uint16_t OffsetOf(const SpaceObject& _object) const noexcept
  {
    return static_cast<std::uint16_t>(reinterpret_cast<const std::uint8_t*>(&_object) - DataBytes().data());
  }

  /// The slot of shipSlots numbered _slot (SpaceObject.h). A number past the last slot throws std::out_of_range.
  [[nodiscard]] SpaceObject& Slot(SlotIndex _slot)
  {
    return ObjectAt(SlotOffset(CheckedSlot(_slot)));
  }

  [[nodiscard]] const SpaceObject& Slot(SlotIndex _slot) const
  {
    return ObjectAt(SlotOffset(CheckedSlot(_slot)));
  }

  /// _object's number, a slot of shipSlots.
  [[nodiscard]] SlotIndex IndexOf(const SpaceObject& _object) const noexcept
  {
    return SlotAt(OffsetOf(_object));
  }

  /// The offset in the data segment of the slot numbered _slot, past the last one too: what the original holds in SI or DI
  /// for it, and where a loop that waits carries it in its turns (ADR-015).
  [[nodiscard]] static constexpr std::uint16_t SlotOffset(SlotIndex _slot) noexcept
  {
    return static_cast<std::uint16_t>(SHIP_SLOTS_OFFSET + Number(_slot) * sizeof(SpaceObject));
  }

  /// The number of the slot at DS:_offset, an offset of shipSlots' records, or the end of their last.
  [[nodiscard]] static constexpr SlotIndex SlotAt(std::uint16_t _offset) noexcept
  {
    return static_cast<SlotIndex>((_offset - SHIP_SLOTS_OFFSET) / sizeof(SpaceObject));
  }

  /// The offset in the data segment of _member, a member of Data() at any depth: what the original holds in a register for
  /// it, where a loop that waits carries it in its turns (ADR-015).
  template <typename Member> [[nodiscard]] std::uint16_t OffsetOf(const Member& _member) const noexcept
  {
    return static_cast<std::uint16_t>(reinterpret_cast<const std::uint8_t*>(&_member) - DataBytes().data());
  }

  /// The text that starts at _member, a member of Data(), up to its NUL: as the original reads a text, wherever in the data
  /// segment its NUL is, past the member's end too. The view is of the data segment's bytes, as they are when it is taken.
  template <typename Member> [[nodiscard]] std::string_view TextAt(const Member& _member) const noexcept
  {
    return TextFrom(OffsetOf(_member));
  }

  /// The text after _text's NUL, up to its own: the next of the texts that follow one another in the data segment, as the
  /// original steps past a NUL to the next. _text is a view TextAt or TextAfter gave.
  [[nodiscard]] std::string_view TextAfter(std::string_view _text) const noexcept
  {
    const auto* const end = reinterpret_cast<const std::uint8_t*>(_text.data() + _text.size());
    return TextFrom(static_cast<std::uint16_t>(end - DataBytes().data() + 1));
  }

  /// The text at DS:_offset up to its NUL, for a text the game writes that the symbol table names no row for: a buffer inside
  /// a row or a gap, which change 3 of D33's piece 3 names (D33). Every other text is a member's (TextAt) or a constant of the
  /// subsystem's generated tables (Tools/GenerateTables.py).
  [[nodiscard]] std::string_view TextAtOffset(std::uint16_t _offset) const noexcept
  {
    return TextFrom(_offset);
  }

  /// _count bytes of the data segment from _member's first, a member of Data() at any depth, as bytes that count their own
  /// changes: a buffer a text routine writes into, past the member's end too where the symbol table sizes it short. A span
  /// that would run past the data segment's end throws std::out_of_range.
  template <typename Member> [[nodiscard]] std::span<Counted<std::uint8_t>> BytesFrom(Member& _member, std::size_t _count)
  {
    return BytesAt(OffsetOf(_member), _count);
  }

  /// BytesFrom, to read.
  template <typename Member> [[nodiscard]] std::span<const Counted<std::uint8_t>> BytesFrom(const Member& _member, std::size_t _count) const
  {
    return BytesAt(OffsetOf(_member), _count);
  }

  /// _count bytes of the data segment from DS:_offset, as BytesFrom gives them, for a buffer the symbol table names no row for
  /// (TextAtOffset).
  [[nodiscard]] std::span<Counted<std::uint8_t>> BytesAt(std::uint16_t _offset, std::size_t _count)
  {
    if (_offset + _count > sizeof(GameData))
    {
      RefuseObject();
    }
    return std::span<Counted<std::uint8_t>>(reinterpret_cast<Counted<std::uint8_t>*>(&DataStorage()[_offset]), _count);
  }

  /// BytesAt, to read.
  [[nodiscard]] std::span<const Counted<std::uint8_t>> BytesAt(std::uint16_t _offset, std::size_t _count) const
  {
    if (_offset + _count > sizeof(GameData))
    {
      RefuseObject();
    }
    return std::span<const Counted<std::uint8_t>>(reinterpret_cast<const Counted<std::uint8_t>*>(&DataBytes()[_offset]), _count);
  }

  /// The word at the start of _member, a member of Data() at any depth whose first word the symbol table does not name on its
  /// own, such as creditsTenths's low word.
  template <typename Member> [[nodiscard]] std::uint16_t WordOf(const Member& _member) const noexcept
  {
    return Word(OffsetOf(_member));
  }

  /// _value into the word at the start of _member, as WordOf reads it, each byte counted when it changes.
  template <typename Member> void SetWordOf(Member& _member, std::uint16_t _value) noexcept
  {
    SetWord(OffsetOf(_member), _value);
  }

  /// The text _into bytes into _member, a member of Data(), up to its NUL, past the member's end too: a text that follows other
  /// bytes in a record, such as a docked frame's title after its two attributes.
  template <typename Member> [[nodiscard]] std::string_view TextAt(const Member& _member, std::uint16_t _into) const noexcept
  {
    return TextFrom(static_cast<std::uint16_t>(OffsetOf(_member) + _into));
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

  /// The 20-bit address of _segment:_offset.
  [[nodiscard]] static constexpr std::uint32_t Linear(std::uint16_t _segment, std::uint16_t _offset) noexcept
  {
    return ((static_cast<std::uint32_t>(_segment) << 4) + _offset) & ADDRESS_MASK;
  }

private:
  // Only a Game loads a GameState.
  friend class Game;

  static constexpr std::uint32_t ADDRESS_MASK = 0xFFFFF;
  static constexpr std::uint16_t SHIP_SLOTS_OFFSET = GAME_DATA_OFFSET(shipSlots);
  static constexpr std::uint32_t VIDEO_LINEAR = std::uint32_t{VIDEO_SEGMENT} << 4;

  /// Takes the reference's data segment as DOS would have loaded it with its code segment at _codeSegment, uncounted: _image,
  /// the data segment's image, from DS:0000, which is paragraph DATA_SEGMENT_PARAGRAPH on from _codeSegment, with zeros past the
  /// image's end. Video memory is left as it is.
  void Load(std::uint16_t _codeSegment, std::span<const std::uint8_t> _image) noexcept;

  [[nodiscard]] std::span<std::uint8_t, sizeof(GameData)> DataStorage() noexcept
  {
    return std::span<std::uint8_t, sizeof(GameData)>(reinterpret_cast<std::uint8_t*>(&m_block->data), sizeof(GameData));
  }

  /// The text at DS:_offset, up to its NUL or the data segment's end.
  [[nodiscard]] std::string_view TextFrom(std::uint16_t _offset) const noexcept
  {
    const std::span<const std::uint8_t> rest = DataBytes().subspan(_offset);
    const auto nul = std::ranges::find(rest, std::uint8_t{0});
    return std::string_view(reinterpret_cast<const char*>(rest.data()), static_cast<std::size_t>(nul - rest.begin()));
  }

  /// _offset, when a record there ends inside the data segment; otherwise throws std::out_of_range.
  [[nodiscard]] static std::uint16_t ObjectOffset(std::uint16_t _offset)
  {
    if (_offset > sizeof(GameData) - sizeof(SpaceObject))
    {
      RefuseObject();
    }
    return _offset;
  }

  /// Throws the std::out_of_range of an object record past the data segment's end.
  [[noreturn]] static void RefuseObject();

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

  /// The data segment and its change count, in storage aligned to COUNTED_BLOCK_ALIGNMENT, so that a member of GameData finds
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

  enum class Region : std::uint8_t
  {
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
    RefuseAddress();
  }

  /// Throws the std::out_of_range of an address outside the game's memory.
  [[noreturn]] static void RefuseAddress();

  /// Where the byte at _linear is: in the data segment or video memory. Throws std::out_of_range for any other address.
  [[nodiscard]] std::uint8_t* At(std::uint32_t _linear)
  {
    const Place place = Locate(_linear);
    return place.region == Region::Data ? &DataStorage()[place.index] : &m_video[place.index];
  }

  [[nodiscard]] const std::uint8_t* At(std::uint32_t _linear) const
  {
    const Place place = Locate(_linear);
    return place.region == Region::Data ? &DataBytes()[place.index] : &m_video[place.index];
  }

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
