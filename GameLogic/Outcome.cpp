#include "pch.h"

#include "Outcome.h"

#include "DataOverlay.h"
#include "GameState.h"
#include "SpaceObject.h"

#include <algorithm>
#include <format>

namespace Elite
{

namespace
{

// The commodities of cargoHold: entry 17, the last, holds refugeesTonnes and cargoUsedTonnes instead.
constexpr std::size_t COMMODITIES = 17;
// The slots of shipSlots: the sun and planet, the station, the ships and the debris.
constexpr std::size_t SHIP_SLOTS = DS.shipSlots.SIZE_BYTES / SpaceObject::BYTES;
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
    const auto character = static_cast<char>(_state.Data().commanderBlock.commanderRankText[index]);
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
  const std::size_t slots = std::min<std::size_t>(_state.Data().shipSlotCount, SHIP_SLOTS);
  std::uint32_t inUse = 0;
  for (std::size_t slot = 0; slot < slots; ++slot)
  {
    const std::uint8_t type = _state.ObjectAt(DS.shipSlots.At(slot * SpaceObject::BYTES)).type;
    inUse += (type & SpaceObject::ACTIVE) != 0 ? 1 : 0;
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
    {"galaxy", [](const GameState& _game) { return Number(_game.Data().commanderBlock.galaxyNumber); }},
    {"system", [](const GameState& _game) { return Number(_game.Data().commanderBlock.currentSystemIndex); }},
    {"witchSpace", [](const GameState& _game) { return Number(_game.Data().commanderBlock.witchspaceCountdown); }},
    {"docked", [](const GameState& _game) { return Number(_game.Data().playerDocked); }},
    {"inFlight", [](const GameState& _game) { return Number(_game.Data().inFlight); }},
    {"dead", [](const GameState& _game) { return Number(_game.Data().playerDead); }},
    {"creditsTenths", [](const GameState& _game)
     { return Number(_game.Word(DS.creditsTenths.offset) | (std::uint32_t{_game.Data().commanderBlock.creditsTenths.data75F5} << 16)); }},
    {"legalStatus", [](const GameState& _game) { return Number(_game.Data().commanderBlock.legalStatus); }},
    {"kills", [](const GameState& _game) { return Number(_game.Data().commanderBlock.killCount); }},
    {"title", &Title},
    {"cargo", &Cargo},
    {"refugeesTonnes", [](const GameState& _game) { return Number(_game.Data().commanderBlock.cargoHold.refugeesTonnes); }},
    {"missiles", [](const GameState& _game) { return Number(_game.Data().commanderBlock.missileCount); }},
    {"largeCargoBay", [](const GameState& _game) { return Number(_game.Data().commanderBlock.largeCargoBayFitted); }},
    {"ecm", [](const GameState& _game) { return Number(_game.Data().commanderBlock.ecmFitted); }},
    {"fuelScoops", [](const GameState& _game) { return Number(_game.Data().commanderBlock.fuelScoopsFitted); }},
    {"escapePod", [](const GameState& _game) { return Number(_game.Data().commanderBlock.escapePodFitted); }},
    {"energyBomb", [](const GameState& _game) { return Number(_game.Data().commanderBlock.energyBombFitted); }},
    {"energyUnit", [](const GameState& _game) { return Number(_game.Data().commanderBlock.energyUnitFitted); }},
    {"dockingComputer", [](const GameState& _game) { return Number(_game.Data().commanderBlock.dockingComputerFitted); }},
    {"galacticHyperdrive", [](const GameState& _game) { return Number(_game.Data().commanderBlock.galacticHyperdriveFitted); }},
    {"maskingDevice", [](const GameState& _game) { return Number(_game.Data().commanderBlock.maskingDeviceFitted); }},
    {"antiEcm", [](const GameState& _game) { return Number(_game.Data().commanderBlock.antiEcmEmulatorFitted); }},
    {"pulseLasers", [](const GameState& _game) { return Number(_game.Data().commanderBlock.pulseLaserCount); }},
    {"beamLasers", [](const GameState& _game) { return Number(_game.Data().commanderBlock.beamLaserCount); }},
    {"miningLasers", [](const GameState& _game) { return Number(_game.Data().commanderBlock.miningLaserCount); }},
    {"militaryLasers", [](const GameState& _game) { return Number(_game.Data().commanderBlock.militaryLaserCount); }},
    {"laserMounts", [](const GameState& _game) { return Number(_game.Data().commanderBlock.laserMountsFitted); }},
    {"laserTypes", [](const GameState& _game) { return Number(_game.Data().commanderBlock.laserMountTypes); }},
    {"mission", [](const GameState& _game) { return Number(_game.Data().commanderBlock.missionNumber); }},
    {"missionStage", [](const GameState& _game) { return Number(_game.Data().commanderBlock.missionStage); }},
    {"missionJumps", [](const GameState& _game) { return Number(_game.Data().commanderBlock.missionJumpCount); }},
    {"jumpedSinceBriefing", [](const GameState& _game) { return Number(_game.Data().commanderBlock.jumpedSinceBriefing); }},
    {"maskShipsLeft", [](const GameState& _game) { return Number(_game.Data().commanderBlock.maskMissionShipsLeft); }},
    {"maskShipDestroyed", [](const GameState& _game) { return Number(_game.Data().commanderBlock.maskShipDestroyed); }},
    {"maskingDeviceRecovered", [](const GameState& _game) { return Number(_game.Data().commanderBlock.maskingDeviceRecovered); }},
    {"fledMaskShip", [](const GameState& _game) { return Number(_game.Data().commanderBlock.fledMaskShip); }},
    {"maskSystemJumps", [](const GameState& _game) { return Number(_game.Data().commanderBlock.maskSystemJumps); }},
    {"invasion", [](const GameState& _game) { return Number(_game.Data().commanderBlock.thargoidInvasionActive); }},
    {"invadedStationDestroyed", [](const GameState& _game) { return Number(_game.Data().commanderBlock.invadedStationDestroyed); }},
    {"energy", [](const GameState& _game) { return Number(_game.Data().playerEnergy); }},
    {"foreShield", [](const GameState& _game) { return Number(_game.Data().foreShield); }},
    {"aftShield", [](const GameState& _game) { return Number(_game.Data().aftShield); }},
    {"speed", [](const GameState& _game) { return Number(_game.Data().playerSpeed); }},
    {"fuel", [](const GameState& _game) { return Number(_game.Data().commanderBlock.fuel); }},
    {"shipSlotsInUse", &ShipSlotsInUse},
    {"cabinTemperature", [](const GameState& _game) { return Number(_game.Data().cabinTemperature); }},
    {"laserTemperature", [](const GameState& _game) { return Number(_game.Data().laserTemperature); }},
    {"altitude", [](const GameState& _game) { return Number(_game.Data().altitude); }},
    {"fuelLeakDelayFrames", [](const GameState& _game) { return Number(_game.Data().commanderBlock.fuelLeakDelayFrames); }},
    {"fuelLeakFrames", [](const GameState& _game) { return Number(_game.Data().commanderBlock.fuelLeakFrames); }},
    {"supernovaFrames", [](const GameState& _game) { return Number(_game.Data().commanderBlock.supernovaFrames); }},
    {"supernovaHeat", [](const GameState& _game) { return Number(_game.Data().commanderBlock.supernovaHeat); }},
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
