// GameLogic/StartUp.h
#pragma once

#include "GameState.h"
#include "Host.h"
#include "SpaceObject.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

// The reference's start-up routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): start-up and the way out to DOS. The copy
// protection, which D5 closes (ADR-001), is not ported. Each takes values and gives values back, on the GameState and, where it drives a device, the
// devices (ADR-014). Start is the program: the host runs it (Game).

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState and the devices (ADR-014) ──

/// Start, the program's entry from DOS: the start-up commander kept (SaveStartupCommander); then
/// from RestartPlay the timer, int 0 and int 9, the cheat argument and GameLoop, which comes back only for a disk request
/// (HandleDiskRequest): the interrupts put back, and the request made with the game's own critical-error handler on int 24h, after which it
/// starts again. Returns when the program ends, once a request to leave has wiped the program: a quit (D80). Waits
/// in GameLoop.
[[nodiscard]] Task<void> Start(GameState& _state, Host& _host);

/// GameLoop: RunTitleAndDocked, then RunFlight, and the title again after a death; for ever, but that the disc menu
/// leaves it for the disk (LeaveGameLoopForDisk), whose return addresses the original drops so that a RET returns into Start.
/// Waits as a rule (ADR-015).
[[nodiscard]] Task<void> GameLoop(GameState& _state, Host& _host);

/// InstallDivideAndKeyboardInterrupts: the keys to the game's handler (Host::SetKeysToGame, D91), where the original
/// installed its int 9 handler, and ResetKeyboard.
void InstallDivideAndKeyboardInterrupts(GameState& _state, Host& _host);

/// RestoreDivideAndKeyboardInterrupts: the keys no longer the game's, where the original put the BIOS's int 9
/// handler back.
void RestoreDivideAndKeyboardInterrupts(Host& _host);

/// CheckCheatArgument: cheatEnabled=1 when the game was started with the cheat on (Host::CheatRequested, D80), 0
/// otherwise.
void CheckCheatArgument(GameState& _state, const Host& _host);

/// StartNewGame: the start-up commander copied back over the commander, a byte at a time, going up as REP MOVSB does (the
/// direction flag is clear wherever the game calls it, change 3c), and the per-game state reset.
void StartNewGame(GameState& _state);

/// ShowCredits: the space view's buffer cleared (ClearDrawBuffer), the
/// nine lines of creditsScreenText drawn in it, presented (FinishSpaceViewFrame), then 3000 timer ticks, about 3 s. Waits.
[[nodiscard]] Task<void> ShowCredits(GameState& _state, Host& _host);

} // namespace Elite
