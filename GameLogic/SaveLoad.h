// GameLogic/SaveLoad.h
#pragma once

#include "GameState.h"
#include "Host.h"

#include <cstdint>

namespace Elite
{

// The reference's save-load routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): saving and loading commanders. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// How the disc menu ends.
struct DiscMenuExit
{
  bool leaves;           ///< through LeaveGameLoopForDisk, for a disk operation or to leave for DOS: the original drops the
                         ///< return addresses of the menu's call and RunTitleAndDocked's, so that it returns from GameLoop
                         ///< to Start
  std::uint8_t scanCode; ///< otherwise the F-key that ends it
};

/// ShowDiscControlScreen, with RunDiscControlKeys, ShowDiskResult and PromptCommanderFileName, which
/// jumps into its key loop after a bad name, as one unit (ADR-012 item 14): the Disc/Control menu, or, back from Start's disk
/// work, what it came to or the question whether to retry it; then its keys. Waits for keys as a rule (ADR-015).
[[nodiscard]] Task<DiscMenuExit> ShowDiscControlScreen(GameState& _state, Host& _host);

/// PromptCommanderFileName: FILENAME? and up to 8 characters through ReadTextLine into commanderFileName, folded to
/// upper case, and ".CDR" after them. Returns false for a bad name, which the original answers by dropping its return address
/// and reading the menu's keys on in its place (RejectFileName). Waits for keys as a rule (ADR-015).
[[nodiscard]] Task<bool> PromptCommanderFileName(GameState& _state, Host& _host);

/// PerformDiskRequest: _request 1 loads commanderFileName into commanderBlock, 2 saves it, 3 deletes it, and anything
/// else lists the .CDR files with no attribute into commanderFileList, each through the platform's FileStore (D78).
/// diskError=1 on a failure.
void PerformDiskRequest(GameState& _state, Host& _host, std::uint8_t _request);

/// PrintCommanderCatalogue: ten text rows blanked, and commanderFileList's commanderFileCount names printed in
/// columns of ten.
void PrintCommanderCatalogue(GameState& _state);

/// SaveStartupCommander: commanderFileBytes of commanderBlock copied to startupCommander, a byte at a time, going up as
/// the original's copy does: nothing leaves it running downward where the game calls it (change 3c).
void SaveStartupCommander(GameState& _state);

} // namespace Elite
