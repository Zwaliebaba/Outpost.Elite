#include "pch.h"

#include "Combat.h"

#include "Ai.h"
#include "DataOverlay.h"
#include "Maths.h"
#include "Ships.h"

#include <utility>

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_ZERO;

// Routines outside this file, run through the original.
constexpr std::uint16_t DRAW_CLIPPED_LINE = 0x1603;
constexpr std::uint16_t DRAW_LINE = 0x16D1;
constexpr std::uint16_t IN_SAFE_ZONE = 0x2E63;
constexpr std::uint16_t VECTOR_WITHIN_BOX = 0x2F6E;
constexpr std::uint16_t SHOW_BOUNTY_MESSAGE = 0x3626;
constexpr std::uint16_t LAUNCH_SHIP_FROM_OBJECT = 0x534E;
constexpr std::uint16_t ADD_CREDITS = 0x65EE;
constexpr std::uint16_t START_IMPACT_SOUND = 0x7AC3;
constexpr std::uint16_t START_EXPLOSION_SOUND = 0x7AFC;
constexpr std::uint16_t START_LASER_SOUND = 0x7B71;
constexpr std::uint16_t START_PLAYER_HIT_SOUND = 0x7B96;
constexpr std::uint16_t CANCEL_DOCKING_COMPUTER = 0x8BAA;
constexpr std::uint16_t ROUTINE_8C51 = 0x8C51;
constexpr std::uint16_t PROJECT_TO_SCREEN = 0x8D2E;

// DivideOverflowInterrupt's scratch words in the code segment, and what it leaves in AL or AX (plan §7.1).
constexpr std::uint16_t DIVIDE_SAVED_BX_OFFSET = 0x02A1;
constexpr std::uint16_t DIVIDE_SAVED_DS_OFFSET = 0x02A3;
constexpr std::uint8_t DIVIDE_OVERFLOW_BYTE = 0x7F;
constexpr std::uint16_t DIVIDE_OVERFLOW_WORD = 0x7FFF;

// The laser sights: a 16x16 sprite at the centre of the space view's 64-byte rows, 128 bytes a laser type.
constexpr std::uint16_t VIEW_ROW_BYTES = 64;
constexpr std::uint16_t SIGHTS_OFFSET = 56 * VIEW_ROW_BYTES + 30;
constexpr std::uint16_t SIGHTS_ROWS = 16;
constexpr std::uint16_t SIGHTS_ROW_BYTES = 8; // AND and OR masks for two words

// The beams: from the bottom row to a point within 3 pixels of (7Eh, 3Eh).
constexpr std::uint8_t BEAM_ROW = 0x7F;
constexpr std::uint16_t BEAM_SCATTER_MASK = 0x0303;
constexpr std::uint8_t BEAM_TARGET_X = 0x7E;
constexpr std::uint8_t BEAM_TARGET_ROW = 0x3E;
constexpr std::uint8_t BEAM_OUTER_LEFT = 0x32;
constexpr std::uint8_t BEAM_INNER_LEFT = 0x46;
constexpr std::uint8_t BEAM_INNER_RIGHT = 0xB9;
constexpr std::uint8_t BEAM_OUTER_RIGHT = 0xCD;
constexpr std::uint8_t BEAM_COLOR_MASK = 3;
constexpr std::uint8_t BEAM_TOGGLE = 2;

constexpr std::uint8_t SLOT_DRAWN = 0x80;
constexpr std::uint8_t FLAG_HOSTILE = 0x01;
constexpr std::uint8_t FLAG_BLIP_DRAWN = 0x02;
constexpr std::uint8_t FLAG_INDESTRUCTIBLE = 0x04;
constexpr std::uint8_t FLAG_RESTING = 0x08;
constexpr std::uint8_t FLAG_MINERALS = 0x10;
constexpr std::uint8_t FLAG_DEVICE = 0x20;
constexpr std::uint8_t CROSSHAIR_IGNORED = 0x60; // both bits: an object the crosshairs pass over

constexpr std::uint8_t MASK_SHIP_BOUNTY = 200;
constexpr std::uint8_t DEBRIS_CLASS = 7;
constexpr std::uint8_t SPLINTER_ACTIVE = (TYPE_SPLINTER << 1) | SLOT_ACTIVE;
constexpr std::uint16_t FRAGMENT_SCATTER_MASK = 0x1F1F;
constexpr std::uint8_t FRAGMENT_SCATTER_CENTER = 0x0F;
constexpr std::uint16_t MINERALS_ODDS = 2000;
constexpr std::uint8_t FRAGMENT_LIFETIME_MASK = 0x0F;
constexpr std::uint8_t FRAGMENT_LIFETIME = 0x14;
constexpr std::uint8_t MINED_FRAGMENT_LIFETIME = 0x3C;
constexpr std::uint16_t STATION_FRAGMENT_STEPS = 10;
constexpr std::uint8_t MINING_LASER = 2;

constexpr std::uint8_t CALMER_STEP = 5;
constexpr std::uint8_t CALMEST = 0x14;
constexpr std::uint16_t LASER_GRAZE_BOX = 200;
constexpr std::uint16_t LASER_HIT_BOX = 0x46;
constexpr std::uint8_t HIT_GRAZED = 2;
constexpr std::uint8_t HIT_SQUARE = 1;
constexpr std::uint8_t MISSILE_LAUNCH = 0x14;
constexpr std::uint8_t THARGON_LAUNCH = 7;
constexpr std::uint8_t MISSILE_KILLS_NEEDED = 3;
constexpr std::uint16_t THARGON_ODDS = 300;

constexpr std::uint8_t LEGAL_STATUS_PER_STATION_HIT = 0x28;
constexpr std::uint8_t MOST_KILLS = 0xFE;
constexpr std::uint8_t NO_BOUNTY_TYPE = 0xFF; // bounty byte FFh: no pay, a crime
constexpr std::uint16_t THARGOID_BOUNTY = 500;
constexpr std::uint8_t CRIME = 2;
constexpr std::uint8_t CRIME_AGAINST_POLICE = 4;
constexpr std::uint8_t THARGON_REPAIR = 5;
constexpr std::uint8_t THARGOID_REPAIR = 0x23;
constexpr std::uint16_t MISSILE_MESSAGE_FRAMES = 0x19;
constexpr std::uint16_t REPAIR_MESSAGE_FRAMES = 0x1E;
constexpr std::uint8_t MISSILE_LOCKED = 2;

// Where ApplyEnemyLaserHit's beam ends: on an edge of the view, by four ranges of a random word.
constexpr std::uint16_t EDGE_LEFT_BELOW = 0x53FC;
constexpr std::uint16_t EDGE_RIGHT_BELOW = 0xA7F8;
constexpr std::uint16_t EDGE_TOP_BELOW = 0xD2F0;
constexpr std::uint16_t EDGE_ROW_MASK = 0x7F;
constexpr std::uint8_t ENEMY_BEAM_COLOR = 3;
constexpr std::uint8_t SHIELD_HIT = 0x0F;

[[nodiscard]] std::uint16_t At(std::uint16_t _slot, int _field) noexcept
{
  return static_cast<std::uint16_t>(_slot + _field);
}

[[nodiscard]] std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word & 0xFF);
}

[[nodiscard]] std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

void SetLow(std::uint16_t& _word, std::uint8_t _value) noexcept
{
  _word = static_cast<std::uint16_t>((_word & 0xFF00) | _value);
}

void SetHigh(std::uint16_t& _word, std::uint8_t _value) noexcept
{
  _word = static_cast<std::uint16_t>((_word & 0x00FF) | (_value << 8));
}

[[nodiscard]] std::uint16_t Join(std::uint8_t _high, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_high << 8) | _low);
}

[[nodiscard]] std::uint16_t Swap(std::uint16_t _word) noexcept
{
  return Join(Low(_word), High(_word));
}

[[nodiscard]] std::uint16_t SignExtend(std::uint8_t _byte) noexcept
{
  return static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::int8_t>(_byte)));
}

[[nodiscard]] std::uint16_t SignWord(std::uint16_t _word) noexcept
{
  return (_word & 0x8000) != 0 ? std::uint16_t{0xFFFF} : std::uint16_t{0};
}

[[nodiscard]] std::uint16_t Negate(std::uint16_t _value) noexcept
{
  return static_cast<std::uint16_t>(0u - _value);
}

// SAR r/m8,1.
[[nodiscard]] std::uint8_t HalveSigned(std::uint8_t _value) noexcept
{
  return static_cast<std::uint8_t>(static_cast<std::int8_t>(_value) >> 1);
}

[[nodiscard]] bool FlagSet(Guest& _guest, std::uint16_t _flag) noexcept
{
  return (_guest.Regs().flags & _flag) != 0;
}

[[nodiscard]] std::uint32_t LoopCount(std::uint16_t _count) noexcept
{
  return _count == 0 ? 0x10000u : _count;
}

[[nodiscard]] std::uint8_t SlotType(const Guest& _guest, std::uint16_t _slot) noexcept
{
  return static_cast<std::uint8_t>((_guest.Byte(_slot) >> 1) & TYPE_MASK);
}

void OrByte(Guest& _guest, std::uint16_t _offset, std::uint8_t _bits) noexcept
{
  _guest.SetByte(_offset, static_cast<std::uint8_t>(_guest.Byte(_offset) | _bits));
}

void AddByte(Guest& _guest, std::uint16_t _offset, std::uint8_t _value) noexcept
{
  _guest.SetByte(_offset, static_cast<std::uint8_t>(_guest.Byte(_offset) + _value));
}

// ADD byte,_value / JAE / MOV byte,0FFh: an add that stops at FFh.
void AddSaturating(Guest& _guest, std::uint16_t _offset, std::uint8_t _value) noexcept
{
  const unsigned sum = _guest.Byte(_offset) + unsigned{_value};
  _guest.SetByte(_offset, sum > 0xFF ? std::uint8_t{0xFF} : static_cast<std::uint8_t>(sum));
}

// What DivideOverflowInterrupt (CS:025E) does before it saturates the quotient: BX and DS kept in the code segment.
void DivideOverflow(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  _guest.SetCodeWord(DIVIDE_SAVED_BX_OFFSET, regs.bx);
  _guest.SetCodeWord(DIVIDE_SAVED_DS_OFFSET, regs.ds);
}

// DIV r/m8: AX / _divisor, quotient in AL and remainder in AH; when it does not fit, the trap leaves AL = 7Fh and AH as it was.
void DivideByte(Guest& _guest, std::uint8_t _divisor)
{
  Machine::Registers& regs = _guest.Regs();
  if (High(regs.ax) >= _divisor)
  {
    DivideOverflow(_guest);
    SetLow(regs.ax, DIVIDE_OVERFLOW_BYTE);
    return;
  }
  const std::uint16_t dividend = regs.ax;
  regs.ax = Join(static_cast<std::uint8_t>(dividend % _divisor), static_cast<std::uint8_t>(dividend / _divisor));
}

// DIV r/m16: DX:AX / _divisor; when it does not fit, the trap leaves AX = 7FFFh and DX as it was.
void DivideWord(Guest& _guest, std::uint16_t _divisor)
{
  Machine::Registers& regs = _guest.Regs();
  if (regs.dx >= _divisor)
  {
    DivideOverflow(_guest);
    regs.ax = DIVIDE_OVERFLOW_WORD;
    return;
  }
  const std::uint32_t dividend = (std::uint32_t{regs.dx} << 16) | regs.ax;
  regs.ax = static_cast<std::uint16_t>(dividend / _divisor);
  regs.dx = static_cast<std::uint16_t>(dividend % _divisor);
}

// gameOverFrames | escapePodFrames | maskingBackgroundColor into AL: none of them may run when a ship fires.
[[nodiscard]] bool FiringBlocked(Guest& _guest)
{
  const auto blocked =
    static_cast<std::uint8_t>(_guest.Get(DS.gameOverFrames) | _guest.Get(DS.escapePodFrames) | _guest.Get(DS.maskingBackgroundColor));
  SetLow(_guest.Regs().ax, blocked);
  return blocked != 0;
}

// The next beam colour, 1-3, counting up or down past 0.
[[nodiscard]] std::uint8_t NextBeamColor(std::uint8_t _color, bool _up) noexcept
{
  std::uint8_t color = _color;
  do
  {
    color = static_cast<std::uint8_t>((_up ? color + 1 : color - 1) & BEAM_COLOR_MASK);
  } while (color == 0);
  return color;
}

// One beam: DrawLine from (_x, the bottom row) to DL, DH. DX is kept (PUSH DX / POP DX) unless it is the last.
void DrawBeam(Guest& _guest, std::uint8_t _x, bool _last)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t target = regs.dx;
  regs.cx = Join(BEAM_ROW, _x);
  _guest.Call(DRAW_LINE);
  if (!_last)
  {
    regs.dx = target;
  }
}

// |the coordinate at _field| * 256, divided by the view z, below BX: the crosshairs' test on one axis.
[[nodiscard]] bool ScaledWithinRadius(Guest& _guest, int _field)
{
  Machine::Registers& regs = _guest.Regs();
  std::uint16_t magnitude = _guest.Word(At(regs.di, _field));
  if ((magnitude & 0x8000) != 0)
  {
    magnitude = Negate(magnitude);
  }
  // CWD / MOV DL,AH / MOV AH,AL / XOR AL,AL: DX:AX = the magnitude * 256, with DH its sign.
  regs.dx = Join(High(SignWord(magnitude)), High(magnitude));
  regs.ax = Join(Low(magnitude), 0);
  DivideWord(_guest, _guest.Word(At(regs.di, SLOT_VIEW_Z)));
  return regs.ax < regs.bx;
}

// 8BE5: ShowBountyMessage and AddCredits with AX, DI kept round them; then a kill in a mis-jump's witch space counts the
// countdown to the Nav-Comp's repair down.
void PayBounty(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t slot = regs.di;
  const std::uint16_t bounty = regs.ax;
  _guest.Call(SHOW_BOUNTY_MESSAGE);
  regs.ax = bounty;
  regs.bx = 0;
  _guest.Call(ADD_CREDITS);
  regs.di = slot;
  if (_guest.Get(DS.witchspaceCountdown) == 0)
  {
    return;
  }
  _guest.Call(ROUTINE_8C51);
  if (!FlagSet(_guest, FLAG_ZERO))
  {
    return;
  }
  const std::uint8_t repair = FlagSet(_guest, FLAG_CARRY) ? THARGOID_REPAIR : THARGON_REPAIR;
  SetLow(regs.ax, repair);
  const std::uint8_t countdown = _guest.Get(DS.witchspaceCountdown);
  auto left = static_cast<std::uint8_t>(countdown - repair);
  if (countdown < repair || left == 0)
  {
    left = 1;
  }
  _guest.Set(DS.witchspaceCountdown, left);
  if (left != 1)
  {
    return;
  }
  regs.ax = DS.navCompRepairedMessage.offset;
  _guest.Set(DS.messagePointer, regs.ax);
  _guest.Set(DS.messageFrames, REPAIR_MESSAGE_FRAMES);
}

// The fragments of an exploding object: from 4FEE, CX of them, each a Splinter in a debris slot.
void SpawnFragments(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  for (;;)
  {
    const std::uint16_t fragments = regs.cx;
    FindDebrisSlot(_guest);
    std::swap(regs.si, regs.di);
    CopyObject(_guest);
    const std::uint16_t debris = regs.di;
    _guest.SetByte(At(debris, SLOT_STATE), 0);
    NextRandom(_guest);
    _guest.SetWord(At(debris, SLOT_SPIN_ROLL), regs.ax);
    regs.bx = regs.ax;
    const bool mining = _guest.Get(DS.miningLaserOnAsteroid) == 1;
    // The x and y velocity halved, plus a random -15..16 (halved again for a mined asteroid).
    regs.ax = static_cast<std::uint16_t>(regs.ax & FRAGMENT_SCATTER_MASK);
    auto scatterX = static_cast<std::uint8_t>(Low(regs.ax) - FRAGMENT_SCATTER_CENTER);
    auto scatterY = static_cast<std::uint8_t>(High(regs.ax) - FRAGMENT_SCATTER_CENTER);
    _guest.SetByte(At(debris, SLOT_VELOCITY), HalveSigned(_guest.Byte(At(debris, SLOT_VELOCITY))));
    _guest.SetByte(At(debris, SLOT_VELOCITY + 1), HalveSigned(_guest.Byte(At(debris, SLOT_VELOCITY + 1))));
    if (mining)
    {
      scatterX = HalveSigned(scatterX);
      scatterY = HalveSigned(scatterY);
    }
    regs.ax = Join(scatterY, scatterX);
    AddByte(_guest, At(debris, SLOT_VELOCITY), scatterX);
    AddByte(_guest, At(debris, SLOT_VELOCITY + 1), scatterY);
    regs.bx = static_cast<std::uint16_t>(regs.bx >> 3);
    auto scatterZ = static_cast<std::uint8_t>((Low(regs.bx) & 0x1F) - FRAGMENT_SCATTER_CENTER);
    _guest.SetByte(At(debris, SLOT_VELOCITY + 2), HalveSigned(_guest.Byte(At(debris, SLOT_VELOCITY + 2))));
    if (mining)
    {
      scatterZ = HalveSigned(scatterZ);
    }
    SetLow(regs.bx, scatterZ);
    AddByte(_guest, At(debris, SLOT_VELOCITY + 2), scatterZ);
    _guest.SetByte(At(debris, SLOT_FLAGS), FLAG_RESTING);
    if (mining)
    {
      NextRandom(_guest);
      if (regs.ax < MINERALS_ODDS)
      {
        OrByte(_guest, At(debris, SLOT_FLAGS), FLAG_MINERALS);
      }
    }
    _guest.SetByte(At(debris, SLOT_ENERGY), 0);
    _guest.SetWord(At(debris, SLOT_CARGO), 0);
    _guest.SetByte(At(debris, SLOT_AGE), 0);
    _guest.SetByte(At(debris, SLOT_CLASS), DEBRIS_CLASS);
    NextRandom(_guest);
    auto lifetime = static_cast<std::uint8_t>(Low(regs.ax) & FRAGMENT_LIFETIME_MASK);
    if (mining)
    {
      lifetime = static_cast<std::uint8_t>(lifetime + MINED_FRAGMENT_LIFETIME);
    }
    lifetime = static_cast<std::uint8_t>(lifetime + FRAGMENT_LIFETIME);
    _guest.SetByte(At(debris, SLOT_LIFETIME), lifetime);
    SetLow(regs.ax, SPLINTER_ACTIVE);
    _guest.SetByte(debris, SPLINTER_ACTIVE);
    UpdateDebrisAi(_guest);
    if (_guest.Get(DS.explodingStation) == 1)
    {
      // A station's fragments are flung ten frames further at once.
      for (regs.cx = STATION_FRAGMENT_STEPS; regs.cx != 0; regs.cx = static_cast<std::uint16_t>(regs.cx - 1))
      {
        const std::uint16_t steps = regs.cx;
        UpdateDebrisAi(_guest);
        regs.cx = steps;
      }
    }
    regs.di = regs.si;
    regs.cx = static_cast<std::uint16_t>(fragments - 1);
    if (regs.cx == 0)
    {
      return;
    }
  }
}

// DropCargo (5099): the barrels an exploded object leaves, each copied from it with a random heading. A failed slot search
// leaves DI on what it returned, as the original's XCHG does.
void DropCargo(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if ((_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_DEVICE) != 0)
  {
    regs.cx = 1;
  }
  else
  {
    // rand / (255 / (cargo + 1) + 1) barrels.
    SetLow(regs.bx, _guest.Byte(At(regs.di, SLOT_CARGO)));
    if (Low(regs.bx) == 0)
    {
      return;
    }
    regs.ax = 0xFF;
    SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) + 1));
    DivideByte(_guest, Low(regs.bx));
    SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.ax) + 1));
    NextRandom(_guest);
    SetHigh(regs.ax, 0);
    DivideByte(_guest, Low(regs.bx));
    if (Low(regs.ax) == 0)
    {
      return;
    }
    regs.cx = Low(regs.ax);
  }
  do
  {
    const std::uint16_t barrels = regs.cx;
    FindFreeShipSlot(_guest);
    std::swap(regs.di, regs.si);
    if (FlagSet(_guest, FLAG_CARRY))
    {
      SetLow(regs.ax, _guest.Byte(At(regs.si, SLOT_FLAGS)));
      const std::uint16_t flags = regs.ax;
      CopyObject(_guest);
      InitCargoBarrel(_guest);
      _guest.SetByte(At(regs.di, SLOT_FLAGS), FLAG_RESTING);
      regs.ax = flags;
      // The device's bit (5) becomes the barrel's bit 6.
      SetLow(regs.ax, static_cast<std::uint8_t>((Low(regs.ax) & FLAG_DEVICE) << 1));
      OrByte(_guest, At(regs.di, SLOT_FLAGS), Low(regs.ax));
      NextRandom(_guest);
      _guest.SetWord(At(regs.di, SLOT_PITCH), regs.ax);
      regs.ax = Swap(regs.ax);
      _guest.SetWord(At(regs.di, SLOT_YAW), regs.ax);
      ComputeVelocity(_guest);
      MoveObject(_guest);
      regs.di = regs.si;
    }
    regs.cx = static_cast<std::uint16_t>(barrels - 1);
  } while (regs.cx != 0);
}

// 8B63: the target at DI is destroyed by the player's laser.
void DestroyTarget(Guest& _guest)
{
  CreditKill(_guest);
  CheckMissileTargetDestroyed(_guest);
  ExplodeObject(_guest);
  DrawLaserBeams(_guest);
  _guest.Set(DS.miningLaserOnAsteroid, 0);
  _guest.Set(DS.laserFiring, 0);
}

// The hit on the object at DI. False when it is destroyed (DestroyTarget has run), true when the beams and the laser sound
// follow, as for a miss.
[[nodiscard]] bool HitTarget(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  OrByte(_guest, At(regs.di, SLOT_FLAGS), FLAG_HOSTILE);
  _guest.Call(START_IMPACT_SOUND);
  SetLow(regs.ax, _guest.Get(DS.firingLaserType));
  if (Low(regs.ax) == MINING_LASER)
  {
    SetHigh(regs.ax, SlotType(_guest, regs.di));
    if (High(regs.ax) == TYPE_ASTEROID)
    {
      _guest.Set(DS.miningLaserOnAsteroid, 1);
    }
  }
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) + 1));
  const bool invasion = _guest.Get(DS.thargoidInvasionActive) == 1;
  const std::uint16_t damage = regs.ax;
  IsStation(_guest);
  regs.ax = damage;
  if (FlagSet(_guest, FLAG_ZERO))
  {
    _guest.Call(CANCEL_DOCKING_COMPUTER);
    if (invasion)
    {
      // NEG AL / SAR AL,1 / NEG AL: half the damage, rounded up.
      const auto halved = HalveSigned(static_cast<std::uint8_t>(0u - Low(regs.ax)));
      SetLow(regs.ax, static_cast<std::uint8_t>(0u - halved));
    }
    else
    {
      AddSaturating(_guest, DS.legalStatus.offset, LEGAL_STATUS_PER_STATION_HIT);
    }
  }
  AddSaturating(_guest, At(regs.di, SLOT_AGGRESSION), Low(regs.ax));
  const std::uint8_t energy = _guest.Byte(At(regs.di, SLOT_ENERGY));
  _guest.SetByte(At(regs.di, SLOT_ENERGY), static_cast<std::uint8_t>(energy - Low(regs.ax)));
  if (energy >= Low(regs.ax))
  {
    return true;
  }
  if ((_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_INDESTRUCTIBLE) != 0)
  {
    _guest.SetByte(At(regs.di, SLOT_ENERGY), 0);
    IsStation(_guest);
    if (!FlagSet(_guest, FLAG_ZERO))
    {
      return true;
    }
    if (_guest.Get(DS.thargoidInvasionActive) != 1)
    {
      // Only an add that overflows cancels the docking computer.
      const unsigned status = _guest.Get(DS.legalStatus) + unsigned{LEGAL_STATUS_PER_STATION_HIT};
      _guest.Set(DS.legalStatus, static_cast<std::uint8_t>(status));
      if (status > 0xFF)
      {
        _guest.Set(DS.legalStatus, 0xFF);
        _guest.Call(CANCEL_DOCKING_COMPUTER);
      }
      return true;
    }
    _guest.Set(DS.invadedStationDestroyed, 1);
    _guest.Set(DS.thargoidInvasionActive, 0);
  }
  DestroyTarget(_guest);
  return false;
}

} // namespace

void DrawLaserSights(Guest& _guest)
{
  GetViewLaser(_guest);
  if (!FlagSet(_guest, FLAG_CARRY))
  {
    return;
  }
  Machine::Registers& regs = _guest.Regs();
  // MOV AH,AL / XOR AL,AL / SHR AX,1: 128 bytes a laser type.
  regs.ax = static_cast<std::uint16_t>(Low(regs.ax) << 7);
  regs.di = SIGHTS_OFFSET;
  regs.si = static_cast<std::uint16_t>(DS.laserSights.offset + regs.ax);
  for (regs.cx = SIGHTS_ROWS; regs.cx != 0; regs.cx = static_cast<std::uint16_t>(regs.cx - 1))
  {
    // The buffer's words are big-endian pixels: XCHG AH,AL round the AND and the OR.
    for (int half = 0; half < 2; ++half)
    {
      const std::uint16_t pixels = At(regs.di, 2 * half);
      const std::uint16_t andMask = _guest.Word(At(regs.si, 2 * half));
      const std::uint16_t orMask = _guest.Word(At(regs.si, 2 * half + 4));
      regs.ax = Swap(static_cast<std::uint16_t>((Swap(_guest.Word(pixels)) & andMask) | orMask));
      _guest.SetWord(pixels, regs.ax);
    }
    regs.di = At(regs.di, VIEW_ROW_BYTES);
    regs.si = At(regs.si, SIGHTS_ROW_BYTES);
  }
}

void GetViewLaser(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Get(DS.viewAngle);
  // MOV BL,AH / SHR BX,1 / AND BX,3: the view, which clears CF.
  regs.bx = static_cast<std::uint16_t>((Join(High(regs.bx), High(regs.ax)) >> 1) & 3);
  const std::uint8_t mount = _guest.Byte(DS.viewLaserMount.At(regs.bx));
  SetLow(regs.cx, mount);
  // RCR AL,CL through that clear CF, a bit at a time as the 8088 does: CF is bit CL-1 of laserMountsFitted.
  std::uint8_t fitted = _guest.Get(DS.laserMountsFitted);
  bool carry = false;
  for (unsigned count = mount; count != 0; --count)
  {
    const bool out = (fitted & 1) != 0;
    fitted = static_cast<std::uint8_t>((fitted >> 1) | (carry ? 0x80 : 0));
    carry = out;
  }
  SetLow(regs.ax, fitted);
  if (!carry)
  {
    _guest.SetFlag(FLAG_CARRY, false);
    return;
  }
  // DEC CL / SHL CL,1 / SHR AL,CL / AND AL,3: the mount's 2-bit field of laserMountTypes.
  const auto shift = static_cast<std::uint8_t>((mount - 1) * 2);
  SetLow(regs.cx, shift);
  const std::uint8_t types = _guest.Get(DS.laserMountTypes);
  SetLow(regs.ax, static_cast<std::uint8_t>((shift >= 8 ? 0 : types >> shift) & 3));
  _guest.SetFlag(FLAG_CARRY, true);
}

void DrawLaserBeams(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  NextRandom(_guest);
  regs.ax = static_cast<std::uint16_t>(regs.ax & BEAM_SCATTER_MASK);
  regs.ax = Join(static_cast<std::uint8_t>(High(regs.ax) + BEAM_TARGET_ROW), static_cast<std::uint8_t>(Low(regs.ax) + BEAM_TARGET_X));
  regs.dx = regs.ax;
  const std::uint8_t type = _guest.Get(DS.firingLaserType);
  if ((type & 1) != 0)
  {
    if ((type & 2) != 0)
    {
      // Type 3: all four, the inner pair in the toggled colour, the outer in the other.
      SetLow(regs.ax, _guest.Get(DS.laserBeamColorToggle));
      _guest.Set(DS.drawColor, Low(regs.ax));
      _guest.Set(DS.laserBeamColorToggle, static_cast<std::uint8_t>(_guest.Get(DS.laserBeamColorToggle) ^ BEAM_TOGGLE));
      DrawBeam(_guest, BEAM_INNER_LEFT, false);
      DrawBeam(_guest, BEAM_INNER_RIGHT, false);
      _guest.Set(DS.drawColor, static_cast<std::uint8_t>(_guest.Get(DS.drawColor) ^ BEAM_TOGGLE));
    }
    else
    {
      // Type 1: the outer pair.
      SetLow(regs.ax, NextBeamColor(_guest.Get(DS.laserBeamColor), true));
      _guest.Set(DS.laserBeamColor, Low(regs.ax));
      _guest.Set(DS.drawColor, Low(regs.ax));
    }
    DrawBeam(_guest, BEAM_OUTER_LEFT, false);
    DrawBeam(_guest, BEAM_OUTER_RIGHT, true);
    return;
  }
  // Types 0 and 2: one side a shot, alternately.
  SetLow(regs.ax, NextBeamColor(_guest.Get(DS.laserBeamColor), (type & 2) != 0));
  _guest.Set(DS.laserBeamColor, Low(regs.ax));
  _guest.Set(DS.drawColor, Low(regs.ax));
  const auto side = static_cast<std::uint8_t>(_guest.Get(DS.laserBeamSide) ^ 1);
  _guest.Set(DS.laserBeamSide, side);
  if (side != 0)
  {
    DrawBeam(_guest, BEAM_OUTER_LEFT, false);
    DrawBeam(_guest, BEAM_INNER_LEFT, true);
    return;
  }
  DrawBeam(_guest, BEAM_OUTER_RIGHT, false);
  DrawBeam(_guest, BEAM_INNER_RIGHT, true);
}

void ExplodeObject(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  TallyMaskMissionKill(_guest);
  if ((_guest.Byte(regs.di) & SLOT_DRAWN) == 0)
  {
    RemoveObject(_guest);
    return;
  }
  RemoveObject(_guest);
  _guest.Call(START_EXPLOSION_SOUND);
  _guest.Set(DS.explodingStation, 1);
  IsStation(_guest);
  if (!FlagSet(_guest, FLAG_ZERO))
  {
    _guest.Set(DS.explodingStation, 0);
  }
  // MOV CL,[DI+2Dh] / AND CX,0FFh.
  regs.cx = _guest.Byte(At(regs.di, SLOT_FRAGMENTS));
  if (regs.cx != 0)
  {
    SpawnFragments(_guest);
  }
  DropCargo(_guest);
}

void TallyMaskMissionKill(Guest& _guest)
{
  if (_guest.Get(DS.maskMissionShipsLeft) == 0)
  {
    return;
  }
  Machine::Registers& regs = _guest.Regs();
  SetLow(regs.ax, SlotType(_guest, regs.di));
  if (Low(regs.ax) != TYPE_ASP || _guest.Byte(At(regs.di, SLOT_BOUNTY)) != MASK_SHIP_BOUNTY)
  {
    return;
  }
  _guest.Set(DS.maskMissionShipsLeft, static_cast<std::uint8_t>(_guest.Get(DS.maskMissionShipsLeft) - 1));
  if ((_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_DEVICE) != 0)
  {
    _guest.Set(DS.maskShipDestroyed, 1);
  }
}

void TryFireLaserAtPlayer(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if ((_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_BLIP_DRAWN) == 0)
  {
    return;
  }
  regs.cx = regs.ax;
  NextRandom(_guest);
  if (Low(regs.ax) >= _guest.Byte(At(regs.di, SLOT_AGGRESSION)))
  {
    return;
  }
  CheckSafeZoneHoldFire(_guest);
  if (FlagSet(_guest, FLAG_CARRY) || FiringBlocked(_guest))
  {
    return;
  }
  // Each shot calms the ship by 5, unless that would take it below 20.
  SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Byte(At(regs.di, SLOT_AGGRESSION)) - CALMER_STEP));
  if (Low(regs.ax) >= CALMEST)
  {
    _guest.SetByte(At(regs.di, SLOT_AGGRESSION), Low(regs.ax));
  }
  // VectorWithinBox on AX = 0 and the aim errors in BX and CX.
  regs.ax = 0;
  regs.dx = LASER_GRAZE_BOX;
  _guest.Call(VECTOR_WITHIN_BOX);
  if (!FlagSet(_guest, FLAG_CARRY))
  {
    return;
  }
  _guest.Set(DS.playerHitBy, regs.di);
  _guest.Set(DS.playerHitPending, HIT_GRAZED);
  SetLow(regs.ax, _guest.Byte(At(regs.di, SLOT_VIEW_Z_HIGH)));
  _guest.Set(DS.playerHitByDepth, Low(regs.ax));
  regs.dx = LASER_HIT_BOX;
  _guest.Call(VECTOR_WITHIN_BOX);
  if (FlagSet(_guest, FLAG_CARRY))
  {
    _guest.Set(DS.playerHitPending, HIT_SQUARE);
  }
}

void TryLaunchMissileAtPlayer(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.killCount) < MISSILE_KILLS_NEEDED || (_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_HOSTILE) == 0)
  {
    return;
  }
  CheckSafeZoneHoldFire(_guest);
  if (FlagSet(_guest, FLAG_CARRY) || _guest.Byte(At(regs.di, SLOT_MISSILES)) == 0 || FiringBlocked(_guest))
  {
    return;
  }
  NextRandom(_guest);
  if (regs.ax >= regs.bx)
  {
    return;
  }
  SetLow(regs.dx, MISSILE_LAUNCH);
  _guest.Call(LAUNCH_SHIP_FROM_OBJECT);
  if (FlagSet(_guest, FLAG_CARRY))
  {
    _guest.SetByte(At(regs.di, SLOT_MISSILES), static_cast<std::uint8_t>(_guest.Byte(At(regs.di, SLOT_MISSILES)) - 1));
  }
}

void TryLaunchThargon(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  IsThargoidType(_guest);
  if (!FlagSet(_guest, FLAG_ZERO) || _guest.Byte(At(regs.di, SLOT_THARGONS)) == 0)
  {
    return;
  }
  NextRandom(_guest);
  if (regs.ax >= THARGON_ODDS)
  {
    return;
  }
  SetLow(regs.dx, THARGON_LAUNCH);
  _guest.Call(LAUNCH_SHIP_FROM_OBJECT);
  if (FlagSet(_guest, FLAG_CARRY))
  {
    _guest.SetByte(At(regs.di, SLOT_THARGONS), static_cast<std::uint8_t>(_guest.Byte(At(regs.di, SLOT_THARGONS)) - 1));
  }
}

void FindShipInCrosshairs(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = DS.stationSlot.offset;
  regs.cx = static_cast<std::uint8_t>(_guest.Get(DS.objectSlotCount) - 2);
  regs.bp = 0xFFFF; // the nearest z so far
  for (std::uint32_t count = LoopCount(regs.cx); count != 0; --count)
  {
    const std::uint8_t type = _guest.Byte(regs.di);
    SetLow(regs.bx, type);
    SetLow(regs.ax, static_cast<std::uint8_t>(type & (SLOT_DRAWN | SLOT_ACTIVE)));
    if (Low(regs.ax) == (SLOT_DRAWN | SLOT_ACTIVE))
    {
      SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Byte(At(regs.di, SLOT_FLAGS)) & CROSSHAIR_IGNORED));
      if (Low(regs.ax) != CROSSHAIR_IGNORED)
      {
        // The radius * 256 / z, plus 2, against |x| * 256 / z and |y| * 256 / z. The divides overflow into the trap.
        regs.bx = static_cast<std::uint16_t>(regs.bx & (TYPE_MASK << 1));
        regs.ax = Swap(_guest.Word(At(DS.shipTargetRadius.offset, regs.bx)));
        regs.dx = 0;
        DivideWord(_guest, _guest.Word(At(regs.di, SLOT_VIEW_Z)));
        regs.bx = static_cast<std::uint16_t>(regs.ax + 2);
        if (ScaledWithinRadius(_guest, SLOT_VIEW_X) && ScaledWithinRadius(_guest, SLOT_VIEW_Y) &&
            _guest.Word(At(regs.di, SLOT_VIEW_Z)) < regs.bp)
        {
          regs.si = regs.di;
          regs.bp = _guest.Word(At(regs.di, SLOT_VIEW_Z));
        }
      }
    }
    regs.di = At(regs.di, SLOT_BYTES);
  }
  regs.cx = 0;
  regs.bp = static_cast<std::uint16_t>(regs.bp + 1);
  if (regs.bp == 0)
  {
    _guest.SetFlag(FLAG_CARRY, false);
    return;
  }
  regs.di = regs.si;
  _guest.SetFlag(FLAG_CARRY, true);
}

void ResolveLaserFire(Guest& _guest)
{
  if (_guest.Get(DS.laserFiring) != 1)
  {
    return;
  }
  FindShipInCrosshairs(_guest);
  if (FlagSet(_guest, FLAG_CARRY) && !HitTarget(_guest))
  {
    return;
  }
  DrawLaserBeams(_guest);
  _guest.Call(START_LASER_SOUND);
  _guest.Set(DS.miningLaserOnAsteroid, 0);
  _guest.Set(DS.laserFiring, 0);
}

void CheckMissileTargetDestroyed(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.missileState) != MISSILE_LOCKED || regs.di != _guest.Get(DS.missileTarget))
  {
    return;
  }
  regs.ax = DS.missileTargetDestroyedMessage.offset;
  _guest.Set(DS.messagePointer, regs.ax);
  _guest.Set(DS.messageFrames, MISSILE_MESSAGE_FRAMES);
  _guest.Set(DS.missileState, 0);
}

void CreditKill(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetLow(regs.ax, _guest.Byte(At(regs.di, SLOT_BOUNTY)));
  if (Low(regs.ax) == 0)
  {
    return;
  }
  // INC, then CMP 0FFh: a count of FFh becomes FEh, and one that wraps to 0 stays there.
  const auto kills = static_cast<std::uint8_t>(_guest.Get(DS.killCount) + 1);
  _guest.Set(DS.killCount, kills == 0xFF ? MOST_KILLS : kills);
  if (Low(regs.ax) != NO_BOUNTY_TYPE)
  {
    regs.ax = Low(regs.ax);
    PayBounty(_guest);
    return;
  }
  SetLow(regs.ax, 0); // INC AL
  IsThargoidType(_guest);
  regs.ax = THARGOID_BOUNTY;
  if (FlagSet(_guest, FLAG_ZERO))
  {
    PayBounty(_guest);
    return;
  }
  // Killing what carries no bounty is a crime: 4 for a police Viper anywhere, 2 for anything else inside the safe zone.
  _guest.Call(IN_SAFE_ZONE);
  const bool inside = FlagSet(_guest, FLAG_CARRY);
  IsPoliceViper(_guest);
  const bool police = FlagSet(_guest, FLAG_ZERO);
  if (!inside && !police)
  {
    return;
  }
  SetLow(regs.ax, police ? CRIME_AGAINST_POLICE : CRIME);
  AddSaturating(_guest, DS.legalStatus.offset, Low(regs.ax));
}

void ApplyEnemyLaserHit(Guest& _guest)
{
  if (_guest.Get(DS.playerHitPending) == 0)
  {
    return;
  }
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(START_PLAYER_HIT_SOUND);
  regs.di = _guest.Get(DS.playerHitBy);
  if ((_guest.Byte(regs.di) & SLOT_DRAWN) != 0)
  {
    // A beam from the attacker on screen to a random point on an edge of the view.
    regs.ax = _guest.Word(At(regs.di, SLOT_VIEW_X));
    regs.bx = _guest.Word(At(regs.di, SLOT_VIEW_Y));
    regs.cx = _guest.Word(At(regs.di, SLOT_VIEW_Z));
    _guest.Call(PROJECT_TO_SCREEN);
    regs.dx = regs.ax;
    NextRandom(_guest);
    if (regs.ax < EDGE_LEFT_BELOW)
    {
      regs.cx = Low(regs.ax);
      regs.ax = 0;
    }
    else if (regs.ax < EDGE_RIGHT_BELOW)
    {
      regs.cx = Low(regs.ax);
      regs.ax = EDGE_ROW_MASK;
    }
    else if (regs.ax < EDGE_TOP_BELOW)
    {
      regs.ax = static_cast<std::uint16_t>(regs.ax & EDGE_ROW_MASK);
      regs.cx = 0;
    }
    else
    {
      regs.ax = static_cast<std::uint16_t>(regs.ax & EDGE_ROW_MASK);
      regs.cx = 0xFF;
    }
    _guest.Set(DS.drawColor, ENEMY_BEAM_COLOR);
    _guest.Call(DRAW_CLIPPED_LINE);
  }
  _guest.Set(DS.playerHitPending, 0);
  // Bit 7 of the attacker's depth byte: behind, so the aft shield takes it.
  const bool aft = (_guest.Get(DS.playerHitByDepth) & 0x80) != 0;
  SetLow(regs.ax, SHIELD_HIT);
  const DataField<std::uint8_t> shield = aft ? DS.aftShield : DS.foreShield;
  const std::uint8_t strength = _guest.Get(shield);
  if (strength >= SHIELD_HIT)
  {
    _guest.Set(shield, static_cast<std::uint8_t>(strength - SHIELD_HIT));
    return;
  }
  // What the shield could not take comes off the energy: NEG AL / CBW on the shield's wrapped value.
  _guest.Set(shield, 0);
  SetLow(regs.ax, static_cast<std::uint8_t>(0u - static_cast<std::uint8_t>(strength - SHIELD_HIT)));
  regs.ax = SignExtend(Low(regs.ax));
  const std::uint16_t energy = _guest.Get(DS.playerEnergy);
  if (energy >= regs.ax)
  {
    _guest.Set(DS.playerEnergy, static_cast<std::uint16_t>(energy - regs.ax));
    return;
  }
  _guest.Set(DS.playerEnergy, 0);
  _guest.Set(DS.playerDead, 1);
}

namespace
{

using Machine::REGISTER_ALL;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract CLOBBERS_ALL{REGISTER_ALL, 0};
constexpr Machine::NativeContract LAUNCHES{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_BP | REGISTER_ES,
                                           0};

constexpr std::array ENTRIES = {
  NativeEntry{0x0630, "DrawLaserSights", &DrawLaserSights,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_DI, 0}},
  NativeEntry{0x066C, "GetViewLaser", &GetViewLaser, Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX, FLAG_CARRY}},
  NativeEntry{0x0A9A, "DrawLaserBeams", &DrawLaserBeams,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP | REGISTER_DI, 0}},
  NativeEntry{0x4FC1, "ExplodeObject", &ExplodeObject, CLOBBERS_ALL},
  NativeEntry{0x50FE, "TallyMaskMissionKill", &TallyMaskMissionKill, PRESERVES_ALL},
  NativeEntry{0x518B, "TryFireLaserAtPlayer", &TryFireLaserAtPlayer,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0}},
  NativeEntry{0x543A, "TryLaunchMissileAtPlayer", &TryLaunchMissileAtPlayer, LAUNCHES},
  NativeEntry{0x5471, "TryLaunchThargon", &TryLaunchThargon, LAUNCHES},
  NativeEntry{0x8A46, "FindShipInCrosshairs", &FindShipInCrosshairs,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP | REGISTER_SI, FLAG_CARRY}},
  NativeEntry{0x8AC2, "ResolveLaserFire", &ResolveLaserFire, CLOBBERS_ALL},
  NativeEntry{0x8B8B, "CheckMissileTargetDestroyed", &CheckMissileTargetDestroyed, PRESERVES_ALL},
  NativeEntry{0x8BC6, "CreditKill", &CreditKill, PRESERVES_ALL},
  NativeEntry{0x8C8E, "ApplyEnemyLaserHit", &ApplyEnemyLaserHit, CLOBBERS_ALL},
};

} // namespace

std::span<const NativeEntry> CombatEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
