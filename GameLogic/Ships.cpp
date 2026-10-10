#include "pch.h"

#include "Ships.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Maths.h"

#include <bit>

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

// spawnTemplates: the barrel's record, and the first of the hunters' and the wolves'.
constexpr std::size_t BARREL_TEMPLATE = 3;
constexpr std::size_t HUNTER_TEMPLATES = 15;
constexpr std::size_t WOLF_TEMPLATES = 22;
constexpr std::uint16_t TEMPLATE_BYTES = 10;
// Where InitObjectFromTemplate puts bytes 1-9 of a record.
constexpr std::array<std::uint16_t, 9> TEMPLATE_FIELDS = {SLOT_SPEED,     SLOT_TURN_RATE, SLOT_BOUNTY, SLOT_MISSILES, SLOT_CARGO,
                                                          SLOT_FRAGMENTS, SLOT_ENERGY,    SLOT_DETAIL, SLOT_RANGE};

constexpr std::uint8_t BARREL_CLASS = 3;
constexpr std::uint8_t WOLF_CLASS = 5;
constexpr std::uint8_t HUNTER_CLASS = 6;
constexpr std::uint8_t HUNTER_CHOICE_DIVISOR = 37; // a random byte / 37: one of seven
constexpr std::uint8_t WOLF_CHOICE_DIVISOR = 52;   // a random byte / 52: one of five
constexpr std::uint8_t WITCH_SPACE_WOLF = 5;       // the Thargoid's record among the wolves'
constexpr std::uint8_t HUNTER_AGGRESSION_MASK = 0x1F;
constexpr std::uint8_t WOLF_AGGRESSION_MASK = 0x3F;
constexpr std::uint8_t ANARCHY_AGGRESSION = 0x20;
constexpr std::uint8_t THARGON_COUNT_MASK = 3;
constexpr std::uint8_t FEWEST_THARGONS = 2;

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
[[nodiscard]] std::uint16_t StringStep(Guest& _guest, std::uint16_t _bytes) noexcept
{
  return _guest.Flag(FLAG_DIRECTION) ? Negate(_bytes) : _bytes;
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
[[nodiscard]] std::uint8_t SlotType(const Guest& _guest, std::uint16_t _slot) noexcept
{
  return static_cast<std::uint8_t>((_guest.Byte(_slot) >> 1) & TYPE_MASK);
}

// The type of the slot at DI into AL, then CMP AL with each of _types until one matches, as the type tests chain them.
void CompareType(Guest& _guest, std::span<const std::uint8_t> _types)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t type = SlotType(_guest, regs.di);
  SetLow(regs.ax, type);
  for (const std::uint8_t candidate : _types)
  {
    SetCompareFlags(_guest, type, candidate, false);
    if (type == candidate)
    {
      return;
    }
  }
}

// The test IsObjectNear and IsObjectNearKeepBlip share: each high byte of the slot's 24-bit position is the sign extension
// of its low word. AL is left as the last high byte looked at, INC'd and DEC'd back, which leaves 0 for FFh.
[[nodiscard]] bool PositionFitsWords(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  for (int axis = 0; axis < 3; ++axis)
  {
    const std::uint8_t high = _guest.Byte(At(regs.di, SLOT_X_HIGH + axis));
    const bool negative = (_guest.Byte(At(regs.di, SLOT_X + 2 * axis + 1)) & 0x80) != 0;
    SetLow(regs.ax, high == 0xFF ? std::uint8_t{0} : high);
    const bool fits = high == 0xFF ? negative : high == 0 && !negative;
    if (!fits)
    {
      return false;
    }
  }
  return true;
}

// AND AX,7FFh / SHR AX,1 / NEG AX when the bit shifted out was set: a scatter of +-1023.
[[nodiscard]] std::uint16_t Scatter(std::uint16_t _random) noexcept
{
  const auto masked = static_cast<std::uint16_t>(_random & SPAWN_SCATTER_MASK);
  const auto halved = static_cast<std::uint16_t>(masked >> 1);
  return (masked & 1) != 0 ? Negate(halved) : halved;
}

} // namespace

void IsObjectNear(Guest& _guest)
{
  if (PositionFitsWords(_guest))
  {
    _guest.SetFlag(FLAG_CARRY, true);
    return;
  }
  _guest.Call(ERASE_SCANNER_BLIP);
  _guest.SetFlag(FLAG_CARRY, false);
}

void IsSunOrPlanet(Guest& _guest)
{
  CompareType(_guest, SUN_OR_PLANET);
}

void IsPlanet(Guest& _guest)
{
  CompareType(_guest, PLANET);
}

void IsStation(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t type = SlotType(_guest, regs.di);
  SetLow(regs.ax, type);
  if (type == TYPE_DODO)
  {
    SetCompareFlags(_guest, type, TYPE_DODO, false);
    _guest.SetFlag(FLAG_CARRY, true);
    return;
  }
  SetCompareFlags(_guest, type, TYPE_CORIOLIS, false);
  _guest.SetFlag(FLAG_CARRY, false);
}

void IsObjectNearKeepBlip(Guest& _guest)
{
  _guest.SetFlag(FLAG_CARRY, PositionFitsWords(_guest));
}

void InitCargoBarrel(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = DS.spawnTemplates.At(BARREL_TEMPLATE);
  SetLow(regs.ax, 0);
  InitObjectFromTemplate(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), BARREL_CLASS);
}

void SpawnRandomHunter(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  NextRandom(_guest);
  // XOR AH,AH / DIV BL: the quotient picks the record, and the remainder stays in AH.
  const std::uint8_t random = Low(regs.ax);
  regs.ax = static_cast<std::uint16_t>(((random % HUNTER_CHOICE_DIVISOR) << 8) | (random / HUNTER_CHOICE_DIVISOR));
  regs.bx = DS.spawnTemplates.At(HUNTER_TEMPLATES);
  InitObjectFromTemplate(_guest);
  PlaceAtSpawnPoint(_guest);
  FacePlayerWithRandomRoll(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), HUNTER_CLASS);
  NextRandom(_guest);
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
    NextRandom(_guest);
    const std::uint8_t random = Low(regs.ax);
    regs.ax = static_cast<std::uint16_t>(((random % WOLF_CHOICE_DIVISOR) << 8) | (random / WOLF_CHOICE_DIVISOR));
  }
  regs.bx = DS.spawnTemplates.At(WOLF_TEMPLATES);
  InitObjectFromTemplate(_guest);
  PlaceAtSpawnPoint(_guest);
  FacePlayerWithRandomRoll(_guest);
  _guest.SetByte(At(regs.di, SLOT_CLASS), WOLF_CLASS);
  NextRandom(_guest);
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
  NextRandom(_guest);
  const auto thargons = static_cast<std::uint8_t>((((Low(regs.ax) ^ High(regs.ax)) >> 3) & THARGON_COUNT_MASK) + FEWEST_THARGONS);
  SetLow(regs.ax, thargons);
  _guest.SetByte(At(regs.di, SLOT_THARGONS), thargons);
}

void InitObjectFromTemplate(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  _guest.SetByte(At(slot, SLOT_STATE), 0);
  _guest.SetByte(At(slot, SLOT_FLAGS), 0);
  _guest.SetByte(At(slot, SLOT_AGGRESSION), 0);
  _guest.SetByte(At(slot, SLOT_SCANNED), 1);
  // XOR AH,AH, then BX += AL*2 and BX += AL*8 with the doublings done in AX, which keeps AL*8.
  const std::uint8_t index = Low(regs.ax);
  regs.bx = static_cast<std::uint16_t>(regs.bx + index * TEMPLATE_BYTES);
  regs.ax = static_cast<std::uint16_t>(index * 8);
  const std::uint16_t record = regs.bx;
  // STC / RCL AL,1: the type, active.
  _guest.SetByte(slot, static_cast<std::uint8_t>((_guest.Byte(record) << 1) | SLOT_ACTIVE));
  for (std::size_t field = 0; field < TEMPLATE_FIELDS.size(); ++field)
  {
    const std::uint8_t value = _guest.Byte(At(record, static_cast<int>(field) + 1));
    _guest.SetByte(At(slot, TEMPLATE_FIELDS[field]), value);
    SetLow(regs.ax, value);
  }
}

void PlaceAtSpawnPoint(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  NextRandom(_guest);
  regs.ax = Scatter(regs.ax);
  regs.bx = SPAWN_DISTANCE;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(7));
  regs.cx = regs.ax;
  NextRandom(_guest);
  regs.ax = Scatter(regs.ax);
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(6));
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
  NextRandom(_guest);
  Machine::Registers& regs = _guest.Regs();
  _guest.SetWord(At(regs.di, SLOT_ROLL), regs.ax);
}

void GetObjectPosition(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Word(At(regs.di, SLOT_X));
  regs.bx = _guest.Word(At(regs.di, SLOT_Y));
  regs.cx = _guest.Word(At(regs.di, SLOT_Z));
}

void GetVectorToPlayer(Guest& _guest)
{
  GetObjectPosition(_guest);
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
  SetSinCos(_guest, DS.rotationSinCos.At(7));
  regs.ax = _guest.Word(At(slot, SLOT_YAW));
  SetSinCos(_guest, DS.rotationSinCos.At(6));
  regs.bx = SignExtend(_guest.Byte(At(slot, SLOT_SPEED)));
  regs.ax = 0;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(6));
  _guest.SetByte(At(slot, SLOT_VELOCITY), Low(regs.ax));
  regs.ax = 0;
  RotateByStoredSinCos(_guest, DS.rotationSinCos.At(7));
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

void FindFreeShipSlot(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.firstShipSlot.offset;
  const auto slots = static_cast<std::uint8_t>(_guest.Get(DS.objectSlotCount) - FIRST_SHIP_SLOT);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    if ((_guest.Byte(regs.si) & SLOT_ACTIVE) == 0)
    {
      _guest.SetFlag(FLAG_CARRY, true);
      return;
    }
    regs.si = At(regs.si, SLOT_BYTES);
  }
  _guest.SetFlag(FLAG_CARRY, false);
}

void ClearAllObjects(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.es = regs.ds;
  regs.di = DS.shipSlots.offset;
  // MOV CH,shipSlotCount / XOR CL,CL / SHR CX,1 three times: 32 words a slot, cleared by REP STOSW.
  const std::uint16_t step = StringStep(_guest, 2);
  for (std::uint16_t words = static_cast<std::uint16_t>(_guest.Get(DS.shipSlotCount) << 5); words != 0; --words)
  {
    _guest.SetFarWord(regs.es, regs.di, 0);
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
  regs.ax = 0;
  regs.cx = 0;
}

void FindDebrisSlot(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t slots = _guest.Get(DS.debrisSlotCount);
  regs.si = DS.debrisSlots.offset;
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    if ((_guest.Byte(regs.si) & SLOT_ACTIVE) == 0)
    {
      return;
    }
    regs.si = At(regs.si, SLOT_BYTES);
  }
  // None is free: the oldest, the last of equals. BX is kept, so with no slot at all SI comes back as BX.
  std::uint16_t oldest = regs.bx;
  std::uint8_t age = 0;
  regs.si = DS.debrisSlots.offset;
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    const std::uint8_t slotAge = _guest.Byte(At(regs.si, SLOT_AGE));
    if (slotAge >= age)
    {
      age = slotAge;
      oldest = regs.si;
    }
    regs.si = At(regs.si, SLOT_BYTES);
  }
  regs.si = oldest;
}

void CopyObject(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.es = regs.ds;
  // REP MOVSW, 32 words, word by word as the 8088 does it; SI, DI and CX are pushed and popped round it.
  const std::uint16_t step = StringStep(_guest, 2);
  std::uint16_t source = regs.si;
  std::uint16_t destination = regs.di;
  for (std::uint16_t words = SLOT_BYTES / 2; words != 0; --words)
  {
    _guest.SetFarWord(regs.es, destination, _guest.FarWord(regs.ds, source));
    source = static_cast<std::uint16_t>(source + step);
    destination = static_cast<std::uint16_t>(destination + step);
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
  CompareType(_guest, DEBRIS_TYPES);
}

void IsViperType(Guest& _guest)
{
  // PUSH AX / POP AX round the test: only the flags change.
  SetCompareFlags(_guest, SlotType(_guest, _guest.Regs().di), TYPE_VIPER, false);
}

void IsPoliceViper(Guest& _guest)
{
  IsViperType(_guest);
  if (!_guest.Flag(FLAG_ZERO))
  {
    return;
  }
  SetCompareFlags(_guest, _guest.Word(At(_guest.Regs().di, SLOT_OWNER)), 1, true);
}

void IsThargoidType(Guest& _guest)
{
  CompareType(_guest, THARGOID);
}

void IsThargonType(Guest& _guest)
{
  CompareType(_guest, THARGON);
}

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
constexpr Machine::NativeContract CLOBBERS_AX_BX{REGISTER_AX | REGISTER_BX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_DX{REGISTER_AX | REGISTER_BX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX_BP{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP, 0};
constexpr Machine::NativeContract NEAR_TEST{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, FLAG_CARRY};
constexpr Machine::NativeContract TYPE_IN_AL{0, FLAG_ZERO};
constexpr Machine::NativeContract TYPE_CLOBBERS_AL{REGISTER_AX, FLAG_ZERO};

constexpr std::array ENTRIES = {
  NativeEntry{0x3B9A, "IsObjectNear", &IsObjectNear, NEAR_TEST},
  NativeEntry{0x3F2A, "IsSunOrPlanet", &IsSunOrPlanet, TYPE_IN_AL},
  NativeEntry{0x3F37, "IsPlanet", &IsPlanet, TYPE_IN_AL},
  NativeEntry{0x3F40, "IsStation", &IsStation, Machine::NativeContract{0, FLAG_ZERO | FLAG_CARRY}},
  NativeEntry{0x460E, "IsObjectNearKeepBlip", &IsObjectNearKeepBlip, Machine::NativeContract{REGISTER_AX, FLAG_CARRY}},
  NativeEntry{0x4C99, "InitCargoBarrel", &InitCargoBarrel, CLOBBERS_AX_BX},
  NativeEntry{0x4D3B, "SpawnRandomHunter", &SpawnRandomHunter, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4D60, "SpawnRandomWolf", &SpawnRandomWolf, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4E1B, "InitObjectFromTemplate", &InitObjectFromTemplate, CLOBBERS_AX_BX},
  NativeEntry{0x4E75, "PlaceAtSpawnPoint", &PlaceAtSpawnPoint, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x4EC5, "FacePlayerWithRandomRoll", &FacePlayerWithRandomRoll, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x4EF4, "GetObjectPosition", &GetObjectPosition, PRESERVES_ALL},
  NativeEntry{0x4EFE, "GetVectorToPlayer", &GetVectorToPlayer, PRESERVES_ALL},
  NativeEntry{0x4F48, "ComputeVelocity", &ComputeVelocity, CLOBBERS_AX_BX_DX},
  NativeEntry{0x4F6E, "MoveObject", &MoveObject, REMOVES},
  NativeEntry{0x4F98, "RemoveObject", &RemoveObject, REMOVES},
  NativeEntry{0x513E, "FacePlayer", &FacePlayer, CLOBBERS_AX_BX_CX_DX_BP},
  NativeEntry{0x51E0, "FindFreeShipSlot", &FindFreeShipSlot, Machine::NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x52B2, "ClearAllObjects", &ClearAllObjects, Machine::NativeContract{REGISTER_AX | REGISTER_CX | REGISTER_DI, 0}},
  NativeEntry{0x52EC, "FindDebrisSlot", &FindDebrisSlot, PRESERVES_ALL},
  NativeEntry{0x5320, "CopyObject", &CopyObject, PRESERVES_ALL},
  NativeEntry{0x5330, "UpdateDebrisAi", &UpdateDebrisAi, REMOVES},
  NativeEntry{0x53FE, "IsDebrisType", &IsDebrisType, TYPE_CLOBBERS_AL},
  NativeEntry{0x5413, "IsViperType", &IsViperType, TYPE_IN_AL},
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
