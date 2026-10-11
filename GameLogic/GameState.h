// GameLogic/GameState.h
#pragma once

#include "Cga.h"
#include "Counted.h"
#include "DataPlaces.h"
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

/// The game's memory (ADR-012, D19, D38): the game's state as its struct (GameData), by member (Data()) or by the original's
/// address; and the CGA's video memory. It owns both, and it has no registers: a routine that takes a GameState takes its inputs
/// as parameters and gives its results back.
///
/// The host makes one for the whole run, inside a Game, which loads the reference's image into it (Game::Load) at the segment
/// DOS would have put it at, so that the original's far addresses, segment and offset, still name its bytes: the data segment at
/// DS:0000, and video memory at B800:0000. A far access resolves its address to one of the two; an address outside them is not
/// the game's memory, and throws std::out_of_range. The code segment, which the original keeps data and patches in, is not here
/// (D33, piece 3): native code reads the only bytes of it it needs from generated tables, and its segment is where the interrupt
/// vectors the game installs point. The vector table, the BIOS's data area and the PSP are not here either: they are the
/// platform's (Platform, Hardware).
///
/// The data segment is distributed (D38): GameData is its groups' structs, not the 1987 layout. The accessors by offset, the far
/// ones and OffsetOf still speak the original's addresses, its 1987 offsets in the data segment, for native code that still
/// reaches the data by offset (change 3b converts it) and for the loops that wait, which carry the offsets the original holds in
/// its registers in their turns (ADR-015). Each such address is found where its byte is now (DATA_PLACES): in GameData, or, for
/// a byte the state does not hold, a constant or a gap nothing uses, in the reference's image, which nothing writes. A write
/// there, or a text or span that would run across from one place to another, throws std::out_of_range.
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

  /// The game's state by member: `_state.Data().commander.fuel`. Every member counts its own changes (Counted).
  [[nodiscard]] GameData& Data() noexcept
  {
    return m_block->data;
  }

  [[nodiscard]] const GameData& Data() const noexcept
  {
    return m_block->data;
  }

  /// The object record at the original's DS:_offset, where its SI or DI points: one of shipSlots' 36 slots, or wherever else it
  /// reads 64 bytes as one. A record whose bytes the state does not hold together throws std::out_of_range, as does one that
  /// would run past the data segment's end, which the original would wrap at 64 KiB.
  [[nodiscard]] SpaceObject& ObjectAt(std::uint16_t _offset)
  {
    return *reinterpret_cast<SpaceObject*>(&DataStorage()[HeldRun(ObjectOffset(_offset), sizeof(SpaceObject))]);
  }

  [[nodiscard]] const SpaceObject& ObjectAt(std::uint16_t _offset) const
  {
    return *reinterpret_cast<const SpaceObject*>(&DataBytes()[HeldRun(ObjectOffset(_offset), sizeof(SpaceObject))]);
  }

  /// The original's offset in the data segment of _object, a record ObjectAt gave: what the original holds in SI or DI for it.
  [[nodiscard]] std::uint16_t OffsetOf(const SpaceObject& _object) const
  {
    return OriginalOffsetOf(reinterpret_cast<const std::uint8_t*>(&_object));
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

  /// _object's number, a slot of shipSlots.
  [[nodiscard]] SlotIndex IndexOf(const SpaceObject& _object) const
  {
    return SlotAt(OffsetOf(_object));
  }

  /// The original's offset in the data segment of the slot numbered _slot, past the last one too: what the original holds in SI
  /// or DI for it, and where a loop that waits carries it in its turns (ADR-015).
  [[nodiscard]] static constexpr std::uint16_t SlotOffset(SlotIndex _slot) noexcept
  {
    return static_cast<std::uint16_t>(SHIP_SLOTS_OFFSET + Number(_slot) * sizeof(SpaceObject));
  }

  /// The number of the slot at the original's DS:_offset, an offset of shipSlots' records, or the end of their last.
  [[nodiscard]] static constexpr SlotIndex SlotAt(std::uint16_t _offset) noexcept
  {
    return static_cast<SlotIndex>((_offset - SHIP_SLOTS_OFFSET) / sizeof(SpaceObject));
  }

  /// The original's offset in the data segment of _member, a member of Data() at any depth, or a byte of a view TextAt or
  /// BytesAt gave: what the original holds in a register for it, where a loop that waits carries it in its turns (ADR-015).
  /// Anything else throws std::out_of_range.
  template <typename Member> [[nodiscard]] std::uint16_t OffsetOf(const Member& _member) const
  {
    return OriginalOffsetOf(reinterpret_cast<const std::uint8_t*>(&_member));
  }

  /// The text that starts at _member, a member of Data(), up to its NUL: as the original reads a text, wherever in the data
  /// segment its NUL is, past the member's end too, where the state holds the bytes after it beside it. The view is of the
  /// state's bytes, as they are when it is taken.
  template <typename Member> [[nodiscard]] std::string_view TextAt(const Member& _member) const
  {
    return TextFrom(OffsetOf(_member));
  }

  /// The text after _text's NUL, up to its own: the next of the texts that follow one another in the data segment, as the
  /// original steps past a NUL to the next. _text is a view TextAt or TextAfter gave.
  [[nodiscard]] std::string_view TextAfter(std::string_view _text) const
  {
    return TextFrom(static_cast<std::uint16_t>(OffsetOf(*(_text.data() + _text.size())) + 1));
  }

  /// The text at the original's DS:_offset up to its NUL, for a text the symbol table names no row for: a buffer inside a row
  /// or a gap, which change 3b of D38 names. Every other text is a member's (TextAt) or a constant of the subsystem's generated
  /// tables (Tools/GenerateTables.py).
  [[nodiscard]] std::string_view TextAtOffset(std::uint16_t _offset) const
  {
    return TextFrom(_offset);
  }

  /// _count bytes from _member's first, a member of Data() at any depth, as bytes that count their own changes: a buffer a text
  /// routine writes into, past the member's end too where the symbol table sizes it short and the state holds what follows
  /// beside it. Bytes the state does not hold together throw std::out_of_range.
  template <typename Member> [[nodiscard]] std::span<Counted<std::uint8_t>> BytesFrom(Member& _member, std::size_t _count)
  {
    return BytesAt(OffsetOf(_member), _count);
  }

  /// BytesFrom, to read.
  template <typename Member> [[nodiscard]] std::span<const Counted<std::uint8_t>> BytesFrom(const Member& _member, std::size_t _count) const
  {
    return BytesAt(OffsetOf(_member), _count);
  }

  /// _count bytes from the original's DS:_offset, as BytesFrom gives them, for a buffer the symbol table names no row for
  /// (TextAtOffset). Bytes the state does not hold together throw std::out_of_range.
  [[nodiscard]] std::span<Counted<std::uint8_t>> BytesAt(std::uint16_t _offset, std::size_t _count)
  {
    return std::span<Counted<std::uint8_t>>(reinterpret_cast<Counted<std::uint8_t>*>(&DataStorage()[HeldRun(_offset, _count)]), _count);
  }

  /// BytesAt, to read: bytes the state holds together, or bytes of the reference's image it does not hold, which never change.
  [[nodiscard]] std::span<const Counted<std::uint8_t>> BytesAt(std::uint16_t _offset, std::size_t _count) const
  {
    return std::span<const Counted<std::uint8_t>>(reinterpret_cast<const Counted<std::uint8_t>*>(ReadRun(_offset, _count)), _count);
  }

  /// The word at the start of _member, a member of Data() at any depth whose first word the symbol table does not name on its
  /// own, such as creditsTenths's low word.
  template <typename Member> [[nodiscard]] std::uint16_t WordOf(const Member& _member) const
  {
    return Word(OffsetOf(_member));
  }

  /// _value into the word at the start of _member, as WordOf reads it, each byte counted when it changes.
  template <typename Member> void SetWordOf(Member& _member, std::uint16_t _value)
  {
    SetWord(OffsetOf(_member), _value);
  }

  /// The text _into bytes into _member, a member of Data(), up to its NUL, past the member's end too: a text that follows other
  /// bytes in a record, such as a docked frame's title after its two attributes.
  template <typename Member> [[nodiscard]] std::string_view TextAt(const Member& _member, std::uint16_t _into) const
  {
    return TextFrom(static_cast<std::uint16_t>(OffsetOf(_member) + _into));
  }

  /// The byte at the original's DS:_offset, where DS is the reference's data segment.
  [[nodiscard]] std::uint8_t Byte(std::uint16_t _offset) const noexcept
  {
    return *ReadAt(_offset);
  }

  /// The word at the original's DS:_offset; at offset FFFFh its high byte is at offset 0, as on the 8088.
  [[nodiscard]] std::uint16_t Word(std::uint16_t _offset) const noexcept
  {
    return static_cast<std::uint16_t>(Byte(_offset) | (Byte(static_cast<std::uint16_t>(_offset + 1)) << 8));
  }

  /// _value into the byte at the original's DS:_offset, which the state must hold; a byte it does not throws std::out_of_range.
  void SetByte(std::uint16_t _offset, std::uint8_t _value)
  {
    Store(WriteAt(_offset), _value);
  }

  void SetWord(std::uint16_t _offset, std::uint16_t _value)
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

  /// The 20-bit address of _segment:_offset.
  [[nodiscard]] static constexpr std::uint32_t Linear(std::uint16_t _segment, std::uint16_t _offset) noexcept
  {
    return ((static_cast<std::uint32_t>(_segment) << 4) + _offset) & ADDRESS_MASK;
  }

private:
  // Only a Game loads a GameState.
  friend class Game;

  static constexpr std::uint32_t ADDRESS_MASK = 0xFFFFF;
  static constexpr std::uint16_t SHIP_SLOTS_OFFSET = OriginalOffset("shipSlots");
  static constexpr std::uint32_t VIDEO_LINEAR = std::uint32_t{VIDEO_SEGMENT} << 4;
  static constexpr std::uint32_t SEGMENT_BYTES = 0x10000;

  /// Where each of the original's addresses in the data segment is now, and where each byte of GameData was (DATA_PLACES): the
  /// same for every GameState, built once.
  struct OriginalLayout
  {
    /// The value of places for a byte the state does not hold: it is the reference's image's.
    static constexpr std::uint32_t IN_IMAGE = 0xFFFFFFFF;

    /// For each of the original's offsets in the data segment, its byte's offset in GameData, or IN_IMAGE.
    std::array<std::uint32_t, SEGMENT_BYTES> places;
    /// For each of the original's offsets, the original's offset where the bytes laid out as they were from it end: the end
    /// of the run of the data segment that is in one place, in GameData or in the image, a byte after another.
    std::array<std::uint32_t, SEGMENT_BYTES> runEnds;
    /// For each offset in GameData, its byte's original offset.
    std::array<std::uint16_t, sizeof(GameData)> originals;
    /// The data segment as the reference's image holds it, zeros past it: the bytes the state does not hold, which nothing
    /// writes.
    std::array<std::uint8_t, SEGMENT_BYTES> image;
  };

  /// Builds the original's layout from DATA_PLACES and the reference's image, once, for every GameState.
  [[nodiscard]] static const OriginalLayout& BuildOriginalLayout();

  /// Takes the game's state as DOS would have loaded the reference with its code segment at _codeSegment, uncounted: _state,
  /// GameData's bytes as ReferenceLoadImage lays them out, from the data segment's image. Video memory is left as it is. The
  /// data segment is paragraph DATA_SEGMENT_PARAGRAPH on from _codeSegment.
  void Load(std::uint16_t _codeSegment, std::span<const std::uint8_t> _state) noexcept;

  [[nodiscard]] std::span<std::uint8_t, sizeof(GameData)> DataStorage() noexcept
  {
    return std::span<std::uint8_t, sizeof(GameData)>(reinterpret_cast<std::uint8_t*>(&m_block->data), sizeof(GameData));
  }

  /// The byte at the original's DS:_offset, to read: in GameData, or the image's when the state does not hold it.
  [[nodiscard]] const std::uint8_t* ReadAt(std::uint16_t _offset) const noexcept
  {
    const std::uint32_t place = m_layout->places[_offset];
    return place == OriginalLayout::IN_IMAGE ? &m_layout->image[_offset] : &DataBytes()[place];
  }

  /// The byte at the original's DS:_offset, to write: in GameData. A byte the state does not hold throws std::out_of_range.
  [[nodiscard]] std::uint8_t& WriteAt(std::uint16_t _offset)
  {
    const std::uint32_t place = m_layout->places[_offset];
    if (place == OriginalLayout::IN_IMAGE)
    {
      RefuseWrite(_offset);
    }
    return DataStorage()[place];
  }

  /// The place in GameData of the _count bytes from the original's DS:_offset, which the state must hold, a byte after another;
  /// otherwise throws std::out_of_range.
  [[nodiscard]] std::uint32_t HeldRun(std::uint16_t _offset, std::size_t _count) const
  {
    const std::uint32_t place = m_layout->places[_offset];
    if (place == OriginalLayout::IN_IMAGE || _offset + _count > m_layout->runEnds[_offset])
    {
      RefuseRun(_offset, _count);
    }
    return place;
  }

  /// The first of the _count bytes from the original's DS:_offset, which must be in one place, a byte after another: in
  /// GameData or in the image. Otherwise throws std::out_of_range.
  [[nodiscard]] const std::uint8_t* ReadRun(std::uint16_t _offset, std::size_t _count) const
  {
    if (_offset + _count > m_layout->runEnds[_offset])
    {
      RefuseRun(_offset, _count);
    }
    return ReadAt(_offset);
  }

  /// The original's offset of _byte, a byte of GameData or of the image; anything else throws std::out_of_range.
  [[nodiscard]] std::uint16_t OriginalOffsetOf(const std::uint8_t* _byte) const
  {
    const auto address = reinterpret_cast<std::uintptr_t>(_byte);
    if (const std::uintptr_t into = address - reinterpret_cast<std::uintptr_t>(DataBytes().data()); into < sizeof(GameData))
    {
      return m_layout->originals[into];
    }
    if (const std::uintptr_t into = address - reinterpret_cast<std::uintptr_t>(m_layout->image.data()); into < SEGMENT_BYTES)
    {
      return static_cast<std::uint16_t>(into);
    }
    RefuseOriginal();
  }

  /// The text at the original's DS:_offset, up to its NUL, which must be in one place with it; or up to the data segment's end.
  [[nodiscard]] std::string_view TextFrom(std::uint16_t _offset) const
  {
    const std::uint32_t end = m_layout->runEnds[_offset];
    const std::span<const std::uint8_t> rest(ReadAt(_offset), end - _offset);
    const auto nul = std::ranges::find(rest, std::uint8_t{0});
    if (nul == rest.end() && end < SEGMENT_BYTES)
    {
      RefuseRun(_offset, rest.size() + 1);
    }
    return std::string_view(reinterpret_cast<const char*>(rest.data()), static_cast<std::size_t>(nul - rest.begin()));
  }

  /// _offset, when a record there ends inside the data segment; otherwise throws std::out_of_range.
  [[nodiscard]] static std::uint16_t ObjectOffset(std::uint16_t _offset)
  {
    if (_offset > SEGMENT_BYTES - sizeof(SpaceObject))
    {
      RefuseObject();
    }
    return _offset;
  }

  /// Throws the std::out_of_range of an object record past the data segment's end.
  [[noreturn]] static void RefuseObject();

  /// Throws the std::out_of_range of a write to the original's DS:_offset, a byte the state does not hold.
  [[noreturn]] static void RefuseWrite(std::uint16_t _offset);

  /// Throws the std::out_of_range of _count bytes from the original's DS:_offset that are not in one place.
  [[noreturn]] static void RefuseRun(std::uint16_t _offset, std::size_t _count);

  /// Throws the std::out_of_range of an address that is neither the state's nor the image's.
  [[noreturn]] static void RefuseOriginal();

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

  /// Which region holds the byte at _linear, and where in it: the original's offset in the data segment, or the offset in video
  /// memory. Throws std::out_of_range for an address in neither. Inline, and a subtraction and one comparison a region, the
  /// data segment first: every far access of the game's comes here, the copy of the picture to video memory most of all.
  [[nodiscard]] Place Locate(std::uint32_t _linear) const
  {
    const std::uint32_t linear = _linear & ADDRESS_MASK;
    if (const std::uint32_t index = linear - Linear(m_dataSegment, 0); index < SEGMENT_BYTES)
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

  /// Where the byte at _linear is: in GameData or video memory. Throws std::out_of_range for any other address, and for a byte
  /// of the data segment the state does not hold.
  [[nodiscard]] std::uint8_t* At(std::uint32_t _linear)
  {
    const Place place = Locate(_linear);
    return place.region == Region::Data ? &WriteAt(static_cast<std::uint16_t>(place.index)) : &m_video[place.index];
  }

  /// Where the byte at _linear is, to read: in GameData, the image or video memory. Throws std::out_of_range for any other
  /// address.
  [[nodiscard]] const std::uint8_t* At(std::uint32_t _linear) const
  {
    const Place place = Locate(_linear);
    return place.region == Region::Data ? ReadAt(static_cast<std::uint16_t>(place.index)) : &m_video[place.index];
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
  const OriginalLayout* m_layout;
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
