#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Ships.h"

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
constexpr std::uint16_t UPDATE_STATION_AI = 0x5595;
constexpr std::uint16_t UPDATE_DRIFTING_OBJECT_AI = 0x5681;
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
    rig.AssertAllAgreed(UPDATE_WOLF_AI, std::size(wolves));
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
};

} // namespace GameLogicTests
