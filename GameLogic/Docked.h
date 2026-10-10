// GameLogic/Docked.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's docked routines, ported (plan §5 Phase 3, ADR-010): the docked screens and their menus. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> DockedEntries() noexcept;

/// PrintCreditsOnMessageLine (CS:658F): creditBalanceText at B800:0078 in the swapped textAttribute. In: ES=B800.
void PrintCreditsOnMessageLine(Guest& _guest);

/// FormatFuelLightYears (CS:6923): fuel as "n.n" light years into the fuel text. Out: SI=DS:83E9; AX, BX and DI
/// clobbered.
void FormatFuelLightYears(Guest& _guest);

/// DrawDockedFrame (CS:7C88): a docked screen's text page, border and frame, from the descriptor at SI. Out: SI=the
/// title text, ES=B800.
void DrawDockedFrame(Guest& _guest);

/// DrawFrameSides (CS:7CE9): BAh at columns 0 and 39 of CX rows from ES:DI, in attribute AH.
void DrawFrameSides(Guest& _guest);

/// DrawFrameRow (CS:7CF8): AX in 38 cells from ES:DI.
void DrawFrameRow(Guest& _guest);

} // namespace Elite
