#include "pch.h"

#include "GameState.h"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace Elite
{

GameState::GameState()
  : m_block(new (::operator new(sizeof(Block), std::align_val_t{COUNTED_BLOCK_ALIGNMENT})) Block{})
{
}

void GameState::FreeBlock::operator()(Block* _block) const noexcept
{
  _block->~Block();
  ::operator delete(_block, std::align_val_t{COUNTED_BLOCK_ALIGNMENT});
}

void GameState::Load(std::uint16_t _codeSegment, std::span<const std::uint8_t> _image) noexcept
{
  m_codeSegment = _codeSegment;
  m_dataSegment = static_cast<std::uint16_t>(_codeSegment + DATA_SEGMENT_PARAGRAPH);
  const std::span<std::uint8_t, sizeof(GameData)> data = DataStorage();
  std::ranges::fill(data, std::uint8_t{0});
  std::ranges::copy(_image.first(std::min<std::size_t>(_image.size(), data.size())), data.begin());
}

void GameState::RefuseAddress()
{
  throw std::out_of_range("GameState: an address outside the game's data segment and video memory");
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
