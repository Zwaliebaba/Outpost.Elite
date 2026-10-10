// GameLogic/Galaxy.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's galaxy routines, ported (plan §5 Phase 3, ADR-010): the procedural galaxy, its systems and the charts. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> GalaxyEntries() noexcept;

/// ShowGalacticChart (CS:0CAE): F5's chart of the galaxy's 256 systems, a frame at a time until Esc or another F key
/// closes it, the cursor moved by the steering. D shows the distance to the system nearest the cursor, F finds one
/// by name, keypad 5 or fire recentres the cursor. Waits for keys. Out: AX the closing key.
void ShowGalacticChart(Guest& _guest);

/// ShowShortRangeChart (CS:0E52): F6's chart of the systems around the current one, labelled, as ShowGalacticChart
/// runs its own. Waits for keys. Out: AX the closing key.
void ShowShortRangeChart(Guest& _guest);

/// GetShortRangeOffset (CS:1076): CF set if the system in systemSeeds is on the current chart. On the
/// short-range chart, DX = its x less the current system's and CX = its chart row less the current one's.
void GetShortRangeOffset(Guest& _guest);

/// IsSystemOnChart (CS:10AF): GetShortRangeOffset's CF, keeping AX, CX and DX.
void IsSystemOnChart(Guest& _guest);

/// TwistSystemSeeds (CS:10C0): systemSeed0-2 (a, b, c) become (b, c, a+b+c).
void TwistSystemSeeds(Guest& _guest);

/// LoadGalaxySeeds (CS:10D6): galaxySeeds[galaxyNumber] into systemSeed0-2, system 0 of the galaxy.
void LoadGalaxySeeds(Guest& _guest);

/// GetCursorGalaxyPosition (CS:10FE): the chart cursor in galaxy units. Out: AX = BX, AL x, AH y/2.
void GetCursorGalaxyPosition(Guest& _guest);

/// MoveCursorToSystem (CS:1146): chartCursorX/Y onto the system in systemSeeds.
void MoveCursorToSystem(Guest& _guest);

/// SelectSystemAtCursor (CS:1199): the system nearest the cursor, its distance, its data and its name.
void SelectSystemAtCursor(Guest& _guest);

/// FindNearestSystem (CS:1292): the charted system nearest the cursor into selectedSystemIndex and
/// systemSeed0-2, and the cursor onto it.
void FindNearestSystem(Guest& _guest);

/// ComputeDistanceToSystem (CS:12F9): selectedDistanceTenthsLy from the current system.
void ComputeDistanceToSystem(Guest& _guest);

/// ShowNearestSystemDistance (CS:1341): the charted system nearest the cursor selected, and its distance and name
/// on the lines under the chart. Clobbers AX, BX, CX, DX, SI, DI, BP.
void ShowNearestSystemDistance(Guest& _guest);

/// LoadSystemSeeds (CS:139C): the seeds of system CL of the current galaxy. Out: CX = 0.
void LoadSystemSeeds(Guest& _guest);

/// AdvanceToNextSystem (CS:13B4): four TwistSystemSeeds.
void AdvanceToNextSystem(Guest& _guest);

/// GenerateSystemName (CS:13C1): selectedSystemName from systemSeed0-2, leaving the seeds on the next
/// system. Out: DH = the name's length.
void GenerateSystemName(Guest& _guest);

/// FindSystemByName (CS:140D): reads a name under the chart (ReadTextLine) and, if a system of the galaxy has it
/// and it is on the chart, moves the cursor there and ShowNearestSystemDistance; otherwise 'not on map'. Nothing
/// typed is ShowNearestSystemDistance alone. Waits for keys. Clobbers AX, BX, CX, DX, SI, DI, BP, ES.
void FindSystemByName(Guest& _guest);

/// DrawChartItems (CS:14C5): the short-range chart's discs and labels.
void DrawChartItems(Guest& _guest);

/// PlaceChartLabels (CS:1505): each pending label, nudged clear of every chart item, into chartItems.
void PlaceChartLabels(Guest& _guest);

/// AddChartLabel (CS:1552): appends a label item, DL/DH its x range and BL/BH its rows, for the name at
/// chartLabelCursor, and moves chartLabelCursor past the name.
void AddChartLabel(Guest& _guest);

/// NudgeChartLabel (CS:157D): moves the rows BL/BH out by the next step. CF set if it moved, clear once
/// out of tries.
void NudgeChartLabel(Guest& _guest);

/// ChartItemOverlaps (CS:15A9): CF set if the chart item at DI overlaps x range DL/DH and rows BL/BH.
void ChartItemOverlaps(Guest& _guest);

/// ClearChartTextLines (CS:15BC): blanks the two text lines under a chart.
void ClearChartTextLines(Guest& _guest);

/// ShowSystemDataScreen (CS:5CDE): F7's data on the selected system, then the keys until Esc or another F key
/// (WaitForScreenExitKey, CS:60B4), which selects the system at the chart cursor again. Waits for keys. Out: AX the
/// closing key.
void ShowSystemDataScreen(Guest& _guest);

/// TerminateSelectedSystemName (CS:60EB): a NUL after selectedSystemName. Out: SI at the name.
void TerminateSelectedSystemName(Guest& _guest);

/// FormatSelectedSystemDistance (CS:60F7): the distance digits with a decimal point, ending at DS:7D5F.
/// Out: SI at the first digit.
void FormatSelectedSystemDistance(Guest& _guest);

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

/// BackspaceDescription (CS:7107): control code 4, DI back one.
void BackspaceDescription(Guest& _guest);

/// StartCapitalizing (CS:7109): control code 5, descriptionCapitalize = 1.
void StartCapitalizing(Guest& _guest);

/// StopCapitalizing (CS:710F): control code 6, descriptionCapitalize = 0.
void StopCapitalizing(Guest& _guest);

/// NextDescriptionRandom (CS:7115): the Fibonacci step on descriptionSeed0-1. Out: AX.
void NextDescriptionRandom(Guest& _guest);

/// CopySelectedNameLower (CS:7124): selectedSystemName into descriptionNameBuffer, all but its first
/// letter lower-cased, then a space. Out: DI at the space.
void CopySelectedNameLower(Guest& _guest);

} // namespace Elite
