// GameLogic/ChartView.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Elite
{

/// The two charts (D82): the galactic chart, D04 and D05, and the short-range chart, D06 and D07.
enum class ChartKind : std::uint8_t
{
  Galactic,
  ShortRange,
};

/// A point on a chart in its own units, as the original's cursor has it: on the galactic chart a system's x and its row, half its
/// y, 0 to 255 and 0 to 127; on the short-range chart 3.5 of them to a galaxy unit about the current system, which is at 80, 64.
struct ChartClick
{
  std::uint8_t x = 0;
  std::uint8_t row = 0;
};

/// A system as a chart shows it.
struct ChartStar
{
  std::uint8_t x = 0;   ///< in the chart's units
  std::uint8_t row = 0; ///< in the chart's units
  bool large = false;   ///< the larger of the two dots the short-range chart draws
  std::string name;     ///< on the short-range chart, which names every system on it; empty on the galactic chart
};

/// What a system is, as Data on System prints it (DescribeSystem).
struct SystemFacts
{
  std::string name;                 ///< RIEDQUAT, as the game stores it
  std::uint8_t index = 0;           ///< in its galaxy, 0 to 255
  std::uint16_t distanceTenths = 0; ///< from the current system, in tenths of a light year
  std::string economy;              ///< Mainly Industrial
  std::string government;           ///< Anarchy
  std::uint8_t techLevel = 0;       ///< as shown, 1 to 16
  std::uint16_t populationTenthsBillion = 0;
  std::string species; ///< Human Colonials, Green Fat Insects
  std::uint16_t productivityMillionCredits = 0;
  std::uint16_t radiusKm = 0;
  std::string description; ///< the system's description, as Data on System expands it
};

/// The chart's target, the system nearest the cursor or the one locked for the jump, with its place on the chart and whether the
/// fuel reaches it.
struct ChartTarget
{
  SystemFacts facts;
  std::uint8_t x = 0;            ///< in the chart's units
  std::uint8_t row = 0;          ///< in the chart's units
  bool inRange = false;          ///< the hyperspace jump would go: the distance and the fuel allow it
  std::uint16_t shortTenths = 0; ///< out of range: how far beyond the fuel's reach, in tenths of a light year
};

/// The chart's Find (D05): closed, a name being typed, or the last name typed not on this chart.
enum class FindState : std::uint8_t
{
  Closed,
  Typing,
  NotOnChart,
};

/// What a chart shows, for the shell (R9): plain values, nothing of the engine's and nothing of the game's state. The game
/// publishes one each time the chart changes (Host::ShowChart).
struct ChartView
{
  std::uint64_t sequence = 0; ///< from 1, one more for each view published
  ChartKind kind = ChartKind::Galactic;
  bool flight = false;     ///< shown in flight (D07), otherwise docked
  std::string systemName;  ///< the current system's name, as the game stores it
  bool dodo = false;       ///< docked at a Dodo, not a Coriolis
  bool witchSpace = false; ///< in flight, in witch space
  std::uint32_t creditsTenths = 0;
  std::uint8_t galaxyNumber = 1; ///< as the title shows it, from 1

  std::uint8_t currentX = 0; ///< the current system, in the chart's units
  std::uint8_t currentRow = 0;
  std::uint8_t reachTenths = 0; ///< how far the fuel reaches, in tenths of a light year
  float ringRadius = 0.0f;      ///< that reach, in the chart's units: the fuel ring about the current system (D100)

  std::uint8_t cursorX = 0; ///< in the chart's units
  std::uint8_t cursorRow = 0;
  std::vector<ChartStar> stars; ///< every system of the galaxy, or every system on the short-range chart

  ChartTarget target;
  bool locked = false;        ///< the hyperspace countdown runs: the target is fixed until the jump
  std::uint8_t countdown = 0; ///< the countdown, 10 to 1, while it runs

  FindState find = FindState::Closed;
  std::string typed; ///< Find's name, as typed, or the last one not on the chart
};

} // namespace Elite
