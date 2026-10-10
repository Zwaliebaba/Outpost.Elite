// GameLogic/SaveLoad.h
#pragma once

#include "GameState.h"
#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's save-load routines, ported (plan §5 Phase 3, ADR-010): saving and loading commanders. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> SaveLoadEntries() noexcept;

/// CriticalErrorInterrupt (CS:02F0), the int 24h handler while SaveScreenshot writes: diskError=1, and AL=0, which
/// tells DOS to ignore the error; AH is left the data segment's high byte.
void CriticalErrorInterrupt(Guest& _guest);

/// PerformDiskRequest (CS:02FF): AL=1 loads commanderFileName into commanderBlock, 2 saves it, 3 deletes it, anything
/// else lists the *.cdr files into commanderFileList. diskError=1 on a failure. AX, BX, CX, DX, SI and DI clobbered.
void PerformDiskRequest(Guest& _guest);

/// ShowDiskError (CS:0470): diskError cleared and "DISC ERROR" shown, on the text page or drawn on the graphics
/// screen. Out: ES=B800h; AX, BX, CX, SI, DI clobbered.
void ShowDiskError(Guest& _guest);

/// ShowDiscControlScreen (CS:660B): the Disc/Control menu, or, back from Start's disk work, what it came to. It
/// waits for keys. A function key returns, with AH its scan code; a disk operation (or leaving for DOS) drops this
/// call's return address and RunTitleAndDocked's, through LeaveGameLoopForDisk, so that its RET returns from GameLoop
/// to Start.
void ShowDiscControlScreen(Guest& _guest);

/// PromptCommanderFileName (CS:6862): FILENAME? and up to 8 characters through ReadTextLine into
/// commanderFileName, folded to upper case, and ".CDR" after them. Out: CF=0. A bad name drops the return address
/// and carries on in ShowDiscControlScreen's key loop in its place.
void PromptCommanderFileName(Guest& _guest);

/// PrintCommanderCatalogue (CS:68CE): ten text rows blanked, and commanderFileList printed in columns of ten. Every
/// general register clobbered.
void PrintCommanderCatalogue(Guest& _guest);

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// SaveStartupCommander (CS:4660): commanderFileBytes of commanderBlock copied to startupCommander, a byte at a time,
/// going up, or down from each start when _backward (REP MOVSB with the direction flag set).
void SaveStartupCommander(GameState& _state, bool _backward);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void SaveStartupCommanderEntry(Guest& _guest); ///< AX, CX, SI, DI and ES clobbered.

} // namespace Elite
