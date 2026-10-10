#include "pch.h"

#include "Ships.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Flight.h"
#include "Maths.h"

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_DIRECTION;
using Machine::FLAG_ZERO;

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
// The velocity, by axis: signed bytes.
constexpr std::array<SlotByte, 3> VELOCITY = {SlotByte::VelocityX, SlotByte::VelocityY, SlotByte::VelocityZ};

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

// The heading's three words, in the order RandomizeOrientation writes them.
constexpr std::array<SlotWord, 3> ORIENTATION = {SlotWord::Pitch, SlotWord::Yaw, SlotWord::Roll};

// The step of a string instruction: backwards when DF is set.
[[nodiscard]] std::uint16_t StringStep(bool _backward, std::uint16_t _bytes) noexcept
{
  return _backward ? Negate(_bytes) : _bytes;
}

// MOV AL,[DI] / SHR AL,1 / AND AL,1Fh.
[[nodiscard]] std::uint8_t TypeOf(const ObjectSlot& _slot) noexcept
{
  return static_cast<std::uint8_t>((_slot.Get(SlotByte::Type) >> 1) & TYPE_MASK);
}

// CWD / MOV [slot+4+2*axis],AX / MOV [slot+1+axis],DL: _value as the 24-bit coordinate _axis (0 x, 1 y, 2 z) of _slot, the low
// word, then its sign as the high byte.
void SetCoordinate(ObjectSlot _slot, std::size_t _axis, std::int16_t _value)
{
  const auto value = static_cast<std::uint16_t>(_value);
  _slot.Set(POSITION_LOW[_axis], value);
  _slot.Set(POSITION_HIGH[_axis], Low(SignWord(value)));
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

// AND AH,0Fh / XOR AL,AL / SHR AX,1 twice: ReclaimShipSlot's random word made the offset of one of 16 slots from slot 4.
[[nodiscard]] std::uint16_t EvictedSlotBytes(std::uint16_t _random) noexcept
{
  return static_cast<std::uint16_t>((High(_random) & 0x0F) << 6);
}

// MOV BX,record / XOR AL,AL / CALL InitObjectFromTemplate / MOV [DI+33h],_class: _slot from one fixed record of spawnTemplates,
// of behaviour class _class.
void InitFromRecord(GameState& _state, ObjectSlot _slot, std::size_t _record, std::uint8_t _class)
{
  InitObjectFromTemplate(_state, _slot, DS.spawnTemplates.At(_record), 0);
  _slot.Set(SlotByte::Class, _class);
}

// The start every spawned ship shares: _slot from record _index of the table at _table, placed at the spawn point and turned to
// the player. Returns what FacePlayerWithRandomRoll gave it.
RandomRollFacing SpawnFromRecord(GameState& _state, ObjectSlot _slot, std::uint16_t _table, std::uint8_t _index)
{
  InitObjectFromTemplate(_state, _slot, _table, _index);
  PlaceAtSpawnPoint(_state, _slot);
  return FacePlayerWithRandomRoll(_state, _slot);
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

void InitPoliceViper(GameState& _state, ObjectSlot _slot)
{
  InitFromRecord(_state, _slot, VIPER_TEMPLATE, TRADER_CLASS);
  _slot.Set(SlotWord::Owner, POLICE_OWNER);
  _slot.Set(SlotByte::Aggression, POLICE_AGGRESSION);
}

void InitAbandonedCobra(GameState& _state, ObjectSlot _slot)
{
  InitFromRecord(_state, _slot, COBRA_TEMPLATE, ABANDONED_CLASS);
}

void InitEscapePod(GameState& _state, ObjectSlot _slot)
{
  InitFromRecord(_state, _slot, ESCAPE_POD_TEMPLATE, DRIFTER_CLASS);
}

void InitShuttle(GameState& _state, ObjectSlot _slot)
{
  InitFromRecord(_state, _slot, SHUTTLE_TEMPLATE, DRIFTER_CLASS);
}

void InitKraitHunter(GameState& _state, ObjectSlot _slot)
{
  InitFromRecord(_state, _slot, KRAIT_TEMPLATE, HUNTER_CLASS);
}

void InitThargon(GameState& _state, ObjectSlot _slot)
{
  InitFromRecord(_state, _slot, THARGON_TEMPLATE, WOLF_CLASS);
}

void SpawnRandomDrifter(GameState& _state, ObjectSlot _slot)
{
  // ROR AX,1 / XOR AL,AH / AND AL,7.
  const std::uint16_t random = NextRandom(_state);
  const auto rotated = static_cast<std::uint16_t>((random >> 1) | (random << 15));
  const auto record = static_cast<std::uint8_t>((Low(rotated) ^ High(rotated)) & DRIFTER_CHOICE_MASK);
  (void)SpawnFromRecord(_state, _slot, DS.spawnTemplates.At(ESCAPE_POD_TEMPLATE), record);
  _slot.Set(SlotByte::Class, DRIFTER_CLASS);
  _slot.Set(SlotByte::TurnRate, DRIFTER_TURN_RATE);
  (void)ComputeVelocity(_state, _slot);
}

RandomRollFacing SpawnRandomTrader(GameState& _state, ObjectSlot _slot)
{
  // XOR AH,AH / MOV BL,2Bh / DIV BL: the quotient of a random byte picks the record.
  const auto record = static_cast<std::uint8_t>(Low(NextRandom(_state)) / TRADER_CHOICE_DIVISOR);
  const RandomRollFacing facing = SpawnFromRecord(_state, _slot, DS.spawnTemplates.At(COBRA_TEMPLATE), record);
  _slot.Set(SlotByte::Class, TRADER_CLASS);
  (void)ComputeVelocity(_state, _slot);
  if (!IsViperType(_slot))
  {
    return facing;
  }
  // A Viper is police one time in two: AND AX,1 written as a word. Then MOV AL,legalStatus over the 0 of AH, written as a word,
  // which zeroes the bounty above the aggression.
  const auto police = static_cast<std::uint16_t>(NextRandom(_state) & POLICE_OWNER);
  _slot.Set(SlotWord::Owner, police);
  if (police != 0)
  {
    _slot.Set(SlotWord::Aggression, _state.Get(DS.legalStatus));
  }
  return facing;
}

void SpawnMaskMissionShip(GameState& _state, ObjectSlot _slot, bool _maskShip)
{
  // MOV AL,18h, or, by the sign of a random word, 12h or 13h; PUSH AX and POP AX keep it round the record's set-up.
  std::uint8_t type = TYPE_ASP;
  if (!_maskShip)
  {
    type = (NextRandom(_state) & 0x8000) != 0 ? MISSION_TYPE_IF_NEGATIVE : MISSION_TYPE_IF_POSITIVE;
  }
  (void)SpawnFromRecord(_state, _slot, DS.spawnTemplates.At(ASP_TEMPLATE), 0);
  // SHL AL,1 / INC AL: the type, active.
  _slot.Set(SlotByte::Type, static_cast<std::uint8_t>((type << 1) | ObjectSlot::ACTIVE));
  _slot.Set(SlotByte::Class, WOLF_CLASS);
  _slot.Set(SlotByte::Aggression, static_cast<std::uint8_t>(Low(NextRandom(_state)) & MISSION_AGGRESSION_MASK));
  _slot.Set(SlotByte::Energy, MISSION_ENERGY);
  _slot.Set(SlotByte::Cargo, 0);
  _slot.Set(SlotByte::Missiles, MISSION_MISSILES);
  _slot.Set(SlotByte::Bounty, MISSION_BOUNTY);
}

void SpawnInvasionThargoid(GameState& _state, ObjectSlot _slot)
{
  (void)SpawnFromRecord(_state, _slot, DS.spawnTemplates.At(THARGOID_TEMPLATE), 0);
  _slot.Set(SlotByte::Class, WOLF_CLASS);
  _slot.Set(SlotByte::Aggression, static_cast<std::uint8_t>(Low(NextRandom(_state)) & MISSION_AGGRESSION_MASK));
  _slot.Set(SlotByte::Energy, INVADER_ENERGY);
  _slot.Set(SlotByte::Cargo, 0);
  _slot.Set(SlotByte::Missiles, MISSION_MISSILES);
  _slot.Set(SlotByte::Thargons, INVADER_THARGONS);
}

void RandomizeOrientation(GameState& _state, ObjectSlot _slot)
{
  for (const SlotWord field : ORIENTATION)
  {
    _slot.Set(field, NextRandom(_state));
  }
}

ReclaimedSlot ReclaimShipSlot(GameState& _state)
{
  // SUB CL,3 / XOR CH,CH, then LOOP: a count of 0 runs 65,536 times.
  std::uint16_t slot = DS.firstShipSlot.offset;
  const auto slots = static_cast<std::uint8_t>(_state.Get(DS.objectSlotCount) - FIRST_SHIP_SLOT);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    if ((ObjectSlot(_state, slot).Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) == 0)
    {
      return ReclaimedSlot{slot, false, std::nullopt};
    }
    slot = Offset(slot, ObjectSlot::BYTES);
  }
  // Every one has a blip: one of slots 4-19 at random (AND AH,0Fh / XOR AL,AL / SHR AX,1 twice), removed first.
  const auto evicted =
    static_cast<std::uint16_t>(DS.shipSlots.offset + FIRST_EVICTED_SLOT * ObjectSlot::BYTES + EvictedSlotBytes(NextRandom(_state)));
  return ReclaimedSlot{evicted, true, RemoveObject(_state, ObjectSlot(_state, evicted))};
}

NearTest IsObjectNear(GameState& _state, ObjectSlot _slot)
{
  const PositionFit fit = PositionFitsWords(_slot);
  if (fit.fits)
  {
    return NearTest{true, std::nullopt, fit.lastHigh};
  }
  return NearTest{false, EraseScannerBlip(_state, _slot), fit.lastHigh};
}

bool IsSunOrPlanet(const ObjectSlot& _slot)
{
  const std::uint8_t type = TypeOf(_slot);
  return type == TYPE_SUN || type == TYPE_PLANET;
}

bool IsPlanet(const ObjectSlot& _slot)
{
  return TypeOf(_slot) == TYPE_PLANET;
}

StationTest IsStation(const ObjectSlot& _slot)
{
  const std::uint8_t type = TypeOf(_slot);
  return StationTest{type, type == TYPE_DODO || type == TYPE_CORIOLIS, type == TYPE_DODO};
}

bool IsObjectNearKeepBlip(const ObjectSlot& _slot)
{
  return PositionFitsWords(_slot).fits;
}

void InitCargoBarrel(GameState& _state, ObjectSlot _slot)
{
  InitFromRecord(_state, _slot, BARREL_TEMPLATE, BARREL_CLASS);
}

void SpawnRandomHunter(GameState& _state, ObjectSlot _slot)
{
  // XOR AH,AH / MOV BL,25h / DIV BL: the quotient of a random byte picks the record.
  const auto record = static_cast<std::uint8_t>(Low(NextRandom(_state)) / HUNTER_CHOICE_DIVISOR);
  (void)SpawnFromRecord(_state, _slot, DS.spawnTemplates.At(HUNTER_TEMPLATES), record);
  _slot.Set(SlotByte::Class, HUNTER_CLASS);
  _slot.Set(SlotByte::Aggression, static_cast<std::uint8_t>(Low(NextRandom(_state)) & HUNTER_AGGRESSION_MASK));
  (void)ComputeVelocity(_state, _slot);
}

void SpawnRandomWolf(GameState& _state, ObjectSlot _slot)
{
  // MOV AL,5, or out of witch space XOR AH,AH / MOV BL,34h / DIV BL: the quotient of a random byte picks the record.
  std::uint8_t record = WITCH_SPACE_WOLF;
  if (_state.Get(DS.witchspaceCountdown) == 0)
  {
    record = static_cast<std::uint8_t>(Low(NextRandom(_state)) / WOLF_CHOICE_DIVISOR);
  }
  (void)SpawnFromRecord(_state, _slot, DS.spawnTemplates.At(WOLF_TEMPLATES), record);
  _slot.Set(SlotByte::Class, WOLF_CLASS);
  auto aggression = static_cast<std::uint8_t>(Low(NextRandom(_state)) & WOLF_AGGRESSION_MASK);
  if (_state.Get(DS.spawnGovernment) == 0)
  {
    aggression = static_cast<std::uint8_t>(aggression + ANARCHY_AGGRESSION);
  }
  _slot.Set(SlotByte::Aggression, aggression);
  (void)ComputeVelocity(_state, _slot);
  if (!IsThargoidType(_slot))
  {
    return;
  }
  // XOR AL,AH / SHR AL,1 three times / AND AL,3 / ADD AL,2: 2 to 5 Thargons.
  const std::uint16_t random = NextRandom(_state);
  _slot.Set(SlotByte::Thargons, static_cast<std::uint8_t>((((Low(random) ^ High(random)) >> 3) & THARGON_COUNT_MASK) + FEWEST_THARGONS));
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

void PlaceAtSpawnPoint(GameState& _state, ObjectSlot _slot)
{
  // (scatter, 10000) by rotation pair 7, then (another scatter, what that left of 10000) by pair 6.
  const auto scatter = [&_state] { return static_cast<std::int16_t>(Scatter(NextRandom(_state))); };
  const Pair first = RotateByStoredSinCos(_state, 7, Pair{scatter(), static_cast<std::int16_t>(SPAWN_DISTANCE)});
  const Pair second = RotateByStoredSinCos(_state, 6, Pair{scatter(), first.second});
  // y, then x from the first rotation, then z.
  SetCoordinate(_slot, 1, second.first);
  SetCoordinate(_slot, 0, first.first);
  SetCoordinate(_slot, 2, second.second);
}

RandomRollFacing FacePlayerWithRandomRoll(GameState& _state, ObjectSlot _slot)
{
  const Angles heading = FacePlayer(_state, _slot);
  const std::uint16_t roll = NextRandom(_state);
  _slot.Set(SlotWord::Roll, roll);
  return RandomRollFacing{heading, roll};
}

Vector GetObjectPosition(const ObjectSlot& _slot)
{
  return Vector{static_cast<std::int16_t>(_slot.Get(SlotWord::X)), static_cast<std::int16_t>(_slot.Get(SlotWord::Y)),
                static_cast<std::int16_t>(_slot.Get(SlotWord::Z))};
}

Vector GetVectorToPlayer(const ObjectSlot& _slot)
{
  const Vector position = GetObjectPosition(_slot);
  const auto negated = [](std::int16_t _value) { return static_cast<std::int16_t>(Negate(static_cast<std::uint16_t>(_value))); };
  return Vector{negated(position.x), negated(position.y), negated(position.z)};
}

Vector ComputeVelocity(GameState& _state, ObjectSlot _slot)
{
  (void)SetSinCos(_state, 7, _slot.Get(SlotWord::Pitch));
  (void)SetSinCos(_state, 6, _slot.Get(SlotWord::Yaw));
  // (0, speed) by pair 6, then (0, what that left) by pair 7: x, then y and z.
  const auto speed = static_cast<std::int16_t>(SignExtend(_slot.Get(SlotByte::Speed))); // CBW
  const Pair first = RotateByStoredSinCos(_state, 6, Pair{0, speed});
  _slot.Set(SlotByte::VelocityX, Low(static_cast<std::uint16_t>(first.first)));
  const Pair second = RotateByStoredSinCos(_state, 7, Pair{0, first.second});
  _slot.Set(SlotByte::VelocityY, Low(static_cast<std::uint16_t>(second.first)));
  _slot.Set(SlotByte::VelocityZ, Low(static_cast<std::uint16_t>(second.second)));
  return Vector{first.first, second.first, second.second};
}

MovedObject MoveObject(GameState& _state, ObjectSlot _slot)
{
  // MOV AL,[DI+19h+axis] / CBW / CWD, then ADD on the low word and ADC on the high byte: x, then y and z.
  for (std::size_t axis = 0; axis < VELOCITY.size(); ++axis)
  {
    AddToCoordinate(_slot, static_cast<int>(axis), static_cast<std::int16_t>(SignExtend(_slot.Get(VELOCITY[axis]))));
  }
  const NearTest near = IsObjectNear(_state, _slot);
  if (near.nearby)
  {
    return MovedObject{near, std::nullopt};
  }
  return MovedObject{near, RemoveObject(_state, _slot)};
}

std::optional<DashboardPixel> RemoveObject(GameState& _state, ObjectSlot _slot)
{
  _slot.Set(SlotByte::Type, static_cast<std::uint8_t>(_slot.Get(SlotByte::Type) & ~ObjectSlot::ACTIVE));
  return EraseScannerBlip(_state, _slot);
}

Angles FacePlayer(GameState& _state, ObjectSlot _slot)
{
  const Angles heading = ConvertVectorToAngles(_state, GetVectorToPlayer(_slot));
  _slot.Set(SlotWord::Pitch, heading.first);
  _slot.Set(SlotWord::Yaw, heading.second);
  return heading;
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

std::optional<MovedObject> UpdateDebrisAi(GameState& _state, ObjectSlot _slot)
{
  const auto lifetime = static_cast<std::uint8_t>(_slot.Get(SlotByte::Lifetime) - 1);
  _slot.Set(SlotByte::Lifetime, lifetime);
  if (lifetime == 0)
  {
    _slot.Set(SlotByte::Type, static_cast<std::uint8_t>(_slot.Get(SlotByte::Type) & ~ObjectSlot::ACTIVE));
    return std::nullopt;
  }
  _slot.Set(SlotByte::Age, static_cast<std::uint8_t>(_slot.Get(SlotByte::Age) + 1));
  // MOV AL,[DI+26h] / CBW / ADD [DI+0Eh],AX, then the same from +27h into the pitch.
  _slot.Set(SlotWord::Roll, Offset(_slot.Get(SlotWord::Roll), SignExtend(_slot.Get(SlotByte::SpinRoll))));
  _slot.Set(SlotWord::Pitch, Offset(_slot.Get(SlotWord::Pitch), SignExtend(_slot.Get(SlotByte::SpinPitch))));
  return MoveObject(_state, _slot);
}

bool IsDebrisType(const ObjectSlot& _slot)
{
  const std::uint8_t type = TypeOf(_slot);
  return type == TYPE_PLATE || type == TYPE_BOULDER || type == TYPE_ASTEROID || type == TYPE_SPLINTER;
}

bool IsViperType(const ObjectSlot& _slot)
{
  return TypeOf(_slot) == TYPE_VIPER;
}

bool IsPoliceViper(const ObjectSlot& _slot)
{
  return IsViperType(_slot) && _slot.Get(SlotWord::Owner) == POLICE_OWNER;
}

bool IsThargoidType(const ObjectSlot& _slot)
{
  return TypeOf(_slot) == TYPE_THARGOID;
}

bool IsThargonType(const ObjectSlot& _slot)
{
  return TypeOf(_slot) == TYPE_THARGON;
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
// MoveObject's: every register as the original leaves it (MoveObjectEntry).
constexpr Machine::NativeContract MOVES{0, 0};
constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX{REGISTER_AX | REGISTER_BX, 0};
// ComputeVelocity: the BX it leaves is compared (ComputeVelocityEntry).
constexpr Machine::NativeContract CLOBBERS_AX_DX{REGISTER_AX | REGISTER_DX, 0};
// PlaceAtSpawnPoint's, and FacePlayer's, FacePlayerWithRandomRoll's and SpawnRandomTrader's, whose BP is compared
// (FacePlayerEntry, SpawnRandomTraderEntry).
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX_BP{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_DI{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0};
// IsObjectNear's: every register as the original leaves it, from EraseScannerBlip once it erases a blip (IsObjectNearEntry).
constexpr Machine::NativeContract NEAR_TEST{0, FLAG_CARRY};
// RemoveObject's: the AX, BX, DX and ES EraseScannerBlip leaves once it erases a blip are compared (RemoveObjectEntry); nothing
// reads its CX.
constexpr Machine::NativeContract REMOVES_OBJECT{REGISTER_CX, 0};
constexpr Machine::NativeContract KEEP_BLIP_TEST{REGISTER_AX, FLAG_CARRY};
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

void IsObjectNearOut(Guest& _guest, const ObjectSlot& _slot, const NearTest& _test)
{
  // The position the test read is unchanged: EraseScannerBlip writes only the blip and the flags.
  SetLow(_guest.Regs().ax, PositionFitsWords(_slot).lastHigh);
  EraseScannerBlipOut(_guest, _slot, _test.erasedBlip);
  _guest.SetFlag(FLAG_CARRY, _test.nearby);
}

void IsObjectNearEntry(Guest& _guest)
{
  const ObjectSlot slot = SlotAtDi(_guest);
  // AL, the last high byte the test looked at, INC'd and DEC'd back, and the registers EraseScannerBlip leaves once it erases a
  // blip. The contract compares them all: UpdateSafeZone and UpdateDashboard go on with AL, IsMassLocked with AH, BX and DX,
  // EngageJumpDrive and CheckShipInRange with what EraseScannerBlip leaves, and UpdateDriftingObjectAi with CX and DX.
  IsObjectNearOut(_guest, slot, IsObjectNear(_guest.State(), slot));
  _guest.Clobber(NEAR_TEST);
}

void IsSunOrPlanetEntry(Guest& _guest)
{
  const ObjectSlot slot = SlotAtDi(_guest);
  // The original leaves the type in AL, which the contract compares.
  SetLow(_guest.Regs().ax, TypeOf(slot));
  _guest.SetFlag(FLAG_ZERO, IsSunOrPlanet(slot));
  _guest.Clobber(TYPE_IN_AL);
}

void IsPlanetEntry(Guest& _guest)
{
  const ObjectSlot slot = SlotAtDi(_guest);
  // The original leaves the type in AL, which the contract compares.
  SetLow(_guest.Regs().ax, TypeOf(slot));
  _guest.SetFlag(FLAG_ZERO, IsPlanet(slot));
  _guest.Clobber(TYPE_IN_AL);
}

void IsStationEntry(Guest& _guest)
{
  const StationTest test = IsStation(SlotAtDi(_guest));
  SetLow(_guest.Regs().ax, test.type);
  _guest.SetFlag(FLAG_ZERO, test.station);
  _guest.SetFlag(FLAG_CARRY, test.dodo);
  _guest.Clobber(STATION_TEST);
}

void IsObjectNearKeepBlipEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_CARRY, IsObjectNearKeepBlip(SlotAtDi(_guest)));
  _guest.Clobber(KEEP_BLIP_TEST);
}

void InitPoliceViperEntry(Guest& _guest)
{
  InitPoliceViper(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void InitCargoBarrelEntry(Guest& _guest)
{
  InitCargoBarrel(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void InitAbandonedCobraEntry(Guest& _guest)
{
  InitAbandonedCobra(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void InitEscapePodEntry(Guest& _guest)
{
  InitEscapePod(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void InitShuttleEntry(Guest& _guest)
{
  InitShuttle(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void InitKraitHunterEntry(Guest& _guest)
{
  InitKraitHunter(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void InitThargonEntry(Guest& _guest)
{
  InitThargon(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void SpawnRandomDrifterEntry(Guest& _guest)
{
  SpawnRandomDrifter(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX_BP);
}

void SpawnRandomTraderEntry(Guest& _guest)
{
  // The pitch ConvertVectorToAngles leaves in BP (FacePlayerEntry), which UpdateStationAi's contract compares after it.
  _guest.Regs().bp = SpawnRandomTrader(_guest.State(), SlotAtDi(_guest)).heading.first;
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX);
}

void SpawnRandomHunterEntry(Guest& _guest)
{
  SpawnRandomHunter(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX_BP);
}

void SpawnRandomWolfEntry(Guest& _guest)
{
  SpawnRandomWolf(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX_BP);
}

void SpawnMaskMissionShipEntry(Guest& _guest)
{
  SpawnMaskMissionShip(_guest.State(), SlotAtDi(_guest), _guest.Flag(FLAG_CARRY));
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX_BP);
}

void SpawnInvasionThargoidEntry(Guest& _guest)
{
  SpawnInvasionThargoid(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX_BP);
}

void InitObjectFromTemplateEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  InitObjectFromTemplate(_guest.State(), SlotAtDi(_guest), regs.bx, Low(regs.ax));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void PlaceAtSpawnPointEntry(Guest& _guest)
{
  PlaceAtSpawnPoint(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX);
}

void FacePlayerWithRandomRollEntry(Guest& _guest)
{
  // NextRandom's word in AX, and the pitch ConvertVectorToAngles leaves in BP (FacePlayerEntry).
  Machine::Registers& regs = _guest.Regs();
  const RandomRollFacing facing = FacePlayerWithRandomRoll(_guest.State(), SlotAtDi(_guest));
  regs.ax = facing.roll;
  regs.bp = facing.heading.first;
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX);
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

void GetVectorToPlayerEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const Vector vector = GetVectorToPlayer(SlotAtDi(_guest));
  regs.ax = static_cast<std::uint16_t>(vector.x);
  regs.bx = static_cast<std::uint16_t>(vector.y);
  regs.cx = static_cast<std::uint16_t>(vector.z);
  _guest.Clobber(PRESERVES_ALL);
}

void RandomizeOrientationEntry(Guest& _guest)
{
  RandomizeOrientation(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(CLOBBERS_AX);
}

void ComputeVelocityEntry(Guest& _guest)
{
  const Vector velocity = ComputeVelocity(_guest.State(), SlotAtDi(_guest));
  // The original leaves the z word in BX, and UpdateMissileAi's contract compares BX after it.
  _guest.Regs().bx = static_cast<std::uint16_t>(velocity.z);
  _guest.Clobber(CLOBBERS_AX_DX);
}

void MoveObjectOut(Guest& _guest, const ObjectSlot& _slot, const MovedObject& _moved)
{
  // The last axis's CBW and CWD leave AX and DX the z velocity, sign-extended; then IsObjectNear's AL, and the registers it
  // leaves once it erased a blip; then RemoveObject's, once that erased one.
  Machine::Registers& regs = _guest.Regs();
  regs.ax = SignExtend(_slot.Get(SlotByte::VelocityZ));
  regs.dx = SignWord(regs.ax);
  IsObjectNearOut(_guest, _slot, _moved.near);
  EraseScannerBlipOut(_guest, _slot, _moved.removedBlip);
}

void MoveObjectEntry(Guest& _guest)
{
  // What the original leaves, which the contract compares: UpdateDriftingObjectAi's and UpdateMissileAi's contracts compare
  // every register after it.
  const ObjectSlot slot = SlotAtDi(_guest);
  MoveObjectOut(_guest, slot, MoveObject(_guest.State(), slot));
  _guest.Clobber(MOVES);
}

void RemoveObjectEntry(Guest& _guest)
{
  // The registers EraseScannerBlip leaves once it erases a blip, which the contract compares but for CX: ExplodeObject stores
  // what it finds in them, UpdateMissileAi's contract compares AX and BX after it, and TryScoopObject and TransformShip go on
  // with DX and ES.
  const ObjectSlot slot = SlotAtDi(_guest);
  EraseScannerBlipOut(_guest, slot, RemoveObject(_guest.State(), slot));
  _guest.Clobber(REMOVES_OBJECT);
}

void FacePlayerEntry(Guest& _guest)
{
  // ConvertVectorToAngles leaves the pitch in BP, and UpdateStationAi's contract compares BP after SpawnRandomTrader, through
  // FacePlayerWithRandomRoll.
  _guest.Regs().bp = FacePlayer(_guest.State(), SlotAtDi(_guest)).first;
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX);
}

void FindFreeShipSlotEntry(Guest& _guest)
{
  const SlotSearch search = FindFreeShipSlot(_guest.State());
  _guest.Regs().si = search.slot;
  _guest.SetFlag(FLAG_CARRY, search.found);
  _guest.Clobber(RETURNS_CARRY);
}

void ReclaimShipSlotEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ReclaimedSlot reclaimed = ReclaimShipSlot(_guest.State());
  regs.si = reclaimed.slot;
  if (reclaimed.evicted)
  {
    regs.di = reclaimed.slot;
  }
  _guest.Clobber(REMOVES);
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

void UpdateDebrisAiEntry(Guest& _guest)
{
  (void)UpdateDebrisAi(_guest.State(), SlotAtDi(_guest));
  _guest.Clobber(REMOVES);
}

void IsDebrisTypeEntry(Guest& _guest)
{
  const ObjectSlot slot = SlotAtDi(_guest);
  // The original leaves the type in AL, and UpdateDriftingObjectAi's contract compares AX after it.
  SetLow(_guest.Regs().ax, TypeOf(slot));
  _guest.SetFlag(FLAG_ZERO, IsDebrisType(slot));
  _guest.Clobber(TYPE_IN_AL);
}

void IsViperTypeEntry(Guest& _guest)
{
  // PUSH AX / POP AX round the test: only ZF is its result.
  _guest.SetFlag(FLAG_ZERO, IsViperType(SlotAtDi(_guest)));
  _guest.Clobber(TYPE_IN_AL);
}

void IsPoliceViperEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_ZERO, IsPoliceViper(SlotAtDi(_guest)));
  _guest.Clobber(TYPE_IN_AL);
}

void IsThargoidTypeEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_ZERO, IsThargoidType(SlotAtDi(_guest)));
  _guest.Clobber(TYPE_CLOBBERS_AL);
}

void IsThargonTypeEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_ZERO, IsThargonType(SlotAtDi(_guest)));
  _guest.Clobber(TYPE_CLOBBERS_AL);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x2FD8, "ClearObjectSlot", &ClearObjectSlotEntry, PRESERVES_ALL},
  NativeEntry{0x3B9A, "IsObjectNear", &IsObjectNearEntry, NEAR_TEST},
  NativeEntry{0x3F2A, "IsSunOrPlanet", &IsSunOrPlanetEntry, TYPE_IN_AL},
  NativeEntry{0x3F37, "IsPlanet", &IsPlanetEntry, TYPE_IN_AL},
  NativeEntry{0x3F40, "IsStation", &IsStationEntry, STATION_TEST},
  NativeEntry{0x460E, "IsObjectNearKeepBlip", &IsObjectNearKeepBlipEntry, KEEP_BLIP_TEST},
  NativeEntry{0x4C76, "InitPoliceViper", &InitPoliceViperEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4C99, "InitCargoBarrel", &InitCargoBarrelEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4CA6, "InitAbandonedCobra", &InitAbandonedCobraEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4CB3, "InitEscapePod", &InitEscapePodEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4CC0, "InitShuttle", &InitShuttleEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4CCD, "InitKraitHunter", &InitKraitHunterEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4CDA, "InitThargon", &InitThargonEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4CE7, "SpawnRandomDrifter", &SpawnRandomDrifterEntry, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4D08, "SpawnRandomTrader", &SpawnRandomTraderEntry, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x4D3B, "SpawnRandomHunter", &SpawnRandomHunterEntry, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4D60, "SpawnRandomWolf", &SpawnRandomWolfEntry, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4DAE, "SpawnMaskMissionShip", &SpawnMaskMissionShipEntry, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4DF0, "SpawnInvasionThargoid", &SpawnInvasionThargoidEntry, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4E1B, "InitObjectFromTemplate", &InitObjectFromTemplateEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4E75, "PlaceAtSpawnPoint", &PlaceAtSpawnPointEntry, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x4EC5, "FacePlayerWithRandomRoll", &FacePlayerWithRandomRollEntry, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x4EF4, "GetObjectPosition", &GetObjectPositionEntry, PRESERVES_ALL},
  NativeEntry{0x4EFE, "GetVectorToPlayer", &GetVectorToPlayerEntry, PRESERVES_ALL},
  NativeEntry{0x4F35, "RandomizeOrientation", &RandomizeOrientationEntry, CLOBBERS_AX},
  NativeEntry{0x4F48, "ComputeVelocity", &ComputeVelocityEntry, CLOBBERS_AX_DX},
  NativeEntry{0x4F6E, "MoveObject", &MoveObjectEntry, MOVES},
  NativeEntry{0x4F98, "RemoveObject", &RemoveObjectEntry, REMOVES_OBJECT},
  NativeEntry{0x513E, "FacePlayer", &FacePlayerEntry, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x51E0, "FindFreeShipSlot", &FindFreeShipSlotEntry, RETURNS_CARRY},
  NativeEntry{0x51FD, "ReclaimShipSlot", &ReclaimShipSlotEntry, REMOVES},
  NativeEntry{0x52B2, "ClearAllObjects", &ClearAllObjectsEntry, CLOBBERS_AX_CX_DI},
  NativeEntry{0x52EC, "FindDebrisSlot", &FindDebrisSlotEntry, PRESERVES_ALL},
  NativeEntry{0x5320, "CopyObject", &CopyObjectEntry, PRESERVES_ALL},
  NativeEntry{0x5330, "UpdateDebrisAi", &UpdateDebrisAiEntry, REMOVES},
  NativeEntry{0x53FE, "IsDebrisType", &IsDebrisTypeEntry, TYPE_IN_AL},
  NativeEntry{0x5413, "IsViperType", &IsViperTypeEntry, TYPE_IN_AL},
  NativeEntry{0x541E, "IsPoliceViper", &IsPoliceViperEntry, TYPE_IN_AL},
  NativeEntry{0x5428, "IsThargoidType", &IsThargoidTypeEntry, TYPE_CLOBBERS_AL},
  NativeEntry{0x5431, "IsThargonType", &IsThargonTypeEntry, TYPE_CLOBBERS_AL},
};

} // namespace

std::span<const NativeEntry> ShipsEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
