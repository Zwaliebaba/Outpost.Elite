// Machine/ServiceFault.h
#pragma once

#include <cstdint>

namespace Machine
{

/// Why a BIOS, DOS or mouse call was refused.
enum class FaultKind : std::uint8_t
{
  UnknownFunction,    ///< AH (AX for int 33h) selects a function the reference never calls.
  UnknownSubfunction, ///< The function is one it calls, but AL or BH selects a variant it never asks for.
  UnsupportedRequest, ///< A function it calls, with an argument it never passes: a video mode above 6, a standard handle...
  WouldBlock          ///< The call would wait for input that can never arrive.
};

/// A software interrupt the services refused to service (plan §5, Phase 2). The call is recorded and nothing is done:
/// no register, flag or byte of memory changes, and execution continues after the INT. The integrator checks for one
/// after every step and stops; anything else would let the services drift into a general BIOS and DOS.
///
/// A plain record (R8). `ip` is the INT instruction's own address: every serviced vector arrives through the two-byte
/// CD nn, so it is two bytes before the return address.
struct ServiceFault
{
  FaultKind kind = FaultKind::UnknownFunction;
  std::uint8_t vector = 0;
  std::uint8_t ah = 0;
  std::uint8_t al = 0;
  std::uint16_t cs = 0;
  std::uint16_t ip = 0;
};

} // namespace Machine
