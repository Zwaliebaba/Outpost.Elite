#include "pch.h"

#include "Hardware.h"

#include "Arithmetic.h"
#include "Pc.h"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>

namespace Elite
{

namespace
{

constexpr std::uint16_t PIC_COMMAND_PORT = 0x20;
constexpr std::uint8_t END_OF_INTERRUPT = 0x20;
constexpr std::uint16_t PIT_CHANNEL_0_PORT = 0x40;
constexpr std::uint16_t PIT_CHANNEL_2_PORT = 0x42;
constexpr std::uint16_t PIT_COMMAND_PORT = 0x43;
constexpr std::uint8_t PIT_CHANNEL_2_SQUARE_WAVE = 0xB6; // channel 2, low then high byte, mode 3
constexpr std::uint16_t SYSTEM_CONTROL_PORT = 0x61;
constexpr std::uint16_t KEYBOARD_DATA_PORT = 0x60;
constexpr std::uint8_t KEYBOARD_ACKNOWLEDGE = 0x80; // port 61h bit 7, the XT's PB7
constexpr std::uint16_t GAME_PORT = 0x201;

constexpr std::uint8_t VIDEO_VECTOR = 0x10;
constexpr std::uint8_t BIOS_KEYBOARD_VECTOR = 0x16;
constexpr std::uint8_t DOS_VECTOR = 0x21;
constexpr std::uint8_t MOUSE_VECTOR = 0x33;
constexpr std::uint8_t VIDEO_SET_MODE = 0x00;
constexpr std::uint8_t BIOS_READ_KEY = 0x00;
constexpr std::uint8_t BIOS_KEY_STATUS = 0x01;
constexpr std::uint8_t DOS_PRINT_STRING = 0x09;
constexpr std::uint16_t MOUSE_RESET = 0x0000;
constexpr std::uint16_t MOUSE_BUTTON_PRESSES = 0x0005;
constexpr std::uint16_t MOUSE_MOTION = 0x000B;

// A service operation: INT _vector with the registers _setInputs leaves (from the processor's own), and the registers the
// service leaves returned. The processor's registers are then put back as they were, so the operation has no register effect
// of its own.
template <typename SetInputs> Machine::Registers CallService(Machine::Pc& _pc, std::uint8_t _vector, SetInputs _setInputs)
{
  Machine::Registers& regs = _pc.Processor().Regs();
  const Machine::Registers kept = regs;
  _setInputs(regs);
  _pc.CallInterrupt(_vector);
  const Machine::Registers left = regs;
  regs = kept;
  return left;
}

} // namespace

std::uint8_t Hardware::SystemControl()
{
  return m_pc.Ports().In8(SYSTEM_CONTROL_PORT);
}

void Hardware::SetSystemControl(std::uint8_t _value)
{
  m_pc.Ports().Out8(SYSTEM_CONTROL_PORT, _value);
}

void Hardware::SetToneDivisor(std::uint16_t _divisor)
{
  m_pc.Ports().Out8(PIT_COMMAND_PORT, PIT_CHANNEL_2_SQUARE_WAVE);
  m_pc.Ports().Out8(PIT_CHANNEL_2_PORT, Low(_divisor));
  m_pc.Ports().Out8(PIT_CHANNEL_2_PORT, High(_divisor));
}

void Hardware::SetTickDivisor(std::uint8_t _mode, std::uint16_t _divisor)
{
  m_pc.Ports().Out8(PIT_COMMAND_PORT, _mode);
  m_pc.Ports().Out8(PIT_CHANNEL_0_PORT, Low(_divisor));
  m_pc.Ports().Out8(PIT_CHANNEL_0_PORT, High(_divisor));
}

void Hardware::EndOfInterrupt()
{
  m_pc.Ports().Out8(PIC_COMMAND_PORT, END_OF_INTERRUPT);
}

void Hardware::EnableInterrupts() noexcept
{
  m_pc.Processor().Regs().flags |= Machine::FLAG_INTERRUPT;
}

void Hardware::LoopTurn(std::uint16_t _loop, std::initializer_list<std::uint16_t> _carried)
{
  std::array<std::uint16_t, Machine::Pc::MOST_TURN_WORDS> signature{};
  if (_carried.size() >= signature.size())
  {
    throw std::logic_error("Hardware::LoopTurn: more carried values than a turn signature holds");
  }
  signature[0] = _loop;
  std::ranges::copy(_carried, signature.begin() + 1);
  m_pc.LoopTurn(std::span<const std::uint16_t>(signature.data(), _carried.size() + 1));
}

void Hardware::DisableInterrupts() noexcept
{
  m_pc.Processor().Regs().flags = static_cast<std::uint16_t>(m_pc.Processor().Regs().flags & ~Machine::FLAG_INTERRUPT);
}

std::uint8_t Hardware::KeyboardData()
{
  return m_pc.Ports().In8(KEYBOARD_DATA_PORT);
}

void Hardware::AcknowledgeKeyboard()
{
  const auto pulse = static_cast<std::uint8_t>(m_pc.Ports().In8(SYSTEM_CONTROL_PORT) | KEYBOARD_ACKNOWLEDGE);
  m_pc.Ports().Out8(SYSTEM_CONTROL_PORT, pulse);
  m_pc.Ports().Out8(SYSTEM_CONTROL_PORT, static_cast<std::uint8_t>(pulse & ~KEYBOARD_ACKNOWLEDGE));
}

std::uint8_t Hardware::GamePortButtons()
{
  return m_pc.Ports().In8(GAME_PORT);
}

MouseReset Hardware::ResetMouse()
{
  const Machine::Registers left = CallService(m_pc, MOUSE_VECTOR, [](Machine::Registers& _regs) { _regs.ax = MOUSE_RESET; });
  return MouseReset{left.ax, left.bx};
}

MousePresses Hardware::ReadMousePresses(std::uint16_t _button)
{
  const Machine::Registers left = CallService(m_pc, MOUSE_VECTOR,
                                              [_button](Machine::Registers& _regs)
                                              {
                                                _regs.ax = MOUSE_BUTTON_PRESSES;
                                                _regs.bx = _button;
                                              });
  return MousePresses{left.ax, left.bx, left.cx, left.dx};
}

MouseMotion Hardware::ReadMouseMotion()
{
  const Machine::Registers left = CallService(m_pc, MOUSE_VECTOR, [](Machine::Registers& _regs) { _regs.ax = MOUSE_MOTION; });
  return MouseMotion{left.cx, left.dx};
}

void Hardware::SetVideoMode(std::uint8_t _mode)
{
  CallService(m_pc, VIDEO_VECTOR, [_mode](Machine::Registers& _regs) { _regs.ax = Join(VIDEO_SET_MODE, _mode); });
}

void Hardware::PrintDosString(std::uint16_t _segment, std::uint16_t _offset)
{
  CallService(m_pc, DOS_VECTOR,
              [_segment, _offset](Machine::Registers& _regs)
              {
                SetHigh(_regs.ax, DOS_PRINT_STRING);
                _regs.ds = _segment;
                _regs.dx = _offset;
              });
}

std::optional<std::uint16_t> Hardware::PeekBiosKey()
{
  const Machine::Registers left =
    CallService(m_pc, BIOS_KEYBOARD_VECTOR, [](Machine::Registers& _regs) { SetHigh(_regs.ax, BIOS_KEY_STATUS); });
  if ((left.flags & Machine::FLAG_ZERO) != 0)
  {
    return std::nullopt;
  }
  return left.ax;
}

std::uint16_t Hardware::ReadBiosKey()
{
  return CallService(m_pc, BIOS_KEYBOARD_VECTOR, [](Machine::Registers& _regs) { SetHigh(_regs.ax, BIOS_READ_KEY); }).ax;
}

} // namespace Elite
