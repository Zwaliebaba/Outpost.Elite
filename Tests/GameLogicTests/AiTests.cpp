#include "pch.h"

#include "GameState.h"
#include "Maths.h"
#include "Repository.h"
#include "SpaceObject.h"
#include "Ships.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Commander = Elite::CommanderState;

// The twins' prepared commanders, from Replays/: commanders the original game saved, changed as its own code would change them.
constexpr std::string_view ARMED = "armed.cdr";
constexpr std::string_view INVASION_MISSION = "invasion-mission.cdr";
// From the title: the credits, the status screen, D10, and JAMESON loaded from its catalogue.
constexpr std::string_view LOAD_JAMESON = "key space; wait 4\nkey Escape; wait 0.3\nkey 1; wait 0.3\nkey Return; wait 0.5";
constexpr std::uint16_t FIRST_SHIP_SLOT = 3;
constexpr std::uint8_t TYPE_SHUTTLE = 0x08;
// LaunchAtOffender puts what it launches 0F0h along z from the station: a ship within this of it on every axis has just left it,
// where one SpawnRandomDrifter spawns is 10000 out.
constexpr std::int32_t LAUNCH_REACH = 0x800;

// Replays/_source, a prepared commander, copied into the twin's DOS directory as JAMESON.CDR, as a replay's file step copies
// one (DirectoryFileStore gives a file it did not write attribute 0 and DOS's stamp), and loaded at D10 as the game loads
// its own saves.
void LoadCommander(TwinRig& _rig, std::string_view _source)
{
  const std::filesystem::path source = Elite::FindInRepository(std::filesystem::path("Replays") / _source);
  Assert::IsFalse(source.empty(), L"the commander in Replays/");
  std::filesystem::copy_file(source, _rig.Files() / "JAMESON.CDR", std::filesystem::copy_options::overwrite_existing);
  _rig.Play(LOAD_JAMESON);
}

// The twin's random state _draws draws on, by NextRandom itself: a state some later moment of play reaches, as the prepared
// commanders' random states were stepped.
void DrawRandomNumbers(TwinRig& _rig, std::uint32_t _draws)
{
  Elite::GameState& state = _rig.State();
  for (std::uint32_t draw = 0; draw < _draws; ++draw)
    (void)Elite::NextRandom(state);
}

// The byte _member names, as the twin holds it.
template <typename Struct> [[nodiscard]] std::uint8_t TwinByte(TwinRig& _rig, DataByte<Struct> _member)
{
  std::uint8_t value = 0;
  _rig.Both([&value, _member](Elite::Host&, Elite::GameState& _state) { value = DataByteIn(_state, _member); });
  return value;
}

// Whether two 24-bit coordinates lie within LAUNCH_REACH of each other.
[[nodiscard]] bool WithinReach(std::uint32_t _first, std::uint32_t _second) noexcept
{
  constexpr std::uint32_t SIGN = 0x800000;
  const auto difference = static_cast<std::int32_t>(((_first - _second) & 0xFFFFFF) ^ SIGN) - static_cast<std::int32_t>(SIGN);
  return difference > -LAUNCH_REACH && difference < LAUNCH_REACH;
}

// The active ships of a type in the twin's ship slots: all of them, and those just launched from the station.
struct ShipCount
{
  int all;
  int fromStation;
};

[[nodiscard]] ShipCount CountShips(TwinRig& _rig, std::uint8_t _type)
{
  ShipCount count{0, 0};
  Elite::GameState& state = _rig.State();
  const Elite::SpaceObject& station = state.Slot(Elite::SlotIndex::Station);
  for (std::uint16_t index = FIRST_SHIP_SLOT; index < state.Data().flight.objectSlotCount; ++index)
  {
    const Elite::SpaceObject& ship = state.Slot(static_cast<Elite::SlotIndex>(index));
    const std::uint8_t type = ship.type;
    if ((type & Elite::SLOT_ACTIVE) == 0 || ((type >> 1) & Elite::TYPE_MASK) != _type)
      continue;
    ++count.all;
    if (WithinReach(ship.PositionX(), station.PositionX()) && WithinReach(ship.PositionY(), station.PositionY()) &&
        WithinReach(ship.PositionZ(), station.PositionZ()))
      ++count.fromStation;
  }
  return count;
}

} // namespace

// Twins for the ship behaviour the replays do not reach (ADR-016).
TEST_CLASS(AiTests)
{
public:
  // A Shuttle the station launches (ADR-008 item 8, D20). UpdateStationAi launches a ship (LaunchAtOffender) when the station is
  // angry, near, and the player outside the box it guards and an offender: at odds of 90 in 65536 a frame, or 2000 for a
  // fugitive; a police Viper for a random word from 10000, a Shuttle from 5000, and a trader below. From armed.cdr, as
  // attack-the-station.replay starts, with its random state 492 draws on, where a search of 2,000 counts found the first launch a
  // Shuttle: launched, the aft pulse laser on the station in the rear view angers it and makes the commander a fugitive
  // (HitTarget), and the station launches a Shuttle, then a police Viper.
  TEST_METHOD(StationLaunchesAShuttle)
  {
    TwinRig rig("TwinStationShuttle");
    LoadCommander(rig, ARMED);
    DrawRandomNumbers(rig, 492);
    rig.Play("key F1; wait 1\ndown F2; wait 0.1; up F2; wait 0.3\ndown space; wait 0.5; up space\ndigest fired");
    Assert::AreEqual(1, CountShips(rig, TYPE_SHUTTLE).fromStation, L"a Shuttle launched");
    rig.Play("wait 1\ndigest launched");
    Assert::AreEqual(1, CountShips(rig, Elite::TYPE_VIPER).fromStation, L"then a police Viper");
  }

  // An invasion's Thargoids (ADR-008 item 8, D20). UpdateObjectsAndSpawn spawns one a frame (SpawnInvasionWave,
  // SpawnInvasionThargoid) while the invasion is on, a jump has been made since its briefing, the station stands and the player
  // is in the safe zone, until eight wolves fly. From invasion-mission.cdr, as invasion-mission.replay starts: the galactic jump
  // that gives the third mission; the escape capsule to the station, whose status screen briefs it; fuel, which the mission's
  // leak emptied, and another capsule; Atorat chosen on the short-range chart and jumped to; the capsule to Atorat's station; and
  // the launch from it, into the safe zone, where the Thargoids come.
  TEST_METHOD(InvasionThargoidsSpawnInTheSafeZone)
  {
    TwinRig rig("TwinInvasion");
    LoadCommander(rig, INVASION_MISSION);
    rig.Play("key F1; wait 0.5\ndown g; wait 1; up g\ndown h; wait 0.1; up h\nwait 8\ndigest mission-given");
    rig.Play("down c; wait 0.1; up c\nwait 5\ndigest briefing\nkey space; wait 1");
    Assert::AreEqual(std::uint8_t{1}, TwinByte(rig, &Commander::thargoidInvasionActive), L"the invasion on");
    // Equip Ship, its cursor on Fuel: the fuel, and seven rows down an escape capsule.
    rig.Play("key F3; wait 0.8\nkey b; wait 0.3\n"
             "key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15\n"
             "key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15\nkey b; wait 0.3\ndigest bought");
    // The short-range chart at the moment recorded (ADR-064).
    rig.Play("wait 0.05\nkey F5; wait 0.95\nclick 108 46; wait 0.649\nkey F1; wait 0.951\ndown h; wait 0.2; up h\nwait 7.5\n"
             "digest arrived");
    Assert::AreEqual(std::uint8_t{1}, TwinByte(rig, &Commander::jumpedSinceBriefing), L"a jump since the briefing");
    rig.Play("down c; wait 0.1; up c\nwait 5\nkey F1; wait 1.5\ndigest launched");
    Assert::IsTrue(CountShips(rig, Elite::TYPE_THARGOID).all != 0, L"the first Thargoid");
    rig.Play("wait 1\ndigest invaded");
    Assert::IsTrue(CountShips(rig, Elite::TYPE_THARGOID).all > 1, L"and more");
  }
};

} // namespace GameLogicTests
