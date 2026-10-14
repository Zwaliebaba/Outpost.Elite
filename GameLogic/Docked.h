// GameLogic/Docked.h
#pragma once

#include "GameState.h"
#include "Host.h"
#include "Input.h"
#include "Text.h"

#include <cstdint>
#include <optional>

namespace Elite
{

// The reference's docked routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the docked screens and their menus. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the 1987 routine of its name computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// The key that closed a docked screen.
struct ScreenKey
{
  std::uint8_t scanCode; ///< Esc, or the F-key of the screen to show next
};

/// AwardArchangelTitle: 'ARCHANGEL' over the rank in commanderRankText.
void AwardArchangelTitle(GameState& _state);

/// WaitForScreenExitKey, the tail that ShowSystemDataScreen, ShowMarketPricesScreen, ShowCommanderStatusScreen and
/// ShowInventoryScreen jump into: GetKey until Esc, or an F-key other than the screen's own, _ownKey, then SelectSystemAtCursor.
/// Waits for keys as a rule (ADR-015).
[[nodiscard]] Task<ScreenKey> WaitForScreenExitKey(GameState& _state, Host& _host, std::uint8_t _ownKey);

/// ShowSellCargoScreen, F2, with PrintNameAndPrice, PrintUnit and EndTradeRow as one unit (ADR-012 item 14): the
/// products with their sell prices and the units held, then RunCargoTradeMenu, which returns the key that closes it. Waits for
/// keys as a rule (ADR-015).
[[nodiscard]] Task<ScreenKey> ShowSellCargoScreen(GameState& _state, Host& _host);

/// ShowBuyCargoScreen, F3, as ShowSellCargoScreen: the products with their buy prices and the quantities on sale,
/// drawn from the market's random state the first time after an arrival, then RunCargoTradeMenu.
[[nodiscard]] Task<ScreenKey> ShowBuyCargoScreen(GameState& _state, Host& _host);

/// ShowCommanderStatusScreen, F9, docked or in flight: docked, a mission's briefing or debriefing first; then the
/// commander, systems, fuel, cash, legal status, rating and equipment, and the wait for the key that closes it
/// (WaitForScreenExitKey).
[[nodiscard]] Task<ScreenKey> ShowCommanderStatusScreen(GameState& _state, Host& _host);

/// ShowInventoryScreen, F10: fuel, cash and every product held, and the wait for the key that closes it, as
/// ShowCommanderStatusScreen's.
[[nodiscard]] Task<ScreenKey> ShowInventoryScreen(GameState& _state, Host& _host);

/// ShowMissionBriefing: missionStage=1, and the EMERGENCY screen of missionNumber, which waits for a key (Y or N
/// for the supernova's refugees, who take the hold).
[[nodiscard]] Task<void> ShowMissionBriefing(GameState& _state, Host& _host);

/// ShowMissionDebriefing: nothing while the mission is not done; otherwise the TASK COMPLETE screen, which waits
/// for a key, and the reward.
[[nodiscard]] Task<void> ShowMissionDebriefing(GameState& _state, Host& _host);

/// PrintCreditsOnMessageLine: creditBalanceText at B800:0078 in the swapped textAttribute, which is swapped
/// back after.
void PrintCreditsOnMessageLine(GameState& _state);

/// FormatFuelLightYears: fuel as "n.n" light years into the fuel text.
void FormatFuelLightYears(GameState& _state);

/// DrawDockedFrame, up to its title: a docked screen's text page from the first two bytes of its descriptor, the
/// screen's attribute _screen and the frame's, _frame: the text mode set unless screenLayout says the text page shows,
/// textAttribute the screen's attribute without its blink bit and the border its background, the page cleared, the frame's rows
/// at lines 0, 2 and 24 and its sides in the frame's attribute, and its six corners and tees from frameCorners. The clear and the
/// rows' REP STOSW run forwards.
void DrawDockedFrameAt(GameState& _state, Host& _host, std::uint8_t _screen, std::uint8_t _frame);

/// DrawDockedFrame: DrawDockedFrameAt from _descriptor, the bytes of GameData from where a frame's descriptor starts to the end
/// of the member that holds it: the two attributes, then the title. The symbol table sizes a descriptor as it pleases, two bytes or the
/// whole screen's text, so the caller passes the bytes its title is within. Returns the title.
inline std::string_view DrawDockedFrame(GameState& _state, Host& _host, std::span<const std::uint8_t> _descriptor)
{
  DrawDockedFrameAt(_state, _host, ByteWithin(_descriptor, 0), ByteWithin(_descriptor, 1));
  return TextWithin(_descriptor, 2);
}

/// DrawDockedFrame for a frame's descriptor that is a constant of the data segment (D38), its two attributes and then
/// its title, a table of the subsystem's: DrawDockedFrameAt from the attributes. Returns the title.
std::string_view DrawDockedFrame(GameState& _state, Host& _host, std::string_view _descriptor);

/// DrawFrameSides: BAh in attribute _attribute at columns 0 and 39 of _rows rows of the text page in video memory from _cell,
/// 65,536 for 0, as LOOP counts. The original takes the page's segment in ES, which is the video segment wherever it is called.
void DrawFrameSides(GameState& _state, std::uint16_t _cell, std::uint16_t _rows, std::uint8_t _attribute);

/// DrawFrameRow: the character and attribute _value in 38 cells of video memory from _cell,.
void DrawFrameRow(GameState& _state, std::uint16_t _cell, std::uint16_t _value);

/// RunTitleAndDocked: unless titleShown, the title (RunTitle) until a key, the credits and a new game; then the
/// docked screens (DockedKeyDispatch) from the status screen, or from the disc menu after a disk request, until F1 or the
/// disc menu leaves for the disk. Returns whether the disc menu left for the disk, through LeaveGameLoopForDisk (DiscMenuExit);
/// otherwise F1 ended them. Waits as a rule (ADR-015).
[[nodiscard]] Task<bool> RunTitleAndDocked(GameState& _state, Host& _host);

} // namespace Elite
