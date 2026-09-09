#include "pch.h"

#include "FrameClock.h"

namespace Outpost
{

  namespace
  {
    constexpr std::int64_t NANOSECONDS_PER_SECOND = 1'000'000'000;
  } // namespace

  std::int64_t FrameClock::Tick() noexcept
  {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if (!m_started)
    {
      m_started = true;
      m_last = now;
      return 0;
    }

    std::chrono::nanoseconds elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_last);
    m_last = now;

    if (elapsed.count() <= 0)
    {
      return 0;
    }
    if (elapsed > MAX_ELAPSED)
    {
      elapsed = MAX_ELAPSED;
    }

    /*
     * Nanoseconds to cycles, with the remainder kept for the next turn.
     *
     * The multiply is bounded by `MAX_ELAPSED`: sixty seconds of nanoseconds by a megahertz is
     * about 6.1e16, which is a hundredth of what a signed 64-bit integer holds, so the clamp above
     * is what makes this arithmetic safe rather than a comment claiming it is.
     */
    const std::int64_t scaled = elapsed.count() * static_cast<std::int64_t>(m_timing.clockHz) + m_carryNanoseconds;
    m_carryNanoseconds = scaled % NANOSECONDS_PER_SECOND;
    return scaled / NANOSECONDS_PER_SECOND;
  }

} // namespace Outpost
