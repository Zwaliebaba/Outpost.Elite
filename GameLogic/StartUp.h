// GameLogic/StartUp.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's start-up and protection routines, ported (plan §5 Phase 3, ADR-010): start-up, the copy protection's remains and the way out to DOS. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> StartUpEntries() noexcept;

} // namespace Elite
