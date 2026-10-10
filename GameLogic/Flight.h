// GameLogic/Flight.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's flight routines, ported (plan §5 Phase 3, ADR-010): flying: the player's ship, the local bubble, the dashboard's logic. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> FlightEntries() noexcept;

} // namespace Elite
