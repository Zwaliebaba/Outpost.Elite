// GameLogic/Galaxy.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's galaxy routines, ported (plan §5 Phase 3, ADR-010): the procedural galaxy, its systems and the charts. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> GalaxyEntries() noexcept;

} // namespace Elite
