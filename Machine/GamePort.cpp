#include "pch.h"

#include "GamePort.h"

#include <algorithm>

namespace Machine
{

namespace
{

constexpr std::uint8_t BUTTON_SHIFT = 4;
constexpr std::uint8_t BUTTON_BITS = 0xF0;

} // namespace

GamePort::GamePort(const Cycles& _clock) noexcept
  : m_clock(_clock)
{
}

void GamePort::SetAxisResistance(std::size_t _axis, std::uint32_t _ohms) noexcept
{
  if (_axis >= AXIS_COUNT)
  {
    return;
  }
  m_ohms[_axis] = _ohms == AXIS_DISCONNECTED ? _ohms : std::min(_ohms, MAXIMUM_OHMS);
}

void GamePort::SetButton(std::size_t _button, bool _pressed) noexcept
{
  if (_button >= BUTTON_COUNT)
  {
    return;
  }
  const auto bit = static_cast<std::uint8_t>(1u << _button);
  m_buttonsPressed = static_cast<std::uint8_t>(_pressed ? m_buttonsPressed | bit : m_buttonsPressed & ~bit);
}

std::uint8_t GamePort::In8(std::uint16_t /*_port*/) noexcept
{
  auto value = static_cast<std::uint8_t>(~(m_buttonsPressed << BUTTON_SHIFT) & BUTTON_BITS);
  for (std::size_t axis = 0; axis < AXIS_COUNT; ++axis)
  {
    if (m_clock < m_timeoutAt[axis])
    {
      value = static_cast<std::uint8_t>(value | (1u << axis));
    }
  }
  return value;
}

void GamePort::Out8(std::uint16_t /*_port*/, std::uint8_t /*_value*/) noexcept
{
  for (std::size_t axis = 0; axis < AXIS_COUNT; ++axis)
  {
    if (m_clock < m_timeoutAt[axis])
    {
      continue; // still timing: the 558 ignores the trigger
    }
    m_timeoutAt[axis] = m_ohms[axis] == AXIS_DISCONNECTED ? NEVER : m_clock + OneShotCycles(m_ohms[axis]);
  }
}

} // namespace Machine
