// GameLogic/Timer.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's timer routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the 1 kHz tick and what it drives. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014). The timer's interrupt handler
// also has its entry, at the end, which int 8 reaches (ADR-010).

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> TimerEntries() noexcept;

// ── The routines de-assembled (ADR-012), on the GameState and the devices (ADR-014) ──

/// TimerInterrupt (CS:0215): int 8's handler, without the IRET: TimerTick, then the end of the interrupt; on the Amstrad, every
/// 18th tick, or every 55th with its mouse driver's rate, the BIOS's handler too (biosTimerChainCountdown).
void TimerInterrupt(GameState& _state, Hardware& _hardware);

/// InstallTimerInterrupt (CS:00C6): the PIT at about 1 kHz, port 61h saved and the speaker's gate and data cleared,
/// TimerInterrupt installed on int 8 in place of the BIOS's handler, which the original keeps in its code segment
/// (savedTimerVector) and native code takes from the BIOS, and interrupts enabled.
void InstallTimerInterrupt(GameState& _state, Hardware& _hardware);

/// RestoreTimerInterrupt (CS:016B): with interrupts off, the BIOS's handler put back on int 8, port 61h
/// as InstallTimerInterrupt found it, and the PIT's tick as the BIOS has it (on the Amstrad, as its mouse driver does); then
/// interrupts on, and the BIOS clock set back by a day when it is past one. Returns the clock as int 1Ah gave it.
BiosClock RestoreTimerInterrupt(GameState& _state, Hardware& _hardware);

/// What RestoreTimerInterrupt leaves in CX: the high word of its SUB DX / SBB CX, _clock less a day, whether or not it set the
/// clock back. Start hands it to PerformDiskRequest, whose catalogue searches with it as the attributes (ADR-012 item 14).
[[nodiscard]] std::uint16_t ClockLessADayHigh(const BiosClock& _clock) noexcept;

/// WaitForTimerTick (CS:7772): until timerTicks changes, at most one tick. Waits as a rule (ADR-015).
void WaitForTimerTick(GameState& _state, Hardware& _hardware);

/// TimerTick (CS:7150): one tick's counters and the sound.
void TimerTick(GameState& _state, Hardware& _hardware);

// ── The entry of the timer's interrupt handler ──

void TimerInterruptEntry(Guest& _guest); ///< Preserves every register; the hook's IRET takes the flags.

} // namespace Elite
