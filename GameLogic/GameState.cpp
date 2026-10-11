#include "pch.h"

#include "GameState.h"

#include <algorithm>
#include <stdexcept>

namespace Elite
{

void GameState::Load(std::uint16_t _codeSegment, std::span<const std::uint8_t> _image) noexcept
{
  m_codeSegment = _codeSegment;
  m_dataSegment = static_cast<std::uint16_t>(_codeSegment + DATA_SEGMENT_PARAGRAPH);
  const std::span<std::uint8_t, sizeof(GameData)> data = DataStorage();
  std::ranges::fill(m_code, std::uint8_t{0});
  std::ranges::fill(data, std::uint8_t{0});
  const std::span<const std::uint8_t> code = _image.first(std::min<std::size_t>(_image.size(), m_code.size()));
  std::ranges::copy(code, m_code.begin());
  if (_image.size() > m_code.size())
  {
    const std::span<const std::uint8_t> rest = _image.subspan(m_code.size());
    std::ranges::copy(rest.first(std::min<std::size_t>(rest.size(), data.size())), data.begin());
  }
}

void GameState::RefuseAddress()
{
  throw std::out_of_range("GameState: an address outside the game's code segment, data segment and video memory");
}

} // namespace Elite
