#include "pch.h"

#include "GameState.h"

#include "StartState.h"
#include "StateWriter.h"

#include <stdexcept>

namespace Elite
{

GameState::GameState()
  : m_data(std::make_unique<GameData>())
{
}

void GameState::Start(std::uint16_t _codeSegment)
{
  m_codeSegment = _codeSegment;
  m_dataSegment = static_cast<std::uint16_t>(_codeSegment + DATA_SEGMENT_PARAGRAPH);
  ZeroMembers(*m_data);
  SetStartState(*m_data);
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
