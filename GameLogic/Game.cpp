#include "pch.h"

#include "Game.h"

#include "NativeRoutines.h"

namespace Elite
{

Game::Game(FileStore& _files, const Platform::Desc& _desc)
  : m_platform(m_state, _files, _desc)
{
}

bool Game::Load(std::uint16_t _pspSegment)
{
  const auto loadSegment = static_cast<std::uint16_t>(_pspSegment + PSP_PARAGRAPHS);
  const std::optional<std::vector<std::uint8_t>> image = ReferenceLoadImage();
  if (!image)
  {
    return false;
  }
  m_state.Load(loadSegment, *image);
  m_platform.StartProgram();
  InstallNativeRoutines(m_platform, m_state);
  return true;
}

} // namespace Elite
