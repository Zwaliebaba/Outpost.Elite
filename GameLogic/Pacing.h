// GameLogic/Pacing.h
#pragma once

#include "Timing.h"

#include <cstdint>

namespace Machine
{
class Pc;
struct LoadedProgram;
} // namespace Machine

namespace Elite
{

/// Where paced time charges the time the IBM PC's 8088 spent on work the reference sets no pace for, and how
/// much (ADR-013, D18): at CS:offset, cycles, measured on the interpreter clocked as a 4.77 MHz 8088 from the
/// end of one of the loop's waits to the start of the next. Paced time otherwise lets instructions take no
/// time, so a loop whose only wait is the CGA's retrace runs at 60 turns a second.
struct PacingPoint
{
  std::uint16_t offset;
  Machine::Cycles cycles;
};

/// A frame of the galactic chart: the loop's call of PresentChartFrame. 521,504 to 522,342 cycles over 24
/// frames, so each lands on the seventh retrace: 116.8 ms, 8.6 frames a second.
inline constexpr PacingPoint GALACTIC_CHART_PACING{0x0DB1, 521'816};

/// A frame of the short-range chart: the loop's call of PresentChartFrame. 400,233 to 402,216 cycles over 32
/// frames, just past five retraces, so the IBM PC's frames took five or six: 84 to 100 ms.
inline constexpr PacingPoint SHORT_RANGE_CHART_PACING{0x0FD7, 401'339};

/// Every pacing point, on _pc, for the reference loaded as _program. LoadReference installs them.
void InstallPacing(Machine::Pc& _pc, const Machine::LoadedProgram& _program);

} // namespace Elite
