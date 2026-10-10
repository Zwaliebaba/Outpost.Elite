#include "pch.h"

#include "Game.h"

#include "NativeRoutines.h"

namespace Elite
{

Game::Game(Machine::FileStore& _files, const Machine::Pc::Desc& _desc)
  : m_pc(_files, _desc)
{
}

bool Game::Load(std::uint16_t _pspSegment)
{
  if (!LoadReference(m_pc, _pspSegment, m_program))
  {
    return false;
  }
  m_state.Map(m_pc.Ram(), m_program.loadSegment, DataSegment(m_program));
  InstallNativeRoutines(m_pc, m_state);
  return true;
}

} // namespace Elite
