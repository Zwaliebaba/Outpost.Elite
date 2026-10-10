// Machine/HostServices.h
#pragma once

#include <cstdint>

namespace Machine
{

struct Registers;

/// The hook through which software interrupts reach the host: BIOS and DOS, emulated at the call
/// level for exactly the functions the reference uses (plan §5, Phase 2).
///
/// The CPU consults it on INT n (CD), INT 3 (CC) and INTO (CE, only when OF is set and the interrupt
/// is therefore taken), and Pc::CallInterrupt on native code's INT n. When ServiceInterrupt returns true the call was handled in C++: the CPU does
/// not touch the stack or the interrupt table, and execution continues at the instruction after the
/// INT, with whatever registers and memory the service left. When it returns false the CPU vectors
/// through the interrupt table exactly as an 8088 does, so a handler the program installed itself
/// runs as written.
///
/// CPU exceptions (the divide error) and hardware interrupts never come here; they always vector.
class HostServices
{
public:
  HostServices() = default;
  HostServices(const HostServices&) = delete;
  HostServices& operator=(const HostServices&) = delete;
  virtual ~HostServices() = default;

  /// Called with the registers the INT is made with, IP already past the instruction. Returns true if the
  /// interrupt was serviced.
  [[nodiscard]] virtual bool ServiceInterrupt(Registers& _regs, std::uint8_t _vector) = 0;
};

} // namespace Machine
