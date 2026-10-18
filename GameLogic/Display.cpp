#include "pch.h"

#include "Display.h"

namespace Elite
{

namespace
{

constexpr std::uint8_t COLOR_SELECT_BITS = 0x3F;
constexpr std::uint8_t BRIGHT_COLOR_OFFSET = 8;

/// Colours 1-3 of the space view's two palettes, before intensity.
constexpr std::array<std::uint8_t, 3> PALETTE_0 = {2, 4, 6}; // green, red, brown
constexpr std::array<std::uint8_t, 3> PALETTE_1 = {3, 5, 7}; // cyan, magenta, light gray

} // namespace

Display::Display(const Milliseconds& _clock) noexcept
  : m_clock(_clock)
{
}

bool Display::TakeRetrace() noexcept
{
  const Milliseconds start = RetraceStart();
  if (start == NO_EVENT)
  {
    return false;
  }
  const bool fresh = start != m_retraceSeen;
  m_retraceSeen = start;
  return fresh;
}

bool Display::InRetrace() const noexcept
{
  return RetraceStart() != NO_EVENT;
}

Milliseconds Display::RetraceStart() const noexcept
{
  const Milliseconds start = FrameStart(FrameAt(m_clock));
  return m_clock < start + RETRACE_MILLISECONDS ? start : NO_EVENT;
}

Milliseconds Display::NextChangeAt() const noexcept
{
  const Milliseconds start = FrameStart(FrameAt(m_clock));
  if (m_clock < start + RETRACE_MILLISECONDS)
  {
    return start + RETRACE_MILLISECONDS;
  }
  return FrameStart(FrameAt(m_clock) + 1);
}

void Display::SetColorSelect(std::uint8_t _value) noexcept
{
  m_colorSelect = static_cast<std::uint8_t>(_value & COLOR_SELECT_BITS);
}

std::array<std::uint8_t, 4> Display::GraphicsColors() const noexcept
{
  const auto background = static_cast<std::uint8_t>(m_colorSelect & COLOR_SELECT_COLOR);
  const std::uint8_t brightness = (m_colorSelect & COLOR_SELECT_INTENSITY) != 0 ? BRIGHT_COLOR_OFFSET : 0;
  const std::array<std::uint8_t, 3>& palette = (m_colorSelect & COLOR_SELECT_PALETTE_1) != 0 ? PALETTE_1 : PALETTE_0;
  return {background, static_cast<std::uint8_t>(palette[0] + brightness), static_cast<std::uint8_t>(palette[1] + brightness),
          static_cast<std::uint8_t>(palette[2] + brightness)};
}

} // namespace Elite
