// GameLogic/Timer.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's timer routines, ported (plan §5 Phase 3, ADR-010): the 1 kHz tick and what it drives. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> TimerEntries() noexcept;

/// InstallTimerInterrupt (CS:00C6): the PIT at about 1 kHz, the speaker port saved, and TimerInterrupt on int 8. Out:
/// ES=0; AX clobbered.
void InstallTimerInterrupt(Guest& _guest);

/// RestoreTimerInterrupt (CS:016B): int 8, port 61h and the PIT as they were, and the BIOS clock kept within a day.
/// Out: ES=0, or the int 33h segment on the Amstrad; AX, BX, CX and DX clobbered.
void RestoreTimerInterrupt(Guest& _guest);

/// TimerInterrupt (CS:0215): int 8's handler, without the IRET: TimerTick, then the end of the interrupt.
void TimerInterrupt(Guest& _guest);

/// TimerTick (CS:7150): one tick's counters, the protection's answer check, and the sound. AX clobbered.
void TimerTick(Guest& _guest);

/// WaitForTimerTick (CS:7772): until timerTicks changes, at most one tick. Waits as a rule. Preserves every register.
void WaitForTimerTick(Guest& _guest);

} // namespace Elite
