#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Ships.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t DRAW_LASER_BEAMS = 0x0A9A;
constexpr std::uint16_t TAKE_DAMAGE = 0x2C9B;
constexpr std::uint16_t DETONATE_ENERGY_BOMB = 0x2ED6;
constexpr std::uint16_t SPAWN_PLAYER_WRECKAGE = 0x2FE3;
constexpr std::uint16_t KILL_PLAYER = 0x3115;
constexpr std::uint16_t INIT_MISSILE = 0x4C8C;
constexpr std::uint16_t REMOVE_ALL_MISSILES = 0x4F9F;
constexpr std::uint16_t EXPLODE_OBJECT = 0x4FC1;
constexpr std::uint16_t TALLY_MASK_MISSION_KILL = 0x50FE;
constexpr std::uint16_t TRY_FIRE_LASER_AT_PLAYER = 0x518B;
constexpr std::uint16_t LAUNCH_PLAYER_MISSILE = 0x5242;
constexpr std::uint16_t LAUNCH_SHIP_FROM_OBJECT = 0x534E;
constexpr std::uint16_t TRY_LAUNCH_MISSILE_AT_PLAYER = 0x543A;
constexpr std::uint16_t TRY_LAUNCH_THARGON = 0x5471;
constexpr std::uint16_t UPDATE_MISSILE_AI = 0x54F2;
constexpr std::uint16_t RESOLVE_LASER_FIRE = 0x8AC2;
constexpr std::uint16_t CHECK_MISSILE_TARGET_DESTROYED = 0x8B8B;
constexpr std::uint16_t CREDIT_KILL = 0x8BC6;
constexpr std::uint16_t ROUTINE_8C51 = 0x8C51;
constexpr std::uint16_t APPLY_ENEMY_LASER_HIT = 0x8C8E;
constexpr std::uint16_t USE_MASKING_DEVICE = 0x8ECF;

constexpr std::uint8_t ALL_SLOTS = 36;
constexpr std::uint8_t OBJECT_SLOTS = 20;
constexpr std::uint8_t DEBRIS_SLOTS = 16;
constexpr std::uint8_t SLOT_DRAWN = 0x80;
constexpr std::uint8_t TYPE_COBRA = 0x0E;
constexpr std::uint8_t NO_BOUNTY = 0xFF;
constexpr std::uint8_t BLIP_DRAWN = 0x02;
constexpr std::uint8_t INDESTRUCTIBLE = 0x04;
constexpr std::uint8_t MISSILE_CLASS = 2;

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
    // And a shot with nothing in the crosshairs.
    space.Clear();
    space.Set(DS.firingLaserType, 1);
    space.Set(DS.laserFiring, 1);
    rig.Call(RESOLVE_LASER_FIRE, {});
    rig.AssertAllAgreed(RESOLVE_LASER_FIRE, std::size(shots) + 1);
  }

  // A ship that fires at the player with its aim inside 200 on both axes hits; inside 70, squarely.
  TEST_METHOD(TryFireLaserAtPlayerAgreesOnAHit)
  {
    ComparisonRig rig("CombatEnemyFire");
    Space space(rig);
    space.Clear();
    space.Set(DS.thargoidInvasionActive, 0);
    space.Set(DS.safeZoneFlags, 0);
    space.Set(DS.gameOverFrames, 0);
    space.Set(DS.escapePodFrames, 0);
    space.Set(DS.maskingBackgroundColor, 0);
    const std::uint16_t slot = space.Object(4, TYPE_COBRA, 0, 0x100, 0x80, 0x800);
    for (const std::uint16_t aim : {std::uint16_t{0x0080}, std::uint16_t{0x0010}})
    {
      space.SetField(slot, Elite::SLOT_FLAGS, BLIP_DRAWN);
      space.SetField(slot, Elite::SLOT_AGGRESSION, 0xFF);
      space.Random(0x0010, 0x0100);
      rig.Call(TRY_FIRE_LASER_AT_PLAYER, {.ax = aim, .bx = aim, .di = slot});
    }
    rig.AssertAllAgreed(TRY_FIRE_LASER_AT_PLAYER, 2);
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

  // Damage from 100h on empties the shield and takes the rest off the energy; less is the shield's until it runs out, and an
  // excess over 127 sign-extends; energy below 0 kills, unless the escape pod flies.
  TEST_METHOD(TakeDamageAndKillPlayerAgree)
  {
    ComparisonRig rig("CombatDamage");
    Space space(rig);
    struct Damage
    {
      std::uint16_t amount;
      std::uint8_t shield;
      std::uint16_t energy;
      std::uint8_t pod;
    };
    const Damage damages[] = {
      {0x320, 0x40, 0x300, 0}, {0x320, 0x10, 0x100, 0}, {0x20, 0x40, 0x100, 0}, {0x20, 0x10, 0x100, 0},
      {0x20, 0x10, 0x08, 0},   {0xC0, 0x00, 0x300, 0},  {0x20, 0x10, 0x100, 5},
    };
    for (const Damage& damage : damages)
    {
      space.Set(DS.foreShield, damage.shield);
      space.Set(DS.playerEnergy, damage.energy);
      space.Set(DS.escapePodFrames, damage.pod);
      space.Set(DS.playerDead, 0);
      rig.Call(TAKE_DAMAGE, {.ax = damage.amount, .bx = 0x1234});
    }
    for (const std::uint8_t pod : {std::uint8_t{0}, std::uint8_t{3}})
    {
      space.Set(DS.escapePodFrames, pod);
      space.Set(DS.playerDead, 0);
      rig.Call(KILL_PLAYER, {.ax = 0x5555});
    }
    space.Set(DS.escapePodFrames, 0);
    rig.AssertAllAgreed(TAKE_DAMAGE, std::size(damages));
    rig.AssertAllAgreed(KILL_PLAYER, 2);
  }

  // Every active ship with a blip goes, drawn or not; one without a blip, or inactive, stays; in the safe zone it is a crime,
  // to the limit of the legal status.
  TEST_METHOD(DetonateEnergyBombAgreesInAndOutOfTheSafeZone)
  {
    ComparisonRig rig("CombatEnergyBomb");
    Space space(rig);
    const std::uint8_t statuses[][2] = {{1, 0x10}, {1, 0xF0}, {0, 0x10}};
    for (const auto& status : statuses)
    {
      space.Clear();
      const std::uint16_t drawn = space.Object(4, TYPE_COBRA, SLOT_DRAWN, 100, 200, 300);
      space.SetField(drawn, Elite::SLOT_FLAGS, BLIP_DRAWN);
      space.SetField(drawn, Elite::SLOT_CARGO, 5);
      space.SetField(drawn, Elite::SLOT_FRAGMENTS, 2);
      const std::uint16_t hidden = space.Object(6, TYPE_COBRA, 0, -300, 200, 900);
      space.SetField(hidden, Elite::SLOT_FLAGS, BLIP_DRAWN);
      space.Object(7, TYPE_COBRA, SLOT_DRAWN, 50, 60, 70);
      space.SetField(Slot(9), Elite::SLOT_FLAGS, BLIP_DRAWN);
      space.Set(DS.safeZoneFlags, status[0]);
      space.Set(DS.legalStatus, status[1]);
      rig.Call(DETONATE_ENERGY_BOMB, {});
    }
    rig.AssertAllAgreed(DETONATE_ENERGY_BOMB, std::size(statuses));
  }

  // The wreck with and without cargo: its barrel in a free slot, in the first slot without a blip, or in one evicted at random;
  // the drift from the player's angles and view, negative and positive.
  TEST_METHOD(SpawnPlayerWreckageAgreesWithAndWithoutCargo)
  {
    ComparisonRig rig("CombatWreckage");
    Space space(rig);
    struct Wreck
    {
      std::uint8_t cargo;
      int fill; // 0 none, 1 every ship slot but one without a blip, 2 every one with a blip
      std::uint16_t view;
      std::uint16_t pitch;
    };
    const Wreck wrecks[] = {{0, 0, 0, 0}, {5, 0, 0, 0x123}, {5, 1, 0x400, 0x5A0}, {5, 2, 0x200, 0x2F0}, {5, 2, 0x600, 0x7E0}};
    std::uint16_t random = 0x1357;
    for (const Wreck& wreck : wrecks)
    {
      space.Clear();
      if (wreck.fill != 0)
      {
        for (int index = 3; index < OBJECT_SLOTS; ++index)
        {
          const std::uint16_t slot = space.Object(index, TYPE_COBRA, 0, static_cast<std::int16_t>(0x100 * index), 0x200, 0x300);
          space.SetField(slot, Elite::SLOT_FLAGS, BLIP_DRAWN);
        }
        if (wreck.fill == 1)
          space.SetField(Slot(11), Elite::SLOT_FLAGS, 0);
      }
      space.Set(DS.cargoUsedTonnes, wreck.cargo);
      space.Set(DS.viewAngle, wreck.view);
      space.Set(DS.playerPitchAngle, wreck.pitch);
      space.Set(DS.playerYawAngle, static_cast<std::uint16_t>(wreck.pitch * 3));
      space.Set(DS.playerRollAngle, static_cast<std::uint16_t>(wreck.pitch + 0x100));
      space.Random(random, static_cast<std::uint16_t>(~random));
      random = static_cast<std::uint16_t>(random * 7 + 0x3F1);
      rig.Call(SPAWN_PLAYER_WRECKAGE, {});
    }
    rig.AssertAllAgreed(SPAWN_PLAYER_WRECKAGE, std::size(wrecks));
  }

  // Only active missiles go: not an inactive one, nor any other type.
  TEST_METHOD(RemoveAllMissilesAgreesOnMissilesOnly)
  {
    ComparisonRig rig("CombatRemoveMissiles");
    Space space(rig);
    space.Clear();
    space.Object(4, Elite::TYPE_MISSILE, 0, 100, 200, 300);
    space.Object(5, Elite::TYPE_MISSILE, 0, 100, 200, 300);
    space.SetByte(Slot(5), static_cast<std::uint8_t>(Elite::TYPE_MISSILE << 1));
    space.Object(6, TYPE_COBRA, 0, 100, 200, 300);
    space.SetField(space.Object(9, Elite::TYPE_MISSILE, 0, -100, 200, 300), Elite::SLOT_FLAGS, BLIP_DRAWN);
    rig.Call(REMOVE_ALL_MISSILES, {.ax = 0x1111, .bx = 0x2222});
    rig.AssertAllAgreed(REMOVE_ALL_MISSILES, 1);
  }

  // The player's missile, in a free slot or in one reclaimed, aimed at its target from 100 along the nose.
  TEST_METHOD(LaunchPlayerMissileAgreesWithAndWithoutAFreeSlot)
  {
    ComparisonRig rig("CombatPlayerMissile");
    Space space(rig);
    for (int full = 0; full < 2; ++full)
    {
      space.Clear();
      if (full != 0)
      {
        for (int index = 3; index < OBJECT_SLOTS; ++index)
          space.SetField(space.Object(index, TYPE_COBRA, 0, static_cast<std::int16_t>(0x80 * index), -0x200, 0x300), Elite::SLOT_FLAGS,
                         BLIP_DRAWN);
      }
      const std::uint16_t target = space.Object(5, TYPE_COBRA, SLOT_DRAWN, 0x300, -0x150, 0x900);
      space.Set(DS.missileTarget, target);
      space.Set(DS.playerPitchAngle, static_cast<std::uint16_t>(0x120 + 0x300 * full));
      space.Set(DS.playerYawAngle, 0x7A0);
      space.Set(DS.playerRollAngle, 0x055);
      space.Random(0x4321, 0x8765);
      rig.Call(LAUNCH_PLAYER_MISSILE, {.di = Slot(2)});
    }
    rig.AssertAllAgreed(LAUNCH_PLAYER_MISSILE, 2);
  }

  // A missile at the player, an escape pod, a Thargon and a kind it does not know, from a launcher; and no free slot. (DL=5, a
  // Krait, returns to CS:launcher, which a comparison cannot follow.)
  TEST_METHOD(LaunchShipFromObjectAgreesOnEveryKind)
  {
    ComparisonRig rig("CombatLaunchShip");
    Space space(rig);
    const std::uint8_t kinds[] = {0x14, 0x15, 0x07, 0x00, 0x16};
    for (const std::uint8_t kind : kinds)
    {
      space.Clear();
      const std::uint16_t launcher = space.Object(4, Elite::TYPE_THARGOID, 0, 0x300, -0x400, 0x500);
      space.SetField(launcher, Elite::SLOT_SPEED, 12);
      space.Random(0x2468, 0xACE0);
      rig.Call(LAUNCH_SHIP_FROM_OBJECT, {.dx = static_cast<std::uint16_t>(0x3300 | kind), .di = launcher});
    }
    for (int index = 3; index < OBJECT_SLOTS; ++index)
      space.Object(index, TYPE_COBRA, 0, 0, 0, 0x1000);
    rig.Call(LAUNCH_SHIP_FROM_OBJECT, {.dx = 0x14, .di = Slot(4)});
    rig.Call(INIT_MISSILE, {.ax = 0x7777, .di = Slot(6)});
    rig.AssertAllAgreed(LAUNCH_SHIP_FROM_OBJECT, std::size(kinds) + 1);
    rig.AssertAllAgreed(INIT_MISSILE, 1);
  }

  // A missile far from its target turns to it; near it, it explodes: on the player (TakeDamage), on a gone target, on a ship
  // (credited, and destroyed unless indestructible), on a station (a crime, or in an invasion its energy).
  TEST_METHOD(UpdateMissileAiAgreesOnEveryTarget)
  {
    ComparisonRig rig("CombatMissileAi");
    Space space(rig);
    struct Flight
    {
      int target; // -1 the player, else a slot index
      std::uint8_t targetType;
      bool active;
      std::int16_t distance; // the missile's offset from the target on each axis
      std::uint8_t flags;
      std::uint8_t legalStatus;
      std::uint8_t invasion;
      std::uint8_t energy;
    };
    const Flight flights[] = {
      {-1, 0, true, 0x2000, 0, 0, 0, 0},
      {-1, 0, true, 0x40, 0, 0, 0, 0},
      {6, TYPE_COBRA, false, 0x40, 0, 0, 0, 0},
      {6, TYPE_COBRA, true, -0x1000, 0, 0, 0, 0},
      {6, TYPE_COBRA, true, 0x40, 0, 0, 0, 0x20},
      {6, TYPE_COBRA, true, -0x40, INDESTRUCTIBLE, 0, 0, 0x20},
      {2, Elite::TYPE_CORIOLIS, true, 0x40, 0, 0x10, 0, 0x20},
      {2, Elite::TYPE_CORIOLIS, true, 0x40, 0, 0xFE, 0, 0x20},
      {2, Elite::TYPE_CORIOLIS, true, 0x40, 0, 0, 1, 0x20},
      {2, Elite::TYPE_CORIOLIS, true, 0x40, 0, 0, 1, 0x05},
    };
    for (const Flight& flight : flights)
    {
      space.Clear();
      space.Set(DS.legalStatus, flight.legalStatus);
      space.Set(DS.thargoidInvasionActive, flight.invasion);
      space.Set(DS.invadedStationDestroyed, 0);
      space.Set(DS.foreShield, 0x20);
      space.Set(DS.playerEnergy, 0x200);
      space.Set(DS.escapePodFrames, 0);
      space.Set(DS.witchspaceCountdown, 0);
      std::int16_t origin[3] = {0, 0, 0};
      std::uint16_t target = 0;
      if (flight.target >= 0)
      {
        origin[0] = 0x500;
        origin[1] = -0x300;
        origin[2] = 0x700;
        target = space.Object(flight.target, flight.targetType, SLOT_DRAWN, origin[0], origin[1], origin[2]);
        space.SetField(target, Elite::SLOT_FLAGS, flight.flags);
        space.SetField(target, Elite::SLOT_ENERGY, flight.energy);
        space.SetField(target, Elite::SLOT_BOUNTY, 10);
        if (!flight.active)
          space.SetByte(target, static_cast<std::uint8_t>(flight.targetType << 1));
      }
      const std::uint16_t missile =
        space.Object(5, Elite::TYPE_MISSILE, SLOT_DRAWN, static_cast<std::int16_t>(origin[0] + flight.distance),
                     static_cast<std::int16_t>(origin[1] + flight.distance), static_cast<std::int16_t>(origin[2] - flight.distance));
      space.SetField(missile, Elite::SLOT_CLASS, MISSILE_CLASS);
      space.SetField(missile, Elite::SLOT_SPEED, 30);
      space.SetField(missile, Elite::SLOT_TURN_RATE, 0x20);
      space.SetFieldWord(missile, Elite::SLOT_TARGET, target);
      rig.Call(UPDATE_MISSILE_AI, {.ax = 0x0101, .bx = 0x0202, .cx = 0x0303, .dx = 0x0404, .di = missile, .bp = 0x0505});
    }
    rig.AssertAllAgreed(UPDATE_MISSILE_AI, std::size(flights));
  }

  // A Thargon, a Thargoid, and anything else.
  TEST_METHOD(Routine8C51AgreesOnThargoidsAndThargons)
  {
    ComparisonRig rig("Combat8C51");
    Space space(rig);
    space.Clear();
    const std::uint8_t types[] = {Elite::TYPE_THARGON, Elite::TYPE_THARGOID, TYPE_COBRA};
    for (const std::uint8_t type : types)
      rig.Call(ROUTINE_8C51, {.ax = 0x9999, .di = space.Object(4, type, 0, 0, 0, 0)});
    rig.AssertAllAgreed(ROUTINE_8C51, std::size(types));
  }

  // The device's energy with and without 12 to take, and every slot calmed, by 2 or to 0.
  TEST_METHOD(UseMaskingDeviceAgreesOnEnergyAndAggression)
  {
    ComparisonRig rig("CombatMasking");
    Space space(rig);
    for (const std::uint16_t energy : {std::uint16_t{0x100}, std::uint16_t{5}})
    {
      space.Clear();
      for (int index = 0; index < OBJECT_SLOTS; ++index)
      {
        space.SetField(Slot(index), Elite::SLOT_STATE, static_cast<std::uint8_t>(index));
        space.SetField(Slot(index), Elite::SLOT_FLAGS, static_cast<std::uint8_t>(0xF0 | index));
        space.SetField(Slot(index), Elite::SLOT_AGGRESSION, static_cast<std::uint8_t>(index * 13));
      }
      space.Set(DS.playerEnergy, energy);
      rig.Call(USE_MASKING_DEVICE, {.cx = 0x1234, .si = 0x5678});
    }
    rig.AssertAllAgreed(USE_MASKING_DEVICE, 2);
  }
};

} // namespace GameLogicTests
