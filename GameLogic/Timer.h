// GameLogic/Timer.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's timer routines, ported (plan §5 Phase 3, ADR-010): the 1 kHz tick and what it drives. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> TimerEntries() noexcept;

/// RestoreTimerInterrupt (CS:016B): int 8, port 61h and the PIT as they were, and the BIOS clock kept within a day.
/// Out: ES=0, or the int 33h segment on the Amstrad; AX, BX, CX and DX clobbered.
void RestoreTimerInterrupt(Guest& _guest);

/// TimerInterrupt (CS:0215): int 8's handler, without the IRET: TimerTick, then the end of the interrupt.
void TimerInterrupt(Guest& _guest);

/// WaitForTimerTick (CS:7772): until timerTicks changes, at most one tick. Waits as a rule. Preserves every register.
void WaitForTimerTick(Guest& _guest);

// ── The routines de-assembled (ADR-012), on the GameState and the devices (ADR-014) ──

/// InstallTimerInterrupt (CS:00C6): the PIT at about 1 kHz, port 61h saved and the speaker's gate and data cleared,
/// TimerInterrupt on int 8, and interrupts enabled.
void InstallTimerInterrupt(GameState& _state, Hardware& _hardware);

/// TimerTick (CS:7150): one tick's counters, the protection's answer check, and the sound. _backward is the direction
/// flag the interrupted code left, which the answer check compares with.
void TimerTick(GameState& _state, Hardware& _hardware, bool _backward);

// ── Their entries ──

void InstallTimerInterruptEntry(Guest& _guest); ///< Out: ES=0; AX clobbered.
void TimerTickEntry(Guest& _guest);             ///< AX clobbered.

} // namespace Elite
