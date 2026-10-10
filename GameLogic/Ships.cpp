#include "pch.h"

#include "Ships.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Maths.h"

#include <bit>
#include <initializer_list>

namespace Elite
{

namespace
{

using Machine::FLAG_AUXILIARY;
using Machine::FLAG_CARRY;
using Machine::FLAG_DIRECTION;
using Machine::FLAG_OVERFLOW;
using Machine::FLAG_PARITY;
using Machine::FLAG_SIGN;
using Machine::FLAG_ZERO;

// Routines outside this file, run through the original.
constexpr std::uint16_t ERASE_SCANNER_BLIP = 0x42D6;
constexpr std::uint16_t CONVERT_VECTOR_TO_ANGLES = 0x4F08;

constexpr std::uint16_t FIRST_SHIP_SLOT = 3; // firstShipSlot's index: the sun, the planet and the station come first

// spawnTemplates: the records the routines here start from.
constexpr std::size_t ESCAPE_POD_TEMPLATE = 1; // the first of the eight drifters'
constexpr std::size_t BARREL_TEMPLATE = 3;
constexpr std::size_t SHUTTLE_TEMPLATE = 7;
constexpr std::size_t COBRA_TEMPLATE = 9; // the first of the six traders'
constexpr std::size_t VIPER_TEMPLATE = 14;
constexpr std::size_t HUNTER_TEMPLATES = 15;
constexpr std::size_t KRAIT_TEMPLATE = 17;
constexpr std::size_t WOLF_TEMPLATES = 22;
constexpr std::size_t ASP_TEMPLATE = 23;
constexpr std::size_t THARGOID_TEMPLATE = 27;
constexpr std::size_t THARGON_TEMPLATE = 28;
constexpr std::uint16_t TEMPLATE_BYTES = 10;
// Where InitObjectFromTemplate puts bytes 1-9 of a record.
constexpr std::array<SlotByte, 9> TEMPLATE_FIELDS = {SlotByte::Speed,    SlotByte::TurnRate, SlotByte::Bounty,
                                                     SlotByte::Missiles, SlotByte::Cargo,    SlotByte::Fragments,
                                                     SlotByte::Energy,   SlotByte::Detail,   SlotByte::Range};

// The 24-bit position, by axis: the low words and the high bytes.
constexpr std::array<SlotWord, 3> POSITION_LOW = {SlotWord::X, SlotWord::Y, SlotWord::Z};
constexpr std::array<SlotByte, 3> POSITION_HIGH = {SlotByte::XHigh, SlotByte::YHigh, SlotByte::ZHigh};

constexpr std::uint8_t ABANDONED_CLASS = 0; // no behaviour
constexpr std::uint8_t BARREL_CLASS = 3;
constexpr std::uint8_t DRIFTER_CLASS = 3;
constexpr std::uint8_t TRADER_CLASS = 4;
constexpr std::uint8_t WOLF_CLASS = 5;
constexpr std::uint8_t HUNTER_CLASS = 6;
constexpr std::uint8_t HUNTER_CHOICE_DIVISOR = 37; // a random byte / 37: one of seven
constexpr std::uint8_t WOLF_CHOICE_DIVISOR = 52;   // a random byte / 52: one of five
constexpr std::uint8_t TRADER_CHOICE_DIVISOR = 43; // a random byte / 43: one of six
constexpr std::uint8_t DRIFTER_CHOICE_MASK = 7;    // one of eight
constexpr std::uint8_t WITCH_SPACE_WOLF = 5;       // the Thargoid's record among the wolves'
constexpr std::uint8_t HUNTER_AGGRESSION_MASK = 0x1F;
constexpr std::uint8_t WOLF_AGGRESSION_MASK = 0x3F;
constexpr std::uint8_t ANARCHY_AGGRESSION = 0x20;
constexpr std::uint8_t THARGON_COUNT_MASK = 3;
constexpr std::uint8_t FEWEST_THARGONS = 2;

constexpr std::uint8_t DRIFTER_TURN_RATE = 0x1E;
constexpr std::uint16_t POLICE_OWNER = 1; // +3Ah of a police Viper
constexpr std::uint8_t POLICE_AGGRESSION = 0x64;

// The mask mission's ships: the Asp (the mask ship) or, by the sign of a random word, one of two others, all from the Asp's record.
constexpr std::uint8_t MISSION_TYPE_IF_NEGATIVE = 0x12;
constexpr std::uint8_t MISSION_TYPE_IF_POSITIVE = 0x13;
constexpr std::uint8_t MISSION_AGGRESSION_MASK = 0x7F;
constexpr std::uint8_t MISSION_ENERGY = 0x96;
constexpr std::uint8_t MISSION_MISSILES = 6;
constexpr std::uint8_t MISSION_BOUNTY = 0xC8;
constexpr std::uint8_t INVADER_ENERGY = 0x32;
constexpr std::uint8_t INVADER_THARGONS = 8;

constexpr std::uint8_t FLAG_BLIP_DRAWN = 0x02;
constexpr std::uint16_t FIRST_EVICTED_SLOT = 4; // ReclaimShipSlot evicts one of slots 4-19

constexpr std::uint16_t SPAWN_DISTANCE = 10000;
constexpr std::uint16_t SPAWN_SCATTER_MASK = 0x7FF; // halved and signed by its low bit: +-1023

constexpr std::array<std::uint8_t, 2> SUN_OR_PLANET = {TYPE_SUN, TYPE_PLANET};
constexpr std::array<std::uint8_t, 1> PLANET = {TYPE_PLANET};
constexpr std::array<std::uint8_t, 4> DEBRIS_TYPES = {TYPE_PLATE, TYPE_BOULDER, TYPE_ASTEROID, TYPE_SPLINTER};
constexpr std::array<std::uint8_t, 1> THARGOID = {TYPE_THARGOID};
constexpr std::array<std::uint8_t, 1> THARGON = {TYPE_THARGON};

[[nodiscard]] std::uint16_t At(std::uint16_t _slot, int _field) noexcept
{
  return static_cast<std::uint16_t>(_slot + _field);
}

// The step of a string instruction: backwards when DF is set.
[[nodiscard]] std::uint16_t StringStep(bool _backward, std::uint16_t _bytes) noexcept
{
  return _backward ? Negate(_bytes) : _bytes;
}

// The flags CMP _a,_b leaves, byte-sized or word-sized: what the type tests return.
void SetCompareFlags(Guest& _guest, std::uint16_t _a, std::uint16_t _b, bool _word) noexcept
{
  const std::uint32_t mask = _word ? 0xFFFFu : 0xFFu;
  const std::uint32_t sign = _word ? 0x8000u : 0x80u;
  const std::uint32_t full = std::uint32_t{_a} - std::uint32_t{_b};
  const std::uint32_t result = full & mask;
  _guest.SetFlag(FLAG_CARRY, (full & (mask + 1)) != 0);
  _guest.SetFlag(FLAG_AUXILIARY, ((_a ^ _b ^ result) & 0x10u) != 0);
  _guest.SetFlag(FLAG_OVERFLOW, ((std::uint32_t{_a} ^ _b) & (_a ^ result) & sign) != 0);
  _guest.SetFlag(FLAG_SIGN, (result & sign) != 0);
  _guest.SetFlag(FLAG_ZERO, result == 0);
  _guest.SetFlag(FLAG_PARITY, (std::popcount(result & 0xFFu) & 1) == 0);
}

// MOV AL,[DI] / SHR AL,1 / AND AL,1Fh.
[[nodiscard]] std::uint8_t TypeOf(const ObjectSlot& _slot) noexcept
{
  return static_cast<std::uint8_t>((_slot.Get(SlotByte::Type) >> 1) & TYPE_MASK);
}

// What a type test built on CompareType finds: the type, and the last of the types it compared it with.
struct TypeMatch
{
  std::uint8_t type;
  std::uint8_t compared; // the one the type matched, or the last of them: the CMP whose flags the test leaves
};

// The type of _slot, compared with each of _types (at least one) until one matches, as the type tests chain their CMPs.
[[nodiscard]] TypeMatch CompareType(const ObjectSlot& _slot, std::span<const std::uint8_t> _types) noexcept
{
  const std::uint8_t type = TypeOf(_slot);
  std::uint8_t compared = type;
  for (const std::uint8_t candidate : _types)
  {
    compared = candidate;
    if (type == candidate)
    {
      break;
    }
  }
  return TypeMatch{type, compared};
}

// What PositionFitsWords finds.
struct PositionFit
{
  bool fits;
  std::uint8_t lastHigh; // the last high byte looked at, INC'd and DEC'd back, which leaves 0 for FFh: what the original leaves in AL
};

// The test IsObjectNear and IsObjectNearKeepBlip share: each high byte of _slot's 24-bit position is the sign extension of
// its low word, axis by axis until one is not.
[[nodiscard]] PositionFit PositionFitsWords(const ObjectSlot& _slot) noexcept
{
  std::uint8_t lastHigh = 0;
  for (std::size_t axis = 0; axis < POSITION_HIGH.size(); ++axis)
  {
    const std::uint8_t high = _slot.Get(POSITION_HIGH[axis]);
    const bool negative = (_slot.Get(POSITION_LOW[axis]) & 0x8000) != 0;
    lastHigh = high == 0xFF ? std::uint8_t{0} : high;
    const bool fits = high == 0xFF ? negative : high == 0 && !negative;
    if (!fits)
    {
      return PositionFit{false, lastHigh};
    }
  }
  return PositionFit{true, lastHigh};
}

// AND AX,7FFh / SHR AX,1 / NEG AX when the bit shifted out was set: a scatter of +-1023.
[[nodiscard]] std::uint16_t Scatter(std::uint16_t _random) noexcept
{
  const auto masked = static_cast<std::uint16_t>(_random & SPAWN_SCATTER_MASK);
  const auto halved = static_cast<std::uint16_t>(masked >> 1);
  return (masked & 1) != 0 ? Negate(halved) : halved;
}

// MOV BX,record / XOR AL,AL / CALL InitObjectFromTemplate / MOV [DI+33h],_class: the slot at DI from one fixed record.
void InitFromRecord(Guest& _guest, std::size_t _record, std::uint8_t _class)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = DS.spawnTemplates.At(_record);
  SetLow(regs.ax, 0);
  InitObjectFromTemplateEntry(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), _class);
}

// The start every spawned ship shares: record AL of the table at BX, placed at the spawn point and turned to the player.
void SpawnFromRecord(Guest& _guest)
{
  InitObjectFromTemplateEntry(_guest);
  PlaceAtSpawnPoint(_guest);
  FacePlayerWithRandomRoll(_guest);
}

} // namespace

void AddToCoordinate(ObjectSlot _slot, int _axis, std::int16_t _value)
{
  const auto axis = static_cast<std::size_t>(_axis);
  const auto value = static_cast<std::uint16_t>(_value);
  const std::uint32_t sum = std::uint32_t{_slot.Get(POSITION_LOW[axis])} + value;
  _slot.Set(POSITION_LOW[axis], static_cast<std::uint16_t>(sum));
  _slot.Set(POSITION_HIGH[axis], static_cast<std::uint8_t>(_slot.Get(POSITION_HIGH[axis]) + Low(SignWord(value)) + (sum >> 16)));
}

void ClearObjectSlot(GameState& _state, std::uint16_t _slot)
{
  // MOV [SI],CH while CX counts down from 40h: CH is 0 throughout.
  for (std::uint16_t index = 0; index < ObjectSlot::BYTES; ++index)
  {
    _state.SetByte(Offset(_slot, index), 0);
  }
}

void InitPoliceViper(Guest& _guest)
{
  InitFromRecord(_guest, VIPER_TEMPLATE, TRADER_CLASS);
  const std::uint16_t slot = _guest.Regs().di;
  _guest.SetWord(At(slot, SLOT_OWNER), POLICE_OWNER);
  _guest.SetByte(At(slot, SLOT_AGGRESSION), POLICE_AGGRESSION);
}

void InitAbandonedCobra(Guest& _guest)
{
  InitFromRecord(_guest, COBRA_TEMPLATE, ABANDONED_CLASS);
}

void InitEscapePod(Guest& _guest)
{
  InitFromRecord(_guest, ESCAPE_POD_TEMPLATE, DRIFTER_CLASS);
}

void InitShuttle(Guest& _guest)
{
  InitFromRecord(_guest, SHUTTLE_TEMPLATE, DRIFTER_CLASS);
}

void InitKraitHunter(Guest& _guest)
{
  InitFromRecord(_guest, KRAIT_TEMPLATE, HUNTER_CLASS);
}

void InitThargon(Guest& _guest)
{
  InitFromRecord(_guest, THARGON_TEMPLATE, WOLF_CLASS);
}

void SpawnRandomDrifter(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  NextRandomEntry(_guest);
  // ROR AX,1 / XOR AL,AH / AND AL,7.
  const auto rotated = static_cast<std::uint16_t>((regs.ax >> 1) | (regs.ax << 15));
  regs.ax = Join(High(rotated), static_cast<std::uint8_t>((Low(rotated) ^ High(rotated)) & DRIFTER_CHOICE_MASK));
  regs.bx = DS.spawnTemplates.At(ESCAPE_POD_TEMPLATE);
  SpawnFromRecord(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), DRIFTER_CLASS);
  _guest.SetByte(At(regs.di, SLOT_TURN_RATE), DRIFTER_TURN_RATE);
  ComputeVelocity(_guest);
}

void SpawnRandomTrader(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  NextRandomEntry(_guest);
  // XOR AH,AH / DIV BL: the quotient picks the record, and the remainder stays in AH.
  const std::uint8_t random = Low(regs.ax);
  regs.ax = Join(static_cast<std::uint8_t>(random % TRADER_CHOICE_DIVISOR), static_cast<std::uint8_t>(random / TRADER_CHOICE_DIVISOR));
  regs.bx = DS.spawnTemplates.At(COBRA_TEMPLATE);
  SpawnFromRecord(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), TRADER_CLASS);
  ComputeVelocity(_guest);
  IsViperTypeEntry(_guest);
  if (!_guest.Flag(FLAG_ZERO))
  {
    return;
  }
  // A Viper is police one time in two; then a word write of the legal status, which zeroes the bounty above it.
  NextRandomEntry(_guest);
  regs.ax = static_cast<std::uint16_t>(regs.ax & POLICE_OWNER);
  _guest.SetWord(At(regs.di, SLOT_OWNER), regs.ax);
  if (regs.ax == 0)
  {
    return;
  }
  SetLow(regs.ax, _guest.Get(DS.legalStatus));
  _guest.SetWord(At(regs.di, SLOT_AGGRESSION), regs.ax);
}

void SpawnMaskMissionShip(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  std::uint8_t type = TYPE_ASP;
  SetLow(regs.ax, type);
  if (!_guest.Flag(FLAG_CARRY))
  {
    NextRandomEntry(_guest);
    type = (High(regs.ax) & 0x80) != 0 ? MISSION_TYPE_IF_NEGATIVE : MISSION_TYPE_IF_POSITIVE;
    SetLow(regs.ax, type);
  }
  const std::uint16_t kept = regs.ax; // PUSH AX / POP AX round the record's set-up
  SetLow(regs.ax, 0);
  regs.bx = DS.spawnTemplates.At(ASP_TEMPLATE);
  SpawnFromRecord(_guest);
  // SHL AL,1 / INC AL: the type, active.
  regs.ax = WithLow(kept, static_cast<std::uint8_t>((type << 1) | SLOT_ACTIVE));
  _guest.SetByte(regs.di, Low(regs.ax));
  _guest.SetByte(At(regs.di, SLOT_CLASS), WOLF_CLASS);
  NextRandomEntry(_guest);
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & MISSION_AGGRESSION_MASK));
  _guest.SetByte(At(regs.di, SLOT_AGGRESSION), Low(regs.ax));
  _guest.SetByte(At(regs.di, SLOT_ENERGY), MISSION_ENERGY);
  _guest.SetByte(At(regs.di, SLOT_CARGO), 0);
  _guest.SetByte(At(regs.di, SLOT_MISSILES), MISSION_MISSILES);
  _guest.SetByte(At(regs.di, SLOT_BOUNTY), MISSION_BOUNTY);
}

void SpawnInvasionThargoid(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetLow(regs.ax, 0);
  regs.bx = DS.spawnTemplates.At(THARGOID_TEMPLATE);
  SpawnFromRecord(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), WOLF_CLASS);
  NextRandomEntry(_guest);
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & MISSION_AGGRESSION_MASK));
  _guest.SetByte(At(regs.di, SLOT_AGGRESSION), Low(regs.ax));
  _guest.SetByte(At(regs.di, SLOT_ENERGY), INVADER_ENERGY);
  _guest.SetByte(At(regs.di, SLOT_CARGO), 0);
  _guest.SetByte(At(regs.di, SLOT_MISSILES), MISSION_MISSILES);
  _guest.SetByte(At(regs.di, SLOT_THARGONS), INVADER_THARGONS);
}

void RandomizeOrientation(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  for (const std::uint16_t field : {SLOT_PITCH, SLOT_YAW, SLOT_ROLL})
  {
    NextRandomEntry(_guest);
    _guest.SetWord(At(regs.di, field), regs.ax);
  }
}

void ReclaimShipSlot(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.firstShipSlot.offset;
  regs.cx = static_cast<std::uint8_t>(_guest.Get(DS.objectSlotCount) - FIRST_SHIP_SLOT);
  do
  {
    if ((_guest.Byte(At(regs.si, SLOT_FLAGS)) & FLAG_BLIP_DRAWN) == 0)
    {
      return;
    }
    regs.si = At(regs.si, SLOT_BYTES);
    regs.cx = static_cast<std::uint16_t>(regs.cx - 1);
  } while (regs.cx != 0);
  // Every one has a blip: one of slots 4-19 at random (AND AH,0Fh / XOR AL,AL / SHR AX,1 twice), removed first.
  NextRandomEntry(_guest);
  regs.ax = static_cast<std::uint16_t>((High(regs.ax) & 0x0F) << 6);
  regs.si = static_cast<std::uint16_t>(DS.shipSlots.offset + FIRST_EVICTED_SLOT * SLOT_BYTES + regs.ax);
  const std::uint16_t slot = regs.si;
  regs.di = slot;
  RemoveObject(_guest);
  regs.si = slot;
}

void IsObjectNear(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const PositionFit fit = PositionFitsWords(ObjectSlot(_guest.State(), regs.di));
  SetLow(regs.ax, fit.lastHigh);
  if (fit.fits)
  {
    _guest.SetFlag(FLAG_CARRY, true);
    return;
  }
  _guest.Call(ERASE_SCANNER_BLIP);
  _guest.SetFlag(FLAG_CARRY, false);
}

void IsSunOrPlanet(Guest& _guest)
{
  const TypeMatch match = CompareType(ObjectSlot(_guest.State(), _guest.Regs().di), SUN_OR_PLANET);
  SetLow(_guest.Regs().ax, match.type);
  SetCompareFlags(_guest, match.type, match.compared, false);
}

void IsPlanet(Guest& _guest)
{
  const TypeMatch match = CompareType(ObjectSlot(_guest.State(), _guest.Regs().di), PLANET);
  SetLow(_guest.Regs().ax, match.type);
  SetCompareFlags(_guest, match.type, match.compared, false);
}

StationTest IsStation(const ObjectSlot& _slot)
{
  const std::uint8_t type = TypeOf(_slot);
  return StationTest{type, type == TYPE_DODO || type == TYPE_CORIOLIS, type == TYPE_DODO};
}

void IsObjectNearKeepBlip(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const PositionFit fit = PositionFitsWords(ObjectSlot(_guest.State(), regs.di));
  SetLow(regs.ax, fit.lastHigh);
  _guest.SetFlag(FLAG_CARRY, fit.fits);
}

void InitCargoBarrel(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = DS.spawnTemplates.At(BARREL_TEMPLATE);
  SetLow(regs.ax, 0);
  InitObjectFromTemplateEntry(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), BARREL_CLASS);
}

void SpawnRandomHunter(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  NextRandomEntry(_guest);
  // XOR AH,AH / DIV BL: the quotient picks the record, and the remainder stays in AH.
  const std::uint8_t random = Low(regs.ax);
  regs.ax = static_cast<std::uint16_t>(((random % HUNTER_CHOICE_DIVISOR) << 8) | (random / HUNTER_CHOICE_DIVISOR));
  regs.bx = DS.spawnTemplates.At(HUNTER_TEMPLATES);
  InitObjectFromTemplateEntry(_guest);
  PlaceAtSpawnPoint(_guest);
  FacePlayerWithRandomRoll(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), HUNTER_CLASS);
  NextRandomEntry(_guest);
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & HUNTER_AGGRESSION_MASK));
  _guest.SetByte(At(regs.di, SLOT_AGGRESSION), Low(regs.ax));
  ComputeVelocity(_guest);
}

void SpawnRandomWolf(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetLow(regs.ax, WITCH_SPACE_WOLF);
  if (_guest.Get(DS.witchspaceCountdown) == 0)
  {
    NextRandomEntry(_guest);
    const std::uint8_t random = Low(regs.ax);
    regs.ax = static_cast<std::uint16_t>(((random % WOLF_CHOICE_DIVISOR) << 8) | (random / WOLF_CHOICE_DIVISOR));
  }
  regs.bx = DS.spawnTemplates.At(WOLF_TEMPLATES);
  InitObjectFromTemplateEntry(_guest);
  PlaceAtSpawnPoint(_guest);
  FacePlayerWithRandomRoll(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), WOLF_CLASS);
  NextRandomEntry(_guest);
  auto aggression = static_cast<std::uint8_t>(Low(regs.ax) & WOLF_AGGRESSION_MASK);
  if (_guest.Get(DS.spawnGovernment) == 0)
  {
    aggression = static_cast<std::uint8_t>(aggression + ANARCHY_AGGRESSION);
  }
  SetLow(regs.ax, aggression);
  _guest.SetByte(At(regs.di, SLOT_AGGRESSION), aggression);
  ComputeVelocity(_guest);
  IsThargoidType(_guest);
  if (!_guest.Flag(FLAG_ZERO))
  {
    return;
  }
  NextRandomEntry(_guest);
  const auto thargons = static_cast<std::uint8_t>((((Low(regs.ax) ^ High(regs.ax)) >> 3) & THARGON_COUNT_MASK) + FEWEST_THARGONS);
  SetLow(regs.ax, thargons);
  _guest.SetByte(At(regs.di, SLOT_THARGONS), thargons);
}

void InitObjectFromTemplate(GameState& _state, ObjectSlot _slot, std::uint16_t _table, std::uint8_t _index)
{
  _slot.Set(SlotByte::State, 0);
  _slot.Set(SlotByte::Flags, 0);
  _slot.Set(SlotByte::Aggression, 0);
  _slot.Set(SlotByte::Scanned, 1);
  // BX += AL*2, then BX += AL*8.
  const std::uint16_t record = Offset(_table, static_cast<std::uint16_t>(_index * TEMPLATE_BYTES));
  // STC / RCL AL,1: the type, active.
  _slot.Set(SlotByte::Type, static_cast<std::uint8_t>((_state.Byte(record) << 1) | ObjectSlot::ACTIVE));
  for (std::size_t field = 0; field < TEMPLATE_FIELDS.size(); ++field)
  {
    _slot.Set(TEMPLATE_FIELDS[field], _state.Byte(Offset(record, static_cast<std::uint16_t>(field + 1))));
  }
}

void PlaceAtSpawnPoint(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  NextRandomEntry(_guest);
  regs.ax = Scatter(regs.ax);
  regs.bx = SPAWN_DISTANCE;
  RotateByStoredSinCosEntry(_guest, DS.rotationSinCos.At(7));
  regs.cx = regs.ax;
  NextRandomEntry(_guest);
  regs.ax = Scatter(regs.ax);
  RotateByStoredSinCosEntry(_guest, DS.rotationSinCos.At(6));
  // Each word with its sign extension (CWD) as the high byte: y, then x from CX, then z from BX.
  const std::uint16_t slot = regs.di;
  const std::array<std::uint16_t, 3> coordinates = {regs.ax, regs.cx, regs.bx};
  constexpr std::array<int, 3> AXES = {1, 0, 2};
  for (std::size_t index = 0; index < coordinates.size(); ++index)
  {
    regs.ax = coordinates[index];
    regs.dx = SignWord(regs.ax);
    _guest.SetWord(At(slot, SLOT_X + 2 * AXES[index]), regs.ax);
    _guest.SetByte(At(slot, SLOT_X_HIGH + AXES[index]), Low(regs.dx));
  }
}

void FacePlayerWithRandomRoll(Guest& _guest)
{
  FacePlayer(_guest);
  NextRandomEntry(_guest);
  Machine::Registers& regs = _guest.Regs();
  _guest.SetWord(At(regs.di, SLOT_ROLL), regs.ax);
}

Vector GetObjectPosition(const ObjectSlot& _slot)
{
  return Vector{static_cast<std::int16_t>(_slot.Get(SlotWord::X)), static_cast<std::int16_t>(_slot.Get(SlotWord::Y)),
                static_cast<std::int16_t>(_slot.Get(SlotWord::Z))};
}

void GetVectorToPlayer(Guest& _guest)
{
  GetObjectPositionEntry(_guest);
  Machine::Registers& regs = _guest.Regs();
  regs.ax = Negate(regs.ax);
  regs.bx = Negate(regs.bx);
  regs.cx = Negate(regs.cx);
}

void ComputeVelocity(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  regs.ax = _guest.Word(At(slot, SLOT_PITCH));
  SetSinCosEntry(_guest, DS.rotationSinCos.At(7));
  regs.ax = _guest.Word(At(slot, SLOT_YAW));
  SetSinCosEntry(_guest, DS.rotationSinCos.At(6));
  regs.bx = SignExtend(_guest.Byte(At(slot, SLOT_SPEED)));
  regs.ax = 0;
  RotateByStoredSinCosEntry(_guest, DS.rotationSinCos.At(6));
  _guest.SetByte(At(slot, SLOT_VELOCITY), Low(regs.ax));
  regs.ax = 0;
  RotateByStoredSinCosEntry(_guest, DS.rotationSinCos.At(7));
  _guest.SetByte(At(slot, SLOT_VELOCITY + 1), Low(regs.ax));
  _guest.SetByte(At(slot, SLOT_VELOCITY + 2), Low(regs.bx));
}

void MoveObject(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  for (int axis = 0; axis < 3; ++axis)
  {
    // CBW / CWD, then ADD the low word and ADC the high byte: a 24-bit add.
    regs.ax = SignExtend(_guest.Byte(At(slot, SLOT_VELOCITY + axis)));
    regs.dx = SignWord(regs.ax);
    const std::uint16_t low = At(slot, SLOT_X + 2 * axis);
    const std::uint32_t sum = std::uint32_t{_guest.Word(low)} + regs.ax;
    _guest.SetWord(low, static_cast<std::uint16_t>(sum));
    const std::uint16_t high = At(slot, SLOT_X_HIGH + axis);
    _guest.SetByte(high, static_cast<std::uint8_t>(_guest.Byte(high) + Low(regs.dx) + (sum >> 16)));
  }
  IsObjectNear(_guest);
  if (!_guest.Flag(FLAG_CARRY))
  {
    RemoveObject(_guest);
  }
}

void RemoveObject(Guest& _guest)
{
  const std::uint16_t slot = _guest.Regs().di;
  _guest.SetByte(slot, static_cast<std::uint8_t>(_guest.Byte(slot) & ~SLOT_ACTIVE));
  _guest.Call(ERASE_SCANNER_BLIP);
}

void FacePlayer(Guest& _guest)
{
  GetVectorToPlayer(_guest);
  _guest.Call(CONVERT_VECTOR_TO_ANGLES);
  Machine::Registers& regs = _guest.Regs();
  _guest.SetWord(At(regs.di, SLOT_PITCH), regs.ax);
  _guest.SetWord(At(regs.di, SLOT_YAW), regs.bx);
}

SlotSearch FindFreeShipSlot(GameState& _state)
{
  std::uint16_t slot = DS.firstShipSlot.offset;
  const auto slots = static_cast<std::uint8_t>(_state.Get(DS.objectSlotCount) - FIRST_SHIP_SLOT);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    if ((ObjectSlot(_state, slot).Get(SlotByte::Type) & ObjectSlot::ACTIVE) == 0)
    {
      return SlotSearch{true, slot};
    }
    slot = Offset(slot, ObjectSlot::BYTES);
  }
  return SlotSearch{false, slot};
}

void ClearAllObjects(GameState& _state, bool _backward)
{
  // MOV CH,shipSlotCount / XOR CL,CL / SHR CX,1 three times: 32 words a slot, cleared by REP STOSW into ES = DS.
  const std::uint16_t step = StringStep(_backward, 2);
  std::uint16_t at = DS.shipSlots.offset;
  for (auto words = static_cast<std::uint16_t>(_state.Get(DS.shipSlotCount) << 5); words != 0; --words)
  {
    _state.SetWord(at, 0);
    at = Offset(at, step);
  }
}

std::uint16_t FindDebrisSlot(GameState& _state)
{
  const std::uint8_t slots = _state.Get(DS.debrisSlotCount);
  std::uint16_t slot = DS.debrisSlots.offset;
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    if ((ObjectSlot(_state, slot).Get(SlotByte::Type) & ObjectSlot::ACTIVE) == 0)
    {
      return slot;
    }
    slot = Offset(slot, ObjectSlot::BYTES);
  }
  // None is free: the oldest, the last of equals. The first slot always passes the test (an age of at least 0), so the
  // original's BX, its first candidate, never comes back.
  std::uint16_t oldest = DS.debrisSlots.offset;
  std::uint8_t age = 0;
  slot = DS.debrisSlots.offset;
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    const std::uint8_t slotAge = ObjectSlot(_state, slot).Get(SlotByte::Age);
    if (slotAge >= age)
    {
      age = slotAge;
      oldest = slot;
    }
    slot = Offset(slot, ObjectSlot::BYTES);
  }
  return oldest;
}

void CopyObject(GameState& _state, std::uint16_t _source, std::uint16_t _destination, bool _backward)
{
  // REP MOVSW from DS:SI to ES:DI, ES = DS: 32 words, word by word as the 8088 does it.
  const std::uint16_t step = StringStep(_backward, 2);
  std::uint16_t source = _source;
  std::uint16_t destination = _destination;
  for (std::uint16_t words = ObjectSlot::BYTES / 2; words != 0; --words)
  {
    _state.SetWord(destination, _state.Word(source));
    source = Offset(source, step);
    destination = Offset(destination, step);
  }
}

void UpdateDebrisAi(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  const auto lifetime = static_cast<std::uint8_t>(_guest.Byte(At(slot, SLOT_LIFETIME)) - 1);
  _guest.SetByte(At(slot, SLOT_LIFETIME), lifetime);
  if (lifetime == 0)
  {
    _guest.SetByte(slot, static_cast<std::uint8_t>(_guest.Byte(slot) & ~SLOT_ACTIVE));
    return;
  }
  _guest.SetByte(At(slot, SLOT_AGE), static_cast<std::uint8_t>(_guest.Byte(At(slot, SLOT_AGE)) + 1));
  regs.ax = SignExtend(_guest.Byte(At(slot, SLOT_SPIN_ROLL)));
  _guest.SetWord(At(slot, SLOT_ROLL), static_cast<std::uint16_t>(_guest.Word(At(slot, SLOT_ROLL)) + regs.ax));
  regs.ax = SignExtend(_guest.Byte(At(slot, SLOT_SPIN_PITCH)));
  _guest.SetWord(At(slot, SLOT_PITCH), static_cast<std::uint16_t>(_guest.Word(At(slot, SLOT_PITCH)) + regs.ax));
  MoveObject(_guest);
}

void IsDebrisType(Guest& _guest)
{
  const TypeMatch match = CompareType(ObjectSlot(_guest.State(), _guest.Regs().di), DEBRIS_TYPES);
  SetLow(_guest.Regs().ax, match.type);
  SetCompareFlags(_guest, match.type, match.compared, false);
}

bool IsViperType(const ObjectSlot& _slot)
{
  return TypeOf(_slot) == TYPE_VIPER;
}

void IsPoliceViper(Guest& _guest)
{
  IsViperTypeEntry(_guest);
  if (!_guest.Flag(FLAG_ZERO))
  {
    return;
  }
  SetCompareFlags(_guest, _guest.Word(At(_guest.Regs().di, SLOT_OWNER)), 1, true);
}

void IsThargoidType(Guest& _guest)
{
  const TypeMatch match = CompareType(ObjectSlot(_guest.State(), _guest.Regs().di), THARGOID);
  SetLow(_guest.Regs().ax, match.type);
  SetCompareFlags(_guest, match.type, match.compared, false);
}

void IsThargonType(Guest& _guest)
{
  const TypeMatch match = CompareType(ObjectSlot(_guest.State(), _guest.Regs().di), THARGON);
  SetLow(_guest.Regs().ax, match.type);
  SetCompareFlags(_guest, match.type, match.compared, false);
}

// ── The entries of the routines de-assembled (ADR-012) ──

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;

constexpr Machine::NativeContract REMOVES{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_ES, 0};
constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX{REGISTER_AX | REGISTER_BX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_DX{REGISTER_AX | REGISTER_BX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX_BP{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DI{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0};
constexpr Machine::NativeContract NEAR_TEST{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, FLAG_CARRY};
constexpr Machine::NativeContract RETURNS_CARRY{0, FLAG_CARRY};
constexpr Machine::NativeContract STATION_TEST{0, FLAG_ZERO | FLAG_CARRY};
constexpr Machine::NativeContract TYPE_IN_AL{0, FLAG_ZERO};
constexpr Machine::NativeContract TYPE_CLOBBERS_AL{REGISTER_AX, FLAG_ZERO};

[[nodiscard]] ObjectSlot SlotAtDi(Guest& _guest) noexcept
{
  return ObjectSlot(_guest.State(), _guest.Regs().di);
}

} // namespace

void ClearObjectSlotEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.si;
  ClearObjectSlot(_guest.State(), slot);
  regs.di = slot;
  regs.si = Offset(slot, ObjectSlot::BYTES);
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

void IsStationEntry(Guest& _guest)
{
  const StationTest test = IsStation(SlotAtDi(_guest));
  SetLow(_guest.Regs().ax, test.type);
  _guest.SetFlag(FLAG_ZERO, test.station);
  _guest.SetFlag(FLAG_CARRY, test.dodo);
  _guest.Clobber(STATION_TEST);
}

void InitObjectFromTemplateEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  InitObjectFromTemplate(_guest.State(), SlotAtDi(_guest), regs.bx, Low(regs.ax));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void GetObjectPositionEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Vector position = GetObjectPosition(SlotAtDi(_guest));
  regs.ax = static_cast<std::uint16_t>(position.x);
  regs.bx = static_cast<std::uint16_t>(position.y);
  regs.cx = static_cast<std::uint16_t>(position.z);
  _guest.Clobber(PRESERVES_ALL);
}

void FindFreeShipSlotEntry(Guest& _guest)
{
  const SlotSearch search = FindFreeShipSlot(_guest.State());
  _guest.Regs().si = search.slot;
  _guest.SetFlag(FLAG_CARRY, search.found);
  _guest.Clobber(RETURNS_CARRY);
}

void ClearAllObjectsEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.es = regs.ds;
  ClearAllObjects(_guest.State(), _guest.Flag(FLAG_DIRECTION));
  _guest.Clobber(CLOBBERS_AX_CX_DI);
}

void FindDebrisSlotEntry(Guest& _guest)
{
  _guest.Regs().si = FindDebrisSlot(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void CopyObjectEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.es = regs.ds;
  CopyObject(_guest.State(), regs.si, regs.di, _guest.Flag(FLAG_DIRECTION));
  _guest.Clobber(PRESERVES_ALL);
}

void IsViperTypeEntry(Guest& _guest)
{
  // PUSH AX / POP AX round the test: only ZF is its result.
  _guest.SetFlag(FLAG_ZERO, IsViperType(SlotAtDi(_guest)));
  _guest.Clobber(TYPE_IN_AL);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x2FD8, "ClearObjectSlot", &ClearObjectSlotEntry, PRESERVES_ALL},
  NativeEntry{0x3B9A, "IsObjectNear", &IsObjectNear, NEAR_TEST},
  NativeEntry{0x3F2A, "IsSunOrPlanet", &IsSunOrPlanet, TYPE_IN_AL},
  NativeEntry{0x3F37, "IsPlanet", &IsPlanet, TYPE_IN_AL},
  NativeEntry{0x3F40, "IsStation", &IsStationEntry, STATION_TEST},
  NativeEntry{0x460E, "IsObjectNearKeepBlip", &IsObjectNearKeepBlip, Machine::NativeContract{REGISTER_AX, FLAG_CARRY}},
  NativeEntry{0x4C76, "InitPoliceViper", &InitPoliceViper, CLOBBERS_AX_BX},
  NativeEntry{0x4C99, "InitCargoBarrel", &InitCargoBarrel, CLOBBERS_AX_BX},
  NativeEntry{0x4CA6, "InitAbandonedCobra", &InitAbandonedCobra, CLOBBERS_AX_BX},
  NativeEntry{0x4CB3, "InitEscapePod", &InitEscapePod, CLOBBERS_AX_BX},
  NativeEntry{0x4CC0, "InitShuttle", &InitShuttle, CLOBBERS_AX_BX},
  NativeEntry{0x4CCD, "InitKraitHunter", &InitKraitHunter, CLOBBERS_AX_BX},
  NativeEntry{0x4CDA, "InitThargon", &InitThargon, CLOBBERS_AX_BX},
  NativeEntry{0x4CE7, "SpawnRandomDrifter", &SpawnRandomDrifter, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4D08, "SpawnRandomTrader", &SpawnRandomTrader, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4D3B, "SpawnRandomHunter", &SpawnRandomHunter, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4D60, "SpawnRandomWolf", &SpawnRandomWolf, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4DAE, "SpawnMaskMissionShip", &SpawnMaskMissionShip, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4DF0, "SpawnInvasionThargoid", &SpawnInvasionThargoid, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4E1B, "InitObjectFromTemplate", &InitObjectFromTemplateEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4E75, "PlaceAtSpawnPoint", &PlaceAtSpawnPoint, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x4EC5, "FacePlayerWithRandomRoll", &FacePlayerWithRandomRoll, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4EF4, "GetObjectPosition", &GetObjectPositionEntry, PRESERVES_ALL},
  NativeEntry{0x4EFE, "GetVectorToPlayer", &GetVectorToPlayer, PRESERVES_ALL},
  NativeEntry{0x4F35, "RandomizeOrientation", &RandomizeOrientation, CLOBBERS_AX},
  NativeEntry{0x4F48, "ComputeVelocity", &ComputeVelocity, CLOBBERS_AX_BX_DX},
  NativeEntry{0x4F6E, "MoveObject", &MoveObject, REMOVES},
  NativeEntry{0x4F98, "RemoveObject", &RemoveObject, REMOVES},
  NativeEntry{0x513E, "FacePlayer", &FacePlayer, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x51E0, "FindFreeShipSlot", &FindFreeShipSlotEntry, RETURNS_CARRY},
  NativeEntry{0x51FD, "ReclaimShipSlot", &ReclaimShipSlot, REMOVES},
  NativeEntry{0x52B2, "ClearAllObjects", &ClearAllObjectsEntry, CLOBBERS_AX_CX_DI},
  NativeEntry{0x52EC, "FindDebrisSlot", &FindDebrisSlotEntry, PRESERVES_ALL},
  NativeEntry{0x5320, "CopyObject", &CopyObjectEntry, PRESERVES_ALL},
  NativeEntry{0x5330, "UpdateDebrisAi", &UpdateDebrisAi, REMOVES},
  NativeEntry{0x53FE, "IsDebrisType", &IsDebrisType, TYPE_CLOBBERS_AL},
  NativeEntry{0x5413, "IsViperType", &IsViperTypeEntry, TYPE_IN_AL},
  NativeEntry{0x541E, "IsPoliceViper", &IsPoliceViper, TYPE_IN_AL},
  NativeEntry{0x5428, "IsThargoidType", &IsThargoidType, TYPE_CLOBBERS_AL},
  NativeEntry{0x5431, "IsThargonType", &IsThargonType, TYPE_CLOBBERS_AL},
};

} // namespace

std::span<const NativeEntry> ShipsEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
