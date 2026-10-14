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

constexpr char DOS_STRING_TERMINATOR = '$'; // int 21h AH=09h's

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

void Hardware::Wait()
{
  m_platform.Wait();
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

std::uint8_t Hardware::GamePortButtons()
{
  return m_platform.Joystick().Read();
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

void Hardware::SetVideoMode(std::uint8_t _mode)
{
  if (!m_platform.Modes().SetMode(_mode))
  {
    throw std::logic_error("Hardware::SetVideoMode: a mode the game never sets");
  }
}

void Hardware::PrintDosString(std::string_view _text)
{
  // Each character up to the '$' through the BIOS's teletype, as DOS writes them.
  const std::size_t end = _text.find(DOS_STRING_TERMINATOR);
  if (end == std::string_view::npos)
  {
    throw std::logic_error("Hardware::PrintDosString: a text with no '$', which DOS would print on past");
  }
  for (const char character : _text.substr(0, end))
  {
    m_platform.Modes().WriteTeletype(static_cast<std::uint8_t>(character));
  }
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

void Hardware::SelectPalette(std::uint8_t _palette)
{
  m_platform.Modes().SelectPalette(_palette);
}

void Hardware::FireGamePort()
{
  m_platform.Joystick().Fire();
}

std::uint8_t Hardware::GamePortOneShots()
{
  return m_platform.Joystick().Read();
}

void Hardware::CountInstructionCycles(Cycles _cycles) noexcept
{
  m_platform.CountInstructionCycles(_cycles);
}

ClockReading Hardware::ReadTimeOfDay()
{
  return m_platform.Day().ReadClock();
}

// DOS's file services, native (D27): the platform's DosFiles. A stamp is DOS's time at the platform's clock, as the emulated
// DOS's was.

void Hardware::SetDiskTransferArea(std::span<Field<std::uint8_t>> _area)
{
  m_platform.Files().SetTransferArea(_area);
}

DosFileAttributes Hardware::ReadFileAttributes(std::string_view _name)
{
  return m_platform.Files().ReadAttributes(_name);
}

DosAnswer Hardware::SetFileAttributes(std::string_view _name, std::uint16_t _attributes)
{
  return m_platform.Files().SetAttributes(_name, _attributes);
}

DosAnswer Hardware::CreateFile(std::string_view _name, std::uint16_t _attributes)
{
  return m_platform.Files().Create(_name, _attributes, m_platform.Clock());
}

DosAnswer Hardware::OpenFile(std::string_view _name, std::uint8_t _access)
{
  return m_platform.Files().Open(_name, _access);
}

DosAnswer Hardware::ReadFile(std::uint16_t _handle, std::span<Field<std::uint8_t>> _buffer)
{
  return m_platform.Files().Read(_handle, _buffer);
}

DosAnswer Hardware::WriteFile(std::uint16_t _handle, std::span<const std::uint8_t> _buffer)
{
  return m_platform.Files().Write(_handle, _buffer, m_platform.Clock());
}

DosAnswer Hardware::CloseFile(std::uint16_t _handle)
{
  return m_platform.Files().Close(_handle);
}

DosAnswer Hardware::DeleteFile(std::string_view _name)
{
  return m_platform.Files().Delete(_name);
}

DosAnswer Hardware::FindFirstFile(std::string_view _pattern, std::uint16_t _attributes)
{
  return m_platform.Files().FindFirst(_pattern, _attributes);
}

DosAnswer Hardware::FindNextFile()
{
  return m_platform.Files().FindNext();
}

void Hardware::Spend(const PacingPoint& _point)
{
  m_platform.Spend(_point.cycles);
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

std::string_view Hardware::CommandTail() const noexcept
{
  return m_platform.CommandTail();
}

} // namespace Elite
