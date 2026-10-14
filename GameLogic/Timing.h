// GameLogic/Timing.h
#pragma once

#include <cstdint>
#include <limits>

namespace Elite
{

/// Time inside the game is counted in whole milliseconds since power-on, and nothing else: no device reads the host's clock,
/// so a run is a function of its inputs (ADR-005, D87).
using Milliseconds = std::uint64_t;

/// The game's tick: its handler's music, effects and counters run once a millisecond while it is installed (D87).
inline constexpr Milliseconds TICK_MILLISECONDS = 1;

/// The display's frames, 60 a second (D87): frame n starts at FrameStart(n), n * 1000 / 60 rounded down, so frames are 16 or 17
/// ms long and three of them 50 ms.
inline constexpr std::uint64_t FRAMES_PER_SECOND = 60;

/// The time of an event that is not coming: what a device's "next event" query returns when it has nothing scheduled.
inline constexpr Milliseconds NO_EVENT = std::numeric_limits<Milliseconds>::max();

/// When frame _frame starts.
[[nodiscard]] constexpr Milliseconds FrameStart(std::uint64_t _frame) noexcept
{
  return _frame * 1'000 / FRAMES_PER_SECOND;
}

/// The frame in progress at _time: the last that starts at or before it.
[[nodiscard]] constexpr std::uint64_t FrameAt(Milliseconds _time) noexcept
{
  return (_time * FRAMES_PER_SECOND + FRAMES_PER_SECOND - 1) / 1'000;
}

} // namespace Elite
