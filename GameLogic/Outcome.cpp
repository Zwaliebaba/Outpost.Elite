#include "pch.h"

#include "Outcome.h"

#include "DataOverlay.h"
#include "GameState.h"
#include "ObjectSlot.h"

#include <algorithm>
#include <format>

namespace Elite
{

namespace
{

// The commodities of cargoHold: entry 17, the last, holds refugeesTonnes and cargoUsedTonnes instead.
constexpr std::size_t COMMODITIES = 17;
// The slots of shipSlots: the sun and planet, the station, the ships and the debris.
constexpr std::size_t SHIP_SLOTS = DS.shipSlots.SIZE_BYTES / ObjectSlot::BYTES;
constexpr char FIRST_PRINTABLE = '!';
constexpr char LAST_PRINTABLE = '~';

[[nodiscard]] std::string Number(std::uint32_t _value)
{
  return std::to_string(_value);
}

// The amount of each commodity held, in the market's order, separated by commas.
[[nodiscard]] std::string Cargo(const GameState& _state)
{
  std::string cargo;
  for (std::size_t commodity = 0; commodity < COMMODITIES; ++commodity)
  {
    cargo += (commodity == 0 ? "" : ",") + Number(_state.Byte(DS.cargoHold.At(commodity)));
  }
  return cargo;
}

// commanderRankText's characters, any that would not survive in an outcome line as %XX.
[[nodiscard]] std::string Title(const GameState& _state)
{
  std::string title;
  for (std::size_t index = 0; index < DS.commanderRankText.SIZE_BYTES; ++index)
  {
    const auto character = static_cast<char>(_state.Byte(DS.commanderRankText.At(index)));
    if (character < FIRST_PRINTABLE || character > LAST_PRINTABLE || character == '%' || character == '=')
    {
      title += std::format("%{:02X}", static_cast<std::uint8_t>(character));
    }
    else
    {
      title += character;
    }
  }
  return title;
}

// The slots of the first shipSlotCount, as the game walks them (UpdateObjectsAndSpawn), whose type byte says they are active.
[[nodiscard]] std::string ShipSlotsInUse(const GameState& _state)
{
  const std::size_t slots = std::min<std::size_t>(_state.Get(DS.shipSlotCount), SHIP_SLOTS);
  std::uint32_t inUse = 0;
  for (std::size_t slot = 0; slot < slots; ++slot)
  {
    const std::uint8_t type = _state.Byte(DS.shipSlots.At(slot * ObjectSlot::BYTES + static_cast<std::size_t>(SlotByte::Type)));
    inUse += (type & ObjectSlot::ACTIVE) != 0 ? 1 : 0;
  }
  return Number(inUse);
}

// The words of _text, which are separated by single spaces.
[[nodiscard]] std::vector<std::string_view> Pairs(std::string_view _text)
{
  std::vector<std::string_view> pairs;
  while (!_text.empty())
  {
    const std::size_t space = _text.find(' ');
    pairs.push_back(_text.substr(0, space));
    _text = space == std::string_view::npos ? std::string_view{} : _text.substr(space + 1);
  }
  return pairs;
}

// The place of outcome _name in OUTCOMES, looked for first at _hint, where a text written in their order has it; or
// OUTCOMES.size() for a name that is not an outcome.
[[nodiscard]] std::size_t IndexOf(std::string_view _name, std::size_t _hint) noexcept
{
  if (_hint < OUTCOMES.size() && OUTCOMES[_hint].name == _name)
  {
    return _hint;
  }
  std::size_t index = 0;
  while (index < OUTCOMES.size() && OUTCOMES[index].name != _name)
  {
    ++index;
  }
  return index;
}

// What _text holds of each outcome, by its place in OUTCOMES; the last value given, if one is given twice, and nothing for
// a pair that is not an outcome's.
[[nodiscard]] std::array<std::optional<std::string_view>, OUTCOMES.size()> ValuesOf(std::string_view _text)
{
  std::array<std::optional<std::string_view>, OUTCOMES.size()> values{};
  std::size_t next = 0;
  for (const std::string_view pair : Pairs(_text))
  {
    const std::size_t equals = pair.find('=');
    if (equals == std::string_view::npos)
    {
      continue;
    }
    const std::size_t index = IndexOf(pair.substr(0, equals), next);
    if (index < OUTCOMES.size())
    {
      values[index] = pair.substr(equals + 1);
      next = index + 1;
    }
  }
  return values;
}

} // namespace

std::optional<std::string> OutcomeValue(const GameState& _state, std::string_view _name)
{
  using Reader = std::string (*)(const GameState&);
  struct Source
  {
    std::string_view name;
    Reader read;
  };
  static constexpr auto SOURCES = std::to_array<Source>({
    {"galaxy", [](const GameState& _game) { return Number(_game.Get(DS.galaxyNumber)); }},
    {"system", [](const GameState& _game) { return Number(_game.Get(DS.currentSystemIndex)); }},
    {"witchSpace", [](const GameState& _game) { return Number(_game.Get(DS.witchspaceCountdown)); }},
    {"docked", [](const GameState& _game) { return Number(_game.Get(DS.playerDocked)); }},
    {"inFlight", [](const GameState& _game) { return Number(_game.Get(DS.inFlight)); }},
    {"dead", [](const GameState& _game) { return Number(_game.Get(DS.playerDead)); }},
    {"creditsTenths",
     [](const GameState& _game) { return Number(_game.Word(DS.creditsTenths.offset) | (std::uint32_t{_game.Get(DS.data75F5)} << 16)); }},
    {"legalStatus", [](const GameState& _game) { return Number(_game.Get(DS.legalStatus)); }},
    {"kills", [](const GameState& _game) { return Number(_game.Get(DS.killCount)); }},
    {"title", &Title},
    {"cargo", &Cargo},
    {"refugeesTonnes", [](const GameState& _game) { return Number(_game.Get(DS.refugeesTonnes)); }},
    {"missiles", [](const GameState& _game) { return Number(_game.Get(DS.missileCount)); }},
    {"largeCargoBay", [](const GameState& _game) { return Number(_game.Get(DS.largeCargoBayFitted)); }},
    {"ecm", [](const GameState& _game) { return Number(_game.Get(DS.ecmFitted)); }},
    {"fuelScoops", [](const GameState& _game) { return Number(_game.Get(DS.fuelScoopsFitted)); }},
    {"escapePod", [](const GameState& _game) { return Number(_game.Get(DS.escapePodFitted)); }},
    {"energyBomb", [](const GameState& _game) { return Number(_game.Get(DS.energyBombFitted)); }},
    {"energyUnit", [](const GameState& _game) { return Number(_game.Get(DS.energyUnitFitted)); }},
    {"dockingComputer", [](const GameState& _game) { return Number(_game.Get(DS.dockingComputerFitted)); }},
    {"galacticHyperdrive", [](const GameState& _game) { return Number(_game.Get(DS.galacticHyperdriveFitted)); }},
    {"maskingDevice", [](const GameState& _game) { return Number(_game.Get(DS.maskingDeviceFitted)); }},
    {"antiEcm", [](const GameState& _game) { return Number(_game.Get(DS.antiEcmEmulatorFitted)); }},
    {"pulseLasers", [](const GameState& _game) { return Number(_game.Get(DS.pulseLaserCount)); }},
    {"beamLasers", [](const GameState& _game) { return Number(_game.Get(DS.beamLaserCount)); }},
    {"miningLasers", [](const GameState& _game) { return Number(_game.Get(DS.miningLaserCount)); }},
    {"militaryLasers", [](const GameState& _game) { return Number(_game.Get(DS.militaryLaserCount)); }},
    {"laserMounts", [](const GameState& _game) { return Number(_game.Get(DS.laserMountsFitted)); }},
    {"laserTypes", [](const GameState& _game) { return Number(_game.Get(DS.laserMountTypes)); }},
    {"mission", [](const GameState& _game) { return Number(_game.Get(DS.missionNumber)); }},
    {"missionStage", [](const GameState& _game) { return Number(_game.Get(DS.missionStage)); }},
    {"missionJumps", [](const GameState& _game) { return Number(_game.Get(DS.missionJumpCount)); }},
    {"jumpedSinceBriefing", [](const GameState& _game) { return Number(_game.Get(DS.jumpedSinceBriefing)); }},
    {"maskShipsLeft", [](const GameState& _game) { return Number(_game.Get(DS.maskMissionShipsLeft)); }},
    {"maskShipDestroyed", [](const GameState& _game) { return Number(_game.Get(DS.maskShipDestroyed)); }},
    {"maskingDeviceRecovered", [](const GameState& _game) { return Number(_game.Get(DS.maskingDeviceRecovered)); }},
    {"fledMaskShip", [](const GameState& _game) { return Number(_game.Get(DS.fledMaskShip)); }},
    {"maskSystemJumps", [](const GameState& _game) { return Number(_game.Get(DS.maskSystemJumps)); }},
    {"invasion", [](const GameState& _game) { return Number(_game.Get(DS.thargoidInvasionActive)); }},
    {"invadedStationDestroyed", [](const GameState& _game) { return Number(_game.Get(DS.invadedStationDestroyed)); }},
    {"energy", [](const GameState& _game) { return Number(_game.Get(DS.playerEnergy)); }},
    {"foreShield", [](const GameState& _game) { return Number(_game.Get(DS.foreShield)); }},
    {"aftShield", [](const GameState& _game) { return Number(_game.Get(DS.aftShield)); }},
    {"speed", [](const GameState& _game) { return Number(_game.Get(DS.playerSpeed)); }},
    {"fuel", [](const GameState& _game) { return Number(_game.Get(DS.fuel)); }},
    {"shipSlotsInUse", &ShipSlotsInUse},
    {"cabinTemperature", [](const GameState& _game) { return Number(_game.Get(DS.cabinTemperature)); }},
    {"laserTemperature", [](const GameState& _game) { return Number(_game.Get(DS.laserTemperature)); }},
    {"altitude", [](const GameState& _game) { return Number(_game.Get(DS.altitude)); }},
    {"fuelLeakDelayFrames", [](const GameState& _game) { return Number(_game.Get(DS.fuelLeakDelayFrames)); }},
    {"fuelLeakFrames", [](const GameState& _game) { return Number(_game.Get(DS.fuelLeakFrames)); }},
    {"supernovaFrames", [](const GameState& _game) { return Number(_game.Get(DS.supernovaFrames)); }},
    {"supernovaHeat", [](const GameState& _game) { return Number(_game.Get(DS.supernovaHeat)); }},
  });
  static_assert(
    []
    {
      if (SOURCES.size() != OUTCOMES.size())
      {
        return false;
      }
      for (std::size_t index = 0; index < SOURCES.size(); ++index)
      {
        if (SOURCES[index].name != OUTCOMES[index].name)
        {
          return false;
        }
      }
      return true;
    }(),
    "every outcome is read, in the order of OUTCOMES, and nothing else");
  for (const Source& source : SOURCES)
  {
    if (source.name == _name)
    {
      return source.read(_state);
    }
  }
  return std::nullopt;
}

std::string OutcomeText(const GameState& _state)
{
  std::string text;
  for (const OutcomeName& outcome : OUTCOMES)
  {
    text += std::format("{}{}={}", text.empty() ? "" : " ", outcome.name, OutcomeValue(_state, outcome.name).value_or(""));
  }
  return text;
}

std::optional<std::string_view> FindOutcome(std::string_view _text, std::string_view _name)
{
  for (const std::string_view pair : Pairs(_text))
  {
    if (pair.size() > _name.size() && pair.starts_with(_name) && pair[_name.size()] == '=')
    {
      return pair.substr(_name.size() + 1);
    }
  }
  return std::nullopt;
}

bool CheckOutcomeText(std::string_view _text, std::string& _error)
{
  // By place in OUTCOMES: a text in their order, as OutcomeText writes it, finds each name at the first place it looks.
  std::array<bool, OUTCOMES.size()> seen{};
  std::size_t next = 0;
  bool any = false;
  for (const std::string_view pair : Pairs(_text))
  {
    const std::size_t equals = pair.find('=');
    const std::string_view name = pair.substr(0, equals);
    if (equals == std::string_view::npos || equals + 1 == pair.size())
    {
      _error = std::format("'{}' is not a name=value pair", pair);
      return false;
    }
    const std::size_t index = IndexOf(name, next);
    if (index == OUTCOMES.size())
    {
      _error = std::format("'{}' is not an outcome", name);
      return false;
    }
    if (seen[index])
    {
      _error = std::format("'{}' is given twice", name);
      return false;
    }
    seen[index] = true;
    next = index + 1;
    any = true;
  }
  if (!any)
  {
    _error = "no outcome is given";
    return false;
  }
  return true;
}

std::vector<OutcomeDifference> CompareOutcomes(std::string_view _recorded, std::string_view _actual)
{
  const auto recordedValues = ValuesOf(_recorded);
  const auto actualValues = ValuesOf(_actual);
  std::vector<OutcomeDifference> differences;
  for (std::size_t index = 0; index < OUTCOMES.size(); ++index)
  {
    const std::optional<std::string_view>& recorded = recordedValues[index];
    const std::string actual(actualValues[index].value_or(""));
    if (!recorded || *recorded != actual)
    {
      differences.push_back(OutcomeDifference{OUTCOMES[index].name, OUTCOMES[index].kind,
                                              recorded ? std::optional<std::string>(*recorded) : std::nullopt, actual});
    }
  }
  return differences;
}

} // namespace Elite
