// Machine/Cpu.h
#pragma once

#include "Registers.h"

#include <cstdint>
#include <vector>

namespace Machine
{

class HostServices;
class InterruptSource;
class Memory;
class PortBus;

/// An 8086/8088 interpreter: the whole real-mode instruction set, as an 8088 executes it.
///
/// Fidelity. Checked against the SingleStepTests 8088 v2 suite (ADR-003), all of which passes:
/// every register, every byte of memory, and every flag except those the suite's metadata calls
/// undefined for the opcode. So every flag the 8088 defines is exact, AF and PF included, and so
/// are several Intel calls undefined: OF after a multi-bit shift or rotate, and all of DAA's and
/// DAS's flags but OF, which differ from Intel's later pseudocode (see ExecuteBcd).
///
/// The undocumented opcodes behave as on the 8088, as the suite shows: 60-6F are aliases of the
/// conditional jumps 70-7F, C0/C1/C8/C9 of the returns C2/C3/CA/CB, 82 of 80, D6 is SALC, D0-D3 /6
/// is SETMO and SETMOC, F6/F7 /1 alias TEST, FF /7 aliases PUSH, 8C/8E look only at the low two
/// bits of the segment field (so MOV CS, r/m works), C6/C7 ignore the reg field, and D8-DF (ESC)
/// decode their operand and do nothing else. A REP prefix in front of IDIV negates the quotient.
///
/// Not covered by the suite, and therefore unverified: 0F (POP CS, implemented as such); 8F with a
/// reg field other than 0 (treated as POP); FE /2-7 (treated as FF /2-7); the register forms of
/// LEA, LES, LDS and of FF /3 and /5 (the 8088 reuses its last effective address there; this reads
/// 0000:0000, and LEA leaves the register alone); WAIT (returns at once, no coprocessor); HLT;
/// LOCK and its alias F1 (ignored); REP in front of IMUL (negates the product, as reenigne
/// describes the microcode); the trap flag; and everything to do with interrupt timing. The game
/// executes none of these opcodes. It does depend on hardware interrupts (plan §7.3), which the
/// suite cannot exercise; ADR-003's boot trace and replays are what will check them.
///
/// The divide error vectors through IVT entry 0 the way the 8088 does it (plan §7.1): the return
/// address pushed is that of the next instruction (later CPUs push the faulting one), and the flags
/// pushed are those of the microcode's last ALU step, which the suite pins down exactly (see
/// ExecuteDivide).
///
/// One Step() is one instruction, prefixes included. A REP-prefixed string instruction runs to
/// completion within that one Step(), however large CX is, so a hardware interrupt cannot land in
/// the middle of one; the 8088 would take it between iterations. The game's string instructions
/// are short block copies and fills, and nothing it does depends on the difference.
///
/// Interrupts. The INTR line is wired to an InterruptSource, the PIC. At the start of every Step()
/// for which IF is set and no interrupt shadow is in force, the CPU asks it InterruptPending(); if
/// so, it runs the acknowledge cycle, AcknowledgeInterrupt(), and vectors through the entry that
/// returns. Loading any segment register with MOV or POP casts a shadow over the following
/// instruction, as on the 8088 (later CPUs only do this for SS), and so does STI, whose effect is
/// therefore delayed by one instruction. Single-step (TF) traps through vector 1 after an
/// instruction that began and ended with TF set. HLT stops execution until an interrupt is taken.
///
/// Execution map. Optionally, the CPU marks the linear address of every instruction it starts, its
/// first prefix byte, with 1 in a map of the address space: what Phase 2's traces use to settle
/// which code runs (Reference-Map.md, "Coverage"). Without a map it costs one predictable branch.
///
/// Timing. Step() returns an approximate 8088 clock count: the documented best case for the
/// instruction from Intel's 8086 table, plus the effective-address cycles, plus 4 clocks for every
/// word the 8088 moves over its 8-bit bus, plus 2 per prefix byte. It does not model the prefetch
/// queue, wait states, DRAM refresh or bus contention, so a real 8088 is usually somewhat slower.
/// MUL, IMUL, DIV, IDIV and AAM take data-dependent time on the real part and are given Intel's
/// minimum. Deterministic: no wall clock, no randomness.
class Cpu
{
public:
  Cpu(Memory& _memory, PortBus& _ports) noexcept;

  /// The hook consulted on INT n, INT 3 and INTO. Null (the default) means every interrupt vectors.
  void SetHostServices(HostServices* _host) noexcept;

  /// What the INTR line is wired to. Null (the default) means no hardware interrupt ever arrives.
  void SetInterruptSource(InterruptSource* _source) noexcept;

  /// Where to mark the instructions executed, or null to stop. A map holding fewer than
  /// Memory::SIZE_BYTES entries is grown to that size with zeros, which is why this is not noexcept.
  /// Marks are only ever set; clearing the map is the caller's business.
  void SetExecutionMap(std::vector<std::uint8_t>* _map);

  /// The 8088's reset state: CS:IP = FFFF:0000, flags clear, everything else zero.
  void Reset() noexcept;

  /// Executes one instruction and returns its approximate 8088 clock count. A pending hardware
  /// interrupt is taken first when it can be, and its entry cycles are included. A halted CPU with
  /// nothing to take returns HALT_IDLE_CYCLES without executing anything.
  std::uint32_t Step();

  [[nodiscard]] Registers& Regs() noexcept
  {
    return m_regs;
  }

  [[nodiscard]] const Registers& Regs() const noexcept
  {
    return m_regs;
  }

  [[nodiscard]] bool Halted() const noexcept
  {
    return m_halted;
  }

  /// Instructions executed since construction or Reset(): the clock replays are recorded against.
  [[nodiscard]] std::uint64_t InstructionCount() const noexcept
  {
    return m_instructionCount;
  }

  static constexpr std::uint32_t HALT_IDLE_CYCLES = 4;

private:
  // A decoded ModRM operand. For a register operand only `rm` matters; for memory, the segment
  // and offset the effective address resolved to.
  struct Operand
  {
    bool isRegister = false;
    std::uint8_t reg = 0;
    std::uint8_t rm = 0;
    std::uint16_t segment = 0;
    std::uint16_t offset = 0;
  };

  enum class Repeat : std::uint8_t
  {
    None,
    WhileZero,   // F3: REP, REPE, REPZ
    WhileNotZero // F2: REPNE, REPNZ
  };

  void Execute(std::uint8_t _opcode);
  void ExecuteAlu(std::uint8_t _opcode);
  void ExecuteGroup1(std::uint8_t _opcode);
  void ExecuteShift(std::uint8_t _opcode);
  void ExecuteGroup3(std::uint8_t _opcode);
  void ExecuteGroup4And5(std::uint8_t _opcode);
  void ExecuteString(std::uint8_t _opcode);
  void ExecuteBcd(std::uint8_t _opcode);
  void ExecuteMultiply(const Operand& _operand, bool _word, bool _signed);
  void ExecuteDivide(const Operand& _operand, bool _word, bool _signed);

  // Fetch and decode.
  [[nodiscard]] std::uint8_t Fetch8() noexcept;
  [[nodiscard]] std::uint16_t Fetch16() noexcept;
  [[nodiscard]] std::uint16_t FetchImmediate(bool _word) noexcept;
  [[nodiscard]] Operand DecodeModRm() noexcept;
  [[nodiscard]] std::uint16_t SegmentOr(std::uint16_t _default) const noexcept;

  // Operands.
  [[nodiscard]] std::uint16_t Reg16(std::uint8_t _index) const noexcept;
  void SetReg16(std::uint8_t _index, std::uint16_t _value) noexcept;
  [[nodiscard]] std::uint8_t Reg8(std::uint8_t _index) const noexcept;
  void SetReg8(std::uint8_t _index, std::uint8_t _value) noexcept;
  [[nodiscard]] std::uint16_t ReadReg(std::uint8_t _index, bool _word) const noexcept;
  void WriteReg(std::uint8_t _index, bool _word, std::uint16_t _value) noexcept;
  [[nodiscard]] std::uint16_t ReadMemory(std::uint16_t _segment, std::uint16_t _offset, bool _word) const noexcept;
  [[nodiscard]] std::uint16_t ReadOperand(const Operand& _operand, bool _word) const noexcept;
  void WriteOperand(const Operand& _operand, bool _word, std::uint16_t _value) noexcept;
  [[nodiscard]] std::uint16_t Segment(std::uint8_t _index) const noexcept;
  void SetSegment(std::uint8_t _index, std::uint16_t _value) noexcept;

  // Stack and control transfer.
  void Push(std::uint16_t _value) noexcept;
  [[nodiscard]] std::uint16_t Pop() noexcept;
  void EnterInterrupt(std::uint8_t _vector) noexcept;
  void SoftwareInterrupt(std::uint8_t _vector);
  void JumpShort(bool _taken, std::uint32_t _takenCycles, std::uint32_t _notTakenCycles) noexcept;

  // Flags and arithmetic. `_word` picks 16-bit (true) or 8-bit operation; results are masked.
  [[nodiscard]] bool Flag(std::uint16_t _flag) const noexcept
  {
    return (m_regs.flags & _flag) != 0;
  }

  void SetFlag(std::uint16_t _flag, bool _set) noexcept;
  void SetFlagsWord(std::uint16_t _value) noexcept;
  void SetSignZeroParity(std::uint32_t _result, bool _word) noexcept;
  [[nodiscard]] std::uint16_t Add(std::uint32_t _a, std::uint32_t _b, std::uint32_t _carry, bool _word) noexcept;
  [[nodiscard]] std::uint16_t Subtract(std::uint32_t _a, std::uint32_t _b, std::uint32_t _borrow, bool _word) noexcept;
  [[nodiscard]] std::uint16_t Logic(std::uint32_t _result, bool _word) noexcept;
  [[nodiscard]] std::uint16_t Alu(std::uint8_t _operation, std::uint16_t _a, std::uint16_t _b, bool _word) noexcept;
  [[nodiscard]] std::uint16_t IncDec(std::uint16_t _value, bool _increment, bool _word) noexcept;
  [[nodiscard]] std::uint16_t Shift(std::uint8_t _operation, std::uint16_t _value, std::uint32_t _count, bool _word) noexcept;

  Memory& m_memory;
  PortBus& m_ports;
  HostServices* m_host = nullptr;
  Registers m_regs{};
  InterruptSource* m_interrupts = nullptr;
  std::vector<std::uint8_t>* m_executionMap = nullptr;
  std::uint64_t m_instructionCount = 0;
  std::uint32_t m_cycles = 0;

  // Prefix state for the instruction being executed.
  std::int8_t m_segmentOverride = -1; // 0 ES, 1 CS, 2 SS, 3 DS, -1 none
  Repeat m_repeat = Repeat::None;

  bool m_halted = false;
  bool m_interruptShadow = false;
};

} // namespace Machine
