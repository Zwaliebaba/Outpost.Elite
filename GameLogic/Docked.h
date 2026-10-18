// GameLogic/Docked.h
#pragma once

#include "BriefingView.h"
#include "DockedKeys.h"
#include "GameState.h"
#include "Host.h"
#include "Input.h"
#include "StatusView.h"
#include "Text.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Elite
{

// The reference's docked routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the docked screens and their menus. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the original routine of its name computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// The key that closed a docked screen.
struct ScreenKey
{
  std::uint8_t scanCode; ///< Esc, or the F-key of the screen to show next
};

/// AwardArchangelTitle: 'ARCHANGEL' over the rank in commanderRankText.
void AwardArchangelTitle(GameState& _state);

/// WaitForScreenExitKey, the tail that ShowMarketPricesScreen and ShowCommanderStatusScreen jump into:
/// GetKey until a key that closes a screen at _place (ClosesScreen) other than the screen's own, _ownKey, then
/// SelectSystemAtCursor. Waits for keys as a rule (ADR-015).
[[nodiscard]] Task<ScreenKey> WaitForScreenExitKey(GameState& _state, Host& _host, std::uint8_t _ownKey, ScreenPlace _place);

/// ShowCommanderStatusScreen, F6 docked (D96) and F9 in flight: docked, a mission's briefing or debriefing first; then the
/// commander, systems, fuel, cash, legal status, rating and equipment, and the wait for the key that closes it
/// (WaitForScreenExitKey).
[[nodiscard]] Task<ScreenKey> ShowCommanderStatusScreen(GameState& _state, Host& _host, ScreenPlace _place);

/// The legal status the status screen names for _legalStatus: Clean below 1, Offender below 40, Fugitive from 40.
[[nodiscard]] LegalStanding LegalStandingOf(std::uint8_t _legalStatus) noexcept;

/// The rating the status screen names for _kills: the first of ratingThresholds above them, 0 Harmless to 8 Elite.
[[nodiscard]] std::uint8_t RatingOf(std::uint8_t _kills) noexcept;

/// What D08 shows of the commander as _state holds them (ADR-063): what ShowCommanderStatusScreen prints, and the cargo the
/// inventory lists (D75), as plain values.
[[nodiscard]] StatusView StatusViewOf(const GameState& _state);

/// The paragraphs of a transmission's _lines as the original prints them, a row each (ADR-068): the lines between blank ones
/// joined by a space, or by nothing after a line that ends in a hyphen, and "Press any key to continue!" left out, as D09's
/// button stands for it.
[[nodiscard]] std::vector<std::string> BriefingParagraphs(std::span<const std::string_view> _lines);

/// ShowMissionBriefing: missionStage=1, and the EMERGENCY screen of missionNumber, which waits for a key (Y or N
/// for the supernova's refugees, who take the hold). Docked, D09 shows it over D08 while it waits (ADR-068).
[[nodiscard]] Task<void> ShowMissionBriefing(GameState& _state, Host& _host);

/// ShowMissionDebriefing: nothing while the mission is not done; otherwise the TASK COMPLETE screen, which waits
/// for a key, and the reward. D09 shows it over D08 while it waits (ADR-068).
[[nodiscard]] Task<void> ShowMissionDebriefing(GameState& _state, Host& _host);

/// What DrawDockedFrame did of a docked screen's text page, which the shell draws in its place now (ADR-069): the text layout
/// unless it shows already (SetTextMode).
void EnterDockedScreen(GameState& _state, Host& _host);

/// RunTitleAndDocked: unless titleShown, the title (RunTitle) until a key, the credits and a new game; then the
/// docked screens (DockedKeyDispatch) from the status screen, or from D10 after a disk request, until F1 or D10 leaves for the
/// disk. Returns whether D10 left for the disk, through LeaveGameLoopForDisk (DiscExit); otherwise F1 ended them. Waits as a rule (ADR-015).
[[nodiscard]] Task<bool> RunTitleAndDocked(GameState& _state, Host& _host);

} // namespace Elite
