#include "pch.h"

#include "NativeFirmware.h"

#include "Firmware.h"
#include "Pc.h"

namespace Machine
{

namespace
{

// The BIOS data area's tick count and its rollover flag (Firmware).
constexpr std::uint16_t TIMER_LOW = 0x6C;
constexpr std::uint16_t TIMER_HIGH = 0x6E;
constexpr std::uint16_t TIMER_OVERFLOW = 0x70;
constexpr std::uint16_t TICKS_PER_DAY_HIGH = 0x18;
constexpr std::uint16_t TICKS_PER_DAY_LOW = 0xB0;

constexpr std::uint8_t VECTOR_USER_TIMER = 0x1C;
constexpr std::uint16_t AFTER_USER_TIMER_CALL = 0x30; // the timer handler's offset after its INT 1Ch
constexpr std::uint8_t IRET = 0xCF;

constexpr std::uint16_t PIC_COMMAND_PORT = 0x20;
constexpr std::uint8_t END_OF_INTERRUPT = 0x20;
constexpr std::uint16_t KEYBOARD_DATA_PORT = 0x60;
constexpr std::uint16_t KEYBOARD_CONTROL_PORT = 0x61;
constexpr std::uint8_t KEYBOARD_CLEAR = 0x80;

// What the interpreter charged for each instruction of the two handlers, measured by stepping it through them.
constexpr Cycles STI = 2;
constexpr Cycles PUSH_SEGMENT = 14;
constexpr Cycles PUSH_REGISTER = 15;
constexpr Cycles MOVE_IMMEDIATE = 4; // MOV AX,imm16 and MOV AL,imm8
constexpr Cycles MOVE_TO_SEGMENT = 2;
constexpr Cycles INCREMENT_MEMORY = 29;
constexpr Cycles JUMP_TAKEN = 16;
constexpr Cycles JUMP_NOT_TAKEN = 4;
constexpr Cycles COMPARE_MEMORY = 20;
constexpr Cycles XOR_REGISTERS = 3;
constexpr Cycles STORE_WORD = 14;
constexpr Cycles STORE_IMMEDIATE_BYTE = 16;
constexpr Cycles SOFTWARE_INTERRUPT = 71;
constexpr Cycles INTERRUPT_RETURN = 36;
constexpr Cycles PORT_ACCESS = 10;    // IN AL,imm8 and OUT imm8,AL
constexpr Cycles LOGIC_IMMEDIATE = 4; // OR AL,imm8 and AND AL,imm8
constexpr Cycles POP_REGISTER = 12;   // POP AX and POP DS

[[nodiscard]] bool EvenParity(std::uint16_t _value) noexcept
{
  std::uint32_t bits = _value & 0xFFu;
  bits ^= bits >> 4;
  bits ^= bits >> 2;
  bits ^= bits >> 1;
  return (bits & 1u) == 0;
}

// _flags with the six an ALU operation sets replaced, as the 8088 sets them for a word.
[[nodiscard]] std::uint16_t WithArithmeticFlags(std::uint16_t _flags, bool _carry, bool _auxiliary, bool _overflow,
                                                std::uint16_t _result) noexcept
{
  std::uint32_t flags =
    _flags & ~static_cast<std::uint32_t>(FLAG_CARRY | FLAG_PARITY | FLAG_AUXILIARY | FLAG_ZERO | FLAG_SIGN | FLAG_OVERFLOW);
  flags |= _carry ? FLAG_CARRY : 0u;
  flags |= EvenParity(_result) ? FLAG_PARITY : 0u;
  flags |= _auxiliary ? FLAG_AUXILIARY : 0u;
  flags |= _result == 0 ? FLAG_ZERO : 0u;
  flags |= (_result & 0x8000u) != 0 ? FLAG_SIGN : 0u;
  flags |= _overflow ? FLAG_OVERFLOW : 0u;
  return static_cast<std::uint16_t>(flags);
}

// CMP word _a, _b.
[[nodiscard]] std::uint16_t CompareFlags(std::uint16_t _flags, std::uint16_t _a, std::uint16_t _b) noexcept
{
  const std::uint32_t full = static_cast<std::uint32_t>(_a) - _b;
  const auto result = static_cast<std::uint16_t>(full);
  return WithArithmeticFlags(_flags, (full & 0x10000u) != 0, ((_a ^ _b ^ result) & 0x10u) != 0, ((_a ^ _b) & (_a ^ result) & 0x8000u) != 0,
                             result);
}

void Push(Pc& _pc, std::uint16_t _value) noexcept
{
  Registers& regs = _pc.Processor().Regs();
  regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
  _pc.Ram().Write16(regs.ss, regs.sp, _value);
}

[[nodiscard]] std::uint16_t Pop(Pc& _pc) noexcept
{
  Registers& regs = _pc.Processor().Regs();
  const std::uint16_t value = _pc.Ram().Read16(regs.ss, regs.sp);
  regs.sp = static_cast<std::uint16_t>(regs.sp + 2);
  return value;
}

// INT 1Ch from the timer handler, with IP already past it. While the vector is the ROM's own IRET, as the
// program leaves it, the call and that IRET are made here, byte for byte; otherwise the handler it names
// is called as native code calls an interrupt.
void CallUserTimer(Pc& _pc)
{
  Registers& regs = _pc.Processor().Regs();
  Memory& memory = _pc.Ram();
  const std::uint32_t vector = static_cast<std::uint32_t>(VECTOR_USER_TIMER) * 4u;
  const std::uint16_t offset = memory.Read16(vector);
  const std::uint16_t segment = memory.Read16(vector + 2u);
  if (segment != Firmware::ROM_SEGMENT || memory.Read8(segment, offset) != IRET)
  {
    _pc.CountInstructionCycles(SOFTWARE_INTERRUPT);
    _pc.CallInterrupt(VECTOR_USER_TIMER);
    return;
  }
  const std::uint16_t flags = regs.flags;
  Push(_pc, flags);
  Push(_pc, regs.cs);
  Push(_pc, regs.ip);
  regs.ip = Pop(_pc);
  regs.cs = Pop(_pc);
  regs.flags = static_cast<std::uint16_t>((Pop(_pc) & FLAGS_WRITABLE) | FLAGS_FIXED_ONES);
  _pc.CountInstructionCycles(SOFTWARE_INTERRUPT + INTERRUPT_RETURN);
}

// F000:E000: counts the tick at 0040:006C, starts again from 0 once a day has passed, calls int 1Ch and
// sends EOI.
void RunTimerHandler(Pc& _pc)
{
  Registers& regs = _pc.Processor().Regs();
  Memory& memory = _pc.Ram();
  constexpr std::uint16_t BIOS_DATA = Firmware::DATA_SEGMENT;
  regs.flags = static_cast<std::uint16_t>(regs.flags | FLAG_INTERRUPT);
  Push(_pc, regs.ds);
  Push(_pc, regs.ax);
  regs.ax = BIOS_DATA;
  regs.ds = BIOS_DATA;
  Cycles cycles = STI + PUSH_SEGMENT + PUSH_REGISTER + MOVE_IMMEDIATE + MOVE_TO_SEGMENT + INCREMENT_MEMORY;
  const auto low = static_cast<std::uint16_t>(memory.Read16(BIOS_DATA, TIMER_LOW) + 1);
  memory.Write16(BIOS_DATA, TIMER_LOW, low);
  if (low == 0)
  {
    memory.Write16(BIOS_DATA, TIMER_HIGH, static_cast<std::uint16_t>(memory.Read16(BIOS_DATA, TIMER_HIGH) + 1));
    cycles += JUMP_NOT_TAKEN + INCREMENT_MEMORY;
  }
  else
  {
    cycles += JUMP_TAKEN;
  }
  const std::uint16_t high = memory.Read16(BIOS_DATA, TIMER_HIGH);
  regs.flags = CompareFlags(regs.flags, high, TICKS_PER_DAY_HIGH);
  cycles += COMPARE_MEMORY;
  if (high != TICKS_PER_DAY_HIGH)
  {
    cycles += JUMP_TAKEN;
  }
  else
  {
    const std::uint16_t now = memory.Read16(BIOS_DATA, TIMER_LOW);
    regs.flags = CompareFlags(regs.flags, now, TICKS_PER_DAY_LOW);
    cycles += JUMP_NOT_TAKEN + COMPARE_MEMORY;
    if (now != TICKS_PER_DAY_LOW)
    {
      cycles += JUMP_TAKEN;
    }
    else
    {
      // XOR AX,AX, as the 8088 leaves the flags for a zero result; then the two words and the flag.
      regs.ax = 0;
      regs.flags = WithArithmeticFlags(regs.flags, false, false, false, 0);
      memory.Write16(BIOS_DATA, TIMER_HIGH, 0);
      memory.Write16(BIOS_DATA, TIMER_LOW, 0);
      memory.Write8(BIOS_DATA, TIMER_OVERFLOW, 1);
      cycles += JUMP_NOT_TAKEN + XOR_REGISTERS + 2 * STORE_WORD + STORE_IMMEDIATE_BYTE;
    }
  }
  _pc.CountInstructionCycles(cycles);
  regs.ip = static_cast<std::uint16_t>(Firmware::TIMER_HANDLER_OFFSET + AFTER_USER_TIMER_CALL);
  CallUserTimer(_pc);
  regs.ax = static_cast<std::uint16_t>((regs.ax & 0xFF00u) | END_OF_INTERRUPT);
  _pc.Ports().Out8(PIC_COMMAND_PORT, END_OF_INTERRUPT);
  regs.ax = Pop(_pc);
  regs.ds = Pop(_pc);
  _pc.CountInstructionCycles(MOVE_IMMEDIATE + PORT_ACCESS + 2 * POP_REGISTER + INTERRUPT_RETURN);
  _pc.ReturnInterrupt();
}

// F000:E040: reads the scan code and discards it, pulses port 61h bit 7 to acknowledge it, and sends EOI.
void RunKeyboardHandler(Pc& _pc)
{
  Registers& regs = _pc.Processor().Regs();
  PortRouter& ports = _pc.Ports();
  regs.flags = static_cast<std::uint16_t>(regs.flags | FLAG_INTERRUPT);
  Push(_pc, regs.ax);
  const auto setLow = [&regs](std::uint8_t _value) { regs.ax = static_cast<std::uint16_t>((regs.ax & 0xFF00u) | _value); };
  setLow(ports.In8(KEYBOARD_DATA_PORT));
  setLow(ports.In8(KEYBOARD_CONTROL_PORT));
  const auto cleared = static_cast<std::uint8_t>(regs.ax | KEYBOARD_CLEAR);
  setLow(cleared);
  ports.Out8(KEYBOARD_CONTROL_PORT, cleared);
  const auto enabled = static_cast<std::uint8_t>(cleared & ~KEYBOARD_CLEAR);
  setLow(enabled);
  ports.Out8(KEYBOARD_CONTROL_PORT, enabled);
  setLow(END_OF_INTERRUPT);
  ports.Out8(PIC_COMMAND_PORT, END_OF_INTERRUPT);
  regs.ax = Pop(_pc);
  _pc.CountInstructionCycles(STI + PUSH_REGISTER + 2 * PORT_ACCESS + LOGIC_IMMEDIATE + PORT_ACCESS + LOGIC_IMMEDIATE + PORT_ACCESS +
                             MOVE_IMMEDIATE + PORT_ACCESS + POP_REGISTER + INTERRUPT_RETURN);
  _pc.ReturnInterrupt();
}

} // namespace

void HookNativeFirmware(Pc& _pc)
{
  _pc.Hook(Firmware::ROM_SEGMENT, Firmware::TIMER_HANDLER_OFFSET, "BiosTimerHandler", &RunTimerHandler, NativeContract{},
           NativeReturn::Interrupt);
  _pc.Hook(Firmware::ROM_SEGMENT, Firmware::KEYBOARD_HANDLER_OFFSET, "BiosKeyboardHandler", &RunKeyboardHandler, NativeContract{},
           NativeReturn::Interrupt);
}

} // namespace Machine
