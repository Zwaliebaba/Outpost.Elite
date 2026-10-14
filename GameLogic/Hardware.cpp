#include "pch.h"

#include "Hardware.h"

#include <span>
#include <stdexcept>
#include <string_view>

namespace Elite
{

namespace
{

constexpr std::uint8_t KEYBOARD_ACKNOWLEDGE = 0x80; // port 61h bit 7, the XT's PB7

constexpr std::uint16_t MOUSE_BUTTON_PRESSES = 0x0005;

constexpr std::uint8_t CRTC_CURSOR_ADDRESS_HIGH = 0x0E;

} // namespace

std::uint8_t Hardware::SystemControl()
{
  return m_platform.SystemControl();
}

void Hardware::SetSystemControl(std::uint8_t _value)
{
  m_platform.SetSystemControl(_value);
}

void Hardware::SetToneDivisor(std::uint16_t _divisor)
{
  m_platform.Sound().SetTone(_divisor);
}

void Hardware::SetTickDivisor(std::uint16_t _divisor)
{
  m_platform.Events().SetTickDivisor(_divisor);
  m_platform.TakeDueInterrupts();
}

void Hardware::EndOfInterrupt()
{
  m_platform.Events().EndOfInterrupt();
  m_platform.TakeDueInterrupts();
}

void Hardware::EnableInterrupts()
{
  m_platform.EnableInterrupts();
}

void Hardware::DisableInterrupts() noexcept
{
  m_platform.DisableInterrupts();
}

Platform::RunAwaiter Hardware::Wait()
{
  return m_platform.Wait();
}

void Hardware::Spin()
{
  m_platform.Spin();
}

std::uint8_t Hardware::KeyboardData()
{
  return m_platform.Events().ScanCode();
}

void Hardware::AcknowledgeKeyboard()
{
  const auto pulse = static_cast<std::uint8_t>(m_platform.SystemControl() | KEYBOARD_ACKNOWLEDGE);
  m_platform.SetSystemControl(pulse);
  m_platform.SetSystemControl(static_cast<std::uint8_t>(pulse & ~KEYBOARD_ACKNOWLEDGE));
}

// The mouse driver's calls: the driver's when a mouse is there; otherwise int 33h reaches what its vector names, the ROM's IRET,
// and the registers come back as they went (Platform::CallAbsentMouse).

void Hardware::ResetMouse()
{
  Mouse& mouse = m_platform.MouseDriver();
  if (!mouse.Present())
  {
    m_platform.CallAbsentMouse();
    return;
  }
  mouse.Reset();
}

std::uint16_t Hardware::ReadMousePresses()
{
  const Mouse& mouse = m_platform.MouseDriver();
  if (!mouse.Present())
  {
    m_platform.CallAbsentMouse();
    return MOUSE_BUTTON_PRESSES;
  }
  return mouse.Buttons();
}

MouseMotion Hardware::ReadMouseMotion()
{
  Mouse& mouse = m_platform.MouseDriver();
  if (!mouse.Present())
  {
    m_platform.CallAbsentMouse();
    return MouseMotion{0, 0};
  }
  return mouse.Motion();
}

void Hardware::SetVideoMode(CgaMode _mode) noexcept
{
  m_platform.Video().SetMode(_mode);
}

std::uint8_t Hardware::CgaStatus()
{
  return m_platform.Video().ReadStatus();
}

void Hardware::SetColorSelect(std::uint8_t _value)
{
  m_platform.Video().SetColorSelect(_value);
}

void Hardware::SetCursorAddressHigh(std::uint8_t _value)
{
  m_platform.Video().WriteCrtc(CRTC_CURSOR_ADDRESS_HIGH, _value);
}

void Hardware::SetModeControl(std::uint8_t _value)
{
  m_platform.Video().SetModeControl(_value);
}

FileStore& Hardware::Files() noexcept
{
  return m_platform.Files();
}

FileStamp Hardware::FileStampNow() const
{
  return m_platform.FileStampNow();
}

Task<void> Hardware::Spend(const PacingPoint& _point)
{
  return m_platform.Spend(_point.cycles);
}

// ── What the emulated PC kept in memory, outside the game's (D33) ──

IrqHandler Hardware::Handler(Irq _irq) const noexcept
{
  return m_platform.Handler(_irq);
}

void Hardware::SetHandler(Irq _irq, IrqHandler _handler) noexcept
{
  m_platform.SetHandler(_irq, _handler);
}

bool Hardware::MouseDriverLoaded() const noexcept
{
  return m_platform.MouseDriverLoaded();
}

bool Hardware::CheatRequested() const noexcept
{
  return m_platform.CheatRequested();
}

} // namespace Elite
