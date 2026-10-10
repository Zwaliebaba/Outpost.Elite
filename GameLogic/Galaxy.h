// GameLogic/Galaxy.h
#pragma once

#include "NativeEntry.h"

#include <cstdint>
#include <optional>
#include <span>

namespace Elite
{

// The reference's galaxy routines, ported (plan §5 Phase 3, ADR-010): the procedural galaxy, its systems and the charts. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv. The routines de-assembled so far (ADR-012) take values and
// give values back, and their entries, at the end, keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> GalaxyEntries() noexcept;

/// ShowGalacticChart (CS:0CAE): F5's chart of the galaxy's 256 systems, a frame at a time until Esc or another F key
/// closes it, the cursor moved by the steering. D shows the distance to the system nearest the cursor, F finds one
/// by name, keypad 5 or fire recentres the cursor. Waits for keys. Out: AX the closing key.
void ShowGalacticChart(Guest& _guest);

/// ShowShortRangeChart (CS:0E52): F6's chart of the systems around the current one, labelled, as ShowGalacticChart
/// runs its own. Waits for keys. Out: AX the closing key.
void ShowShortRangeChart(Guest& _guest);

/// SelectSystemAtCursor (CS:1199): the system nearest the cursor, its distance, its data and its name.
void SelectSystemAtCursor(Guest& _guest);

/// FindNearestSystem (CS:1292): the charted system nearest the cursor into selectedSystemIndex and
/// systemSeed0-2, and the cursor onto it.
void FindNearestSystem(Guest& _guest);

/// ShowNearestSystemDistance (CS:1341): the charted system nearest the cursor selected, and its distance and name
/// on the lines under the chart. Clobbers AX, BX, CX, DX, SI, DI, BP.
void ShowNearestSystemDistance(Guest& _guest);

/// LoadSystemSeeds (CS:139C): the seeds of system CL of the current galaxy. Out: CX = 0.
void LoadSystemSeeds(Guest& _guest);

/// FindSystemByName (CS:140D): reads a name under the chart (ReadTextLine) and, if a system of the galaxy has it
/// and it is on the chart, moves the cursor there and ShowNearestSystemDistance; otherwise 'not on map'. Nothing
/// typed is ShowNearestSystemDistance alone. Waits for keys. Clobbers AX, BX, CX, DX, SI, DI, BP, ES.
void FindSystemByName(Guest& _guest);

/// DrawChartItems (CS:14C5): the short-range chart's discs and labels.
void DrawChartItems(Guest& _guest);

/// PlaceChartLabels (CS:1505): each pending label, nudged clear of every chart item, into chartItems.
void PlaceChartLabels(Guest& _guest);

/// ClearChartTextLines (CS:15BC): blanks the two text lines under a chart.
void ClearChartTextLines(Guest& _guest);

/// ShowSystemDataScreen (CS:5CDE): F7's data on the selected system, then the keys until Esc or another F key
/// (WaitForScreenExitKey, CS:60B4), which selects the system at the chart cursor again. Waits for keys. Out: AX the
/// closing key.
void ShowSystemDataScreen(Guest& _guest);

/// ShowSystemDescription (CS:6FC0): the selected system's description, expanded and word-wrapped onto
/// the text screen.
void ShowSystemDescription(Guest& _guest);

/// ExpandDescriptionText (CS:700F): expands the coded text at SI into DI, recursively. Out: DI past the
/// output.
void ExpandDescriptionText(Guest& _guest);

/// InsertSystemName (CS:707A): control code 1, the selected system's name into DI.
void InsertSystemName(Guest& _guest);

/// InsertSystemAdjective (CS:708D): control code 2, the selected system's name as an adjective into DI.
void InsertSystemAdjective(Guest& _guest);

/// InsertRandomName (CS:70C1): control code 3, a name made from descriptionSeed0-1 into DI.
void InsertRandomName(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight: its inputs are
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

/// ComputeDistanceToSystem (CS:12F9): the distance from the current system to the one in systemSeeds, in
/// tenths of a light year: four times the root of dx^2 + (dy/2)^2. It writes selectedDistanceTenthsLy.
std::uint16_t ComputeDistanceToSystem(GameState& _state);

/// AdvanceToNextSystem (CS:13B4): four TwistSystemSeeds, the seeds of the next system.
void AdvanceToNextSystem(GameState& _state);

/// GenerateSystemName (CS:13C1): selectedSystemName and its length from systemSeed0-2, leaving the seeds on the next
/// system. Returns the length.
std::uint8_t GenerateSystemName(GameState& _state);

/// AddChartLabel (CS:1552): appends a label item, its x range _x and its rows _rows, for the name at
/// chartLabelCursor, and moves chartLabelCursor past the name.
void AddChartLabel(GameState& _state, ChartSpan _x, ChartSpan _rows);

/// NudgeChartLabel (CS:157D): moves a label's rows out by the next step, up for an odd step and down for an
/// even one, skipping a move off the chart, until it is out of tries.
[[nodiscard]] LabelNudge NudgeChartLabel(GameState& _state, ChartSpan _rows);

/// ChartItemOverlaps (CS:15A9): whether the chart item at DS:_item overlaps x range _x and rows _rows.
[[nodiscard]] bool ChartItemOverlaps(const GameState& _state, std::uint16_t _item, ChartSpan _x, ChartSpan _rows);

/// TerminateSelectedSystemName (CS:60EB): a NUL after selectedSystemName. Returns the name's length.
std::uint8_t TerminateSelectedSystemName(GameState& _state);

/// FormatSelectedSystemDistance (CS:60F7): the distance digits with a decimal point, ending at DS:7D5F.
/// Returns the offset of the first digit.
std::uint16_t FormatSelectedSystemDistance(GameState& _state);

/// BackspaceDescription (CS:7107): control code 4, the description's output at _output back one.
[[nodiscard]] std::uint16_t BackspaceDescription(std::uint16_t _output);

/// StartCapitalizing (CS:7109): control code 5, descriptionCapitalize = 1.
void StartCapitalizing(GameState& _state);

/// StopCapitalizing (CS:710F): control code 6, descriptionCapitalize = 0.
void StopCapitalizing(GameState& _state);

/// NextDescriptionRandom (CS:7115): the Fibonacci step on descriptionSeed0-1, (a, b) to (b, a+b), and a+b.
std::uint16_t NextDescriptionRandom(GameState& _state);

/// CopySelectedNameLower (CS:7124): selectedSystemName, terminated, into descriptionNameBuffer, all but its first
/// letter lower-cased, then a space and a NUL. Returns the offset of the space.
std::uint16_t CopySelectedNameLower(GameState& _state);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes its
// results back there. The registers the contract leaves to the routine it hands to Guest::Clobber.

void GetShortRangeOffsetEntry(Guest& _guest);
void IsSystemOnChartEntry(Guest& _guest);
void TwistSystemSeedsEntry(Guest& _guest);
void LoadGalaxySeedsEntry(Guest& _guest);
void GetCursorGalaxyPositionEntry(Guest& _guest);
void MoveCursorToSystemEntry(Guest& _guest);
void ComputeDistanceToSystemEntry(Guest& _guest);
void AdvanceToNextSystemEntry(Guest& _guest);
void GenerateSystemNameEntry(Guest& _guest);
void AddChartLabelEntry(Guest& _guest);
void NudgeChartLabelEntry(Guest& _guest);
void ChartItemOverlapsEntry(Guest& _guest);
void TerminateSelectedSystemNameEntry(Guest& _guest);
void FormatSelectedSystemDistanceEntry(Guest& _guest);
void BackspaceDescriptionEntry(Guest& _guest);
void StartCapitalizingEntry(Guest& _guest);
void StopCapitalizingEntry(Guest& _guest);
void NextDescriptionRandomEntry(Guest& _guest);
void CopySelectedNameLowerEntry(Guest& _guest);

} // namespace Elite
