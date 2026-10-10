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
using Machine::FLAG_ZERO;

// Routines outside these files, run through the original.
constexpr std::uint16_t CONVERT_VECTOR_TO_ANGLES = 0x4F08;

constexpr std::uint8_t FLAG_HOSTILE = 0x01;
constexpr std::uint8_t FLAG_BLIP_DRAWN = 0x02;
constexpr std::uint8_t FLAG_DEVICE = 0x20;

constexpr std::uint16_t CLASS_COUNTS_CLEARED = 9; // activeObjectCount and the eight behaviorClassCounts
constexpr std::uint8_t DEBRIS_CLASS = 7;          // not counted as active
constexpr std::uint8_t HUNTER_CLASS = 6;
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

void AddWord(Guest& _guest, std::uint16_t _offset, std::uint16_t _value) noexcept
{
  _guest.SetWord(_offset, static_cast<std::uint16_t>(_guest.Word(_offset) + _value));
}

void SetState(Guest& _guest, std::uint8_t _state) noexcept
{
  _guest.SetByte(At(_guest.Regs().di, SLOT_STATE), _state);
}

[[nodiscard]] std::uint8_t State(Guest& _guest) noexcept
{
  return _guest.Byte(At(_guest.Regs().di, SLOT_STATE));
}

// WithinBox, MOV DX,_halfSize / CALL ObjectWithinBox: whether the slot at DI is within the box on every axis. Its value is
// ObjectWithinBox's, so only its register code is left, which keeps the DX it loads. Nothing reads the magnitudes
// ObjectWithinBox leaves in AX, BX and CX, nor its flags, here or after WithinRange: poisoned, every comparison and digest
// still agrees.
[[nodiscard]] bool WithinBoxOnRegisters(Guest& _guest, std::uint16_t _halfSize)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = _halfSize;
  return ObjectWithinBox(ObjectSlot(_guest.State(), regs.di), _halfSize);
}

// WithinRange: whether _slot is within the box whose half size has its range byte for the high byte and _low, what DL holds
// there, for the low.
[[nodiscard]] bool WithinRange(const ObjectSlot& _slot, std::uint8_t _low)
{
  return ObjectWithinBox(_slot, Join(_slot.Get(SlotByte::Range), _low));
}

// MOV DH,[DI+1Ch] / CALL ObjectWithinBox: WithinRange for the slot at DI, DL as it is. The register code keeps the DH it loads.
[[nodiscard]] bool WithinRangeOnRegisters(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  SetHigh(regs.dx, slot.Get(SlotByte::Range));
  return WithinRange(slot, Low(regs.dx));
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

// TurnToVector on AX, BX, CX and the slot at DI, with the registers TurnTowardAngles leaves (TurnOut). Nothing reads the DX
// it leaves, which its two entries' contracts gave to them, nor its flags.
void TurnToVectorOnRegisters(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  const Vector vector{static_cast<std::int16_t>(regs.ax), static_cast<std::int16_t>(regs.bx), static_cast<std::int16_t>(regs.cx)};
  TurnOut(regs, slot, TurnToVector(_guest.State(), slot, vector));
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
  bool launched;                          // a copy of the station went into a free slot
  std::optional<RandomRollFacing> trader; // when the copy was made a trader, what FacePlayerWithRandomRoll gave it
};

// From 55A9: the station at _station, near, launches a police Viper, a shuttle or a trader at an offender out of the box it
// guards: a copy of itself (CopyObject, backwards when _backward) in a free slot, out along z, turned round, its roll reversed,
// and its velocity set. PUSH DI and POP DI keep the station.
OffenderLaunch LaunchAtOffender(GameState& _state, ObjectSlot _station, bool _backward)
{
  OffenderLaunch launch{IsObjectNear(_state, _station), false, std::nullopt};
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
  launch.launched = true;
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
  (void)ComputeVelocity(_state, ship);
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
  bool removed; // the ECM ran, and RemoveAllMissiles with it
  bool erased;  // that erased a scanner blip
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
      return StationEcm{false, false};
    }
    AddSaturating(_state, DS.legalStatus, *crime);
    if (_state.Get(DS.antiEcmActive) == 1)
    {
      return StationEcm{false, false};
    }
    _state.Set(DS.npcEcmFrames, STATION_ECM_FRAMES);
  }
  if (_state.Get(DS.thargoidInvasionActive) == 1)
  {
    _state.Set(DS.npcEcmFrames, 0);
    return StationEcm{false, false};
  }
  _state.Set(DS.ecmFired, 1);
  const bool erased = RemoveAllMissiles(_state);
  _state.Set(DS.npcEcmFrames, static_cast<std::uint8_t>(_state.Get(DS.npcEcmFrames) - 1));
  return StationEcm{true, erased};
}

// UpdateWolfAi's state 3 (586D): turning away until beyond its range, then a pass is counted off.
void WolfTurnAway(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (WithinRangeOnRegisters(_guest))
  {
    GetObjectPositionEntry(_guest);
    TurnToVectorOnRegisters(_guest);
    ComputeVelocityEntry(_guest);
    TryLaunchThargonEntry(_guest);
    MoveObjectEntry(_guest);
    return;
  }
  const std::uint16_t passes = At(regs.di, SLOT_PASSES);
  _guest.SetByte(passes, static_cast<std::uint8_t>(_guest.Byte(passes) - 1));
  bool rest = _guest.Byte(passes) == 0;
  if (!rest)
  {
    // A Thargon goes back to its run only while its mother is a Thargoid with a blip.
    IsThargonTypeEntry(_guest);
    if (_guest.Flag(FLAG_ZERO))
    {
      regs.si = _guest.Word(At(regs.di, SLOT_OWNER));
      if (regs.si != 0)
      {
        SetLow(regs.ax, static_cast<std::uint8_t>((_guest.Byte(regs.si) >> 1) & TYPE_MASK));
        rest = Low(regs.ax) != TYPE_THARGOID || (_guest.Byte(At(regs.si, SLOT_FLAGS)) & FLAG_BLIP_DRAWN) == 0;
      }
    }
  }
  if (rest)
  {
    _guest.SetByte(At(regs.di, SLOT_AGGRESSION), RESTING_AGGRESSION);
    _guest.SetByte(At(regs.di, SLOT_FLAGS), static_cast<std::uint8_t>(_guest.Byte(At(regs.di, SLOT_FLAGS)) & ~FLAG_HOSTILE));
    SetState(_guest, STATE_ATTACK);
    IsThargonTypeEntry(_guest);
    if (_guest.Flag(FLAG_ZERO))
    {
      SetState(_guest, THARGON_ADRIFT);
    }
  }
  else
  {
    SetState(_guest, STATE_ATTACK_RUN);
  }
  MoveObjectEntry(_guest);
}

// MOV AH,1 / ROR AL,1 / NEG AX when the bit rotated out was set: a jink of +-(256 + AL/2, with AL's low bit on top).
[[nodiscard]] std::uint16_t Jink(std::uint16_t _random) noexcept
{
  const std::uint8_t low = Low(_random);
  const bool odd = (low & 1) != 0;
  const auto jink = static_cast<std::uint16_t>((1 << 8) | (low >> 1) | (odd ? 0x80 : 0));
  return odd ? Negate(jink) : jink;
}

// UpdateHunterAi's state 3 (5987): evading with random jinks until beyond its range.
void HunterEvade(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (!WithinRangeOnRegisters(_guest))
  {
    SetState(_guest, STATE_IDLE);
    MoveObjectEntry(_guest);
    return;
  }
  const std::uint16_t frames = At(regs.di, SLOT_JINK_FRAMES);
  if (_guest.Byte(frames) == 0)
  {
    NextRandomEntry(_guest);
    regs.ax = Jink(regs.ax);
    _guest.SetWord(At(regs.di, SLOT_JINK_PITCH), regs.ax);
    NextRandomEntry(_guest);
    regs.ax = Jink(regs.ax);
    _guest.SetWord(At(regs.di, SLOT_JINK_YAW), regs.ax);
    _guest.SetByte(frames, JINK_FRAMES);
  }
  _guest.SetByte(frames, static_cast<std::uint8_t>(_guest.Byte(frames) - 1));
  if (_guest.Byte(frames) == 0)
  {
    _guest.SetByte(frames, JINK_FRAMES);
    _guest.SetWord(At(regs.di, SLOT_JINK_PITCH), Negate(_guest.Word(At(regs.di, SLOT_JINK_PITCH))));
    _guest.SetWord(At(regs.di, SLOT_JINK_YAW), Negate(_guest.Word(At(regs.di, SLOT_JINK_YAW))));
  }
  // Away from the player: the heading to it turned half round, plus the jink.
  GetVectorToPlayerEntry(_guest);
  _guest.Call(CONVERT_VECTOR_TO_ANGLES);
  regs.ax = static_cast<std::uint16_t>(regs.ax + HALF_TURN + _guest.Word(At(regs.di, SLOT_JINK_PITCH)));
  regs.bx = static_cast<std::uint16_t>(regs.bx + _guest.Word(At(regs.di, SLOT_JINK_YAW)));
  TurnTowardAnglesEntry(_guest);
  regs.bx = HUNTER_MISSILE_ODDS;
  TryLaunchMissileAtPlayerEntry(_guest);
  ComputeVelocityEntry(_guest);
  MoveObjectEntry(_guest);
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
    else if (hunters.count == 1)
    {
      // MOV [DI+29h],BP: the mate CountOtherHuntersOnScanner found last.
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

// UpdateTraderOrPoliceAi's state 2 (56FC): at the player, firing, the police with missiles against an offender, until close or,
// for any but the police, weak.
void TraderAttack(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  IsPoliceViperEntry(_guest);
  if (!_guest.Flag(FLAG_ZERO) && _guest.Byte(At(regs.di, SLOT_ENERGY)) < TRADER_FLEE_ENERGY)
  {
    SetState(_guest, TRADER_FLEEING);
    MoveObjectEntry(_guest);
    return;
  }
  if (WithinBoxOnRegisters(_guest, TRADER_BREAK_OFF_BOX))
  {
    SetState(_guest, TRADER_BREAKING_OFF);
    MoveObjectEntry(_guest);
    return;
  }
  GetVectorToPlayerEntry(_guest);
  TurnToVectorOnRegisters(_guest);
  TryFireLaserAtPlayerEntry(_guest);
  IsPoliceViperEntry(_guest);
  if (_guest.Flag(FLAG_ZERO) && _guest.Get(DS.legalStatus) != 0)
  {
    regs.bx = _guest.Get(DS.legalStatus) >= FUGITIVE_FOR_POLICE ? FUGITIVE_MISSILE_ODDS : POLICE_MISSILE_ODDS;
    TryLaunchMissileAtPlayerEntry(_guest);
  }
  ComputeVelocityEntry(_guest);
  MoveObjectEntry(_guest);
}

// UpdateTraderOrPoliceAi's state 3 (5750): fleeing from the player with random jinks, a missile when nearly dead, and now and
// then an ECM.
void TraderFlee(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Byte(At(regs.di, SLOT_ENERGY)) < DESPERATE_ENERGY)
  {
    regs.bx = DESPERATE_MISSILE_ODDS;
    TryLaunchMissileAtPlayerEntry(_guest);
  }
  const std::uint16_t frames = At(regs.di, SLOT_JINK_FRAMES);
  if (_guest.Byte(frames) == 0)
  {
    NextRandomEntry(_guest);
    regs.ax = Jink(regs.ax);
    _guest.SetWord(At(regs.di, SLOT_JINK_PITCH), regs.ax);
    NextRandomEntry(_guest);
    regs.ax = Jink(regs.ax);
    _guest.SetWord(At(regs.di, SLOT_JINK_YAW), regs.ax);
    _guest.SetByte(frames, JINK_FRAMES);
  }
  _guest.SetByte(frames, static_cast<std::uint8_t>(_guest.Byte(frames) - 1));
  if (_guest.Byte(frames) == 0)
  {
    _guest.SetByte(frames, JINK_FRAMES);
    _guest.SetWord(At(regs.di, SLOT_JINK_PITCH), Negate(_guest.Word(At(regs.di, SLOT_JINK_PITCH))));
    _guest.SetWord(At(regs.di, SLOT_JINK_YAW), Negate(_guest.Word(At(regs.di, SLOT_JINK_YAW))));
  }
  GetVectorToPlayerEntry(_guest);
  _guest.Call(CONVERT_VECTOR_TO_ANGLES);
  regs.ax = static_cast<std::uint16_t>(regs.ax + HALF_TURN + _guest.Word(At(regs.di, SLOT_JINK_PITCH)));
  regs.bx = static_cast<std::uint16_t>(regs.bx + _guest.Word(At(regs.di, SLOT_JINK_YAW)));
  TurnTowardAnglesEntry(_guest);
  ComputeVelocityEntry(_guest);
  NextRandomEntry(_guest);
  if (regs.ax < TRADER_ECM_ODDS)
  {
    _guest.Set(DS.npcEcmFrames, TRADER_ECM_FRAMES);
  }
  MoveObjectEntry(_guest);
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

} // namespace

void UpdateObjectsAndSpawn(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.es = regs.ds;
  // REP STOSB of AL = 0 over activeObjectCount and behaviorClassCounts.
  regs.di = DS.activeObjectCount.offset;
  SetLow(regs.ax, 0);
  const std::uint16_t step = _guest.Flag(FLAG_DIRECTION) ? std::uint16_t{0xFFFF} : std::uint16_t{1};
  for (regs.cx = CLASS_COUNTS_CLEARED; regs.cx != 0; regs.cx = static_cast<std::uint16_t>(regs.cx - 1))
  {
    _guest.SetFarByte(regs.es, regs.di, 0);
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
  // Every slot: an active one is counted and its class's handler run (CALL [BX+behaviorHandlers]), CX and DI pushed round it.
  regs.di = DS.shipSlots.offset;
  regs.cx = _guest.Get(DS.shipSlotCount);
  do
  {
    const std::uint16_t remaining = regs.cx;
    const std::uint16_t slot = regs.di;
    if ((_guest.Byte(slot) & SLOT_ACTIVE) != 0)
    {
      const std::uint8_t behavior = _guest.Byte(At(slot, SLOT_CLASS));
      if (behavior != DEBRIS_CLASS)
      {
        _guest.Set(DS.activeObjectCount, static_cast<std::uint8_t>(_guest.Get(DS.activeObjectCount) + 1));
      }
      regs.bx = behavior;
      const std::uint16_t count = At(DS.behaviorClassCounts.offset, regs.bx);
      _guest.SetByte(count, static_cast<std::uint8_t>(_guest.Byte(count) + 1));
      regs.bx = static_cast<std::uint16_t>(regs.bx << 1);
      _guest.Call(_guest.Word(At(DS.behaviorHandlers.offset, regs.bx)));
    }
    regs.di = At(slot, SLOT_BYTES);
    regs.cx = static_cast<std::uint16_t>(remaining - 1);
  } while (regs.cx != 0);

  _guest.Set(DS.maskingBackgroundColor, 0);
  if (_guest.Get(DS.activeObjectCount) >= MOST_ACTIVE_FOR_SPAWNING)
  {
    return;
  }
  SetLow(regs.ax, _guest.Get(DS.objectSlotCount));
  if (Low(regs.ax) < _guest.Get(DS.activeObjectCount))
  {
    return;
  }
  // What the spawning leaves in the registers is not reproduced: the contract compares none of them, and poisoned, every
  // comparison and digest still agrees.
  if (_guest.Get(DS.maskMissionShipsLeft) != 0)
  {
    if (_guest.Get(DS.maskSystemJumps) == 1)
    {
      SpawnMaskMissionShips(_guest.State());
      return;
    }
  }
  else
  {
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Get(DS.thargoidInvasionActive) & _guest.Get(DS.jumpedSinceBriefing)));
    if (Low(regs.ax) != 0 && _guest.Get(DS.invadedStationDestroyed) != 1)
    {
      SpawnInvasionWave(_guest.State());
      return;
    }
  }
  SpawnByGovernment(_guest.State());
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

void UpdateStationAi(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot station(_guest.State(), regs.di);
  AddWord(_guest, At(regs.di, SLOT_ROLL), STATION_SPIN);
  if (_guest.Get(DS.spawnGovernment) >= 1 && (_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_HOSTILE) != 0)
  {
    // What the look for an offender leaves of what the contract compares, and of DX, which the next slot's handler can read
    // (WithinRange's DL): what IsObjectNear leaves, ES = B800h and DX the pixel once it erased the station's blip; DX = 1C2h,
    // the guarded box, once it is near; CopyObject's ES = DS once it launched; and the pitch FacePlayerWithRandomRoll leaves in
    // BP once that was a trader. ComputeVelocity's DX after a launch is not reproduced, nor the AX, BX, CX and SI it leaves:
    // poisoned, every comparison and digest still agrees.
    const OffenderLaunch launch = LaunchAtOffender(_guest.State(), station, _guest.Flag(FLAG_DIRECTION));
    IsObjectNearOut(_guest, station, launch.near);
    if (launch.near.nearby)
    {
      regs.dx = STATION_GUARD_BOX;
    }
    if (launch.launched)
    {
      regs.es = regs.ds;
    }
    if (launch.trader)
    {
      regs.bp = launch.trader->heading.first;
    }
  }
  // DI past the slots and ES as RemoveAllMissiles leaves them, which the contract compares, once the ECM ran. The AX, BX, CX and
  // SI its look at the missiles leaves, which the contract does not compare, and the DX RemoveAllMissiles leaves, are not
  // reproduced: poisoned, every comparison and digest still agrees.
  const StationEcm ecm = CheckMissilesAtStation(_guest.State(), station);
  if (ecm.removed)
  {
    RemoveAllMissilesOut(_guest, ecm.erased);
  }
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

void UpdateTraderOrPoliceAi(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  IsDebrisTypeEntry(_guest);
  if (_guest.Flag(FLAG_ZERO))
  {
    // A rock only spins. The split at 56B1 (LaunchShipFromObject with DL=5) never runs: 56A5 jumps past it when bit 0 of +1Eh is
    // clear, and 56AB when it is set.
    AddWord(_guest, At(regs.di, SLOT_ROLL), ROCK_SPIN);
    MoveObjectEntry(_guest);
    return;
  }
  const std::uint8_t state = State(_guest);
  if (state == STATE_IDLE)
  {
    SetState(_guest, TRADER_DECIDING);
    MoveObjectEntry(_guest);
    return;
  }
  // The states de-assembled leave what MoveObject leaves (MoveObjectOut): AX, and DX, whose DL the next slot's handler can take
  // for its range's box (WithinRange). The rest but DI is not reproduced: the contract compares none of it, and poisoned, every
  // comparison and digest still agrees.
  const ObjectSlot slot(_guest.State(), regs.di);
  if (state == TRADER_DECIDING)
  {
    MoveObjectOut(_guest, slot, TraderDecide(_guest.State(), slot));
    return;
  }
  if (state == TRADER_ATTACKING)
  {
    TraderAttack(_guest);
    return;
  }
  if (state == TRADER_FLEEING)
  {
    TraderFlee(_guest);
    return;
  }
  MoveObjectOut(_guest, slot, TraderBreakOff(_guest.State(), slot, Low(regs.dx)));
}

void UpdateWolfAi(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  IsThargoidTypeEntry(_guest);
  bool spins = _guest.Flag(FLAG_ZERO);
  if (spins)
  {
    NextRandomEntry(_guest);
    if (regs.ax < THARGOID_ECM_ODDS)
    {
      _guest.Set(DS.npcEcmFrames, THARGOID_ECM_FRAMES);
    }
  }
  else
  {
    IsThargonTypeEntry(_guest);
    spins = _guest.Flag(FLAG_ZERO);
  }
  if (spins)
  {
    AddWord(_guest, At(regs.di, SLOT_ROLL), WOLF_SPIN);
  }

  if (State(_guest) == STATE_IDLE)
  {
    SetState(_guest, STATE_ATTACK);
    MoveObjectEntry(_guest);
    return;
  }
  if (State(_guest) == STATE_ATTACK)
  {
    // Resting: a new set of 2-5 passes once it is not hostile, or calm no more.
    if ((_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_HOSTILE) == 0 || _guest.Byte(At(regs.di, SLOT_AGGRESSION)) >= CALM)
    {
      NextRandomEntry(_guest);
      SetHigh(regs.ax, static_cast<std::uint8_t>((High(regs.ax) & PASS_COUNT_MASK) + FEWEST_PASSES));
      _guest.SetByte(At(regs.di, SLOT_PASSES), High(regs.ax));
      SetState(_guest, STATE_ATTACK_RUN);
    }
    MoveObjectEntry(_guest);
    return;
  }
  if (State(_guest) == STATE_ATTACK_RUN)
  {
    if (WithinBoxOnRegisters(_guest, BREAK_OFF_BOX))
    {
      SetState(_guest, STATE_TURN_AWAY);
      MoveObjectEntry(_guest);
      return;
    }
    GetVectorToPlayerEntry(_guest);
    TurnToVectorOnRegisters(_guest);
    TryFireLaserAtPlayerEntry(_guest);
    regs.bx = WOLF_MISSILE_ODDS;
    TryLaunchMissileAtPlayerEntry(_guest);
    TryLaunchThargonEntry(_guest);
    ComputeVelocityEntry(_guest);
    MoveObjectEntry(_guest);
    AddWord(_guest, At(regs.di, SLOT_ROLL), ATTACK_RUN_SPIN);
    return;
  }
  if (State(_guest) == STATE_TURN_AWAY)
  {
    WolfTurnAway(_guest);
    return;
  }
  // A Thargon adrift slows down to 10.
  const std::uint16_t speed = At(regs.di, SLOT_SPEED);
  if (_guest.Byte(speed) >= SLOWEST_ADRIFT)
  {
    _guest.SetByte(speed, static_cast<std::uint8_t>(_guest.Byte(speed) - ADRIFT_SLOWING));
    ComputeVelocityEntry(_guest);
  }
  MoveObjectEntry(_guest);
}

void UpdateHunterAi(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (State(_guest) == STATE_IDLE)
  {
    // What MoveObject leaves (MoveObjectOut), as UpdateTraderOrPoliceAi's states de-assembled leave it.
    const ObjectSlot slot(_guest.State(), regs.di);
    MoveObjectOut(_guest, slot, HunterIdle(_guest.State(), slot));
    return;
  }
  if (State(_guest) == STATE_ATTACK)
  {
    if (WithinBoxOnRegisters(_guest, BREAK_OFF_BOX))
    {
      SetState(_guest, STATE_TURN_AWAY);
      MoveObjectEntry(_guest);
      return;
    }
    GetVectorToPlayerEntry(_guest);
    TurnToVectorOnRegisters(_guest);
    TryFireLaserAtPlayerEntry(_guest);
    regs.bx = HUNTER_MISSILE_ODDS;
    TryLaunchMissileAtPlayerEntry(_guest);
    ComputeVelocityEntry(_guest);
    AddWord(_guest, At(regs.di, SLOT_ROLL), HUNTER_SPIN);
    MoveObjectEntry(_guest);
    return;
  }
  if (State(_guest) == STATE_FORMATION)
  {
    regs.si = _guest.Word(At(regs.di, SLOT_TARGET));
    regs.dx = FORMATION_BOX;
    GetVectorToObjectEntry(_guest);
    if (_guest.Flag(FLAG_CARRY))
    {
      SetState(_guest, STATE_CLOSING);
      MoveObjectEntry(_guest);
      return;
    }
    TurnToVectorOnRegisters(_guest);
    ComputeVelocityEntry(_guest);
    MoveObjectEntry(_guest);
    return;
  }
  if (State(_guest) == STATE_TURN_AWAY)
  {
    HunterEvade(_guest);
    return;
  }
  // Closing on the player while farther than 5000 on some axis.
  if (WithinBoxOnRegisters(_guest, CLOSING_BOX))
  {
    SetState(_guest, STATE_IDLE);
    MoveObjectEntry(_guest);
    return;
  }
  GetVectorToPlayerEntry(_guest);
  TurnToVectorOnRegisters(_guest);
  TryFireLaserAtPlayerEntry(_guest);
  regs.bx = CLOSING_MISSILE_ODDS;
  TryLaunchMissileAtPlayerEntry(_guest);
  ComputeVelocityEntry(_guest);
  MoveObjectEntry(_guest);
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

// "Clobbers most registers": all but DI, the slot, and DS.
constexpr Machine::NativeContract CLOBBERS_MOST{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_BP | REGISTER_ES, 0};

constexpr Machine::NativeContract CLOBBERS_CX_SI{REGISTER_CX | REGISTER_SI, 0};
constexpr Machine::NativeContract MASK_SHIP_SEARCH{REGISTER_CX, FLAG_CARRY};
constexpr Machine::NativeContract ESCORT_PLACED{REGISTER_AX | REGISTER_CX | REGISTER_DX | REGISTER_SI, 0};
// TurnTowardAngles: the CX and BP it leaves are compared (TurnTowardAnglesEntry).
constexpr Machine::NativeContract TURNED{REGISTER_DX, 0};
constexpr Machine::NativeContract OBJECT_VECTOR{REGISTER_DX | REGISTER_BP, FLAG_CARRY};
// CheckSafeZoneHoldFire: every register compared, AL as InSafeZone leaves it (CheckSafeZoneHoldFireEntry).
constexpr Machine::NativeContract HOLD_FIRE{0, FLAG_CARRY};

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
  NativeEntry{0x4A10, "UpdateObjectsAndSpawn", &UpdateObjectsAndSpawn, Machine::NativeContract{REGISTER_ALL, 0}},
  NativeEntry{0x4C0E, "ScaleSpawnOdds", &ScaleSpawnOddsEntry, PRESERVES_ALL},
  NativeEntry{0x4C20, "IsMaskShipPresent", &IsMaskShipPresentEntry, MASK_SHIP_SEARCH},
  NativeEntry{0x4C38, "PlaceEscortNear", &PlaceEscortNearEntry, ESCORT_PLACED},
  NativeEntry{0x514B, "TurnTowardAngles", &TurnTowardAnglesEntry, TURNED},
  NativeEntry{0x5166, "ClampTurnStep", &ClampTurnStepEntry, PRESERVES_ALL},
  NativeEntry{0x548F, "CountOtherHuntersOnScanner", &CountOtherHuntersOnScannerEntry, CLOBBERS_CX_SI},
  NativeEntry{0x54B4, "GetVectorToObject", &GetVectorToObjectEntry, OBJECT_VECTOR},
  NativeEntry{0x5594, "SkipInertObjectAi", &SkipInertObjectAi, PRESERVES_ALL},
  NativeEntry{0x5595, "UpdateStationAi", &UpdateStationAi,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI, 0}},
  NativeEntry{0x5681, "UpdateDriftingObjectAi", &UpdateDriftingObjectAiEntry, PRESERVES_ALL},
  NativeEntry{0x569C, "UpdateTraderOrPoliceAi", &UpdateTraderOrPoliceAi, CLOBBERS_MOST},
  NativeEntry{0x57E8, "UpdateWolfAi", &UpdateWolfAi, CLOBBERS_MOST},
  NativeEntry{0x58DE, "UpdateHunterAi", &UpdateHunterAi, CLOBBERS_MOST},
  NativeEntry{0x5A10, "CheckSafeZoneHoldFire", &CheckSafeZoneHoldFireEntry, HOLD_FIRE},
};

} // namespace

std::span<const NativeEntry> AiEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
