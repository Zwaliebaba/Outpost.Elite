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

/// Start (CS:0000), the program's entry from DOS (DS=ES=PSP): the DOS version, the start-up commander kept,
/// the Amstrad's ROM looked for; then from RestartPlay the timer, the protection, int 0 and int 9, the cheat
/// argument and GameLoop, which comes back only for a disk request (HandleDiskRequest), after which it
/// starts again. A request to leave wipes the program and exits to DOS (ExitToDos) through the far return
/// to PSP:0000 it pushed first. Waits in GameLoop.
void Start(Guest& _guest);

/// WipeProgram (CS:0554): CS:0564-8F30 filled with AL, the code segment's low byte, under CLI. Out: IF=0;
/// AX=ES=CS, CX=0 and DI=8F31 (forwards).
void WipeProgram(Guest& _guest);

/// GameLoop (CS:7D30): RunTitleAndDocked, then RunFlight, and the title again after a death; for ever.
/// Left only by LeaveGameLoopForDisk, which returns past it into Start.
void GameLoop(Guest& _guest);

/// ShowCredits (CS:8F02): the nine lines of creditsScreenText in the view, presented, then 3000 timer ticks
/// (3 s). Clobbers all but DS.
void ShowCredits(Guest& _guest);

/// InstallDivideAndKeyboardInterrupts (CS:0105): int 0 and int 9 saved and replaced by the game's, int 8's segment
/// rewritten, then ResetKeyboard. In: ES=0. Out: ES=B800; AX clobbered.
void InstallDivideAndKeyboardInterrupts(Guest& _guest);

/// RestoreDivideAndKeyboardInterrupts (CS:0148): int 9 and int 0 put back. Out: ES=0; AX clobbered.
void RestoreDivideAndKeyboardInterrupts(Guest& _guest);

/// CheckCheatArgument (CS:02A5): cheatEnabled=1 when the command tail is exactly ' cheat'. AX, BX, CX and SI
/// clobbered.
void CheckCheatArgument(Guest& _guest);

/// CopyProtection (CS:04A3): returns at once while protectionShown is set; otherwise asks a question from the manual.
/// Clobbers all.
void CopyProtection(Guest& _guest);

/// StartNewGame (CS:4671): the start-up commander copied back, and the per-game state reset. Out: ES=DS; AX, CX, SI
/// and DI clobbered.
void StartNewGame(Guest& _guest);

} // namespace Elite
