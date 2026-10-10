// GameLogic/Docked.h
#pragma once

#include "Input.h"
#include "NativeEntry.h"
#include "Text.h"

#include <cstdint>
#include <optional>
#include <span>

namespace Elite
{

// The reference's docked routines, ported (plan §5 Phase 3, ADR-010): the docked screens and their menus. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv. The routines de-assembled so far (ADR-012) take values
// and give values back, and their entries, at the end, keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> DockedEntries() noexcept;

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// The key that closed a docked screen, and AL as the original leaves it with the key in AH.
struct ScreenKey
{
  std::uint8_t scanCode; ///< Esc, or the F-key of the screen to show next
  std::uint8_t al;       ///< what the screen left in AL, shifted left by each GetKey that took a code, its Shift in bit 0
  /// BP as the screen leaves it when it selects the system at the cursor as it closes (SelectSystemAtCursor), as the screens
  /// shown next read it; a screen that does not leaves BP as it found it.
  std::optional<std::uint16_t> countLeft;
};

/// AL as GetKey leaves it, from _al, for _key: shifted left with the key's Shift in bit 0 when it took a code, and as it was
/// otherwise. The docked menus hold AL from one turn of their loops to the next, and fire the stick with it (ReadSteering).
[[nodiscard]] std::uint8_t AlAfterKey(std::uint8_t _al, const KeyPress& _key) noexcept;

/// AwardArchangelTitle (CS:49E4): 'ARCHANGEL' over the rank in commanderRankText.
void AwardArchangelTitle(GameState& _state);

/// WaitForScreenExitKey (CS:60B4), the tail that ShowSystemDataScreen, ShowMarketPricesScreen, ShowCommanderStatusScreen and
/// ShowInventoryScreen jump into: GetKey until Esc, or an F-key other than the screen's own, _ownKey, then SelectSystemAtCursor
/// with _countIfNone. _al is what the screen left in AL. Waits for keys as a rule (ADR-015).
ScreenKey WaitForScreenExitKey(GameState& _state, Hardware& _hardware, std::uint8_t _ownKey, std::uint8_t _al, std::uint16_t _countIfNone);

/// ShowSellCargoScreen (CS:5A30), F2, with PrintNameAndPrice, PrintUnit and EndTradeRow as one unit (ADR-012 item 14): the
/// products with their sell prices and the units held, then RunCargoTradeMenu, which returns the key that closes it. _backward
/// is the direction flag, which DrawDockedFrame goes by. Waits for keys as a rule (ADR-015).
ScreenKey ShowSellCargoScreen(GameState& _state, Hardware& _hardware, bool _backward);

/// ShowBuyCargoScreen (CS:5AE9), F3, as ShowSellCargoScreen: the products with their buy prices and the quantities on sale,
/// drawn from the market's random state the first time after an arrival, then RunCargoTradeMenu.
ScreenKey ShowBuyCargoScreen(GameState& _state, Hardware& _hardware, bool _backward);

/// ShowCommanderStatusScreen (CS:5EA9), F9, docked or in flight: docked, a mission's briefing or debriefing first; then the
/// commander, systems, fuel, cash, legal status, rating and equipment, and the wait for the key that closes it
/// (WaitForScreenExitKey, with _countIfNone). _backward is the direction flag, which DrawDockedFrame goes by.
ScreenKey ShowCommanderStatusScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone);

/// ShowInventoryScreen (CS:6020), F10: fuel, cash and every product held, and the wait for the key that closes it, as
/// ShowCommanderStatusScreen's.
ScreenKey ShowInventoryScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone);

/// ShowMissionBriefing (CS:6DF2): missionStage=1, and the EMERGENCY screen of missionNumber, which waits for a key (Y or N
/// for the supernova's refugees, who take the hold).
void ShowMissionBriefing(GameState& _state, Hardware& _hardware, bool _backward);

/// ShowMissionDebriefing (CS:6EB7): nothing while the mission is not done; otherwise the TASK COMPLETE screen, which waits
/// for a key, and the reward.
void ShowMissionDebriefing(GameState& _state, Hardware& _hardware, bool _backward);

/// PrintCreditsOnMessageLine (CS:658F): creditBalanceText at B800:0078 in the swapped textAttribute, which is swapped
/// back after.
PrintedText PrintCreditsOnMessageLine(GameState& _state);

/// FormatFuelLightYears (CS:6923): fuel as "n.n" light years into the fuel text at DS:83E9.
void FormatFuelLightYears(GameState& _state);

/// DrawDockedFrame (CS:7C88): a docked screen's text page from the descriptor at DS:_descriptor, the screen's attribute,
/// the frame's, then the title: the text mode set unless screenLayout says the text page shows, textAttribute the screen's
/// attribute without its blink bit and the border its background, the page cleared, the frame's rows at lines 0, 2 and 24
/// and its sides in the frame's attribute, and its six corners and tees from frameCorners. _backward is the direction flag,
/// which the clear and the rows' REP STOSW go by. Returns where the title is.
std::uint16_t DrawDockedFrame(GameState& _state, Hardware& _hardware, std::uint16_t _descriptor, bool _backward);

/// DrawFrameSides (CS:7CE9): BAh in attribute _attribute at columns 0 and 39 of _rows rows of the text page at
/// _segment from _cell, 65,536 for 0, as LOOP counts. Returns the cell a row below the last.
std::uint16_t DrawFrameSides(GameState& _state, std::uint16_t _segment, std::uint16_t _cell, std::uint16_t _rows, std::uint8_t _attribute);

/// DrawFrameRow (CS:7CF8): the character and attribute _value in 38 cells from _segment:_cell, forwards or,
/// _backwards, down. Returns the cell after the last, as REP STOSW leaves it.
std::uint16_t DrawFrameRow(GameState& _state, std::uint16_t _segment, std::uint16_t _cell, std::uint16_t _value, bool _backwards);

/// How the docked screens end (DockedKeyDispatch, CS:0B40), and with them RunTitleAndDocked.
struct DockedExit
{
  bool leaves;             ///< the disc menu left through LeaveGameLoopForDisk, which returns past RunTitleAndDocked (DiscMenuExit)
  ScreenKey key;           ///< otherwise F1, with AL as the screen and the waits for a key leave it
  std::uint16_t countLeft; ///< BP, as the screens leave it
  bool backward;           ///< the direction flag, as they leave it
};

/// RunTitleAndDocked (CS:7D81): unless titleShown, the title (RunTitle, CS:7D8B) until a key, the credits and a new game; then the
/// docked screens (DockedKeyDispatch, CS:0B40) from the status screen, or from the disc menu after a disk request, until F1 or the
/// disc menu leaves for the disk. _countIfNone is BP and _backward the direction flag as GameLoop leaves them, which the screens
/// carry from one to the next; after the title they are its 20h, PresentSpaceView's, and clear. Waits as a rule (ADR-015).
DockedExit RunTitleAndDocked(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes its
// results back there. The registers the contract leaves to the routine it hands to Guest::Clobber.

void AwardArchangelTitleEntry(Guest& _guest);
void PrintCreditsOnMessageLineEntry(Guest& _guest); ///< In: ES=B800, which it sets again.
void FormatFuelLightYearsEntry(Guest& _guest);      ///< Out: SI=DS:83E9, the fuel text; AX, BX and DI clobbered.
void DrawDockedFrameEntry(Guest& _guest);           ///< In: SI=the descriptor. Out: SI=the title, ES=B800h.
void DrawFrameSidesEntry(Guest& _guest);
void DrawFrameRowEntry(Guest& _guest);
/// In: BP and DF, carried to the screens. Out: ES=B800h, BP and DF as the screens leave them, AX the F1 that ends it; or, once the
/// disc menu leaves for the disk, its own return address popped, so that its RET returns from GameLoop. All but DS clobbered.
void RunTitleAndDockedEntry(Guest& _guest);
void ShowSellCargoScreenEntry(Guest& _guest);       ///< Out: AX the key; all but DS clobbered.
void ShowBuyCargoScreenEntry(Guest& _guest);        ///< Out: AX the key; all but DS clobbered.
void ShowCommanderStatusScreenEntry(Guest& _guest); ///< In: BP, as SelectSystemAtCursorEntry. Out: AX the key; all but DS clobbered.
void ShowInventoryScreenEntry(Guest& _guest);       ///< As ShowCommanderStatusScreenEntry.
void ShowMissionBriefingEntry(Guest& _guest);       ///< Clobbers all but DS.
void ShowMissionDebriefingEntry(Guest& _guest);     ///< Clobbers all but DS.

} // namespace Elite
