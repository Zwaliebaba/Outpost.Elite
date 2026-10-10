#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "GameState.h"
#include "Maths.h"
#include "ObjectSlot.h"
#include "Ships.h"
#include "TwinRig.h"

#include <functional>
#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t UPDATE_OBJECTS_AND_SPAWN = 0x4A10;
constexpr std::uint16_t SCALE_SPAWN_ODDS = 0x4C0E;
constexpr std::uint16_t IS_MASK_SHIP_PRESENT = 0x4C20;
constexpr std::uint16_t PLACE_ESCORT_NEAR = 0x4C38;
constexpr std::uint16_t TURN_TOWARD_ANGLES = 0x514B;
constexpr std::uint16_t GET_VECTOR_TO_OBJECT = 0x54B4;
constexpr std::uint16_t UPDATE_STATION_AI = 0x5595;
constexpr std::uint16_t UPDATE_DRIFTING_OBJECT_AI = 0x5681;
constexpr std::uint16_t UPDATE_TRADER_OR_POLICE_AI = 0x569C;
constexpr std::uint16_t UPDATE_WOLF_AI = 0x57E8;
constexpr std::uint16_t UPDATE_HUNTER_AI = 0x58DE;
constexpr std::uint16_t CHECK_SAFE_ZONE_HOLD_FIRE = 0x5A10;

constexpr std::uint8_t ALL_SLOTS = 36;
constexpr std::uint8_t OBJECT_SLOTS = 20;
constexpr std::uint8_t DEBRIS_SLOTS = 16;
constexpr int STATION_SLOT = 2;
constexpr std::uint8_t TYPE_COBRA = 0x0E;
constexpr std::uint8_t STATION_CLASS = 1;
constexpr std::uint8_t DRIFTER_CLASS = 3;
constexpr std::uint8_t TRADER_CLASS = 4;
constexpr std::uint8_t WOLF_CLASS = 5;
constexpr std::uint8_t HUNTER_CLASS = 6;
constexpr std::uint8_t DEBRIS_CLASS = 7;
constexpr std::uint8_t HOSTILE = 0x01;
constexpr std::uint8_t BLIP_DRAWN = 0x02;
constexpr std::uint8_t CARRIES_DEVICE = 0x20;

/// The reference's data segment in a rig, where the slots and variables a routine reads are set up before a call.
class Space
{
public:
  explicit Space(ComparisonRig& _rig) noexcept
    : m_memory(_rig.Host().Ram()),
      m_segment(Elite::DataSegment(_rig.Program()))
  {
  }

  void SetByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    m_memory.Write8(m_segment, _offset, _value);
  }

  void SetWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    m_memory.Write16(m_segment, _offset, _value);
  }

  void Set(Elite::DataField<std::uint8_t> _field, std::uint8_t _value) noexcept
  {
    SetByte(_field.offset, _value);
  }

  void Set(Elite::DataField<std::uint16_t> _field, std::uint16_t _value) noexcept
  {
    SetWord(_field.offset, _value);
  }

  void SetField(std::uint16_t _slot, std::uint16_t _field, std::uint8_t _value) noexcept
  {
    SetByte(static_cast<std::uint16_t>(_slot + _field), _value);
  }

  void SetFieldWord(std::uint16_t _slot, std::uint16_t _field, std::uint16_t _value) noexcept
  {
    SetWord(static_cast<std::uint16_t>(_slot + _field), _value);
  }

  /// Every slot cleared, the slot counts SetUpLocalSpace gives, and none of the states that stop ships firing.
  void Clear() noexcept
  {
    for (int offset = 0; offset < ALL_SLOTS * Elite::SLOT_BYTES; ++offset)
      SetByte(static_cast<std::uint16_t>(DS.shipSlots.offset + offset), 0);
    Set(DS.shipSlotCount, ALL_SLOTS);
    Set(DS.objectSlotCount, OBJECT_SLOTS);
    Set(DS.debrisSlotCount, DEBRIS_SLOTS);
    Set(DS.gameOverFrames, 0);
    Set(DS.escapePodFrames, 0);
    Set(DS.maskingBackgroundColor, 0);
    Set(DS.npcEcmFrames, 0);
  }

  /// NextRandom's next two results.
  void Random(std::uint16_t _first, std::uint16_t _second) noexcept
  {
    Set(DS.randomState0, _first);
    Set(DS.randomState1, 0);
    Set(DS.randomState2, _second);
  }

  /// Slot _index made an active object of _type and behaviour class _class at the signed position (_x, _y, _z). Out: its
  /// offset.
  std::uint16_t Ship(int _index, std::uint8_t _type, std::uint8_t _class, std::int16_t _x, std::int16_t _y, std::int16_t _z) noexcept
  {
    const auto slot = static_cast<std::uint16_t>(DS.shipSlots.offset + _index * Elite::SLOT_BYTES);
    SetByte(slot, static_cast<std::uint8_t>((_type << 1) | Elite::SLOT_ACTIVE));
    SetField(slot, Elite::SLOT_CLASS, _class);
    const std::int16_t position[] = {_x, _y, _z};
    for (int axis = 0; axis < 3; ++axis)
    {
      SetFieldWord(slot, static_cast<std::uint16_t>(Elite::SLOT_X + 2 * axis), static_cast<std::uint16_t>(position[axis]));
      SetField(slot, static_cast<std::uint16_t>(Elite::SLOT_X_HIGH + axis), position[axis] < 0 ? std::uint8_t{0xFF} : std::uint8_t{0});
    }
    SetField(slot, Elite::SLOT_SPEED, 20);
    SetField(slot, Elite::SLOT_TURN_RATE, 8);
    return slot;
  }

  /// Every ship slot from slot 3 taken by a fragment, which UpdateObjectsAndSpawn does not count as active.
  void FillWithDebris() noexcept
  {
    for (int index = 3; index < OBJECT_SLOTS; ++index)
    {
      const std::uint16_t slot = Ship(index, Elite::TYPE_SPLINTER, DEBRIS_CLASS, 0, 0, 0x4000);
      SetField(slot, Elite::SLOT_LIFETIME, 50);
    }
  }

  /// This government's spawn limits (classes 3, 4, 6, 5) and odds out of 65536.
  void SpawnTables(std::uint8_t _government, const std::uint8_t (&_limits)[4], const std::uint16_t (&_odds)[4]) noexcept
  {
    Set(DS.currentGovernment, _government);
    Set(DS.spawnGovernment, _government);
    for (int column = 0; column < 4; ++column)
    {
      SetByte(static_cast<std::uint16_t>(DS.spawnLimitsByGovernment.At(_government) + column), _limits[column]);
      SetWord(static_cast<std::uint16_t>(DS.spawnOddsByGovernment.At(_government) + 2 * column), _odds[column]);
    }
  }

private:
  Machine::Memory& m_memory;
  std::uint16_t m_segment;
};

[[nodiscard]] std::uint16_t Slot(int _index) noexcept
{
  return static_cast<std::uint16_t>(DS.shipSlots.offset + _index * Elite::SLOT_BYTES);
}

// The twins' prepared commanders, from Replays/ (Tools/PrepareCommanders.py).
constexpr std::string_view ARMED = "armed.cdr";
constexpr std::string_view INVASION_MISSION = "invasion-mission.cdr";
// From the title: the credits, the status screen, the disc menu, and JAMESON loaded.
constexpr std::string_view LOAD_JAMESON = "key space; wait 4\nkey Escape; wait 0.3\nkey l; wait 0.3\n"
                                          "key j; key a; key m; key e; key s; key o; key n; key Return; wait 0.5";
constexpr std::uint16_t FIRST_SHIP_SLOT = 3;
constexpr std::uint8_t TYPE_SHUTTLE = 0x08;
// LaunchAtOffender puts what it launches 0F0h along z from the station: a ship within this of it on every axis has just left it,
// where one SpawnRandomDrifter spawns is 10000 out.
constexpr std::int32_t LAUNCH_REACH = 0x800;

// Replays/_source, a prepared commander, copied into both twins' DOS directories as JAMESON.CDR, as a replay's file step copies
// one (DirectoryFileStore gives a file it did not write attribute 0 and DOS's stamp), and loaded at the disc menu as the game
// loads its own saves.
void LoadCommander(TwinRig& _rig, std::string_view _source)
{
  const std::filesystem::path source = Elite::FindInRepository(std::filesystem::path("Replays") / _source);
  Assert::IsFalse(source.empty(), L"the commander in Replays/");
  for (const bool native : {false, true})
    std::filesystem::copy_file(source, _rig.Files(native) / "JAMESON.CDR", std::filesystem::copy_options::overwrite_existing);
  _rig.Play(LOAD_JAMESON);
}

// Both twins' random state _draws draws on, by NextRandom itself: a state some later moment of play reaches, as
// Tools/PrepareCommanders.py steps a commander's.
void DrawRandomNumbers(TwinRig& _rig, std::uint32_t _draws)
{
  _rig.Both(
    [_draws](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      Elite::GameState state(_pc.Ram(), _program.loadSegment, Elite::DataSegment(_program));
      for (std::uint32_t draw = 0; draw < _draws; ++draw)
        (void)Elite::NextRandom(state);
    });
}

// The byte _field as the native twin holds it; the interpreted twin's is the same, or the digests would differ.
[[nodiscard]] std::uint8_t TwinByte(TwinRig& _rig, Elite::DataField<std::uint8_t> _field)
{
  std::uint8_t value = 0;
  _rig.Both([&value, _field](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { value = _pc.Ram().Read8(Elite::DataSegment(_program), _field.offset); });
  return value;
}

// Whether two 24-bit coordinates lie within LAUNCH_REACH of each other.
[[nodiscard]] bool WithinReach(std::uint32_t _first, std::uint32_t _second) noexcept
{
  constexpr std::uint32_t SIGN = 0x800000;
  const auto difference = static_cast<std::int32_t>(((_first - _second) & 0xFFFFFF) ^ SIGN) - static_cast<std::int32_t>(SIGN);
  return difference > -LAUNCH_REACH && difference < LAUNCH_REACH;
}

// The active ships of a type in the native twin's ship slots: all of them, and those just launched from the station.
struct ShipCount
{
  int all;
  int fromStation;
};

[[nodiscard]] ShipCount CountShips(TwinRig& _rig, std::uint8_t _type)
{
  ShipCount count{0, 0};
  _rig.Both(
    [&count, _type](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      count = ShipCount{0, 0};
      Elite::GameState state(_pc.Ram(), _program.loadSegment, Elite::DataSegment(_program));
      const Elite::ObjectSlot station(state, DS.stationSlot.offset);
      for (std::uint16_t index = FIRST_SHIP_SLOT; index < state.Get(DS.objectSlotCount); ++index)
      {
        const Elite::ObjectSlot ship(state, static_cast<std::uint16_t>(DS.shipSlots.offset + index * Elite::SLOT_BYTES));
        const std::uint8_t type = ship.Get(Elite::SlotByte::Type);
        if ((type & Elite::SLOT_ACTIVE) == 0 || ((type >> 1) & Elite::TYPE_MASK) != _type)
          continue;
        ++count.all;
        if (WithinReach(ship.PositionX(), station.PositionX()) && WithinReach(ship.PositionY(), station.PositionY()) &&
            WithinReach(ship.PositionZ(), station.PositionZ()))
          ++count.fromStation;
      }
    });
  return count;
}

/// The missions, invasions and witch space that change what spawns, all off.
void Ordinary(Space& _space) noexcept
{
  _space.Set(DS.maskMissionShipsLeft, 0);
  _space.Set(DS.maskSystemJumps, 0);
  _space.Set(DS.maskShipDestroyed, 0);
  _space.Set(DS.thargoidInvasionActive, 0);
  _space.Set(DS.jumpedSinceBriefing, 0);
  _space.Set(DS.invadedStationDestroyed, 0);
  _space.Set(DS.witchspaceCountdown, 0);
  _space.Set(DS.miningLaserCount, 0);
  _space.Set(DS.jumpDriveEngaged, 0);
  _space.Set(DS.safeZoneFlags, 0);
  _space.Set(DS.legalStatus, 0);
}

} // namespace

// Constructed inputs for the ported ship behaviour (plan §6.3): the branches the replays do not reach.
TEST_CLASS(AiTests)
{
public:
  // What spawns: an invasion's Thargoids, the mask mission's ships, witch space's Thargoids, and each class by the
  // government's limits and odds, with and without a free slot.
  TEST_METHOD(UpdateObjectsAndSpawnAgreesOnEverySpawn)
  {
    ComparisonRig rig("AiSpawn");
    Space space(rig);
    const std::uint8_t noLimits[4] = {0, 0, 0, 0};
    const std::uint8_t allLimits[4] = {5, 5, 5, 5};
    const std::uint16_t always[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    const std::uint16_t never[4] = {0, 0, 0, 0};
    std::uint64_t calls = 0;
    const auto run = [&](const std::function<void()>& _setUp)
    {
      space.Clear();
      Ordinary(space);
      space.SpawnTables(2, noLimits, never);
      _setUp();
      rig.Call(UPDATE_OBJECTS_AND_SPAWN, {});
      ++calls;
    };
    const auto wolves = [&](int _count)
    {
      for (int index = 0; index < _count; ++index)
        space.Ship(5 + index, TYPE_COBRA, WOLF_CLASS, static_cast<std::int16_t>(0x400 * index), 0x300, 0x2000);
    };

    // Invasions.
    run(
      [&]
      {
        space.Set(DS.thargoidInvasionActive, 1);
        space.Set(DS.jumpedSinceBriefing, 1);
        space.Set(DS.safeZoneFlags, 1);
      });
    run(
      [&]
      {
        space.Set(DS.thargoidInvasionActive, 1);
        space.Set(DS.jumpedSinceBriefing, 1);
      });
    run(
      [&]
      {
        space.Set(DS.thargoidInvasionActive, 1);
        space.Set(DS.jumpedSinceBriefing, 1);
        space.Set(DS.safeZoneFlags, 1);
        wolves(8);
      });
    run(
      [&]
      {
        space.Set(DS.thargoidInvasionActive, 1);
        space.Set(DS.jumpedSinceBriefing, 1);
        space.Set(DS.safeZoneFlags, 1);
        space.FillWithDebris();
      });
    run(
      [&]
      {
        space.Set(DS.thargoidInvasionActive, 1);
        space.Set(DS.jumpedSinceBriefing, 1);
        space.Set(DS.invadedStationDestroyed, 1);
      });

    // The mask mission.
    for (const std::uint16_t random : std::initializer_list<std::uint16_t>{0x1234, 0x9234})
      run(
        [&]
        {
          space.Set(DS.maskMissionShipsLeft, 3);
          space.Set(DS.maskSystemJumps, 1);
          space.Random(random, static_cast<std::uint16_t>(~random));
        });
    run(
      [&]
      {
        space.Set(DS.maskMissionShipsLeft, 3);
        space.Set(DS.maskSystemJumps, 1);
        wolves(1);
      });
    run(
      [&]
      {
        space.Set(DS.maskMissionShipsLeft, 3);
        space.Set(DS.maskSystemJumps, 1);
        wolves(2);
        space.SetField(Slot(6), Elite::SLOT_FLAGS, CARRIES_DEVICE);
      });
    run(
      [&]
      {
        space.Set(DS.maskMissionShipsLeft, 3);
        space.Set(DS.maskSystemJumps, 1);
        wolves(3);
      });
    run(
      [&]
      {
        space.Set(DS.maskMissionShipsLeft, 3);
        space.Set(DS.maskSystemJumps, 1);
        space.Set(DS.maskShipDestroyed, 1);
      });
    run(
      [&]
      {
        space.Set(DS.maskMissionShipsLeft, 3);
        space.Set(DS.maskSystemJumps, 1);
        space.FillWithDebris();
      });
    run(
      [&]
      {
        space.Set(DS.maskMissionShipsLeft, 3);
        space.Set(DS.maskSystemJumps, 1);
        wolves(1);
        space.FillWithDebris();
      });
    run(
      [&]
      {
        space.Set(DS.maskMissionShipsLeft, 3);
        space.Set(DS.maskSystemJumps, 1);
        for (int index = 3; index < 18; ++index)
          space.Ship(index, Elite::TYPE_SPLINTER, DEBRIS_CLASS, 0, 0, 0x4000);
      });
    run(
      [&]
      {
        space.Set(DS.maskMissionShipsLeft, 3);
        space.Set(DS.maskSystemJumps, 2);
        space.SpawnTables(2, allLimits, always);
      });

    // Witch space, and each class by the government's tables.
    run(
      [&]
      {
        space.Set(DS.witchspaceCountdown, 50);
        space.SpawnTables(0, allLimits, never);
      });
    for (const std::uint16_t random : std::initializer_list<std::uint16_t>{0x0000, 0x0100, 0x8000, 0xFFF0})
      run(
        [&]
        {
          space.SpawnTables(3, allLimits, always);
          space.Set(DS.miningLaserCount, 1);
          space.Random(random, static_cast<std::uint16_t>(random + 0x777));
        });
    run(
      [&]
      {
        space.SpawnTables(0, allLimits, always);
        space.Set(DS.jumpDriveEngaged, 1);
      });
    const std::uint8_t onlyDrifters[4] = {5, 0, 0, 0};
    const std::uint8_t onlyTraders[4] = {0, 5, 0, 0};
    const std::uint8_t onlyHunters[4] = {0, 0, 5, 0};
    const std::uint8_t onlyWolves[4] = {0, 0, 0, 5};
    for (const auto* limits : {&onlyDrifters, &onlyTraders, &onlyHunters, &onlyWolves})
    {
      run(
        [&]
        {
          space.SpawnTables(4, *limits, always);
          space.FillWithDebris();
        });
    }
    // The trader's record is the second random byte / 43: from D7h, a Viper, which may be police.
    for (const std::uint16_t random : std::initializer_list<std::uint16_t>{0x0003, 0x00D8, 0x00E0, 0x00F7, 0x00FF, 0x01E5})
      run(
        [&]
        {
          space.SpawnTables(1, onlyTraders, always);
          space.Random(0, random);
        });
    for (int count = 9; count <= 10; ++count)
      run(
        [&]
        {
          for (int index = 0; index < count; ++index)
            space.Ship(3 + index, Elite::TYPE_ASTEROID, DRIFTER_CLASS, static_cast<std::int16_t>(0x200 * index), 0x100, 0x3000);
          space.SpawnTables(4, allLimits, always);
        });
    run(
      [&]
      {
        space.Set(DS.objectSlotCount, 2);
        space.Ship(0, Elite::TYPE_SUN, 0, 0, 0, 0x7000);
        space.Ship(1, Elite::TYPE_PLANET, 0, 0, 0, 0x6000);
        space.Ship(3, Elite::TYPE_ASTEROID, DRIFTER_CLASS, 0, 0, 0x3000);
      });
    rig.AssertAllAgreed(UPDATE_OBJECTS_AND_SPAWN, calls);
  }

  TEST_METHOD(ScaleSpawnOddsAgreesWithTheJumpDrive)
  {
    ComparisonRig rig("AiScaleOdds");
    Space space(rig);
    for (const std::uint8_t engaged : std::initializer_list<std::uint8_t>{0, 1, 2})
    {
      space.Set(DS.jumpDriveEngaged, engaged);
      rig.Call(SCALE_SPAWN_ODDS, {.bx = 0x1234});
    }
    rig.AssertAllAgreed(SCALE_SPAWN_ODDS, 3);
  }

  // The station launches at an offender out of the box it guards, by legal status and chance, and answers missiles aimed
  // at it, or at the police in the safe zone, with its ECM.
  TEST_METHOD(UpdateStationAiAgreesOnLaunchesAndMissiles)
  {
    ComparisonRig rig("AiStation");
    Space space(rig);
    std::uint64_t calls = 0;
    const auto run = [&](const std::function<void(std::uint16_t)>& _setUp)
    {
      space.Clear();
      Ordinary(space);
      space.Set(DS.spawnGovernment, 1);
      space.Set(DS.antiEcmActive, 0);
      const std::uint16_t station = space.Ship(STATION_SLOT, Elite::TYPE_CORIOLIS, STATION_CLASS, 0x1000, -0x200, 0x800);
      space.SetField(station, Elite::SLOT_FLAGS, HOSTILE);
      _setUp(station);
      rig.Call(UPDATE_STATION_AI, {.di = station});
      ++calls;
    };
    for (const std::uint16_t kind : std::initializer_list<std::uint16_t>{0x3000, 0x2000, 0x0100})
      run(
        [&](std::uint16_t)
        {
          space.Set(DS.legalStatus, 0x30);
          space.Random(0x0010, kind);
        });
    run(
      [&](std::uint16_t)
      {
        space.Set(DS.legalStatus, 0x30);
        space.Random(0x0800, 0);
      });
    run(
      [&](std::uint16_t)
      {
        space.Set(DS.legalStatus, 0x10);
        space.Random(0x0010, 0x0100);
      });
    run(
      [&](std::uint16_t)
      {
        space.Set(DS.legalStatus, 0x10);
        space.Random(0x0060, 0x0100);
      });
    run([&](std::uint16_t) { space.Set(DS.legalStatus, 0x05); });
    run(
      [&](std::uint16_t _station)
      {
        space.Set(DS.legalStatus, 0x30);
        space.SetFieldWord(_station, Elite::SLOT_X, 0x100);
        space.SetFieldWord(_station, Elite::SLOT_Y, 0x100);
        space.SetFieldWord(_station, Elite::SLOT_Z, 0x100);
      });
    run(
      [&](std::uint16_t _station)
      {
        space.Set(DS.legalStatus, 0x30);
        space.SetField(_station, Elite::SLOT_X_HIGH, 1);
      });
    run(
      [&](std::uint16_t)
      {
        space.Set(DS.legalStatus, 0x30);
        space.Random(0x0010, 0x3000);
        for (int index = 3; index < OBJECT_SLOTS; ++index)
          space.Ship(index, Elite::TYPE_SPLINTER, DEBRIS_CLASS, 0, 0, 0x4000);
      });

    // Missiles: at the station, at the police in and out of the safe zone, at a trader, at nothing.
    const auto missile = [&](int _index, std::uint16_t _target)
    {
      const std::uint16_t slot = space.Ship(_index, Elite::TYPE_MISSILE, 2, 0x100, 0x100, 0x100);
      space.SetFieldWord(slot, Elite::SLOT_TARGET, _target);
    };
    const auto police = [&](int _index)
    {
      const std::uint16_t slot = space.Ship(_index, Elite::TYPE_VIPER, 4, 0x200, 0x100, 0x100);
      space.SetFieldWord(slot, Elite::SLOT_OWNER, 1);
      return slot;
    };
    run(
      [&](std::uint16_t _station)
      {
        missile(5, _station);
        space.Set(DS.legalStatus, 0xF0);
      });
    run(
      [&](std::uint16_t _station)
      {
        missile(5, _station);
        space.Set(DS.antiEcmActive, 1);
      });
    run(
      [&](std::uint16_t _station)
      {
        missile(5, _station);
        space.Set(DS.thargoidInvasionActive, 1);
      });
    run(
      [&](std::uint16_t)
      {
        missile(5, police(6));
        space.Set(DS.safeZoneFlags, 1);
      });
    run([&](std::uint16_t) { missile(5, police(6)); });
    run(
      [&](std::uint16_t)
      {
        missile(5, space.Ship(6, TYPE_COBRA, 4, 0, 0, 0x300));
        missile(7, 0);
      });
    run(
      [&](std::uint16_t)
      {
        space.Set(DS.npcEcmFrames, 5);
        missile(5, 0);
      });
    run(
      [&](std::uint16_t)
      {
        space.Set(DS.npcEcmFrames, 5);
        space.Set(DS.thargoidInvasionActive, 1);
      });
    rig.AssertAllAgreed(UPDATE_STATION_AI, calls);
  }

  // Rocks tumble one way or the other by bit 1 of their type byte; anything else only drifts.
  TEST_METHOD(UpdateDriftingObjectAiAgreesOnRocks)
  {
    ComparisonRig rig("AiDrifting");
    Space space(rig);
    space.Clear();
    const std::uint8_t types[] = {Elite::TYPE_ASTEROID, Elite::TYPE_BOULDER, Elite::TYPE_SPLINTER, Elite::TYPE_PLATE, TYPE_COBRA};
    int index = 3;
    for (const std::uint8_t type : types)
    {
      const std::uint16_t slot = space.Ship(index++, type, DRIFTER_CLASS, 0x123, -0x456, 0x789);
      space.SetField(slot, Elite::SLOT_VELOCITY, 3);
      rig.Call(UPDATE_DRIFTING_OBJECT_AI, {.di = slot});
    }
    rig.AssertAllAgreed(UPDATE_DRIFTING_OBJECT_AI, std::size(types));
  }

  // A wolf's states: the Thargoid's ECM, resting, attack runs, turning away and counting passes, a Thargon whose mother is
  // gone, and a Thargon adrift.
  TEST_METHOD(UpdateWolfAiAgreesInEveryState)
  {
    ComparisonRig rig("AiWolf");
    Space space(rig);
    struct Wolf
    {
      std::uint8_t type;
      std::uint8_t state;
      std::uint8_t flags;
      std::uint8_t aggression;
      std::uint8_t passes;
      std::int16_t x;
      std::uint8_t range;
      std::uint8_t speed;
      int mother; // slot index, -1 none
      std::uint8_t motherType;
      std::uint8_t motherFlags;
      std::uint16_t random;
    };
    const Wolf wolves[] = {
      {Elite::TYPE_THARGOID, 0, 0, 0, 0, 0x2000, 0x10, 20, -1, 0, 0, 0x0010},
      {Elite::TYPE_THARGOID, 0, 0, 0, 0, 0x2000, 0x10, 20, -1, 0, 0, 0x0400},
      {Elite::TYPE_THARGON, 1, HOSTILE, 5, 0, 0x2000, 0x10, 20, -1, 0, 0, 0},
      {Elite::TYPE_THARGON, 1, HOSTILE, 20, 0, 0x2000, 0x10, 20, -1, 0, 0, 0x5000},
      {TYPE_COBRA, 1, 0, 0, 0, 0x2000, 0x10, 20, -1, 0, 0, 0xB000},
      {TYPE_COBRA, 2, HOSTILE | BLIP_DRAWN, 0x40, 0, 0x100, 0x10, 20, -1, 0, 0, 0},
      {TYPE_COBRA, 2, HOSTILE | BLIP_DRAWN, 0x40, 0, 0x2000, 0x10, 20, -1, 0, 0, 0},
      {TYPE_COBRA, 3, HOSTILE, 0, 2, 0x100, 0x10, 20, -1, 0, 0, 0},
      {TYPE_COBRA, 3, HOSTILE, 0, 1, 0x2000, 0x10, 20, -1, 0, 0, 0},
      {TYPE_COBRA, 3, HOSTILE, 0, 2, 0x2000, 0x10, 20, -1, 0, 0, 0},
      {Elite::TYPE_THARGON, 3, HOSTILE, 0, 1, 0x2000, 0x10, 20, -1, 0, 0, 0},
      {Elite::TYPE_THARGON, 3, HOSTILE, 0, 2, 0x2000, 0x10, 20, -1, 0, 0, 0},
      {Elite::TYPE_THARGON, 3, HOSTILE, 0, 2, 0x2000, 0x10, 20, 9, Elite::TYPE_THARGOID, BLIP_DRAWN, 0},
      {Elite::TYPE_THARGON, 3, HOSTILE, 0, 2, 0x2000, 0x10, 20, 9, Elite::TYPE_THARGOID, 0, 0},
      {Elite::TYPE_THARGON, 3, HOSTILE, 0, 2, 0x2000, 0x10, 20, 9, TYPE_COBRA, BLIP_DRAWN, 0},
      {Elite::TYPE_THARGON, 0x0A, 0, 0, 0, 0x2000, 0x10, 20, -1, 0, 0, 0},
      {Elite::TYPE_THARGON, 0x0A, 0, 0, 0, 0x2000, 0x10, 9, -1, 0, 0, 0},
    };
    for (const Wolf& wolf : wolves)
    {
      space.Clear();
      Ordinary(space);
      space.Set(DS.killCount, 5);
      const std::uint16_t slot = space.Ship(4, wolf.type, WOLF_CLASS, wolf.x, -0x300, 0x500);
      space.SetField(slot, Elite::SLOT_STATE, wolf.state);
      space.SetField(slot, Elite::SLOT_FLAGS, wolf.flags);
      space.SetField(slot, Elite::SLOT_AGGRESSION, wolf.aggression);
      space.SetField(slot, Elite::SLOT_PASSES, wolf.passes);
      space.SetField(slot, Elite::SLOT_RANGE, wolf.range);
      space.SetField(slot, Elite::SLOT_SPEED, wolf.speed);
      space.SetField(slot, Elite::SLOT_THARGONS, 3);
      space.SetField(slot, Elite::SLOT_MISSILES, 1);
      if (wolf.mother >= 0)
      {
        const std::uint16_t mother = space.Ship(wolf.mother, wolf.motherType, WOLF_CLASS, 0x3000, 0x100, 0x100);
        space.SetField(mother, Elite::SLOT_FLAGS, wolf.motherFlags);
        space.SetFieldWord(slot, Elite::SLOT_OWNER, mother);
      }
      space.Random(wolf.random, 0x0100);
      rig.Call(UPDATE_WOLF_AI, {.dx = 0x00C0, .di = slot});
    }
    // An attack run within 1000 of the player on every axis turns away.
    space.Clear();
    Ordinary(space);
    const std::uint16_t close = space.Ship(4, TYPE_COBRA, WOLF_CLASS, 0x100, -0x300, 0x300);
    space.SetField(close, Elite::SLOT_STATE, 2);
    space.SetField(close, Elite::SLOT_FLAGS, HOSTILE | BLIP_DRAWN);
    rig.Call(UPDATE_WOLF_AI, {.dx = 0x00C0, .di = close});
    rig.AssertAllAgreed(UPDATE_WOLF_AI, std::size(wolves) + 1);
  }

  // A turn towards wanted angles a little either way of the heading, which it takes whole, and far either way, which it takes
  // at the turn rate.
  TEST_METHOD(TurnTowardAnglesAgreesOnEveryStep)
  {
    ComparisonRig rig("AiTurn");
    Space space(rig);
    space.Clear();
    const std::uint16_t slot = space.Ship(4, TYPE_COBRA, WOLF_CLASS, 0x100, 0x100, 0x400);
    struct Turn
    {
      std::uint16_t pitch;
      std::uint16_t yaw;
    };
    const Turn turns[] = {{0x0203, 0x0300}, {0x01FD, 0x0100}, {0x0100, 0x01FD}, {0x0300, 0x0203}};
    for (const Turn& turn : turns)
    {
      space.SetFieldWord(slot, Elite::SLOT_PITCH, 0x0200);
      space.SetFieldWord(slot, Elite::SLOT_YAW, 0x0200);
      rig.Call(TURN_TOWARD_ANGLES, {.ax = turn.pitch, .bx = turn.yaw, .di = slot});
    }
    rig.AssertAllAgreed(TURN_TOWARD_ANGLES, std::size(turns));
  }

  // A hunter's states: flying at the player, joining a pack or a mate, attacking, formation flight, evading with jinks,
  // and closing in.
  TEST_METHOD(UpdateHunterAiAgreesInEveryState)
  {
    ComparisonRig rig("AiHunter");
    Space space(rig);
    struct Hunter
    {
      std::uint8_t state;
      std::uint8_t flags;
      int others;
      std::uint8_t legalStatus;
      std::int16_t x;
      std::int16_t yz;         // y, and -z
      std::int16_t mateOffset; // on every axis
      std::uint8_t jinkFrames;
      std::uint16_t random;
      std::uint16_t second;
    };
    const Hunter hunters[] = {
      {0, 0, 0, 0, 0x2000, 0x300, 0x40, 0, 0, 0x100},
      {0, BLIP_DRAWN | HOSTILE, 0, 0, 0x2000, 0x300, 0x40, 0, 0, 0x100},
      {0, BLIP_DRAWN, 2, 0x30, 0x2000, 0x300, 0x40, 0, 0, 0x100},
      {0, BLIP_DRAWN, 2, 0, 0x2000, 0x300, 0x40, 0, 0x0010, 0x100},
      {0, BLIP_DRAWN, 3, 0, 0x2000, 0x300, 0x40, 0, 0x0100, 0x100},
      {0, BLIP_DRAWN, 1, 0, 0x2000, 0x300, 0x40, 0, 0, 0x100},
      {0, BLIP_DRAWN, 0, 0, 0x2000, 0x300, 0x40, 0, 0, 0x100},
      {1, BLIP_DRAWN | HOSTILE, 0, 0, 0x100, 0x100, 0x40, 0, 0, 0x100},
      {1, BLIP_DRAWN | HOSTILE, 0, 0, 0x2000, 0x300, 0x40, 0, 0, 0x100},
      {2, BLIP_DRAWN, 1, 0, 0x2000, 0x300, 0x40, 0, 0, 0x100},
      {2, BLIP_DRAWN, 1, 0, 0x2000, 0x300, 0x1000, 0, 0, 0x100},
      {3, HOSTILE, 0, 0, 0x100, 0x300, 0x40, 0, 0x0123, 0x0100},
      {3, HOSTILE, 0, 0, 0x100, 0x300, 0x40, 0, 0x0122, 0x0101},
      {3, HOSTILE, 0, 0, 0x100, 0x300, 0x40, 1, 0, 0x100},
      {3, HOSTILE, 0, 0, 0x100, 0x300, 0x40, 5, 0, 0x100},
      {3, HOSTILE, 0, 0, 0x2000, 0x300, 0x40, 5, 0, 0x100},
      {4, HOSTILE, 0, 0, 0x100, 0x300, 0x40, 0, 0, 0x100},
      {4, HOSTILE | BLIP_DRAWN, 0, 0, 0x2000, 0x300, 0x40, 0, 0, 0x100},
    };
    for (const Hunter& hunter : hunters)
    {
      space.Clear();
      Ordinary(space);
      space.Set(DS.killCount, 5);
      space.Set(DS.legalStatus, hunter.legalStatus);
      const std::uint16_t slot = space.Ship(4, TYPE_COBRA, HUNTER_CLASS, hunter.x, hunter.yz, static_cast<std::int16_t>(-hunter.yz));
      space.SetField(slot, Elite::SLOT_STATE, hunter.state);
      space.SetField(slot, Elite::SLOT_FLAGS, hunter.flags);
      space.SetField(slot, Elite::SLOT_RANGE, 0x10);
      space.SetField(slot, Elite::SLOT_JINK_FRAMES, hunter.jinkFrames);
      space.SetFieldWord(slot, Elite::SLOT_JINK_PITCH, 0x0123);
      space.SetFieldWord(slot, Elite::SLOT_JINK_YAW, 0xFE00);
      space.SetField(slot, Elite::SLOT_AGGRESSION, 0x40);
      space.SetField(slot, Elite::SLOT_MISSILES, 1);
      for (int other = 0; other < hunter.others; ++other)
      {
        const std::uint16_t mate =
          space.Ship(6 + other, TYPE_COBRA, HUNTER_CLASS, static_cast<std::int16_t>(hunter.x + hunter.mateOffset),
                     static_cast<std::int16_t>(hunter.yz + hunter.mateOffset), static_cast<std::int16_t>(hunter.mateOffset - hunter.yz));
        space.SetField(mate, Elite::SLOT_FLAGS, BLIP_DRAWN);
        space.SetFieldWord(slot, Elite::SLOT_TARGET, mate);
      }
      space.Random(hunter.random, hunter.second);
      rig.Call(UPDATE_HUNTER_AI, {.dx = 0x0030, .di = slot});
    }
    rig.AssertAllAgreed(UPDATE_HUNTER_AI, std::size(hunters));
  }

  // The police and every ship in an invasion may fire inside the safe zone; anyone else may not.
  TEST_METHOD(CheckSafeZoneHoldFireAgreesForThePoliceAndInvaders)
  {
    ComparisonRig rig("AiHoldFire");
    Space space(rig);
    space.Clear();
    const std::uint16_t viper = space.Ship(4, Elite::TYPE_VIPER, 4, 0, 0, 0);
    space.SetFieldWord(viper, Elite::SLOT_OWNER, 1);
    const std::uint16_t cobra = space.Ship(5, TYPE_COBRA, 4, 0, 0, 0);
    space.Set(DS.safeZoneFlags, 1);
    space.Set(DS.thargoidInvasionActive, 1);
    rig.Call(CHECK_SAFE_ZONE_HOLD_FIRE, {.ax = 0x4444, .di = cobra});
    space.Set(DS.thargoidInvasionActive, 0);
    rig.Call(CHECK_SAFE_ZONE_HOLD_FIRE, {.ax = 0x4444, .di = viper});
    rig.Call(CHECK_SAFE_ZONE_HOLD_FIRE, {.ax = 0x4444, .di = cobra});
    rig.AssertAllAgreed(CHECK_SAFE_ZONE_HOLD_FIRE, 3);
  }

  // The device's bit on an inactive slot counts; with none, every object slot is looked at.
  TEST_METHOD(IsMaskShipPresentAgreesWithAndWithoutTheDevice)
  {
    ComparisonRig rig("AiMaskShipPresent");
    Space space(rig);
    space.Clear();
    rig.Call(IS_MASK_SHIP_PRESENT, {.cx = 0x1234, .si = 0x4321});
    space.SetField(Slot(9), Elite::SLOT_FLAGS, CARRIES_DEVICE);
    rig.Call(IS_MASK_SHIP_PRESENT, {.cx = 0x1234, .si = 0x4321});
    rig.AssertAllAgreed(IS_MASK_SHIP_PRESENT, 2);
  }

  // An escort takes the leader's first 16 bytes, then a random offset on each axis, carried into the high bytes either way.
  TEST_METHOD(PlaceEscortNearAgreesAcrossCarries)
  {
    ComparisonRig rig("AiEscort");
    Space space(rig);
    space.Clear();
    const std::uint16_t leader = space.Ship(4, Elite::TYPE_ASP, WOLF_CLASS, 0x7F00, -0x7F00, 0x0100);
    space.SetFieldWord(leader, Elite::SLOT_PITCH, 0x1234);
    const std::uint16_t randoms[][2] = {{0x07FF, 0x0000}, {0x0000, 0x07FF}, {0x0123, 0x0456}};
    for (const auto& random : randoms)
    {
      space.Random(random[0], random[1]);
      rig.Call(PLACE_ESCORT_NEAR, {.ax = 0x1111, .si = leader, .di = Slot(6)});
    }
    rig.AssertAllAgreed(PLACE_ESCORT_NEAR, std::size(randoms));
  }

  // The quartered difference of two positions, within the box and not, negative coordinates rounding down.
  TEST_METHOD(GetVectorToObjectAgreesInAndOutOfTheBox)
  {
    ComparisonRig rig("AiVectorToObject");
    Space space(rig);
    space.Clear();
    const std::uint16_t self = space.Ship(4, TYPE_COBRA, HUNTER_CLASS, -0x103, 0x205, -0x7FF);
    const std::uint16_t near = space.Ship(5, TYPE_COBRA, HUNTER_CLASS, -0x0FD, 0x1FF, -0x800);
    const std::uint16_t far = space.Ship(6, TYPE_COBRA, HUNTER_CLASS, 0x2000, 0x1FF, -0x800);
    rig.Call(GET_VECTOR_TO_OBJECT, {.dx = 0x7D0, .si = near, .di = self, .bp = 0x9999});
    rig.Call(GET_VECTOR_TO_OBJECT, {.dx = 0x7D0, .si = far, .di = self, .bp = 0x9999});
    rig.Call(GET_VECTOR_TO_OBJECT, {.dx = 0x7D0, .si = self, .di = far, .bp = 0x9999});
    rig.AssertAllAgreed(GET_VECTOR_TO_OBJECT, 3);
  }

  // A trader's and a police Viper's states: rocks spinning, deciding by the legal status and chance, attacking with and without
  // missiles, fleeing with jinks, missiles and the ECM, breaking off, and turning to flee when weak.
  TEST_METHOD(UpdateTraderOrPoliceAiAgreesInEveryState)
  {
    ComparisonRig rig("AiTrader");
    Space space(rig);
    struct Trader
    {
      std::uint8_t type;
      bool police;
      std::uint8_t state;
      std::uint8_t flags;
      std::uint8_t energy;
      std::uint8_t legalStatus;
      std::int16_t x;
      std::uint8_t jinkFrames;
      std::uint16_t random;
      std::uint16_t second;
    };
    const Trader traders[] = {
      {Elite::TYPE_ASTEROID, false, 0, HOSTILE, 0x20, 0, 0x2000, 0, 0, 0},
      {TYPE_COBRA, false, 0, 0, 0x20, 0, 0x2000, 0, 0, 0},
      {Elite::TYPE_VIPER, true, 1, 0, 0x20, 5, 0x2000, 0, 0, 0},
      {Elite::TYPE_VIPER, true, 1, 0, 0x20, 4, 0x2000, 0, 0, 0},
      {TYPE_COBRA, false, 1, HOSTILE, 0x20, 0, 0x2000, 0, 0x1000, 0},
      {TYPE_COBRA, false, 1, HOSTILE, 0x20, 0, 0x2000, 0, 0x6000, 0},
      {TYPE_COBRA, false, 1, 0, 0x20, 0, 0x2000, 0, 0, 0},
      {TYPE_COBRA, false, 2, BLIP_DRAWN | HOSTILE, 0x07, 0, 0x2000, 0, 0, 0},
      {Elite::TYPE_VIPER, true, 2, BLIP_DRAWN | HOSTILE, 0x07, 0, 0x100, 0, 0, 0},
      {TYPE_COBRA, false, 2, BLIP_DRAWN | HOSTILE, 0x20, 0, 0x2000, 0, 0, 0x100},
      {Elite::TYPE_VIPER, true, 2, BLIP_DRAWN | HOSTILE, 0x20, 0, 0x2000, 0, 0, 0x100},
      {Elite::TYPE_VIPER, true, 2, BLIP_DRAWN | HOSTILE, 0x20, 5, 0x2000, 0, 0, 0x100},
      {Elite::TYPE_VIPER, true, 2, BLIP_DRAWN | HOSTILE, 0x20, 0x20, 0x2000, 0, 0, 0x100},
      {TYPE_COBRA, false, 3, HOSTILE, 0x02, 0, 0x2000, 0, 0x0123, 0x0100},
      {TYPE_COBRA, false, 3, HOSTILE, 0x20, 0, 0x2000, 0, 0x0122, 0x0101},
      {TYPE_COBRA, false, 3, HOSTILE, 0x20, 0, 0x2000, 0, 0x0123, 0x0100},
      {TYPE_COBRA, false, 3, HOSTILE, 0x20, 0, 0x2000, 1, 0x0010, 0x100},
      {TYPE_COBRA, false, 3, HOSTILE, 0x20, 0, 0x2000, 5, 0x0010, 0x100},
      {TYPE_COBRA, false, 4, HOSTILE, 0x20, 0, 0x2000, 0, 0, 0},
      {TYPE_COBRA, false, 4, HOSTILE, 0x04, 0, 0x100, 0, 0, 0},
      {Elite::TYPE_VIPER, true, 4, HOSTILE, 0x04, 0, 0x100, 0, 0, 0},
      {TYPE_COBRA, false, 7, HOSTILE, 0x20, 0, 0x100, 0, 0, 0},
    };
    for (const Trader& trader : traders)
    {
      space.Clear();
      Ordinary(space);
      space.Set(DS.killCount, 5);
      space.Set(DS.legalStatus, trader.legalStatus);
      const std::uint16_t slot = space.Ship(4, trader.type, TRADER_CLASS, trader.x, 0x300, -0x300);
      space.SetField(slot, Elite::SLOT_STATE, trader.state);
      space.SetField(slot, Elite::SLOT_FLAGS, trader.flags);
      space.SetField(slot, Elite::SLOT_ENERGY, trader.energy);
      space.SetField(slot, Elite::SLOT_RANGE, 0x10);
      space.SetField(slot, Elite::SLOT_AGGRESSION, 0x40);
      space.SetField(slot, Elite::SLOT_MISSILES, 1);
      space.SetField(slot, Elite::SLOT_JINK_FRAMES, trader.jinkFrames);
      space.SetFieldWord(slot, Elite::SLOT_JINK_PITCH, 0x0123);
      space.SetFieldWord(slot, Elite::SLOT_JINK_YAW, 0xFE00);
      space.SetFieldWord(slot, Elite::SLOT_OWNER, trader.police ? std::uint16_t{1} : std::uint16_t{0});
      space.Random(trader.random, trader.second);
      rig.Call(UPDATE_TRADER_OR_POLICE_AI, {.dx = 0x0030, .di = slot});
    }
    rig.AssertAllAgreed(UPDATE_TRADER_OR_POLICE_AI, std::size(traders));
  }

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
    Assert::AreEqual(std::uint8_t{1}, TwinByte(rig, DS.thargoidInvasionActive), L"the invasion on");
    // Equip Ship, its cursor on Fuel: the fuel, and seven rows down an escape capsule.
    rig.Play("key F4; wait 0.8\nkey b; wait 0.3\n"
             "key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15\n"
             "key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15\nkey b; wait 0.3\ndigest bought");
    rig.Play("key F6; wait 1\ndown Right; wait 0.55; up Right; wait 0.05\nkey F1; wait 1\ndown h; wait 0.2; up h\nwait 7.5\n"
             "digest arrived");
    Assert::AreEqual(std::uint8_t{1}, TwinByte(rig, DS.jumpedSinceBriefing), L"a jump since the briefing");
    rig.Play("down c; wait 0.1; up c\nwait 5\nkey F1; wait 1.5\ndigest launched");
    Assert::IsTrue(CountShips(rig, Elite::TYPE_THARGOID).all != 0, L"the first Thargoid");
    rig.Play("wait 1\ndigest invaded");
    Assert::IsTrue(CountShips(rig, Elite::TYPE_THARGOID).all > 1, L"and more");
  }
};

} // namespace GameLogicTests
