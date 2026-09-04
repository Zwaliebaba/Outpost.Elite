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
    cycles = 0;
    m_crossedPage = false;
  }

  /*
   * The documented NMOS 6502 cycle counts.
   *
   * Grouped by addressing mode and by what the instruction does with the address, because that is
   * how 6502 timing is actually defined: the count is the operand fetches plus the access, and the
   * exceptions are all one shape -- an instruction that must write cannot start writing until it
   * knows the address is right, so it spends the extra cycle every time rather than only when the
   * index carries.
   *
   * Three groups therefore look like anomalies and are not:
   *
   *   * absolute,X and absolute,Y READS cost 4, and 5 when the index carries into a new page.
   *   * absolute,X and absolute,Y STORES cost 5 always. Never 4, never 6.
   *   * read-modify-write absolute,X costs 7 always -- it reads, holds, and writes back.
   *
   * The same split applies to (zero page),Y: 5 for a read, 6 for a store.
   *
   * Anything not listed returns 0, which means every undocumented opcode. The interpreter does not
   * execute those, and a test checks that the two sets agree -- an opcode Step ran and this did not
   * price would be counted as free, which is the one way this table can lie quietly.
   */
  std::uint8_t Cpu6502::BaseCycles(std::uint8_t _opcode) noexcept
  {
    switch (_opcode)
    {
    // Implied and accumulator. No operand, no memory access.
    case 0x0A:
    case 0x2A:
    case 0x4A:
    case 0x6A: // ASL/ROL/LSR/ROR A
    case 0x18:
    case 0x38:
    case 0x58:
    case 0x78: // CLC SEC CLI SEI
    case 0xB8:
    case 0xD8:
    case 0xF8: // CLV CLD SED
    case 0x88:
    case 0xC8:
    case 0xCA:
    case 0xE8: // DEY INY DEX INX
    case 0xAA:
    case 0xA8:
    case 0x8A:
    case 0x98: // TAX TAY TXA TYA
    case 0xBA:
    case 0x9A: // TSX TXS
    case 0xEA: // NOP
    // Immediate. One operand byte, no memory access.
    case 0x09:
    case 0x29:
    case 0x49:
    case 0x69: // ORA AND EOR ADC
    case 0xA9:
    case 0xC9:
    case 0xE9: // LDA CMP SBC
    case 0xA2:
    case 0xA0:
    case 0xE0:
    case 0xC0: // LDX LDY CPX CPY
    // Branches, NOT TAKEN. Branch() adds one for taking it and one more for crossing a page.
    case 0x10:
    case 0x30:
    case 0x50:
    case 0x70: // BPL BMI BVC BVS
    case 0x90:
    case 0xB0:
    case 0xD0:
    case 0xF0: // BCC BCS BNE BEQ
      return 2;

    // Zero page.
    case 0x05:
    case 0x25:
    case 0x45:
    case 0x65: // ORA AND EOR ADC
    case 0xA5:
    case 0xC5:
    case 0xE5: // LDA CMP SBC
    case 0xA6:
    case 0xA4:
    case 0xE4:
    case 0xC4:
    case 0x24: // LDX LDY CPX CPY BIT
    case 0x85:
    case 0x86:
    case 0x84: // STA STX STY
    // Stack pushes, and an absolute jump: three bus cycles each.
    case 0x48:
    case 0x08: // PHA PHP
    case 0x4C: // JMP abs
      return 3;

    // Zero page indexed. The index is added in a wasted cycle and wraps within the page, so
    // there is no crossing to pay for -- which is why these are a flat 4 for reads and writes.
    case 0x15:
    case 0x35:
    case 0x55:
    case 0x75: // ORA AND EOR ADC
    case 0xB5:
    case 0xD5:
    case 0xF5:
    case 0xB4: // LDA CMP SBC LDY
    case 0x95:
    case 0x94: // STA STY
    case 0xB6:
    case 0x96: // LDX STX  (zero page,Y)
    // Absolute.
    case 0x0D:
    case 0x2D:
    case 0x4D:
    case 0x6D: // ORA AND EOR ADC
    case 0xAD:
    case 0xCD:
    case 0xED: // LDA CMP SBC
    case 0xAE:
    case 0xAC:
    case 0xEC:
    case 0xCC:
    case 0x2C: // LDX LDY CPX CPY BIT
    case 0x8D:
    case 0x8E:
    case 0x8C: // STA STX STY
    // Absolute indexed READS. One more when the index carries: see PaysPageCrossPenalty.
    case 0x1D:
    case 0x3D:
    case 0x5D:
    case 0x7D: // ORA AND EOR ADC  abs,X
    case 0xBD:
    case 0xDD:
    case 0xFD:
    case 0xBC: // LDA CMP SBC LDY  abs,X
    case 0x19:
    case 0x39:
    case 0x59:
    case 0x79: // ORA AND EOR ADC  abs,Y
    case 0xB9:
    case 0xD9:
    case 0xF9:
    case 0xBE: // LDA CMP SBC LDX  abs,Y
    // Stack pulls: one more than a push, for the increment.
    case 0x28:
    case 0x68: // PLP PLA
      return 4;

    // Absolute indexed STORES. Five whether the index carries or not.
    case 0x9D:
    case 0x99: // STA abs,X / abs,Y
    // (zero page),Y READS. One more when the index carries.
    case 0x11:
    case 0x31:
    case 0x51:
    case 0x71: // ORA AND EOR ADC
    case 0xB1:
    case 0xD1:
    case 0xF1: // LDA CMP SBC
    // Read-modify-write, zero page.
    case 0x06:
    case 0x26:
    case 0x46:
    case 0x66: // ASL ROL LSR ROR
    case 0xE6:
    case 0xC6: // INC DEC
    case 0x6C: // JMP (indirect)
      return 5;

    // (zero page,X). The pointer is read from the zero page after the index is added.
    case 0x01:
    case 0x21:
    case 0x41:
    case 0x61: // ORA AND EOR ADC
    case 0xA1:
    case 0xC1:
    case 0xE1:
    case 0x81: // LDA CMP SBC STA
    case 0x91: // STA (zero page),Y
    // Read-modify-write, zero page,X and absolute.
    case 0x16:
    case 0x36:
    case 0x56:
    case 0x76:
    case 0xF6:
    case 0xD6: // ASL ROL LSR ROR INC DEC
    case 0x0E:
    case 0x2E:
    case 0x4E:
    case 0x6E:
    case 0xEE:
    case 0xCE: // ASL ROL LSR ROR INC DEC
    case 0x20:
    case 0x60:
    case 0x40: // JSR RTS RTI
      return 6;

    // Read-modify-write, absolute,X. Seven whether the index carries or not.
    case 0x1E:
    case 0x3E:
    case 0x5E:
    case 0x7E:
    case 0xFE:
    case 0xDE: // ASL ROL LSR ROR INC DEC
    case 0x00: // BRK
      return 7;

    default:
      return 0;
    }
  }

  bool Cpu6502::PaysPageCrossPenalty(std::uint8_t _opcode) noexcept
  {
    switch (_opcode)
    {
    // absolute,X reads
    case 0x1D:
    case 0x3D:
    case 0x5D:
    case 0x7D:
    case 0xBD:
    case 0xDD:
    case 0xFD:
    case 0xBC:
    // absolute,Y reads
    case 0x19:
    case 0x39:
    case 0x59:
    case 0x79:
    case 0xB9:
    case 0xD9:
    case 0xF9:
    case 0xBE:
    // (zero page),Y reads
    case 0x11:
    case 0x31:
    case 0x51:
    case 0x71:
    case 0xB1:
    case 0xD1:
    case 0xF1:
      return true;
    default:
      return false;
    }
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
    return Indexed(address, y);
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

  /*
   * A branch costs two cycles, three when it is taken, and four when taking it lands in a
   * different page from the instruction after it.
   *
   * The page compared is the one the pc is in AFTER the offset byte has been fetched, not the one
   * the opcode sits in. Those differ for a branch that straddles a page boundary, and getting it
   * wrong would mis-time exactly the loops that sit at the end of a page.
   */
  void Cpu6502::Branch(bool _condition) noexcept
  {
    const std::int8_t offset = static_cast<std::int8_t>(Fetch());
    if (!_condition)
    {
      return;
    }

    const std::uint16_t from = pc;
    pc = static_cast<std::uint16_t>(pc + offset);

    cycles += 1u;
    if ((from & 0xFF00u) != (pc & 0xFF00u))
    {
      cycles += 1u;
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

  void Cpu6502::AddTrap(std::uint16_t _address, TrapExit _exit)
  {
    for (Trap& existing : traps)
    {
      if (existing.address == _address)
      {
        existing.exit = _exit;
        return;
      }
    }
    traps.push_back(Trap{_address, _exit});
  }

  bool Cpu6502::Step() noexcept
  {
    // A trapped address is recorded and returned from rather than executed. The pop mirrors what
    // the routine's own RTS would have done, so the caller continues as if it had run.
    if (!traps.empty())
    {
      for (const Trap& trapped : traps)
      {
        if (pc == trapped.address)
        {
          TrapHit hit{pc, a, x, y, {}};
          for (std::size_t slot = 0; slot < WATCH_SLOTS; ++slot)
          {
            hit.watched[slot] = memory[watch[slot]];
          }
          trapHits.push_back(hit);
          const std::uint8_t lo = Pop();
          const std::uint8_t hi = Pop();
          pc = static_cast<std::uint16_t>((lo | (hi << 8)) + 1);
          if (trapped.exit == TrapExit::ClearCarry)
          {
            c = false;
          }
          else if (trapped.exit == TrapExit::SetCarry)
          {
            c = true;
          }
          return true;
        }
      }
    }

    /*
     * Cleared defensively, and no test can catch its removal.
     *
     * Every opcode that consults this flag reaches it through AddrAbsoluteX, AddrAbsoluteY or
     * AddrIndirectY, and all three write it before Step reads it -- so a stale value from the
     * previous instruction is always overwritten and the clear is provably dead. It stays because
     * that argument holds only while the set of penalty-paying opcodes and the set of helpers that
     * call Indexed coincide exactly, and an addressing mode added later would break it silently.
     */
    m_crossedPage = false;

    const std::uint16_t opcodeAddress = pc;
    const std::uint8_t opcode = Fetch();

    switch (opcode)
    {
    // ---- load and store ----------------------------------------------------------------
    case 0xA9:
      a = Fetch();
      SetNz(a);
      break;
    case 0xA5:
      a = memory[AddrZeroPage()];
      SetNz(a);
      break;
    case 0xB5:
      a = memory[AddrZeroPageX()];
      SetNz(a);
      break;
    case 0xAD:
      a = memory[AddrAbsolute()];
      SetNz(a);
      break;
    case 0xBD:
      a = memory[AddrAbsoluteX()];
      SetNz(a);
      break;
    case 0xB9:
      a = memory[AddrAbsoluteY()];
      SetNz(a);
      break;
    case 0xA1:
      a = memory[AddrIndirectX()];
      SetNz(a);
      break;
    case 0xB1:
      a = memory[AddrIndirectY()];
      SetNz(a);
      break;

    case 0xA2:
      x = Fetch();
      SetNz(x);
      break;
    case 0xA6:
      x = memory[AddrZeroPage()];
      SetNz(x);
      break;
    case 0xB6:
      x = memory[AddrZeroPageY()];
      SetNz(x);
      break;
    case 0xAE:
      x = memory[AddrAbsolute()];
      SetNz(x);
      break;
    case 0xBE:
      x = memory[AddrAbsoluteY()];
      SetNz(x);
      break;

    case 0xA0:
      y = Fetch();
      SetNz(y);
      break;
    case 0xA4:
      y = memory[AddrZeroPage()];
      SetNz(y);
      break;
    case 0xB4:
      y = memory[AddrZeroPageX()];
      SetNz(y);
      break;
    case 0xAC:
      y = memory[AddrAbsolute()];
      SetNz(y);
      break;
    case 0xBC:
      y = memory[AddrAbsoluteX()];
      SetNz(y);
      break;

    case 0x85:
      memory[AddrZeroPage()] = a;
      break;
    case 0x95:
      memory[AddrZeroPageX()] = a;
      break;
    case 0x8D:
      memory[AddrAbsolute()] = a;
      break;
    case 0x9D:
      memory[AddrAbsoluteX()] = a;
      break;
    case 0x99:
      memory[AddrAbsoluteY()] = a;
      break;
    case 0x81:
      memory[AddrIndirectX()] = a;
      break;
    case 0x91:
      memory[AddrIndirectY()] = a;
      break;

    case 0x86:
      memory[AddrZeroPage()] = x;
      break;
    case 0x96:
      memory[AddrZeroPageY()] = x;
      break;
    case 0x8E:
      memory[AddrAbsolute()] = x;
      break;

    case 0x84:
      memory[AddrZeroPage()] = y;
      break;
    case 0x94:
      memory[AddrZeroPageX()] = y;
      break;
    case 0x8C:
      memory[AddrAbsolute()] = y;
      break;

    // ---- register transfers ------------------------------------------------------------
    case 0xAA:
      x = a;
      SetNz(x);
      break;
    case 0xA8:
      y = a;
      SetNz(y);
      break;
    case 0x8A:
      a = x;
      SetNz(a);
      break;
    case 0x98:
      a = y;
      SetNz(a);
      break;
    case 0xBA:
      x = sp;
      SetNz(x);
      break;
    case 0x9A:
      sp = x;
      break;

    // ---- stack ------------------------------------------------------------------------
    case 0x48:
      Push(a);
      break;
    case 0x68:
      a = Pop();
      SetNz(a);
      break;
    case 0x08:
      Push(static_cast<std::uint8_t>(StatusByte() | FLAG_BREAK));
      break;
    case 0x28:
      SetStatusByte(Pop());
      break;

    // ---- arithmetic -------------------------------------------------------------------
    case 0x69:
      Adc(Fetch());
      break;
    case 0x65:
      Adc(memory[AddrZeroPage()]);
      break;
    case 0x75:
      Adc(memory[AddrZeroPageX()]);
      break;
    case 0x6D:
      Adc(memory[AddrAbsolute()]);
      break;
    case 0x7D:
      Adc(memory[AddrAbsoluteX()]);
      break;
    case 0x79:
      Adc(memory[AddrAbsoluteY()]);
      break;
    case 0x61:
      Adc(memory[AddrIndirectX()]);
      break;
    case 0x71:
      Adc(memory[AddrIndirectY()]);
      break;

    case 0xE9:
      Sbc(Fetch());
      break;
    case 0xE5:
      Sbc(memory[AddrZeroPage()]);
      break;
    case 0xF5:
      Sbc(memory[AddrZeroPageX()]);
      break;
    case 0xED:
      Sbc(memory[AddrAbsolute()]);
      break;
    case 0xFD:
      Sbc(memory[AddrAbsoluteX()]);
      break;
    case 0xF9:
      Sbc(memory[AddrAbsoluteY()]);
      break;
    case 0xE1:
      Sbc(memory[AddrIndirectX()]);
      break;
    case 0xF1:
      Sbc(memory[AddrIndirectY()]);
      break;

    case 0xC9:
      Compare(a, Fetch());
      break;
    case 0xC5:
      Compare(a, memory[AddrZeroPage()]);
      break;
    case 0xD5:
      Compare(a, memory[AddrZeroPageX()]);
      break;
    case 0xCD:
      Compare(a, memory[AddrAbsolute()]);
      break;
    case 0xDD:
      Compare(a, memory[AddrAbsoluteX()]);
      break;
    case 0xD9:
      Compare(a, memory[AddrAbsoluteY()]);
      break;
    case 0xC1:
      Compare(a, memory[AddrIndirectX()]);
      break;
    case 0xD1:
      Compare(a, memory[AddrIndirectY()]);
      break;

    case 0xE0:
      Compare(x, Fetch());
      break;
    case 0xE4:
      Compare(x, memory[AddrZeroPage()]);
      break;
    case 0xEC:
      Compare(x, memory[AddrAbsolute()]);
      break;

    case 0xC0:
      Compare(y, Fetch());
      break;
    case 0xC4:
      Compare(y, memory[AddrZeroPage()]);
      break;
    case 0xCC:
      Compare(y, memory[AddrAbsolute()]);
      break;

    // ---- increment and decrement -------------------------------------------------------
    case 0xE6:
    {
      const std::uint16_t at = AddrZeroPage();
      SetNz(++memory[at]);
      break;
    }
    case 0xF6:
    {
      const std::uint16_t at = AddrZeroPageX();
      SetNz(++memory[at]);
      break;
    }
    case 0xEE:
    {
      const std::uint16_t at = AddrAbsolute();
      SetNz(++memory[at]);
      break;
    }
    case 0xFE:
    {
      const std::uint16_t at = AddrAbsoluteX();
      SetNz(++memory[at]);
      break;
    }

    case 0xC6:
    {
      const std::uint16_t at = AddrZeroPage();
      SetNz(--memory[at]);
      break;
    }
    case 0xD6:
    {
      const std::uint16_t at = AddrZeroPageX();
      SetNz(--memory[at]);
      break;
    }
    case 0xCE:
    {
      const std::uint16_t at = AddrAbsolute();
      SetNz(--memory[at]);
      break;
    }
    case 0xDE:
    {
      const std::uint16_t at = AddrAbsoluteX();
      SetNz(--memory[at]);
      break;
    }

    case 0xE8:
      SetNz(++x);
      break;
    case 0xC8:
      SetNz(++y);
      break;
    case 0xCA:
      SetNz(--x);
      break;
    case 0x88:
      SetNz(--y);
      break;

    // ---- logic ------------------------------------------------------------------------
    case 0x29:
      a &= Fetch();
      SetNz(a);
      break;
    case 0x25:
      a &= memory[AddrZeroPage()];
      SetNz(a);
      break;
    case 0x35:
      a &= memory[AddrZeroPageX()];
      SetNz(a);
      break;
    case 0x2D:
      a &= memory[AddrAbsolute()];
      SetNz(a);
      break;
    case 0x3D:
      a &= memory[AddrAbsoluteX()];
      SetNz(a);
      break;
    case 0x39:
      a &= memory[AddrAbsoluteY()];
      SetNz(a);
      break;
    case 0x21:
      a &= memory[AddrIndirectX()];
      SetNz(a);
      break;
    case 0x31:
      a &= memory[AddrIndirectY()];
      SetNz(a);
      break;

    case 0x09:
      a |= Fetch();
      SetNz(a);
      break;
    case 0x05:
      a |= memory[AddrZeroPage()];
      SetNz(a);
      break;
    case 0x15:
      a |= memory[AddrZeroPageX()];
      SetNz(a);
      break;
    case 0x0D:
      a |= memory[AddrAbsolute()];
      SetNz(a);
      break;
    case 0x1D:
      a |= memory[AddrAbsoluteX()];
      SetNz(a);
      break;
    case 0x19:
      a |= memory[AddrAbsoluteY()];
      SetNz(a);
      break;
    case 0x01:
      a |= memory[AddrIndirectX()];
      SetNz(a);
      break;
    case 0x11:
      a |= memory[AddrIndirectY()];
      SetNz(a);
      break;

    case 0x49:
      a ^= Fetch();
      SetNz(a);
      break;
    case 0x45:
      a ^= memory[AddrZeroPage()];
      SetNz(a);
      break;
    case 0x55:
      a ^= memory[AddrZeroPageX()];
      SetNz(a);
      break;
    case 0x4D:
      a ^= memory[AddrAbsolute()];
      SetNz(a);
      break;
    case 0x5D:
      a ^= memory[AddrAbsoluteX()];
      SetNz(a);
      break;
    case 0x59:
      a ^= memory[AddrAbsoluteY()];
      SetNz(a);
      break;
    case 0x41:
      a ^= memory[AddrIndirectX()];
      SetNz(a);
      break;
    case 0x51:
      a ^= memory[AddrIndirectY()];
      SetNz(a);
      break;

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
    case 0x0A:
      a = ShiftLeft(a);
      break;
    case 0x06:
    {
      const std::uint16_t at = AddrZeroPage();
      memory[at] = ShiftLeft(memory[at]);
      break;
    }
    case 0x16:
    {
      const std::uint16_t at = AddrZeroPageX();
      memory[at] = ShiftLeft(memory[at]);
      break;
    }
    case 0x0E:
    {
      const std::uint16_t at = AddrAbsolute();
      memory[at] = ShiftLeft(memory[at]);
      break;
    }
    case 0x1E:
    {
      const std::uint16_t at = AddrAbsoluteX();
      memory[at] = ShiftLeft(memory[at]);
      break;
    }

    case 0x4A:
      a = ShiftRight(a);
      break;
    case 0x46:
    {
      const std::uint16_t at = AddrZeroPage();
      memory[at] = ShiftRight(memory[at]);
      break;
    }
    case 0x56:
    {
      const std::uint16_t at = AddrZeroPageX();
      memory[at] = ShiftRight(memory[at]);
      break;
    }
    case 0x4E:
    {
      const std::uint16_t at = AddrAbsolute();
      memory[at] = ShiftRight(memory[at]);
      break;
    }
    case 0x5E:
    {
      const std::uint16_t at = AddrAbsoluteX();
      memory[at] = ShiftRight(memory[at]);
      break;
    }

    case 0x2A:
      a = RollLeft(a);
      break;
    case 0x26:
    {
      const std::uint16_t at = AddrZeroPage();
      memory[at] = RollLeft(memory[at]);
      break;
    }
    case 0x36:
    {
      const std::uint16_t at = AddrZeroPageX();
      memory[at] = RollLeft(memory[at]);
      break;
    }
    case 0x2E:
    {
      const std::uint16_t at = AddrAbsolute();
      memory[at] = RollLeft(memory[at]);
      break;
    }
    case 0x3E:
    {
      const std::uint16_t at = AddrAbsoluteX();
      memory[at] = RollLeft(memory[at]);
      break;
    }

    case 0x6A:
      a = RollRight(a);
      break;
    case 0x66:
    {
      const std::uint16_t at = AddrZeroPage();
      memory[at] = RollRight(memory[at]);
      break;
    }
    case 0x76:
    {
      const std::uint16_t at = AddrZeroPageX();
      memory[at] = RollRight(memory[at]);
      break;
    }
    case 0x6E:
    {
      const std::uint16_t at = AddrAbsolute();
      memory[at] = RollRight(memory[at]);
      break;
    }
    case 0x7E:
    {
      const std::uint16_t at = AddrAbsoluteX();
      memory[at] = RollRight(memory[at]);
      break;
    }

    // ---- jumps, calls and returns ------------------------------------------------------
    case 0x4C:
      pc = FetchWord();
      break;

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
    case 0x90:
      Branch(!c);
      break;
    case 0xB0:
      Branch(c);
      break;
    case 0xD0:
      Branch(!z);
      break;
    case 0xF0:
      Branch(z);
      break;
    case 0x10:
      Branch(!n);
      break;
    case 0x30:
      Branch(n);
      break;
    case 0x50:
      Branch(!v);
      break;
    case 0x70:
      Branch(v);
      break;

    // ---- flags -------------------------------------------------------------------------
    case 0x18:
      c = false;
      break;
    case 0x38:
      c = true;
      break;
    case 0x58:
      i = false;
      break;
    case 0x78:
      i = true;
      break;
    case 0xB8:
      v = false;
      break;
    case 0xD8:
      d = false;
      break;
    case 0xF8:
      d = true;
      break;

    case 0xEA:
      break;

    default:
      pc = opcodeAddress;
      return false;
    }

    /*
     * Priced after the switch, so that an opcode this interpreter does not implement costs nothing
     * -- it did not run. Branch() has already added its own extras by now, which is fine: this is
     * a sum, and the order the terms arrive in does not matter.
     */
    cycles += BaseCycles(opcode);
    if (m_crossedPage && PaysPageCrossPenalty(opcode))
    {
      cycles += 1u;
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
