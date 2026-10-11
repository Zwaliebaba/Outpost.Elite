#include "pch.h"

#include "Platform.h"

namespace Elite
{

Platform::Platform(GameState& _state, Machine::FileStore& _files, const Machine::Dos::DateTime& _startMoment) noexcept
  : m_files(_state, _files, _startMoment)
{
}

void Platform::StartProgram(std::uint16_t _pspSegment) noexcept
{
  m_files.StartProgram(_pspSegment);
}

} // namespace Elite
