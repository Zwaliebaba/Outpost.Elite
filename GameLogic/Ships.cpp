#include "pch.h"

#include "Ships.h"

#include "Arithmetic.h"
#include "Flight.h"
#include "Maths.h"
#include "ShipsTables.h"
#include "Text.h"

#include <span>
#include <utility>

namespace Elite
{

namespace
{

constexpr std::uint16_t FIRST_SHIP_SLOT = 3; // firstShipSlot's index: the sun, the planet and the station come first

// spawnTemplates: the records the routines here start from.
constexpr std::size_t ESCAPE_POD_TEMPLATE = 1; // the first of the eight drifters'
constexpr std::size_t BARREL_TEMPLATE = 3;
constexpr std::size_t SHUTTLE_TEMPLATE = 7;
constexpr std::size_t COBRA_TEMPLATE = 9; // the first of the six traders'
constexpr std::size_t VIPER_TEMPLATE = 14;
constexpr std::size_t HUNTER_TEMPLATES = 15;
constexpr std::size_t WOLF_TEMPLATES = 22;
constexpr std::size_t ASP_TEMPLATE = 23;
constexpr std::size_t THARGOID_TEMPLATE = 27;
constexpr std::size_t THARGON_TEMPLATE = 28;
constexpr std::uint16_t TEMPLATE_BYTES = 10;
// Where InitObjectFromTemplate puts bytes 1-9 of a record.
constexpr std::array<ObjectByte, 9> TEMPLATE_FIELDS = {&SpaceObject::speed,    &SpaceObject::turnRate, &SpaceObject::bounty,
                                                       &SpaceObject::missiles, &SpaceObject::cargo,    &SpaceObject::fragments,
                                                       &SpaceObject::energy,   &SpaceObject::detail,   &SpaceObject::range};

// The 24-bit position, by axis: the low words and the high bytes.
constexpr std::array<ObjectWord, 3> POSITION_LOW = {&SpaceObject::x, &SpaceObject::y, &SpaceObject::z};
constexpr std::array<ObjectByte, 3> POSITION_HIGH = {&SpaceObject::xHigh, &SpaceObject::yHigh, &SpaceObject::zHigh};
// The velocity, by axis: signed bytes.
constexpr std::array<ObjectByte, 3> VELOCITY = {&SpaceObject::velocityX, &SpaceObject::velocityY, &SpaceObject::velocityZ};

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

// MOV AL,[DI] / SHR AL,1 / AND AL,1Fh.
[[nodiscard]] std::uint8_t TypeOf(const SpaceObject& _slot) noexcept
{
  return static_cast<std::uint8_t>((_slot.type >> 1) & TYPE_MASK);
}

// CWD / MOV [slot+4+2*axis],AX / MOV [slot+1+axis],DL: _value as the 24-bit coordinate _axis (0 x, 1 y, 2 z) of _slot, the low
// word, then its sign as the high byte.
void SetCoordinate(SpaceObject& _slot, std::size_t _axis, std::int16_t _value)
{
  const auto value = static_cast<std::uint16_t>(_value);
  _slot.*POSITION_LOW[_axis] = value;
  _slot.*POSITION_HIGH[_axis] = Low(SignWord(value));
}

// What PositionFitsWords finds.
struct PositionFit
{
  bool fits;
  std::uint8_t lastHigh; // the last high byte looked at, INC'd and DEC'd back, which leaves 0 for FFh: what the original leaves in AL
};

// The test IsObjectNear and IsObjectNearKeepBlip share: each high byte of _slot's 24-bit position is the sign extension of
// its low word, axis by axis until one is not.
[[nodiscard]] PositionFit PositionFitsWords(const SpaceObject& _slot) noexcept
{
  std::uint8_t lastHigh = 0;
  for (std::size_t axis = 0; axis < POSITION_HIGH.size(); ++axis)
  {
    const std::uint8_t high = _slot.*POSITION_HIGH[axis];
    const bool negative = (_slot.*POSITION_LOW[axis] & 0x8000) != 0;
    lastHigh = high == 0xFF ? std::uint8_t{0} : high;
    const bool fits = high == 0xFF ? negative : high == 0 && !negative;
    if (!fits)
    {
      return PositionFit{false, lastHigh};
    }
  }
  return PositionFit{true, lastHigh};
}

// What RotateBySinCos leaves in DX: its last product, the second coordinate doubled times the sine, then SHL AX,1 / RCL DX,1 /
// SHL AX,1 / ADC DX,0, the high word of the product doubled, rounded on the bit below.
[[nodiscard]] std::uint16_t RotationLeftover(std::int16_t _second, std::int16_t _sine) noexcept
{
  const auto doubled = static_cast<std::int16_t>(static_cast<std::uint16_t>(_second) << 1);
  const auto product = static_cast<std::uint32_t>(std::int32_t{doubled} * _sine) << 1;
  return static_cast<std::uint16_t>((product >> 16) + ((product >> 15) & 1));
}

// AND AX,7FFh / SHR AX,1 / NEG AX when the bit shifted out was set: a scatter of +-1023.
[[nodiscard]] std::uint16_t Scatter(std::uint16_t _random) noexcept
{
  const auto masked = static_cast<std::uint16_t>(_random & SPAWN_SCATTER_MASK);
  const auto halved = static_cast<std::uint16_t>(masked >> 1);
  return (masked & 1) != 0 ? Negate(halved) : halved;
}

// AND AH,0Fh / XOR AL,AL / SHR AX,1 twice: ReclaimShipSlot's random word made the offset of one of 16 slots from slot 4, which
// is its number from there, 0-15, 40h times.
[[nodiscard]] std::uint16_t EvictedSlots(std::uint16_t _random) noexcept
{
  return static_cast<std::uint16_t>(High(_random) & 0x0F);
}

// MOV BX,record / XOR AL,AL / CALL InitObjectFromTemplate / MOV [DI+33h],_class: _slot from one fixed record of spawnTemplates,
// of behaviour class _class.
void InitFromRecord(SpaceObject& _slot, std::size_t _record, std::uint8_t _class)
{
  InitObjectFromTemplate(_slot, _record, 0);
  _slot.behaviorClass = _class;
}

// The start every spawned ship shares: _slot from record _index of spawnTemplates from _first, placed at the spawn point and
// turned to the player.
void SpawnFromRecord(GameState& _state, SpaceObject& _slot, std::size_t _first, std::uint8_t _index)
{
  InitObjectFromTemplate(_slot, _first, _index);
  PlaceAtSpawnPoint(_state, _slot);
  FacePlayerWithRandomRoll(_state, _slot);
}

} // namespace

void AddToCoordinate(SpaceObject& _slot, int _axis, std::int16_t _value)
{
  const auto axis = static_cast<std::size_t>(_axis);
  const auto value = static_cast<std::uint16_t>(_value);
  const std::uint32_t sum = std::uint32_t{_slot.*POSITION_LOW[axis]} + value;
  _slot.*POSITION_LOW[axis] = static_cast<std::uint16_t>(sum);
  _slot.*POSITION_HIGH[axis] = static_cast<std::uint8_t>(_slot.*POSITION_HIGH[axis] + Low(SignWord(value)) + (sum >> 16));
}

void ClearObjectSlot(SpaceObject& _slot)
{
  // MOV [SI],CH while CX counts down from 40h: CH is 0 throughout.
  for (Counted<std::uint8_t>& byte : BytesOf(_slot))
  {
    byte = 0;
  }
}

void InitPoliceViper(SpaceObject& _slot)
{
  InitFromRecord(_slot, VIPER_TEMPLATE, TRADER_CLASS);
  _slot.owner = SlotIndex::Police;
  _slot.aggression = POLICE_AGGRESSION;
}

void InitAbandonedCobra(SpaceObject& _slot)
{
  InitFromRecord(_slot, COBRA_TEMPLATE, ABANDONED_CLASS);
}

void InitShuttle(SpaceObject& _slot)
{
  InitFromRecord(_slot, SHUTTLE_TEMPLATE, DRIFTER_CLASS);
}

void InitThargon(SpaceObject& _slot)
{
  InitFromRecord(_slot, THARGON_TEMPLATE, WOLF_CLASS);
}

void SpawnRandomDrifter(GameState& _state, SpaceObject& _slot)
{
  // ROR AX,1 / XOR AL,AH / AND AL,7.
  const std::uint16_t random = NextRandom(_state);
  const auto rotated = static_cast<std::uint16_t>((random >> 1) | (random << 15));
  const auto record = static_cast<std::uint8_t>((Low(rotated) ^ High(rotated)) & DRIFTER_CHOICE_MASK);
  SpawnFromRecord(_state, _slot, ESCAPE_POD_TEMPLATE, record);
  _slot.behaviorClass = DRIFTER_CLASS;
  _slot.turnRate = DRIFTER_TURN_RATE;
  (void)ComputeVelocity(_state, _slot);
}

void SpawnRandomTrader(GameState& _state, SpaceObject& _slot)
{
  // XOR AH,AH / MOV BL,2Bh / DIV BL: the quotient of a random byte picks the record.
  const auto record = static_cast<std::uint8_t>(Low(NextRandom(_state)) / TRADER_CHOICE_DIVISOR);
  SpawnFromRecord(_state, _slot, COBRA_TEMPLATE, record);
  _slot.behaviorClass = TRADER_CLASS;
  (void)ComputeVelocity(_state, _slot);
  if (!IsViperType(_slot))
  {
    return;
  }
  // A Viper is police one time in two: AND AX,1 written as a word. Then MOV AL,legalStatus over the 0 of AH, written as a word,
  // which zeroes the bounty above the aggression.
  const bool police = (NextRandom(_state) & 1) != 0;
  _slot.owner = police ? SlotIndex::Police : SlotIndex::None;
  if (police)
  {
    // Written as a word: the aggression, and a bounty of 0 above it.
    _slot.aggression = std::uint8_t{_state.Data().commander.legalStatus};
    _slot.bounty = 0;
  }
}

void SpawnMaskMissionShip(GameState& _state, SpaceObject& _slot, bool _maskShip)
{
  // MOV AL,18h, or, by the sign of a random word, 12h or 13h; PUSH AX and POP AX keep it round the record's set-up.
  std::uint8_t type = TYPE_ASP;
  if (!_maskShip)
  {
    type = (NextRandom(_state) & 0x8000) != 0 ? MISSION_TYPE_IF_NEGATIVE : MISSION_TYPE_IF_POSITIVE;
  }
  SpawnFromRecord(_state, _slot, ASP_TEMPLATE, 0);
  // SHL AL,1 / INC AL: the type, active.
  _slot.type = static_cast<std::uint8_t>((type << 1) | SpaceObject::ACTIVE);
  _slot.behaviorClass = WOLF_CLASS;
  _slot.aggression = static_cast<std::uint8_t>(Low(NextRandom(_state)) & MISSION_AGGRESSION_MASK);
  _slot.energy = MISSION_ENERGY;
  _slot.cargo = 0;
  _slot.missiles = MISSION_MISSILES;
  _slot.bounty = MISSION_BOUNTY;
}

void SpawnInvasionThargoid(GameState& _state, SpaceObject& _slot)
{
  SpawnFromRecord(_state, _slot, THARGOID_TEMPLATE, 0);
  _slot.behaviorClass = WOLF_CLASS;
  _slot.aggression = static_cast<std::uint8_t>(Low(NextRandom(_state)) & MISSION_AGGRESSION_MASK);
  _slot.energy = INVADER_ENERGY;
  _slot.cargo = 0;
  _slot.missiles = MISSION_MISSILES;
  _slot.thargons = INVADER_THARGONS;
}

ReclaimedSlot ReclaimShipSlot(GameState& _state)
{
  // SUB CL,3 / XOR CH,CH, then LOOP: a count of 0 runs 65,536 times.
  SlotIndex slot = SlotIndex::FirstShip;
  const auto slots = static_cast<std::uint8_t>(_state.Data().flight.objectSlotCount - FIRST_SHIP_SLOT);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    if ((_state.Slot(slot).flags & FLAG_BLIP_DRAWN) == 0)
    {
      return ReclaimedSlot{slot, false};
    }
    slot = SlotAfter(slot);
  }
  // Every one has a blip: one of slots 4-19 at random (AND AH,0Fh / XOR AL,AL / SHR AX,1 twice), removed first.
  const SlotIndex evicted = SlotAfter(static_cast<SlotIndex>(FIRST_EVICTED_SLOT), EvictedSlots(NextRandom(_state)));
  (void)RemoveObject(_state, _state.Slot(evicted));
  return ReclaimedSlot{evicted, true};
}

NearTest IsObjectNear(GameState& _state, SpaceObject& _slot)
{
  const PositionFit fit = PositionFitsWords(_slot);
  if (fit.fits)
  {
    return NearTest{true, std::nullopt, fit.lastHigh};
  }
  return NearTest{false, EraseScannerBlip(_state, _slot), fit.lastHigh};
}

bool IsSunOrPlanet(const SpaceObject& _slot)
{
  const std::uint8_t type = TypeOf(_slot);
  return type == TYPE_SUN || type == TYPE_PLANET;
}

bool IsPlanet(const SpaceObject& _slot)
{
  return TypeOf(_slot) == TYPE_PLANET;
}

StationTest IsStation(const SpaceObject& _slot)
{
  const std::uint8_t type = TypeOf(_slot);
  return StationTest{type == TYPE_DODO || type == TYPE_CORIOLIS, type == TYPE_DODO};
}

bool IsObjectNearKeepBlip(const SpaceObject& _slot)
{
  return PositionFitsWords(_slot).fits;
}

void InitCargoBarrel(SpaceObject& _slot)
{
  InitFromRecord(_slot, BARREL_TEMPLATE, BARREL_CLASS);
}

void SpawnRandomHunter(GameState& _state, SpaceObject& _slot)
{
  // XOR AH,AH / MOV BL,25h / DIV BL: the quotient of a random byte picks the record.
  const auto record = static_cast<std::uint8_t>(Low(NextRandom(_state)) / HUNTER_CHOICE_DIVISOR);
  SpawnFromRecord(_state, _slot, HUNTER_TEMPLATES, record);
  _slot.behaviorClass = HUNTER_CLASS;
  _slot.aggression = static_cast<std::uint8_t>(Low(NextRandom(_state)) & HUNTER_AGGRESSION_MASK);
  (void)ComputeVelocity(_state, _slot);
}

void SpawnRandomWolf(GameState& _state, SpaceObject& _slot)
{
  // MOV AL,5, or out of witch space XOR AH,AH / MOV BL,34h / DIV BL: the quotient of a random byte picks the record.
  std::uint8_t record = WITCH_SPACE_WOLF;
  if (_state.Data().commander.witchspaceCountdown == 0)
  {
    record = static_cast<std::uint8_t>(Low(NextRandom(_state)) / WOLF_CHOICE_DIVISOR);
  }
  SpawnFromRecord(_state, _slot, WOLF_TEMPLATES, record);
  _slot.behaviorClass = WOLF_CLASS;
  auto aggression = static_cast<std::uint8_t>(Low(NextRandom(_state)) & WOLF_AGGRESSION_MASK);
  if (_state.Data().flight.spawnGovernment == 0)
  {
    aggression = static_cast<std::uint8_t>(aggression + ANARCHY_AGGRESSION);
  }
  _slot.aggression = aggression;
  (void)ComputeVelocity(_state, _slot);
  if (!IsThargoidType(_slot))
  {
    return;
  }
  // XOR AL,AH / SHR AL,1 three times / AND AL,3 / ADD AL,2: 2 to 5 Thargons.
  const std::uint16_t random = NextRandom(_state);
  _slot.thargons = static_cast<std::uint8_t>((((Low(random) ^ High(random)) >> 3) & THARGON_COUNT_MASK) + FEWEST_THARGONS);
}

void InitObjectFromTemplate(SpaceObject& _slot, std::size_t _first, std::uint8_t _index)
{
  _slot.aiState = 0;
  _slot.flags = 0;
  _slot.aggression = 0;
  _slot.scanned = 1;
  // BX += AL*2, then BX += AL*8: the record _index of ten bytes on.
  const std::array<std::uint8_t, TEMPLATE_BYTES>& record = SPAWN_TEMPLATES[_first + _index];
  // STC / RCL AL,1: the type, active.
  _slot.type = static_cast<std::uint8_t>((record[0] << 1) | SpaceObject::ACTIVE);
  for (std::size_t field = 0; field < TEMPLATE_FIELDS.size(); ++field)
  {
    _slot.*TEMPLATE_FIELDS[field] = std::uint8_t{record[field + 1]};
  }
}

void PlaceAtSpawnPoint(GameState& _state, SpaceObject& _slot)
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

void FacePlayerWithRandomRoll(GameState& _state, SpaceObject& _slot)
{
  FacePlayer(_state, _slot);
  _slot.roll = NextRandom(_state);
}

Vector GetObjectPosition(const SpaceObject& _slot)
{
  return Vector{static_cast<std::int16_t>(_slot.x), static_cast<std::int16_t>(_slot.y), static_cast<std::int16_t>(_slot.z)};
}

Vector GetVectorToPlayer(const SpaceObject& _slot)
{
  const Vector position = GetObjectPosition(_slot);
  const auto negated = [](std::int16_t _value) { return static_cast<std::int16_t>(Negate(static_cast<std::uint16_t>(_value))); };
  return Vector{negated(position.x), negated(position.y), negated(position.z)};
}

Velocity ComputeVelocity(GameState& _state, SpaceObject& _slot)
{
  const SinCos pitch = SetSinCos(_state, 7, _slot.pitch);
  (void)SetSinCos(_state, 6, _slot.yaw);
  // (0, speed) by pair 6, then (0, what that left) by pair 7: x, then y and z.
  const auto speed = static_cast<std::int16_t>(SignExtend(_slot.speed)); // CBW
  const Pair first = RotateByStoredSinCos(_state, 6, Pair{0, speed});
  _slot.velocityX = Low(static_cast<std::uint16_t>(first.first));
  const Pair second = RotateByStoredSinCos(_state, 7, Pair{0, first.second});
  _slot.velocityY = Low(static_cast<std::uint16_t>(second.first));
  _slot.velocityZ = Low(static_cast<std::uint16_t>(second.second));
  return Velocity{Vector{first.first, second.first, second.second}, RotationLeftover(first.second, pitch.sine)};
}

MovedObject MoveObject(GameState& _state, SpaceObject& _slot)
{
  // MOV AL,[DI+19h+axis] / CBW / CWD, then ADD on the low word and ADC on the high byte: x, then y and z.
  for (std::size_t axis = 0; axis < VELOCITY.size(); ++axis)
  {
    AddToCoordinate(_slot, static_cast<int>(axis), static_cast<std::int16_t>(SignExtend(_slot.*VELOCITY[axis])));
  }
  // The last CWD leaves DX the z velocity's sign; then an erased blip leaves its last pixel there.
  std::uint16_t dx = SignWord(SignExtend(_slot.velocityZ));
  const NearTest near = IsObjectNear(_state, _slot);
  if (near.erasedBlip)
  {
    dx = PixelPlace(*near.erasedBlip);
  }
  if (near.nearby)
  {
    return MovedObject{near, std::nullopt, dx};
  }
  const std::optional<DashboardPixel> removed = RemoveObject(_state, _slot);
  if (removed)
  {
    dx = PixelPlace(*removed);
  }
  return MovedObject{near, removed, dx};
}

std::optional<DashboardPixel> RemoveObject(GameState& _state, SpaceObject& _slot)
{
  _slot.type = static_cast<std::uint8_t>(_slot.type & ~SpaceObject::ACTIVE);
  return EraseScannerBlip(_state, _slot);
}

void FacePlayer(GameState& _state, SpaceObject& _slot)
{
  const Angles heading = ConvertVectorToAngles(_state, GetVectorToPlayer(_slot));
  _slot.pitch = heading.first;
  _slot.yaw = heading.second;
}

SlotSearch FindFreeShipSlot(GameState& _state)
{
  SlotIndex slot = SlotIndex::FirstShip;
  const auto slots = static_cast<std::uint8_t>(_state.Data().flight.objectSlotCount - FIRST_SHIP_SLOT);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    if ((_state.Slot(slot).type & SpaceObject::ACTIVE) == 0)
    {
      return SlotSearch{true, slot};
    }
    slot = SlotAfter(slot);
  }
  return SlotSearch{false, slot};
}

void ClearAllObjects(GameState& _state)
{
  // MOV CH,shipSlotCount / XOR CL,CL / SHR CX,1 three times: 32 words a slot, cleared by REP STOSW into ES = DS, upwards, as the direction
  // flag is clear wherever the game calls it: the only STD, MaskOutsideTunnel's, has its CLD before the routine returns (change 3c): the
  // first shipSlotCount slots, a word at a time, low byte first. shipSlotCount is only ever 3, 20h or 24h, so every store is in shipSlots'
  // 36, and a count past them throws (GameState::Slot).
  SlotIndex slot = SlotIndex::Sun;
  for (std::uint8_t slots = _state.Data().world.shipSlotCount; slots != 0; --slots)
  {
    ClearObjectSlot(_state.Slot(slot));
    slot = SlotAfter(slot);
  }
}

SlotIndex FindDebrisSlot(GameState& _state)
{
  const std::uint8_t slots = _state.Data().flight.debrisSlotCount;
  SlotIndex slot = SlotIndex::FirstDebris;
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    if ((_state.Slot(slot).type & SpaceObject::ACTIVE) == 0)
    {
      return slot;
    }
    slot = SlotAfter(slot);
  }
  // None is free: the oldest, the last of equals. The first slot always passes the test (an age of at least 0), so the
  // original's BX, its first candidate, never comes back.
  SlotIndex oldest = SlotIndex::FirstDebris;
  std::uint8_t age = 0;
  slot = SlotIndex::FirstDebris;
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    const std::uint8_t slotAge = _state.Slot(slot).age;
    if (slotAge >= age)
    {
      age = slotAge;
      oldest = slot;
    }
    slot = SlotAfter(slot);
  }
  return oldest;
}

void CopyObject(const SpaceObject& _source, SpaceObject& _destination)
{
  // REP MOVSW from DS:SI to ES:DI, ES = DS: 32 words, word by word as the 8088 does it, each read whole and then written, from the first,
  // upwards, as the direction flag is clear wherever the game calls it: the only STD, MaskOutsideTunnel's, has its CLD before the routine
  // returns (change 3c).
  const std::span<const Counted<std::uint8_t>> source = BytesOf(std::as_const(_source));
  const std::span<Counted<std::uint8_t>> destination = BytesOf(_destination);
  for (std::size_t word = 0; word < SpaceObject::BYTES; word += 2)
  {
    SetWordIn(destination, word, WordIn(source, word));
  }
}

std::optional<MovedObject> UpdateDebrisAi(GameState& _state, SpaceObject& _slot)
{
  const auto lifetime = static_cast<std::uint8_t>(_slot.lifetime - 1);
  _slot.lifetime = lifetime;
  if (lifetime == 0)
  {
    _slot.type = static_cast<std::uint8_t>(_slot.type & ~SpaceObject::ACTIVE);
    return std::nullopt;
  }
  _slot.age = static_cast<std::uint8_t>(_slot.age + 1);
  // MOV AL,[DI+26h] / CBW / ADD [DI+0Eh],AX, then the same from +27h into the pitch.
  _slot.roll = Offset(_slot.roll, SignExtend(_slot.SpinRoll()));
  _slot.pitch = Offset(_slot.pitch, SignExtend(_slot.SpinPitch()));
  return MoveObject(_state, _slot);
}

bool IsDebrisType(const SpaceObject& _slot)
{
  const std::uint8_t type = TypeOf(_slot);
  return type == TYPE_PLATE || type == TYPE_BOULDER || type == TYPE_ASTEROID || type == TYPE_SPLINTER;
}

bool IsViperType(const SpaceObject& _slot)
{
  return TypeOf(_slot) == TYPE_VIPER;
}

bool IsPoliceViper(const SpaceObject& _slot)
{
  return IsViperType(_slot) && _slot.owner == SlotIndex::Police;
}

bool IsThargoidType(const SpaceObject& _slot)
{
  return TypeOf(_slot) == TYPE_THARGOID;
}

bool IsThargonType(const SpaceObject& _slot)
{
  return TypeOf(_slot) == TYPE_THARGON;
}

} // namespace Elite
