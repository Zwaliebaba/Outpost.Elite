// GameLogic/Galaxy.h
#pragma once

#include "Arithmetic.h"
#include "ChartView.h"
#include "Docked.h"
#include "GameState.h"
#include "Host.h"
#include "Text.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Elite
{

// The reference's galaxy routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the procedural galaxy, its systems and the
// charts. Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the original's routine of its name computes, with nothing held in a register: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// A system's seeds: systemSeed0-2 for the system the galaxy's routines are on, and each entry of
/// galaxySeeds for the first system of a galaxy. Everything about a system comes from these three words, and
/// its place is in their high bytes.
struct SystemSeeds
{
  std::uint16_t seed0;
  std::uint16_t seed1;
  std::uint16_t seed2;

  /// systemX: the system's x in the galaxy, and on the galactic chart.
  [[nodiscard]] constexpr std::uint8_t X() const noexcept
  {
    return static_cast<std::uint8_t>(seed1 >> 8);
  }

  /// systemY halved: the system's row on the galactic chart, which shows half the galaxy's height.
  [[nodiscard]] constexpr std::uint8_t Row() const noexcept
  {
    return static_cast<std::uint8_t>(seed0 >> 9);
  }
};

/// systemSeed0-2: the seeds of the system the galaxy's routines are on.
[[nodiscard]] SystemSeeds ReadSystemSeeds(const GameState& _state);

/// systemX, the high byte of systemSeed1: the x on the galactic chart of the system being generated.
[[nodiscard]] inline std::uint8_t SystemX(const GameState& _state) noexcept
{
  return High(_state.Data().galaxy.systemSeed1);
}

/// systemY, the high byte of systemSeed0: twice the row on the galactic chart of the system being generated,
/// and its size's bit.
[[nodiscard]] inline std::uint8_t SystemY(const GameState& _state) noexcept
{
  return High(_state.Data().galaxy.systemSeed0);
}

/// systemSeed2High, the high byte of systemSeed2.
[[nodiscard]] inline std::uint8_t SystemSeed2High(const GameState& _state) noexcept
{
  return High(_state.Data().galaxy.systemSeed2);
}

/// A place in the galaxy in chart units: x, and the row, half the galaxy's y.
struct ChartPoint
{
  std::uint8_t x;
  std::uint8_t row;
};

/// Where GetShortRangeOffset finds the system in systemSeeds, from the current system.
struct ShortRangeOffset
{
  bool onChart;                    ///< on the current chart; every system is on the galactic chart
  std::optional<std::int16_t> x;   ///< its x less the current system's, measured on the short-range chart
  std::optional<std::int16_t> row; ///< its chart row less the current system's, measured once x is within the chart
};

/// GetShortRangeOffset: whether the system in systemSeeds is on the current chart, and on the
/// short-range chart its offsets from the current system.
[[nodiscard]] ShortRangeOffset GetShortRangeOffset(const GameState& _state);

/// IsSystemOnChart: whether the system in systemSeeds is on the current chart, as GetShortRangeOffset finds.
[[nodiscard]] bool IsSystemOnChart(const GameState& _state);

/// TwistSystemSeeds: systemSeed0-2 (a, b, c) become (b, c, a+b+c).
void TwistSystemSeeds(GameState& _state);

/// LoadGalaxySeeds: galaxySeeds[galaxyNumber] into systemSeed0-2, system 0 of the galaxy.
void LoadGalaxySeeds(GameState& _state);

/// GetCursorGalaxyPosition: the chart cursor in galaxy units. On the short-range chart that is
/// the current system plus 2/7 of the cursor's offset from the chart's centre, at least 0.
[[nodiscard]] ChartPoint GetCursorGalaxyPosition(const GameState& _state);

/// MoveCursorToSystem: chartCursorX/Y onto the system in systemSeeds.
void MoveCursorToSystem(GameState& _state);

/// SelectSystemAtCursor: the system nearest the chart cursor selected (FindNearestSystem),
/// its distance into distanceDigits, then DescribeSystem.
void SelectSystemAtCursor(GameState& _state);

/// The rest of SelectSystemAtCursor, named so that the galaxies' known answers can call it (D33): from the seeds in
/// systemSeed0-2, the system's government, economy, tech level, population, species, productivity and radius into the
/// selected system's record, the description's seeds, and its name (GenerateSystemName, which leaves the seeds on the next
/// system). The cursor cannot reach every system by itself: two systems of a galaxy can share a place on the chart, and the
/// search selects the first.
void DescribeSystem(GameState& _state);

/// FindNearestSystem: the system nearest the chart cursor, by dx^2 + (dy/2)^2 in chart units, of those on the
/// current chart: its index into selectedSystem.index, its seeds into systemSeed0-2, and the cursor onto it. A distance whose
/// sum carries is passed over. The index is 100h less the count the original's loop had left at the system, or, when no system
/// is on the chart, which never happens, the current system's (ADR-046). Returns the index.
std::uint8_t FindNearestSystem(GameState& _state);

/// ComputeDistanceToSystem: the distance from the current system to the one in systemSeeds, in
/// tenths of a light year: four times the root of dx^2 + (dy/2)^2. It writes selectedSystem.distanceTenthsLy.
void ComputeDistanceToSystem(GameState& _state);

/// What the original's D showed under the chart, without the showing: the system nearest the chart cursor selected
/// (FindNearestSystem), its distance into distanceDigits and distanceText, and its name generated.
void NoteNearestSystem(GameState& _state);

/// LoadSystemSeeds: the seeds of system _system of the current galaxy: LoadGalaxySeeds, then
/// AdvanceToNextSystem _system times.
void LoadSystemSeeds(GameState& _state, std::uint8_t _system);

/// AdvanceToNextSystem: four TwistSystemSeeds, the seeds of the next system.
void AdvanceToNextSystem(GameState& _state);

/// GenerateSystemName: selectedSystem.name and its length from systemSeed0-2, leaving the seeds on the next
/// system. Returns the length.
std::uint8_t GenerateSystemName(GameState& _state);

/// What the original's Find found for the name in findInput, as Find leaves it typed: nothing typed, the nearest system
/// (NoteNearestSystem); a system of the galaxy by that name on the current chart, the cursor moved onto it and NoteNearestSystem;
/// otherwise none on this chart.
enum class FindResult : std::uint8_t
{
  Nearest,
  Found,
  NotOnChart,
};

/// The original's Find, once the name is typed into findInput: the name echoed into findInputEcho and upper-cased, and each
/// system's name compared with it, the cursor moved as FindResult says.
FindResult FindTypedSystem(GameState& _state);

/// What the original's chart did on opening, without the drawing: the galactic chart (_kind) the current system's place on it,
/// its number in the title's place, and the cursor it keeps; the short-range chart the cursor it keeps; either the chart's
/// screen layout (DrawChartFrame) and chartIsShortRange.
void EnterChart(GameState& _state, Host& _host, ChartKind _kind);

/// What the original's chart did as a key closed it: unless the hyperspace countdown runs, the system at the cursor selected
/// (SelectSystemAtCursor) and the cursor kept for the chart open, as chartIsShortRange says.
void LeaveChart(GameState& _state);

/// Keypad 5's: the cursor back on the current system, the galactic chart's on it and the short-range chart's on its centre.
void RecenterChartCursor(GameState& _state);

/// The cursor moved to _point, in the open chart's units, held to its 128 rows as the steering held it.
void PlaceChartCursor(GameState& _state, ChartClick _point);

/// The short-range chart's centre, in its units: the current system's place.
inline constexpr ChartClick SHORT_RANGE_CENTER{.x = 0x50, .row = 0x40};

/// The selected system's facts, as DescribeSystem leaves them in selectedSystem and Data on System prints them, its description
/// expanded (SystemDescription).
[[nodiscard]] SystemFacts SelectedSystemFacts(GameState& _state);

/// Every system of the open chart, in its units: on the galactic chart the galaxy's 256, on the short-range chart those on it, with
/// their names. It moves the seeds.
[[nodiscard]] std::vector<ChartStar> ChartStars(GameState& _state);

/// System _system's place on the open chart, in its units: where MoveCursorToSystem would put the cursor. It moves the seeds and
/// the cursor.
[[nodiscard]] ChartClick SystemChartPlace(GameState& _state, std::uint8_t _system);

/// TerminateSelectedSystemName: a NUL after selectedSystem.name.
void TerminateSelectedSystemName(GameState& _state);

/// SystemDescription: descriptionBuffer zeroed a word at a time, forwards, and the selected system's description expanded into
/// it from descriptionTemplate. Returns the description.
std::string SystemDescription(GameState& _state);

/// ExpandDescriptionText: the coded text _text expanded into descriptionBuffer from its byte _output, recursively: a byte
/// of 1-6 runs the
/// control code's handler, as textControlCodes lists them, one of 80h up a phrase from its list (DESCRIPTION_PHRASE_LISTS)
/// picked by NextDescriptionRandom, and any other is copied, but a space after a space, and a letter after a space
/// upper-cased while descriptionCapitalize is 1. Returns the place past the last character written.
std::uint16_t ExpandDescriptionText(GameState& _state, std::string_view _text, std::uint16_t _output);

/// InsertSystemName: control code 1, the selected system's name, as CopySelectedNameLower leaves it,
/// expanded into descriptionBuffer from its byte _output. Returns the place past the last character written.
std::uint16_t InsertSystemName(GameState& _state, std::uint16_t _output);

/// InsertSystemAdjective: control code 2, the selected system's name less a final vowel, and "ian ",
/// expanded into descriptionBuffer from its byte _output. Returns the place past the last character written.
std::uint16_t InsertSystemAdjective(GameState& _state, std::uint16_t _output);

/// InsertRandomName: control code 3, a name GenerateSystemName makes from descriptionSeed0-1, expanded into
/// descriptionBuffer from its byte _output; selectedSystem.name's eight bytes are kept in invasionDebriefText meanwhile
/// and put back, its length not. Returns the place past the last character written.
std::uint16_t InsertRandomName(GameState& _state, std::uint16_t _output);

/// BackspaceDescription: control code 4, the description's output at _output back one.
[[nodiscard]] std::uint16_t BackspaceDescription(std::uint16_t _output);

/// StartCapitalizing: control code 5, descriptionCapitalize = 1.
void StartCapitalizing(GameState& _state);

/// StopCapitalizing: control code 6, descriptionCapitalize = 0.
void StopCapitalizing(GameState& _state);

/// NextDescriptionRandom: the Fibonacci step on descriptionSeed0-1, (a, b) to (b, a+b), and a+b.
std::uint16_t NextDescriptionRandom(GameState& _state);

/// CopySelectedNameLower: selectedSystem.name, terminated, into descriptionNameBuffer, all but its first
/// letter lower-cased, then a space and a NUL. Returns the index of the space in descriptionNameBuffer.
std::size_t CopySelectedNameLower(GameState& _state);

} // namespace Elite
