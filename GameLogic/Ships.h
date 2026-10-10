// GameLogic/Ships.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's ships routines, ported (plan §5 Phase 3, ADR-010): the ship slots, blueprints and their vertex programs. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> ShipsEntries() noexcept;

} // namespace Elite
