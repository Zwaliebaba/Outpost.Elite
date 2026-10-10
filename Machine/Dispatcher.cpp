#include "pch.h"

#include "Dispatcher.h"

#include "Firmware.h"
#include "HostServices.h"
#include "InterruptSource.h"
#include "Memory.h"

namespace Machine
{

namespace
{

constexpr std::uint8_t OPCODE_IRET = 0xCF;
constexpr std::uint8_t OPCODE_INT = 0xCD;
constexpr std::uint8_t VECTOR_TERMINATE = 0x20;
constexpr std::uint16_t INT_BYTES = 2;

// What the interpreter charged for the two instructions, from Intel's 8086 table plus 4 clocks for every word the
// 8088 moves over its 8-bit bus: IRET pops three words; INT pushes three and reads the vector's two.
constexpr std::uint32_t WORD_TRANSFER_CYCLES = 4;
constexpr std::uint32_t IRET_CYCLES = 24 + 3 * WORD_TRANSFER_CYCLES;
constexpr std::uint32_t INTERRUPT_ENTRY_CYCLES = 51 + 5 * WORD_TRANSFER_CYCLES;

} // namespace

Dispatcher::Dispatcher(Memory& _memory) noexcept
  : m_memory(_memory)
{
  Reset();
}

void Dispatcher::SetHostServices(HostServices* _host) noexcept
{
  m_host = _host;
}

void Dispatcher::SetInterruptSource(InterruptSource* _source) noexcept
{
  m_interrupts = _source;
}

void Dispatcher::SetHookMap(const std::vector<std::uint8_t>* _map) noexcept
{
  m_hookMap = _map;
}

bool Dispatcher::InterruptDue() const
{
  // No instruction runs here that could cast an interrupt shadow: STI and MOV SS are the program's.
  return m_interrupts != nullptr && (m_regs.flags & FLAG_INTERRUPT) != 0 && m_interrupts->InterruptPending();
}

void Dispatcher::Reset() noexcept
{
  m_regs = Registers{};
  m_regs.cs = 0xFFFF;
  m_regs.flags = FLAGS_FIXED_ONES;
  m_instructionCount = 0;
  m_hardwareInterrupts = 0;
  m_atHook = false;
  m_atUnportedCode = false;
}

std::uint32_t Dispatcher::Step()
{
  std::uint32_t cycles = 0;
  m_atHook = false;
  m_atUnportedCode = false;
  if (InterruptDue())
  {
    // The acknowledge cycle: the controller puts the vector on the bus and marks it in service.
    EnterInterrupt(m_interrupts->AcknowledgeInterrupt());
    cycles += HARDWARE_INTERRUPT_CYCLES;
    ++m_hardwareInterrupts;
  }
  if (m_hookMap != nullptr && (*m_hookMap)[Memory::Linear(m_regs.cs, m_regs.ip)] != 0)
  {
    m_atHook = true; // the Pc runs the native code that stands in for what is here
    return cycles;
  }
  return cycles + RunMachineCode();
}

std::uint32_t Dispatcher::RunMachineCode()
{
  const std::uint8_t opcode = m_memory.Read8(m_regs.cs, m_regs.ip);
  const bool romIret = m_regs.cs == Firmware::ROM_SEGMENT && opcode == OPCODE_IRET;
  const bool pspTerminate = m_regs.ip == 0 && opcode == OPCODE_INT && m_memory.Read8(m_regs.cs, 1) == VECTOR_TERMINATE;
  if (!romIret && !pspTerminate)
  {
    m_atUnportedCode = true;
    return 0;
  }
  ++m_instructionCount;
  if (romIret)
  {
    m_regs.ip = Pop();
    m_regs.cs = Pop();
    m_regs.flags = static_cast<std::uint16_t>((Pop() & FLAGS_WRITABLE) | FLAGS_FIXED_ONES);
    return IRET_CYCLES;
  }
  m_regs.ip = static_cast<std::uint16_t>(m_regs.ip + INT_BYTES);
  if (m_host == nullptr || !m_host->ServiceInterrupt(m_regs, VECTOR_TERMINATE))
  {
    EnterInterrupt(VECTOR_TERMINATE);
  }
  return INTERRUPT_ENTRY_CYCLES;
}

void Dispatcher::EnterInterrupt(std::uint8_t _vector) noexcept
{
  // As the 8088 enters one: the flags pushed as they are, IF and TF cleared, then CS and IP.
  Push(m_regs.flags);
  m_regs.flags = static_cast<std::uint16_t>(m_regs.flags & ~(FLAG_INTERRUPT | FLAG_TRAP));
  Push(m_regs.cs);
  Push(m_regs.ip);
  const std::uint32_t entry = static_cast<std::uint32_t>(_vector) * 4u;
  m_regs.ip = m_memory.Read16(entry);
  m_regs.cs = m_memory.Read16(entry + 2u);
}

void Dispatcher::Push(std::uint16_t _value) noexcept
{
  m_regs.sp = static_cast<std::uint16_t>(m_regs.sp - 2);
  m_memory.Write16(m_regs.ss, m_regs.sp, _value);
}

std::uint16_t Dispatcher::Pop() noexcept
{
  const std::uint16_t value = m_memory.Read16(m_regs.ss, m_regs.sp);
  m_regs.sp = static_cast<std::uint16_t>(m_regs.sp + 2);
  return value;
}

} // namespace Machine
