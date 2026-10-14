#include "pch.h"

#include "Hardware.h"

#include <span>
#include <stdexcept>
#include <string_view>

namespace Elite
{

namespace
{

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

void Hardware::SetTickRunning(bool _running)
{
  m_platform.Events().SetTickRunning(_running);
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

KeyEvent Hardware::TakeKey() noexcept
{
  return m_platform.Events().TakeKey();
}

Mouse& Hardware::MouseDevice() noexcept
{
  return m_platform.MouseDevice();
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
  return m_platform.Spend(_point.milliseconds);
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

bool Hardware::CheatRequested() const noexcept
{
  return m_platform.CheatRequested();
}

} // namespace Elite
