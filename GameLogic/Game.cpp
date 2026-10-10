#include "pch.h"

#include "Game.h"

#include "NativeRoutines.h"

namespace Elite
{

Game::Game(Machine::FileStore& _files, const Machine::Pc::Desc& _desc)
  : m_pc(_files, _desc)
{
}

ReferenceFailure Game::Load(std::span<const std::uint8_t> _file, std::uint16_t _pspSegment)
{
  const ReferenceFailure failure = LoadReference(m_pc, _file, _pspSegment, m_program);
  if (failure != ReferenceFailure::None)
  {
    return failure;
  }
  m_state.Map(m_pc.Ram(), m_program.loadSegment, DataSegment(m_program));
  InstallNativeRoutines(m_pc, m_state);
  return ReferenceFailure::None;
}

} // namespace Elite
