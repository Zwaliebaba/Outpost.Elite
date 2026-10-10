#include "pch.h"

#include "Cpu.h"

#include "HostServices.h"
#include "InstructionObserver.h"
#include "InterruptSource.h"
#include "Memory.h"
#include "PortBus.h"

#include <array>

namespace Machine
{

namespace
{

// ModRM register numbering: AX CX DX BX SP BP SI DI, and for bytes AL CL DL BL AH CH DH BH.
constexpr std::array<std::uint16_t Registers::*, 8> REGISTERS_16 = {&Registers::ax, &Registers::cx, &Registers::dx, &Registers::bx,
                                                                    &Registers::sp, &Registers::bp, &Registers::si, &Registers::di};
// Segment register numbering in prefixes and in 8C/8E: ES CS SS DS.
constexpr std::array<std::uint16_t Registers::*, 4> SEGMENTS = {&Registers::es, &Registers::cs, &Registers::ss, &Registers::ds};

constexpr std::uint8_t REGISTER_SP = 4;

// The ALU operations, numbered as in opcodes 00-3F and the reg field of 80-83.
constexpr std::uint8_t ALU_ADD = 0;
constexpr std::uint8_t ALU_OR = 1;
constexpr std::uint8_t ALU_ADC = 2;
constexpr std::uint8_t ALU_SBB = 3;
constexpr std::uint8_t ALU_AND = 4;
constexpr std::uint8_t ALU_SUB = 5;
constexpr std::uint8_t ALU_XOR = 6;
constexpr std::uint8_t ALU_CMP = 7;

constexpr std::uint8_t VECTOR_DIVIDE_ERROR = 0;
constexpr std::uint8_t VECTOR_SINGLE_STEP = 1;
constexpr std::uint8_t VECTOR_BREAKPOINT = 3;
constexpr std::uint8_t VECTOR_OVERFLOW = 4;

// Clocks. The 8088 moves a word over its 8-bit bus as two byte transfers, 4 clocks longer than the
// 8086's one; Intel's table is for the 8086, so every word transfer adds this.
constexpr std::uint32_t WORD_TRANSFER_CYCLES = 4;
constexpr std::uint32_t PREFIX_CYCLES = 2;
// Interrupt entry: Intel's 8086 figures plus five word transfers (three pushes, two vector reads).
constexpr std::uint32_t INTERRUPT_ENTRY_CYCLES = 51 + 5 * WORD_TRANSFER_CYCLES;
constexpr std::uint32_t HARDWARE_INTERRUPT_CYCLES = 61 + 5 * WORD_TRANSFER_CYCLES;
constexpr std::uint32_t SINGLE_STEP_CYCLES = 50 + 5 * WORD_TRANSFER_CYCLES;

constexpr std::uint16_t ToWord(std::uint32_t _value) noexcept
{
  return static_cast<std::uint16_t>(_value & 0xFFFFu);
}

constexpr std::uint8_t ToByte(std::uint32_t _value) noexcept
{
  return static_cast<std::uint8_t>(_value & 0xFFu);
}

constexpr std::uint32_t WidthMask(bool _word) noexcept
{
  return _word ? 0xFFFFu : 0xFFu;
}

constexpr std::uint32_t SignBit(bool _word) noexcept
{
  return _word ? 0x8000u : 0x80u;
}

constexpr std::uint16_t SignExtend8(std::uint32_t _value) noexcept
{
  return ToWord((_value & 0x80u) != 0 ? (_value | 0xFF00u) : (_value & 0xFFu));
}

constexpr std::uint32_t WordPenalty(bool _word, std::uint32_t _transfers) noexcept
{
  return _word ? _transfers * WORD_TRANSFER_CYCLES : 0;
}

constexpr bool EvenParity(std::uint32_t _value) noexcept
{
  std::uint32_t bits = _value & 0xFFu;
  bits ^= bits >> 4;
  bits ^= bits >> 2;
  bits ^= bits >> 1;
  return (bits & 1u) == 0;
}

// A signed value of the given width, as a plain int.
constexpr std::int32_t AsSigned(std::uint32_t _value, bool _word) noexcept
{
  const std::uint32_t masked = _value & WidthMask(_word);
  return (masked & SignBit(_word)) != 0 ? static_cast<std::int32_t>(masked) - static_cast<std::int32_t>(WidthMask(_word)) - 1
                                        : static_cast<std::int32_t>(masked);
}

} // namespace

Cpu::Cpu(Memory& _memory, PortBus& _ports) noexcept
  : m_memory(_memory),
    m_ports(_ports)
{
  Reset();
}

void Cpu::SetHostServices(HostServices* _host) noexcept
{
  m_host = _host;
}

void Cpu::SetInterruptSource(InterruptSource* _source) noexcept
{
  m_interrupts = _source;
}

void Cpu::SetExecutionMap(std::vector<std::uint8_t>* _map)
{
  if (_map != nullptr && _map->size() < Memory::SIZE_BYTES)
  {
    _map->resize(Memory::SIZE_BYTES, 0);
  }
  m_executionMap = _map;
}

void Cpu::SetInstructionObserver(InstructionObserver* _observer) noexcept
{
  m_observer = _observer;
}

void Cpu::Reset() noexcept
{
  m_regs = Registers{};
  m_regs.cs = 0xFFFF;
  m_regs.flags = FLAGS_FIXED_ONES;
  m_instructionCount = 0;
  m_hardwareInterrupts = 0;
  m_halted = false;
  m_interruptShadow = false;
}

std::uint32_t Cpu::Step()
{
  m_cycles = 0;
  if (m_interrupts != nullptr && !m_interruptShadow && Flag(FLAG_INTERRUPT) && m_interrupts->InterruptPending())
  {
    // The acknowledge cycle: the controller puts the vector on the bus and marks it in service.
    const std::uint8_t vector = m_interrupts->AcknowledgeInterrupt();
    EnterInterrupt(vector);
    m_cycles += HARDWARE_INTERRUPT_CYCLES;
    ++m_hardwareInterrupts;
  }
  if (m_halted)
  {
    return HALT_IDLE_CYCLES;
  }
  m_interruptShadow = false;
  const bool trapping = Flag(FLAG_TRAP);
  if (m_executionMap != nullptr)
  {
    (*m_executionMap)[Memory::Linear(m_regs.cs, m_regs.ip)] = 1;
  }
  if (m_observer != nullptr)
  {
    m_observer->BeforeInstruction(m_regs);
  }

  m_segmentOverride = -1;
  m_repeat = Repeat::None;
  std::uint8_t opcode = Fetch8();
  for (bool prefix = true; prefix;)
  {
    switch (opcode)
    {
    case 0x26:
    case 0x2E:
    case 0x36:
    case 0x3E:
      m_segmentOverride = static_cast<std::int8_t>((opcode >> 3) & 3);
      break;
    case 0xF0:
    case 0xF1: // LOCK, and its undocumented alias: nothing to lock on a single-processor bus
      break;
    case 0xF2:
      m_repeat = Repeat::WhileNotZero;
      break;
    case 0xF3:
      m_repeat = Repeat::WhileZero;
      break;
    default:
      prefix = false;
      break;
    }
    if (prefix)
    {
      m_cycles += PREFIX_CYCLES;
      opcode = Fetch8();
    }
  }

  Execute(opcode);
  ++m_instructionCount;

  if (trapping && Flag(FLAG_TRAP) && !m_interruptShadow)
  {
    EnterInterrupt(VECTOR_SINGLE_STEP);
    m_cycles += SINGLE_STEP_CYCLES;
  }
  return m_cycles;
}

// ── Decode ──────────────────────────────────────────────────────────────────────────────────

std::uint8_t Cpu::Fetch8() noexcept
{
  const std::uint8_t value = m_memory.Read8(m_regs.cs, m_regs.ip);
  m_regs.ip = ToWord(m_regs.ip + 1u);
  return value;
}

std::uint16_t Cpu::Fetch16() noexcept
{
  const std::uint32_t low = Fetch8();
  const std::uint32_t high = Fetch8();
  return ToWord(low | (high << 8));
}

std::uint16_t Cpu::FetchImmediate(bool _word) noexcept
{
  if (_word)
  {
    return Fetch16();
  }
  return Fetch8();
}

std::uint16_t Cpu::SegmentOr(std::uint16_t _default) const noexcept
{
  return m_segmentOverride < 0 ? _default : Segment(static_cast<std::uint8_t>(m_segmentOverride));
}

Cpu::Operand Cpu::DecodeModRm() noexcept
{
  const std::uint8_t modrm = Fetch8();
  const std::uint8_t mod = static_cast<std::uint8_t>(modrm >> 6);
  Operand operand;
  operand.reg = static_cast<std::uint8_t>((modrm >> 3) & 7);
  operand.rm = static_cast<std::uint8_t>(modrm & 7);
  if (mod == 3)
  {
    operand.isRegister = true;
    return operand;
  }

  // Effective-address clocks from Intel's table: 5 for a base or index alone, 7 or 8 for a base
  // plus an index, 6 for a bare displacement, and 4 more when a displacement is added to registers.
  std::uint32_t offset = 0;
  std::uint32_t eaCycles = 0;
  std::uint16_t segment = m_regs.ds;
  switch (operand.rm)
  {
  case 0:
    offset = m_regs.bx + static_cast<std::uint32_t>(m_regs.si);
    eaCycles = 7;
    break;
  case 1:
    offset = m_regs.bx + static_cast<std::uint32_t>(m_regs.di);
    eaCycles = 8;
    break;
  case 2:
    offset = m_regs.bp + static_cast<std::uint32_t>(m_regs.si);
    segment = m_regs.ss;
    eaCycles = 8;
    break;
  case 3:
    offset = m_regs.bp + static_cast<std::uint32_t>(m_regs.di);
    segment = m_regs.ss;
    eaCycles = 7;
    break;
  case 4:
    offset = m_regs.si;
    eaCycles = 5;
    break;
  case 5:
    offset = m_regs.di;
    eaCycles = 5;
    break;
  case 6:
    if (mod == 0)
    {
      offset = Fetch16();
      eaCycles = 6;
    }
    else
    {
      offset = m_regs.bp;
      segment = m_regs.ss;
      eaCycles = 5;
    }
    break;
  default:
    offset = m_regs.bx;
    eaCycles = 5;
    break;
  }
  if (mod == 1)
  {
    offset += SignExtend8(Fetch8());
    eaCycles += 4;
  }
  else if (mod == 2)
  {
    offset += Fetch16();
    eaCycles += 4;
  }
  operand.offset = ToWord(offset);
  operand.segment = SegmentOr(segment);
  m_cycles += eaCycles;
  return operand;
}

// ── Operands ────────────────────────────────────────────────────────────────────────────────

std::uint16_t Cpu::Reg16(std::uint8_t _index) const noexcept
{
  return m_regs.*REGISTERS_16[_index & 7u];
}

void Cpu::SetReg16(std::uint8_t _index, std::uint16_t _value) noexcept
{
  m_regs.*REGISTERS_16[_index & 7u] = _value;
}

std::uint8_t Cpu::Reg8(std::uint8_t _index) const noexcept
{
  const std::uint16_t full = m_regs.*REGISTERS_16[_index & 3u];
  return (_index & 4u) != 0 ? ToByte(full >> 8u) : ToByte(full);
}

void Cpu::SetReg8(std::uint8_t _index, std::uint8_t _value) noexcept
{
  std::uint16_t& full = m_regs.*REGISTERS_16[_index & 3u];
  if ((_index & 4u) != 0)
  {
    full = ToWord((full & 0x00FFu) | (static_cast<std::uint32_t>(_value) << 8));
  }
  else
  {
    full = ToWord((full & 0xFF00u) | _value);
  }
}

std::uint16_t Cpu::ReadReg(std::uint8_t _index, bool _word) const noexcept
{
  if (_word)
  {
    return Reg16(_index);
  }
  return Reg8(_index);
}

void Cpu::WriteReg(std::uint8_t _index, bool _word, std::uint16_t _value) noexcept
{
  if (_word)
  {
    SetReg16(_index, _value);
  }
  else
  {
    SetReg8(_index, ToByte(_value));
  }
}

std::uint16_t Cpu::ReadOperand(const Operand& _operand, bool _word) const noexcept
{
  if (_operand.isRegister)
  {
    return ReadReg(_operand.rm, _word);
  }
  return ReadMemory(_operand.segment, _operand.offset, _word);
}

std::uint16_t Cpu::ReadMemory(std::uint16_t _segment, std::uint16_t _offset, bool _word) const noexcept
{
  if (_word)
  {
    return m_memory.Read16(_segment, _offset);
  }
  return m_memory.Read8(_segment, _offset);
}

void Cpu::WriteOperand(const Operand& _operand, bool _word, std::uint16_t _value) noexcept
{
  if (_operand.isRegister)
  {
    WriteReg(_operand.rm, _word, _value);
  }
  else if (_word)
  {
    m_memory.Write16(_operand.segment, _operand.offset, _value);
  }
  else
  {
    m_memory.Write8(_operand.segment, _operand.offset, ToByte(_value));
  }
}

std::uint16_t Cpu::Segment(std::uint8_t _index) const noexcept
{
  return m_regs.*SEGMENTS[_index & 3u];
}

void Cpu::SetSegment(std::uint8_t _index, std::uint16_t _value) noexcept
{
  m_regs.*SEGMENTS[_index & 3u] = _value;
  // The 8088 holds off interrupts for one instruction after any segment register load, so that
  // MOV SS / MOV SP pairs are safe; later CPUs only do this for SS.
  m_interruptShadow = true;
}

// ── Stack and control transfer ──────────────────────────────────────────────────────────────

void Cpu::Push(std::uint16_t _value) noexcept
{
  m_regs.sp = ToWord(m_regs.sp - 2u);
  m_memory.Write16(m_regs.ss, m_regs.sp, _value);
}

std::uint16_t Cpu::Pop() noexcept
{
  const std::uint16_t value = m_memory.Read16(m_regs.ss, m_regs.sp);
  m_regs.sp = ToWord(m_regs.sp + 2u);
  return value;
}

void Cpu::EnterInterrupt(std::uint8_t _vector) noexcept
{
  Push(m_regs.flags);
  m_regs.flags = ToWord(m_regs.flags & ~static_cast<std::uint32_t>(FLAG_INTERRUPT | FLAG_TRAP));
  Push(m_regs.cs);
  Push(m_regs.ip);
  const std::uint32_t entry = static_cast<std::uint32_t>(_vector) * 4u;
  m_regs.ip = m_memory.Read16(entry);
  m_regs.cs = m_memory.Read16(entry + 2u);
  m_halted = false;
}

void Cpu::SoftwareInterrupt(std::uint8_t _vector)
{
  if (m_host != nullptr && m_host->ServiceInterrupt(*this, _vector))
  {
    return;
  }
  EnterInterrupt(_vector);
}

void Cpu::JumpShort(bool _taken, std::uint32_t _takenCycles, std::uint32_t _notTakenCycles) noexcept
{
  const std::uint16_t displacement = SignExtend8(Fetch8());
  if (_taken)
  {
    m_regs.ip = ToWord(m_regs.ip + static_cast<std::uint32_t>(displacement));
    m_cycles += _takenCycles;
  }
  else
  {
    m_cycles += _notTakenCycles;
  }
}

// ── Flags and arithmetic ────────────────────────────────────────────────────────────────────

void Cpu::SetFlag(std::uint16_t _flag, bool _set) noexcept
{
  m_regs.flags =
    _set ? ToWord(m_regs.flags | static_cast<std::uint32_t>(_flag)) : ToWord(m_regs.flags & ~static_cast<std::uint32_t>(_flag));
}

void Cpu::SetFlagsWord(std::uint16_t _value) noexcept
{
  m_regs.flags = ToWord((_value & static_cast<std::uint32_t>(FLAGS_WRITABLE)) | FLAGS_FIXED_ONES);
}

void Cpu::SetSignZeroParity(std::uint32_t _result, bool _word) noexcept
{
  const std::uint32_t masked = _result & WidthMask(_word);
  SetFlag(FLAG_SIGN, (masked & SignBit(_word)) != 0);
  SetFlag(FLAG_ZERO, masked == 0);
  SetFlag(FLAG_PARITY, EvenParity(masked));
}

std::uint16_t Cpu::Add(std::uint32_t _a, std::uint32_t _b, std::uint32_t _carry, bool _word) noexcept
{
  const std::uint32_t full = _a + _b + _carry;
  const std::uint32_t result = full & WidthMask(_word);
  SetFlag(FLAG_CARRY, full > WidthMask(_word));
  SetFlag(FLAG_AUXILIARY, ((_a ^ _b ^ result) & 0x10u) != 0);
  SetFlag(FLAG_OVERFLOW, ((_a ^ result) & (_b ^ result) & SignBit(_word)) != 0);
  SetSignZeroParity(result, _word);
  return ToWord(result);
}

std::uint16_t Cpu::Subtract(std::uint32_t _a, std::uint32_t _b, std::uint32_t _borrow, bool _word) noexcept
{
  const std::uint32_t full = _a - _b - _borrow;
  const std::uint32_t result = full & WidthMask(_word);
  SetFlag(FLAG_CARRY, (full & (WidthMask(_word) + 1u)) != 0);
  SetFlag(FLAG_AUXILIARY, ((_a ^ _b ^ result) & 0x10u) != 0);
  SetFlag(FLAG_OVERFLOW, ((_a ^ _b) & (_a ^ result) & SignBit(_word)) != 0);
  SetSignZeroParity(result, _word);
  return ToWord(result);
}

std::uint16_t Cpu::Logic(std::uint32_t _result, bool _word) noexcept
{
  const std::uint32_t result = _result & WidthMask(_word);
  SetFlag(FLAG_CARRY, false);
  SetFlag(FLAG_OVERFLOW, false);
  SetFlag(FLAG_AUXILIARY, false);
  SetSignZeroParity(result, _word);
  return ToWord(result);
}

std::uint16_t Cpu::Alu(std::uint8_t _operation, std::uint16_t _a, std::uint16_t _b, bool _word) noexcept
{
  const std::uint32_t carry = Flag(FLAG_CARRY) ? 1u : 0u;
  switch (_operation & 7u)
  {
  case ALU_ADD:
    return Add(_a, _b, 0, _word);
  case ALU_OR:
    return Logic(static_cast<std::uint32_t>(_a) | _b, _word);
  case ALU_ADC:
    return Add(_a, _b, carry, _word);
  case ALU_SBB:
    return Subtract(_a, _b, carry, _word);
  case ALU_AND:
    return Logic(static_cast<std::uint32_t>(_a) & _b, _word);
  case ALU_XOR:
    return Logic(static_cast<std::uint32_t>(_a) ^ _b, _word);
  case ALU_SUB:
  default: // ALU_CMP, whose result the caller does not store
    return Subtract(_a, _b, 0, _word);
  }
}

std::uint16_t Cpu::IncDec(std::uint16_t _value, bool _increment, bool _word) noexcept
{
  const bool carry = Flag(FLAG_CARRY);
  const std::uint16_t result = _increment ? Add(_value, 1, 0, _word) : Subtract(_value, 1, 0, _word);
  SetFlag(FLAG_CARRY, carry);
  return result;
}

std::uint16_t Cpu::Shift(std::uint8_t _operation, std::uint16_t _value, std::uint32_t _count, bool _word) noexcept
{
  // The 8088 does not mask the count (the 80186 and later mask it to five bits): it shifts one
  // bit per microcode iteration, CL times. A count of zero changes nothing, flags included. CF and
  // OF come from the last iteration, as Intel defines them for a count of one.
  if (_count == 0)
  {
    return _value;
  }
  const std::uint32_t mask = WidthMask(_word);
  const std::uint32_t sign = SignBit(_word);
  std::uint32_t value = _value & mask;
  bool carry = Flag(FLAG_CARRY);
  const std::uint8_t operation = static_cast<std::uint8_t>(_operation & 7u);

  if (operation == 6)
  {
    // SETMO / SETMOC: the undocumented /6 sets every bit of the operand.
    value = mask;
    SetFlag(FLAG_CARRY, false);
    SetFlag(FLAG_OVERFLOW, false);
    SetFlag(FLAG_AUXILIARY, false);
    SetSignZeroParity(value, _word);
    return ToWord(value);
  }

  for (std::uint32_t iteration = 0; iteration < _count; ++iteration)
  {
    switch (operation)
    {
    case 0: // ROL
      carry = (value & sign) != 0;
      value = ((value << 1) | (carry ? 1u : 0u)) & mask;
      break;
    case 1: // ROR
      carry = (value & 1u) != 0;
      value = (value >> 1) | (carry ? sign : 0u);
      break;
    case 2: // RCL
    {
      const bool out = (value & sign) != 0;
      value = ((value << 1) | (carry ? 1u : 0u)) & mask;
      carry = out;
      break;
    }
    case 3: // RCR
    {
      const bool out = (value & 1u) != 0;
      value = (value >> 1) | (carry ? sign : 0u);
      carry = out;
      break;
    }
    case 4: // SHL, SAL
      carry = (value & sign) != 0;
      value = (value << 1) & mask;
      break;
    case 5: // SHR
      carry = (value & 1u) != 0;
      value >>= 1;
      break;
    default: // SAR
      carry = (value & 1u) != 0;
      value = (value >> 1) | (value & sign);
      break;
    }
  }

  SetFlag(FLAG_CARRY, carry);
  const bool top = (value & sign) != 0;
  const bool next = (value & (sign >> 1)) != 0;
  // Left shifts and rotates: OF is the new top bit against the carry. Right ones: OF is the change
  // in the top bit made by the last step, which the top two bits of the result still show.
  SetFlag(FLAG_OVERFLOW, (operation == 0 || operation == 2 || operation == 4) ? top != carry : top != next);
  if (operation >= 4)
  {
    SetSignZeroParity(value, _word);
  }
  return ToWord(value);
}

// ── Execution ───────────────────────────────────────────────────────────────────────────────

void Cpu::Execute(std::uint8_t _opcode)
{
  switch (_opcode)
  {
  case 0x00:
  case 0x01:
  case 0x02:
  case 0x03:
  case 0x04:
  case 0x05:
  case 0x08:
  case 0x09:
  case 0x0A:
  case 0x0B:
  case 0x0C:
  case 0x0D:
  case 0x10:
  case 0x11:
  case 0x12:
  case 0x13:
  case 0x14:
  case 0x15:
  case 0x18:
  case 0x19:
  case 0x1A:
  case 0x1B:
  case 0x1C:
  case 0x1D:
  case 0x20:
  case 0x21:
  case 0x22:
  case 0x23:
  case 0x24:
  case 0x25:
  case 0x28:
  case 0x29:
  case 0x2A:
  case 0x2B:
  case 0x2C:
  case 0x2D:
  case 0x30:
  case 0x31:
  case 0x32:
  case 0x33:
  case 0x34:
  case 0x35:
  case 0x38:
  case 0x39:
  case 0x3A:
  case 0x3B:
  case 0x3C:
  case 0x3D:
    ExecuteAlu(_opcode);
    break;

  case 0x06: // PUSH ES / CS / SS / DS
  case 0x0E:
  case 0x16:
  case 0x1E:
    Push(Segment(static_cast<std::uint8_t>((_opcode >> 3) & 3)));
    m_cycles += 10 + WORD_TRANSFER_CYCLES;
    break;

  case 0x07: // POP ES / CS (undocumented, 8086/8088 only) / SS / DS
  case 0x0F:
  case 0x17:
  case 0x1F:
    SetSegment(static_cast<std::uint8_t>((_opcode >> 3) & 3), Pop());
    m_cycles += 8 + WORD_TRANSFER_CYCLES;
    break;

  case 0x27: // DAA, DAS, AAA, AAS
  case 0x2F:
  case 0x37:
  case 0x3F:
    ExecuteBcd(_opcode);
    break;

  case 0x40: // INC r16
  case 0x41:
  case 0x42:
  case 0x43:
  case 0x44:
  case 0x45:
  case 0x46:
  case 0x47:
  case 0x48: // DEC r16
  case 0x49:
  case 0x4A:
  case 0x4B:
  case 0x4C:
  case 0x4D:
  case 0x4E:
  case 0x4F:
  {
    const std::uint8_t index = static_cast<std::uint8_t>(_opcode & 7);
    SetReg16(index, IncDec(Reg16(index), _opcode < 0x48, true));
    m_cycles += 2;
    break;
  }

  case 0x50: // PUSH r16
  case 0x51:
  case 0x52:
  case 0x53:
  case 0x54:
  case 0x55:
  case 0x56:
  case 0x57:
  {
    const std::uint8_t index = static_cast<std::uint8_t>(_opcode & 7);
    if (index == REGISTER_SP)
    {
      // The 8086 and 8088 push SP as it is after the decrement; the 80286 and later push the old value.
      m_regs.sp = ToWord(m_regs.sp - 2u);
      m_memory.Write16(m_regs.ss, m_regs.sp, m_regs.sp);
    }
    else
    {
      Push(Reg16(index));
    }
    m_cycles += 11 + WORD_TRANSFER_CYCLES;
    break;
  }

  case 0x58: // POP r16
  case 0x59:
  case 0x5A:
  case 0x5B:
  case 0x5C:
  case 0x5D:
  case 0x5E:
  case 0x5F:
  {
    const std::uint16_t value = Pop();
    SetReg16(static_cast<std::uint8_t>(_opcode & 7), value);
    m_cycles += 8 + WORD_TRANSFER_CYCLES;
    break;
  }

  case 0x60: // Jcc. 60-6F are undocumented aliases of 70-7F.
  case 0x61:
  case 0x62:
  case 0x63:
  case 0x64:
  case 0x65:
  case 0x66:
  case 0x67:
  case 0x68:
  case 0x69:
  case 0x6A:
  case 0x6B:
  case 0x6C:
  case 0x6D:
  case 0x6E:
  case 0x6F:
  case 0x70:
  case 0x71:
  case 0x72:
  case 0x73:
  case 0x74:
  case 0x75:
  case 0x76:
  case 0x77:
  case 0x78:
  case 0x79:
  case 0x7A:
  case 0x7B:
  case 0x7C:
  case 0x7D:
  case 0x7E:
  case 0x7F:
  {
    const bool sign = Flag(FLAG_SIGN);
    const bool overflow = Flag(FLAG_OVERFLOW);
    bool condition = false;
    switch ((_opcode >> 1) & 7)
    {
    case 0:
      condition = overflow;
      break;
    case 1:
      condition = Flag(FLAG_CARRY);
      break;
    case 2:
      condition = Flag(FLAG_ZERO);
      break;
    case 3:
      condition = Flag(FLAG_CARRY) || Flag(FLAG_ZERO);
      break;
    case 4:
      condition = sign;
      break;
    case 5:
      condition = Flag(FLAG_PARITY);
      break;
    case 6:
      condition = sign != overflow;
      break;
    default:
      condition = Flag(FLAG_ZERO) || sign != overflow;
      break;
    }
    JumpShort((_opcode & 1) != 0 ? !condition : condition, 16, 4);
    break;
  }

  case 0x80: // Group 1: ALU r/m, imm. 82 is an undocumented alias of 80.
  case 0x81:
  case 0x82:
  case 0x83:
    ExecuteGroup1(_opcode);
    break;

  case 0x84: // TEST r/m, r
  case 0x85:
  {
    const bool word = (_opcode & 1) != 0;
    const Operand operand = DecodeModRm();
    (void)Logic(static_cast<std::uint32_t>(ReadOperand(operand, word)) & ReadReg(operand.reg, word), word);
    m_cycles += operand.isRegister ? 3 : 9 + WordPenalty(word, 1);
    break;
  }

  case 0x86: // XCHG r/m, r
  case 0x87:
  {
    const bool word = (_opcode & 1) != 0;
    const Operand operand = DecodeModRm();
    const std::uint16_t value = ReadOperand(operand, word);
    WriteOperand(operand, word, ReadReg(operand.reg, word));
    WriteReg(operand.reg, word, value);
    m_cycles += operand.isRegister ? 4 : 17 + WordPenalty(word, 2);
    break;
  }

  case 0x88: // MOV r/m, r
  case 0x89:
  {
    const bool word = (_opcode & 1) != 0;
    const Operand operand = DecodeModRm();
    WriteOperand(operand, word, ReadReg(operand.reg, word));
    m_cycles += operand.isRegister ? 2 : 9 + WordPenalty(word, 1);
    break;
  }

  case 0x8A: // MOV r, r/m
  case 0x8B:
  {
    const bool word = (_opcode & 1) != 0;
    const Operand operand = DecodeModRm();
    WriteReg(operand.reg, word, ReadOperand(operand, word));
    m_cycles += operand.isRegister ? 2 : 8 + WordPenalty(word, 1);
    break;
  }

  case 0x8C: // MOV r/m16, sreg. Only the low two bits of the reg field select the segment register.
  {
    const Operand operand = DecodeModRm();
    WriteOperand(operand, true, Segment(operand.reg));
    m_cycles += operand.isRegister ? 2 : 9 + WORD_TRANSFER_CYCLES;
    break;
  }

  case 0x8D: // LEA. The register form is undefined, and leaves the destination alone here.
  {
    const Operand operand = DecodeModRm();
    if (!operand.isRegister)
    {
      SetReg16(operand.reg, operand.offset);
    }
    m_cycles += 2;
    break;
  }

  case 0x8E: // MOV sreg, r/m16, CS included.
  {
    const Operand operand = DecodeModRm();
    SetSegment(operand.reg, ReadOperand(operand, true));
    m_cycles += operand.isRegister ? 2 : 8 + WORD_TRANSFER_CYCLES;
    break;
  }

  case 0x8F: // POP r/m16. The reg field is ignored.
  {
    const Operand operand = DecodeModRm();
    const std::uint16_t value = Pop();
    WriteOperand(operand, true, value);
    m_cycles += operand.isRegister ? 8 + WORD_TRANSFER_CYCLES : 17 + 2 * WORD_TRANSFER_CYCLES;
    break;
  }

  case 0x90: // XCHG AX, r16; 90 is NOP
  case 0x91:
  case 0x92:
  case 0x93:
  case 0x94:
  case 0x95:
  case 0x96:
  case 0x97:
  {
    const std::uint8_t index = static_cast<std::uint8_t>(_opcode & 7);
    const std::uint16_t value = Reg16(index);
    SetReg16(index, m_regs.ax);
    m_regs.ax = value;
    m_cycles += 3;
    break;
  }

  case 0x98: // CBW
    m_regs.ax = SignExtend8(m_regs.ax);
    m_cycles += 2;
    break;

  case 0x99: // CWD
    m_regs.dx = (m_regs.ax & 0x8000u) != 0 ? std::uint16_t{0xFFFF} : std::uint16_t{0x0000};
    m_cycles += 5;
    break;

  case 0x9A: // CALL far direct
  {
    const std::uint16_t offset = Fetch16();
    const std::uint16_t segment = Fetch16();
    Push(m_regs.cs);
    Push(m_regs.ip);
    m_regs.cs = segment;
    m_regs.ip = offset;
    m_cycles += 28 + 2 * WORD_TRANSFER_CYCLES;
    break;
  }

  case 0x9B: // WAIT: there is no coprocessor, so TEST is never asserted and this returns at once.
    m_cycles += 3;
    break;

  case 0x9C: // PUSHF
    Push(m_regs.flags);
    m_cycles += 10 + WORD_TRANSFER_CYCLES;
    break;

  case 0x9D: // POPF
    SetFlagsWord(Pop());
    m_cycles += 8 + WORD_TRANSFER_CYCLES;
    break;

  case 0x9E: // SAHF
    SetFlagsWord(ToWord((m_regs.flags & 0xFF00u) | (static_cast<std::uint32_t>(m_regs.ax) >> 8)));
    m_cycles += 4;
    break;

  case 0x9F: // LAHF
    SetReg8(4, ToByte(m_regs.flags));
    m_cycles += 4;
    break;

  case 0xA0: // MOV AL/AX, moffs and MOV moffs, AL/AX
  case 0xA1:
  case 0xA2:
  case 0xA3:
  {
    const bool word = (_opcode & 1) != 0;
    const std::uint16_t offset = Fetch16();
    Operand operand;
    operand.segment = SegmentOr(m_regs.ds);
    operand.offset = offset;
    if (_opcode < 0xA2)
    {
      WriteReg(0, word, ReadOperand(operand, word));
    }
    else
    {
      WriteOperand(operand, word, ReadReg(0, word));
    }
    m_cycles += 10 + WordPenalty(word, 1);
    break;
  }

  case 0xA4: // MOVS, CMPS
  case 0xA5:
  case 0xA6:
  case 0xA7:
  case 0xAA: // STOS, LODS, SCAS
  case 0xAB:
  case 0xAC:
  case 0xAD:
  case 0xAE:
  case 0xAF:
    ExecuteString(_opcode);
    break;

  case 0xA8: // TEST AL/AX, imm
  case 0xA9:
  {
    const bool word = (_opcode & 1) != 0;
    const std::uint16_t immediate = FetchImmediate(word);
    (void)Logic(static_cast<std::uint32_t>(ReadReg(0, word)) & immediate, word);
    m_cycles += 4;
    break;
  }

  case 0xB0: // MOV r8, imm8
  case 0xB1:
  case 0xB2:
  case 0xB3:
  case 0xB4:
  case 0xB5:
  case 0xB6:
  case 0xB7:
    SetReg8(static_cast<std::uint8_t>(_opcode & 7), Fetch8());
    m_cycles += 4;
    break;

  case 0xB8: // MOV r16, imm16
  case 0xB9:
  case 0xBA:
  case 0xBB:
  case 0xBC:
  case 0xBD:
  case 0xBE:
  case 0xBF:
    SetReg16(static_cast<std::uint8_t>(_opcode & 7), Fetch16());
    m_cycles += 4;
    break;

  case 0xC0: // RET imm16 (C0 is an undocumented alias of C2)
  case 0xC2:
  {
    const std::uint16_t release = Fetch16();
    m_regs.ip = Pop();
    m_regs.sp = ToWord(m_regs.sp + static_cast<std::uint32_t>(release));
    m_cycles += 12 + WORD_TRANSFER_CYCLES;
    break;
  }

  case 0xC1: // RET (C1 is an undocumented alias of C3)
  case 0xC3:
    m_regs.ip = Pop();
    m_cycles += 8 + WORD_TRANSFER_CYCLES;
    break;

  case 0xC4: // LES, LDS. The register forms are undefined; they load from offset 0 of the segment here.
  case 0xC5:
  {
    const Operand operand = DecodeModRm();
    SetReg16(operand.reg, m_memory.Read16(operand.segment, operand.offset));
    const std::uint16_t segment = m_memory.Read16(operand.segment, ToWord(operand.offset + 2u));
    if (_opcode == 0xC4)
    {
      m_regs.es = segment;
    }
    else
    {
      m_regs.ds = segment;
    }
    m_cycles += 16 + 2 * WORD_TRANSFER_CYCLES;
    break;
  }

  case 0xC6: // MOV r/m, imm. The reg field is ignored.
  case 0xC7:
  {
    const bool word = (_opcode & 1) != 0;
    const Operand operand = DecodeModRm();
    const std::uint16_t immediate = FetchImmediate(word);
    WriteOperand(operand, word, immediate);
    m_cycles += operand.isRegister ? 4 : 10 + WordPenalty(word, 1);
    break;
  }

  case 0xC8: // RETF imm16 (C8 is an undocumented alias of CA)
  case 0xCA:
  {
    const std::uint16_t release = Fetch16();
    m_regs.ip = Pop();
    m_regs.cs = Pop();
    m_regs.sp = ToWord(m_regs.sp + static_cast<std::uint32_t>(release));
    m_cycles += 17 + 2 * WORD_TRANSFER_CYCLES;
    break;
  }

  case 0xC9: // RETF (C9 is an undocumented alias of CB)
  case 0xCB:
    m_regs.ip = Pop();
    m_regs.cs = Pop();
    m_cycles += 18 + 2 * WORD_TRANSFER_CYCLES;
    break;

  case 0xCC: // INT 3
    m_cycles += INTERRUPT_ENTRY_CYCLES + 1;
    SoftwareInterrupt(VECTOR_BREAKPOINT);
    break;

  case 0xCD: // INT imm8
  {
    const std::uint8_t vector = Fetch8();
    m_cycles += INTERRUPT_ENTRY_CYCLES;
    SoftwareInterrupt(vector);
    break;
  }

  case 0xCE: // INTO
    if (Flag(FLAG_OVERFLOW))
    {
      m_cycles += INTERRUPT_ENTRY_CYCLES + 2;
      SoftwareInterrupt(VECTOR_OVERFLOW);
    }
    else
    {
      m_cycles += 4;
    }
    break;

  case 0xCF: // IRET
    m_regs.ip = Pop();
    m_regs.cs = Pop();
    SetFlagsWord(Pop());
    m_cycles += 24 + 3 * WORD_TRANSFER_CYCLES;
    break;

  case 0xD0: // Group 2: shifts and rotates
  case 0xD1:
  case 0xD2:
  case 0xD3:
    ExecuteShift(_opcode);
    break;

  case 0xD4: // AAM, AAD
  case 0xD5:
    ExecuteBcd(_opcode);
    break;

  case 0xD6: // SALC (undocumented): AL = CF ? FF : 00, flags unchanged
    SetReg8(0, Flag(FLAG_CARRY) ? std::uint8_t{0xFF} : std::uint8_t{0x00});
    m_cycles += 4;
    break;

  case 0xD7: // XLAT
    SetReg8(0, m_memory.Read8(SegmentOr(m_regs.ds), ToWord(m_regs.bx + static_cast<std::uint32_t>(Reg8(0)))));
    m_cycles += 11;
    break;

  case 0xD8: // ESC: a coprocessor instruction. The operand is decoded so that IP moves past it.
  case 0xD9:
  case 0xDA:
  case 0xDB:
  case 0xDC:
  case 0xDD:
  case 0xDE:
  case 0xDF:
  {
    const Operand operand = DecodeModRm();
    m_cycles += operand.isRegister ? 2 : 8 + WORD_TRANSFER_CYCLES;
    break;
  }

  case 0xE0: // LOOPNZ
    m_regs.cx = ToWord(m_regs.cx - 1u);
    JumpShort(m_regs.cx != 0 && !Flag(FLAG_ZERO), 19, 5);
    break;

  case 0xE1: // LOOPZ
    m_regs.cx = ToWord(m_regs.cx - 1u);
    JumpShort(m_regs.cx != 0 && Flag(FLAG_ZERO), 18, 6);
    break;

  case 0xE2: // LOOP
    m_regs.cx = ToWord(m_regs.cx - 1u);
    JumpShort(m_regs.cx != 0, 17, 5);
    break;

  case 0xE3: // JCXZ
    JumpShort(m_regs.cx == 0, 18, 6);
    break;

  case 0xE4: // IN AL, imm8
    SetReg8(0, m_ports.In8(Fetch8()));
    m_cycles += 10;
    break;

  case 0xE5: // IN AX, imm8
    m_regs.ax = m_ports.In16(Fetch8());
    m_cycles += 10 + WORD_TRANSFER_CYCLES;
    break;

  case 0xE6: // OUT imm8, AL
    m_ports.Out8(Fetch8(), Reg8(0));
    m_cycles += 10;
    break;

  case 0xE7: // OUT imm8, AX
    m_ports.Out16(Fetch8(), m_regs.ax);
    m_cycles += 10 + WORD_TRANSFER_CYCLES;
    break;

  case 0xE8: // CALL near relative
  {
    const std::uint16_t displacement = Fetch16();
    Push(m_regs.ip);
    m_regs.ip = ToWord(m_regs.ip + static_cast<std::uint32_t>(displacement));
    m_cycles += 19 + WORD_TRANSFER_CYCLES;
    break;
  }

  case 0xE9: // JMP near relative
  {
    const std::uint16_t displacement = Fetch16();
    m_regs.ip = ToWord(m_regs.ip + static_cast<std::uint32_t>(displacement));
    m_cycles += 15;
    break;
  }

  case 0xEA: // JMP far direct
  {
    const std::uint16_t offset = Fetch16();
    m_regs.cs = Fetch16();
    m_regs.ip = offset;
    m_cycles += 15;
    break;
  }

  case 0xEB: // JMP short
    JumpShort(true, 15, 15);
    break;

  case 0xEC: // IN AL, DX
    SetReg8(0, m_ports.In8(m_regs.dx));
    m_cycles += 8;
    break;

  case 0xED: // IN AX, DX
    m_regs.ax = m_ports.In16(m_regs.dx);
    m_cycles += 8 + WORD_TRANSFER_CYCLES;
    break;

  case 0xEE: // OUT DX, AL
    m_ports.Out8(m_regs.dx, Reg8(0));
    m_cycles += 8;
    break;

  case 0xEF: // OUT DX, AX
    m_ports.Out16(m_regs.dx, m_regs.ax);
    m_cycles += 8 + WORD_TRANSFER_CYCLES;
    break;

  case 0xF4: // HLT
    m_halted = true;
    m_cycles += 2;
    break;

  case 0xF5: // CMC
    SetFlag(FLAG_CARRY, !Flag(FLAG_CARRY));
    m_cycles += 2;
    break;

  case 0xF6: // Group 3: TEST, NOT, NEG, MUL, IMUL, DIV, IDIV
  case 0xF7:
    ExecuteGroup3(_opcode);
    break;

  case 0xF8: // CLC
    SetFlag(FLAG_CARRY, false);
    m_cycles += 2;
    break;

  case 0xF9: // STC
    SetFlag(FLAG_CARRY, true);
    m_cycles += 2;
    break;

  case 0xFA: // CLI
    SetFlag(FLAG_INTERRUPT, false);
    m_cycles += 2;
    break;

  case 0xFB: // STI: interrupts are recognised again only after the next instruction.
    SetFlag(FLAG_INTERRUPT, true);
    m_interruptShadow = true;
    m_cycles += 2;
    break;

  case 0xFC: // CLD
    SetFlag(FLAG_DIRECTION, false);
    m_cycles += 2;
    break;

  case 0xFD: // STD
    SetFlag(FLAG_DIRECTION, true);
    m_cycles += 2;
    break;

  case 0xFE: // Group 4 and 5: INC, DEC, CALL, JMP, PUSH
  case 0xFF:
    ExecuteGroup4And5(_opcode);
    break;

  default: // The prefixes, which Step() consumes before it gets here.
    break;
  }
}

void Cpu::ExecuteAlu(std::uint8_t _opcode)
{
  const std::uint8_t operation = static_cast<std::uint8_t>((_opcode >> 3) & 7);
  const bool word = (_opcode & 1) != 0;
  const bool store = operation != ALU_CMP;
  switch (_opcode & 7)
  {
  case 0: // r/m, r
  case 1:
  {
    const Operand operand = DecodeModRm();
    const std::uint16_t result = Alu(operation, ReadOperand(operand, word), ReadReg(operand.reg, word), word);
    if (store)
    {
      WriteOperand(operand, word, result);
    }
    if (operand.isRegister)
    {
      m_cycles += 3;
    }
    else
    {
      m_cycles += store ? 16 + WordPenalty(word, 2) : 9 + WordPenalty(word, 1);
    }
    break;
  }
  case 2: // r, r/m
  case 3:
  {
    const Operand operand = DecodeModRm();
    const std::uint16_t result = Alu(operation, ReadReg(operand.reg, word), ReadOperand(operand, word), word);
    if (store)
    {
      WriteReg(operand.reg, word, result);
    }
    m_cycles += operand.isRegister ? 3 : 9 + WordPenalty(word, 1);
    break;
  }
  default: // AL/AX, imm
  {
    const std::uint16_t immediate = FetchImmediate(word);
    const std::uint16_t result = Alu(operation, ReadReg(0, word), immediate, word);
    if (store)
    {
      WriteReg(0, word, result);
    }
    m_cycles += 4;
    break;
  }
  }
}

void Cpu::ExecuteGroup1(std::uint8_t _opcode)
{
  const bool word = (_opcode & 1) != 0;
  const Operand operand = DecodeModRm();
  std::uint16_t immediate = 0;
  if (_opcode == 0x81)
  {
    immediate = Fetch16();
  }
  else if (_opcode == 0x83)
  {
    immediate = SignExtend8(Fetch8());
  }
  else
  {
    immediate = Fetch8();
  }
  const bool store = operand.reg != ALU_CMP;
  const std::uint16_t result = Alu(operand.reg, ReadOperand(operand, word), immediate, word);
  if (store)
  {
    WriteOperand(operand, word, result);
  }
  if (operand.isRegister)
  {
    m_cycles += 4;
  }
  else
  {
    m_cycles += store ? 17 + WordPenalty(word, 2) : 10 + WordPenalty(word, 1);
  }
}

void Cpu::ExecuteShift(std::uint8_t _opcode)
{
  const bool word = (_opcode & 1) != 0;
  const bool byCount = _opcode >= 0xD2;
  const Operand operand = DecodeModRm();
  const std::uint32_t count = byCount ? (m_regs.cx & 0xFFu) : 1u;
  const std::uint16_t result = Shift(operand.reg, ReadOperand(operand, word), count, word);
  WriteOperand(operand, word, result);
  if (operand.isRegister)
  {
    m_cycles += byCount ? 8 + 4 * count : 2;
  }
  else
  {
    m_cycles += (byCount ? 20 + 4 * count : 15) + WordPenalty(word, 2);
  }
}

void Cpu::ExecuteGroup3(std::uint8_t _opcode)
{
  const bool word = (_opcode & 1) != 0;
  const Operand operand = DecodeModRm();
  switch (operand.reg)
  {
  case 0: // TEST r/m, imm (/1 is an undocumented alias)
  case 1:
  {
    const std::uint16_t immediate = FetchImmediate(word);
    (void)Logic(static_cast<std::uint32_t>(ReadOperand(operand, word)) & immediate, word);
    m_cycles += operand.isRegister ? 5 : 11 + WordPenalty(word, 1);
    break;
  }
  case 2: // NOT
    WriteOperand(operand, word, ToWord(~static_cast<std::uint32_t>(ReadOperand(operand, word))));
    m_cycles += operand.isRegister ? 3 : 16 + WordPenalty(word, 2);
    break;
  case 3: // NEG
    WriteOperand(operand, word, Subtract(0, ReadOperand(operand, word), 0, word));
    m_cycles += operand.isRegister ? 3 : 16 + WordPenalty(word, 2);
    break;
  case 4: // MUL
    ExecuteMultiply(operand, word, false);
    break;
  case 5: // IMUL
    ExecuteMultiply(operand, word, true);
    break;
  case 6: // DIV
    ExecuteDivide(operand, word, false);
    break;
  default: // IDIV
    ExecuteDivide(operand, word, true);
    break;
  }
}

void Cpu::ExecuteGroup4And5(std::uint8_t _opcode)
{
  const bool word = _opcode == 0xFF;
  const Operand operand = DecodeModRm();
  switch (operand.reg)
  {
  case 0: // INC r/m
  case 1: // DEC r/m
    WriteOperand(operand, word, IncDec(ReadOperand(operand, word), operand.reg == 0, word));
    if (operand.isRegister)
    {
      m_cycles += word ? 2 : 3;
    }
    else
    {
      m_cycles += 15 + WordPenalty(word, 2);
    }
    break;
  case 2: // CALL near r/m16
  {
    const std::uint16_t target = ReadOperand(operand, true);
    Push(m_regs.ip);
    m_regs.ip = target;
    m_cycles += operand.isRegister ? 16 + WORD_TRANSFER_CYCLES : 21 + 2 * WORD_TRANSFER_CYCLES;
    break;
  }
  case 3: // CALL far m16:16
  {
    const std::uint16_t offset = m_memory.Read16(operand.segment, operand.offset);
    const std::uint16_t segment = m_memory.Read16(operand.segment, ToWord(operand.offset + 2u));
    Push(m_regs.cs);
    Push(m_regs.ip);
    m_regs.cs = segment;
    m_regs.ip = offset;
    m_cycles += 37 + 4 * WORD_TRANSFER_CYCLES;
    break;
  }
  case 4: // JMP near r/m16
    m_regs.ip = ReadOperand(operand, true);
    m_cycles += operand.isRegister ? 11 : 18 + WORD_TRANSFER_CYCLES;
    break;
  case 5: // JMP far m16:16
  {
    const std::uint16_t offset = m_memory.Read16(operand.segment, operand.offset);
    m_regs.cs = m_memory.Read16(operand.segment, ToWord(operand.offset + 2u));
    m_regs.ip = offset;
    m_cycles += 24 + 2 * WORD_TRANSFER_CYCLES;
    break;
  }
  default: // PUSH r/m16 (/7 is an undocumented alias)
  {
    // Pushing SP itself stores the decremented value, exactly as opcode 54 does.
    const bool stackPointer = operand.isRegister && operand.rm == REGISTER_SP;
    const std::uint16_t value = stackPointer ? ToWord(m_regs.sp - 2u) : ReadOperand(operand, true);
    Push(value);
    m_cycles += operand.isRegister ? 11 + WORD_TRANSFER_CYCLES : 16 + 2 * WORD_TRANSFER_CYCLES;
    break;
  }
  }
}

void Cpu::ExecuteString(std::uint8_t _opcode)
{
  const bool word = (_opcode & 1) != 0;
  const std::uint32_t size = word ? 2u : 1u;
  const std::uint32_t step = Flag(FLAG_DIRECTION) ? 0x10000u - size : size;
  const std::uint16_t source = SegmentOr(m_regs.ds);
  const std::uint8_t kind = static_cast<std::uint8_t>(_opcode & 0xFE);
  const bool compares = kind == 0xA6 || kind == 0xAE;

  // Clocks per element, and (for a REP) per repetition, from Intel's table.
  std::uint32_t single = 0;
  std::uint32_t repeated = 0;
  std::uint32_t transfers = 0;
  switch (kind)
  {
  case 0xA4: // MOVS
    single = 18;
    repeated = 17;
    transfers = 2;
    break;
  case 0xA6: // CMPS
    single = 22;
    repeated = 22;
    transfers = 2;
    break;
  case 0xAA: // STOS
    single = 11;
    repeated = 10;
    transfers = 1;
    break;
  case 0xAC: // LODS
    single = 12;
    repeated = 13;
    transfers = 1;
    break;
  default: // SCAS
    single = 15;
    repeated = 15;
    transfers = 1;
    break;
  }

  const auto once = [&]
  {
    switch (kind)
    {
    case 0xA4:
      if (word)
      {
        m_memory.Write16(m_regs.es, m_regs.di, m_memory.Read16(source, m_regs.si));
      }
      else
      {
        m_memory.Write8(m_regs.es, m_regs.di, m_memory.Read8(source, m_regs.si));
      }
      m_regs.si = ToWord(m_regs.si + step);
      m_regs.di = ToWord(m_regs.di + step);
      break;
    case 0xA6:
    {
      const std::uint16_t left = ReadMemory(source, m_regs.si, word);
      const std::uint16_t right = ReadMemory(m_regs.es, m_regs.di, word);
      (void)Subtract(left, right, 0, word);
      m_regs.si = ToWord(m_regs.si + step);
      m_regs.di = ToWord(m_regs.di + step);
      break;
    }
    case 0xAA:
      if (word)
      {
        m_memory.Write16(m_regs.es, m_regs.di, m_regs.ax);
      }
      else
      {
        m_memory.Write8(m_regs.es, m_regs.di, Reg8(0));
      }
      m_regs.di = ToWord(m_regs.di + step);
      break;
    case 0xAC:
      WriteReg(0, word, ReadMemory(source, m_regs.si, word));
      m_regs.si = ToWord(m_regs.si + step);
      break;
    default:
    {
      const std::uint16_t right = ReadMemory(m_regs.es, m_regs.di, word);
      (void)Subtract(ReadReg(0, word), right, 0, word);
      m_regs.di = ToWord(m_regs.di + step);
      break;
    }
    }
  };

  if (m_repeat == Repeat::None)
  {
    once();
    m_cycles += single + WordPenalty(word, transfers);
    return;
  }

  // REP runs the whole repetition here, in one Step() (see Cpu.h). F2 and F3 differ only for
  // CMPS and SCAS; for the others either one simply repeats CX times.
  m_cycles += 9;
  while (m_regs.cx != 0)
  {
    once();
    m_regs.cx = ToWord(m_regs.cx - 1u);
    m_cycles += repeated + WordPenalty(word, transfers);
    if (compares && (Flag(FLAG_ZERO) != (m_repeat == Repeat::WhileZero)))
    {
      break;
    }
  }
}

void Cpu::ExecuteBcd(std::uint8_t _opcode)
{
  const std::uint8_t al = Reg8(0);
  switch (_opcode)
  {
  case 0x27: // DAA
  case 0x2F: // DAS
  {
    // As the 8088 does it, which differs from Intel's later pseudocode in two places (measured
    // against the SingleStepTests suite): the high digit is adjusted when AL was above 9F rather
    // than 99 if AF was set, and CF comes from the high adjustment alone, never from a carry or
    // borrow out of the low one.
    const bool subtract = _opcode == 0x2F;
    const bool auxiliary = Flag(FLAG_AUXILIARY);
    const bool lowAdjust = (al & 0x0Fu) > 9 || auxiliary;
    const bool highAdjust = al > (auxiliary ? 0x9Fu : 0x99u) || Flag(FLAG_CARRY);
    std::uint32_t result = al;
    if (lowAdjust)
    {
      result = subtract ? result - 0x06u : result + 0x06u;
    }
    if (highAdjust)
    {
      result = subtract ? result - 0x60u : result + 0x60u;
    }
    SetFlag(FLAG_AUXILIARY, lowAdjust);
    SetFlag(FLAG_CARRY, highAdjust);
    SetSignZeroParity(result, false);
    SetReg8(0, ToByte(result));
    m_cycles += 4;
    break;
  }
  case 0x37: // AAA
  case 0x3F: // AAS
  {
    const bool subtract = _opcode == 0x3F;
    std::uint32_t low = al;
    std::uint32_t high = static_cast<std::uint32_t>(m_regs.ax) >> 8;
    const bool adjust = (al & 0x0Fu) > 9 || Flag(FLAG_AUXILIARY);
    if (adjust)
    {
      low = subtract ? low - 6u : low + 6u;
      high = subtract ? high - 1u : high + 1u;
    }
    SetFlag(FLAG_AUXILIARY, adjust);
    SetFlag(FLAG_CARRY, adjust);
    SetSignZeroParity(low & 0x0Fu, false);
    m_regs.ax = ToWord(((high & 0xFFu) << 8) | (low & 0x0Fu));
    m_cycles += 4;
    break;
  }
  case 0xD4: // AAM imm8: an 8-bit division, and a divide error when the base is zero.
  {
    const std::uint8_t base = Fetch8();
    if (base == 0)
    {
      // AAM divides with the same microcode as DIV, whose first step subtracts the divisor from
      // the high half (here zero); the divide error pushes that subtraction's flags.
      (void)Subtract(0, 0, 0, false);
      m_cycles += 83 + INTERRUPT_ENTRY_CYCLES;
      EnterInterrupt(VECTOR_DIVIDE_ERROR);
      break;
    }
    const std::uint32_t quotient = al / static_cast<std::uint32_t>(base);
    const std::uint32_t remainder = al % static_cast<std::uint32_t>(base);
    m_regs.ax = ToWord((quotient << 8) | remainder);
    SetSignZeroParity(remainder, false);
    m_cycles += 83;
    break;
  }
  default: // AAD imm8
  {
    const std::uint8_t base = Fetch8();
    const std::uint32_t high = static_cast<std::uint32_t>(m_regs.ax) >> 8;
    const std::uint16_t result = Add(al, (high * base) & 0xFFu, 0, false);
    m_regs.ax = ToWord(result & 0xFFu);
    m_cycles += 60;
    break;
  }
  }
}

void Cpu::ExecuteMultiply(const Operand& _operand, bool _word, bool _signed)
{
  const std::uint32_t source = ReadOperand(_operand, _word);
  // A REP prefix in front of IMUL negates the product: the microcode keeps its sign in the same
  // internal flag the prefix sets.
  const bool negate = _signed && m_repeat != Repeat::None;
  if (!_word)
  {
    std::uint32_t product = 0;
    bool overflow = false;
    if (_signed)
    {
      std::int32_t signedProduct = AsSigned(m_regs.ax, false) * AsSigned(source, false);
      if (negate)
      {
        signedProduct = -signedProduct;
      }
      product = static_cast<std::uint32_t>(signedProduct) & 0xFFFFu;
      overflow = signedProduct != AsSigned(product, false);
    }
    else
    {
      product = (m_regs.ax & 0xFFu) * source;
      overflow = (product & 0xFF00u) != 0;
    }
    m_regs.ax = ToWord(product);
    SetFlag(FLAG_CARRY, overflow);
    SetFlag(FLAG_OVERFLOW, overflow);
    SetSignZeroParity(product >> 8, false);
    m_cycles += _signed ? 80 : 70;
  }
  else
  {
    std::uint32_t product = 0;
    bool overflow = false;
    if (_signed)
    {
      std::int64_t signedProduct = static_cast<std::int64_t>(AsSigned(m_regs.ax, true)) * AsSigned(source, true);
      if (negate)
      {
        signedProduct = -signedProduct;
      }
      product = static_cast<std::uint32_t>(signedProduct & 0xFFFFFFFF);
      overflow = signedProduct != AsSigned(product, true);
    }
    else
    {
      product = static_cast<std::uint32_t>(m_regs.ax) * source;
      overflow = (product & 0xFFFF0000u) != 0;
    }
    m_regs.ax = ToWord(product);
    m_regs.dx = ToWord(product >> 16);
    SetFlag(FLAG_CARRY, overflow);
    SetFlag(FLAG_OVERFLOW, overflow);
    SetSignZeroParity(product >> 16, true);
    m_cycles += _signed ? 128 : 118;
  }
  if (!_operand.isRegister)
  {
    m_cycles += 6 + WordPenalty(_word, 1);
  }
}

void Cpu::ExecuteDivide(const Operand& _operand, bool _word, bool _signed)
{
  const std::uint32_t divisor = ReadOperand(_operand, _word);
  const std::uint32_t mask = WidthMask(_word);
  const std::uint32_t shift = _word ? 16u : 8u;
  const std::uint32_t dividend = _word ? (static_cast<std::uint32_t>(m_regs.dx) << 16) | m_regs.ax : m_regs.ax;
  const std::uint32_t high = dividend >> shift;
  const std::uint32_t low = dividend & mask;
  m_cycles += _signed ? (_word ? 165u : 101u) : (_word ? 144u : 80u);
  if (!_operand.isRegister)
  {
    m_cycles += 6 + WordPenalty(_word, 1);
  }

  std::uint32_t quotient = 0;
  std::uint32_t remainder = 0;
  bool fault = false;
  if (!_signed)
  {
    // The microcode's first step subtracts the divisor from the high half; no borrow means the
    // quotient cannot fit, and the divide error leaves that subtraction's flags behind.
    if (high >= divisor)
    {
      (void)Subtract(high, divisor, 0, _word);
      fault = true;
    }
    else
    {
      quotient = dividend / divisor;
      remainder = dividend % divisor;
    }
  }
  else
  {
    // IDIV makes both operands positive, remembering their signs, and divides the magnitudes as
    // DIV does, with the same first-step check and the same flags when it fails.
    const bool dividendNegative = (high & SignBit(_word)) != 0;
    const bool divisorNegative = (divisor & SignBit(_word)) != 0;
    const std::uint64_t full = (static_cast<std::uint64_t>(high) << shift) | low;
    const std::uint64_t fullMask = (std::uint64_t{1} << (2 * shift)) - 1;
    const std::uint64_t dividendMagnitude = dividendNegative ? ((~full + 1) & fullMask) : full;
    const std::uint32_t divisorMagnitude = divisorNegative ? ((~divisor + 1u) & mask) : divisor;
    const std::uint32_t highMagnitude = static_cast<std::uint32_t>(dividendMagnitude >> shift);
    if (highMagnitude >= divisorMagnitude)
    {
      (void)Subtract(highMagnitude, divisorMagnitude, 0, _word);
      fault = true;
    }
    else
    {
      std::uint32_t magnitude = static_cast<std::uint32_t>(dividendMagnitude / divisorMagnitude);
      const std::uint32_t rest = static_cast<std::uint32_t>(dividendMagnitude % divisorMagnitude);
      // The 8088 refuses a quotient whose magnitude reaches the sign bit, so -128 and -32768 fault
      // too. The flags it pushes are those of the division loop's last trial subtraction of the
      // divisor from the partial remainder, with CF clear (measured against the SingleStepTests
      // suite: every IDIV fault of this kind in F6.7 and F7.7 matches).
      if ((magnitude & SignBit(_word)) != 0)
      {
        const std::uint32_t partial = (magnitude & 1u) != 0 ? rest + divisorMagnitude : rest;
        (void)Subtract(partial, divisorMagnitude, 0, _word);
        SetFlag(FLAG_CARRY, false);
        fault = true;
      }
      else
      {
        bool negative = dividendNegative != divisorNegative;
        if (m_repeat != Repeat::None)
        {
          negative = !negative;
        }
        magnitude = negative ? (~magnitude + 1u) & mask : magnitude;
        quotient = magnitude;
        remainder = dividendNegative ? (~rest + 1u) & mask : rest;
      }
    }
  }

  if (fault)
  {
    m_cycles += INTERRUPT_ENTRY_CYCLES;
    EnterInterrupt(VECTOR_DIVIDE_ERROR);
    return;
  }
  if (_word)
  {
    m_regs.ax = ToWord(quotient);
    m_regs.dx = ToWord(remainder);
  }
  else
  {
    m_regs.ax = ToWord(((remainder & 0xFFu) << 8) | (quotient & 0xFFu));
  }
}

} // namespace Machine
