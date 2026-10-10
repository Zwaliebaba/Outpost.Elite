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

/// PerformDiskRequest (CS:02FF): AL=1 loads commanderFileName into commanderBlock, 2 saves it, 3 deletes it, anything
/// else lists the *.cdr files into commanderFileList. diskError=1 on a failure. AX, BX, CX, DX, SI and DI clobbered.
void PerformDiskRequest(Guest& _guest);

/// SaveStartupCommander (CS:4660): commanderBlock copied to startupCommander. AX, CX, SI, DI and ES clobbered.
void SaveStartupCommander(Guest& _guest);

} // namespace Elite
