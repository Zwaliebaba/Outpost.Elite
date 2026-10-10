// GameLogic/Docked.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's docked routines, ported (plan §5 Phase 3, ADR-010): the docked screens and their menus. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> DockedEntries() noexcept;

} // namespace Elite
