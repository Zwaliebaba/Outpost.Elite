// GameLogic/Timer.h
#pragma once

#include "GameState.h"
#include "Host.h"

#include <span>

namespace Elite
{

// The reference's timer routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the 1 kHz tick and what it drives. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014). The game installs
// TimerInterrupt as its tick's handler (Game).

// ── The routines de-assembled (ADR-012), on the GameState and the devices (ADR-014) ──

/// TimerInterrupt (CS:0215): int 8's handler, without the IRET: TimerTick, then the end of the interrupt. The original's chain
/// to the BIOS's handler on an Amstrad (biosTimerChainCountdown) is not taken: the game runs as on an IBM PC (D12).
void TimerInterrupt(GameState& _state, Host& _host);

/// InstallTimerInterrupt (CS:00C6): the PIT at about 1 kHz, port 61h saved and the speaker's gate and data cleared,
/// TimerInterrupt installed on int 8 in place of the BIOS's handler, which the original keeps in its code segment
/// (savedTimerVector) and native code takes from the BIOS, and interrupts enabled.
void InstallTimerInterrupt(GameState& _state, Host& _host);

/// RestoreTimerInterrupt (CS:016B): with interrupts off, the BIOS's handler put back on int 8, port 61h as
/// InstallTimerInterrupt found it, and the PIT's tick as the BIOS has it; then interrupts on. The original's Amstrad rate is not
/// taken (D12); its reading of the clock by int 1Ah, which only its catalogue's search attributes took (ADR-012 item 14), went
/// with the files' DOS (ADR-049).
void RestoreTimerInterrupt(GameState& _state, Host& _host);

/// WaitForTimerTick (CS:7772): until timerTicks changes, at most one tick. Waits as a rule (ADR-015).
[[nodiscard]] Task<void> WaitForTimerTick(GameState& _state, Host& _host);

/// TimerTick (CS:7150): one tick's counters and the sound.
void TimerTick(GameState& _state, Host& _host);

} // namespace Elite
