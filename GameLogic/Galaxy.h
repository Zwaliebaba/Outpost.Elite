// GameLogic/Galaxy.h
#pragma once

#include "Arithmetic.h"
#include "Docked.h"
#include "GameState.h"
#include "Hardware.h"
#include "Text.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace Elite
{

// The reference's galaxy routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the procedural galaxy, its systems and the
// charts. Each takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the 1987 routine of its name computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// A system's seeds: systemSeed0-2 (DS:2970) for the system the galaxy's routines are on, and each entry of
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

/// systemX, the high byte of systemSeed1 (DS:2973 in 1987): the x on the galactic chart of the system being generated.
[[nodiscard]] inline std::uint8_t SystemX(const GameState& _state) noexcept
{
  return High(_state.Data().galaxy.systemSeed1);
}

/// systemY, the high byte of systemSeed0 (DS:2971 in 1987): twice the row on the galactic chart of the system being generated,
/// and its size's bit.
[[nodiscard]] inline std::uint8_t SystemY(const GameState& _state) noexcept
{
  return High(_state.Data().galaxy.systemSeed0);
}

/// systemSeed2High, the high byte of systemSeed2 (DS:2975 in 1987).
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

/// The first and the last of a span on the short-range chart: a chart item's x range, or its rows.
struct ChartSpan
{
  std::uint8_t first;
  std::uint8_t last;
};

/// What NudgeChartLabel does with a label's rows.
struct LabelNudge
{
  bool moved;     ///< it moved them; otherwise it is out of tries
  ChartSpan rows; ///< the rows, as it leaves them either way
};

/// GetShortRangeOffset (CS:1076): whether the system in systemSeeds is on the current chart, and on the
/// short-range chart its offsets from the current system.
[[nodiscard]] ShortRangeOffset GetShortRangeOffset(const GameState& _state);

/// IsSystemOnChart (CS:10AF): whether the system in systemSeeds is on the current chart, as GetShortRangeOffset finds.
[[nodiscard]] bool IsSystemOnChart(const GameState& _state);

/// TwistSystemSeeds (CS:10C0): systemSeed0-2 (a, b, c) become (b, c, a+b+c).
void TwistSystemSeeds(GameState& _state);

/// LoadGalaxySeeds (CS:10D6): galaxySeeds[galaxyNumber] into systemSeed0-2, system 0 of the galaxy.
void LoadGalaxySeeds(GameState& _state);

/// GetCursorGalaxyPosition (CS:10FE): the chart cursor in galaxy units. On the short-range chart that is
/// the current system plus 2/7 of the cursor's offset from the chart's centre, at least 0.
[[nodiscard]] ChartPoint GetCursorGalaxyPosition(const GameState& _state);

/// MoveCursorToSystem (CS:1146): chartCursorX/Y onto the system in systemSeeds.
void MoveCursorToSystem(GameState& _state);

/// SelectSystemAtCursor (CS:1199): the system nearest the chart cursor selected (FindNearestSystem),
/// its distance into distanceDigits, then DescribeSystem.
void SelectSystemAtCursor(GameState& _state);

/// The rest of SelectSystemAtCursor, named so that the galaxies' known answers can call it (D33): from the seeds in
/// systemSeed0-2, the system's government, economy, tech level, population, species, productivity and radius into the
/// selected system's record, the description's seeds, and its name (GenerateSystemName, which leaves the seeds on the next
/// system). The cursor cannot reach every system by itself: two systems of a galaxy can share a place on the chart, and the
/// search selects the first.
void DescribeSystem(GameState& _state);

/// FindNearestSystem (CS:1292): the system nearest the chart cursor, by dx^2 + (dy/2)^2 in chart units, of those on the
/// current chart: its index into selectedSystem.index, its seeds into systemSeed0-2, and the cursor onto it. A distance whose
/// sum carries is passed over. The index is 100h less the count the original's loop had left at the system, or, when no system
/// is on the chart, which never happens, the current system's (ADR-046). Returns the index.
std::uint8_t FindNearestSystem(GameState& _state);

/// ComputeDistanceToSystem (CS:12F9): the distance from the current system to the one in systemSeeds, in
/// tenths of a light year: four times the root of dx^2 + (dy/2)^2. It writes selectedSystem.distanceTenthsLy.
void ComputeDistanceToSystem(GameState& _state);

/// ShowNearestSystemDistance (CS:1341): the system nearest the chart cursor selected (FindNearestSystem), and
/// 'Distance: nnn.n Light Years' and its name on the two text lines under the chart.
void ShowNearestSystemDistance(GameState& _state);

/// LoadSystemSeeds (CS:139C): the seeds of system _system of the current galaxy: LoadGalaxySeeds, then
/// AdvanceToNextSystem _system times.
void LoadSystemSeeds(GameState& _state, std::uint8_t _system);

/// AdvanceToNextSystem (CS:13B4): four TwistSystemSeeds, the seeds of the next system.
void AdvanceToNextSystem(GameState& _state);

/// GenerateSystemName (CS:13C1): selectedSystem.name and its length from systemSeed0-2, leaving the seeds on the next
/// system. Returns the length.
std::uint8_t GenerateSystemName(GameState& _state);

/// FindSystemByName (CS:140D): a name read under the chart (ReadTextLine) and, if a system of the galaxy has it and
/// it is on the chart, the cursor moved there and ShowNearestSystemDistance; otherwise 'not on map'. Nothing typed is
/// ShowNearestSystemDistance alone. Waits for keys as a rule (ADR-015).
void FindSystemByName(GameState& _state, Hardware& _hardware);

/// ShowSystemDataScreen (CS:5CDE): F7's data on the selected system and its description, then the keys until Esc or another F
/// key (WaitForScreenExitKey), which selects the system at the chart cursor again. Waits for keys as a rule (ADR-015).
ScreenKey ShowSystemDataScreen(GameState& _state, Hardware& _hardware);

/// ShowGalacticChart (CS:0CAE): F5's chart of the galaxy's 256 systems: the current system and the cursor kept for the galactic
/// chart, a frame at a time until Esc or another F key closes it, the cursor moved by the steering. D shows the distance to the
/// system nearest the cursor, F finds one by name, keypad 5 or fire recentres the cursor; a key that closes it selects the system
/// at the cursor unless the hyperspace countdown runs. Waits for keys as a rule (ADR-015).
ScreenKey ShowGalacticChart(GameState& _state, Hardware& _hardware);

/// ShowShortRangeChart (CS:0E52): F6's chart of the systems around the current one, each a disc and a label placed clear of the
/// others (PlaceChartLabels), run as ShowGalacticChart runs its own.
ScreenKey ShowShortRangeChart(GameState& _state, Hardware& _hardware);

/// DrawChartItems (CS:14C5): the short-range chart's chartItemCount items in colour 3: a label by DrawSmallViewString in ink
/// FFFFh at its top-left corner, a disc (DrawDisc) at its centre.
void DrawChartItems(GameState& _state);

/// PlaceChartLabels (CS:1505): each of chartItemCount pending labels, nudged clear of every chart item until
/// NudgeChartLabel runs out of tries, added to chartItems by AddChartLabel.
void PlaceChartLabels(GameState& _state);

/// AddChartLabel (CS:1552): appends a label item, its x range _x and its rows _rows, for the name at
/// chartLabelCursor, and moves chartLabelCursor past the name.
void AddChartLabel(GameState& _state, ChartSpan _x, ChartSpan _rows);

/// NudgeChartLabel (CS:157D): moves a label's rows out by the next step, up for an odd step and down for an
/// even one, skipping a move off the chart, until it is out of tries.
[[nodiscard]] LabelNudge NudgeChartLabel(GameState& _state, ChartSpan _rows);

/// ChartItemOverlaps (CS:15A9): whether item _item of chartItems overlaps x range _x and rows _rows.
[[nodiscard]] bool ChartItemOverlaps(const GameState& _state, std::size_t _item, ChartSpan _x, ChartSpan _rows);

/// ClearChartTextLines (CS:15BC): blankChartLine drawn in _ink over the two text lines under a chart.
void ClearChartTextLines(GameState& _state, std::uint16_t _ink);

/// TerminateSelectedSystemName (CS:60EB): a NUL after selectedSystem.name.
void TerminateSelectedSystemName(GameState& _state);

/// FormatSelectedSystemDistance (CS:60F7): the distance digits with a decimal point, copied into selectedDistanceText, ending at
/// its last byte (DS:7D5F in 1987). Returns the text, from its first digit.
std::string_view FormatSelectedSystemDistance(GameState& _state);

/// ShowSystemDescription (CS:6FC0): descriptionBuffer zeroed a word at a time as REP STOSW does, forwards, the selected
/// system's description expanded into it from descriptionTemplate, and printed on the text
/// page from row 19, word-wrapped at the last space within 36 characters.
void ShowSystemDescription(GameState& _state);

/// ExpandDescriptionText (CS:700F): the coded text _text expanded into descriptionBuffer from its byte _output, recursively: a byte
/// of 1-6 runs the
/// control code's handler, as textControlCodes lists them, one of 80h up a phrase from its list (DESCRIPTION_PHRASE_LISTS)
/// picked by NextDescriptionRandom, and any other is copied, but a space after a space, and a letter after a space
/// upper-cased while descriptionCapitalize is 1. Returns the place past the last character written.
std::uint16_t ExpandDescriptionText(GameState& _state, std::string_view _text, std::uint16_t _output);

/// InsertSystemName (CS:707A): control code 1, the selected system's name, as CopySelectedNameLower leaves it,
/// expanded into descriptionBuffer from its byte _output. Returns the place past the last character written.
std::uint16_t InsertSystemName(GameState& _state, std::uint16_t _output);

/// InsertSystemAdjective (CS:708D): control code 2, the selected system's name less a final vowel, and "ian ",
/// expanded into descriptionBuffer from its byte _output. Returns the place past the last character written.
std::uint16_t InsertSystemAdjective(GameState& _state, std::uint16_t _output);

/// InsertRandomName (CS:70C1): control code 3, a name GenerateSystemName makes from descriptionSeed0-1, expanded into
/// descriptionBuffer from its byte _output; selectedSystem.name's eight bytes are kept in invasionDebriefText meanwhile (at DS:9650
/// in 1987) and put back, its length not. Returns the place past the last character written.
std::uint16_t InsertRandomName(GameState& _state, std::uint16_t _output);

/// BackspaceDescription (CS:7107): control code 4, the description's output at _output back one.
[[nodiscard]] std::uint16_t BackspaceDescription(std::uint16_t _output);

/// StartCapitalizing (CS:7109): control code 5, descriptionCapitalize = 1.
void StartCapitalizing(GameState& _state);

/// StopCapitalizing (CS:710F): control code 6, descriptionCapitalize = 0.
void StopCapitalizing(GameState& _state);

/// NextDescriptionRandom (CS:7115): the Fibonacci step on descriptionSeed0-1, (a, b) to (b, a+b), and a+b.
std::uint16_t NextDescriptionRandom(GameState& _state);

/// CopySelectedNameLower (CS:7124): selectedSystem.name, terminated, into descriptionNameBuffer, all but its first
/// letter lower-cased, then a space and a NUL. Returns the index of the space in descriptionNameBuffer.
std::size_t CopySelectedNameLower(GameState& _state);

} // namespace Elite
