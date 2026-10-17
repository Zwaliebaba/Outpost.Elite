// GameLogic/ChartScreen.h
#pragma once

#include "ChartView.h"
#include "Docked.h"
#include "DockedKeys.h"
#include "Task.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Elite
{

class GameState;
class Host;

/// The most letters Find takes: a system's name.
inline constexpr std::size_t FIND_LETTERS_MOST = 8;

/// The chart's own state, which the original has no counterpart for in this shape: Find's state and the name typed.
struct ChartScreen
{
  FindState find = FindState::Closed;
  std::string typed;
};

/// What a key asks a chart to do (D98, D99, D05).
enum class ChartAction : std::uint8_t
{
  None,
  Recenter,  ///< keypad 5 or Space: the cursor back on the current system
  Nearest,   ///< D: the cursor onto the system nearest it, selected
  OpenFind,  ///< F: Find opened, empty
  Type,      ///< in Find: a character typed
  Erase,     ///< in Find: Backspace
  Submit,    ///< in Find: Enter
  CloseFind, ///< in Find: Esc, Find closed and the chart kept
};

struct ChartInput
{
  ChartAction action = ChartAction::None;
  std::uint8_t character = 0; ///< for Type
};

/// The action of _key on a chart, with Find open (_finding) or not: keypad 5 and Space recentre (D98), D and F; in Find, its
/// characters, Backspace, Enter and Esc. Nothing for a key the chart leaves to the screens (Esc and the F-keys, when Find is closed).
[[nodiscard]] ChartInput ChartKey(const KeyPress& _key, bool _finding);

/// _screen with a character typed into Find (_input Type, Erase), or Find opened or closed. Submit is the routine's, as it searches.
[[nodiscard]] ChartScreen StepFind(const ChartScreen& _screen, const ChartInput& _input);

/// What the chart open in _state shows with _screen, computed on _scratch, a copy of _state's data, so that nothing of _state moves
/// (R9): the stars, the target, the nearest system to the cursor or the locked one, and the fuel's reach.
[[nodiscard]] ChartView ChartViewOf(const GameState& _state, GameState& _scratch, const ChartScreen& _screen, ChartKind _kind,
                                    ScreenPlace _place);

/// D04 to D07, the galactic and the short-range chart (D82), which F4 and F5 open docked and F5 and F6 in flight, in place of the
/// original's charts and Data on System (D112). The chart is entered as the original entered it (EnterChart), then each tick
/// the clicks the shell sent move the cursor (D99, D109), a key acts (ChartKey), and the view is published (Host::ShowChart). A
/// key that closes it selects the system at the cursor unless the countdown runs (LeaveChart). Returns that key: Esc, or an
/// F-key other than the chart's own.
[[nodiscard]] Task<ScreenKey> ShowChartScreen(GameState& _state, Host& _host, ChartKind _kind, ScreenPlace _place);

} // namespace Elite
