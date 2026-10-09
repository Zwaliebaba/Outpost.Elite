#include "pch.h"

#include "Pc.h"

#include <stdexcept>

namespace Machine
{

namespace
{

constexpr std::uint16_t CGA_FIRST_PORT = 0x3D0;
constexpr std::uint16_t CGA_LAST_PORT = 0x3DF;

} // namespace

Pc::Pc(FileStore& _files, const Desc& _desc)
  : m_speaker(m_clock),
    m_pit(m_clock, m_pic, m_speaker),
    m_keyboard(m_clock, m_pic, m_pit, m_speaker),
    m_gamePort(m_clock),
    m_cga(m_clock),
    m_services(m_memory, m_ports, _files, m_clock, _desc),
    m_cpu(m_memory, m_ports)
{
  MapPorts(Pic::COMMAND_PORT, Pic::DATA_PORT, m_pic);
  MapPorts(Pit::FIRST_PORT, Pit::LAST_PORT, m_pit);
  MapPorts(Keyboard::FIRST_PORT, Keyboard::LAST_PORT, m_keyboard);
  MapPorts(GamePort::PORT, GamePort::PORT, m_gamePort);
  MapPorts(CGA_FIRST_PORT, CGA_LAST_PORT, m_cga);
  m_cpu.SetInterruptSource(&m_pic);
  m_cpu.SetHostServices(&m_services);
  m_services.PowerOn();
}

LoadError Pc::Load(std::span<const std::uint8_t> _file, const ExeLoader::Desc& _desc, LoadedProgram& _program)
{
  const LoadError error = ExeLoader::Load(m_memory, _file, _desc, _program);
  if (error != LoadError::None)
    return error;
  m_cpu.Regs() = _program.registers;
  m_services.StartProgram(_program.pspSegment);
  return LoadError::None;
}

void Pc::Step()
{
  m_clock += m_cpu.Step();
  m_pit.Advance();
  m_keyboard.Advance();
}

StopReason Pc::RunUntil(Cycles _cycle)
{
  while (m_clock < _cycle)
  {
    Step();
    if (const StopReason reason = Stopped(); reason != StopReason::Reached)
      return reason;
  }
  return StopReason::Reached;
}

void Pc::MapPorts(std::uint16_t _first, std::uint16_t _last, PortBus& _device)
{
  if (!m_ports.Map(_first, _last, _device))
    throw std::logic_error("Pc: two devices claim the same port");
}

StopReason Pc::Stopped() const noexcept
{
  if (m_services.Fault().has_value())
    return StopReason::Fault;
  if (m_services.Terminated())
    return StopReason::Terminated;
  if (m_cpu.Halted() && (m_cpu.Regs().flags & FLAG_INTERRUPT) == 0)
    return StopReason::Deadlocked;
  return StopReason::Reached;
}

} // namespace Machine
