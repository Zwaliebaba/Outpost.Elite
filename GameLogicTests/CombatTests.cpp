#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Ships.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t DRAW_LASER_BEAMS = 0x0A9A;
constexpr std::uint16_t EXPLODE_OBJECT = 0x4FC1;
constexpr std::uint16_t TALLY_MASK_MISSION_KILL = 0x50FE;
constexpr std::uint16_t TRY_LAUNCH_MISSILE_AT_PLAYER = 0x543A;
constexpr std::uint16_t TRY_LAUNCH_THARGON = 0x5471;
constexpr std::uint16_t RESOLVE_LASER_FIRE = 0x8AC2;
constexpr std::uint16_t CHECK_MISSILE_TARGET_DESTROYED = 0x8B8B;
constexpr std::uint16_t CREDIT_KILL = 0x8BC6;
constexpr std::uint16_t APPLY_ENEMY_LASER_HIT = 0x8C8E;

constexpr std::uint8_t ALL_SLOTS = 36;
constexpr std::uint8_t OBJECT_SLOTS = 20;
constexpr std::uint8_t DEBRIS_SLOTS = 16;
constexpr std::uint8_t SLOT_DRAWN = 0x80;
constexpr std::uint8_t TYPE_COBRA = 0x0E;
constexpr std::uint8_t NO_BOUNTY = 0xFF;

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

  /// A field of the slot at _slot.
  void SetField(std::uint16_t _slot, std::uint16_t _field, std::uint8_t _value) noexcept
  {
    SetByte(static_cast<std::uint16_t>(_slot + _field), _value);
  }

  void SetFieldWord(std::uint16_t _slot, std::uint16_t _field, std::uint16_t _value) noexcept
  {
    SetWord(static_cast<std::uint16_t>(_slot + _field), _value);
  }

  /// Every slot cleared, and the slot counts SetUpLocalSpace gives.
  void Clear() noexcept
  {
    for (int offset = 0; offset < ALL_SLOTS * Elite::SLOT_BYTES; ++offset)
      SetByte(static_cast<std::uint16_t>(DS.shipSlots.offset + offset), 0);
    Set(DS.shipSlotCount, ALL_SLOTS);
    Set(DS.objectSlotCount, OBJECT_SLOTS);
    Set(DS.debrisSlotCount, DEBRIS_SLOTS);
  }

  /// NextRandom's next two results.
  void Random(std::uint16_t _first, std::uint16_t _second) noexcept
  {
    Set(DS.randomState0, _first);
    Set(DS.randomState1, 0);
    Set(DS.randomState2, _second);
  }

  /// Slot _index made an active object of _type, _flags (SLOT_DRAWN) in its first byte, at the signed position (_x, _y,
  /// _z) and the same in the camera's frame. Out: its offset.
  std::uint16_t Object(int _index, std::uint8_t _type, std::uint8_t _flags, std::int16_t _x, std::int16_t _y, std::int16_t _z) noexcept
  {
    const auto slot = static_cast<std::uint16_t>(DS.shipSlots.offset + _index * Elite::SLOT_BYTES);
    SetByte(slot, static_cast<std::uint8_t>((_type << 1) | Elite::SLOT_ACTIVE | _flags));
    const std::int16_t position[] = {_x, _y, _z};
    for (int axis = 0; axis < 3; ++axis)
    {
      SetFieldWord(slot, static_cast<std::uint16_t>(Elite::SLOT_X + 2 * axis), static_cast<std::uint16_t>(position[axis]));
      SetField(slot, static_cast<std::uint16_t>(Elite::SLOT_X_HIGH + axis), position[axis] < 0 ? std::uint8_t{0xFF} : std::uint8_t{0});
      SetFieldWord(slot, static_cast<std::uint16_t>(Elite::SLOT_VIEW_X + 2 * axis), static_cast<std::uint16_t>(position[axis]));
    }
    return slot;
  }

private:
  Machine::Memory& m_memory;
  std::uint16_t m_segment;
};

[[nodiscard]] std::uint16_t Slot(int _index) noexcept
{
  return static_cast<std::uint16_t>(DS.shipSlots.offset + _index * Elite::SLOT_BYTES);
}

} // namespace

// Constructed inputs for the ported combat routines (plan §6.3): the branches the replays do not reach.
TEST_CLASS(CombatTests)
{
public:
  // Laser types 1, 2 and 3 draw their own patterns; type 0 and 2 alternate sides.
  TEST_METHOD(DrawLaserBeamsAgreesForEveryLaserType)
  {
    ComparisonRig rig("CombatBeams");
    Space space(rig);
    const std::uint8_t types[] = {1, 3, 3, 2, 2, 0, 0};
    for (const std::uint8_t type : types)
    {
      space.Set(DS.firingLaserType, type);
      rig.Call(DRAW_LASER_BEAMS, {});
    }
    rig.AssertAllAgreed(DRAW_LASER_BEAMS, std::size(types));
  }

  // An object not drawn is only removed; a drawn one may carry the device, or 255 tonnes, whose barrel count divides by zero
  // into the game's trap; a mined asteroid's fragments, and a station's, fly differently.
  TEST_METHOD(ExplodeObjectAgreesOnItsBranches)
  {
    ComparisonRig rig("CombatExplode");
    Space space(rig);
    space.Clear();
    std::uint16_t slot = space.Object(4, TYPE_COBRA, 0, 100, 200, 300);
    rig.Call(EXPLODE_OBJECT, {.di = slot});

    space.Clear();
    slot = space.Object(4, TYPE_COBRA, SLOT_DRAWN, 100, 200, 300);
    space.SetField(slot, Elite::SLOT_FLAGS, 0x20);
    rig.Call(EXPLODE_OBJECT, {.di = slot});

    space.Clear();
    slot = space.Object(4, TYPE_COBRA, SLOT_DRAWN, -100, 200, 300);
    space.SetField(slot, Elite::SLOT_CARGO, 0xFF);
    space.Random(0x00C0, 0x1234);
    rig.Call(EXPLODE_OBJECT, {.di = slot});

    space.Clear();
    slot = space.Object(4, Elite::TYPE_ASTEROID, SLOT_DRAWN, 50, -60, 700);
    space.SetField(slot, Elite::SLOT_FRAGMENTS, 2);
    space.SetField(slot, Elite::SLOT_CARGO, 3);
    space.SetField(slot, Elite::SLOT_VELOCITY, 0x85);
    space.Set(DS.miningLaserOnAsteroid, 1);
    space.Random(0x4321, 0x0100);
    rig.Call(EXPLODE_OBJECT, {.di = slot});
    space.Random(0x4321, 0x9000);
    rig.Call(EXPLODE_OBJECT, {.di = slot});
    space.Set(DS.miningLaserOnAsteroid, 0);

    space.Clear();
    slot = space.Object(2, Elite::TYPE_CORIOLIS, SLOT_DRAWN, 0, 0, 2000);
    space.SetField(slot, Elite::SLOT_FRAGMENTS, 2);
    rig.Call(EXPLODE_OBJECT, {.di = slot});
    rig.AssertAllAgreed(EXPLODE_OBJECT, 6);
  }

  // Only an Asp with a bounty of 200 counts, and only the one with the device ends the mission.
  TEST_METHOD(TallyMaskMissionKillAgreesOnTheMissionsShips)
  {
    ComparisonRig rig("CombatTally");
    Space space(rig);
    space.Clear();
    space.Set(DS.maskMissionShipsLeft, 3);
    const std::uint16_t asp = space.Object(4, Elite::TYPE_ASP, 0, 0, 0, 0);
    space.SetField(asp, Elite::SLOT_BOUNTY, 200);
    rig.Call(TALLY_MASK_MISSION_KILL, {.ax = 0x7777, .di = asp});
    space.SetField(asp, Elite::SLOT_FLAGS, 0x20);
    rig.Call(TALLY_MASK_MISSION_KILL, {.di = asp});
    space.SetField(asp, Elite::SLOT_BOUNTY, 199);
    rig.Call(TALLY_MASK_MISSION_KILL, {.di = asp});
    const std::uint16_t cobra = space.Object(5, TYPE_COBRA, 0, 0, 0, 0);
    rig.Call(TALLY_MASK_MISSION_KILL, {.di = cobra});
    rig.AssertAllAgreed(TALLY_MASK_MISSION_KILL, 4);
  }

  // A hostile ship with missiles and a player with kills launches one, if a slot is free; a Thargoid launches Thargons.
  TEST_METHOD(LaunchesAgreeWithAndWithoutAFreeSlot)
  {
    ComparisonRig rig("CombatLaunch");
    Space space(rig);
    space.Clear();
    space.Set(DS.killCount, 3);
    const std::uint16_t launcher = space.Object(4, TYPE_COBRA, 0, 300, -400, 500);
    space.SetField(launcher, Elite::SLOT_FLAGS, 0x01);
    space.SetField(launcher, Elite::SLOT_MISSILES, 2);
    space.Random(0, 0);
    rig.Call(TRY_LAUNCH_MISSILE_AT_PLAYER, {.bx = 0x100, .di = launcher});
    space.SetField(launcher, Elite::SLOT_MISSILES, 0);
    rig.Call(TRY_LAUNCH_MISSILE_AT_PLAYER, {.bx = 0x100, .di = launcher});

    const std::uint16_t thargoid = space.Object(6, Elite::TYPE_THARGOID, 0, -300, 400, 900);
    space.SetField(thargoid, Elite::SLOT_THARGONS, 2);
    space.Random(0x12B, 0);
    rig.Call(TRY_LAUNCH_THARGON, {.di = thargoid});
    space.Random(0x12C, 0);
    rig.Call(TRY_LAUNCH_THARGON, {.di = thargoid});

    for (int index = 3; index < OBJECT_SLOTS; ++index)
      space.SetByte(Slot(index), static_cast<std::uint8_t>(Elite::SLOT_ACTIVE | (Elite::TYPE_SPLINTER << 1)));
    space.SetByte(launcher, static_cast<std::uint8_t>(Elite::SLOT_ACTIVE | (TYPE_COBRA << 1)));
    space.SetByte(thargoid, static_cast<std::uint8_t>(Elite::SLOT_ACTIVE | (Elite::TYPE_THARGOID << 1)));
    space.SetField(launcher, Elite::SLOT_MISSILES, 2);
    space.Random(0, 0);
    rig.Call(TRY_LAUNCH_MISSILE_AT_PLAYER, {.bx = 0x100, .di = launcher});
    space.Random(0, 0);
    rig.Call(TRY_LAUNCH_THARGON, {.di = thargoid});
    rig.AssertAllAgreed(TRY_LAUNCH_MISSILE_AT_PLAYER, 3);
    rig.AssertAllAgreed(TRY_LAUNCH_THARGON, 3);
  }

  // The player's laser on a target in the crosshairs: the mining laser on an asteroid, a station with and without an
  // invasion, aggression and legal status at their limits, and indestructible targets.
  TEST_METHOD(ResolveLaserFireAgreesOnEveryKindOfHit)
  {
    ComparisonRig rig("CombatLaser");
    Space space(rig);
    struct Shot
    {
      std::uint8_t type;
      std::uint8_t laser;
      std::uint8_t energy;
      std::uint8_t flags;
      std::uint8_t aggression;
      std::uint8_t legalStatus;
      std::uint8_t invasion;
    };
    const Shot shots[] = {
      {Elite::TYPE_ASTEROID, 2, 100, 0, 0, 0, 0},    {Elite::TYPE_ASTEROID, 1, 100, 0, 0xFE, 0, 0},
      {Elite::TYPE_CORIOLIS, 3, 100, 0, 0, 0, 1},    {Elite::TYPE_CORIOLIS, 0, 100, 0, 0, 0xF0, 0},
      {Elite::TYPE_CORIOLIS, 0, 100, 0, 0, 0x10, 0}, {Elite::TYPE_CORIOLIS, 3, 1, 4, 0, 0, 0},
      {Elite::TYPE_CORIOLIS, 3, 1, 4, 0, 0xE0, 0},   {TYPE_COBRA, 3, 1, 4, 0, 0, 0},
      {Elite::TYPE_DODO, 1, 0, 4, 0, 0, 1},
    };
    for (const Shot& shot : shots)
    {
      space.Clear();
      const std::uint16_t slot = space.Object(2, shot.type, SLOT_DRAWN, 0, 0, 0x200);
      space.SetField(slot, Elite::SLOT_ENERGY, shot.energy);
      space.SetField(slot, Elite::SLOT_FLAGS, shot.flags);
      space.SetField(slot, Elite::SLOT_AGGRESSION, shot.aggression);
      space.Set(DS.legalStatus, shot.legalStatus);
      space.Set(DS.thargoidInvasionActive, shot.invasion);
      space.Set(DS.firingLaserType, shot.laser);
      space.Set(DS.dockingComputerOn, 1);
      space.Set(DS.laserFiring, 1);
      rig.Call(RESOLVE_LASER_FIRE, {});
    }
    rig.AssertAllAgreed(RESOLVE_LASER_FIRE, std::size(shots));
  }

  // A missile locked on the destroyed object loses its lock, with the message.
  TEST_METHOD(CheckMissileTargetDestroyedAgreesOnALockedMissile)
  {
    ComparisonRig rig("CombatMissileTarget");
    Space space(rig);
    space.Set(DS.missileState, 2);
    space.Set(DS.missileTarget, Slot(5));
    rig.Call(CHECK_MISSILE_TARGET_DESTROYED, {.ax = 0x1111, .di = Slot(6)});
    rig.Call(CHECK_MISSILE_TARGET_DESTROYED, {.ax = 0x1111, .di = Slot(5)});
    rig.AssertAllAgreed(CHECK_MISSILE_TARGET_DESTROYED, 2);
  }

  // Bounties paid, the kill count at its limit, witch space's countdown taken down by Thargons and Thargoids, and the
  // crimes of killing what has no bounty.
  TEST_METHOD(CreditKillAgreesOnBountiesAndCrimes)
  {
    ComparisonRig rig("CombatCredit");
    Space space(rig);
    struct Kill
    {
      std::uint8_t type;
      std::uint8_t bounty;
      std::uint16_t owner;
      std::uint8_t kills;
      std::uint8_t countdown;
      std::uint8_t safeZone;
      std::uint8_t legalStatus;
    };
    const Kill kills[] = {
      {TYPE_COBRA, 10, 0, 0xFE, 0, 0, 0},
      {TYPE_COBRA, 10, 0, 0xFF, 0, 0, 0},
      {Elite::TYPE_THARGON, 5, 0, 1, 6, 0, 0},
      {Elite::TYPE_THARGON, 5, 0, 1, 5, 0, 0},
      {Elite::TYPE_THARGON, 5, 0, 1, 3, 0, 0},
      {Elite::TYPE_THARGOID, NO_BOUNTY, 0, 1, 0x10, 0, 0},
      {Elite::TYPE_THARGOID, NO_BOUNTY, 0, 1, 0x40, 0, 0},
      {TYPE_COBRA, 10, 0, 1, 0x40, 0, 0},
      {TYPE_COBRA, NO_BOUNTY, 0, 1, 0, 1, 0},
      {Elite::TYPE_VIPER, NO_BOUNTY, 1, 1, 0, 1, 0xFE},
      {Elite::TYPE_VIPER, NO_BOUNTY, 1, 1, 0, 0, 0x20},
      {TYPE_COBRA, NO_BOUNTY, 0, 1, 0, 0, 0x20},
    };
    space.Clear();
    for (const Kill& kill : kills)
    {
      const std::uint16_t slot = space.Object(5, kill.type, 0, 0, 0, 0);
      space.SetField(slot, Elite::SLOT_BOUNTY, kill.bounty);
      space.SetFieldWord(slot, Elite::SLOT_OWNER, kill.owner);
      space.Set(DS.killCount, kill.kills);
      space.Set(DS.witchspaceCountdown, kill.countdown);
      space.Set(DS.safeZoneFlags, kill.safeZone);
      space.Set(DS.legalStatus, kill.legalStatus);
      rig.Call(CREDIT_KILL, {.di = slot});
    }
    rig.AssertAllAgreed(CREDIT_KILL, std::size(kills));
  }

  // The enemy's beam from where the attacker is on screen to a random point on an edge: vertical, horizontal, diagonal and
  // clipped lines; then the shield's damage, with what it cannot take off the energy, to the player's death.
  TEST_METHOD(ApplyEnemyLaserHitAgreesOnBeamsAndDamage)
  {
    ComparisonRig rig("CombatEnemyLaser");
    Space space(rig);
    struct Hit
    {
      std::int16_t x;
      std::int16_t y;
      std::int16_t z;
      std::uint16_t random;
      std::uint8_t depth;
      std::uint8_t shield;
      std::uint16_t energy;
    };
    const Hit hits[] = {
      {0, 0, 0x100, 0x0080, 0, 0x40, 0x100},     {0, 0, 0x100, 0x0040, 0, 0x40, 0x100},           {0, 0, 0x100, 0xA840, 0, 0x40, 0x100},
      {1, 0, 0x100, 0xFFC0, 0, 0x40, 0x100},     {-0x80, 0, 0x100, 0xA840, 0, 0x40, 0x100},       {0x200, 0, 0x100, 0x0010, 0, 0x40, 0x100},
      {0x200, 0, 0x100, 0xFFC0, 0, 0x40, 0x100}, {-0x200, -0x100, 0x100, 0x0080, 0, 0x40, 0x100}, {125, 0, 0x100, 0xFFC0, 0, 0x40, 0x100},
      {1, -64, 0x100, 0x0082, 0, 0x40, 0x100},   {0, 0x40, 0x100, 0x8000, 0x80, 0x05, 0x100},     {0, 0, 0x100, 0x6000, 0, 0x03, 0x05},
      {0, 0, 0x100, 0x6000, 0, 0x0F, 0x05},
    };
    space.Clear();
    for (const Hit& hit : hits)
    {
      const std::uint16_t slot = space.Object(4, TYPE_COBRA, SLOT_DRAWN, hit.x, hit.y, hit.z);
      space.Set(DS.playerHitPending, 1);
      space.Set(DS.playerHitBy, slot);
      space.Set(DS.playerHitByDepth, hit.depth);
      space.Set(DS.foreShield, hit.shield);
      space.Set(DS.aftShield, hit.shield);
      space.Set(DS.playerEnergy, hit.energy);
      space.Random(hit.random, 0);
      rig.Call(APPLY_ENEMY_LASER_HIT, {});
    }
    space.Set(DS.playerHitPending, 1);
    space.SetByte(Slot(4), static_cast<std::uint8_t>(Elite::SLOT_ACTIVE | (TYPE_COBRA << 1)));
    rig.Call(APPLY_ENEMY_LASER_HIT, {});
    rig.AssertAllAgreed(APPLY_ENEMY_LASER_HIT, std::size(hits) + 1);
  }
};

} // namespace GameLogicTests
