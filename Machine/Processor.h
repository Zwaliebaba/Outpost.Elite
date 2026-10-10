// Machine/Processor.h
#pragma once

#include "Registers.h"

#include <cstdint>
#include <vector>

namespace Machine
{

class HostServices;
class InstructionObserver;
class InterruptSource;

/// What runs the program in a Pc (ADR-011): the registers, the step, and the interrupt line. Two stand
/// behind it:
///
/// * Cpu, the 8088 interpreter (the Interpreter project), which executes the program's instructions;
/// * Dispatcher, which executes none of them. Once every routine the program reaches is native (ADR-010),
///   a step only ever takes an interrupt and stops at a hooked entry, and that is all a Dispatcher does.
///
/// A step starts by taking a hardware interrupt, when IF is set, no interrupt shadow is in force and the
/// interrupt source has a request: the acknowledge cycle, then the flags, CS and IP pushed, IF and TF
/// cleared, and CS:IP loaded from the vector. Then, at a hooked entry, the step stops without executing
/// anything and AtHook() says so; the Pc runs the native routine. Both processors do this alike, so a Pc
/// whose program is all native runs the same, step for step, on either.
class Processor
{
public:
  Processor() = default;
  Processor(const Processor&) = delete;
  Processor& operator=(const Processor&) = delete;
  virtual ~Processor() = default;

  /// The hook consulted on a software interrupt. Null means every interrupt vectors.
  virtual void SetHostServices(HostServices* _host) noexcept = 0;

  /// What the INTR line is wired to. Null means no hardware interrupt ever arrives.
  virtual void SetInterruptSource(InterruptSource* _source) noexcept = 0;

  /// Where to mark the linear address of every instruction started, or null to stop. A map holding fewer
  /// than Memory::SIZE_BYTES entries is grown to that size with zeros.
  virtual void SetExecutionMap(std::vector<std::uint8_t>* _map) = 0;

  /// Told about every instruction as it starts, or null to stop.
  virtual void SetInstructionObserver(InstructionObserver* _observer) noexcept = 0;

  /// A map of the address space with a non-zero byte at every hooked entry (ADR-010), or null for none.
  /// The map must outlive its use.
  virtual void SetHookMap(const std::vector<std::uint8_t>* _map) noexcept = 0;

  /// Whether the next Step() takes a hardware interrupt before anything else.
  [[nodiscard]] virtual bool InterruptDue() const = 0;

  /// Whether the last Step() stopped at a hooked entry instead of executing.
  [[nodiscard]] virtual bool AtHook() const noexcept = 0;

  /// Whether the last Step() reached code it cannot run: never for an interpreter; for a Dispatcher, any
  /// code no native routine stands in for. The Pc stops there (StopReason::Unported).
  [[nodiscard]] virtual bool AtUnportedCode() const noexcept = 0;

  /// The 8088's reset state: CS:IP = FFFF:0000, flags clear, everything else zero.
  virtual void Reset() noexcept = 0;

  /// One step, and its approximate 8088 clock count: the interrupt entry's, if it took one, and the
  /// instruction's, if it executed one.
  virtual std::uint32_t Step() = 0;

  [[nodiscard]] virtual Registers& Regs() noexcept = 0;
  [[nodiscard]] virtual const Registers& Regs() const noexcept = 0;

  [[nodiscard]] virtual bool Halted() const noexcept = 0;

  /// Instructions executed since construction or Reset().
  [[nodiscard]] virtual std::uint64_t InstructionCount() const noexcept = 0;

  /// Hardware interrupts taken since construction or Reset().
  [[nodiscard]] virtual std::uint64_t HardwareInterruptCount() const noexcept = 0;

  /// Whether it executes the program's instructions, as an interpreter does. A Pc compares native code
  /// with the original (ADR-010 item 4) only on one that does.
  [[nodiscard]] virtual bool Interprets() const noexcept = 0;

  /// What a hardware interrupt's entry costs the 8088: Intel's 8086 figure plus five word transfers over
  /// the 8-bit bus (three pushes, two vector reads).
  static constexpr std::uint32_t HARDWARE_INTERRUPT_CYCLES = 61 + 5 * 4;

  /// What a halted processor's step costs while it waits.
  static constexpr std::uint32_t HALT_IDLE_CYCLES = 4;
};

} // namespace Machine
