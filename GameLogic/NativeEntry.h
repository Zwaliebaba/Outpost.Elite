// GameLogic/NativeEntry.h
#pragma once

#include "Guest.h"

#include <cstdint>
#include <string_view>

namespace Elite
{

/// How the original leaves a hooked entry, which says what the entry is.
enum class EntryReturn : std::uint8_t
{
  Far,      ///< RETF: Start's, to the far return to PSP:0000 it pushed, where DOS's int 20h ends the program
  Interrupt ///< IRET: offset, segment, then flags; an interrupt handler's
};

/// One entry of the reference that the platform reaches (ADR-010): where it is, what Symbols.tsv calls it, the native body,
/// and how the original returns. Each subsystem's file lists its own, and InstallNativeRoutines hands them all to the
/// platform: the program's start, and the interrupt handlers its vectors name.
struct NativeEntry
{
  std::uint16_t offset;
  std::string_view name;
  void (*body)(Guest&);
  EntryReturn exit;
};

} // namespace Elite
