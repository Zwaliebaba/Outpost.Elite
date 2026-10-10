// GameLogic/SaveLoad.h
#pragma once

#include "GameState.h"
#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's save-load routines, ported (plan §5 Phase 3, ADR-010): saving and loading commanders. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> SaveLoadEntries() noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// How the disc menu ends.
struct DiscMenuExit
{
  bool leaves;           ///< through LeaveGameLoopForDisk, for a disk operation or to leave for DOS: its two POP AX drop the
                         ///< return addresses of the menu's call and RunTitleAndDocked's, so that the RET returns from GameLoop
                         ///< to Start
  std::uint8_t scanCode; ///< otherwise the F-key that ends it
  std::uint8_t al;       ///< and AL as its GetKey leaves it with the key in AH
};

/// ShowDiscControlScreen (CS:660B), with RunDiscControlKeys, ChooseJoystick, ShowDiskResult and PromptCommanderFileName, which
/// jumps into its key loop after a bad name, as one unit (ADR-012 item 14): the Disc/Control menu, or, back from Start's disk
/// work, what it came to or the question whether to retry it; then its keys. _backward is the direction flag, which
/// DrawDockedFrame goes by. Waits for keys as a rule (ADR-015).
DiscMenuExit ShowDiscControlScreen(GameState& _state, Hardware& _hardware, bool _backward);

/// PromptCommanderFileName (CS:6862): FILENAME? and up to 8 characters through ReadTextLine into commanderFileName, folded to
/// upper case, and ".CDR" after them. Returns false for a bad name, which the original answers by dropping its return address
/// and reading the menu's keys on in its place (RejectFileName). Waits for keys as a rule (ADR-015).
[[nodiscard]] bool PromptCommanderFileName(GameState& _state, Hardware& _hardware);

/// CriticalErrorInterrupt (CS:02F0), the int 24h handler while PerformDiskRequest or SaveScreenshot works: diskError=1.
/// Returns 0, the answer that tells DOS to ignore the error.
[[nodiscard]] std::uint8_t CriticalErrorInterrupt(GameState& _state);

/// PerformDiskRequest (CS:02FF): DOS's disk transfer area set to diskTransferArea, then _request 1 loads commanderFileName
/// into commanderBlock, 2 saves it, 3 deletes it, and anything else lists the *.cdr files with no attribute beyond
/// _attributes' hidden, system and directory bits into commanderFileList. diskError=1 on a failure, but for DOS's having
/// no more files.
void PerformDiskRequest(GameState& _state, Hardware& _hardware, std::uint8_t _request, std::uint16_t _attributes);

/// ShowDiskError (CS:0470): diskError cleared and diskErrorText shown: its characters over the text page's when
/// screenLayout says the text page shows, and drawn in colour 3 on the graphics screen otherwise.
void ShowDiskError(GameState& _state);

/// PrintCommanderCatalogue (CS:68CE): ten text rows blanked, and commanderFileList's commanderFileCount names printed in
/// columns of ten.
void PrintCommanderCatalogue(GameState& _state);

/// SaveStartupCommander (CS:4660): commanderFileBytes of commanderBlock copied to startupCommander, a byte at a time,
/// going up, or down from each start when _backward (REP MOVSB with the direction flag set).
void SaveStartupCommander(GameState& _state, bool _backward);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void CriticalErrorInterruptEntry(Guest& _guest); ///< Out: AL=0, AH the data segment's high byte.

void PerformDiskRequestEntry(Guest& _guest); ///< In: AL=the request, CX=the attributes a listing searches with. AX, BX, CX, DX,
                                             ///< SI and DI clobbered.

void ShowDiskErrorEntry(Guest& _guest); ///< Out: ES=B800h; AX, BX, CX, SI, DI clobbered.

void PrintCommanderCatalogueEntry(Guest& _guest); ///< Out: ES=B800h; every general register clobbered.

/// Out: AX the F-key that ends it, or after LeaveGameLoopForDisk's pops the return address it took last; ES=B800h; the other
/// general registers clobbered.
void ShowDiscControlScreenEntry(Guest& _guest);

/// Out: CF=0 for a good name. A bad name drops the return address and reads the menu's keys on, as ShowDiscControlScreenEntry.
void PromptCommanderFileNameEntry(Guest& _guest);

void SaveStartupCommanderEntry(Guest& _guest); ///< AX, CX, SI, DI and ES clobbered.

} // namespace Elite
