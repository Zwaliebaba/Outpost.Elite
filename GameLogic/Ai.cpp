#include "pch.h"

#include "Ai.h"

#include "Arithmetic.h"
#include "Combat.h"
#include "DataOverlay.h"
#include "Maths.h"
#include "Ships.h"

#include <utility>

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_DIRECTION;

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
[[nodiscard]] bool WithinRange(const ObjectSlot& _slot, std::uint8_t _low)
{
  return ObjectWithinBox(_slot, Join(_slot.Get(SlotByte::Range), _low));
}

// What TurnTowardAngles leaves in the registers, which its entry and TurnToVector's register code reproduce: AX and BX the
// errors' magnitudes, which TryFireLaserAtPlayer takes as its aim errors; CX the yaw's turn rate, negated when it is the
// step, and BP the pitch error's magnitude, which UpdateMissileAi's contract compares.
void TurnOut(Machine::Registers& _regs, const ObjectSlot& _slot, Turn _turn) noexcept
{
  _regs.ax = _turn.pitch.errorMagnitude;
  _regs.bx = _turn.yaw.errorMagnitude;
  const std::uint16_t rate = _slot.Get(SlotByte::TurnRate);
  _regs.cx = _turn.yaw.errorMagnitude < rate ? rate : static_cast<std::uint16_t>(_turn.yaw.step);
  _regs.bp = _turn.pitch.errorMagnitude;
}

// TurnToVector: _slot turned toward the angles of _vector, ConvertVectorToAngles' then TurnTowardAngles'.
Turn TurnToVector(GameState& _state, ObjectSlot _slot, Vector _vector)
{
  return TurnTowardAngles(_slot, ConvertVectorToAngles(_state, _vector));
}

// What SpawnOddsMet draws: a random word, and the odds it is held against.
struct SpawnOdds
{
  bool met; // the random word is below the odds
  std::uint16_t random;
  std::uint16_t odds;
};

// NextRandom against the government's entry, at spawnOddsOffset, of the column of spawnOddsByGovernment at _column, scaled.
[[nodiscard]] SpawnOdds SpawnOddsMet(GameState& _state, std::uint16_t _column)
{
  const std::uint16_t random = NextRandom(_state);
  const std::uint16_t odds = ScaleSpawnOdds(_state, _state.Word(At(_column, _state.Get(DS.spawnOddsOffset))));
  return SpawnOdds{random < odds, random, odds};
}

// What BelowSpawnLimit finds: the government's limit for a class, and whether there is room below it.
struct SpawnLimit
{
  bool below;
  std::uint8_t limit; // what the original loads into AL
};

// The government's entry, at spawnLimitOffset, of the column of spawnLimitsByGovernment at _column, against the class count
// _count: below while there is room.
[[nodiscard]] SpawnLimit BelowSpawnLimit(const GameState& _state, std::uint16_t _column, DataField<std::uint8_t> _count)
{
  const std::uint8_t limit = _state.Byte(At(_column, _state.Get(DS.spawnLimitOffset)));
  return SpawnLimit{_state.Get(_count) < limit, limit};
}

// FindFreeShipSlot for a spawner: the slot it fills, when one is free. None ends UpdateObjectsAndSpawn.
[[nodiscard]] SlotSearch TakeFreeShipSlot(GameState& _state)
{
  return FindFreeShipSlot(_state);
}

// SpawnByGovernment (4A86): spawnOddsOffset and spawnLimitOffset from the government, then a drifter, a trader, a hunter and a
// wolf, each while its class is below the government's limit and a random word is below its odds, in a free slot; outside
// anarchies a wolf needs a random word below 1C2h too. In witch space only a wolf, at no odds. A spawner that finds no free
// slot ends it.
void SpawnByGovernment(GameState& _state)
{
  _state.Set(DS.spawnOddsOffset, static_cast<std::uint16_t>(_state.Get(DS.currentGovernment) << 3));
  _state.Set(DS.spawnLimitOffset, static_cast<std::uint16_t>(_state.Get(DS.spawnGovernment) << 2));
  if (_state.Get(DS.witchspaceCountdown) == 0)
  {
    // MOV AL,[BX+spawnLimitsByGovernment], INC AL with a mining laser fitted.
    auto drifters = _state.Byte(At(DS.spawnLimitsByGovernment.offset, _state.Get(DS.spawnLimitOffset)));
    if (_state.Get(DS.miningLaserCount) == 1)
    {
      drifters = static_cast<std::uint8_t>(drifters + 1);
    }
    if (_state.Get(DS.drifterCount) < drifters && SpawnOddsMet(_state, DS.spawnOddsByGovernment.offset).met)
    {
      const SlotSearch free = TakeFreeShipSlot(_state);
      if (!free.found)
      {
        return;
      }
      SpawnRandomDrifter(_state, ObjectSlot(_state, free.slot));
    }
    if (BelowSpawnLimit(_state, DS.traderLimitColumn.offset, DS.traderCount).below && SpawnOddsMet(_state, DS.traderOddsColumn.offset).met)
    {
      const SlotSearch free = TakeFreeShipSlot(_state);
      if (!free.found)
      {
        return;
      }
      (void)SpawnRandomTrader(_state, ObjectSlot(_state, free.slot));
    }
    if (BelowSpawnLimit(_state, DS.hunterLimitColumn.offset, DS.hunterCount).below && SpawnOddsMet(_state, DS.hunterOddsColumn.offset).met)
    {
      const SlotSearch free = TakeFreeShipSlot(_state);
      if (!free.found)
      {
        return;
      }
      SpawnRandomHunter(_state, ObjectSlot(_state, free.slot));
    }
  }
  if (!BelowSpawnLimit(_state, DS.wolfLimitColumn.offset, DS.wolfCount).below)
  {
    return;
  }
  if (_state.Get(DS.witchspaceCountdown) == 0)
  {
    const SpawnOdds odds = SpawnOddsMet(_state, DS.wolfOddsColumn.offset);
    if (!odds.met || (_state.Get(DS.spawnGovernment) != 0 && odds.random >= WOLF_ODDS_LIMIT))
    {
      return;
    }
  }
  if (const SlotSearch free = TakeFreeShipSlot(_state); free.found)
  {
    SpawnRandomWolf(_state, ObjectSlot(_state, free.slot));
  }
}

// The mask mission's Asp made the mask ship: it carries the device, its flashing starts with 20 frames shown (in the cargo's
// byte, which DropCargo does not read for a ship that carries the device), and no aggression yet.
void MarkMaskShip(ObjectSlot _slot)
{
  _slot.Set(SlotByte::Flags, static_cast<std::uint8_t>(_slot.Get(SlotByte::Flags) | FLAG_DEVICE));
  _slot.Set(SlotByte::FlashFrames, MASK_SHIP_FLASH_FRAMES);
  _slot.Set(SlotByte::Aggression, 0);
}

// SpawnMaskMissionShip in a free slot, _maskShip saying whether it is the mask ship. Returns the slot, or nothing when none
// is free.
std::optional<std::uint16_t> SpawnMaskMissionShipInFreeSlot(GameState& _state, bool _maskShip)
{
  const SlotSearch free = TakeFreeShipSlot(_state);
  if (!free.found)
  {
    return std::nullopt;
  }
  SpawnMaskMissionShip(_state, ObjectSlot(_state, free.slot), _maskShip);
  return free.slot;
}

// SpawnMaskMissionShips (4B73): with no wolves about, the mask ship, its slot kept in maskShipSlot, and two escorts placed near
// it (PlaceEscortNear); with one or two, one more mission ship, which becomes the mask ship, an Asp, when none carries the
// device. Nothing once the mask ship is destroyed, or with three wolves or more.
void SpawnMaskMissionShips(GameState& _state)
{
  if (_state.Get(DS.maskShipDestroyed) == 1)
  {
    return;
  }
  const std::uint8_t wolves = _state.Get(DS.wolfCount);
  if (wolves != 0 && wolves >= MOST_WOLVES_WITH_MASK_SHIP)
  {
    return;
  }
  if (wolves == 0 && _state.Get(DS.maskShipDestroyed) != 1)
  {
    const SlotSearch free = TakeFreeShipSlot(_state);
    if (!free.found)
    {
      return;
    }
    _state.Set(DS.maskShipSlot, free.slot);
    const ObjectSlot maskShip(_state, free.slot);
    SpawnMaskMissionShip(_state, maskShip, true);
    MarkMaskShip(maskShip);
    for (int escort = 0; escort < 2; ++escort)
    {
      const std::optional<std::uint16_t> slot = SpawnMaskMissionShipInFreeSlot(_state, false);
      if (!slot)
      {
        return;
      }
      PlaceEscortNear(_state, ObjectSlot(_state, *slot), _state.Get(DS.maskShipSlot));
    }
    return;
  }
  const std::optional<std::uint16_t> slot = SpawnMaskMissionShipInFreeSlot(_state, false);
  if (!slot || IsMaskShipPresent(_state).found || _state.Get(DS.maskShipDestroyed) == 1)
  {
    return;
  }
  ObjectSlot ship(_state, *slot);
  MarkMaskShip(ship);
  ship.Set(SlotByte::Type, ASP_ACTIVE);
}

// SpawnInvasionWave (4BF7): a Thargoid in a free slot while the player is in the safe zone and fewer than 8 wolves fly.
void SpawnInvasionWave(GameState& _state)
{
  if (!InSafeZone(_state).inside || _state.Get(DS.wolfCount) >= MOST_INVADERS)
  {
    return;
  }
  if (const SlotSearch free = TakeFreeShipSlot(_state); free.found)
  {
    SpawnInvasionThargoid(_state, ObjectSlot(_state, free.slot));
  }
}

// What the station's look for an offender found.
struct OffenderLaunch
{
  NearTest near;                          // what IsObjectNear found of the station
  std::optional<Velocity> launched;       // when a copy of the station went into a free slot, what ComputeVelocity gave it
  std::optional<RandomRollFacing> trader; // when the copy was made a trader, what FacePlayerWithRandomRoll gave it
};

// From 55A9: the station at _station, near, launches a police Viper, a shuttle or a trader at an offender out of the box it
// guards: a copy of itself (CopyObject, backwards when _backward) in a free slot, out along z, turned round, its roll reversed,
// and its velocity set. PUSH DI and POP DI keep the station.
OffenderLaunch LaunchAtOffender(GameState& _state, ObjectSlot _station, bool _backward)
{
  OffenderLaunch launch{IsObjectNear(_state, _station), std::nullopt, std::nullopt};
  if (!launch.near.nearby || ObjectWithinBox(_station, STATION_GUARD_BOX))
  {
    return launch;
  }
  const std::uint8_t status = _state.Get(DS.legalStatus);
  if (status < OFFENDER || NextRandom(_state) >= (status >= FUGITIVE ? FUGITIVE_LAUNCH_ODDS : OFFENDER_LAUNCH_ODDS))
  {
    return launch;
  }
  const SlotSearch free = FindFreeShipSlot(_state);
  if (!free.found)
  {
    return launch;
  }
  CopyObject(_state, _station.Offset(), free.slot, _backward);
  ObjectSlot ship(_state, free.slot);
  const std::uint16_t pick = NextRandom(_state);
  if (pick >= POLICE_FROM)
  {
    InitPoliceViper(_state, ship);
  }
  else if (pick >= SHUTTLE_FROM)
  {
    InitShuttle(_state, ship);
  }
  else
  {
    launch.trader = SpawnRandomTrader(_state, ship);
  }
  // ADD [DI+8],0F0h / ADC [DI+3],0: out along z, turned round, its roll reversed.
  const std::uint32_t z = std::uint32_t{ship.Get(SlotWord::Z)} + LAUNCH_DISTANCE;
  ship.Set(SlotWord::Z, static_cast<std::uint16_t>(z));
  ship.Set(SlotByte::ZHigh, static_cast<std::uint8_t>(ship.Get(SlotByte::ZHigh) + (z >> 16)));
  ship.Set(SlotWord::Yaw, Offset(ship.Get(SlotWord::Yaw), LAUNCH_TURN));
  ship.Set(SlotWord::Roll, Negate(ship.Get(SlotWord::Roll)));
  launch.launched = ComputeVelocity(_state, ship);
  return launch;
}

// ADD byte,_value / JAE / MOV byte,0FFh on _field: the sum written, and FFh over it on a carry.
void AddSaturating(GameState& _state, DataField<std::uint8_t> _field, std::uint8_t _value)
{
  const unsigned sum = _state.Get(_field) + unsigned{_value};
  _state.Set(_field, static_cast<std::uint8_t>(sum));
  if (sum > 0xFF)
  {
    _state.Set(_field, 0xFF);
  }
}

// What CheckMissilesAtStation did with the station's ECM.
struct StationEcm
{
  bool removed;                         // the ECM ran, and RemoveAllMissiles with it
  std::optional<DashboardPixel> erased; // the last pixel of the last scanner blip that erased, if it erased one
};

// CheckMissilesAtStation (5611): a missile aimed at the station at _station, or at a police Viper in the safe zone, is a crime,
// added to legalStatus up to FFh (the sum written, then FFh over it on a carry, 5650 and 5656), and starts the station's ECM
// unless the player's anti-ECM is on; while npcEcmFrames runs, the ECM removes every missile, and an invasion stops it.
StationEcm CheckMissilesAtStation(GameState& _state, const ObjectSlot& _station)
{
  if (_state.Get(DS.npcEcmFrames) == 0)
  {
    // MOV CL,objectSlotCount / XOR CH,CH, then LOOP: a count of 0 runs 65,536 times.
    std::optional<std::uint8_t> crime;
    std::uint16_t slot = DS.shipSlots.offset;
    for (std::uint32_t count = LoopCount(_state.Get(DS.objectSlotCount)); count != 0 && !crime; --count)
    {
      const ObjectSlot missile(_state, slot);
      const std::uint8_t type = missile.Get(SlotByte::Type);
      const std::uint16_t target = missile.Get(SlotWord::Target);
      if ((type & ObjectSlot::ACTIVE) != 0 && ((type >> 1) & TYPE_MASK) == TYPE_MISSILE && target != 0)
      {
        if (target == _station.Offset())
        {
          crime = STATION_MISSILE_CRIME;
        }
        else if (IsPoliceViper(ObjectSlot(_state, target)) && InSafeZone(_state).inside)
        {
          crime = POLICE_MISSILE_CRIME;
        }
      }
      slot = Offset(slot, ObjectSlot::BYTES);
    }
    if (!crime)
    {
      return StationEcm{false, std::nullopt};
    }
    AddSaturating(_state, DS.legalStatus, *crime);
    if (_state.Get(DS.antiEcmActive) == 1)
    {
      return StationEcm{false, std::nullopt};
    }
    _state.Set(DS.npcEcmFrames, STATION_ECM_FRAMES);
  }
  if (_state.Get(DS.thargoidInvasionActive) == 1)
  {
    _state.Set(DS.npcEcmFrames, 0);
    return StationEcm{false, std::nullopt};
  }
  _state.Set(DS.ecmFired, 1);
  const std::optional<DashboardPixel> erased = RemoveAllMissiles(_state);
  _state.Set(DS.npcEcmFrames, static_cast<std::uint8_t>(_state.Get(DS.npcEcmFrames) - 1));
  return StationEcm{true, erased};
}

// UpdateWolfAi's state 3 (586D): turning away, a Thargoid launching Thargons, until beyond its range, whose box takes _rangeLow,
// what DL holds there, for its low byte; then a pass is counted off. After the last it rests, calmed (aggression 9) and no
// longer hostile, and a Thargon drifts (state 0Ah); so does one whose mother is gone or is no Thargoid with a blip. Returns what
// MoveObject did.
MovedObject WolfTurnAway(GameState& _state, ObjectSlot _slot, std::uint8_t _rangeLow, bool _backward)
{
  if (WithinRange(_slot, _rangeLow))
  {
    (void)TurnToVector(_state, _slot, GetObjectPosition(_slot));
    (void)ComputeVelocity(_state, _slot);
    TryLaunchThargon(_state, _slot, _backward);
    return MoveObject(_state, _slot);
  }
  const auto passes = static_cast<std::uint8_t>(_slot.Get(SlotByte::Passes) - 1);
  _slot.Set(SlotByte::Passes, passes);
  bool rest = passes == 0;
  if (!rest && IsThargonType(_slot))
  {
    // A Thargon goes back to its run only while its mother is a Thargoid with a blip.
    const std::uint16_t mother = _slot.Get(SlotWord::Owner);
    if (mother != 0)
    {
      const ObjectSlot owner(_state, mother);
      rest = ((owner.Get(SlotByte::Type) >> 1) & TYPE_MASK) != TYPE_THARGOID || (owner.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) == 0;
    }
  }
  if (rest)
  {
    _slot.Set(SlotByte::Aggression, RESTING_AGGRESSION);
    _slot.Set(SlotByte::Flags, static_cast<std::uint8_t>(_slot.Get(SlotByte::Flags) & ~FLAG_HOSTILE));
    _slot.Set(SlotByte::State, STATE_ATTACK);
    if (IsThargonType(_slot))
    {
      _slot.Set(SlotByte::State, THARGON_ADRIFT);
    }
  }
  else
  {
    _slot.Set(SlotByte::State, STATE_ATTACK_RUN);
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
[[nodiscard]] Angles JinkAway(GameState& _state, ObjectSlot _slot)
{
  if (_slot.Get(SlotByte::JinkFrames) == 0)
  {
    _slot.Set(SlotWord::JinkPitch, Jink(NextRandom(_state)));
    _slot.Set(SlotWord::JinkYaw, Jink(NextRandom(_state)));
    _slot.Set(SlotByte::JinkFrames, JINK_FRAMES);
  }
  const auto frames = static_cast<std::uint8_t>(_slot.Get(SlotByte::JinkFrames) - 1);
  _slot.Set(SlotByte::JinkFrames, frames);
  if (frames == 0)
  {
    _slot.Set(SlotByte::JinkFrames, JINK_FRAMES);
    _slot.Set(SlotWord::JinkPitch, Negate(_slot.Get(SlotWord::JinkPitch)));
    _slot.Set(SlotWord::JinkYaw, Negate(_slot.Get(SlotWord::JinkYaw)));
  }
  // ConvertVectorToAngles of GetVectorToPlayer, then ADD AX,400h / ADD AX,[DI+36h] / ADD BX,[DI+38h].
  const Angles toPlayer = ConvertVectorToAngles(_state, GetVectorToPlayer(_slot));
  return Angles{Offset(Offset(toPlayer.first, HALF_TURN), _slot.Get(SlotWord::JinkPitch)),
                Offset(toPlayer.second, _slot.Get(SlotWord::JinkYaw))};
}

// UpdateHunterAi's state 3 (5987): evading with random jinks, and a missile at odds of 5DCh in 65536, until beyond its range,
// whose box takes _rangeLow, what DL holds there, for its low byte. Returns what MoveObject did.
MovedObject HunterEvade(GameState& _state, ObjectSlot _slot, std::uint8_t _rangeLow, bool _backward)
{
  if (!WithinRange(_slot, _rangeLow))
  {
    _slot.Set(SlotByte::State, STATE_IDLE);
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
MovedObject HunterIdle(GameState& _state, ObjectSlot _slot)
{
  const std::uint8_t flags = _slot.Get(SlotByte::Flags);
  if ((flags & FLAG_BLIP_DRAWN) == 0)
  {
    (void)TurnToVector(_state, _slot, GetVectorToPlayer(_slot));
    (void)ComputeVelocity(_state, _slot);
    return MoveObject(_state, _slot);
  }
  if ((flags & FLAG_HOSTILE) != 0)
  {
    _slot.Set(SlotByte::State, STATE_TURN_AWAY);
  }
  else
  {
    const HunterCount hunters = CountOtherHuntersOnScanner(_state, _slot.Offset());
    if (hunters.count >= 2)
    {
      if (_state.Get(DS.legalStatus) >= FUGITIVE || NextRandom(_state) < PACK_ATTACK_ODDS)
      {
        _slot.Set(SlotByte::State, STATE_ATTACK);
      }
    }
    else if (hunters.count == 1 && hunters.last)
    {
      // MOV [DI+29h],BP: the mate CountOtherHuntersOnScanner found last, which a count of 1 always has.
      _slot.Set(SlotWord::Target, *hunters.last);
      _slot.Set(SlotByte::State, STATE_FORMATION);
    }
  }
  return MoveObject(_state, _slot);
}

// UpdateTraderOrPoliceAi's state 1 (56CE): a police Viper attacks a player of legal status 5 or more; otherwise one the player
// has hit flees, at odds of 53FCh in 65536, or attacks. Returns what MoveObject did.
MovedObject TraderDecide(GameState& _state, ObjectSlot _slot)
{
  if (IsPoliceViper(_slot) && _state.Get(DS.legalStatus) >= POLICE_ATTACK_STATUS)
  {
    _slot.Set(SlotByte::State, TRADER_ATTACKING);
  }
  else if ((_slot.Get(SlotByte::Flags) & FLAG_HOSTILE) != 0)
  {
    _slot.Set(SlotByte::State, NextRandom(_state) < TRADER_FLEE_ODDS ? TRADER_FLEEING : TRADER_ATTACKING);
  }
  return MoveObject(_state, _slot);
}

// UpdateTraderOrPoliceAi's state 2 (56FC): at the player, firing, the police with missiles against an offender, until within
// 320h on every axis, or, for any but the police, until its energy is below 8. Returns what MoveObject did.
MovedObject TraderAttack(GameState& _state, ObjectSlot _slot, bool _backward)
{
  if (!IsPoliceViper(_slot) && _slot.Get(SlotByte::Energy) < TRADER_FLEE_ENERGY)
  {
    _slot.Set(SlotByte::State, TRADER_FLEEING);
    return MoveObject(_state, _slot);
  }
  if (ObjectWithinBox(_slot, TRADER_BREAK_OFF_BOX))
  {
    _slot.Set(SlotByte::State, TRADER_BREAKING_OFF);
    return MoveObject(_state, _slot);
  }
  const Turn turn = TurnToVector(_state, _slot, GetVectorToPlayer(_slot));
  TryFireLaserAtPlayer(_state, _slot, turn.pitch.errorMagnitude, turn.yaw.errorMagnitude);
  const std::uint8_t status = _state.Get(DS.legalStatus);
  if (IsPoliceViper(_slot) && status != 0)
  {
    TryLaunchMissileAtPlayer(_state, _slot, status >= FUGITIVE_FOR_POLICE ? FUGITIVE_MISSILE_ODDS : POLICE_MISSILE_ODDS, _backward);
  }
  (void)ComputeVelocity(_state, _slot);
  return MoveObject(_state, _slot);
}

// UpdateTraderOrPoliceAi's state 3 (5750): fleeing from the player with random jinks, a missile when nearly dead, and now and
// then an ECM. Returns what MoveObject did.
MovedObject TraderFlee(GameState& _state, ObjectSlot _slot, bool _backward)
{
  if (_slot.Get(SlotByte::Energy) < DESPERATE_ENERGY)
  {
    TryLaunchMissileAtPlayer(_state, _slot, DESPERATE_MISSILE_ODDS, _backward);
  }
  (void)TurnTowardAngles(_slot, JinkAway(_state, _slot));
  (void)ComputeVelocity(_state, _slot);
  if (NextRandom(_state) < TRADER_ECM_ODDS)
  {
    _state.Set(DS.npcEcmFrames, TRADER_ECM_FRAMES);
  }
  return MoveObject(_state, _slot);
}

// UpdateTraderOrPoliceAi's state 4 and above (57BC): flying on away until beyond its range, then attacking again; weak, any but
// the police flee. Its range's box takes _rangeLow, what DL holds there, for its low byte. Returns what MoveObject did.
MovedObject TraderBreakOff(GameState& _state, ObjectSlot _slot, std::uint8_t _rangeLow)
{
  if (!WithinRange(_slot, _rangeLow))
  {
    _slot.Set(SlotByte::State, TRADER_ATTACKING);
    return MoveObject(_state, _slot);
  }
  if (_slot.Get(SlotByte::Energy) < RETURN_ENERGY && !IsPoliceViper(_slot))
  {
    _slot.Set(SlotByte::State, TRADER_FLEEING);
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
std::uint16_t RunBehaviorHandler(GameState& _state, Hardware& _hardware, ObjectSlot _slot, std::uint8_t _behavior, bool _backward,
                                 std::uint16_t _dx)
{
  switch (_behavior)
  {
  case INERT_CLASS:
    return _dx;
  case STATION_CLASS:
    return UpdateStationAi(_state, _slot, _backward, _dx).dx;
  case MISSILE_CLASS:
    return UpdateMissileAi(_state, _hardware, _slot, _backward, static_cast<std::uint16_t>(_behavior << 1), _dx).dx;
  case DRIFTING_CLASS:
    return UpdateDriftingObjectAi(_state, _slot).moved.dx;
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
  std::uint16_t count = DS.activeObjectCount.offset;
  for (std::uint16_t bytes = CLASS_COUNTS_CLEARED; bytes != 0; --bytes)
  {
    _state.SetByte(count, 0);
    count = _backward ? static_cast<std::uint16_t>(count - 1) : At(count, 1);
  }
  // Every slot of shipSlotCount, by LOOP: an active one counted, but for debris, and in its class's count, and its class's handler
  // run, CX and DI pushed round it. DX goes on from one handler to the next.
  std::uint16_t dx = _dx;
  std::uint16_t slot = DS.shipSlots.offset;
  for (std::uint32_t slots = LoopCount(_state.Get(DS.shipSlotCount)); slots != 0; --slots)
  {
    const ObjectSlot object(_state, slot);
    if ((object.Get(SlotByte::Type) & SLOT_ACTIVE) != 0)
    {
      const std::uint8_t behavior = object.Get(SlotByte::Class);
      if (behavior != DEBRIS_CLASS)
      {
        _state.Set(DS.activeObjectCount, static_cast<std::uint8_t>(_state.Get(DS.activeObjectCount) + 1));
      }
      const std::uint16_t classCount = At(DS.behaviorClassCounts.offset, behavior);
      _state.SetByte(classCount, static_cast<std::uint8_t>(_state.Byte(classCount) + 1));
      dx = RunBehaviorHandler(_state, _hardware, object, behavior, _backward, dx);
    }
    slot = At(slot, SLOT_BYTES);
  }

  _state.Set(DS.maskingBackgroundColor, 0);
  if (_state.Get(DS.activeObjectCount) >= MOST_ACTIVE_FOR_SPAWNING || _state.Get(DS.objectSlotCount) < _state.Get(DS.activeObjectCount))
  {
    return;
  }
  if (_state.Get(DS.maskMissionShipsLeft) != 0)
  {
    if (_state.Get(DS.maskSystemJumps) == 1)
    {
      SpawnMaskMissionShips(_state);
      return;
    }
  }
  else if ((_state.Get(DS.thargoidInvasionActive) & _state.Get(DS.jumpedSinceBriefing)) != 0 && _state.Get(DS.invadedStationDestroyed) != 1)
  {
    SpawnInvasionWave(_state);
    return;
  }
  SpawnByGovernment(_state);
}

std::uint16_t ScaleSpawnOdds(const GameState& _state, std::uint16_t _odds)
{
  if (_state.Get(DS.jumpDriveEngaged) != 1)
  {
    return _odds;
  }
  return static_cast<std::uint16_t>(_odds << JUMP_DRIVE_ODDS_SHIFT);
}

SlotSearch IsMaskShipPresent(GameState& _state)
{
  // Every object slot, active or not, for the device's bit: LOOP from CX = objectSlotCount.
  std::uint16_t slot = DS.shipSlots.offset;
  for (std::uint32_t count = LoopCount(_state.Get(DS.objectSlotCount)); count != 0; --count)
  {
    if ((ObjectSlot(_state, slot).Get(SlotByte::Flags) & FLAG_DEVICE) != 0)
    {
      return SlotSearch{true, slot};
    }
    slot = Offset(slot, ObjectSlot::BYTES);
  }
  return SlotSearch{false, slot};
}

void PlaceEscortNear(GameState& _state, ObjectSlot _escort, std::uint16_t _leader)
{
  // The leader's first 16 bytes, byte by byte: its type too.
  for (std::uint16_t index = 0; index < ESCORT_BYTES_COPIED; ++index)
  {
    _state.SetByte(Offset(_escort.Offset(), index), _state.Byte(Offset(_leader, index)));
  }
  for (int axis = 0; axis < 3; ++axis)
  {
    const auto scatter = static_cast<std::uint16_t>((NextRandom(_state) & ESCORT_SCATTER_MASK) - ESCORT_SCATTER_CENTER);
    AddToCoordinate(_escort, axis, static_cast<std::int16_t>(scatter));
  }
}

Turn TurnTowardAngles(ObjectSlot _slot, Angles _wanted)
{
  const TurnStep pitch = ClampTurnStep(_slot, _wanted.first, _slot.Get(SlotWord::Pitch));
  _slot.Set(SlotWord::Pitch, Offset(_slot.Get(SlotWord::Pitch), static_cast<std::uint16_t>(pitch.step)));
  const TurnStep yaw = ClampTurnStep(_slot, _wanted.second, _slot.Get(SlotWord::Yaw));
  _slot.Set(SlotWord::Yaw, Offset(_slot.Get(SlotWord::Yaw), static_cast<std::uint16_t>(yaw.step)));
  return Turn{pitch, yaw};
}

TurnStep ClampTurnStep(const ObjectSlot& _slot, std::uint16_t _wanted, std::uint16_t _current)
{
  // The error is not wrapped at 2048, so a turn across 0 goes the long way.
  const auto error = static_cast<std::uint16_t>((_wanted & ANGLE_MASK) - (_current & ANGLE_MASK));
  const std::uint16_t magnitude = (error & 0x8000) != 0 ? Negate(error) : error;
  const std::uint16_t rate = _slot.Get(SlotByte::TurnRate);
  if (magnitude < rate)
  {
    return TurnStep{static_cast<std::int16_t>(error), magnitude};
  }
  return TurnStep{static_cast<std::int16_t>((error & 0x8000) != 0 ? Negate(rate) : rate), magnitude};
}

HunterCount CountOtherHuntersOnScanner(GameState& _state, std::uint16_t _slot)
{
  HunterCount hunters{0, std::nullopt};
  std::uint16_t slot = DS.shipSlots.offset;
  for (std::uint32_t count = LoopCount(_state.Get(DS.objectSlotCount)); count != 0; --count)
  {
    const ObjectSlot other(_state, slot);
    if (other.Get(SlotByte::Class) == HUNTER_CLASS && (other.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) != 0 && slot != _slot)
    {
      hunters.last = slot;
      hunters.count = static_cast<std::uint8_t>(hunters.count + 1);
    }
    slot = Offset(slot, ObjectSlot::BYTES);
  }
  return hunters;
}

VectorToObject GetVectorToObject(const ObjectSlot& _slot, const ObjectSlot& _object, std::uint16_t _halfSize)
{
  // Each coordinate quartered (SAR twice) before the difference.
  const auto difference = [&_slot, &_object](SlotWord _axis)
  { return static_cast<std::int16_t>(Sar(_object.Get(_axis), 2) - Sar(_slot.Get(_axis), 2)); };
  const Vector vector{difference(SlotWord::X), difference(SlotWord::Y), difference(SlotWord::Z)};
  return VectorToObject{vector, VectorWithinBox(vector, static_cast<std::uint16_t>(_halfSize >> 2))};
}

void SkipInertObjectAi(Guest& /*_guest*/) {}

StationTurn UpdateStationAi(GameState& _state, ObjectSlot _station, bool _backward, std::uint16_t _dx)
{
  _station.Set(SlotWord::Roll, Offset(_station.Get(SlotWord::Roll), STATION_SPIN));
  StationTurn turn{std::nullopt, false, std::nullopt, false, false, _dx};
  if (_state.Get(DS.spawnGovernment) >= 1 && (_station.Get(SlotByte::Flags) & FLAG_HOSTILE) != 0)
  {
    // DX: the last pixel of the blip IsObjectNear erased, or MOV DX,1C2h, the guarded box, once it is near; then what
    // ComputeVelocity leaves once it launched.
    const OffenderLaunch launch = LaunchAtOffender(_state, _station, _backward);
    turn.near = launch.near;
    if (launch.near.erasedBlip)
    {
      turn.dx = PixelPlace(*launch.near.erasedBlip);
    }
    if (launch.near.nearby)
    {
      turn.dx = STATION_GUARD_BOX;
    }
    if (launch.launched)
    {
      turn.launched = true;
      turn.dx = launch.launched->dx;
    }
    if (launch.trader)
    {
      turn.traderPitch = launch.trader->heading.first;
    }
  }
  // DX: the last pixel of the last blip RemoveAllMissiles erased, once the ECM ran.
  const StationEcm ecm = CheckMissilesAtStation(_state, _station);
  turn.missilesRemoved = ecm.removed;
  if (ecm.erased)
  {
    turn.blipErased = true;
    turn.dx = PixelPlace(*ecm.erased);
  }
  return turn;
}

DriftingObject UpdateDriftingObjectAi(GameState& _state, ObjectSlot _slot)
{
  const MovedObject moved = MoveObject(_state, _slot);
  if (!IsDebrisType(_slot))
  {
    return DriftingObject{moved, std::nullopt};
  }
  // MOV AX,37h / MOV BX,0FFDFh, exchanged unless bit 1 of the type byte is set: AX to the pitch, then BX to the roll.
  Pair tumble{static_cast<std::int16_t>(ROCK_TUMBLE_A), static_cast<std::int16_t>(ROCK_TUMBLE_B)};
  if ((_slot.Get(SlotByte::Type) & ROCK_TUMBLE_SWAP) == 0)
  {
    std::swap(tumble.first, tumble.second);
  }
  _slot.Set(SlotWord::Pitch, Offset(_slot.Get(SlotWord::Pitch), static_cast<std::uint16_t>(tumble.first)));
  _slot.Set(SlotWord::Roll, Offset(_slot.Get(SlotWord::Roll), static_cast<std::uint16_t>(tumble.second)));
  return DriftingObject{moved, tumble};
}

MovedObject UpdateTraderOrPoliceAi(GameState& _state, ObjectSlot _slot, std::uint8_t _rangeLow, bool _backward)
{
  if (IsDebrisType(_slot))
  {
    // A rock only spins. The split at 56B1 (LaunchShipFromObject with DL=5) never runs: 56A5 jumps past it when bit 0 of +1Eh is
    // clear, and 56AB when it is set.
    _slot.Set(SlotWord::Roll, Offset(_slot.Get(SlotWord::Roll), ROCK_SPIN));
    return MoveObject(_state, _slot);
  }
  switch (_slot.Get(SlotByte::State))
  {
  case STATE_IDLE:
    _slot.Set(SlotByte::State, TRADER_DECIDING);
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

MovedObject UpdateWolfAi(GameState& _state, ObjectSlot _slot, std::uint8_t _rangeLow, bool _backward)
{
  // A Thargoid sets off its ECM at odds of 100 in 65536; a Thargoid or a Thargon spins.
  bool spins = IsThargoidType(_slot);
  if (spins)
  {
    if (NextRandom(_state) < THARGOID_ECM_ODDS)
    {
      _state.Set(DS.npcEcmFrames, THARGOID_ECM_FRAMES);
    }
  }
  else
  {
    spins = IsThargonType(_slot);
  }
  if (spins)
  {
    _slot.Set(SlotWord::Roll, Offset(_slot.Get(SlotWord::Roll), WOLF_SPIN));
  }
  switch (_slot.Get(SlotByte::State))
  {
  case STATE_IDLE:
    _slot.Set(SlotByte::State, STATE_ATTACK);
    return MoveObject(_state, _slot);
  case STATE_ATTACK:
    // Resting: a new set of 2-5 passes once it is not hostile, or calm no more: AND AH,3 / ADD AH,2.
    if ((_slot.Get(SlotByte::Flags) & FLAG_HOSTILE) == 0 || _slot.Get(SlotByte::Aggression) >= CALM)
    {
      _slot.Set(SlotByte::Passes, static_cast<std::uint8_t>((High(NextRandom(_state)) & PASS_COUNT_MASK) + FEWEST_PASSES));
      _slot.Set(SlotByte::State, STATE_ATTACK_RUN);
    }
    return MoveObject(_state, _slot);
  case STATE_ATTACK_RUN:
  {
    if (ObjectWithinBox(_slot, BREAK_OFF_BOX))
    {
      _slot.Set(SlotByte::State, STATE_TURN_AWAY);
      return MoveObject(_state, _slot);
    }
    const Turn turn = TurnToVector(_state, _slot, GetVectorToPlayer(_slot));
    TryFireLaserAtPlayer(_state, _slot, turn.pitch.errorMagnitude, turn.yaw.errorMagnitude);
    TryLaunchMissileAtPlayer(_state, _slot, WOLF_MISSILE_ODDS, _backward);
    TryLaunchThargon(_state, _slot, _backward);
    (void)ComputeVelocity(_state, _slot);
    const MovedObject moved = MoveObject(_state, _slot);
    _slot.Set(SlotWord::Roll, Offset(_slot.Get(SlotWord::Roll), ATTACK_RUN_SPIN));
    return moved;
  }
  case STATE_TURN_AWAY:
    return WolfTurnAway(_state, _slot, _rangeLow, _backward);
  default:
    // A Thargon adrift slows down to 10.
    if (_slot.Get(SlotByte::Speed) >= SLOWEST_ADRIFT)
    {
      _slot.Set(SlotByte::Speed, static_cast<std::uint8_t>(_slot.Get(SlotByte::Speed) - ADRIFT_SLOWING));
      (void)ComputeVelocity(_state, _slot);
    }
    return MoveObject(_state, _slot);
  }
}

MovedObject UpdateHunterAi(GameState& _state, ObjectSlot _slot, std::uint8_t _rangeLow, bool _backward)
{
  switch (_slot.Get(SlotByte::State))
  {
  case STATE_IDLE:
    return HunterIdle(_state, _slot);
  case STATE_ATTACK:
  {
    if (ObjectWithinBox(_slot, BREAK_OFF_BOX))
    {
      _slot.Set(SlotByte::State, STATE_TURN_AWAY);
      return MoveObject(_state, _slot);
    }
    const Turn turn = TurnToVector(_state, _slot, GetVectorToPlayer(_slot));
    TryFireLaserAtPlayer(_state, _slot, turn.pitch.errorMagnitude, turn.yaw.errorMagnitude);
    TryLaunchMissileAtPlayer(_state, _slot, HUNTER_MISSILE_ODDS, _backward);
    (void)ComputeVelocity(_state, _slot);
    _slot.Set(SlotWord::Roll, Offset(_slot.Get(SlotWord::Roll), HUNTER_SPIN));
    return MoveObject(_state, _slot);
  }
  case STATE_FORMATION:
  {
    // In formation with its pack mate, until within 7D0h of it on every axis (GetVectorToObject).
    const VectorToObject toMate = GetVectorToObject(_slot, ObjectSlot(_state, _slot.Get(SlotWord::Target)), FORMATION_BOX);
    if (toMate.within)
    {
      _slot.Set(SlotByte::State, STATE_CLOSING);
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
      _slot.Set(SlotByte::State, STATE_IDLE);
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

HoldFire CheckSafeZoneHoldFire(const GameState& _state, const ObjectSlot& _slot)
{
  if (_state.Get(DS.thargoidInvasionActive) == 1 || IsPoliceViper(_slot))
  {
    return HoldFire{false, std::nullopt};
  }
  const SafeZone zone = InSafeZone(_state);
  return HoldFire{zone.inside, zone};
}

namespace
{

using Machine::REGISTER_ALL;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

// "Clobbers most registers": all but DI, the slot, and DS; and DX, as MoveObject leaves it, whose DL the next slot's handler can
// take for its range's box (WithinRange; MovingHandlerEntry).
constexpr Machine::NativeContract CLOBBERS_MOST{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_BP | REGISTER_ES, 0};

constexpr Machine::NativeContract CLOBBERS_CX_SI{REGISTER_CX | REGISTER_SI, 0};
constexpr Machine::NativeContract MASK_SHIP_SEARCH{REGISTER_CX, FLAG_CARRY};
constexpr Machine::NativeContract ESCORT_PLACED{REGISTER_AX | REGISTER_CX | REGISTER_DX | REGISTER_SI, 0};
// TurnTowardAngles: the CX and BP it leaves are compared (TurnTowardAnglesEntry).
constexpr Machine::NativeContract TURNED{REGISTER_DX, 0};
constexpr Machine::NativeContract OBJECT_VECTOR{REGISTER_DX | REGISTER_BP, FLAG_CARRY};
// CheckSafeZoneHoldFire: every register compared, AL as InSafeZone leaves it (CheckSafeZoneHoldFireEntry).
constexpr Machine::NativeContract HOLD_FIRE{0, FLAG_CARRY};
// UpdateStationAi's: DI, BP and ES as the original leaves them, and DX, of which the next slot's handler can take DL
// (UpdateStationAiEntry).
constexpr Machine::NativeContract STATION_TURN{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI, 0};
constexpr Machine::NativeContract CLOBBERS_ALL{REGISTER_ALL, 0};

} // namespace

// ── The entries of the routines de-assembled (ADR-012) ──

void ScaleSpawnOddsEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = ScaleSpawnOdds(_guest.State(), regs.bx);
  _guest.Clobber(PRESERVES_ALL);
}

void IsMaskShipPresentEntry(Guest& _guest)
{
  const SlotSearch search = IsMaskShipPresent(_guest.State());
  _guest.Regs().si = search.slot;
  _guest.SetFlag(FLAG_CARRY, search.found);
  _guest.Clobber(MASK_SHIP_SEARCH);
}

void PlaceEscortNearEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  PlaceEscortNear(_guest.State(), ObjectSlot(_guest.State(), regs.di), regs.si);
  _guest.Clobber(ESCORT_PLACED);
}

void TurnTowardAnglesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  // The original leaves the yaw's ClampTurnStep in CX, the turn rate, negated when it is the step, and the pitch's error in BP;
  // UpdateMissileAi's contract compares both after it.
  TurnOut(regs, slot, TurnTowardAngles(slot, Angles{regs.ax, regs.bx}));
  _guest.Clobber(TURNED);
}

void ClampTurnStepEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  const TurnStep step = ClampTurnStep(slot, regs.ax, regs.cx);
  regs.ax = static_cast<std::uint16_t>(step.step);
  regs.bx = step.errorMagnitude;
  // The original leaves the turn rate in CX, negated when it is the step, and UpdateMissileAi's contract compares CX after
  // TurnTowardAngles.
  const std::uint16_t rate = slot.Get(SlotByte::TurnRate);
  regs.cx = step.errorMagnitude < rate ? rate : regs.ax;
  _guest.Clobber(PRESERVES_ALL);
}

void CountOtherHuntersOnScannerEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const HunterCount hunters = CountOtherHuntersOnScanner(_guest.State(), regs.di);
  SetLow(regs.ax, hunters.count);
  if (hunters.last.has_value())
  {
    regs.bp = *hunters.last;
  }
  _guest.Clobber(CLOBBERS_CX_SI);
}

void GetVectorToObjectEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const VectorToObject toObject = GetVectorToObject(ObjectSlot(_guest.State(), regs.di), ObjectSlot(_guest.State(), regs.si), regs.dx);
  regs.ax = static_cast<std::uint16_t>(toObject.vector.x);
  regs.bx = static_cast<std::uint16_t>(toObject.vector.y);
  regs.cx = static_cast<std::uint16_t>(toObject.vector.z);
  _guest.SetFlag(FLAG_CARRY, toObject.within);
  _guest.Clobber(OBJECT_VECTOR);
}

namespace
{

// The entry of a behaviour handler whose every path ends with MoveObject: what that leaves (MoveObjectOut), and the contract's
// marks on the rest.
template <MovedObject (*Handler)(GameState&, ObjectSlot, std::uint8_t, bool)> void MovingHandlerEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  MoveObjectOut(_guest, slot, Handler(_guest.State(), slot, Low(regs.dx), _guest.Flag(FLAG_DIRECTION)));
  _guest.Clobber(CLOBBERS_MOST);
}

} // namespace

void UpdateTraderOrPoliceAiEntry(Guest& _guest)
{
  MovingHandlerEntry<&UpdateTraderOrPoliceAi>(_guest);
}

void UpdateWolfAiEntry(Guest& _guest)
{
  MovingHandlerEntry<&UpdateWolfAi>(_guest);
}

void UpdateHunterAiEntry(Guest& _guest)
{
  MovingHandlerEntry<&UpdateHunterAi>(_guest);
}

void UpdateDriftingObjectAiEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  const DriftingObject drift = UpdateDriftingObjectAi(_guest.State(), slot);
  // The contract compares every register: what MoveObject leaves, then IsDebrisType's AL, the type, and a rock's tumble in AX and
  // BX.
  MoveObjectOut(_guest, slot, drift.moved);
  SetLow(regs.ax, static_cast<std::uint8_t>((slot.Get(SlotByte::Type) >> 1) & TYPE_MASK));
  if (drift.tumble)
  {
    regs.ax = static_cast<std::uint16_t>(drift.tumble->first);
    regs.bx = static_cast<std::uint16_t>(drift.tumble->second);
  }
  _guest.Clobber(PRESERVES_ALL);
}

void UpdateObjectsAndSpawnEntry(Guest& _guest)
{
  // The DX the caller left goes on to the first handler that measures its range's box, as the original's does.
  UpdateObjectsAndSpawn(_guest.State(), _guest.Devices(), _guest.Flag(FLAG_DIRECTION), _guest.Regs().dx);
  _guest.Clobber(CLOBBERS_ALL);
}

void UpdateStationAiEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot station(_guest.State(), regs.di);
  const StationTurn turn = UpdateStationAi(_guest.State(), station, _guest.Flag(FLAG_DIRECTION), regs.dx);
  // What the contract compares: what IsObjectNear leaves; CopyObject's ES = DS once it launched; the pitch FacePlayerWithRandomRoll
  // leaves in BP once that was a trader; DI past the slots and ES as RemoveAllMissiles leaves them once the ECM ran; and DX.
  if (turn.near)
  {
    IsObjectNearOut(_guest, station, *turn.near);
  }
  if (turn.launched)
  {
    regs.es = regs.ds;
  }
  if (turn.traderPitch)
  {
    regs.bp = *turn.traderPitch;
  }
  if (turn.missilesRemoved)
  {
    RemoveAllMissilesOut(_guest, turn.blipErased);
  }
  regs.dx = turn.dx;
  _guest.Clobber(STATION_TURN);
}

void CheckSafeZoneHoldFireEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const HoldFire hold = CheckSafeZoneHoldFire(_guest.State(), ObjectSlot(_guest.State(), regs.di));
  // The contract compares AX: once it asks InSafeZone, the original leaves AL what that leaves, the rest of safeZoneFlags.
  if (hold.zone.has_value())
  {
    SetLow(regs.ax, hold.zone->rest);
  }
  _guest.SetFlag(FLAG_CARRY, hold.hold);
  _guest.Clobber(HOLD_FIRE);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x4A10, "UpdateObjectsAndSpawn", &UpdateObjectsAndSpawnEntry, CLOBBERS_ALL},
  NativeEntry{0x4C0E, "ScaleSpawnOdds", &ScaleSpawnOddsEntry, PRESERVES_ALL},
  NativeEntry{0x4C20, "IsMaskShipPresent", &IsMaskShipPresentEntry, MASK_SHIP_SEARCH},
  NativeEntry{0x4C38, "PlaceEscortNear", &PlaceEscortNearEntry, ESCORT_PLACED},
  NativeEntry{0x514B, "TurnTowardAngles", &TurnTowardAnglesEntry, TURNED},
  NativeEntry{0x5166, "ClampTurnStep", &ClampTurnStepEntry, PRESERVES_ALL},
  NativeEntry{0x548F, "CountOtherHuntersOnScanner", &CountOtherHuntersOnScannerEntry, CLOBBERS_CX_SI},
  NativeEntry{0x54B4, "GetVectorToObject", &GetVectorToObjectEntry, OBJECT_VECTOR},
  NativeEntry{0x5594, "SkipInertObjectAi", &SkipInertObjectAi, PRESERVES_ALL},
  NativeEntry{0x5595, "UpdateStationAi", &UpdateStationAiEntry, STATION_TURN},
  NativeEntry{0x5681, "UpdateDriftingObjectAi", &UpdateDriftingObjectAiEntry, PRESERVES_ALL},
  NativeEntry{0x569C, "UpdateTraderOrPoliceAi", &UpdateTraderOrPoliceAiEntry, CLOBBERS_MOST},
  NativeEntry{0x57E8, "UpdateWolfAi", &UpdateWolfAiEntry, CLOBBERS_MOST},
  NativeEntry{0x58DE, "UpdateHunterAi", &UpdateHunterAiEntry, CLOBBERS_MOST},
  NativeEntry{0x5A10, "CheckSafeZoneHoldFire", &CheckSafeZoneHoldFireEntry, HOLD_FIRE},
};

} // namespace

std::span<const NativeEntry> AiEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
