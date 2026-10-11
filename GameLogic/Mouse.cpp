#include "pch.h"

#include "Mouse.h"

namespace Elite
{

Mouse::Mouse(bool _present) noexcept
  : m_present(_present)
{
}

void Mouse::Move(std::int32_t _mickeysX, std::int32_t _mickeysY) noexcept
{
  m_motionX = static_cast<std::uint16_t>(m_motionX + static_cast<std::uint32_t>(_mickeysX));
  m_motionY = static_cast<std::uint16_t>(m_motionY + static_cast<std::uint32_t>(_mickeysY));
}

void Mouse::SetButtons(std::uint8_t _held) noexcept
{
  m_held = static_cast<std::uint8_t>(_held & (BUTTON_LEFT | BUTTON_RIGHT));
}

void Mouse::Reset() noexcept
{
  m_motionX = 0;
  m_motionY = 0;
}

std::uint16_t Mouse::Buttons() const noexcept
{
  return m_held;
}

MouseMotion Mouse::Motion() noexcept
{
  const MouseMotion motion{m_motionX, m_motionY};
  m_motionX = 0;
  m_motionY = 0;
  return motion;
}

} // namespace Elite
