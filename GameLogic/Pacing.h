// GameLogic/Pacing.h
#pragma once

#include "Timing.h"

namespace Elite
{

/// Where paced time charges the time the IBM PC's 8088 spent on work the reference sets no pace for, and how
/// much (ADR-013, D18): measured on the interpreter clocked as a 4.77 MHz 8088 from the end of one of the loop's
/// waits to the start of the next, and held as whole milliseconds since D87. Paced time otherwise lets work take
/// no time, so a loop whose only wait is the retrace runs at 60 turns a second. The native code that stands in for
/// the instruction pays the time there (Hardware::Spend).
struct PacingPoint
{
  Milliseconds milliseconds;
};

/// A frame of the galactic chart: the loop's call of PresentChartFrame, at CS:0DB1. 521,504 to 522,342 cycles
/// over 24 frames, 109.3 ms, so each lands on the seventh retrace: 116.7 ms, 8.6 frames a second.
inline constexpr PacingPoint GALACTIC_CHART_PACING{109};

/// A frame of the short-range chart: the loop's call of PresentChartFrame, at CS:0FD7. 400,233 to 402,216 cycles
/// over 32 frames, 84.1 ms, just past five retraces, so the IBM PC's frames took five or six: 84 to 100 ms.
inline constexpr PacingPoint SHORT_RANGE_CHART_PACING{84};

} // namespace Elite
