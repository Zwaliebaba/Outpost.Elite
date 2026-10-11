#include "pch.h"

#include "Hardware.h"

#include "Arithmetic.h"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>

namespace Elite
{

namespace
{

constexpr std::uint8_t TIMER_CHANNEL_2_SQUARE_WAVE = 0xB6; // channel 2, low then high byte, mode 3
constexpr std::uint8_t KEYBOARD_ACKNOWLEDGE = 0x80;        // port 61h bit 7, the XT's PB7

constexpr std::uint8_t VIDEO_VECTOR = 0x10;
constexpr std::uint8_t BIOS_KEYBOARD_VECTOR = 0x16;
constexpr std::uint8_t MOUSE_VECTOR = 0x33;
constexpr std::uint8_t VIDEO_SET_MODE = 0x00;
constexpr std::uint8_t BIOS_READ_KEY = 0x00;
constexpr std::uint16_t MOUSE_RESET = 0x0000;
constexpr std::uint16_t MOUSE_BUTTON_PRESSES = 0x0005;

constexpr std::uint8_t CRTC_CURSOR_ADDRESS_HIGH = 0x0E;

// DOS 3.30, as the emulated DOS was (ADR-006 item 3).
constexpr std::uint8_t DOS_MAJOR_VERSION = 3;
constexpr std::uint8_t DOS_MINOR_VERSION = 30;

} // namespace

std::uint8_t Hardware::SystemControl()
{
  return m_platform.KeyboardController().SystemControl();
}

void Hardware::SetSystemControl(std::uint8_t _value)
{
  m_platform.NotePortWrites(1);
  m_platform.KeyboardController().SetSystemControl(_value);
}

void Hardware::SetToneDivisor(std::uint16_t _divisor)
{
  m_platform.NotePortWrites(3);
  m_platform.Timer().Program(TIMER_CHANNEL_2_SQUARE_WAVE, _divisor);
}

void Hardware::SetTickDivisor(std::uint8_t _mode, std::uint16_t _divisor)
{
  m_platform.NotePortWrites(3);
  m_platform.Timer().Program(_mode, _divisor);
  m_platform.TakeDueInterrupts();
}

void Hardware::EndOfInterrupt()
{
  m_platform.NotePortWrites(1);
  m_platform.Interrupts().EndOfInterrupt();
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

void Hardware::LoopTurn(std::uint16_t _loop, std::initializer_list<std::uint16_t> _carried)
{
  std::array<std::uint16_t, Platform::MOST_TURN_WORDS> signature{};
  if (_carried.size() >= signature.size())
  {
    throw std::logic_error("Hardware::LoopTurn: more carried values than a turn signature holds");
  }
  signature[0] = _loop;
  std::ranges::copy(_carried, signature.begin() + 1);
  m_platform.LoopTurn(std::span<const std::uint16_t>(signature.data(), _carried.size() + 1));
}

std::uint8_t Hardware::KeyboardData()
{
  return m_platform.KeyboardController().Data();
}

void Hardware::AcknowledgeKeyboard()
{
  Keyboard& keyboard = m_platform.KeyboardController();
  const auto pulse = static_cast<std::uint8_t>(keyboard.SystemControl() | KEYBOARD_ACKNOWLEDGE);
  keyboard.SetSystemControl(pulse);
  keyboard.SetSystemControl(static_cast<std::uint8_t>(pulse & ~KEYBOARD_ACKNOWLEDGE));
  m_platform.NotePortWrites(2);
}

std::uint8_t Hardware::GamePortButtons()
{
  return m_platform.Joystick().Read();
}

// The mouse driver's calls: the driver's when one is loaded; otherwise int 33h reaches what its vector names, the ROM's IRET,
// and the registers come back as they went.

MouseReset Hardware::ResetMouse()
{
  Mouse& mouse = m_platform.MouseDriver();
  if (!mouse.Present())
  {
    m_platform.SoftwareInterrupt(MOUSE_VECTOR);
    return MouseReset{MOUSE_RESET, 0};
  }
  return mouse.Reset();
}

MousePresses Hardware::ReadMousePresses(std::uint16_t _button)
{
  Mouse& mouse = m_platform.MouseDriver();
  if (!mouse.Present())
  {
    m_platform.SoftwareInterrupt(MOUSE_VECTOR);
    return MousePresses{MOUSE_BUTTON_PRESSES, _button, 0, 0};
  }
  const std::optional<MousePresses> presses = mouse.Presses(_button);
  if (!presses)
  {
    m_platform.Refuse(FaultKind::UnsupportedRequest, MOUSE_VECTOR, High(MOUSE_BUTTON_PRESSES), Low(MOUSE_BUTTON_PRESSES));
  }
  return *presses;
}

MouseMotion Hardware::ReadMouseMotion()
{
  Mouse& mouse = m_platform.MouseDriver();
  if (!mouse.Present())
  {
    m_platform.SoftwareInterrupt(MOUSE_VECTOR);
    return MouseMotion{0, 0};
  }
  return mouse.Motion();
}

void Hardware::SetVideoMode(std::uint8_t _mode)
{
  if (!m_platform.BiosServices().SetMode(_mode))
  {
    m_platform.Refuse(FaultKind::UnsupportedRequest, VIDEO_VECTOR, VIDEO_SET_MODE, _mode);
  }
}

void Hardware::PrintDosString(std::uint16_t _segment, std::uint16_t _offset)
{
  m_platform.PrintDosString(_segment, _offset);
}

std::optional<std::uint16_t> Hardware::PeekBiosKey()
{
  return m_platform.BiosServices().PeekKey();
}

std::uint16_t Hardware::ReadBiosKey()
{
  const std::optional<std::uint16_t> key = m_platform.BiosServices().ReadKey();
  if (!key)
  {
    m_platform.Refuse(FaultKind::WouldBlock, BIOS_KEYBOARD_VECTOR, BIOS_READ_KEY, 0);
  }
  return *key;
}

// ── SaveLoad, StartUp, Timer and Input (level 4, group C) ──

std::uint8_t Hardware::CgaStatus()
{
  return m_platform.Video().ReadStatus();
}

void Hardware::SetColorSelect(std::uint8_t _value)
{
  m_platform.NotePortWrites(1);
  m_platform.Video().SetColorSelect(_value);
}

void Hardware::SetCursorAddressHigh(std::uint8_t _value)
{
  m_platform.NotePortWrites(2);
  m_platform.Video().WriteCrtc(CRTC_CURSOR_ADDRESS_HIGH, _value);
}

void Hardware::SetModeControl(std::uint8_t _value)
{
  m_platform.NotePortWrites(1);
  m_platform.Video().SetModeControl(_value);
}

void Hardware::SelectPalette(std::uint8_t _palette)
{
  m_platform.BiosServices().SelectPalette(_palette);
}

void Hardware::FireGamePort(std::uint8_t /*_value*/)
{
  m_platform.NotePortWrites(1);
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

BiosClock Hardware::ReadBiosClock()
{
  return m_platform.BiosServices().ReadClock();
}

void Hardware::SetBiosClock(std::uint32_t _ticks)
{
  m_platform.BiosServices().SetClock(_ticks);
}

void Hardware::RunBiosTimerTick(std::uint16_t _segment, std::uint16_t _offset)
{
  m_platform.CallHandler(FarAddress{_segment, _offset});
}

// DOS's file services, native (D27): the platform's DosFiles. A stamp is DOS's time at the platform's clock, as the emulated
// DOS's was.

void Hardware::SetDiskTransferArea(std::uint16_t _segment, std::uint16_t _offset)
{
  m_platform.Files().SetTransferArea(_segment, _offset);
}

DosFileAttributes Hardware::ReadFileAttributes(std::uint16_t _segment, std::uint16_t _name)
{
  return m_platform.Files().ReadAttributes(_segment, _name);
}

DosAnswer Hardware::SetFileAttributes(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes)
{
  return m_platform.Files().SetAttributes(_segment, _name, _attributes);
}

DosAnswer Hardware::CreateFile(std::uint16_t _segment, std::uint16_t _name, std::uint16_t _attributes)
{
  return m_platform.Files().Create(_segment, _name, _attributes, m_platform.Clock());
}

DosAnswer Hardware::OpenFile(std::uint16_t _segment, std::uint16_t _name, std::uint8_t _access)
{
  return m_platform.Files().Open(_segment, _name, _access);
}

DosAnswer Hardware::ReadFile(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes)
{
  return m_platform.Files().Read(_handle, _segment, _buffer, _bytes);
}

DosAnswer Hardware::WriteFile(std::uint16_t _handle, std::uint16_t _segment, std::uint16_t _buffer, std::uint16_t _bytes)
{
  return m_platform.Files().Write(_handle, _segment, _buffer, _bytes, m_platform.Clock());
}

DosAnswer Hardware::CloseFile(std::uint16_t _handle)
{
  return m_platform.Files().Close(_handle);
}

DosAnswer Hardware::DeleteFile(std::uint16_t _segment, std::uint16_t _name)
{
  return m_platform.Files().Delete(_segment, _name);
}

DosAnswer Hardware::FindFirstFile(std::uint16_t _segment, std::uint16_t _pattern, std::uint16_t _attributes)
{
  return m_platform.Files().FindFirst(_segment, _pattern, _attributes);
}

DosAnswer Hardware::FindNextFile()
{
  return m_platform.Files().FindNext();
}

// ── Docked, Galaxy, StartUp and Scene (level 5, group B3) ──

void Hardware::Spend(const PacingPoint& _point)
{
  m_platform.Spend(_point.cycles);
}

// ── Start, GameLoop and RunFlight (level 5, group F) ──

DosVersion Hardware::ReadDosVersion(std::uint8_t /*_al*/)
{
  return DosVersion{DOS_MAJOR_VERSION, DOS_MINOR_VERSION};
}

DosTime Hardware::ReadDosTime()
{
  const DosDateTime now = m_platform.Files().Now(m_platform.Clock());
  return DosTime{now.hour, now.minute, now.second, now.hundredths};
}

// ── What the emulated PC kept in memory, outside the game's (D33) ──

FarAddress Hardware::InterruptVector(std::uint8_t _vector)
{
  return m_platform.InterruptVector(_vector);
}

void Hardware::SetInterruptVector(std::uint8_t _vector, FarAddress _handler)
{
  m_platform.SetInterruptVector(_vector, _handler);
}

FarAddress Hardware::BiosInterruptVector(std::uint8_t _vector) const noexcept
{
  return m_platform.PowerOnInterruptVector(_vector);
}

std::uint8_t Hardware::RomByte(std::uint16_t _segment, std::uint16_t _offset)
{
  return m_platform.RomByte(FarAddress{_segment, _offset});
}

std::uint8_t Hardware::HandlerOpcode(FarAddress _handler)
{
  return m_platform.HandlerOpcode(_handler);
}

std::string_view Hardware::CommandTail() const noexcept
{
  return m_platform.CommandTail();
}

} // namespace Elite
