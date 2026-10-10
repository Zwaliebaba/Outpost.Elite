// GameLogic/Video.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's video routines, ported (plan §5 Phase 3, ADR-010): drawing: lines, spans, the space view and the dashboard into CGA memory. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> VideoEntries() noexcept;

} // namespace Elite
