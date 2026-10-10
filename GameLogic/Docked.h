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

/// WaitForScreenExitKey (CS:60B4), the tail that ShowSystemDataScreen, ShowMarketPricesScreen,
/// ShowCommanderStatusScreen and ShowInventoryScreen jump into: GetKey until Esc, or an F-key other than the
/// screen's own in DL, then SelectSystemAtCursor. Waits. Out: AH=the key; what SelectSystemAtCursor
/// clobbers but AX.
void WaitForScreenExitKey(Guest& _guest);

/// AwardArchangelTitle (CS:49E4): 'ARCHANGEL' over the rank in commanderRankText. AL, CX, SI and DI clobbered.
void AwardArchangelTitle(Guest& _guest);

/// ShowSellCargoScreen (CS:5A30), F2: the products with their sell prices and the units held, then
/// RunCargoTradeMenu, which waits. Out: AH=the Esc or F-key that closed it; clobbers all but DS.
void ShowSellCargoScreen(Guest& _guest);

/// ShowBuyCargoScreen (CS:5AE9), F3: the products with their buy prices and the quantities on sale, drawn
/// from the market's random state the first time after an arrival, then RunCargoTradeMenu, which waits.
/// Out: AH=the Esc or F-key that closed it; clobbers all but DS.
void ShowBuyCargoScreen(Guest& _guest);

/// ShowCommanderStatusScreen (CS:5EA9), F9, docked or in flight: docked, a mission's briefing or debriefing
/// first; then the commander, systems, fuel, cash, legal status, rating and equipment, and it waits for the
/// key that closes it (WaitForScreenExitKey). Out: AH=that key; clobbers all but DS.
void ShowCommanderStatusScreen(Guest& _guest);

/// ShowInventoryScreen (CS:6020), F10: fuel, cash and every product held, and it waits for the key that
/// closes it. Out: AH=that key; clobbers all but DS.
void ShowInventoryScreen(Guest& _guest);

/// ShowMissionBriefing (CS:6DF2): the EMERGENCY screen of missionNumber, which waits for a key (Y or N for
/// the supernova's refugees). missionStage=1. Clobbers all but DS.
void ShowMissionBriefing(Guest& _guest);

/// ShowMissionDebriefing (CS:6EB7): returns at once while the mission is not done; otherwise the TASK
/// COMPLETE screen, which waits for a key, and the reward. Clobbers all but DS.
void ShowMissionDebriefing(Guest& _guest);

/// RunTitleAndDocked (CS:7D81): unless titleShown, the title until a key, the credits and a new game; then
/// the docked screens (DockedKeyDispatch, CS:0B40) from the status screen, or from the disc menu after a
/// disk request, until F1. Clobbers all but DS.
void RunTitleAndDocked(Guest& _guest);

} // namespace Elite
