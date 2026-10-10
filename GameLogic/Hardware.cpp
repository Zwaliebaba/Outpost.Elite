#include "pch.h"

#include "Hardware.h"

#include "Arithmetic.h"
#include "Pc.h"

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

void Hardware::DisableInterrupts() noexcept
{
  m_pc.Processor().Regs().flags = static_cast<std::uint16_t>(m_pc.Processor().Regs().flags & ~Machine::FLAG_INTERRUPT);
}

} // namespace Elite
