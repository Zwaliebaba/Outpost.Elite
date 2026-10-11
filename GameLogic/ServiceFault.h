// GameLogic/ServiceFault.h
#pragma once

#include <cstdint>

namespace Elite
{

/// Why a BIOS, DOS or mouse call was refused.
enum class FaultKind : std::uint8_t
{
  UnknownFunction,    ///< AH (AX for int 33h) selects a function the reference never calls.
  UnsupportedRequest, ///< A function it calls, with an argument it never passes: a video mode above 6, a standard handle...
  WouldBlock          ///< The call would wait for input that can never arrive.
};

/// A service call the platform refused (ADR-006 item 3): the call is recorded, nothing is done, and the run stops
/// (StopReason::Fault). Anything else would let the services drift into a general BIOS and DOS. A plain record (R8): the
/// call's vector, and the AH and AL it was made with.
struct ServiceFault
{
  FaultKind kind = FaultKind::UnknownFunction;
  std::uint8_t vector = 0;
  std::uint8_t ah = 0;
  std::uint8_t al = 0;
};

} // namespace Elite
