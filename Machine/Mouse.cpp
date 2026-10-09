#include "pch.h"

#include "Mouse.h"

#include <algorithm>

namespace Machine
{

namespace
{

// The driver's default mickey-to-pixel ratios: 8 mickeys per 8 pixels across, 16 per 8 down.
constexpr std::int32_t MICKEYS_PER_PIXEL_X = 1;
constexpr std::int32_t MICKEYS_PER_PIXEL_Y = 2;
constexpr std::int32_t MAX_MICKEYS_X = (Mouse::SCREEN_WIDTH_PIXELS - 1) * MICKEYS_PER_PIXEL_X;
constexpr std::int32_t MAX_MICKEYS_Y = (Mouse::SCREEN_HEIGHT_PIXELS - 1) * MICKEYS_PER_PIXEL_Y;
constexpr std::uint16_t INSTALLED = 0xFFFF;

} // namespace

Mouse::Mouse(bool _present) noexcept
  : m_present(_present)
{
  Reset();
}

void Mouse::Move(std::int32_t _mickeysX, std::int32_t _mickeysY) noexcept
{
  m_motionX = static_cast<std::uint16_t>(m_motionX + static_cast<std::uint32_t>(_mickeysX));
  m_motionY = static_cast<std::uint16_t>(m_motionY + static_cast<std::uint32_t>(_mickeysY));
  m_positionMickeysX = static_cast<std::int32_t>(std::clamp<std::int64_t>(std::int64_t{m_positionMickeysX} + _mickeysX, 0, MAX_MICKEYS_X));
  m_positionMickeysY = static_cast<std::int32_t>(std::clamp<std::int64_t>(std::int64_t{m_positionMickeysY} + _mickeysY, 0, MAX_MICKEYS_Y));
}

void Mouse::SetButtons(std::uint8_t _held) noexcept
{
  for (std::uint16_t button = 0; button < BUTTON_COUNT; ++button)
  {
    const auto bit = static_cast<std::uint8_t>(1u << button);
    if ((_held & bit) != 0 && (m_held & bit) == 0)
    {
      if (m_presses[button] != 0xFFFF)
      {
        ++m_presses[button];
      }
      m_pressX[button] = static_cast<std::uint16_t>(m_positionMickeysX / MICKEYS_PER_PIXEL_X);
      m_pressY[button] = static_cast<std::uint16_t>(m_positionMickeysY / MICKEYS_PER_PIXEL_Y);
    }
  }
  m_held = static_cast<std::uint8_t>(_held & (BUTTON_LEFT | BUTTON_RIGHT));
}

std::optional<FaultKind> Mouse::Call(Registers& _regs) noexcept
{
  switch (_regs.ax)
  {
  case 0x0000:
    Reset();
    _regs.ax = INSTALLED;
    _regs.bx = BUTTON_COUNT;
    return std::nullopt;
  case 0x0005:
  {
    const std::uint16_t button = _regs.bx;
    if (button >= BUTTON_COUNT)
    {
      return FaultKind::UnsupportedRequest;
    }
    _regs.ax = m_held;
    _regs.bx = m_presses[button];
    _regs.cx = m_pressX[button];
    _regs.dx = m_pressY[button];
    m_presses[button] = 0;
    return std::nullopt;
  }
  case 0x000B:
    _regs.cx = m_motionX;
    _regs.dx = m_motionY;
    m_motionX = 0;
    m_motionY = 0;
    return std::nullopt;
  default:
    return FaultKind::UnknownFunction;
  }
}

void Mouse::Reset() noexcept
{
  m_motionX = 0;
  m_motionY = 0;
  m_positionMickeysX = SCREEN_WIDTH_PIXELS / 2 * MICKEYS_PER_PIXEL_X;
  m_positionMickeysY = SCREEN_HEIGHT_PIXELS / 2 * MICKEYS_PER_PIXEL_Y;
  m_presses.fill(0);
  m_pressX.fill(0);
  m_pressY.fill(0);
}

} // namespace Machine
