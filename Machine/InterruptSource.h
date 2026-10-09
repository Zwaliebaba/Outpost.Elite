// Machine/InterruptSource.h
#pragma once

#include <cstdint>

namespace Machine
{

/// What the CPU's INTR line is wired to: on the PC, the 8259 interrupt controller.
///
/// At each instruction boundary where IF is set and no interrupt shadow is in force, the CPU asks
/// InterruptPending(). When it answers true the CPU runs the 8088's acknowledge cycle by calling
/// AcknowledgeInterrupt(), which returns the vector the controller puts on the bus and moves that
/// request from pending to in service. The CPU then vectors through the interrupt table as for INT n.
class InterruptSource
{
public:
  InterruptSource() = default;
  InterruptSource(const InterruptSource&) = delete;
  InterruptSource& operator=(const InterruptSource&) = delete;
  virtual ~InterruptSource() = default;

  [[nodiscard]] virtual bool InterruptPending() const = 0;
  [[nodiscard]] virtual std::uint8_t AcknowledgeInterrupt() = 0;
};

} // namespace Machine
