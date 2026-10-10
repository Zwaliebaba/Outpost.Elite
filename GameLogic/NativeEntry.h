// GameLogic/NativeEntry.h
#pragma once

#include "Guest.h"
#include "NativeCode.h"

#include <cstdint>
#include <string_view>

namespace Elite
{

/// How the original leaves a hooked entry: what InstallNativeRoutines does once the body is done.
enum class EntryReturn : std::uint8_t
{
  Far,      ///< RETF: Start's, to the far return it pushed
  Interrupt ///< IRET: offset, segment, then flags; an interrupt handler's
};

/// One hooked entry of the reference (ADR-010): where it is, what Symbols.tsv calls it, the native body, and how the original
/// returns. Each subsystem's file lists its own, and InstallNativeRoutines hooks them all: the body, then the return the original
/// makes.
struct NativeEntry
{
  std::uint16_t offset;
  std::string_view name;
  void (*body)(Guest&);
  EntryReturn exit;
  Machine::NativeWait wait = Machine::NativeWait::Never; ///< whether it can wait (Pc::Wait)
};

} // namespace Elite
