#include "pch.h"

#include "Pc.h"

#include <algorithm>
#include <stdexcept>

namespace Machine
{

namespace
{

constexpr std::uint16_t CGA_FIRST_PORT = 0x3D0;
constexpr std::uint16_t CGA_LAST_PORT = 0x3DF;

// The jumps a loop closes with: Jcc (70-7F, and their aliases 60-6F), LOOPNZ, LOOPZ, LOOP and JCXZ
// (E0-E3), JMP near and short (E9, EB).
[[nodiscard]] bool IsJump(std::uint8_t _opcode) noexcept
{
  return (_opcode >= 0x60 && _opcode <= 0x7F) || (_opcode >= 0xE0 && _opcode <= 0xE3) || _opcode == 0xE9 || _opcode == 0xEB;
}

} // namespace

Pc::Pc(FileStore& _files, const Desc& _desc)
  : m_speaker(m_clock),
    m_pit(m_clock, m_pic, m_speaker),
    m_keyboard(m_clock, m_pic, m_pit, m_speaker),
    m_gamePort(m_instructionCycles),
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

void Pc::SetTimeMode(TimeMode _mode) noexcept
{
  m_timeMode = _mode;
  m_cga.SetRetraceSeenOnce(_mode == TimeMode::Paced);
  m_stepsSinceIdle = 0;
  m_lastTurn.valid = false;
}

void Pc::SetSpinLimit(std::uint64_t _steps) noexcept
{
  m_spinLimit = _steps;
}

void Pc::Step()
{
  if (m_timeMode == TimeMode::Paced)
  {
    StepPaced();
    return;
  }
  const std::uint32_t cycles = m_cpu.Step();
  m_clock += cycles;
  m_instructionCycles += cycles;
  m_pit.Advance();
  m_keyboard.Advance();
}

void Pc::StepPaced()
{
  if (m_cpu.Halted() && (m_cpu.Regs().flags & FLAG_INTERRUPT) != 0 && !m_pic.InterruptPending())
  {
    // HLT is a wait by definition.
    Idle(m_runLimit);
    return;
  }
  const std::uint16_t segment = m_cpu.Regs().cs;
  const std::uint16_t offset = m_cpu.Regs().ip;
  const std::uint8_t opcode = m_memory.Read8(segment, offset);
  const std::uint64_t interrupts = m_cpu.HardwareInterruptCount();
  m_instructionCycles += m_cpu.Step();
  ++m_stepsSinceIdle;
  const Registers& after = m_cpu.Regs();
  if (after.cs == segment && after.ip <= offset && m_cpu.HardwareInterruptCount() == interrupts && IsJump(opcode))
  {
    NoteBackwardJump();
  }
}

void Pc::NoteBackwardJump()
{
  const LoopTurn turn{m_cpu.Regs(), m_memory.ChangeCount(), m_ports.WriteCount(), true};
  const bool idle = m_lastTurn.valid && turn.registers == m_lastTurn.registers && turn.memoryChanges == m_lastTurn.memoryChanges &&
                    turn.portWrites == m_lastTurn.portWrites;
  m_lastTurn = turn;
  if (idle)
  {
    Idle(m_runLimit);
  }
}

void Pc::Idle(Cycles _limit)
{
  Cycles next = std::min({m_pit.NextInterruptAt(), m_keyboard.NextDeliveryAt(), m_cga.NextStatusChangeAt(), _limit});
  if (next <= m_clock)
  {
    next = m_clock + 1;
  }
  m_clock = next;
  m_stepsSinceIdle = 0;
  m_pit.Advance();
  m_keyboard.Advance();
}

StopReason Pc::RunUntil(Cycles _cycle)
{
  m_runLimit = _cycle;
  while (m_clock < _cycle)
  {
    Step();
    if (const StopReason reason = Stopped(); reason != StopReason::Reached)
    {
      m_runLimit = NO_EVENT;
      return reason;
    }
  }
  m_runLimit = NO_EVENT;
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
  if (m_timeMode == TimeMode::Paced && m_stepsSinceIdle >= m_spinLimit)
    return StopReason::Spinning;
  return StopReason::Reached;
}

} // namespace Machine
