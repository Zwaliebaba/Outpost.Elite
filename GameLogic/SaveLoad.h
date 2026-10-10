// GameLogic/SaveLoad.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's save-load routines, ported (plan §5 Phase 3, ADR-010): saving and loading commanders. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> SaveLoadEntries() noexcept;

} // namespace Elite
