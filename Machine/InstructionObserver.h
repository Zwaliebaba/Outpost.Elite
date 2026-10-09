// Machine/InstructionObserver.h
#pragma once

namespace Machine
{

struct Registers;

/// Told about every instruction the CPU starts, with the registers as they are before it executes:
/// CS:IP at its first byte, prefixes included. When a step takes a hardware interrupt, the observer
/// sees the handler's first instruction, after the entry, not the interrupted one, so a trace built
/// from it lists the instructions that ran. It is what ADR-003's boot trace is recorded through.
class InstructionObserver
{
public:
  InstructionObserver() = default;
  InstructionObserver(const InstructionObserver&) = delete;
  InstructionObserver& operator=(const InstructionObserver&) = delete;
  virtual ~InstructionObserver() = default;

  virtual void BeforeInstruction(const Registers& _registers) = 0;
};

} // namespace Machine
