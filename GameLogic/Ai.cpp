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
constexpr std::uint16_t IN_SAFE_ZONE = 0x2E63;
constexpr std::uint16_t OBJECT_WITHIN_BOX = 0x2F65;
constexpr std::uint16_t IS_MASK_SHIP_PRESENT = 0x4C20;
constexpr std::uint16_t PLACE_ESCORT_NEAR = 0x4C38;
constexpr std::uint16_t INIT_POLICE_VIPER = 0x4C76;
constexpr std::uint16_t INIT_SHUTTLE = 0x4CC0;
constexpr std::uint16_t SPAWN_RANDOM_DRIFTER = 0x4CE7;
constexpr std::uint16_t SPAWN_RANDOM_TRADER = 0x4D08;
constexpr std::uint16_t SPAWN_MASK_MISSION_SHIP = 0x4DAE;
constexpr std::uint16_t SPAWN_INVASION_THARGOID = 0x4DF0;
constexpr std::uint16_t CONVERT_VECTOR_TO_ANGLES = 0x4F08;
constexpr std::uint16_t REMOVE_ALL_MISSILES = 0x4F9F;
constexpr std::uint16_t GET_VECTOR_TO_OBJECT = 0x54B4;

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
constexpr std::uint8_t MASK_SHIP_CARGO = 0x14;
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

// ObjectWithinBox with DX = _halfSize: CF, whether the slot at DI is within it on every axis.
[[nodiscard]] bool WithinBox(Guest& _guest, std::uint16_t _halfSize)
{
  _guest.Regs().dx = _halfSize;
  _guest.Call(OBJECT_WITHIN_BOX);
  return _guest.Flag(FLAG_CARRY);
}

// ObjectWithinBox with DH = the slot's range byte, DL as it is.
[[nodiscard]] bool WithinRange(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetHigh(regs.dx, _guest.Byte(At(regs.di, SLOT_RANGE)));
  _guest.Call(OBJECT_WITHIN_BOX);
  return _guest.Flag(FLAG_CARRY);
}

// ConvertVectorToAngles on AX, BX, CX, then TurnTowardAngles.
void TurnToVector(Guest& _guest)
{
  _guest.Call(CONVERT_VECTOR_TO_ANGLES);
  TurnTowardAngles(_guest);
}

// NextRandom against the column of spawnOddsByGovernment at _column, scaled: true when the random word is below the odds.
[[nodiscard]] bool SpawnOddsMet(Guest& _guest, std::uint16_t _column)
{
  Machine::Registers& regs = _guest.Regs();
  NextRandom(_guest);
  regs.bx = _guest.Get(DS.spawnOddsOffset);
  regs.bx = _guest.Word(At(_column, regs.bx));
  ScaleSpawnOdds(_guest);
  return regs.ax < regs.bx;
}

// The column of spawnLimitsByGovernment at _column into AL, against the class count _count: true while there is room.
[[nodiscard]] bool BelowSpawnLimit(Guest& _guest, std::uint16_t _column, DataField<std::uint8_t> _count)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = _guest.Get(DS.spawnLimitOffset);
  SetLow(regs.ax, _guest.Byte(At(_column, regs.bx)));
  return _guest.Get(_count) < Low(regs.ax);
}

// FindFreeShipSlot, and DI = the slot found. False when there is none, which ends UpdateObjectsAndSpawn.
[[nodiscard]] bool TakeFreeShipSlot(Guest& _guest)
{
  FindFreeShipSlot(_guest);
  if (!_guest.Flag(FLAG_CARRY))
  {
    return false;
  }
  Machine::Registers& regs = _guest.Regs();
  regs.di = regs.si;
  return true;
}

// SpawnByGovernment (4A86): a drifter, a trader, a hunter and a wolf, each while its class is below the government's limit and
// a random word is below its odds.
void SpawnByGovernment(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = static_cast<std::uint16_t>(_guest.Get(DS.currentGovernment) << 3);
  _guest.Set(DS.spawnOddsOffset, regs.bx);
  regs.bx = static_cast<std::uint16_t>(_guest.Get(DS.spawnGovernment) << 2);
  _guest.Set(DS.spawnLimitOffset, regs.bx);
  if (_guest.Get(DS.witchspaceCountdown) == 0)
  {
    SetLow(regs.ax, _guest.Byte(At(DS.spawnLimitsByGovernment.offset, regs.bx)));
    if (_guest.Get(DS.miningLaserCount) == 1)
    {
      SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) + 1));
    }
    if (_guest.Get(DS.drifterCount) < Low(regs.ax) && SpawnOddsMet(_guest, DS.spawnOddsByGovernment.offset))
    {
      if (!TakeFreeShipSlot(_guest))
      {
        return;
      }
      _guest.Call(SPAWN_RANDOM_DRIFTER);
    }
    if (BelowSpawnLimit(_guest, DS.traderLimitColumn.offset, DS.traderCount) && SpawnOddsMet(_guest, DS.traderOddsColumn.offset))
    {
      if (!TakeFreeShipSlot(_guest))
      {
        return;
      }
      _guest.Call(SPAWN_RANDOM_TRADER);
    }
    if (BelowSpawnLimit(_guest, DS.hunterLimitColumn.offset, DS.hunterCount) && SpawnOddsMet(_guest, DS.hunterOddsColumn.offset))
    {
      if (!TakeFreeShipSlot(_guest))
      {
        return;
      }
      SpawnRandomHunter(_guest);
    }
  }
  if (!BelowSpawnLimit(_guest, DS.wolfLimitColumn.offset, DS.wolfCount))
  {
    return;
  }
  if (_guest.Get(DS.witchspaceCountdown) == 0)
  {
    if (!SpawnOddsMet(_guest, DS.wolfOddsColumn.offset))
    {
      return;
    }
    if (_guest.Get(DS.spawnGovernment) != 0 && regs.ax >= WOLF_ODDS_LIMIT)
    {
      return;
    }
  }
  if (TakeFreeShipSlot(_guest))
  {
    SpawnRandomWolf(_guest);
  }
}

// The mask mission's Asp made the mask ship: it carries the device, 20 tonnes of cargo, and no aggression yet.
void MarkMaskShip(Guest& _guest)
{
  const std::uint16_t slot = _guest.Regs().di;
  _guest.SetByte(At(slot, SLOT_FLAGS), static_cast<std::uint8_t>(_guest.Byte(At(slot, SLOT_FLAGS)) | FLAG_DEVICE));
  _guest.SetByte(At(slot, SLOT_CARGO), MASK_SHIP_CARGO);
  _guest.SetByte(At(slot, SLOT_AGGRESSION), 0);
}

// SpawnMaskMissionShip in a free slot, CF in saying whether it is the mask ship. False when there is no slot.
[[nodiscard]] bool SpawnMaskMissionShipInFreeSlot(Guest& _guest, bool _maskShip)
{
  if (!TakeFreeShipSlot(_guest))
  {
    return false;
  }
  _guest.SetFlag(FLAG_CARRY, _maskShip);
  _guest.Call(SPAWN_MASK_MISSION_SHIP);
  return true;
}

// SpawnMaskMissionShips (4B73).
void SpawnMaskMissionShips(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.maskShipDestroyed) == 1)
  {
    return;
  }
  const std::uint8_t wolves = _guest.Get(DS.wolfCount);
  if (wolves != 0 && wolves >= MOST_WOLVES_WITH_MASK_SHIP)
  {
    return;
  }
  if (wolves == 0 && _guest.Get(DS.maskShipDestroyed) != 1)
  {
    // The mask ship and two escorts placed near it.
    if (!TakeFreeShipSlot(_guest))
    {
      return;
    }
    _guest.Set(DS.maskShipSlot, regs.si);
    _guest.SetFlag(FLAG_CARRY, true);
    _guest.Call(SPAWN_MASK_MISSION_SHIP);
    MarkMaskShip(_guest);
    for (int escort = 0; escort < 2; ++escort)
    {
      if (!SpawnMaskMissionShipInFreeSlot(_guest, false))
      {
        return;
      }
      regs.si = _guest.Get(DS.maskShipSlot);
      _guest.Call(PLACE_ESCORT_NEAR);
    }
    return;
  }
  // One more mission ship, which becomes the mask ship when that is missing.
  if (!SpawnMaskMissionShipInFreeSlot(_guest, false))
  {
    return;
  }
  _guest.Call(IS_MASK_SHIP_PRESENT);
  if (_guest.Flag(FLAG_CARRY) || _guest.Get(DS.maskShipDestroyed) == 1)
  {
    return;
  }
  MarkMaskShip(_guest);
  _guest.SetByte(regs.di, ASP_ACTIVE);
}

// SpawnInvasionWave (4BF7): a Thargoid while the player is in the safe zone and fewer than 8 wolves fly.
void SpawnInvasionWave(Guest& _guest)
{
  _guest.Call(IN_SAFE_ZONE);
  if (!_guest.Flag(FLAG_CARRY) || _guest.Get(DS.wolfCount) >= MOST_INVADERS)
  {
    return;
  }
  if (TakeFreeShipSlot(_guest))
  {
    _guest.Call(SPAWN_INVASION_THARGOID);
  }
}

// From 55A9: the station launches a police Viper, a shuttle or a trader at an offender out of the box it guards.
void LaunchAtOffender(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  IsObjectNear(_guest);
  if (!_guest.Flag(FLAG_CARRY) || WithinBox(_guest, STATION_GUARD_BOX))
  {
    return;
  }
  const std::uint8_t status = _guest.Get(DS.legalStatus);
  if (status < OFFENDER)
  {
    return;
  }
  NextRandom(_guest);
  if (regs.ax >= (status >= FUGITIVE ? FUGITIVE_LAUNCH_ODDS : OFFENDER_LAUNCH_ODDS))
  {
    return;
  }
  FindFreeShipSlot(_guest);
  if (!_guest.Flag(FLAG_CARRY))
  {
    return;
  }
  const std::uint16_t station = regs.di;
  std::swap(regs.di, regs.si);
  CopyObject(_guest);
  NextRandom(_guest);
  if (regs.ax >= POLICE_FROM)
  {
    _guest.Call(INIT_POLICE_VIPER);
  }
  else if (regs.ax >= SHUTTLE_FROM)
  {
    _guest.Call(INIT_SHUTTLE);
  }
  else
  {
    _guest.Call(SPAWN_RANDOM_TRADER);
  }
  // ADD [DI+8],0F0h / ADC [DI+3],0: out along z, turned round, its roll reversed.
  const std::uint16_t z = At(regs.di, SLOT_Z);
  const std::uint32_t sum = std::uint32_t{_guest.Word(z)} + LAUNCH_DISTANCE;
  _guest.SetWord(z, static_cast<std::uint16_t>(sum));
  const std::uint16_t zHigh = At(regs.di, SLOT_X_HIGH + 2);
  _guest.SetByte(zHigh, static_cast<std::uint8_t>(_guest.Byte(zHigh) + (sum >> 16)));
  AddWord(_guest, At(regs.di, SLOT_YAW), LAUNCH_TURN);
  _guest.SetWord(At(regs.di, SLOT_ROLL), Negate(_guest.Word(At(regs.di, SLOT_ROLL))));
  ComputeVelocity(_guest);
  regs.di = station;
}

// CheckMissilesAtStation (5611): a missile aimed at the station, or at a police Viper in the safe zone, is a crime and starts
// the station's ECM, which then removes every missile for npcEcmFrames frames.
void CheckMissilesAtStation(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.npcEcmFrames) == 0)
  {
    regs.cx = _guest.Get(DS.objectSlotCount);
    regs.si = DS.shipSlots.offset;
    bool crime = false;
    do
    {
      const std::uint8_t first = _guest.Byte(regs.si);
      SetLow(regs.ax, static_cast<std::uint8_t>(first >> 1));
      if ((first & SLOT_ACTIVE) != 0)
      {
        SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & TYPE_MASK));
        if (Low(regs.ax) == TYPE_MISSILE)
        {
          regs.bx = _guest.Word(At(regs.si, SLOT_TARGET));
        }
        if (Low(regs.ax) == TYPE_MISSILE && regs.bx != 0)
        {
          SetLow(regs.ax, STATION_MISSILE_CRIME);
          if (regs.bx == regs.di)
          {
            crime = true;
            break;
          }
          std::swap(regs.di, regs.bx);
          IsPoliceViper(_guest);
          std::swap(regs.di, regs.bx);
          if (_guest.Flag(FLAG_ZERO))
          {
            _guest.Call(IN_SAFE_ZONE);
            SetLow(regs.ax, POLICE_MISSILE_CRIME);
            if (_guest.Flag(FLAG_CARRY))
            {
              crime = true;
              break;
            }
          }
        }
      }
      regs.si = At(regs.si, SLOT_BYTES);
      regs.cx = static_cast<std::uint16_t>(regs.cx - 1);
    } while (regs.cx != 0);
    if (!crime)
    {
      return;
    }
    const unsigned status = _guest.Get(DS.legalStatus) + unsigned{Low(regs.ax)};
    _guest.Set(DS.legalStatus, status > 0xFF ? std::uint8_t{0xFF} : static_cast<std::uint8_t>(status));
    if (_guest.Get(DS.antiEcmActive) == 1)
    {
      return;
    }
    _guest.Set(DS.npcEcmFrames, STATION_ECM_FRAMES);
  }
  if (_guest.Get(DS.thargoidInvasionActive) == 1)
  {
    _guest.Set(DS.npcEcmFrames, 0);
    return;
  }
  _guest.Set(DS.ecmFired, 1);
  _guest.Call(REMOVE_ALL_MISSILES);
  _guest.Set(DS.npcEcmFrames, static_cast<std::uint8_t>(_guest.Get(DS.npcEcmFrames) - 1));
}

// UpdateWolfAi's state 3 (586D): turning away until beyond its range, then a pass is counted off.
void WolfTurnAway(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (WithinRange(_guest))
  {
    GetObjectPosition(_guest);
    TurnToVector(_guest);
    ComputeVelocity(_guest);
    TryLaunchThargon(_guest);
    MoveObject(_guest);
    return;
  }
  const std::uint16_t passes = At(regs.di, SLOT_PASSES);
  _guest.SetByte(passes, static_cast<std::uint8_t>(_guest.Byte(passes) - 1));
  bool rest = _guest.Byte(passes) == 0;
  if (!rest)
  {
    // A Thargon goes back to its run only while its mother is a Thargoid with a blip.
    IsThargonType(_guest);
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
    IsThargonType(_guest);
    if (_guest.Flag(FLAG_ZERO))
    {
      SetState(_guest, THARGON_ADRIFT);
    }
  }
  else
  {
    SetState(_guest, STATE_ATTACK_RUN);
  }
  MoveObject(_guest);
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
  if (!WithinRange(_guest))
  {
    SetState(_guest, STATE_IDLE);
    MoveObject(_guest);
    return;
  }
  const std::uint16_t frames = At(regs.di, SLOT_JINK_FRAMES);
  if (_guest.Byte(frames) == 0)
  {
    NextRandom(_guest);
    regs.ax = Jink(regs.ax);
    _guest.SetWord(At(regs.di, SLOT_JINK_PITCH), regs.ax);
    NextRandom(_guest);
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
  GetVectorToPlayer(_guest);
  _guest.Call(CONVERT_VECTOR_TO_ANGLES);
  regs.ax = static_cast<std::uint16_t>(regs.ax + HALF_TURN + _guest.Word(At(regs.di, SLOT_JINK_PITCH)));
  regs.bx = static_cast<std::uint16_t>(regs.bx + _guest.Word(At(regs.di, SLOT_JINK_YAW)));
  TurnTowardAngles(_guest);
  regs.bx = HUNTER_MISSILE_ODDS;
  TryLaunchMissileAtPlayer(_guest);
  ComputeVelocity(_guest);
  MoveObject(_guest);
}

// UpdateHunterAi's state 0 (58E4): flying at the player until it turns hostile, then picking how to hunt.
void HunterIdle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t flags = _guest.Byte(At(regs.di, SLOT_FLAGS));
  if ((flags & FLAG_BLIP_DRAWN) == 0)
  {
    GetVectorToPlayer(_guest);
    TurnToVector(_guest);
    ComputeVelocity(_guest);
    MoveObject(_guest);
    return;
  }
  if ((flags & FLAG_HOSTILE) != 0)
  {
    SetState(_guest, STATE_TURN_AWAY);
  }
  else
  {
    CountOtherHuntersOnScanner(_guest);
    if (Low(regs.ax) >= 2)
    {
      bool attack = _guest.Get(DS.legalStatus) >= FUGITIVE;
      if (!attack)
      {
        NextRandom(_guest);
        attack = regs.ax < PACK_ATTACK_ODDS;
      }
      if (attack)
      {
        SetState(_guest, STATE_ATTACK);
      }
    }
    else if (Low(regs.ax) == 1)
    {
      _guest.SetWord(At(regs.di, SLOT_TARGET), regs.bp);
      SetState(_guest, STATE_FORMATION);
    }
  }
  MoveObject(_guest);
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
  if (_guest.Get(DS.maskMissionShipsLeft) != 0)
  {
    if (_guest.Get(DS.maskSystemJumps) == 1)
    {
      SpawnMaskMissionShips(_guest);
      return;
    }
  }
  else
  {
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Get(DS.thargoidInvasionActive) & _guest.Get(DS.jumpedSinceBriefing)));
    if (Low(regs.ax) != 0 && _guest.Get(DS.invadedStationDestroyed) != 1)
    {
      SpawnInvasionWave(_guest);
      return;
    }
  }
  SpawnByGovernment(_guest);
}

void ScaleSpawnOdds(Guest& _guest)
{
  if (_guest.Get(DS.jumpDriveEngaged) == 1)
  {
    Machine::Registers& regs = _guest.Regs();
    regs.bx = static_cast<std::uint16_t>(regs.bx << JUMP_DRIVE_ODDS_SHIFT);
  }
}

void TurnTowardAngles(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.dx = regs.bx;
  regs.cx = _guest.Word(At(regs.di, SLOT_PITCH));
  ClampTurnStep(_guest);
  AddWord(_guest, At(regs.di, SLOT_PITCH), regs.ax);
  regs.bp = regs.bx;
  regs.cx = _guest.Word(At(regs.di, SLOT_YAW));
  regs.ax = regs.dx;
  ClampTurnStep(_guest);
  AddWord(_guest, At(regs.di, SLOT_YAW), regs.ax);
  regs.ax = regs.bp;
}

void ClampTurnStep(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The error is not wrapped at 2048, so a turn across 0 goes the long way.
  const auto error = static_cast<std::uint16_t>((regs.ax & ANGLE_MASK) - (regs.cx & ANGLE_MASK));
  regs.bx = error;
  regs.ax = (error & 0x8000) != 0 ? Negate(error) : error;
  regs.cx = _guest.Byte(At(regs.di, SLOT_TURN_RATE));
  if (regs.ax < regs.cx)
  {
    std::swap(regs.ax, regs.bx);
    return;
  }
  if ((regs.bx & 0x8000) != 0)
  {
    regs.cx = Negate(regs.cx);
  }
  regs.bx = regs.ax;
  regs.ax = regs.cx;
}

void CountOtherHuntersOnScanner(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.shipSlots.offset;
  regs.cx = _guest.Get(DS.objectSlotCount);
  SetLow(regs.ax, 0);
  do
  {
    if (_guest.Byte(At(regs.si, SLOT_CLASS)) == HUNTER_CLASS && (_guest.Byte(At(regs.si, SLOT_FLAGS)) & FLAG_BLIP_DRAWN) != 0 &&
        regs.si != regs.di)
    {
      regs.bp = regs.si;
      SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) + 1));
    }
    regs.si = At(regs.si, SLOT_BYTES);
    regs.cx = static_cast<std::uint16_t>(regs.cx - 1);
  } while (regs.cx != 0);
}

void SkipInertObjectAi(Guest& /*_guest*/) {}

void UpdateStationAi(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  AddWord(_guest, At(regs.di, SLOT_ROLL), STATION_SPIN);
  if (_guest.Get(DS.spawnGovernment) >= 1 && (_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_HOSTILE) != 0)
  {
    LaunchAtOffender(_guest);
  }
  CheckMissilesAtStation(_guest);
}

void UpdateDriftingObjectAi(Guest& _guest)
{
  MoveObject(_guest);
  IsDebrisType(_guest);
  if (!_guest.Flag(FLAG_ZERO))
  {
    return;
  }
  Machine::Registers& regs = _guest.Regs();
  regs.ax = ROCK_TUMBLE_A;
  regs.bx = ROCK_TUMBLE_B;
  if ((_guest.Byte(regs.di) & ROCK_TUMBLE_SWAP) == 0)
  {
    std::swap(regs.ax, regs.bx);
  }
  AddWord(_guest, At(regs.di, SLOT_PITCH), regs.ax);
  AddWord(_guest, At(regs.di, SLOT_ROLL), regs.bx);
}

void UpdateWolfAi(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  IsThargoidType(_guest);
  bool spins = _guest.Flag(FLAG_ZERO);
  if (spins)
  {
    NextRandom(_guest);
    if (regs.ax < THARGOID_ECM_ODDS)
    {
      _guest.Set(DS.npcEcmFrames, THARGOID_ECM_FRAMES);
    }
  }
  else
  {
    IsThargonType(_guest);
    spins = _guest.Flag(FLAG_ZERO);
  }
  if (spins)
  {
    AddWord(_guest, At(regs.di, SLOT_ROLL), WOLF_SPIN);
  }

  if (State(_guest) == STATE_IDLE)
  {
    SetState(_guest, STATE_ATTACK);
    MoveObject(_guest);
    return;
  }
  if (State(_guest) == STATE_ATTACK)
  {
    // Resting: a new set of 2-5 passes once it is not hostile, or calm no more.
    if ((_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_HOSTILE) == 0 || _guest.Byte(At(regs.di, SLOT_AGGRESSION)) >= CALM)
    {
      NextRandom(_guest);
      SetHigh(regs.ax, static_cast<std::uint8_t>((High(regs.ax) & PASS_COUNT_MASK) + FEWEST_PASSES));
      _guest.SetByte(At(regs.di, SLOT_PASSES), High(regs.ax));
      SetState(_guest, STATE_ATTACK_RUN);
    }
    MoveObject(_guest);
    return;
  }
  if (State(_guest) == STATE_ATTACK_RUN)
  {
    if (WithinBox(_guest, BREAK_OFF_BOX))
    {
      SetState(_guest, STATE_TURN_AWAY);
      MoveObject(_guest);
      return;
    }
    GetVectorToPlayer(_guest);
    TurnToVector(_guest);
    TryFireLaserAtPlayer(_guest);
    regs.bx = WOLF_MISSILE_ODDS;
    TryLaunchMissileAtPlayer(_guest);
    TryLaunchThargon(_guest);
    ComputeVelocity(_guest);
    MoveObject(_guest);
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
    ComputeVelocity(_guest);
  }
  MoveObject(_guest);
}

void UpdateHunterAi(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (State(_guest) == STATE_IDLE)
  {
    HunterIdle(_guest);
    return;
  }
  if (State(_guest) == STATE_ATTACK)
  {
    if (WithinBox(_guest, BREAK_OFF_BOX))
    {
      SetState(_guest, STATE_TURN_AWAY);
      MoveObject(_guest);
      return;
    }
    GetVectorToPlayer(_guest);
    TurnToVector(_guest);
    TryFireLaserAtPlayer(_guest);
    regs.bx = HUNTER_MISSILE_ODDS;
    TryLaunchMissileAtPlayer(_guest);
    ComputeVelocity(_guest);
    AddWord(_guest, At(regs.di, SLOT_ROLL), HUNTER_SPIN);
    MoveObject(_guest);
    return;
  }
  if (State(_guest) == STATE_FORMATION)
  {
    regs.si = _guest.Word(At(regs.di, SLOT_TARGET));
    regs.dx = FORMATION_BOX;
    _guest.Call(GET_VECTOR_TO_OBJECT);
    if (_guest.Flag(FLAG_CARRY))
    {
      SetState(_guest, STATE_CLOSING);
      MoveObject(_guest);
      return;
    }
    TurnToVector(_guest);
    ComputeVelocity(_guest);
    MoveObject(_guest);
    return;
  }
  if (State(_guest) == STATE_TURN_AWAY)
  {
    HunterEvade(_guest);
    return;
  }
  // Closing on the player while farther than 5000 on some axis.
  if (WithinBox(_guest, CLOSING_BOX))
  {
    SetState(_guest, STATE_IDLE);
    MoveObject(_guest);
    return;
  }
  GetVectorToPlayer(_guest);
  TurnToVector(_guest);
  TryFireLaserAtPlayer(_guest);
  regs.bx = CLOSING_MISSILE_ODDS;
  TryLaunchMissileAtPlayer(_guest);
  ComputeVelocity(_guest);
  MoveObject(_guest);
}

void CheckSafeZoneHoldFire(Guest& _guest)
{
  if (_guest.Get(DS.thargoidInvasionActive) == 1)
  {
    _guest.SetFlag(FLAG_CARRY, false);
    return;
  }
  IsPoliceViper(_guest);
  if (_guest.Flag(FLAG_ZERO))
  {
    _guest.SetFlag(FLAG_CARRY, false);
    return;
  }
  _guest.Call(IN_SAFE_ZONE);
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

constexpr std::array ENTRIES = {
  NativeEntry{0x4A10, "UpdateObjectsAndSpawn", &UpdateObjectsAndSpawn, Machine::NativeContract{REGISTER_ALL, 0}},
  NativeEntry{0x4C0E, "ScaleSpawnOdds", &ScaleSpawnOdds, PRESERVES_ALL},
  NativeEntry{0x514B, "TurnTowardAngles", &TurnTowardAngles, Machine::NativeContract{REGISTER_CX | REGISTER_DX | REGISTER_BP, 0}},
  NativeEntry{0x5166, "ClampTurnStep", &ClampTurnStep, Machine::NativeContract{REGISTER_CX, 0}},
  NativeEntry{0x548F, "CountOtherHuntersOnScanner", &CountOtherHuntersOnScanner, Machine::NativeContract{REGISTER_CX | REGISTER_SI, 0}},
  NativeEntry{0x5594, "SkipInertObjectAi", &SkipInertObjectAi, PRESERVES_ALL},
  NativeEntry{0x5595, "UpdateStationAi", &UpdateStationAi,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI, 0}},
  NativeEntry{0x5681, "UpdateDriftingObjectAi", &UpdateDriftingObjectAi, PRESERVES_ALL},
  NativeEntry{0x57E8, "UpdateWolfAi", &UpdateWolfAi, CLOBBERS_MOST},
  NativeEntry{0x58DE, "UpdateHunterAi", &UpdateHunterAi, CLOBBERS_MOST},
  NativeEntry{0x5A10, "CheckSafeZoneHoldFire", &CheckSafeZoneHoldFire, Machine::NativeContract{0, FLAG_CARRY}},
};

} // namespace

std::span<const NativeEntry> AiEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
