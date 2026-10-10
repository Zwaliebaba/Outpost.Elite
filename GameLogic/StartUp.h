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
