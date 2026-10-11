#include "pch.h"

#include "Ai.h"

#include "AiTables.h"
#include "Arithmetic.h"
#include "Combat.h"
#include "Maths.h"
#include "Ships.h"

#include <utility>

namespace Elite
{

namespace
{

constexpr std::uint8_t FLAG_HOSTILE = 0x01;
constexpr std::uint8_t FLAG_BLIP_DRAWN = 0x02;
constexpr std::uint8_t FLAG_DEVICE = 0x20;

constexpr std::uint16_t CLASS_COUNTS_CLEARED = 9; // activeObjectCount and the eight behaviorClassCounts
// The behaviour classes, behaviorHandlers' indexes.
constexpr std::uint8_t INERT_CLASS = 0;
constexpr std::uint8_t STATION_CLASS = 1;
constexpr std::uint8_t MISSILE_CLASS = 2;
constexpr std::uint8_t DRIFTING_CLASS = 3;
constexpr std::uint8_t TRADER_CLASS = 4;
constexpr std::uint8_t WOLF_CLASS = 5;
constexpr std::uint8_t HUNTER_CLASS = 6;
constexpr std::uint8_t DEBRIS_CLASS = 7; // not counted as active
constexpr std::uint8_t MOST_ACTIVE_FOR_SPAWNING = 10;
constexpr std::uint16_t WOLF_ODDS_LIMIT = 0x1C2; // outside anarchies, a wolf needs a random word below this too
constexpr std::uint8_t MOST_INVADERS = 8;
constexpr std::uint8_t MOST_WOLVES_WITH_MASK_SHIP = 3;
constexpr std::uint8_t MASK_SHIP_FLASH_FRAMES = 0x14; // the frames it is first shown for, as Scene's FlashedOff counts them
constexpr std::uint8_t ASP_ACTIVE = (TYPE_ASP << 1) | SLOT_ACTIVE;
constexpr unsigned JUMP_DRIVE_ODDS_SHIFT = 5;

constexpr std::uint16_t ANGLE_MASK = 0x7FF;

constexpr std::uint16_t STATION_SPIN = 10;
constexpr std::uint16_t STATION_GUARD_BOX = 0x1C2;
constexpr std::uint8_t OFFENDER = 10;
constexpr std::uint8_t FUGITIVE = 0x28;
constexpr std::uint16_t OFFENDER_LAUNCH_ODDS = 90;
constexpr std::uint16_t FUGITIVE_LAUNCH_ODDS = 2000;
constexpr std::uint16_t POLICE_FROM = 10000;
constexpr std::uint16_t SHUTTLE_FROM = 5000;
constexpr std::uint16_t LAUNCH_DISTANCE = 0xF0;
constexpr std::uint16_t LAUNCH_TURN = 0x400;
constexpr std::uint8_t STATION_MISSILE_CRIME = 0x28;
constexpr std::uint8_t POLICE_MISSILE_CRIME = 0x0A;
constexpr std::uint8_t STATION_ECM_FRAMES = 0x14;

constexpr std::uint16_t ROCK_TUMBLE_A = 0x0037;
constexpr std::uint16_t ROCK_TUMBLE_B = 0xFFDF;
constexpr std::uint8_t ROCK_TUMBLE_SWAP = 0x02;

constexpr std::uint16_t THARGOID_ECM_ODDS = 100;
constexpr std::uint8_t THARGOID_ECM_FRAMES = 0x1E;
constexpr std::uint16_t WOLF_SPIN = 0x14;
constexpr std::uint16_t ATTACK_RUN_SPIN = 0x0F;
constexpr std::uint8_t CALM = 10;
constexpr std::uint8_t RESTING_AGGRESSION = 9;
constexpr std::uint8_t PASS_COUNT_MASK = 3;
constexpr std::uint8_t FEWEST_PASSES = 2;
constexpr std::uint16_t BREAK_OFF_BOX = 1000;
constexpr std::uint16_t WOLF_MISSILE_ODDS = 1000;
constexpr std::uint8_t THARGON_ADRIFT = 0x0A;
constexpr std::uint8_t SLOWEST_ADRIFT = 0x0A;
constexpr std::uint8_t ADRIFT_SLOWING = 3;

constexpr std::uint16_t HUNTER_SPIN = 0x14;
constexpr std::uint16_t PACK_ATTACK_ODDS = 0x32;
constexpr std::uint16_t FORMATION_BOX = 0x7D0;
constexpr std::uint16_t HUNTER_MISSILE_ODDS = 0x5DC;
constexpr std::uint16_t CLOSING_BOX = 5000;
constexpr std::uint16_t CLOSING_MISSILE_ODDS = 0x9C4;
constexpr std::uint16_t JINK_FRAMES = 10;
constexpr std::uint16_t HALF_TURN = 0x400;

constexpr std::uint16_t ESCORT_BYTES_COPIED = 0x10;  // the type, position and heading
constexpr std::uint16_t ESCORT_SCATTER_MASK = 0x7FF; // a random -1024..1023 on each axis
constexpr std::uint16_t ESCORT_SCATTER_CENTER = 0x400;

constexpr std::uint16_t ROCK_SPIN = 0x14;
constexpr std::uint8_t POLICE_ATTACK_STATUS = 5; // the police attack an offender from this legal status
constexpr std::uint16_t TRADER_FLEE_ODDS = 0x53FC;
constexpr std::uint8_t TRADER_FLEE_ENERGY = 8;
constexpr std::uint16_t TRADER_BREAK_OFF_BOX = 0x320;
constexpr std::uint16_t POLICE_MISSILE_ODDS = 0x64;
constexpr std::uint8_t FUGITIVE_FOR_POLICE = 0x0A;
constexpr std::uint16_t FUGITIVE_MISSILE_ODDS = 0xBB8;
constexpr std::uint8_t DESPERATE_ENERGY = 3; // a fleeing trader fires a missile below this
constexpr std::uint16_t DESPERATE_MISSILE_ODDS = 0x3E8;
constexpr std::uint16_t TRADER_ECM_ODDS = 0x32;
constexpr std::uint8_t TRADER_ECM_FRAMES = 0x14;
constexpr std::uint8_t RETURN_ENERGY = 5; // a trader breaking off turns to flee below this, unless it is police

// A trader's or a police Viper's states (+17h).
constexpr std::uint8_t TRADER_DECIDING = 1;
constexpr std::uint8_t TRADER_ATTACKING = 2;
constexpr std::uint8_t TRADER_FLEEING = 3;
constexpr std::uint8_t TRADER_BREAKING_OFF = 4; // and anything above

// The AI states (+17h).
constexpr std::uint8_t STATE_IDLE = 0;
constexpr std::uint8_t STATE_ATTACK = 1;
constexpr std::uint8_t STATE_ATTACK_RUN = 2; // a wolf's; a hunter's 2 is formation flight
constexpr std::uint8_t STATE_FORMATION = 2;
constexpr std::uint8_t STATE_TURN_AWAY = 3;
constexpr std::uint8_t STATE_CLOSING = 4;

[[nodiscard]] std::uint16_t At(std::uint16_t _slot, int _field) noexcept
{
  return static_cast<std::uint16_t>(_slot + _field);
}

// WithinRange: whether _slot is within the box whose half size has its range byte for the high byte and _low, what DL holds
// there, for the low.
[[nodiscard]] bool WithinRange(const SpaceObject& _slot, std::uint8_t _low)
{
  return ObjectWithinBox(_slot, Join(_slot.range, _low));
}

// TurnToVector: _slot turned toward the angles of _vector, ConvertVectorToAngles' then TurnTowardAngles'.
Turn TurnToVector(GameState& _state, SpaceObject& _slot, Vector _vector)
{
  return TurnTowardAngles(_slot, ConvertVectorToAngles(_state, _vector));
}

// A government's row of spawnOddsByGovernment and of spawnLimitsByGovernment: four words and four bytes.
constexpr std::uint16_t SPAWN_ODDS_ROW_BYTES = 8;
constexpr std::uint16_t SPAWN_LIMIT_ROW_BYTES = 4;
// The classes' columns, each its entry's place in a row, a word of spawnOddsByGovernment's and a byte of
// spawnLimitsByGovernment's, where the original holds the address of the column's first entry.
constexpr std::size_t DRIFTER_COLUMN = 0;
constexpr std::size_t TRADER_COLUMN = 1;
constexpr std::size_t HUNTER_COLUMN = 2;
constexpr std::size_t WOLF_COLUMN = 3;

// The offset in spawnLimitsByGovernment of the row of the government spawnLimitOffset holds.
[[nodiscard]] std::uint16_t SpawnLimitRow(const GameState& _state) noexcept
{
  return static_cast<std::uint16_t>(_state.Data().ai.spawnLimitOffset * SPAWN_LIMIT_ROW_BYTES);
}

// What SpawnOddsMet draws: a random word, and the odds it is held against.
struct SpawnOdds
{
  bool met; // the random word is below the odds
  std::uint16_t random;
};

// NextRandom against the entry of the government spawnOddsOffset holds, in _column, a column of spawnOddsByGovernment,
// scaled.
[[nodiscard]] SpawnOdds SpawnOddsMet(GameState& _state, std::size_t _column)
{
  const std::uint16_t random = NextRandom(_state);
  const auto row = static_cast<std::size_t>(_state.Data().ai.spawnOddsOffset * SPAWN_ODDS_ROW_BYTES);
  const std::uint16_t odds = ScaleSpawnOdds(_state, WordIn(SPAWN_ODDS_BY_GOVERNMENT, row + 2 * _column));
  return SpawnOdds{random < odds, random};
}

// What BelowSpawnLimit finds: the government's limit for a class, and whether there is room below it.
struct SpawnLimit
{
  bool below;
};

// The entry of the government spawnLimitOffset holds in _column, a column of spawnLimitsByGovernment, against the class count
// _count: below while there is room.
[[nodiscard]] SpawnLimit BelowSpawnLimit(const GameState& _state, std::size_t _column, const Counted<std::uint8_t>& _count)
{
  const std::uint8_t limit = SPAWN_LIMITS_BY_GOVERNMENT[SpawnLimitRow(_state) + _column];
  return SpawnLimit{_count < limit};
}

// FindFreeShipSlot for a spawner: the slot it fills, when one is free. None ends UpdateObjectsAndSpawn.
[[nodiscard]] SlotSearch TakeFreeShipSlot(GameState& _state)
{
  return FindFreeShipSlot(_state);
}

// SpawnByGovernment (4A86): spawnOddsOffset and spawnLimitOffset the government, where the original holds the offsets of its
// rows, then a drifter, a trader, a hunter and a
// wolf, each while its class is below the government's limit and a random word is below its odds, in a free slot; outside
// anarchies a wolf needs a random word below 1C2h too. In witch space only a wolf, at no odds. A spawner that finds no free
// slot ends it.
void SpawnByGovernment(GameState& _state)
{
  _state.Data().ai.spawnOddsOffset = std::uint16_t{_state.Data().commander.currentGovernment};
  _state.Data().ai.spawnLimitOffset = std::uint16_t{_state.Data().flight.spawnGovernment};
  if (_state.Data().commander.witchspaceCountdown == 0)
  {
    // MOV AL,[BX+spawnLimitsByGovernment], INC AL with a mining laser fitted.
    const std::size_t row = SpawnLimitRow(_state);
    auto drifters = std::uint8_t{SPAWN_LIMITS_BY_GOVERNMENT[row + DRIFTER_COLUMN]};
    if (_state.Data().commander.miningLaserCount == 1)
    {
      drifters = static_cast<std::uint8_t>(drifters + 1);
    }
    if (_state.Data().ai.behaviorClassCounts.drifterCount < drifters && SpawnOddsMet(_state, DRIFTER_COLUMN).met)
    {
      const SlotSearch free = TakeFreeShipSlot(_state);
      if (!free.found)
      {
        return;
      }
      SpawnRandomDrifter(_state, _state.Slot(free.slot));
    }
    if (BelowSpawnLimit(_state, TRADER_COLUMN, _state.Data().ai.behaviorClassCounts.traderCount).below &&
        SpawnOddsMet(_state, TRADER_COLUMN).met)
    {
      const SlotSearch free = TakeFreeShipSlot(_state);
      if (!free.found)
      {
        return;
      }
      (void)SpawnRandomTrader(_state, _state.Slot(free.slot));
    }
    if (BelowSpawnLimit(_state, HUNTER_COLUMN, _state.Data().ai.behaviorClassCounts.hunterCount).below &&
        SpawnOddsMet(_state, HUNTER_COLUMN).met)
    {
      const SlotSearch free = TakeFreeShipSlot(_state);
      if (!free.found)
      {
        return;
      }
      SpawnRandomHunter(_state, _state.Slot(free.slot));
    }
  }
  if (!BelowSpawnLimit(_state, WOLF_COLUMN, _state.Data().ai.behaviorClassCounts.wolfCount).below)
  {
    return;
  }
  if (_state.Data().commander.witchspaceCountdown == 0)
  {
    const SpawnOdds odds = SpawnOddsMet(_state, WOLF_COLUMN);
    if (!odds.met || (_state.Data().flight.spawnGovernment != 0 && odds.random >= WOLF_ODDS_LIMIT))
    {
      return;
    }
  }
  if (const SlotSearch free = TakeFreeShipSlot(_state); free.found)
  {
    SpawnRandomWolf(_state, _state.Slot(free.slot));
  }
}

// The mask mission's Asp made the mask ship: it carries the device, its flashing starts with 20 frames shown (in the cargo's
// byte, which DropCargo does not read for a ship that carries the device), and no aggression yet.
void MarkMaskShip(SpaceObject& _slot)
{
  _slot.flags = static_cast<std::uint8_t>(_slot.flags | FLAG_DEVICE);
  _slot.flashFrames = MASK_SHIP_FLASH_FRAMES;
  _slot.aggression = 0;
}

// SpawnMaskMissionShip in a free slot, _maskShip saying whether it is the mask ship. Returns the slot, or nothing when none
// is free.
std::optional<SlotIndex> SpawnMaskMissionShipInFreeSlot(GameState& _state, bool _maskShip)
{
  const SlotSearch free = TakeFreeShipSlot(_state);
  if (!free.found)
  {
    return std::nullopt;
  }
  SpawnMaskMissionShip(_state, _state.Slot(free.slot), _maskShip);
  return free.slot;
}

// SpawnMaskMissionShips (4B73): with no wolves about, the mask ship, its slot kept in maskShipSlot, and two escorts placed near
// it (PlaceEscortNear); with one or two, one more mission ship, which becomes the mask ship, an Asp, when none carries the
// device. Nothing once the mask ship is destroyed, or with three wolves or more.
void SpawnMaskMissionShips(GameState& _state)
{
  if (_state.Data().commander.maskShipDestroyed == 1)
  {
    return;
  }
  const std::uint8_t wolves = _state.Data().ai.behaviorClassCounts.wolfCount;
  if (wolves != 0 && wolves >= MOST_WOLVES_WITH_MASK_SHIP)
  {
    return;
  }
  if (wolves == 0 && _state.Data().commander.maskShipDestroyed != 1)
  {
    const SlotSearch free = TakeFreeShipSlot(_state);
    if (!free.found)
    {
      return;
    }
    _state.Data().ai.maskShipSlot = free.slot;
    SpaceObject& maskShip = _state.Slot(free.slot);
    SpawnMaskMissionShip(_state, maskShip, true);
    MarkMaskShip(maskShip);
    for (int escort = 0; escort < 2; ++escort)
    {
      const std::optional<SlotIndex> slot = SpawnMaskMissionShipInFreeSlot(_state, false);
      if (!slot)
      {
        return;
      }
      PlaceEscortNear(_state, _state.Slot(*slot), _state.Slot(_state.Data().ai.maskShipSlot));
    }
    return;
  }
  const std::optional<SlotIndex> slot = SpawnMaskMissionShipInFreeSlot(_state, false);
  if (!slot || IsMaskShipPresent(_state).found || _state.Data().commander.maskShipDestroyed == 1)
  {
    return;
  }
  SpaceObject& ship = _state.Slot(*slot);
  MarkMaskShip(ship);
  ship.type = ASP_ACTIVE;
}

// SpawnInvasionWave (4BF7): a Thargoid in a free slot while the player is in the safe zone and fewer than 8 wolves fly.
void SpawnInvasionWave(GameState& _state)
{
  if (!InSafeZone(_state).inside || _state.Data().ai.behaviorClassCounts.wolfCount >= MOST_INVADERS)
  {
    return;
  }
  if (const SlotSearch free = TakeFreeShipSlot(_state); free.found)
  {
    SpawnInvasionThargoid(_state, _state.Slot(free.slot));
  }
}

// What the station's look for an offender found.
struct OffenderLaunch
{
  NearTest near;                    // what IsObjectNear found of the station
  std::optional<Velocity> launched; // when a copy of the station went into a free slot, what ComputeVelocity gave it
};

// From 55A9: the station at _station, near, launches a police Viper, a shuttle or a trader at an offender out of the box it
// guards: a copy of itself (CopyObject, backwards when _backward) in a free slot, out along z, turned round, its roll reversed,
// and its velocity set. PUSH DI and POP DI keep the station.
OffenderLaunch LaunchAtOffender(GameState& _state, SpaceObject& _station, bool _backward)
{
  OffenderLaunch launch{IsObjectNear(_state, _station), std::nullopt};
  if (!launch.near.nearby || ObjectWithinBox(_station, STATION_GUARD_BOX))
  {
    return launch;
  }
  const std::uint8_t status = _state.Data().commander.legalStatus;
  if (status < OFFENDER || NextRandom(_state) >= (status >= FUGITIVE ? FUGITIVE_LAUNCH_ODDS : OFFENDER_LAUNCH_ODDS))
  {
    return launch;
  }
  const SlotSearch free = FindFreeShipSlot(_state);
  if (!free.found)
  {
    return launch;
  }
  SpaceObject& ship = _state.Slot(free.slot);
  CopyObject(_state, _station, ship, _backward);
  const std::uint16_t pick = NextRandom(_state);
  if (pick >= POLICE_FROM)
  {
    InitPoliceViper(ship);
  }
  else if (pick >= SHUTTLE_FROM)
  {
    InitShuttle(ship);
  }
  else
  {
    SpawnRandomTrader(_state, ship);
  }
  // ADD [DI+8],0F0h / ADC [DI+3],0: out along z, turned round, its roll reversed.
  const std::uint32_t z = std::uint32_t{ship.z} + LAUNCH_DISTANCE;
  ship.z = static_cast<std::uint16_t>(z);
  ship.zHigh = static_cast<std::uint8_t>(ship.zHigh + (z >> 16));
  ship.yaw = Offset(ship.yaw, LAUNCH_TURN);
  ship.roll = Negate(ship.roll);
  launch.launched = ComputeVelocity(_state, ship);
  return launch;
}

// CheckMissilesAtStation (5611): a missile aimed at the station at _station, or at a police Viper in the safe zone, is a crime,
// added to legalStatus up to FFh (the sum written, then FFh over it on a carry, 5650 and 5656), and starts the station's ECM
// unless the player's anti-ECM is on; while npcEcmFrames runs, the ECM removes every missile, and an invasion stops it. Returns
// the last pixel of the last scanner blip the ECM erased, if it erased one.
std::optional<DashboardPixel> CheckMissilesAtStation(GameState& _state, const SpaceObject& _station)
{
  if (_state.Data().ai.npcEcmFrames == 0)
  {
    // MOV CL,objectSlotCount / XOR CH,CH, then LOOP: a count of 0 runs 65,536 times.
    std::optional<std::uint8_t> crime;
    SlotIndex slot = SlotIndex::Sun;
    for (std::uint32_t count = LoopCount(_state.Data().flight.objectSlotCount); count != 0 && !crime; --count)
    {
      const SpaceObject& missile = _state.Slot(slot);
      const std::uint8_t type = missile.type;
      const SlotIndex target = missile.target;
      if ((type & SpaceObject::ACTIVE) != 0 && ((type >> 1) & TYPE_MASK) == TYPE_MISSILE && target != SlotIndex::Player)
      {
        if (target == _state.IndexOf(_station))
        {
          crime = STATION_MISSILE_CRIME;
        }
        else if (IsPoliceViper(_state.Slot(target)) && InSafeZone(_state).inside)
        {
          crime = POLICE_MISSILE_CRIME;
        }
      }
      slot = SlotAfter(slot);
    }
    if (!crime)
    {
      return std::nullopt;
    }
    AddSaturating(_state.Data().commander.legalStatus, *crime);
    if (_state.Data().flight.antiEcmActive == 1)
    {
      return std::nullopt;
    }
    _state.Data().ai.npcEcmFrames = STATION_ECM_FRAMES;
  }
  if (_state.Data().commander.thargoidInvasionActive == 1)
  {
    _state.Data().ai.npcEcmFrames = 0;
    return std::nullopt;
  }
  _state.Data().playerShip.ecmFired = 1;
  const std::optional<DashboardPixel> erased = RemoveAllMissiles(_state);
  _state.Data().ai.npcEcmFrames = static_cast<std::uint8_t>(_state.Data().ai.npcEcmFrames - 1);
  return erased;
}

// UpdateWolfAi's state 3 (586D): turning away, a Thargoid launching Thargons, until beyond its range, whose box takes _rangeLow,
// what DL holds there, for its low byte; then a pass is counted off. After the last it rests, calmed (aggression 9) and no
// longer hostile, and a Thargon drifts (state 0Ah); so does one whose mother is gone or is no Thargoid with a blip. Returns what
// MoveObject did.
MovedObject WolfTurnAway(GameState& _state, SpaceObject& _slot, std::uint8_t _rangeLow, bool _backward)
{
  if (WithinRange(_slot, _rangeLow))
  {
    (void)TurnToVector(_state, _slot, GetObjectPosition(_slot));
    (void)ComputeVelocity(_state, _slot);
    TryLaunchThargon(_state, _slot, _backward);
    return MoveObject(_state, _slot);
  }
  const auto passes = static_cast<std::uint8_t>(_slot.passes - 1);
  _slot.passes = passes;
  bool rest = passes == 0;
  if (!rest && IsThargonType(_slot))
  {
    // A Thargon goes back to its run only while its mother is a Thargoid with a blip.
    const SlotIndex mother = _slot.owner;
    if (mother != SlotIndex::None)
    {
      const SpaceObject& owner = _state.Slot(mother);
      rest = ((owner.type >> 1) & TYPE_MASK) != TYPE_THARGOID || (owner.flags & FLAG_BLIP_DRAWN) == 0;
    }
  }
  if (rest)
  {
    _slot.aggression = RESTING_AGGRESSION;
    _slot.flags = static_cast<std::uint8_t>(_slot.flags & ~FLAG_HOSTILE);
    _slot.aiState = STATE_ATTACK;
    if (IsThargonType(_slot))
    {
      _slot.aiState = THARGON_ADRIFT;
    }
  }
  else
  {
    _slot.aiState = STATE_ATTACK_RUN;
  }
  return MoveObject(_state, _slot);
}

// MOV AH,1 / ROR AL,1 / NEG AX when the bit rotated out was set: a jink of +-(256 + AL/2, with AL's low bit on top).
[[nodiscard]] std::uint16_t Jink(std::uint16_t _random) noexcept
{
  const std::uint8_t low = Low(_random);
  const bool odd = (low & 1) != 0;
  const auto jink = static_cast<std::uint16_t>((1 << 8) | (low >> 1) | (odd ? 0x80 : 0));
  return odd ? Negate(jink) : jink;
}

// The evasive jinks of HunterEvade and TraderFlee: with no frames left, a new jink of the pitch and of the yaw and 10 frames;
// then a frame counted off, and at the last the jinks reversed and 10 frames again. Returns the angles turned half round from
// the player's direction, plus the jinks: where the ship turns to.
[[nodiscard]] Angles JinkAway(GameState& _state, SpaceObject& _slot)
{
  if (_slot.jinkFrames == 0)
  {
    _slot.jinkPitch = Jink(NextRandom(_state));
    _slot.jinkYaw = Jink(NextRandom(_state));
    _slot.jinkFrames = JINK_FRAMES;
  }
  const auto frames = static_cast<std::uint8_t>(_slot.jinkFrames - 1);
  _slot.jinkFrames = frames;
  if (frames == 0)
  {
    _slot.jinkFrames = JINK_FRAMES;
    _slot.jinkPitch = Negate(_slot.jinkPitch);
    _slot.jinkYaw = Negate(_slot.jinkYaw);
  }
  // ConvertVectorToAngles of GetVectorToPlayer, then ADD AX,400h / ADD AX,[DI+36h] / ADD BX,[DI+38h].
  const Angles toPlayer = ConvertVectorToAngles(_state, GetVectorToPlayer(_slot));
  return Angles{Offset(Offset(toPlayer.first, HALF_TURN), _slot.jinkPitch), Offset(toPlayer.second, _slot.jinkYaw)};
}

// UpdateHunterAi's state 3 (5987): evading with random jinks, and a missile at odds of 5DCh in 65536, until beyond its range,
// whose box takes _rangeLow, what DL holds there, for its low byte. Returns what MoveObject did.
MovedObject HunterEvade(GameState& _state, SpaceObject& _slot, std::uint8_t _rangeLow, bool _backward)
{
  if (!WithinRange(_slot, _rangeLow))
  {
    _slot.aiState = STATE_IDLE;
    return MoveObject(_state, _slot);
  }
  (void)TurnTowardAngles(_slot, JinkAway(_state, _slot));
  TryLaunchMissileAtPlayer(_state, _slot, HUNTER_MISSILE_ODDS, _backward);
  (void)ComputeVelocity(_state, _slot);
  return MoveObject(_state, _slot);
}

// UpdateHunterAi's state 0 (58E4): flying at the player until it turns hostile, then picking how to hunt: with two or more
// pack mates on the scanner it attacks a fugitive, or at odds of 32h in 65536 anyone; with one it flies in formation with it.
// Returns what MoveObject did.
MovedObject HunterIdle(GameState& _state, SpaceObject& _slot)
{
  const std::uint8_t flags = _slot.flags;
  if ((flags & FLAG_BLIP_DRAWN) == 0)
  {
    (void)TurnToVector(_state, _slot, GetVectorToPlayer(_slot));
    (void)ComputeVelocity(_state, _slot);
    return MoveObject(_state, _slot);
  }
  if ((flags & FLAG_HOSTILE) != 0)
  {
    _slot.aiState = STATE_TURN_AWAY;
  }
  else
  {
    const HunterCount hunters = CountOtherHuntersOnScanner(_state, _slot);
    if (hunters.count >= 2)
    {
      if (_state.Data().commander.legalStatus >= FUGITIVE || NextRandom(_state) < PACK_ATTACK_ODDS)
      {
        _slot.aiState = STATE_ATTACK;
      }
    }
    else if (hunters.count == 1 && hunters.last)
    {
      // MOV [DI+29h],BP: the mate CountOtherHuntersOnScanner found last, which a count of 1 always has.
      _slot.target = *hunters.last;
      _slot.aiState = STATE_FORMATION;
    }
  }
  return MoveObject(_state, _slot);
}

// UpdateTraderOrPoliceAi's state 1 (56CE): a police Viper attacks a player of legal status 5 or more; otherwise one the player
// has hit flees, at odds of 53FCh in 65536, or attacks. Returns what MoveObject did.
MovedObject TraderDecide(GameState& _state, SpaceObject& _slot)
{
  if (IsPoliceViper(_slot) && _state.Data().commander.legalStatus >= POLICE_ATTACK_STATUS)
  {
    _slot.aiState = TRADER_ATTACKING;
  }
  else if ((_slot.flags & FLAG_HOSTILE) != 0)
  {
    _slot.aiState = NextRandom(_state) < TRADER_FLEE_ODDS ? TRADER_FLEEING : TRADER_ATTACKING;
  }
  return MoveObject(_state, _slot);
}

// UpdateTraderOrPoliceAi's state 2 (56FC): at the player, firing, the police with missiles against an offender, until within
// 320h on every axis, or, for any but the police, until its energy is below 8. Returns what MoveObject did.
MovedObject TraderAttack(GameState& _state, SpaceObject& _slot, bool _backward)
{
  if (!IsPoliceViper(_slot) && _slot.energy < TRADER_FLEE_ENERGY)
  {
    _slot.aiState = TRADER_FLEEING;
    return MoveObject(_state, _slot);
  }
  if (ObjectWithinBox(_slot, TRADER_BREAK_OFF_BOX))
  {
    _slot.aiState = TRADER_BREAKING_OFF;
    return MoveObject(_state, _slot);
  }
  const Turn turn = TurnToVector(_state, _slot, GetVectorToPlayer(_slot));
  TryFireLaserAtPlayer(_state, _slot, turn.pitch.errorMagnitude, turn.yaw.errorMagnitude);
  const std::uint8_t status = _state.Data().commander.legalStatus;
  if (IsPoliceViper(_slot) && status != 0)
  {
    TryLaunchMissileAtPlayer(_state, _slot, status >= FUGITIVE_FOR_POLICE ? FUGITIVE_MISSILE_ODDS : POLICE_MISSILE_ODDS, _backward);
  }
  (void)ComputeVelocity(_state, _slot);
  return MoveObject(_state, _slot);
}

// UpdateTraderOrPoliceAi's state 3 (5750): fleeing from the player with random jinks, a missile when nearly dead, and now and
// then an ECM. Returns what MoveObject did.
MovedObject TraderFlee(GameState& _state, SpaceObject& _slot, bool _backward)
{
  if (_slot.energy < DESPERATE_ENERGY)
  {
    TryLaunchMissileAtPlayer(_state, _slot, DESPERATE_MISSILE_ODDS, _backward);
  }
  (void)TurnTowardAngles(_slot, JinkAway(_state, _slot));
  (void)ComputeVelocity(_state, _slot);
  if (NextRandom(_state) < TRADER_ECM_ODDS)
  {
    _state.Data().ai.npcEcmFrames = TRADER_ECM_FRAMES;
  }
  return MoveObject(_state, _slot);
}

// UpdateTraderOrPoliceAi's state 4 and above (57BC): flying on away until beyond its range, then attacking again; weak, any but
// the police flee. Its range's box takes _rangeLow, what DL holds there, for its low byte. Returns what MoveObject did.
MovedObject TraderBreakOff(GameState& _state, SpaceObject& _slot, std::uint8_t _rangeLow)
{
  if (!WithinRange(_slot, _rangeLow))
  {
    _slot.aiState = TRADER_ATTACKING;
    return MoveObject(_state, _slot);
  }
  if (_slot.energy < RETURN_ENERGY && !IsPoliceViper(_slot))
  {
    _slot.aiState = TRADER_FLEEING;
    return MoveObject(_state, _slot);
  }
  (void)TurnToVector(_state, _slot, GetObjectPosition(_slot));
  (void)ComputeVelocity(_state, _slot);
  return MoveObject(_state, _slot);
}

// CALL [BX+behaviorHandlers] (4A41): the handler of class _behavior for _slot, which may take DL of _dx, the DX the last handler
// or the caller left, for its range's box (VectorWithinBox after MOV DH,[DI+1Ch] at 57BC, 5873 and 5987), and the direction flag
// a launch's copy runs by, _backward. BX holds the class doubled, which UpdateMissileAi's explosion would save in the divide
// trap. Returns what the handler leaves in DX. The table's eight words, which nothing writes, name these eight handlers; a class
// of 8 or more, which no template gives, would run data as code, and runs nothing.
std::uint16_t RunBehaviorHandler(GameState& _state, Hardware& _hardware, SpaceObject& _slot, std::uint8_t _behavior, bool _backward,
                                 std::uint16_t _dx)
{
  switch (_behavior)
  {
  case INERT_CLASS:
    return _dx;
  case STATION_CLASS:
    return UpdateStationAi(_state, _slot, _backward, _dx);
  case MISSILE_CLASS:
    return UpdateMissileAi(_state, _hardware, _slot, _backward, static_cast<std::uint16_t>(_behavior << 1), _dx).dx;
  case DRIFTING_CLASS:
    return UpdateDriftingObjectAi(_state, _slot).dx;
  case TRADER_CLASS:
    return UpdateTraderOrPoliceAi(_state, _slot, Low(_dx), _backward).dx;
  case WOLF_CLASS:
    return UpdateWolfAi(_state, _slot, Low(_dx), _backward).dx;
  case HUNTER_CLASS:
    return UpdateHunterAi(_state, _slot, Low(_dx), _backward).dx;
  case DEBRIS_CLASS:
  {
    // A fragment that has run out of lifetime is only cleared, and leaves DX alone.
    const std::optional<MovedObject> moved = UpdateDebrisAi(_state, _slot);
    return moved ? moved->dx : _dx;
  }
  default:
    return _dx;
  }
}

} // namespace

void UpdateObjectsAndSpawn(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _dx)
{
  // PUSH DS / POP ES / MOV DI,activeObjectCount / XOR AL,AL / MOV CX,9 / REP STOSB: activeObjectCount and the eight
  // behaviorClassCounts cleared, backwards when the direction flag is set.
  std::uint16_t count = _state.OffsetOf(_state.Data().ai.activeObjectCount);
  for (std::uint16_t bytes = CLASS_COUNTS_CLEARED; bytes != 0; --bytes)
  {
    _state.SetByte(count, 0);
    count = _backward ? static_cast<std::uint16_t>(count - 1) : At(count, 1);
  }
  // Every slot of shipSlotCount, by LOOP: an active one counted, but for debris, and in its class's count, and its class's handler
  // run, CX and DI pushed round it. DX goes on from one handler to the next.
  std::uint16_t dx = _dx;
  SlotIndex slot = SlotIndex::Sun;
  for (std::uint32_t slots = LoopCount(_state.Data().world.shipSlotCount); slots != 0; --slots)
  {
    SpaceObject& object = _state.Slot(slot);
    if ((object.type & SLOT_ACTIVE) != 0)
    {
      const std::uint8_t behavior = object.behaviorClass;
      if (behavior != DEBRIS_CLASS)
      {
        _state.Data().ai.activeObjectCount = static_cast<std::uint8_t>(_state.Data().ai.activeObjectCount + 1);
      }
      // The class's count in behaviorClassCounts, by the class: INC BYTE PTR [BX+behaviorClassCounts].
      Counted<std::uint8_t>& classCount = _state.BytesFrom(_state.Data().ai.behaviorClassCounts, std::size_t{behavior} + 1)[behavior];
      classCount = static_cast<std::uint8_t>(classCount + 1);
      dx = RunBehaviorHandler(_state, _hardware, object, behavior, _backward, dx);
    }
    slot = SlotAfter(slot);
  }

  _state.Data().drawing.maskingBackgroundColor = 0;
  if (_state.Data().ai.activeObjectCount >= MOST_ACTIVE_FOR_SPAWNING ||
      _state.Data().flight.objectSlotCount < _state.Data().ai.activeObjectCount)
  {
    return;
  }
  if (_state.Data().commander.maskMissionShipsLeft != 0)
  {
    if (_state.Data().commander.maskSystemJumps == 1)
    {
      SpawnMaskMissionShips(_state);
      return;
    }
  }
  else if ((_state.Data().commander.thargoidInvasionActive & _state.Data().commander.jumpedSinceBriefing) != 0 &&
           _state.Data().commander.invadedStationDestroyed != 1)
  {
    SpawnInvasionWave(_state);
    return;
  }
  SpawnByGovernment(_state);
}

std::uint16_t ScaleSpawnOdds(const GameState& _state, std::uint16_t _odds)
{
  if (_state.Data().flight.jumpDriveEngaged != 1)
  {
    return _odds;
  }
  return static_cast<std::uint16_t>(_odds << JUMP_DRIVE_ODDS_SHIFT);
}

SlotSearch IsMaskShipPresent(GameState& _state)
{
  // Every object slot, active or not, for the device's bit: LOOP from CX = objectSlotCount.
  SlotIndex slot = SlotIndex::Sun;
  for (std::uint32_t count = LoopCount(_state.Data().flight.objectSlotCount); count != 0; --count)
  {
    if ((_state.Slot(slot).flags & FLAG_DEVICE) != 0)
    {
      return SlotSearch{true, slot};
    }
    slot = SlotAfter(slot);
  }
  return SlotSearch{false, slot};
}

void PlaceEscortNear(GameState& _state, SpaceObject& _escort, const SpaceObject& _leader)
{
  // The leader's first 16 bytes, byte by byte: its type too.
  const std::span<Counted<std::uint8_t>> escort = _state.BytesFrom(_escort, ESCORT_BYTES_COPIED);
  const std::span<Counted<std::uint8_t>> leader = _state.BytesFrom(_leader, ESCORT_BYTES_COPIED);
  for (std::size_t index = 0; index < ESCORT_BYTES_COPIED; ++index)
  {
    escort[index] = std::uint8_t{leader[index]};
  }
  for (int axis = 0; axis < 3; ++axis)
  {
    const auto scatter = static_cast<std::uint16_t>((NextRandom(_state) & ESCORT_SCATTER_MASK) - ESCORT_SCATTER_CENTER);
    AddToCoordinate(_escort, axis, static_cast<std::int16_t>(scatter));
  }
}

Turn TurnTowardAngles(SpaceObject& _slot, Angles _wanted)
{
  const TurnStep pitch = ClampTurnStep(_slot, _wanted.first, _slot.pitch);
  _slot.pitch = Offset(_slot.pitch, static_cast<std::uint16_t>(pitch.step));
  const TurnStep yaw = ClampTurnStep(_slot, _wanted.second, _slot.yaw);
  _slot.yaw = Offset(_slot.yaw, static_cast<std::uint16_t>(yaw.step));
  return Turn{pitch, yaw};
}

TurnStep ClampTurnStep(const SpaceObject& _slot, std::uint16_t _wanted, std::uint16_t _current)
{
  // The error is not wrapped at 2048, so a turn across 0 goes the long way.
  const auto error = static_cast<std::uint16_t>((_wanted & ANGLE_MASK) - (_current & ANGLE_MASK));
  const std::uint16_t magnitude = (error & 0x8000) != 0 ? Negate(error) : error;
  const std::uint16_t rate = _slot.turnRate;
  if (magnitude < rate)
  {
    return TurnStep{static_cast<std::int16_t>(error), magnitude};
  }
  return TurnStep{static_cast<std::int16_t>((error & 0x8000) != 0 ? Negate(rate) : rate), magnitude};
}

HunterCount CountOtherHuntersOnScanner(GameState& _state, const SpaceObject& _slot)
{
  HunterCount hunters{0, std::nullopt};
  SlotIndex slot = SlotIndex::Sun;
  for (std::uint32_t count = LoopCount(_state.Data().flight.objectSlotCount); count != 0; --count)
  {
    const SpaceObject& other = _state.Slot(slot);
    if (other.behaviorClass == HUNTER_CLASS && (other.flags & FLAG_BLIP_DRAWN) != 0 && &other != &_slot)
    {
      hunters.last = slot;
      hunters.count = static_cast<std::uint8_t>(hunters.count + 1);
    }
    slot = SlotAfter(slot);
  }
  return hunters;
}

VectorToObject GetVectorToObject(const SpaceObject& _slot, const SpaceObject& _object, std::uint16_t _halfSize)
{
  // Each coordinate quartered (SAR twice) before the difference.
  const auto difference = [&_slot, &_object](ObjectWord _axis)
  { return static_cast<std::int16_t>(Sar(_object.*_axis, 2) - Sar(_slot.*_axis, 2)); };
  const Vector vector{difference(&SpaceObject::x), difference(&SpaceObject::y), difference(&SpaceObject::z)};
  return VectorToObject{vector, VectorWithinBox(vector, static_cast<std::uint16_t>(_halfSize >> 2))};
}

std::uint16_t UpdateStationAi(GameState& _state, SpaceObject& _station, bool _backward, std::uint16_t _dx)
{
  _station.roll = Offset(_station.roll, STATION_SPIN);
  std::uint16_t dx = _dx;
  if (_state.Data().flight.spawnGovernment >= 1 && (_station.flags & FLAG_HOSTILE) != 0)
  {
    // DX: the last pixel of the blip IsObjectNear erased, or MOV DX,1C2h, the guarded box, once it is near; then what
    // ComputeVelocity leaves once it launched.
    const OffenderLaunch launch = LaunchAtOffender(_state, _station, _backward);
    if (launch.near.erasedBlip)
    {
      dx = PixelPlace(*launch.near.erasedBlip);
    }
    if (launch.near.nearby)
    {
      dx = STATION_GUARD_BOX;
    }
    if (launch.launched)
    {
      dx = launch.launched->dx;
    }
  }
  // DX: the last pixel of the last blip RemoveAllMissiles erased, once the ECM ran.
  if (const std::optional<DashboardPixel> erased = CheckMissilesAtStation(_state, _station))
  {
    dx = PixelPlace(*erased);
  }
  return dx;
}

MovedObject UpdateDriftingObjectAi(GameState& _state, SpaceObject& _slot)
{
  const MovedObject moved = MoveObject(_state, _slot);
  if (!IsDebrisType(_slot))
  {
    return moved;
  }
  // MOV AX,37h / MOV BX,0FFDFh, exchanged unless bit 1 of the type byte is set: AX to the pitch, then BX to the roll.
  Pair tumble{static_cast<std::int16_t>(ROCK_TUMBLE_A), static_cast<std::int16_t>(ROCK_TUMBLE_B)};
  if ((_slot.type & ROCK_TUMBLE_SWAP) == 0)
  {
    std::swap(tumble.first, tumble.second);
  }
  _slot.pitch = Offset(_slot.pitch, static_cast<std::uint16_t>(tumble.first));
  _slot.roll = Offset(_slot.roll, static_cast<std::uint16_t>(tumble.second));
  return moved;
}

MovedObject UpdateTraderOrPoliceAi(GameState& _state, SpaceObject& _slot, std::uint8_t _rangeLow, bool _backward)
{
  if (IsDebrisType(_slot))
  {
    // A rock only spins. The split at 56B1 (LaunchShipFromObject with DL=5) never runs: 56A5 jumps past it when bit 0 of +1Eh is
    // clear, and 56AB when it is set.
    _slot.roll = Offset(_slot.roll, ROCK_SPIN);
    return MoveObject(_state, _slot);
  }
  switch (_slot.aiState)
  {
  case STATE_IDLE:
    _slot.aiState = TRADER_DECIDING;
    return MoveObject(_state, _slot);
  case TRADER_DECIDING:
    return TraderDecide(_state, _slot);
  case TRADER_ATTACKING:
    return TraderAttack(_state, _slot, _backward);
  case TRADER_FLEEING:
    return TraderFlee(_state, _slot, _backward);
  default:
    return TraderBreakOff(_state, _slot, _rangeLow);
  }
}

MovedObject UpdateWolfAi(GameState& _state, SpaceObject& _slot, std::uint8_t _rangeLow, bool _backward)
{
  // A Thargoid sets off its ECM at odds of 100 in 65536; a Thargoid or a Thargon spins.
  bool spins = IsThargoidType(_slot);
  if (spins)
  {
    if (NextRandom(_state) < THARGOID_ECM_ODDS)
    {
      _state.Data().ai.npcEcmFrames = THARGOID_ECM_FRAMES;
    }
  }
  else
  {
    spins = IsThargonType(_slot);
  }
  if (spins)
  {
    _slot.roll = Offset(_slot.roll, WOLF_SPIN);
  }
  switch (_slot.aiState)
  {
  case STATE_IDLE:
    _slot.aiState = STATE_ATTACK;
    return MoveObject(_state, _slot);
  case STATE_ATTACK:
    // Resting: a new set of 2-5 passes once it is not hostile, or calm no more: AND AH,3 / ADD AH,2.
    if ((_slot.flags & FLAG_HOSTILE) == 0 || _slot.aggression >= CALM)
    {
      _slot.passes = static_cast<std::uint8_t>((High(NextRandom(_state)) & PASS_COUNT_MASK) + FEWEST_PASSES);
      _slot.aiState = STATE_ATTACK_RUN;
    }
    return MoveObject(_state, _slot);
  case STATE_ATTACK_RUN:
  {
    if (ObjectWithinBox(_slot, BREAK_OFF_BOX))
    {
      _slot.aiState = STATE_TURN_AWAY;
      return MoveObject(_state, _slot);
    }
    const Turn turn = TurnToVector(_state, _slot, GetVectorToPlayer(_slot));
    TryFireLaserAtPlayer(_state, _slot, turn.pitch.errorMagnitude, turn.yaw.errorMagnitude);
    TryLaunchMissileAtPlayer(_state, _slot, WOLF_MISSILE_ODDS, _backward);
    TryLaunchThargon(_state, _slot, _backward);
    (void)ComputeVelocity(_state, _slot);
    const MovedObject moved = MoveObject(_state, _slot);
    _slot.roll = Offset(_slot.roll, ATTACK_RUN_SPIN);
    return moved;
  }
  case STATE_TURN_AWAY:
    return WolfTurnAway(_state, _slot, _rangeLow, _backward);
  default:
    // A Thargon adrift slows down to 10.
    if (_slot.speed >= SLOWEST_ADRIFT)
    {
      _slot.speed = static_cast<std::uint8_t>(_slot.speed - ADRIFT_SLOWING);
      (void)ComputeVelocity(_state, _slot);
    }
    return MoveObject(_state, _slot);
  }
}

MovedObject UpdateHunterAi(GameState& _state, SpaceObject& _slot, std::uint8_t _rangeLow, bool _backward)
{
  switch (_slot.aiState)
  {
  case STATE_IDLE:
    return HunterIdle(_state, _slot);
  case STATE_ATTACK:
  {
    if (ObjectWithinBox(_slot, BREAK_OFF_BOX))
    {
      _slot.aiState = STATE_TURN_AWAY;
      return MoveObject(_state, _slot);
    }
    const Turn turn = TurnToVector(_state, _slot, GetVectorToPlayer(_slot));
    TryFireLaserAtPlayer(_state, _slot, turn.pitch.errorMagnitude, turn.yaw.errorMagnitude);
    TryLaunchMissileAtPlayer(_state, _slot, HUNTER_MISSILE_ODDS, _backward);
    (void)ComputeVelocity(_state, _slot);
    _slot.roll = Offset(_slot.roll, HUNTER_SPIN);
    return MoveObject(_state, _slot);
  }
  case STATE_FORMATION:
  {
    // In formation with its pack mate, until within 7D0h of it on every axis (GetVectorToObject).
    const VectorToObject toMate = GetVectorToObject(_slot, _state.Slot(_slot.target), FORMATION_BOX);
    if (toMate.within)
    {
      _slot.aiState = STATE_CLOSING;
      return MoveObject(_state, _slot);
    }
    (void)TurnToVector(_state, _slot, toMate.vector);
    (void)ComputeVelocity(_state, _slot);
    return MoveObject(_state, _slot);
  }
  case STATE_TURN_AWAY:
    return HunterEvade(_state, _slot, _rangeLow, _backward);
  default:
  {
    // Closing on the player while farther than 5000 on some axis.
    if (ObjectWithinBox(_slot, CLOSING_BOX))
    {
      _slot.aiState = STATE_IDLE;
      return MoveObject(_state, _slot);
    }
    const Turn turn = TurnToVector(_state, _slot, GetVectorToPlayer(_slot));
    TryFireLaserAtPlayer(_state, _slot, turn.pitch.errorMagnitude, turn.yaw.errorMagnitude);
    TryLaunchMissileAtPlayer(_state, _slot, CLOSING_MISSILE_ODDS, _backward);
    (void)ComputeVelocity(_state, _slot);
    return MoveObject(_state, _slot);
  }
  }
}

bool CheckSafeZoneHoldFire(const GameState& _state, const SpaceObject& _slot)
{
  if (_state.Data().commander.thargoidInvasionActive == 1 || IsPoliceViper(_slot))
  {
    return false;
  }
  return InSafeZone(_state).inside;
}

} // namespace Elite
