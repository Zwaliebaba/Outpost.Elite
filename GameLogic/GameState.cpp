#include "pch.h"

#include "GameState.h"

#include <algorithm>
#include <stdexcept>

namespace Elite
{

GameState::GameState()
  : m_data(std::make_unique<GameData>())
{
}

void GameState::Load(std::uint16_t _codeSegment, std::span<const std::uint8_t> _state) noexcept
{
  m_codeSegment = _codeSegment;
  m_dataSegment = static_cast<std::uint16_t>(_codeSegment + DATA_SEGMENT_PARAGRAPH);
  const std::span<std::uint8_t, sizeof(GameData)> data = DataStorage();
  std::ranges::fill(data, std::uint8_t{0});
  std::ranges::copy(_state.first(std::min<std::size_t>(_state.size(), data.size())), data.begin());
}

void GameState::RefuseVideo()
{
  throw std::out_of_range("GameState: an offset past the CGA's 16 KiB of video memory");
}

void GameState::RefuseSlot()
{
  throw std::out_of_range("a slot number past shipSlots' last");
}

} // namespace Elite
