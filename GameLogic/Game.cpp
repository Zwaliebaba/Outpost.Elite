#include "pch.h"

#include "Game.h"

#include "NativeRoutines.h"

namespace Elite
{

Game::Game(FileStore& _files, const Platform::Desc& _desc)
  : m_platform(m_state, _files, _desc)
{
}

void Game::Start(std::uint16_t _pspSegment)
{
  m_state.Start(static_cast<std::uint16_t>(_pspSegment + PSP_PARAGRAPHS));
  m_platform.StartProgram();
  InstallNativeRoutines(m_platform, m_state);
}

} // namespace Elite
