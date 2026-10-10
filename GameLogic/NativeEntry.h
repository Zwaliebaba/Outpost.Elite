// GameLogic/NativeEntry.h
#pragma once

#include "Guest.h"
#include "NativeCode.h"

#include <cstdint>
#include <string_view>

namespace Elite
{

/// One hooked entry of the reference (ADR-010): where it is, what Symbols.tsv calls it, the native body,
/// the contract it is compared on, and how the original returns. Each subsystem's file lists its own,
/// and InstallNativeRoutines hooks them all: the body, then the return the original makes.
struct NativeEntry
{
  std::uint16_t offset;
  std::string_view name;
  void (*body)(Guest&);
  Machine::NativeContract contract;
  Machine::NativeReturn exit = Machine::NativeReturn::Near;
  std::uint16_t popBytes = 0;                            ///< what RET n or RETF n also pops
  Machine::NativeWait wait = Machine::NativeWait::Never; ///< whether it can wait (Pc::Wait, or original code it calls)
};

// The contracts most entries have.
inline constexpr Machine::NativeContract PRESERVES_ALL{};

} // namespace Elite
