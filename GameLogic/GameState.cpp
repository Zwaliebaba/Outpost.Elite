#include "pch.h"

#include "GameState.h"

#include "ReferenceImage.h"

#include <algorithm>
#include <format>
#include <new>
#include <stdexcept>
#include <vector>

namespace Elite
{

GameState::GameState()
  : m_block(new (::operator new(sizeof(Block), std::align_val_t{COUNTED_BLOCK_ALIGNMENT})) Block{}),
    m_layout(&BuildOriginalLayout())
{
}

void GameState::FreeBlock::operator()(Block* _block) const noexcept
{
  _block->~Block();
  ::operator delete(_block, std::align_val_t{COUNTED_BLOCK_ALIGNMENT});
}

const GameState::OriginalLayout& GameState::BuildOriginalLayout()
{
  static const std::unique_ptr<const OriginalLayout> LAYOUT = []
  {
    auto layout = std::make_unique<OriginalLayout>();
    layout->places.fill(OriginalLayout::IN_IMAGE);
    std::vector<bool> placed(sizeof(GameData), false);
    for (const DataPlace& place : DATA_PLACES)
    {
      if (place.place == NO_PLACE)
      {
        continue;
      }
      for (std::uint32_t index = 0; index < place.bytes; ++index)
      {
        if (placed[place.place + index])
        {
          throw std::logic_error(std::format("GameState: {} shares a byte of GameData with a member before it", place.name));
        }
        placed[place.place + index] = true;
        layout->places[place.original + index] = place.place + index;
        layout->originals[place.place + index] = static_cast<std::uint16_t>(place.original + index);
      }
    }
    if (!std::ranges::all_of(placed, [](bool _placed) { return _placed; }))
    {
      throw std::logic_error("GameState: a byte of GameData is no member of the data segment's");
    }
    // Each run ends where the next byte is somewhere else: in the other place, or in GameData but not the byte after.
    std::uint32_t end = SEGMENT_BYTES;
    for (std::uint32_t offset = SEGMENT_BYTES; offset-- > 0;)
    {
      if (offset + 1 < SEGMENT_BYTES)
      {
        const std::uint32_t here = layout->places[offset];
        const std::uint32_t next = layout->places[offset + 1];
        const bool together =
          here == OriginalLayout::IN_IMAGE ? next == OriginalLayout::IN_IMAGE : next != OriginalLayout::IN_IMAGE && next == here + 1;
        if (!together)
        {
          end = offset + 1;
        }
      }
      layout->runEnds[offset] = end;
    }
    // The data segment's image, past the code segment's, and zeros past it.
    const std::span<const std::uint8_t> data = std::span<const std::uint8_t>(REFERENCE_IMAGE).subspan(CODE_SEGMENT_BYTES);
    layout->image.fill(0);
    std::ranges::copy(data.first(std::min<std::size_t>(data.size(), SEGMENT_BYTES)), layout->image.begin());
    return std::unique_ptr<const OriginalLayout>(std::move(layout));
  }();
  return *LAYOUT;
}

void GameState::Load(std::uint16_t _codeSegment, std::span<const std::uint8_t> _state) noexcept
{
  m_codeSegment = _codeSegment;
  m_dataSegment = static_cast<std::uint16_t>(_codeSegment + DATA_SEGMENT_PARAGRAPH);
  const std::span<std::uint8_t, sizeof(GameData)> data = DataStorage();
  std::ranges::fill(data, std::uint8_t{0});
  std::ranges::copy(_state.first(std::min<std::size_t>(_state.size(), data.size())), data.begin());
}

void GameState::RefuseAddress()
{
  throw std::out_of_range("GameState: an address outside the game's data segment and video memory");
}

void GameState::RefuseWrite(std::uint16_t _offset)
{
  throw std::out_of_range(std::format("GameState: a write to DS:{:04X}, which the state does not hold", _offset));
}

void GameState::RefuseRun(std::uint16_t _offset, std::size_t _count)
{
  throw std::out_of_range(std::format("GameState: {} bytes from DS:{:04X}, which are not in one place", _count, _offset));
}

void GameState::RefuseOriginal()
{
  throw std::out_of_range("GameState: an address of neither the game's state nor the reference's image");
}

void GameState::RefuseSlot()
{
  throw std::out_of_range("a slot number past shipSlots' last");
}

void GameState::RefuseObject()
{
  throw std::out_of_range("GameState: an object record past the data segment's end");
}

} // namespace Elite
