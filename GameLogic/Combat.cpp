#include "pch.h"

#include "Combat.h"

#include "Ai.h"
#include "Arithmetic.h"
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
constexpr std::uint16_t COMPUTE_DEATH_DEBRIS_VECTOR = 0x2F8B;
constexpr std::uint16_t SHOW_BOUNTY_MESSAGE = 0x3626;
constexpr std::uint16_t ROTATE_ROLL_YAW_PITCH = 0x3EC7;
constexpr std::uint16_t ADD_CREDITS = 0x65EE;
constexpr std::uint16_t START_IMPACT_SOUND = 0x7AC3;
constexpr std::uint16_t START_EXPLOSION_SOUND = 0x7AFC;
constexpr std::uint16_t START_PLAYER_DEATH_SOUND = 0x7B09;
constexpr std::uint16_t START_LASER_SOUND = 0x7B71;
constexpr std::uint16_t START_PLAYER_HIT_SOUND = 0x7B96;
constexpr std::uint16_t CANCEL_DOCKING_COMPUTER = 0x8BAA;
constexpr std::uint16_t PROJECT_TO_SCREEN = 0x8D2E;

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

// What else a launch can put in DL, besides MISSILE_LAUNCH and THARGON_LAUNCH.
constexpr std::uint8_t ESCAPE_POD_LAUNCH = 0x15;
constexpr std::uint8_t KRAIT_LAUNCH = 5;
constexpr std::size_t MISSILE_TEMPLATE = 0; // spawnTemplates' first record
constexpr std::uint8_t MISSILE_CLASS = 2;
constexpr std::uint16_t MISSILE_LAUNCH_DISTANCE = 100; // the player's missile starts this far along the nose
constexpr std::uint16_t MISSILE_SPIN = 0x28;
constexpr std::uint16_t MISSILE_HIT_BOX = 0xC8;
constexpr std::uint16_t MISSILE_DAMAGE = 0x320;
constexpr std::uint8_t MISSILE_AT_STATION_CRIME = 5;
constexpr std::uint8_t INVADED_STATION_HIT = 0x0A;

constexpr std::uint16_t FIRST_SHIP_SLOT = 3; // the sun, the planet and the station come first
constexpr std::uint8_t LEGAL_STATUS_PER_BOMB = 0x28;
constexpr std::uint16_t OVERWHELMING_DAMAGE = 0x100; // from here the shield takes what it holds and the energy the rest
constexpr std::uint16_t MASKING_ENERGY = 0x0C;
constexpr std::uint8_t MASKING_COLOR = 9;
constexpr std::uint8_t MASKING_CALMING = 2;

// The wreck the player leaves: six splinters drifting with it, 40 units along the nose a frame, then a barrel of the cargo.
constexpr std::uint16_t WRECK_SPEED = 8;
constexpr std::uint16_t WRECK_SPLINTERS = 6;
constexpr std::uint8_t WRECK_LIFETIME = 0x32;
constexpr std::uint8_t SPLINTER_SCATTER_MASK = 0x1F; // a random -15..16 on each axis
constexpr std::uint8_t SPLINTER_SCATTER_CENTER = 0x0F;
constexpr std::uint8_t BARREL_SCATTER_MASK = 0x0F; // a random -7..8
constexpr std::uint8_t BARREL_SCATTER_CENTER = 0x07;
constexpr std::uint16_t BARREL_OFFSET_MASK = 0x3F; // a random -31..32, the same on x and y
constexpr std::uint16_t BARREL_OFFSET_CENTER = 0x1F;

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

// SAR r/m8,1.
[[nodiscard]] std::uint8_t HalveSigned(std::uint8_t _value) noexcept
{
  return static_cast<std::uint8_t>(static_cast<std::int8_t>(_value) >> 1);
}

[[nodiscard]] std::uint8_t SlotType(const Guest& _guest, std::uint16_t _slot) noexcept
{
  return static_cast<std::uint8_t>((_guest.Byte(_slot) >> 1) & TYPE_MASK);
}

void AddWord(Guest& _guest, std::uint16_t _offset, std::uint16_t _value) noexcept
{
  _guest.SetWord(_offset, static_cast<std::uint16_t>(_guest.Word(_offset) + _value));
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

// gameOverFrames | escapePodFrames | maskingBackgroundColor, which the original ORs into AL: none of them may run when a ship
// fires, so it may not unless this is 0.
[[nodiscard]] std::uint8_t FiringBlocked(const GameState& _state) noexcept
{
  return static_cast<std::uint8_t>(_state.Get(DS.gameOverFrames) | _state.Get(DS.escapePodFrames) | _state.Get(DS.maskingBackgroundColor));
}

// MOV AL,[DI] / SHR AL,1 / AND AL,1Fh.
[[nodiscard]] std::uint8_t TypeOf(const ObjectSlot& _slot) noexcept
{
  return static_cast<std::uint8_t>((_slot.Get(SlotByte::Type) >> 1) & TYPE_MASK);
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
  Routine8C51Entry(_guest);
  if (!_guest.Flag(FLAG_ZERO))
  {
    return;
  }
  const std::uint8_t repair = _guest.Flag(FLAG_CARRY) ? THARGOID_REPAIR : THARGON_REPAIR;
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
    FindDebrisSlotEntry(_guest);
    std::swap(regs.si, regs.di);
    CopyObjectEntry(_guest);
    const std::uint16_t debris = regs.di;
    _guest.SetByte(At(debris, SLOT_STATE), 0);
    NextRandomEntry(_guest);
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
      NextRandomEntry(_guest);
      if (regs.ax < MINERALS_ODDS)
      {
        OrByte(_guest, At(debris, SLOT_FLAGS), FLAG_MINERALS);
      }
    }
    _guest.SetByte(At(debris, SLOT_ENERGY), 0);
    _guest.SetWord(At(debris, SLOT_CARGO), 0);
    _guest.SetByte(At(debris, SLOT_AGE), 0);
    _guest.SetByte(At(debris, SLOT_CLASS), DEBRIS_CLASS);
    NextRandomEntry(_guest);
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
    NextRandomEntry(_guest);
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
    FindFreeShipSlotEntry(_guest);
    std::swap(regs.di, regs.si);
    if (_guest.Flag(FLAG_CARRY))
    {
      SetLow(regs.ax, _guest.Byte(At(regs.si, SLOT_FLAGS)));
      const std::uint16_t flags = regs.ax;
      CopyObjectEntry(_guest);
      InitCargoBarrelEntry(_guest);
      _guest.SetByte(At(regs.di, SLOT_FLAGS), FLAG_RESTING);
      regs.ax = flags;
      // The device's bit (5) becomes the barrel's bit 6.
      SetLow(regs.ax, static_cast<std::uint8_t>((Low(regs.ax) & FLAG_DEVICE) << 1));
      OrByte(_guest, At(regs.di, SLOT_FLAGS), Low(regs.ax));
      NextRandomEntry(_guest);
      _guest.SetWord(At(regs.di, SLOT_PITCH), regs.ax);
      regs.ax = Swap(regs.ax);
      _guest.SetWord(At(regs.di, SLOT_YAW), regs.ax);
      ComputeVelocityEntry(_guest);
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
  CheckMissileTargetDestroyedEntry(_guest);
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
  IsStationEntry(_guest);
  regs.ax = damage;
  if (_guest.Flag(FLAG_ZERO))
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
    IsStationEntry(_guest);
    if (!_guest.Flag(FLAG_ZERO))
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

// AND AL,_mask / SUB AL,_center / CBW / ADD AX,_drift: a random velocity about the wreck's drift on one axis.
[[nodiscard]] std::uint16_t WreckScatter(std::uint8_t _random, std::uint8_t _mask, std::uint8_t _center, std::uint16_t _drift) noexcept
{
  return static_cast<std::uint16_t>(SignExtend(static_cast<std::uint8_t>((_random & _mask) - _center)) + _drift);
}

} // namespace

void DrawLaserSights(GameState& _state)
{
  const std::optional<std::uint8_t> laser = GetViewLaser(_state);
  if (!laser.has_value())
  {
    return;
  }
  // MOV AH,AL / XOR AL,AL / SHR AX,1: 128 bytes a laser type.
  std::uint16_t row = SIGHTS_OFFSET;
  auto sprite = static_cast<std::uint16_t>(DS.laserSights.offset + (*laser << 7));
  for (std::uint16_t rows = SIGHTS_ROWS; rows != 0; --rows)
  {
    // The buffer's words are big-endian pixels: XCHG AH,AL round the AND and the OR.
    for (int half = 0; half < 2; ++half)
    {
      const std::uint16_t pixels = At(row, 2 * half);
      const std::uint16_t andMask = _state.Word(At(sprite, 2 * half));
      const std::uint16_t orMask = _state.Word(At(sprite, 2 * half + 4));
      _state.SetWord(pixels, Swap(static_cast<std::uint16_t>((Swap(_state.Word(pixels)) & andMask) | orMask)));
    }
    row = At(row, VIEW_ROW_BYTES);
    sprite = At(sprite, SIGHTS_ROW_BYTES);
  }
}

std::optional<std::uint8_t> GetViewLaser(const GameState& _state)
{
  // MOV BL,AH / SHR BX,1 / AND BX,3: the view, which clears CF.
  const auto view = static_cast<std::uint16_t>((High(_state.Get(DS.viewAngle)) >> 1) & 3);
  const std::uint8_t mount = _state.Byte(DS.viewLaserMount.At(view));
  // RCR AL,CL through that clear CF, a bit at a time as the 8088 does: CF is bit CL-1 of laserMountsFitted.
  std::uint8_t fitted = _state.Get(DS.laserMountsFitted);
  bool carry = false;
  for (unsigned count = mount; count != 0; --count)
  {
    const bool out = (fitted & 1) != 0;
    fitted = static_cast<std::uint8_t>((fitted >> 1) | (carry ? 0x80 : 0));
    carry = out;
  }
  if (!carry)
  {
    return std::nullopt;
  }
  // DEC CL / SHL CL,1 / SHR AL,CL / AND AL,3: the mount's 2-bit field of laserMountTypes.
  const auto shift = static_cast<std::uint8_t>((mount - 1) * 2);
  const std::uint8_t types = _state.Get(DS.laserMountTypes);
  return static_cast<std::uint8_t>((shift >= 8 ? 0 : types >> shift) & 3);
}

void DrawLaserBeams(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  NextRandomEntry(_guest);
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
  TallyMaskMissionKillEntry(_guest);
  if ((_guest.Byte(regs.di) & SLOT_DRAWN) == 0)
  {
    RemoveObject(_guest);
    return;
  }
  RemoveObject(_guest);
  _guest.Call(START_EXPLOSION_SOUND);
  _guest.Set(DS.explodingStation, 1);
  IsStationEntry(_guest);
  if (!_guest.Flag(FLAG_ZERO))
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

void TallyMaskMissionKill(GameState& _state, const ObjectSlot& _slot)
{
  if (_state.Get(DS.maskMissionShipsLeft) == 0)
  {
    return;
  }
  if (TypeOf(_slot) != TYPE_ASP || _slot.Get(SlotByte::Bounty) != MASK_SHIP_BOUNTY)
  {
    return;
  }
  _state.Set(DS.maskMissionShipsLeft, static_cast<std::uint8_t>(_state.Get(DS.maskMissionShipsLeft) - 1));
  if ((_slot.Get(SlotByte::Flags) & FLAG_DEVICE) != 0)
  {
    _state.Set(DS.maskShipDestroyed, 1);
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
  NextRandomEntry(_guest);
  if (Low(regs.ax) >= _guest.Byte(At(regs.di, SLOT_AGGRESSION)))
  {
    return;
  }
  CheckSafeZoneHoldFireEntry(_guest);
  if (_guest.Flag(FLAG_CARRY))
  {
    return;
  }
  SetLow(regs.ax, FiringBlocked(_guest.State()));
  if (Low(regs.ax) != 0)
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
  if (!_guest.Flag(FLAG_CARRY))
  {
    return;
  }
  _guest.Set(DS.playerHitBy, regs.di);
  _guest.Set(DS.playerHitPending, HIT_GRAZED);
  SetLow(regs.ax, _guest.Byte(At(regs.di, SLOT_VIEW_Z_HIGH)));
  _guest.Set(DS.playerHitByDepth, Low(regs.ax));
  regs.dx = LASER_HIT_BOX;
  _guest.Call(VECTOR_WITHIN_BOX);
  if (_guest.Flag(FLAG_CARRY))
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
  CheckSafeZoneHoldFireEntry(_guest);
  if (_guest.Flag(FLAG_CARRY) || _guest.Byte(At(regs.di, SLOT_MISSILES)) == 0)
  {
    return;
  }
  SetLow(regs.ax, FiringBlocked(_guest.State()));
  if (Low(regs.ax) != 0)
  {
    return;
  }
  NextRandomEntry(_guest);
  if (regs.ax >= regs.bx)
  {
    return;
  }
  SetLow(regs.dx, MISSILE_LAUNCH);
  LaunchShipFromObject(_guest);
  if (_guest.Flag(FLAG_CARRY))
  {
    _guest.SetByte(At(regs.di, SLOT_MISSILES), static_cast<std::uint8_t>(_guest.Byte(At(regs.di, SLOT_MISSILES)) - 1));
  }
}

void TryLaunchThargon(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  IsThargoidTypeEntry(_guest);
  if (!_guest.Flag(FLAG_ZERO) || _guest.Byte(At(regs.di, SLOT_THARGONS)) == 0)
  {
    return;
  }
  NextRandomEntry(_guest);
  if (regs.ax >= THARGON_ODDS)
  {
    return;
  }
  SetLow(regs.dx, THARGON_LAUNCH);
  LaunchShipFromObject(_guest);
  if (_guest.Flag(FLAG_CARRY))
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
  if (_guest.Flag(FLAG_CARRY) && !HitTarget(_guest))
  {
    return;
  }
  DrawLaserBeams(_guest);
  _guest.Call(START_LASER_SOUND);
  _guest.Set(DS.miningLaserOnAsteroid, 0);
  _guest.Set(DS.laserFiring, 0);
}

bool CheckMissileTargetDestroyed(GameState& _state, std::uint16_t _slot)
{
  if (_state.Get(DS.missileState) != MISSILE_LOCKED || _slot != _state.Get(DS.missileTarget))
  {
    return false;
  }
  _state.Set(DS.messagePointer, DS.missileTargetDestroyedMessage.offset);
  _state.Set(DS.messageFrames, MISSILE_MESSAGE_FRAMES);
  _state.Set(DS.missileState, 0);
  return true;
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
  IsThargoidTypeEntry(_guest);
  regs.ax = THARGOID_BOUNTY;
  if (_guest.Flag(FLAG_ZERO))
  {
    PayBounty(_guest);
    return;
  }
  // Killing what carries no bounty is a crime: 4 for a police Viper anywhere, 2 for anything else inside the safe zone.
  _guest.Call(IN_SAFE_ZONE);
  const bool inside = _guest.Flag(FLAG_CARRY);
  IsPoliceViperEntry(_guest);
  const bool police = _guest.Flag(FLAG_ZERO);
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
    NextRandomEntry(_guest);
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

void TakeDamage(Guest& _guest)
{
  if (_guest.Get(DS.escapePodFrames) != 0)
  {
    return;
  }
  Machine::Registers& regs = _guest.Regs();
  if (regs.ax >= OVERWHELMING_DAMAGE)
  {
    regs.bx = _guest.Get(DS.foreShield);
    regs.ax = static_cast<std::uint16_t>(regs.ax - regs.bx);
    _guest.Set(DS.foreShield, 0);
  }
  else
  {
    // MOV AH,AL / MOV AL,foreShield / SUB AL,AH: only the fore shield takes it, and what it cannot comes off the energy.
    const std::uint8_t damage = Low(regs.ax);
    const std::uint8_t shield = _guest.Get(DS.foreShield);
    regs.ax = Join(damage, static_cast<std::uint8_t>(shield - damage));
    _guest.Set(DS.foreShield, Low(regs.ax));
    if (shield >= damage)
    {
      return;
    }
    _guest.Set(DS.foreShield, 0);
    // NEG AL / CBW: the excess, sign-extended.
    regs.ax = SignExtend(Negate(Low(regs.ax)));
  }
  const std::uint16_t energy = _guest.Get(DS.playerEnergy);
  _guest.Set(DS.playerEnergy, static_cast<std::uint16_t>(energy - regs.ax));
  if (energy >= regs.ax)
  {
    return;
  }
  KillPlayer(_guest);
  _guest.Set(DS.playerEnergy, 0);
}

void DetonateEnergyBomb(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(IN_SAFE_ZONE);
  if (_guest.Flag(FLAG_CARRY))
  {
    AddSaturating(_guest, DS.legalStatus.offset, LEGAL_STATUS_PER_BOMB);
  }
  // Every active ship with a blip explodes, its cargo emptied first.
  regs.di = DS.firstShipSlot.offset;
  regs.cx = static_cast<std::uint8_t>(_guest.Get(DS.objectSlotCount) - FIRST_SHIP_SLOT);
  do
  {
    if ((_guest.Byte(regs.di) & SLOT_ACTIVE) != 0 && (_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_BLIP_DRAWN) != 0)
    {
      _guest.SetByte(At(regs.di, SLOT_CARGO), 0);
      const std::uint16_t slot = regs.di;
      const std::uint16_t remaining = regs.cx;
      ExplodeObject(_guest);
      regs.cx = remaining;
      regs.di = slot;
    }
    regs.di = At(regs.di, SLOT_BYTES);
    regs.cx = static_cast<std::uint16_t>(regs.cx - 1);
  } while (regs.cx != 0);
}

void SpawnPlayerWreckage(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(COMPUTE_DEATH_DEBRIS_VECTOR);
  _guest.Set(DS.wreckDriftX, regs.ax);
  _guest.Set(DS.wreckDriftY, regs.bx);
  _guest.Set(DS.wreckDriftZ, regs.cx);
  _guest.Set(DS.playerVelocityX, 0);
  _guest.Set(DS.playerVelocityY, 0);
  _guest.Set(DS.playerVelocityZ, 0);
  _guest.Set(DS.playerSpeed, WRECK_SPEED);
  _guest.Set(DS.velocityDirty, 1);
  _guest.Set(DS.viewLocked, 1);
  regs.cx = WRECK_SPLINTERS;
  do
  {
    const std::uint16_t splinters = regs.cx;
    FindDebrisSlotEntry(_guest);
    ClearObjectSlotEntry(_guest);
    _guest.SetByte(At(regs.di, SLOT_STATE), 0);
    NextRandomEntry(_guest);
    _guest.SetWord(At(regs.di, SLOT_SPIN_ROLL), regs.ax);
    regs.bx = regs.ax;
    // Each velocity byte from its own bits of the random word: x from BL, y from BH, z from BL shifted right twice.
    regs.ax = WreckScatter(Low(regs.bx), SPLINTER_SCATTER_MASK, SPLINTER_SCATTER_CENTER, _guest.Get(DS.wreckDriftX));
    _guest.SetByte(At(regs.di, SLOT_VELOCITY), Low(regs.ax));
    regs.ax = WreckScatter(High(regs.bx), SPLINTER_SCATTER_MASK, SPLINTER_SCATTER_CENTER, _guest.Get(DS.wreckDriftY));
    _guest.SetByte(At(regs.di, SLOT_VELOCITY + 1), Low(regs.ax));
    regs.ax = WreckScatter(static_cast<std::uint8_t>(Low(regs.bx) >> 2), SPLINTER_SCATTER_MASK, SPLINTER_SCATTER_CENTER,
                           _guest.Get(DS.wreckDriftZ));
    _guest.SetByte(At(regs.di, SLOT_VELOCITY + 2), Low(regs.ax));
    _guest.SetByte(At(regs.di, SLOT_CLASS), DEBRIS_CLASS);
    _guest.SetByte(At(regs.di, SLOT_LIFETIME), WRECK_LIFETIME);
    SetLow(regs.ax, SPLINTER_ACTIVE);
    _guest.SetByte(regs.di, SPLINTER_ACTIVE);
    UpdateDebrisAi(_guest);
    regs.cx = static_cast<std::uint16_t>(splinters - 1);
  } while (regs.cx != 0);
  if (_guest.Get(DS.cargoUsedTonnes) == 0)
  {
    return;
  }
  // The cargo's barrel: four drifts ahead, drifting at a quarter of the drift and a little, turned to face along it.
  FindFreeShipSlotEntry(_guest);
  if (!_guest.Flag(FLAG_CARRY))
  {
    ReclaimShipSlot(_guest);
  }
  ClearObjectSlotEntry(_guest);
  const std::array<DataField<std::uint16_t>, 3> drift = {DS.wreckDriftX, DS.wreckDriftY, DS.wreckDriftZ};
  for (int axis = 0; axis < 3; ++axis)
  {
    const std::uint16_t axisDrift = _guest.Get(drift[static_cast<std::size_t>(axis)]);
    NextRandomEntry(_guest);
    regs.ax = Sar(WreckScatter(Low(regs.ax), BARREL_SCATTER_MASK, BARREL_SCATTER_CENTER, axisDrift), 2);
    _guest.SetByte(At(regs.di, SLOT_VELOCITY + axis), Low(regs.ax));
    regs.ax = static_cast<std::uint16_t>(axisDrift << 2);
    regs.dx = SignWord(regs.ax);
    _guest.SetWord(At(regs.di, SLOT_X + 2 * axis), regs.ax);
    _guest.SetByte(At(regs.di, SLOT_X_HIGH + axis), Low(regs.dx));
  }
  InitCargoBarrelEntry(_guest);
  GetObjectPositionEntry(_guest);
  ConvertVectorToAnglesEntry(_guest);
  _guest.SetWord(At(regs.di, SLOT_PITCH), regs.ax);
  _guest.SetWord(At(regs.di, SLOT_YAW), regs.bx);
  // The same random -31..32 added to x and to y.
  NextRandomEntry(_guest);
  regs.bx = regs.ax;
  regs.ax = static_cast<std::uint16_t>((regs.ax & BARREL_OFFSET_MASK) - BARREL_OFFSET_CENTER);
  regs.dx = SignWord(regs.ax);
  AddToCoordinate(ObjectSlot(_guest.State(), regs.di), 0, static_cast<std::int16_t>(regs.ax));
  regs.bx = static_cast<std::uint16_t>((regs.bx & BARREL_OFFSET_MASK) - BARREL_OFFSET_CENTER);
  regs.ax = regs.bx;
  regs.dx = SignWord(regs.ax);
  AddToCoordinate(ObjectSlot(_guest.State(), regs.di), 1, static_cast<std::int16_t>(regs.ax));
}

void KillPlayer(Guest& _guest)
{
  if (_guest.Get(DS.escapePodFrames) != 0)
  {
    return;
  }
  _guest.Set(DS.playerDead, 1);
  _guest.Call(START_PLAYER_DEATH_SOUND);
}

void InitMissile(GameState& _state, ObjectSlot _slot)
{
  InitObjectFromTemplate(_state, _slot, DS.spawnTemplates.At(MISSILE_TEMPLATE), 0);
  _slot.Set(SlotByte::Class, MISSILE_CLASS);
}

void RemoveAllMissiles(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = DS.shipSlots.offset;
  regs.cx = _guest.Get(DS.objectSlotCount);
  do
  {
    // MOV AL,[DI] / SHR AL,1: the active bit falls into CF, and AL keeps the type above it.
    const std::uint8_t first = _guest.Byte(regs.di);
    SetLow(regs.ax, static_cast<std::uint8_t>(first >> 1));
    if ((first & SLOT_ACTIVE) != 0)
    {
      SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & TYPE_MASK));
      if (Low(regs.ax) == TYPE_MISSILE)
      {
        const std::uint16_t remaining = regs.cx;
        const std::uint16_t slot = regs.di;
        RemoveObject(_guest);
        regs.di = slot;
        regs.cx = remaining;
      }
    }
    regs.di = At(regs.di, SLOT_BYTES);
    regs.cx = static_cast<std::uint16_t>(regs.cx - 1);
  } while (regs.cx != 0);
}

void LaunchPlayerMissile(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  FindFreeShipSlotEntry(_guest);
  if (!_guest.Flag(FLAG_CARRY))
  {
    ReclaimShipSlot(_guest);
  }
  // A copy of the 64 bytes at the caller's DI, made a missile.
  std::swap(regs.si, regs.di);
  CopyObjectEntry(_guest);
  SetLow(regs.dx, MISSILE_LAUNCH);
  InitMissileEntry(_guest);
  // 100 along the player's nose: (0, 100, 0) turned by the player's angles, negated.
  regs.ax = Negate(_guest.Get(DS.playerPitchAngle));
  SetSinCosEntry(_guest, DS.rotationSinCos.At(0));
  regs.ax = Negate(_guest.Get(DS.playerYawAngle));
  SetSinCosEntry(_guest, DS.rotationSinCos.At(1));
  regs.ax = Negate(_guest.Get(DS.playerRollAngle));
  SetSinCosEntry(_guest, DS.rotationSinCos.At(2));
  regs.ax = 0;
  regs.bx = MISSILE_LAUNCH_DISTANCE;
  regs.cx = 0;
  _guest.Call(ROTATE_ROLL_YAW_PITCH);
  const std::array<std::uint16_t, 3> position = {regs.ax, regs.bx, regs.cx};
  for (int axis = 0; axis < 3; ++axis)
  {
    regs.ax = position[static_cast<std::size_t>(axis)];
    _guest.SetWord(At(regs.di, SLOT_X + 2 * axis), regs.ax);
    regs.dx = SignWord(regs.ax);
    _guest.SetByte(At(regs.di, SLOT_X_HIGH + axis), Low(regs.dx));
  }
  // Locked on missileTarget, and aimed at where it is.
  regs.si = _guest.Get(DS.missileTarget);
  _guest.SetWord(At(regs.di, SLOT_TARGET), regs.si);
  regs.ax = _guest.Word(At(regs.si, SLOT_X));
  regs.bx = _guest.Word(At(regs.si, SLOT_Y));
  regs.cx = _guest.Word(At(regs.si, SLOT_Z));
  ConvertVectorToAnglesEntry(_guest);
  _guest.SetWord(At(regs.di, SLOT_PITCH), regs.ax);
  _guest.SetWord(At(regs.di, SLOT_YAW), regs.bx);
  ComputeVelocityEntry(_guest);
  MoveObject(_guest);
  MoveObject(_guest);
}

void LaunchShipFromObject(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  FindFreeShipSlotEntry(_guest);
  if (!_guest.Flag(FLAG_CARRY))
  {
    return;
  }
  // A copy of the launcher in the free slot, made what DL names, and moved clear of it.
  const std::uint16_t launcher = regs.di;
  std::swap(regs.si, regs.di);
  switch (Low(regs.dx))
  {
  case MISSILE_LAUNCH:
    CopyObjectEntry(_guest);
    InitMissileEntry(_guest);
    MoveObject(_guest);
    MoveObject(_guest);
    MoveObject(_guest);
    _guest.SetWord(At(regs.di, SLOT_TARGET), 0); // at the player
    break;
  case ESCAPE_POD_LAUNCH:
    CopyObjectEntry(_guest);
    InitEscapePodEntry(_guest);
    RandomizeOrientationEntry(_guest);
    ComputeVelocityEntry(_guest);
    MoveObject(_guest);
    MoveObject(_guest);
    MoveObject(_guest);
    break;
  case THARGON_LAUNCH:
    CopyObjectEntry(_guest);
    InitThargonEntry(_guest);
    ComputeVelocityEntry(_guest);
    MoveObject(_guest);
    MoveObject(_guest);
    regs.si = launcher;
    _guest.SetWord(At(regs.di, SLOT_OWNER), regs.si);
    break;
  case KRAIT_LAUNCH:
    CopyObjectEntry(_guest);
    InitKraitHunterEntry(_guest);
    ComputeVelocityEntry(_guest);
    MoveObject(_guest);
    MoveObject(_guest);
    // POP DI takes the launcher this path pushed, and RET the one pushed before it: the launch returns to CS:launcher, which
    // is not code. Only the split at 56B1, which never runs, passes DL=5.
    regs.di = launcher;
    _guest.SetFlag(FLAG_CARRY, true);
    _guest.Push(launcher);
    return;
  default:
    regs.di = launcher;
    _guest.SetFlag(FLAG_CARRY, false);
    return;
  }
  regs.di = launcher;
  _guest.SetFlag(FLAG_CARRY, true);
}

void UpdateMissileAi(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.incomingMissileAlert, 0);
  AddWord(_guest, At(regs.di, SLOT_ROLL), MISSILE_SPIN);
  regs.si = _guest.Word(At(regs.di, SLOT_TARGET));
  if (regs.si == 0)
  {
    // At the player.
    GetVectorToPlayerEntry(_guest);
    _guest.Set(DS.incomingMissileAlert, 1);
  }
  else
  {
    if ((_guest.Byte(regs.si) & SLOT_ACTIVE) == 0)
    {
      ExplodeObject(_guest); // its target is gone
      return;
    }
    regs.ax = static_cast<std::uint16_t>(_guest.Word(At(regs.si, SLOT_X)) - _guest.Word(At(regs.di, SLOT_X)));
    regs.bx = static_cast<std::uint16_t>(_guest.Word(At(regs.si, SLOT_Y)) - _guest.Word(At(regs.di, SLOT_Y)));
    regs.cx = static_cast<std::uint16_t>(_guest.Word(At(regs.si, SLOT_Z)) - _guest.Word(At(regs.di, SLOT_Z)));
  }
  // VectorWithinBox with AX, BX and CX pushed and popped round it.
  const std::uint16_t x = regs.ax;
  const std::uint16_t y = regs.bx;
  const std::uint16_t z = regs.cx;
  regs.dx = MISSILE_HIT_BOX;
  VectorWithinBoxEntry(_guest);
  regs.cx = z;
  regs.bx = y;
  regs.ax = x;
  if (!_guest.Flag(FLAG_CARRY))
  {
    ConvertVectorToAnglesEntry(_guest);
    TurnTowardAnglesEntry(_guest);
    ComputeVelocityEntry(_guest);
    MoveObject(_guest);
    return;
  }
  ExplodeObject(_guest);
  regs.si = _guest.Word(At(regs.di, SLOT_TARGET));
  if (regs.si == 0)
  {
    regs.ax = MISSILE_DAMAGE;
    TakeDamage(_guest);
    return;
  }
  std::swap(regs.si, regs.di);
  IsStationEntry(_guest);
  std::swap(regs.si, regs.di);
  if (!_guest.Flag(FLAG_ZERO))
  {
    regs.di = regs.si;
    CreditKill(_guest);
    if ((_guest.Byte(At(regs.di, SLOT_FLAGS)) & FLAG_INDESTRUCTIBLE) == 0)
    {
      ExplodeObject(_guest);
    }
    return;
  }
  if (_guest.Get(DS.thargoidInvasionActive) != 1)
  {
    AddSaturating(_guest, DS.legalStatus.offset, MISSILE_AT_STATION_CRIME);
    return;
  }
  // An invaded station loses energy to each missile, and goes when it runs out.
  regs.di = regs.si;
  const std::uint8_t energy = _guest.Byte(At(regs.di, SLOT_ENERGY));
  _guest.SetByte(At(regs.di, SLOT_ENERGY), static_cast<std::uint8_t>(energy - INVADED_STATION_HIT));
  if (energy >= INVADED_STATION_HIT)
  {
    return;
  }
  ExplodeObject(_guest);
  _guest.Set(DS.invadedStationDestroyed, 1);
  _guest.Set(DS.thargoidInvasionActive, 0);
}

ThargoidTest Routine8C51(const ObjectSlot& _slot)
{
  const std::uint8_t type = TypeOf(_slot);
  return ThargoidTest{type, type == TYPE_THARGON || type == TYPE_THARGOID, type == TYPE_THARGOID};
}

void UseMaskingDevice(GameState& _state)
{
  // SUB, then 0 written over it on a borrow: two writes (8ECF, 8ED6).
  const std::uint16_t energy = _state.Get(DS.playerEnergy);
  _state.Set(DS.playerEnergy, static_cast<std::uint16_t>(energy - MASKING_ENERGY));
  if (energy < MASKING_ENERGY)
  {
    _state.Set(DS.playerEnergy, 0);
  }
  _state.Set(DS.maskingBackgroundColor, MASKING_COLOR);
  // Every ship forgets its state and that the player hit it, and calms a little: LOOP from CX = objectSlotCount.
  std::uint16_t slot = DS.shipSlots.offset;
  for (std::uint32_t count = LoopCount(_state.Get(DS.objectSlotCount)); count != 0; --count)
  {
    ObjectSlot ship(_state, slot);
    ship.Set(SlotByte::State, 0);
    ship.Set(SlotByte::Flags, static_cast<std::uint8_t>(ship.Get(SlotByte::Flags) & ~FLAG_HOSTILE));
    const std::uint8_t aggression = ship.Get(SlotByte::Aggression);
    ship.Set(SlotByte::Aggression, static_cast<std::uint8_t>(aggression - MASKING_CALMING));
    if (aggression < MASKING_CALMING)
    {
      ship.Set(SlotByte::Aggression, 0);
    }
    slot = Offset(slot, ObjectSlot::BYTES);
  }
}

// ── The entries of the routines de-assembled (ADR-012) ──

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

constexpr Machine::NativeContract VIEW_LASER{REGISTER_AX | REGISTER_BX | REGISTER_CX, FLAG_CARRY};
constexpr Machine::NativeContract LASER_SIGHTS{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX{REGISTER_AX | REGISTER_BX, 0};
constexpr Machine::NativeContract THARGOID_TEST{0, FLAG_ZERO | FLAG_CARRY};

} // namespace

void DrawLaserSightsEntry(Guest& _guest)
{
  DrawLaserSights(_guest.State());
  _guest.Clobber(LASER_SIGHTS);
}

void GetViewLaserEntry(Guest& _guest)
{
  const std::optional<std::uint8_t> laser = GetViewLaser(_guest.State());
  // AL is a result in a register the contract clobbers, so the marks go first.
  _guest.Clobber(VIEW_LASER);
  if (laser.has_value())
  {
    SetLow(_guest.Regs().ax, *laser);
  }
  _guest.SetFlag(FLAG_CARRY, laser.has_value());
}

void InitMissileEntry(Guest& _guest)
{
  InitMissile(_guest.State(), ObjectSlot(_guest.State(), _guest.Regs().di));
  _guest.Clobber(CLOBBERS_AX_BX);
}

void TallyMaskMissionKillEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ObjectSlot slot(_guest.State(), regs.di);
  // While the mission runs, the original takes the type into AL, which the contract compares.
  const bool running = _guest.Get(DS.maskMissionShipsLeft) != 0;
  TallyMaskMissionKill(_guest.State(), slot);
  if (running)
  {
    SetLow(regs.ax, TypeOf(slot));
  }
  _guest.Clobber(PRESERVES_ALL);
}

void CheckMissileTargetDestroyedEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The original passes the message through AX, which the contract compares.
  if (CheckMissileTargetDestroyed(_guest.State(), regs.di))
  {
    regs.ax = DS.missileTargetDestroyedMessage.offset;
  }
  _guest.Clobber(PRESERVES_ALL);
}

void Routine8C51Entry(Guest& _guest)
{
  const ThargoidTest test = Routine8C51(ObjectSlot(_guest.State(), _guest.Regs().di));
  SetLow(_guest.Regs().ax, test.type);
  _guest.SetFlag(FLAG_ZERO, test.thargoidOrThargon);
  _guest.SetFlag(FLAG_CARRY, test.thargoid);
  _guest.Clobber(THARGOID_TEST);
}

void UseMaskingDeviceEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The original leaves SI past the slots and CX at 0, which the contract compares; the count is the one it loaded.
  const std::uint8_t slots = _guest.Get(DS.objectSlotCount);
  UseMaskingDevice(_guest.State());
  regs.si = static_cast<std::uint16_t>(DS.shipSlots.offset + LoopCount(slots) * ObjectSlot::BYTES);
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

namespace
{

constexpr Machine::NativeContract CLOBBERS_ALL{REGISTER_ALL, 0};
constexpr Machine::NativeContract LAUNCHES{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_BP | REGISTER_ES,
                                           0};

constexpr std::array ENTRIES = {
  NativeEntry{0x0630, "DrawLaserSights", &DrawLaserSightsEntry, LASER_SIGHTS},
  NativeEntry{0x066C, "GetViewLaser", &GetViewLaserEntry, VIEW_LASER},
  NativeEntry{0x0A9A, "DrawLaserBeams", &DrawLaserBeams,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP | REGISTER_DI, 0}},
  NativeEntry{0x2C9B, "TakeDamage", &TakeDamage, Machine::NativeContract{REGISTER_AX | REGISTER_BX, 0}},
  NativeEntry{0x2ED6, "DetonateEnergyBomb", &DetonateEnergyBomb, CLOBBERS_ALL},
  NativeEntry{0x2FE3, "SpawnPlayerWreckage", &SpawnPlayerWreckage, CLOBBERS_ALL},
  NativeEntry{0x3115, "KillPlayer", &KillPlayer, Machine::NativeContract{REGISTER_AX, 0}},
  NativeEntry{0x4C8C, "InitMissile", &InitMissileEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4F9F, "RemoveAllMissiles", &RemoveAllMissiles,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI | REGISTER_ES, 0}},
  NativeEntry{0x4FC1, "ExplodeObject", &ExplodeObject, CLOBBERS_ALL},
  NativeEntry{0x50FE, "TallyMaskMissionKill", &TallyMaskMissionKillEntry, PRESERVES_ALL},
  NativeEntry{0x518B, "TryFireLaserAtPlayer", &TryFireLaserAtPlayer,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0}},
  NativeEntry{0x5242, "LaunchPlayerMissile", &LaunchPlayerMissile, CLOBBERS_ALL},
  NativeEntry{0x534E, "LaunchShipFromObject", &LaunchShipFromObject, Machine::NativeContract{LAUNCHES.clobbers, FLAG_CARRY}},
  NativeEntry{0x543A, "TryLaunchMissileAtPlayer", &TryLaunchMissileAtPlayer, LAUNCHES},
  NativeEntry{0x5471, "TryLaunchThargon", &TryLaunchThargon, LAUNCHES},
  NativeEntry{0x54F2, "UpdateMissileAi", &UpdateMissileAi, PRESERVES_ALL},
  NativeEntry{0x8A46, "FindShipInCrosshairs", &FindShipInCrosshairs,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP | REGISTER_SI, FLAG_CARRY}},
  NativeEntry{0x8AC2, "ResolveLaserFire", &ResolveLaserFire, CLOBBERS_ALL},
  NativeEntry{0x8B8B, "CheckMissileTargetDestroyed", &CheckMissileTargetDestroyedEntry, PRESERVES_ALL},
  NativeEntry{0x8BC6, "CreditKill", &CreditKill, PRESERVES_ALL},
  NativeEntry{0x8C51, "Routine8C51", &Routine8C51Entry, THARGOID_TEST},
  NativeEntry{0x8C8E, "ApplyEnemyLaserHit", &ApplyEnemyLaserHit, CLOBBERS_ALL},
  NativeEntry{0x8ECF, "UseMaskingDevice", &UseMaskingDeviceEntry, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> CombatEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
