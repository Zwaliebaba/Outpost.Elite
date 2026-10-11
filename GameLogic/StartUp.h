// GameLogic/StartUp.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's start-up and protection routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): start-up, the copy
// protection's remains and the way out to DOS. Each takes values and gives values back, on the GameState and, where it drives a device, the
// devices (ADR-014). Start also has its entry, at the end, where DOS starts the program (ADR-010).

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> StartUpEntries() noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState and the devices (ADR-014) ──

/// Start (CS:0000), the program's entry from DOS, _psp its program segment prefix: the DOS version (DOS 1 is told it
/// is not enough), the start-up commander kept (SaveStartupCommander, by _backward, the direction flag DOS left), the Amstrad's ROM
/// looked for; then from RestartPlay the DOS clock read twice, the timer, the protection, int 0 and int 9, the cheat argument and
/// GameLoop, which comes back only for a disk request (HandleDiskRequest): the interrupts put back, and the request made with the
/// game's own critical-error handler on int 24h, after which it starts again. _countIfNone is BP as DOS left it, which GameLoop
/// takes. Returns when the program ends: on DOS 1, or once a request to leave has wiped the program and said farewell (ExitToDos).
/// Waits in GameLoop.
void Start(GameState& _state, Hardware& _hardware, std::uint16_t _psp, std::uint16_t _countIfNone, bool _backward);

/// What GameLoop leaves for Start when the disc menu leaves it for the disk: BP and the direction flag as the docked screens left
/// them, which Start hands back to it after the request.
struct GameLoopExit
{
  std::uint16_t countLeft; ///< BP
  bool backward;           ///< the direction flag
};

/// GameLoop (CS:7D30): RunTitleAndDocked, then RunFlight, and the title again after a death; for ever, but that the disc menu
/// leaves it for the disk (LeaveGameLoopForDisk), whose return addresses the original drops so that a RET returns into Start.
/// _countIfNone and _backward are BP and the direction flag it is called with, which RunTitleAndDocked takes, and RunFlight
/// leaves for it the next time round. Waits as a rule (ADR-015).
GameLoopExit GameLoop(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward);

/// InstallDivideAndKeyboardInterrupts (CS:0105): with interrupts off, the game's int 0 and int 9 handlers installed in the
/// BIOS's place, int 8's segment rewritten; then interrupts on, and ResetKeyboard. The original first keeps the BIOS's two
/// in the data segment, which native code does not need (RestoreDivideAndKeyboardInterrupts).
void InstallDivideAndKeyboardInterrupts(GameState& _state, Hardware& _hardware);

/// RestoreDivideAndKeyboardInterrupts (CS:0148): with interrupts off, the BIOS's int 9 and int 0 handlers put back
/// (Hardware::BiosInterruptVector), where the original puts back what InstallDivideAndKeyboardInterrupts kept; then interrupts
/// on.
void RestoreDivideAndKeyboardInterrupts(Hardware& _hardware);

/// CopyProtection (CS:04A3): nothing while protectionShown is set, as the D5 byte makes it; otherwise, once, a question from
/// the manual: the BIOS's 40-column text, the title, a question picked by how long NextRandom runs before a vertical retrace,
/// its page, line and word decoded into the prompt, the answer read by DOS and upper-cased, and a timer tick. Waits then.
void CopyProtection(GameState& _state, Hardware& _hardware);

/// CheckCheatArgument (CS:02A5): cheatEnabled=1 when the command tail DOS gave the program is exactly ' cheat', 0 otherwise.
void CheckCheatArgument(GameState& _state, const Hardware& _hardware);

/// WipeProgram (CS:0554): interrupts off. The original then fills CS:0564-8F30 with the code segment's low byte, which nothing
/// reads: the code segment is not the game's memory.
void WipeProgram(Hardware& _hardware);

/// StartNewGame (CS:4671): the start-up commander copied back over the commander, a byte at a time, going up or, when
/// _backward, down (REP MOVSB), and the per-game state reset.
void StartNewGame(GameState& _state, bool _backward);

/// ShowCredits (CS:8F02): the space view's buffer cleared (ClearDrawBuffer, which goes by _backward, the direction flag), the
/// nine lines of creditsScreenText drawn in it, presented (FinishSpaceViewFrame), then 3000 timer ticks, about 3 s. Waits.
void ShowCredits(GameState& _state, Hardware& _hardware, bool _backward);

// ── The entry of the program ──

/// Start, with the PSP's segment, BP and the direction flag as DOS leaves them. The program ends when it returns. Waits.
void StartEntry(Guest& _guest);

} // namespace Elite
