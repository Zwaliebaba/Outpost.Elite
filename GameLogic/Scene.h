// GameLogic/Scene.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's 3d routines, ported (plan §5 Phase 3, ADR-010): the 3D scene: transforming, projecting and drawing objects. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> SceneEntries() noexcept;

} // namespace Elite
