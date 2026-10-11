// GameLogic/Outcome.h
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Elite
{

class GameState;

/// How an outcome is checked (D33).
enum class OutcomeKind : std::uint8_t
{
  Progress, ///< the player's progress, which a small difference in timing should not move: a difference fails
  Timing    ///< a value that moves with the exact timing of flight: a difference is reported, and fails nothing
};

/// A named fact of the game's state that a player could notice (D33), and how it is checked.
struct OutcomeName
{
  std::string_view name;
  OutcomeKind kind;
};

/// Every outcome, in the order an outcome line holds them (Replay.h): the commander's state by the names GameData gives it,
/// then the flight's. They are what a player could notice, not every byte; the state digest checks every byte.
///
/// Progress, which gates: where the commander is (galaxy, system, witch space, docked, in flight, dead), what they have
/// (credits, the hold, the refugees, equipment, missiles, lasers and their mounts), their standing (legal status, kills, the
/// title Archangel replaces) and the missions' state. Each moves only when something happens to the commander, never frame
/// by frame, so a change in timing that keeps the game's events keeps them.
///
/// Timing, which reports: energy, the shields, speed, fuel, the ship slots in use, the temperatures, the altitude and the
/// missions' frame counters. Each moves frame by frame in flight, so it follows exact timing: fuel drains in a fuel leak
/// and fills while the scoops skim the sun, and ships spawn at random on a frame's draw.
inline constexpr auto OUTCOMES = std::to_array<OutcomeName>({
  {"galaxy", OutcomeKind::Progress},
  {"system", OutcomeKind::Progress},
  {"witchSpace", OutcomeKind::Progress},
  {"docked", OutcomeKind::Progress},
  {"inFlight", OutcomeKind::Progress},
  {"dead", OutcomeKind::Progress},
  {"creditsTenths", OutcomeKind::Progress},
  {"legalStatus", OutcomeKind::Progress},
  {"kills", OutcomeKind::Progress},
  {"title", OutcomeKind::Progress},
  {"cargo", OutcomeKind::Progress},
  {"refugeesTonnes", OutcomeKind::Progress},
  {"missiles", OutcomeKind::Progress},
  {"largeCargoBay", OutcomeKind::Progress},
  {"ecm", OutcomeKind::Progress},
  {"fuelScoops", OutcomeKind::Progress},
  {"escapePod", OutcomeKind::Progress},
  {"energyBomb", OutcomeKind::Progress},
  {"energyUnit", OutcomeKind::Progress},
  {"dockingComputer", OutcomeKind::Progress},
  {"galacticHyperdrive", OutcomeKind::Progress},
  {"maskingDevice", OutcomeKind::Progress},
  {"antiEcm", OutcomeKind::Progress},
  {"pulseLasers", OutcomeKind::Progress},
  {"beamLasers", OutcomeKind::Progress},
  {"miningLasers", OutcomeKind::Progress},
  {"militaryLasers", OutcomeKind::Progress},
  {"laserMounts", OutcomeKind::Progress},
  {"laserTypes", OutcomeKind::Progress},
  {"mission", OutcomeKind::Progress},
  {"missionStage", OutcomeKind::Progress},
  {"missionJumps", OutcomeKind::Progress},
  {"jumpedSinceBriefing", OutcomeKind::Progress},
  {"maskShipsLeft", OutcomeKind::Progress},
  {"maskShipDestroyed", OutcomeKind::Progress},
  {"maskingDeviceRecovered", OutcomeKind::Progress},
  {"fledMaskShip", OutcomeKind::Progress},
  {"maskSystemJumps", OutcomeKind::Progress},
  {"invasion", OutcomeKind::Progress},
  {"invadedStationDestroyed", OutcomeKind::Progress},
  {"energy", OutcomeKind::Timing},
  {"foreShield", OutcomeKind::Timing},
  {"aftShield", OutcomeKind::Timing},
  {"speed", OutcomeKind::Timing},
  {"fuel", OutcomeKind::Timing},
  {"shipSlotsInUse", OutcomeKind::Timing},
  {"cabinTemperature", OutcomeKind::Timing},
  {"laserTemperature", OutcomeKind::Timing},
  {"altitude", OutcomeKind::Timing},
  {"fuelLeakDelayFrames", OutcomeKind::Timing},
  {"fuelLeakFrames", OutcomeKind::Timing},
  {"supernovaFrames", OutcomeKind::Timing},
  {"supernovaHeat", OutcomeKind::Timing},
});

/// The value of the outcome _name in _state, as an outcome line writes it, or nothing for a name OUTCOMES does not hold. A
/// number is in decimal; cargo is the 17 commodities' holdings in cargoHold, in the market's order, separated by commas; the
/// title is commanderRankText's nine characters, any that is not printable, or is a space, '%' or '=', written %XX in hex.
///
/// This is the one function that knows where each outcome lives in the game's state, so that a change of its layout (Phase
/// 5's natural layout) changes only this function, and every recorded outcome still reads the same.
[[nodiscard]] std::optional<std::string> OutcomeValue(const GameState& _state, std::string_view _name);

/// Every outcome of _state: name=value for each of OUTCOMES, in their order, separated by single spaces.
[[nodiscard]] std::string OutcomeText(const GameState& _state);

/// What _text holds of outcome _name: its value, or nothing.
[[nodiscard]] std::optional<std::string_view> FindOutcome(std::string_view _text, std::string_view _name);

/// Whether _text is outcomes as OutcomeText writes them: name=value pairs separated by single spaces, each name one of
/// OUTCOMES and given once, and each value not empty. A text that lacks some of OUTCOMES is well formed; checking it reports
/// them as differences (CompareOutcomes). Returns false with _error saying what is wrong.
[[nodiscard]] bool CheckOutcomeText(std::string_view _text, std::string& _error);

/// An outcome whose value differs from the one recorded.
struct OutcomeDifference
{
  std::string_view name;
  OutcomeKind kind;
  std::optional<std::string> recorded; ///< nothing when the recorded outcomes do not hold it, which fails whatever its kind
  std::string actual;
};

/// The outcomes of _actual whose values differ from _recorded's, each of them outcome texts, in the order of OUTCOMES.
[[nodiscard]] std::vector<OutcomeDifference> CompareOutcomes(std::string_view _recorded, std::string_view _actual);

} // namespace Elite
