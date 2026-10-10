// GameLogic/StartUp.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
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

/// GameLoop (CS:7D30): RunTitleAndDocked, then RunFlight, and the title again after a death; for ever.
/// Left only by LeaveGameLoopForDisk, which returns past it into Start.
void GameLoop(Guest& _guest);

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState and the devices (ADR-014) ──

/// InstallDivideAndKeyboardInterrupts (CS:0105): int 0 and int 9 in the interrupt table at _vectors:0000 saved and,
/// with interrupts off, replaced by the game's, int 8's segment rewritten; then interrupts on, and ResetKeyboard.
void InstallDivideAndKeyboardInterrupts(GameState& _state, Hardware& _hardware, std::uint16_t _vectors);

/// RestoreDivideAndKeyboardInterrupts (CS:0148): with interrupts off, int 9 and int 0 put back in the interrupt table,
/// segment then offset, from what InstallDivideAndKeyboardInterrupts saved; then interrupts on.
void RestoreDivideAndKeyboardInterrupts(GameState& _state, Hardware& _hardware);

/// CopyProtection (CS:04A3): nothing while protectionShown is set, as the D5 byte makes it; otherwise, once, a question from
/// the manual: the BIOS's 40-column text, the title, a question picked by how long NextRandom runs before a vertical retrace,
/// its page, line and word decoded into the prompt, the answer read by DOS and upper-cased, and a timer tick. Waits then.
void CopyProtection(GameState& _state, Hardware& _hardware);

/// CheckCheatArgument (CS:02A5): cheatEnabled=1 when the command tail is exactly ' cheat', 0 otherwise.
void CheckCheatArgument(GameState& _state);

/// WipeProgram (CS:0554): interrupts off, and CS:0564-8F30 filled with the code segment's low byte, a byte at a time,
/// going up from 0564h, or down from it when _backward (REP STOSB with the direction flag set).
void WipeProgram(GameState& _state, Hardware& _hardware, bool _backward);

/// StartNewGame (CS:4671): the start-up commander copied back over the commander, a byte at a time, going up or, when
/// _backward, down (REP MOVSB), and the per-game state reset.
void StartNewGame(GameState& _state, bool _backward);

/// ShowCredits (CS:8F02): the space view's buffer cleared (ClearDrawBuffer, which goes by _backward, the direction flag), the
/// nine lines of creditsScreenText drawn in it, presented (FinishSpaceViewFrame), then 3000 timer ticks, about 3 s. Waits.
void ShowCredits(GameState& _state, Hardware& _hardware, bool _backward);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──

void InstallDivideAndKeyboardInterruptsEntry(Guest& _guest); ///< In: ES=0. Out: ES=B800, IF=1; AX clobbered.
void RestoreDivideAndKeyboardInterruptsEntry(Guest& _guest); ///< Out: ES=0, IF=1; AX clobbered.
void CheckCheatArgumentEntry(Guest& _guest);                 ///< AX, BX, CX and SI clobbered.
void CopyProtectionEntry(Guest& _guest);                     ///< Clobbers all. Waits sometimes.
void WipeProgramEntry(Guest& _guest);                        ///< Out: IF=0; AX, CX, DI and ES clobbered.
void StartNewGameEntry(Guest& _guest);                       ///< Out: ES=DS; AX, CX, SI and DI clobbered.
void ShowCreditsEntry(Guest& _guest);                        ///< Out: DF clear, BP=20h; all but DS clobbered. Waits.

} // namespace Elite
