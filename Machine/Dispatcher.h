// Machine/Dispatcher.h
#pragma once

#include "Processor.h"
#include "Registers.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Machine
{

class Memory;
class PortBus;

/// The Processor of a Pc whose program is all native routines (ADR-011). It executes none of the
/// program's instructions. A step takes a hardware interrupt as the 8088 does, then stops at the hooked
/// entry CS:IP is at, and the Pc runs the native routine there.
///
/// Two pieces of code it does run, as the single instructions they are, because they are the machine's
/// own and not the program's:
///
/// * an IRET in the ROM: the stubs the vectors nothing serves point to, such as int 33h with no mouse
///   driver;
/// * the INT 20h at offset 0 of a PSP, where a program's last RETF lands when it ends.
///
/// They take the 8088's cycles, as Cpu charges them, so that the instruction clock the game port runs
/// on (Pc::InstructionCycles) advances as it does when the Pc interprets. Anything else it reaches is code
/// no native routine stands in for: it executes nothing there, and AtUnportedCode() says so.
class Dispatcher final : public Processor
{
public:
  explicit Dispatcher(Memory& _memory) noexcept;

  void SetHostServices(HostServices* _host) noexcept override;
  void SetInterruptSource(InterruptSource* _source) noexcept override;
  void SetExecutionMap(std::vector<std::uint8_t>* _map) override;
  void SetInstructionObserver(InstructionObserver* _observer) noexcept override;
  void SetHookMap(const std::vector<std::uint8_t>* _map) noexcept override;
  [[nodiscard]] bool InterruptDue() const override;

  [[nodiscard]] bool AtHook() const noexcept override
  {
    return m_atHook;
  }

  [[nodiscard]] bool AtUnportedCode() const noexcept override
  {
    return m_atUnportedCode;
  }

  void Reset() noexcept override;
  std::uint32_t Step() override;

  [[nodiscard]] Registers& Regs() noexcept override
  {
    return m_regs;
  }

  [[nodiscard]] const Registers& Regs() const noexcept override
  {
    return m_regs;
  }

  [[nodiscard]] bool Halted() const noexcept override
  {
    return m_halted;
  }

  /// The ROM's IRETs and the PSP's INT 20h executed: the only instructions it runs.
  [[nodiscard]] std::uint64_t InstructionCount() const noexcept override
  {
    return m_instructionCount;
  }

  [[nodiscard]] std::uint64_t HardwareInterruptCount() const noexcept override
  {
    return m_hardwareInterrupts;
  }

  [[nodiscard]] bool Interprets() const noexcept override
  {
    return false;
  }

private:
  void EnterInterrupt(std::uint8_t _vector) noexcept;
  void Push(std::uint16_t _value) noexcept;
  [[nodiscard]] std::uint16_t Pop() noexcept;
  [[nodiscard]] std::uint32_t RunMachineCode();

  Memory& m_memory;
  HostServices* m_host = nullptr;
  InterruptSource* m_interrupts = nullptr;
  std::vector<std::uint8_t>* m_executionMap = nullptr;
  InstructionObserver* m_observer = nullptr;
  const std::vector<std::uint8_t>* m_hookMap = nullptr;
  Registers m_regs{};
  std::uint64_t m_instructionCount = 0;
  std::uint64_t m_hardwareInterrupts = 0;
  bool m_halted = false;
  bool m_atHook = false;
  bool m_atUnportedCode = false;
};

/// A Dispatcher, for a Pc whose program is all native (Pc::ProcessorFactory). _ports is not used: the
/// Dispatcher makes no port access of its own.
[[nodiscard]] std::unique_ptr<Processor> MakeDispatcher(Memory& _memory, PortBus& _ports);

} // namespace Machine
