// Machine/Dispatcher.h
#pragma once

#include "Registers.h"

#include <cstdint>
#include <vector>

namespace Machine
{

class HostServices;
class InterruptSource;
class Memory;

/// What runs the program in a Pc (ADR-011): the registers, the step, and the interrupt line. Every routine the program
/// reaches is native (ADR-010), so it executes none of the program's instructions.
///
/// A step starts by taking a hardware interrupt, when IF is set and the interrupt source has a request: the acknowledge
/// cycle, then the flags, CS and IP pushed, IF and TF cleared, and CS:IP loaded from the vector, as the 8088 does. Then,
/// at a hooked entry, the step stops without executing anything and AtHook() says so; the Pc runs the native routine.
///
/// Two pieces of code it does run, as the single instructions they are, because they are the machine's own and not the
/// program's:
///
/// * an IRET in the ROM: the stubs the vectors nothing serves point to, such as int 33h with no mouse driver;
/// * the INT 20h at offset 0 of a PSP, where a program's last RETF lands when it ends.
///
/// They take the 8088's cycles, as the interpreter charged them before D7 deleted it, so that the instruction clock the
/// game port runs on (Pc::InstructionCycles) advances as it did when the Pc interpreted. Anything else it reaches is code no
/// native routine stands in for: it executes nothing there, and AtUnportedCode() says so.
class Dispatcher
{
public:
  explicit Dispatcher(Memory& _memory) noexcept;
  Dispatcher(const Dispatcher&) = delete;
  Dispatcher& operator=(const Dispatcher&) = delete;
  ~Dispatcher() = default;

  /// The hook consulted on the INT 20h it runs. Null means it vectors.
  void SetHostServices(HostServices* _host) noexcept;

  /// What the INTR line is wired to. Null means no hardware interrupt ever arrives.
  void SetInterruptSource(InterruptSource* _source) noexcept;

  /// A map of the address space with a non-zero byte at every hooked entry (ADR-010), or null for none. The map must
  /// outlive its use.
  void SetHookMap(const std::vector<std::uint8_t>* _map) noexcept;

  /// Whether the next Step() takes a hardware interrupt before anything else.
  [[nodiscard]] bool InterruptDue() const;

  /// Whether the last Step() stopped at a hooked entry instead of executing.
  [[nodiscard]] bool AtHook() const noexcept
  {
    return m_atHook;
  }

  /// Whether the last Step() reached code no native routine stands in for. The Pc stops there (StopReason::Unported).
  [[nodiscard]] bool AtUnportedCode() const noexcept
  {
    return m_atUnportedCode;
  }

  /// The 8088's reset state: CS:IP = FFFF:0000, flags clear, everything else zero.
  void Reset() noexcept;

  /// One step, and its approximate 8088 clock count: the interrupt entry's, if it took one, and the instruction's, if it
  /// ran one.
  std::uint32_t Step();

  [[nodiscard]] Registers& Regs() noexcept
  {
    return m_regs;
  }

  [[nodiscard]] const Registers& Regs() const noexcept
  {
    return m_regs;
  }

  /// The ROM's IRETs and the PSP's INT 20h executed since construction or Reset(): the only instructions it runs.
  [[nodiscard]] std::uint64_t InstructionCount() const noexcept
  {
    return m_instructionCount;
  }

  /// Hardware interrupts taken since construction or Reset().
  [[nodiscard]] std::uint64_t HardwareInterruptCount() const noexcept
  {
    return m_hardwareInterrupts;
  }

  /// What a hardware interrupt's entry costs the 8088: Intel's 8086 figure plus five word transfers over the 8-bit bus
  /// (three pushes, two vector reads).
  static constexpr std::uint32_t HARDWARE_INTERRUPT_CYCLES = 61 + 5 * 4;

private:
  void EnterInterrupt(std::uint8_t _vector) noexcept;
  void Push(std::uint16_t _value) noexcept;
  [[nodiscard]] std::uint16_t Pop() noexcept;
  [[nodiscard]] std::uint32_t RunMachineCode();

  Memory& m_memory;
  HostServices* m_host = nullptr;
  InterruptSource* m_interrupts = nullptr;
  const std::vector<std::uint8_t>* m_hookMap = nullptr;
  Registers m_regs{};
  std::uint64_t m_instructionCount = 0;
  std::uint64_t m_hardwareInterrupts = 0;
  bool m_atHook = false;
  bool m_atUnportedCode = false;
};

} // namespace Machine
