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

/// TimerInterrupt (CS:0215): the game's tick work, which the host runs at each tick (D91): TimerTick. The original's chain
/// to the BIOS's handler on an Amstrad (biosTimerChainCountdown) is not taken: the game runs as on an IBM PC (D12).
void TimerInterrupt(GameState& _state, Host& _host);

/// InstallTimerInterrupt (CS:00C6): the tick started, every millisecond (Host::SetTickRunning, D87), and the speaker's bits
/// saved and cleared, where the original programmed the PIT to about 1 kHz and installed TimerInterrupt on int 8.
void InstallTimerInterrupt(GameState& _state, Host& _host);

/// RestoreTimerInterrupt (CS:016B): the speaker's bits as InstallTimerInterrupt found them, and the tick stopped, where the
/// original put the BIOS's int 8 handler and tick back. The original's Amstrad rate is not
/// taken (D12); its reading of the clock by int 1Ah, which only its catalogue's search attributes took (ADR-012 item 14), went
/// with the files' DOS (ADR-049).
void RestoreTimerInterrupt(GameState& _state, Host& _host);

/// WaitForTimerTick (CS:7772): until timerTicks changes, at most one tick. Waits as a rule (ADR-015).
[[nodiscard]] Task<void> WaitForTimerTick(GameState& _state, Host& _host);

/// TimerTick (CS:7150): one tick's counters and the sound.
void TimerTick(GameState& _state, Host& _host);

} // namespace Elite
