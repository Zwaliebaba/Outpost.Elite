#include "pch.h"

#include "Cpu6502.h"

#include <cstring>

namespace Elite::Testing
{

namespace
{
constexpr std::uint16_t STACK_BASE = 0x0100;
constexpr std::uint8_t FLAG_UNUSED = 0x20;
constexpr std::uint8_t FLAG_BREAK = 0x10;
} // namespace

void Cpu6502::Reset() noexcept
{
  memory.fill(0);
  a = x = y = 0;
  sp = 0xFD;
  pc = 0;
  c = z = i = d = v = n = false;
}

std::uint8_t Cpu6502::StatusByte() const noexcept
{
  std::uint8_t status = FLAG_UNUSED;
  if (c)
  {
    status |= 0x01u;
  }
  if (z)
  {
    status |= 0x02u;
  }
  if (i)
  {
    status |= 0x04u;
  }
  if (d)
  {
    status |= 0x08u;
  }
  if (v)
  {
    status |= 0x40u;
  }
  if (n)
  {
    status |= 0x80u;
  }
  return status;
}

void Cpu6502::SetStatusByte(std::uint8_t _status) noexcept
{
  c = (_status & 0x01u) != 0u;
  z = (_status & 0x02u) != 0u;
  i = (_status & 0x04u) != 0u;
  d = (_status & 0x08u) != 0u;
  v = (_status & 0x40u) != 0u;
  n = (_status & 0x80u) != 0u;
}

void Cpu6502::Load(std::uint16_t _address, const std::uint8_t* _bytes, std::size_t _count) noexcept
{
  for (std::size_t index = 0; index < _count; ++index)
  {
    memory[static_cast<std::uint16_t>(_address + index)] = _bytes[index];
  }
}

std::uint16_t Cpu6502::ReadWord(std::uint16_t _address) const noexcept
{
  return static_cast<std::uint16_t>(memory[_address] | (memory[static_cast<std::uint16_t>(_address + 1)] << 8));
}

std::uint16_t Cpu6502::FetchWord() noexcept
{
  const std::uint8_t lo = Fetch();
  const std::uint8_t hi = Fetch();
  return static_cast<std::uint16_t>(lo | (hi << 8));
}

void Cpu6502::Push(std::uint8_t _value) noexcept
{
  memory[static_cast<std::uint16_t>(STACK_BASE + sp)] = _value;
  --sp;
}

std::uint8_t Cpu6502::Pop() noexcept
{
  ++sp;
  return memory[static_cast<std::uint16_t>(STACK_BASE + sp)];
}

std::uint16_t Cpu6502::AddrIndirectX() noexcept
{
  const std::uint8_t base = static_cast<std::uint8_t>(Fetch() + x);
  return static_cast<std::uint16_t>(memory[base] | (memory[static_cast<std::uint8_t>(base + 1)] << 8));
}

std::uint16_t Cpu6502::AddrIndirectY() noexcept
{
  const std::uint8_t base = Fetch();
  const std::uint16_t address = static_cast<std::uint16_t>(memory[base] | (memory[static_cast<std::uint8_t>(base + 1)] << 8));
  return static_cast<std::uint16_t>(address + y);
}

/*
 * ADC, including decimal mode.
 *
 * Elite clears D at startup and never sets it, so the binary path is the one that matters --
 * but the decimal path costs a dozen lines and removes a whole category of "why does this one
 * routine disagree" from the oracle, so it is here. The NMOS part is that N and V come from
 * the intermediate value and Z from the plain binary sum, which is not what the CMOS parts do.
 */
void Cpu6502::Adc(std::uint8_t _operand) noexcept
{
  const std::uint16_t binary = static_cast<std::uint16_t>(a) + _operand + (c ? 1u : 0u);

  if (!d)
  {
    const std::uint8_t result = static_cast<std::uint8_t>(binary);
    v = ((~(static_cast<unsigned>(a) ^ _operand) & (static_cast<unsigned>(a) ^ result)) & 0x80u) != 0u;
    c = binary > 0xFFu;
    a = result;
    SetNz(a);
    return;
  }

  std::uint16_t lo = static_cast<std::uint16_t>((a & 0x0Fu) + (_operand & 0x0Fu) + (c ? 1u : 0u));
  std::uint16_t hi = static_cast<std::uint16_t>((a >> 4) + (_operand >> 4));

  if (lo > 9u)
  {
    lo += 6u;
    ++hi;
  }

  const std::uint8_t interim = static_cast<std::uint8_t>((hi << 4) | (lo & 0x0Fu));
  n = (interim & 0x80u) != 0u;
  v = ((~(static_cast<unsigned>(a) ^ _operand) & (static_cast<unsigned>(a) ^ interim)) & 0x80u) != 0u;
  z = static_cast<std::uint8_t>(binary) == 0u;

  if (hi > 9u)
  {
    hi += 6u;
  }

  c = hi > 0x0Fu;
  a = static_cast<std::uint8_t>((hi << 4) | (lo & 0x0Fu));
}

void Cpu6502::Sbc(std::uint8_t _operand) noexcept
{
  const std::uint16_t binary = static_cast<std::uint16_t>(a) - _operand - (c ? 0u : 1u);
  const std::uint8_t result = static_cast<std::uint8_t>(binary);

  // The flags come from the binary subtraction in both modes on NMOS parts.
  v = (((static_cast<unsigned>(a) ^ _operand) & (static_cast<unsigned>(a) ^ result)) & 0x80u) != 0u;
  c = binary < 0x100u;
  SetNz(result);

  if (!d)
  {
    a = result;
    return;
  }

  std::uint16_t lo = static_cast<std::uint16_t>((a & 0x0Fu) - (_operand & 0x0Fu) - (c ? 0u : 1u));
  std::uint16_t hi = static_cast<std::uint16_t>((a >> 4) - (_operand >> 4));

  if ((lo & 0x10u) != 0u)
  {
    lo -= 6u;
    --hi;
  }
  if ((hi & 0x10u) != 0u)
  {
    hi -= 6u;
  }

  a = static_cast<std::uint8_t>((hi << 4) | (lo & 0x0Fu));
}

void Cpu6502::Compare(std::uint8_t _register, std::uint8_t _operand) noexcept
{
  const std::uint16_t difference = static_cast<std::uint16_t>(_register) - _operand;
  c = _register >= _operand;
  SetNz(static_cast<std::uint8_t>(difference));
}

void Cpu6502::Branch(bool _condition) noexcept
{
  const std::int8_t offset = static_cast<std::int8_t>(Fetch());
  if (_condition)
  {
    pc = static_cast<std::uint16_t>(pc + offset);
  }
}

std::uint8_t Cpu6502::ShiftLeft(std::uint8_t _value) noexcept
{
  c = (_value & 0x80u) != 0u;
  const std::uint8_t result = static_cast<std::uint8_t>(_value << 1);
  SetNz(result);
  return result;
}

std::uint8_t Cpu6502::ShiftRight(std::uint8_t _value) noexcept
{
  c = (_value & 0x01u) != 0u;
  const std::uint8_t result = static_cast<std::uint8_t>(_value >> 1);
  SetNz(result);
  return result;
}

std::uint8_t Cpu6502::RollLeft(std::uint8_t _value) noexcept
{
  const std::uint8_t result = static_cast<std::uint8_t>((_value << 1) | (c ? 1u : 0u));
  c = (_value & 0x80u) != 0u;
  SetNz(result);
  return result;
}

std::uint8_t Cpu6502::RollRight(std::uint8_t _value) noexcept
{
  const std::uint8_t result = static_cast<std::uint8_t>((_value >> 1) | (c ? 0x80u : 0u));
  c = (_value & 0x01u) != 0u;
  SetNz(result);
  return result;
}

void Cpu6502::AddTrap(std::uint16_t _address)
{
  for (const std::uint16_t existing : traps)
  {
    if (existing == _address)
    {
      return;
    }
  }
  traps.push_back(_address);
}

bool Cpu6502::Step() noexcept
{
  // A trapped address is recorded and returned from rather than executed. The pop mirrors what
  // the routine's own RTS would have done, so the caller continues as if it had run.
  if (!traps.empty())
  {
    for (const std::uint16_t trapped : traps)
    {
      if (pc == trapped)
      {
        trapHits.push_back(TrapHit{ pc, a, x, y });
        const std::uint8_t lo = Pop();
        const std::uint8_t hi = Pop();
        pc = static_cast<std::uint16_t>((lo | (hi << 8)) + 1);
        return true;
      }
    }
  }

  const std::uint16_t opcodeAddress = pc;
  const std::uint8_t opcode = Fetch();

  switch (opcode)
  {
    // ---- load and store ----------------------------------------------------------------
    case 0xA9: a = Fetch();                     SetNz(a); break;
    case 0xA5: a = memory[AddrZeroPage()];      SetNz(a); break;
    case 0xB5: a = memory[AddrZeroPageX()];     SetNz(a); break;
    case 0xAD: a = memory[AddrAbsolute()];      SetNz(a); break;
    case 0xBD: a = memory[AddrAbsoluteX()];     SetNz(a); break;
    case 0xB9: a = memory[AddrAbsoluteY()];     SetNz(a); break;
    case 0xA1: a = memory[AddrIndirectX()];     SetNz(a); break;
    case 0xB1: a = memory[AddrIndirectY()];     SetNz(a); break;

    case 0xA2: x = Fetch();                     SetNz(x); break;
    case 0xA6: x = memory[AddrZeroPage()];      SetNz(x); break;
    case 0xB6: x = memory[AddrZeroPageY()];     SetNz(x); break;
    case 0xAE: x = memory[AddrAbsolute()];      SetNz(x); break;
    case 0xBE: x = memory[AddrAbsoluteY()];     SetNz(x); break;

    case 0xA0: y = Fetch();                     SetNz(y); break;
    case 0xA4: y = memory[AddrZeroPage()];      SetNz(y); break;
    case 0xB4: y = memory[AddrZeroPageX()];     SetNz(y); break;
    case 0xAC: y = memory[AddrAbsolute()];      SetNz(y); break;
    case 0xBC: y = memory[AddrAbsoluteX()];     SetNz(y); break;

    case 0x85: memory[AddrZeroPage()] = a; break;
    case 0x95: memory[AddrZeroPageX()] = a; break;
    case 0x8D: memory[AddrAbsolute()] = a; break;
    case 0x9D: memory[AddrAbsoluteX()] = a; break;
    case 0x99: memory[AddrAbsoluteY()] = a; break;
    case 0x81: memory[AddrIndirectX()] = a; break;
    case 0x91: memory[AddrIndirectY()] = a; break;

    case 0x86: memory[AddrZeroPage()] = x; break;
    case 0x96: memory[AddrZeroPageY()] = x; break;
    case 0x8E: memory[AddrAbsolute()] = x; break;

    case 0x84: memory[AddrZeroPage()] = y; break;
    case 0x94: memory[AddrZeroPageX()] = y; break;
    case 0x8C: memory[AddrAbsolute()] = y; break;

    // ---- register transfers ------------------------------------------------------------
    case 0xAA: x = a; SetNz(x); break;
    case 0xA8: y = a; SetNz(y); break;
    case 0x8A: a = x; SetNz(a); break;
    case 0x98: a = y; SetNz(a); break;
    case 0xBA: x = sp; SetNz(x); break;
    case 0x9A: sp = x; break;

    // ---- stack ------------------------------------------------------------------------
    case 0x48: Push(a); break;
    case 0x68: a = Pop(); SetNz(a); break;
    case 0x08: Push(static_cast<std::uint8_t>(StatusByte() | FLAG_BREAK)); break;
    case 0x28: SetStatusByte(Pop()); break;

    // ---- arithmetic -------------------------------------------------------------------
    case 0x69: Adc(Fetch()); break;
    case 0x65: Adc(memory[AddrZeroPage()]); break;
    case 0x75: Adc(memory[AddrZeroPageX()]); break;
    case 0x6D: Adc(memory[AddrAbsolute()]); break;
    case 0x7D: Adc(memory[AddrAbsoluteX()]); break;
    case 0x79: Adc(memory[AddrAbsoluteY()]); break;
    case 0x61: Adc(memory[AddrIndirectX()]); break;
    case 0x71: Adc(memory[AddrIndirectY()]); break;

    case 0xE9: Sbc(Fetch()); break;
    case 0xE5: Sbc(memory[AddrZeroPage()]); break;
    case 0xF5: Sbc(memory[AddrZeroPageX()]); break;
    case 0xED: Sbc(memory[AddrAbsolute()]); break;
    case 0xFD: Sbc(memory[AddrAbsoluteX()]); break;
    case 0xF9: Sbc(memory[AddrAbsoluteY()]); break;
    case 0xE1: Sbc(memory[AddrIndirectX()]); break;
    case 0xF1: Sbc(memory[AddrIndirectY()]); break;

    case 0xC9: Compare(a, Fetch()); break;
    case 0xC5: Compare(a, memory[AddrZeroPage()]); break;
    case 0xD5: Compare(a, memory[AddrZeroPageX()]); break;
    case 0xCD: Compare(a, memory[AddrAbsolute()]); break;
    case 0xDD: Compare(a, memory[AddrAbsoluteX()]); break;
    case 0xD9: Compare(a, memory[AddrAbsoluteY()]); break;
    case 0xC1: Compare(a, memory[AddrIndirectX()]); break;
    case 0xD1: Compare(a, memory[AddrIndirectY()]); break;

    case 0xE0: Compare(x, Fetch()); break;
    case 0xE4: Compare(x, memory[AddrZeroPage()]); break;
    case 0xEC: Compare(x, memory[AddrAbsolute()]); break;

    case 0xC0: Compare(y, Fetch()); break;
    case 0xC4: Compare(y, memory[AddrZeroPage()]); break;
    case 0xCC: Compare(y, memory[AddrAbsolute()]); break;

    // ---- increment and decrement -------------------------------------------------------
    case 0xE6: { const std::uint16_t at = AddrZeroPage();  SetNz(++memory[at]); break; }
    case 0xF6: { const std::uint16_t at = AddrZeroPageX(); SetNz(++memory[at]); break; }
    case 0xEE: { const std::uint16_t at = AddrAbsolute();  SetNz(++memory[at]); break; }
    case 0xFE: { const std::uint16_t at = AddrAbsoluteX(); SetNz(++memory[at]); break; }

    case 0xC6: { const std::uint16_t at = AddrZeroPage();  SetNz(--memory[at]); break; }
    case 0xD6: { const std::uint16_t at = AddrZeroPageX(); SetNz(--memory[at]); break; }
    case 0xCE: { const std::uint16_t at = AddrAbsolute();  SetNz(--memory[at]); break; }
    case 0xDE: { const std::uint16_t at = AddrAbsoluteX(); SetNz(--memory[at]); break; }

    case 0xE8: SetNz(++x); break;
    case 0xC8: SetNz(++y); break;
    case 0xCA: SetNz(--x); break;
    case 0x88: SetNz(--y); break;

    // ---- logic ------------------------------------------------------------------------
    case 0x29: a &= Fetch();                    SetNz(a); break;
    case 0x25: a &= memory[AddrZeroPage()];     SetNz(a); break;
    case 0x35: a &= memory[AddrZeroPageX()];    SetNz(a); break;
    case 0x2D: a &= memory[AddrAbsolute()];     SetNz(a); break;
    case 0x3D: a &= memory[AddrAbsoluteX()];    SetNz(a); break;
    case 0x39: a &= memory[AddrAbsoluteY()];    SetNz(a); break;
    case 0x21: a &= memory[AddrIndirectX()];    SetNz(a); break;
    case 0x31: a &= memory[AddrIndirectY()];    SetNz(a); break;

    case 0x09: a |= Fetch();                    SetNz(a); break;
    case 0x05: a |= memory[AddrZeroPage()];     SetNz(a); break;
    case 0x15: a |= memory[AddrZeroPageX()];    SetNz(a); break;
    case 0x0D: a |= memory[AddrAbsolute()];     SetNz(a); break;
    case 0x1D: a |= memory[AddrAbsoluteX()];    SetNz(a); break;
    case 0x19: a |= memory[AddrAbsoluteY()];    SetNz(a); break;
    case 0x01: a |= memory[AddrIndirectX()];    SetNz(a); break;
    case 0x11: a |= memory[AddrIndirectY()];    SetNz(a); break;

    case 0x49: a ^= Fetch();                    SetNz(a); break;
    case 0x45: a ^= memory[AddrZeroPage()];     SetNz(a); break;
    case 0x55: a ^= memory[AddrZeroPageX()];    SetNz(a); break;
    case 0x4D: a ^= memory[AddrAbsolute()];     SetNz(a); break;
    case 0x5D: a ^= memory[AddrAbsoluteX()];    SetNz(a); break;
    case 0x59: a ^= memory[AddrAbsoluteY()];    SetNz(a); break;
    case 0x41: a ^= memory[AddrIndirectX()];    SetNz(a); break;
    case 0x51: a ^= memory[AddrIndirectY()];    SetNz(a); break;

    case 0x24:
    case 0x2C:
    {
      const std::uint8_t operand = memory[opcode == 0x24 ? AddrZeroPage() : AddrAbsolute()];
      z = (a & operand) == 0u;
      n = (operand & 0x80u) != 0u;
      v = (operand & 0x40u) != 0u;
      break;
    }

    // ---- shifts and rotates ------------------------------------------------------------
    case 0x0A: a = ShiftLeft(a); break;
    case 0x06: { const std::uint16_t at = AddrZeroPage();  memory[at] = ShiftLeft(memory[at]); break; }
    case 0x16: { const std::uint16_t at = AddrZeroPageX(); memory[at] = ShiftLeft(memory[at]); break; }
    case 0x0E: { const std::uint16_t at = AddrAbsolute();  memory[at] = ShiftLeft(memory[at]); break; }
    case 0x1E: { const std::uint16_t at = AddrAbsoluteX(); memory[at] = ShiftLeft(memory[at]); break; }

    case 0x4A: a = ShiftRight(a); break;
    case 0x46: { const std::uint16_t at = AddrZeroPage();  memory[at] = ShiftRight(memory[at]); break; }
    case 0x56: { const std::uint16_t at = AddrZeroPageX(); memory[at] = ShiftRight(memory[at]); break; }
    case 0x4E: { const std::uint16_t at = AddrAbsolute();  memory[at] = ShiftRight(memory[at]); break; }
    case 0x5E: { const std::uint16_t at = AddrAbsoluteX(); memory[at] = ShiftRight(memory[at]); break; }

    case 0x2A: a = RollLeft(a); break;
    case 0x26: { const std::uint16_t at = AddrZeroPage();  memory[at] = RollLeft(memory[at]); break; }
    case 0x36: { const std::uint16_t at = AddrZeroPageX(); memory[at] = RollLeft(memory[at]); break; }
    case 0x2E: { const std::uint16_t at = AddrAbsolute();  memory[at] = RollLeft(memory[at]); break; }
    case 0x3E: { const std::uint16_t at = AddrAbsoluteX(); memory[at] = RollLeft(memory[at]); break; }

    case 0x6A: a = RollRight(a); break;
    case 0x66: { const std::uint16_t at = AddrZeroPage();  memory[at] = RollRight(memory[at]); break; }
    case 0x76: { const std::uint16_t at = AddrZeroPageX(); memory[at] = RollRight(memory[at]); break; }
    case 0x6E: { const std::uint16_t at = AddrAbsolute();  memory[at] = RollRight(memory[at]); break; }
    case 0x7E: { const std::uint16_t at = AddrAbsoluteX(); memory[at] = RollRight(memory[at]); break; }

    // ---- jumps, calls and returns ------------------------------------------------------
    case 0x4C: pc = FetchWord(); break;

    case 0x6C:
    {
      // The NMOS indirect-JMP page-crossing bug: the high byte is read from the start of the
      // same page rather than the next one. Reproduced because a port that relied on it and
      // an oracle that did not would disagree for one very confusing afternoon.
      const std::uint16_t pointer = FetchWord();
      const std::uint16_t hiAddress = static_cast<std::uint16_t>((pointer & 0xFF00u) | ((pointer + 1) & 0x00FFu));
      pc = static_cast<std::uint16_t>(memory[pointer] | (memory[hiAddress] << 8));
      break;
    }

    case 0x20:
    {
      const std::uint16_t target = FetchWord();
      const std::uint16_t ret = static_cast<std::uint16_t>(pc - 1);
      Push(static_cast<std::uint8_t>(ret >> 8));
      Push(static_cast<std::uint8_t>(ret & 0xFFu));
      pc = target;
      break;
    }

    case 0x60:
    {
      const std::uint8_t lo = Pop();
      const std::uint8_t hi = Pop();
      pc = static_cast<std::uint16_t>((lo | (hi << 8)) + 1);
      break;
    }

    case 0x40:
    {
      SetStatusByte(Pop());
      const std::uint8_t lo = Pop();
      const std::uint8_t hi = Pop();
      pc = static_cast<std::uint16_t>(lo | (hi << 8));
      break;
    }

    case 0x00:
    {
      const std::uint16_t ret = static_cast<std::uint16_t>(pc + 1);
      Push(static_cast<std::uint8_t>(ret >> 8));
      Push(static_cast<std::uint8_t>(ret & 0xFFu));
      Push(static_cast<std::uint8_t>(StatusByte() | FLAG_BREAK));
      i = true;
      pc = ReadWord(0xFFFE);
      break;
    }

    // ---- branches ----------------------------------------------------------------------
    case 0x90: Branch(!c); break;
    case 0xB0: Branch(c); break;
    case 0xD0: Branch(!z); break;
    case 0xF0: Branch(z); break;
    case 0x10: Branch(!n); break;
    case 0x30: Branch(n); break;
    case 0x50: Branch(!v); break;
    case 0x70: Branch(v); break;

    // ---- flags -------------------------------------------------------------------------
    case 0x18: c = false; break;
    case 0x38: c = true; break;
    case 0x58: i = false; break;
    case 0x78: i = true; break;
    case 0xB8: v = false; break;
    case 0xD8: d = false; break;
    case 0xF8: d = true; break;

    case 0xEA: break;

    default:
      pc = opcodeAddress;
      return false;
  }

  return true;
}

RunResult Cpu6502::CallSubroutine(std::uint16_t _address, std::uint32_t _maxInstructions, std::uint16_t _stopAddress) noexcept
{
  RunResult result{};

  const std::uint8_t entrySp = sp;
  const std::uint16_t ret = static_cast<std::uint16_t>(_stopAddress - 1);
  Push(static_cast<std::uint8_t>(ret >> 8));
  Push(static_cast<std::uint8_t>(ret & 0xFFu));
  pc = _address;

  while (result.instructions < _maxInstructions)
  {
    if (pc == _stopAddress)
    {
      result.completed = true;
      break;
    }

    // The routine discarded its return address and jumped away. That is a finished call too.
    if (sp == entrySp)
    {
      result.completed = true;
      break;
    }

    if (!Step())
    {
      result.illegalOpcode = true;
      break;
    }

    ++result.instructions;
  }

  result.stoppedAt = pc;
  return result;
}

} // namespace Elite::Testing
