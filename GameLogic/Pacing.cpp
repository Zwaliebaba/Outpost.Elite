#include "pch.h"

#include "Pacing.h"

#include "Pc.h"

#include <array>

namespace Elite
{

void InstallPacing(Machine::Pc& _pc, const Machine::LoadedProgram& _program)
{
  constexpr std::array POINTS = {GALACTIC_CHART_PACING, SHORT_RANGE_CHART_PACING};
  for (const PacingPoint& point : POINTS)
  {
    _pc.SetPacingCost(Machine::Memory::Linear(_program.loadSegment, point.offset), point.cycles);
  }
}

} // namespace Elite
