#include "pch.h"

#include "PcServices.h"

#include "Cpu.h"
#include "Firmware.h"
#include "Memory.h"

namespace Machine
{

namespace
{

constexpr std::uint8_t VECTOR_VIDEO = 0x10;
constexpr std::uint8_t VECTOR_KEYBOARD = 0x16;
constexpr std::uint8_t VECTOR_TIME_OF_DAY = 0x1A;
constexpr std::uint8_t VECTOR_TERMINATE = 0x20;
constexpr std::uint8_t VECTOR_DOS = 0x21;
constexpr std::uint8_t VECTOR_MOUSE = 0x33;

// Every vector routed here arrives through the two-byte INT nn, so the instruction starts two bytes before the
// return address.
constexpr std::uint16_t INT_BYTES = 2;
constexpr std::uint8_t POWER_ON_VIDEO_MODE = 3;

} // namespace

PcServices::PcServices(Memory& _memory, PortBus& _ports, FileStore& _files, const Cycles& _clock, const Desc& _desc)
  : m_memory(_memory),
    m_desc(_desc),
    m_bios(_memory, _ports),
    m_dos(_memory, m_bios, _files, _clock, _desc.startMoment),
    m_mouse(_desc.mousePresent)
{
}

void PcServices::PowerOn()
{
  const Dos::DateTime& start = m_desc.startMoment;
  const std::uint32_t centiseconds = ((std::uint32_t{start.hour} * 60u + start.minute) * 60u + start.second) * 100u + start.hundredths;
  Firmware::Desc firmware;
  firmware.timerTicks = Firmware::TimerTicksAt(centiseconds);
  firmware.mousePresent = m_desc.mousePresent;
  Firmware::Install(m_memory, firmware);
  m_bios.SetMode(POWER_ON_VIDEO_MODE);
}

void PcServices::StartProgram(std::uint16_t _pspSegment)
{
  m_dos.StartProgram(_pspSegment);
  m_fault.reset();
}

bool PcServices::ServiceInterrupt(Cpu& _cpu, std::uint8_t _vector)
{
  Registers& regs = _cpu.Regs();
  const ServiceFault call{FaultKind::UnknownFunction,
                          _vector,
                          static_cast<std::uint8_t>(regs.ax >> 8),
                          static_cast<std::uint8_t>(regs.ax & 0xFF),
                          regs.cs,
                          static_cast<std::uint16_t>(regs.ip - INT_BYTES)};
  std::optional<FaultKind> fault;
  switch (_vector)
  {
  case VECTOR_VIDEO:
    fault = m_bios.Video(regs);
    break;
  case VECTOR_KEYBOARD:
    fault = m_bios.Keyboard(regs);
    break;
  case VECTOR_TIME_OF_DAY:
    fault = m_bios.TimeOfDay(regs);
    break;
  case VECTOR_TERMINATE:
    m_dos.Terminate(regs);
    break;
  case VECTOR_DOS:
    fault = m_dos.Call(regs);
    break;
  case VECTOR_MOUSE:
    if (!m_mouse.Present())
    {
      return false;
    }
    fault = m_mouse.Call(regs);
    break;
  default:
    return false;
  }
  if (fault && !m_fault)
  {
    m_fault = call;
    m_fault->kind = *fault;
  }
  ++m_calls;
  return true;
}

} // namespace Machine
