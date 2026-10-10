#include "pch.h"

#include "Combat.h"

#include "Ai.h"
#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Docking.h"
#include "Market.h"
#include "Maths.h"
#include "Scene.h"
#include "Ships.h"
#include "Sound.h"
#include "Text.h"
#include "Video.h"

#include <utility>

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_DIRECTION;
using Machine::FLAG_ZERO;

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
constexpr std::uint16_t EDGE_RIGHT_X = 0xFF;
constexpr std::uint8_t ENEMY_BEAM_COLOR = 3;
constexpr std::uint8_t SHIELD_HIT = 0x0F;

[[nodiscard]] std::uint16_t At(std::uint16_t _slot, int _field) noexcept
{
  return static_cast<std::uint16_t>(_slot + _field);
}

// The 24-bit position, by axis: the low words and the high bytes; and the velocity, signed bytes.
constexpr std::array<SlotWord, 3> POSITION_LOW = {SlotWord::X, SlotWord::Y, SlotWord::Z};
constexpr std::array<SlotByte, 3> POSITION_HIGH = {SlotByte::XHigh, SlotByte::YHigh, SlotByte::ZHigh};
constexpr std::array<SlotByte, 3> VELOCITY = {SlotByte::VelocityX, SlotByte::VelocityY, SlotByte::VelocityZ};

// MOV [DI+4+2*axis],AX / CWD / MOV [DI+1+axis],DL: _value as the 24-bit coordinate _axis (0 x, 1 y, 2 z) of _slot, the low word,
// then its sign as the high byte.
void SetCoordinate(ObjectSlot _slot, std::size_t _axis, std::uint16_t _value)
{
  _slot.Set(POSITION_LOW[_axis], _value);
  _slot.Set(POSITION_HIGH[_axis], Low(SignWord(_value)));
}

// SAR r/m8,1.
[[nodiscard]] std::uint8_t HalveSigned(std::uint8_t _value) noexcept
{
  return static_cast<std::uint8_t>(static_cast<std::int8_t>(_value) >> 1);
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

// One beam: DrawLine from (_x, the bottom row) to _target, its x in the low byte and its row in the high, as DL and DH hold it.
// Returns what DrawLine returns.
bool DrawBeam(GameState& _state, std::uint8_t _x, std::uint16_t _target)
{
  return DrawLine(_state, Low(_target), High(_target), _x, BEAM_ROW);
}

// The crosshairs' test on one axis (8A7C, 8A93): |the view coordinate _axis of _slot| * 256 over its view z, below _radius, the
// BX the divide's trap saves.
[[nodiscard]] bool ScaledWithinRadius(GameState& _state, const ObjectSlot& _slot, SlotWord _axis, std::uint16_t _radius)
{
  std::uint16_t magnitude = _slot.Get(_axis);
  if ((magnitude & 0x8000) != 0)
  {
    magnitude = Negate(magnitude);
  }
  // CWD / MOV DL,AH / MOV AH,AL / XOR AL,AL: DX:AX = the magnitude * 256, with DH its sign.
  const std::uint32_t dividend = (std::uint32_t{Join(High(SignWord(magnitude)), High(magnitude))} << 16) | Join(Low(magnitude), 0);
  return DivideWord(_state, dividend, _slot.Get(SlotWord::ViewZ), _radius).quotient < _radius;
}

// What BX holds after MoveObject: the mask of the last blip it erased, as XorDashboardPixel leaves it, else _bx.
[[nodiscard]] std::uint16_t BxAfterMove(const MovedObject& _moved, std::uint16_t _bx) noexcept
{
  if (_moved.removedBlip)
  {
    return _moved.removedBlip->mask;
  }
  if (_moved.near.erasedBlip)
  {
    return _moved.near.erasedBlip->mask;
  }
  return _bx;
}

// ADD [slot+_field],_value / JAE / MOV [slot+_field],0FFh: the sum written, and FFh over it on a carry.
void AddSaturating(ObjectSlot _slot, SlotByte _field, std::uint8_t _value)
{
  const unsigned sum = _slot.Get(_field) + unsigned{_value};
  _slot.Set(_field, static_cast<std::uint8_t>(sum));
  if (sum > 0xFF)
  {
    _slot.Set(_field, 0xFF);
  }
}

// What PayBounty found after paying: what Routine8C51 found of the slot, when a mis-jump's countdown runs, and whether the
// countdown reached 1 and the Nav-Comp's repair was announced.
struct PaidBounty
{
  std::optional<ThargoidTest> killed;
  bool repaired;
};

// 8BE5: _tenths shown (ShowBountyMessage) and added to the credits (AddCredits); then, while a mis-jump's countdown runs, the
// kill of a Thargon or a Thargoid counts it down by 5 or 35, the SUB written before the 1 written over a borrow or a 0, and
// at 1 the Nav-Comp's repair is announced.
PaidBounty PayBounty(GameState& _state, const ObjectSlot& _slot, std::uint16_t _tenths)
{
  ShowBountyMessage(_state, _tenths);
  AddCredits(_state, _tenths);
  if (_state.Get(DS.witchspaceCountdown) == 0)
  {
    return PaidBounty{std::nullopt, false};
  }
  const ThargoidTest killed = Routine8C51(_slot);
  if (!killed.thargoidOrThargon)
  {
    return PaidBounty{killed, false};
  }
  const std::uint8_t repair = killed.thargoid ? THARGOID_REPAIR : THARGON_REPAIR;
  const std::uint8_t countdown = _state.Get(DS.witchspaceCountdown);
  const auto left = static_cast<std::uint8_t>(countdown - repair);
  _state.Set(DS.witchspaceCountdown, left);
  if (countdown < repair || left == 0)
  {
    _state.Set(DS.witchspaceCountdown, 1);
  }
  if (_state.Get(DS.witchspaceCountdown) != 1)
  {
    return PaidBounty{killed, false};
  }
  _state.Set(DS.messagePointer, DS.navCompRepairedMessage.offset);
  _state.Set(DS.messageFrames, REPAIR_MESSAGE_FRAMES);
  return PaidBounty{killed, true};
}

// What the fragments' loop leaves of BX, DX and ES.
struct FragmentsLeft
{
  std::uint16_t bx;                // the last fragment's scattered word, or the mask of the last blip a move erased
  std::optional<std::uint16_t> dx; // what the last UpdateDebrisAi that moved its fragment left there
  std::uint16_t es;                // the data segment from the last copy, or the video segment from a blip a move erased after it
};

// Whether MoveObject erased a blip, which leaves ES on the video memory.
[[nodiscard]] bool ErasedBlip(const MovedObject& _moved) noexcept
{
  return _moved.removedBlip.has_value() || _moved.near.erasedBlip.has_value();
}

// The fragments of an exploding object: from 4FEE, _count of them, at least one, each a Splinter in a debris slot copied from
// _object (CopyObject, backwards when _backward), its velocity halved and scattered by a random -15..16 on each axis (halved
// again for an asteroid the mining laser hit), and run for a frame, or for eleven when the station explodes. PUSH CX and POP CX
// keep the count round each.
FragmentsLeft SpawnFragments(GameState& _state, const ObjectSlot& _object, std::uint16_t _count, bool _backward)
{
  FragmentsLeft left{0, std::nullopt, _state.DataSegment()};
  const auto debrisMoved = [&left](const std::optional<MovedObject>& _moved)
  {
    if (_moved)
    {
      left.bx = BxAfterMove(*_moved, left.bx);
      left.dx = _moved->dx;
      if (ErasedBlip(*_moved))
      {
        left.es = GameState::VIDEO_SEGMENT;
      }
    }
  };
  for (std::uint16_t fragments = _count; fragments != 0; --fragments)
  {
    ObjectSlot debris(_state, FindDebrisSlot(_state));
    CopyObject(_state, _object.Offset(), debris.Offset(), _backward);
    left.es = _state.DataSegment();
    debris.Set(SlotByte::State, 0);
    const std::uint16_t spin = NextRandom(_state);
    debris.Set(SlotWord::Spin, spin);
    // AND AX,1F1Fh / SUB AH,0Fh / SUB AL,0Fh: x and y; then BX shifted right three times / AND BL,1Fh / SUB BL,0Fh: z.
    const auto scatter = static_cast<std::uint16_t>(spin & FRAGMENT_SCATTER_MASK);
    auto scatterX = static_cast<std::uint8_t>(Low(scatter) - FRAGMENT_SCATTER_CENTER);
    auto scatterY = static_cast<std::uint8_t>(High(scatter) - FRAGMENT_SCATTER_CENTER);
    debris.Set(SlotByte::VelocityX, HalveSigned(debris.Get(SlotByte::VelocityX)));
    debris.Set(SlotByte::VelocityY, HalveSigned(debris.Get(SlotByte::VelocityY)));
    if (_state.Get(DS.miningLaserOnAsteroid) == 1)
    {
      scatterX = HalveSigned(scatterX);
      scatterY = HalveSigned(scatterY);
    }
    debris.Set(SlotByte::VelocityX, static_cast<std::uint8_t>(debris.Get(SlotByte::VelocityX) + scatterX));
    debris.Set(SlotByte::VelocityY, static_cast<std::uint8_t>(debris.Get(SlotByte::VelocityY) + scatterY));
    auto scatterZ = static_cast<std::uint8_t>((Low(static_cast<std::uint16_t>(spin >> 3)) & 0x1F) - FRAGMENT_SCATTER_CENTER);
    debris.Set(SlotByte::VelocityZ, HalveSigned(debris.Get(SlotByte::VelocityZ)));
    if (_state.Get(DS.miningLaserOnAsteroid) == 1)
    {
      scatterZ = HalveSigned(scatterZ);
    }
    left.bx = Join(High(static_cast<std::uint16_t>(spin >> 3)), scatterZ);
    debris.Set(SlotByte::VelocityZ, static_cast<std::uint8_t>(debris.Get(SlotByte::VelocityZ) + scatterZ));
    debris.Set(SlotByte::Flags, FLAG_RESTING);
    if (_state.Get(DS.miningLaserOnAsteroid) == 1 && NextRandom(_state) < MINERALS_ODDS)
    {
      debris.Set(SlotByte::Flags, static_cast<std::uint8_t>(debris.Get(SlotByte::Flags) | FLAG_MINERALS));
    }
    debris.Set(SlotByte::Energy, 0);
    debris.Set(SlotWord::Cargo, 0);
    debris.Set(SlotByte::Age, 0);
    debris.Set(SlotByte::Class, DEBRIS_CLASS);
    auto lifetime = static_cast<std::uint8_t>(Low(NextRandom(_state)) & FRAGMENT_LIFETIME_MASK);
    if (_state.Get(DS.miningLaserOnAsteroid) == 1)
    {
      lifetime = static_cast<std::uint8_t>(lifetime + MINED_FRAGMENT_LIFETIME);
    }
    debris.Set(SlotByte::Lifetime, static_cast<std::uint8_t>(lifetime + FRAGMENT_LIFETIME));
    debris.Set(SlotByte::Type, SPLINTER_ACTIVE);
    debrisMoved(UpdateDebrisAi(_state, debris));
    if (_state.Get(DS.explodingStation) == 1)
    {
      // A station's fragments are flung ten frames further at once: LOOP from CX = 10, PUSH CX and POP CX round each.
      for (std::uint16_t steps = STATION_FRAGMENT_STEPS; steps != 0; --steps)
      {
        debrisMoved(UpdateDebrisAi(_state, debris));
      }
    }
  }
  return left;
}

// DropCargo (5099), the tail ExplodeObject ends in, with DI on _object and what ExplodeObject has left in _left: the barrels it
// leaves, one when it carries the device, else a random byte / (255 / (cargo + 1) + 1), its first divide by 0 into the game's
// trap for a cargo of FFh, which saves BX. Each barrel goes in a free ship slot, copied from what DI holds (CopyObject, backwards
// when _backward), with a random heading, its velocity set and moved. A failed slot search leaves DI past the slots and SI on
// what DI held, as the original's XCHG does, so a later barrel would be copied from there. Returns what it leaves in DI, BX, DX,
// SI and ES.
Explosion DropCargo(GameState& _state, const ObjectSlot& _object, bool _backward, Explosion _left)
{
  Explosion left = _left;
  std::uint16_t barrels = 1;
  if ((_object.Get(SlotByte::Flags) & FLAG_DEVICE) == 0)
  {
    // MOV BL,[DI+2Ch] / AND BL,BL / JE, then MOV AX,0FFh / INC BL / DIV BL and MOV BL,AL / INC BL / XOR AH,AH / DIV BL.
    const std::uint8_t cargo = _object.Get(SlotByte::Cargo);
    left.bx = WithLow(left.bx, cargo);
    if (cargo == 0)
    {
      return left;
    }
    left.bx = WithLow(left.bx, static_cast<std::uint8_t>(cargo + 1));
    const std::uint8_t share = DivideByte(_state, 0x00FF, Low(left.bx), left.bx).quotient;
    left.bx = WithLow(left.bx, static_cast<std::uint8_t>(share + 1));
    barrels = DivideByte(_state, Low(NextRandom(_state)), Low(left.bx), left.bx).quotient;
    if (barrels == 0)
    {
      return left;
    }
  }
  // LOOP from CX = the count, PUSH CX and POP CX round each barrel.
  std::uint16_t source = _object.Offset();
  for (; barrels != 0; --barrels)
  {
    const SlotSearch free = FindFreeShipSlot(_state);
    // XCHG DI,SI: the slot found, or past the slots, in DI, and what DI held in SI.
    const std::uint16_t from = source;
    left.slot = free.slot;
    left.si = from;
    if (!free.found)
    {
      source = left.slot;
      continue;
    }
    ObjectSlot barrel(_state, free.slot);
    // MOV AL,[SI+1Eh] / PUSH AX round the copy and the barrel's template: the device's bit (5) becomes the barrel's bit 6.
    const std::uint8_t flags = _state.Byte(Offset(from, SLOT_FLAGS));
    CopyObject(_state, from, free.slot, _backward);
    InitCargoBarrel(_state, barrel);
    barrel.Set(SlotByte::Flags, FLAG_RESTING);
    barrel.Set(SlotByte::Flags, static_cast<std::uint8_t>(barrel.Get(SlotByte::Flags) | ((flags & FLAG_DEVICE) << 1)));
    const std::uint16_t heading = NextRandom(_state);
    barrel.Set(SlotWord::Pitch, heading);
    barrel.Set(SlotWord::Yaw, Swap(heading));
    // PUSH DI / PUSH SI round ComputeVelocity, which leaves BX the z word; then MoveObject, and MOV DI,SI.
    const Velocity velocity = ComputeVelocity(_state, barrel);
    const MovedObject moved = MoveObject(_state, barrel);
    left.bx = BxAfterMove(moved, static_cast<std::uint16_t>(velocity.words.z));
    left.dx = moved.dx;
    left.es = ErasedBlip(moved) ? GameState::VIDEO_SEGMENT : _state.DataSegment();
    left.slot = from;
    source = from;
  }
  return left;
}

// 8B63: _target destroyed by the player's laser: credited (CreditKill), a missile locked on it unlocked
// (CheckMissileTargetDestroyed), and exploded (ExplodeObject), with BX 0 once CreditKill paid, else _bx, what FindShipInCrosshairs
// left there; then the beams, and the laser's flags cleared. Returns whether a beam was a horizontal line.
bool DestroyTarget(GameState& _state, Hardware& _hardware, ObjectSlot _target, bool _backward, std::uint16_t _bx)
{
  const KillCredit credit = CreditKill(_state, _target);
  (void)CheckMissileTargetDestroyed(_state, _target.Offset());
  (void)ExplodeObject(_state, _hardware, _target, _backward, credit.paidTenths ? std::uint16_t{0} : _bx);
  const bool filled = DrawLaserBeams(_state);
  _state.Set(DS.miningLaserOnAsteroid, 0);
  _state.Set(DS.laserFiring, 0);
  return filled;
}

// From 8AD2: the player's laser hits _target, which is marked hostile, with the impact's sound and its STI; the damage is the
// laser's type + 1, and a mining laser marks an asteroid mined. A station's hit cancels the docking computer and is a crime of
// 28h, added to legalStatus up to FFh, or in an invasion does half the damage, rounded up (NEG AL / SAR AL,1 / NEG AL). The
// damage goes on the aggression, up to FFh, and off the energy, each written and then FFh or nothing over it. Returns whether
// the target is to be destroyed (DestroyTarget): it ran out of energy and is not indestructible, or is an invaded station; an
// indestructible one is left at 0, and a station's is a crime of 28h, which cancels the docking computer only when it carries.
[[nodiscard]] bool HitTarget(GameState& _state, Hardware& _hardware, ObjectSlot _target)
{
  _target.Set(SlotByte::Flags, static_cast<std::uint8_t>(_target.Get(SlotByte::Flags) | FLAG_HOSTILE));
  (void)StartImpactSound(_state);
  _hardware.EnableInterrupts();
  const std::uint8_t laser = _state.Get(DS.firingLaserType);
  if (laser == MINING_LASER && TypeOf(_target) == TYPE_ASTEROID)
  {
    _state.Set(DS.miningLaserOnAsteroid, 1);
  }
  auto damage = static_cast<std::uint8_t>(laser + 1);
  const bool invasion = _state.Get(DS.thargoidInvasionActive) == 1;
  if (IsStation(_target).station)
  {
    CancelDockingComputer(_state, _hardware);
    if (invasion)
    {
      damage = static_cast<std::uint8_t>(0u - HalveSigned(static_cast<std::uint8_t>(0u - damage)));
    }
    else
    {
      AddSaturating(_state, DS.legalStatus, LEGAL_STATUS_PER_STATION_HIT);
    }
  }
  AddSaturating(_target, SlotByte::Aggression, damage);
  const std::uint8_t energy = _target.Get(SlotByte::Energy);
  _target.Set(SlotByte::Energy, static_cast<std::uint8_t>(energy - damage));
  if (energy >= damage)
  {
    return false;
  }
  if ((_target.Get(SlotByte::Flags) & FLAG_INDESTRUCTIBLE) == 0)
  {
    return true;
  }
  _target.Set(SlotByte::Energy, 0);
  if (!IsStation(_target).station)
  {
    return false;
  }
  if (_state.Get(DS.thargoidInvasionActive) == 1)
  {
    _state.Set(DS.invadedStationDestroyed, 1);
    _state.Set(DS.thargoidInvasionActive, 0);
    return true;
  }
  // ADD / JAE / MOV 0FFh, and only an add that carries cancels the docking computer.
  const unsigned status = _state.Get(DS.legalStatus) + unsigned{LEGAL_STATUS_PER_STATION_HIT};
  _state.Set(DS.legalStatus, static_cast<std::uint8_t>(status));
  if (status > 0xFF)
  {
    _state.Set(DS.legalStatus, 0xFF);
    CancelDockingComputer(_state, _hardware);
  }
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

bool DrawLaserBeams(GameState& _state)
{
  // AND AX,303h / ADD AL,7Eh / ADD AH,3Eh: the point the beams end at, its x in the low byte and its row in the high, as DX
  // holds it.
  const auto scatter = static_cast<std::uint16_t>(NextRandom(_state) & BEAM_SCATTER_MASK);
  const std::uint16_t target =
    Join(static_cast<std::uint8_t>(High(scatter) + BEAM_TARGET_ROW), static_cast<std::uint8_t>(Low(scatter) + BEAM_TARGET_X));
  bool filled = false;
  const auto beam = [&_state, &filled, target](std::uint8_t _x) { filled = DrawBeam(_state, _x, target) || filled; };
  const std::uint8_t type = _state.Get(DS.firingLaserType);
  if ((type & 1) != 0)
  {
    if ((type & 2) != 0)
    {
      // Type 3: all four, the inner pair in the toggled colour, the outer in the other.
      _state.Set(DS.drawColor, _state.Get(DS.laserBeamColorToggle));
      _state.Set(DS.laserBeamColorToggle, static_cast<std::uint8_t>(_state.Get(DS.laserBeamColorToggle) ^ BEAM_TOGGLE));
      beam(BEAM_INNER_LEFT);
      beam(BEAM_INNER_RIGHT);
      _state.Set(DS.drawColor, static_cast<std::uint8_t>(_state.Get(DS.drawColor) ^ BEAM_TOGGLE));
    }
    else
    {
      // Type 1: the outer pair.
      const std::uint8_t color = NextBeamColor(_state.Get(DS.laserBeamColor), true);
      _state.Set(DS.laserBeamColor, color);
      _state.Set(DS.drawColor, color);
    }
    beam(BEAM_OUTER_LEFT);
    beam(BEAM_OUTER_RIGHT);
    return filled;
  }
  // Types 0 and 2: one side a shot, alternately.
  const std::uint8_t color = NextBeamColor(_state.Get(DS.laserBeamColor), (type & 2) != 0);
  _state.Set(DS.laserBeamColor, color);
  _state.Set(DS.drawColor, color);
  const auto side = static_cast<std::uint8_t>(_state.Get(DS.laserBeamSide) ^ 1);
  _state.Set(DS.laserBeamSide, side);
  if (side != 0)
  {
    beam(BEAM_OUTER_LEFT);
    beam(BEAM_INNER_LEFT);
    return filled;
  }
  beam(BEAM_OUTER_RIGHT);
  beam(BEAM_INNER_RIGHT);
  return filled;
}

Explosion ExplodeObject(GameState& _state, Hardware& _hardware, ObjectSlot _object, bool _backward, std::uint16_t _bx)
{
  TallyMaskMissionKill(_state, _object);
  const bool drawn = (_object.Get(SlotByte::Type) & SLOT_DRAWN) != 0;
  // RemoveObject, whether or not it was drawn: a blip it erases leaves BX its mask, DX its last pixel and ES the video segment.
  Explosion left{_object.Offset(), _bx, std::nullopt, std::nullopt, std::nullopt};
  if (const std::optional<DashboardPixel> erased = RemoveObject(_state, _object))
  {
    left.bx = erased->mask;
    left.dx = PixelPlace(*erased);
    left.es = GameState::VIDEO_SEGMENT;
  }
  if (!drawn)
  {
    return left;
  }
  (void)StartExplosionSound(_state);
  _hardware.EnableInterrupts();
  _state.Set(DS.explodingStation, 1);
  if (!IsStation(_object).station)
  {
    _state.Set(DS.explodingStation, 0);
  }
  // MOV CL,[DI+2Dh] / AND CX,0FFh / JNE: the fragments, then MOV DI,SI, back on the object.
  if (const std::uint8_t fragments = _object.Get(SlotByte::Fragments); fragments != 0)
  {
    const FragmentsLeft flown = SpawnFragments(_state, _object, fragments, _backward);
    left.bx = flown.bx;
    if (flown.dx)
    {
      left.dx = flown.dx;
    }
    left.si = _object.Offset();
    left.es = flown.es;
  }
  return DropCargo(_state, _object, _backward, left);
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

void TryFireLaserAtPlayer(GameState& _state, ObjectSlot _slot, std::uint16_t _pitchError, std::uint16_t _yawError)
{
  if ((_slot.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) == 0)
  {
    return;
  }
  if (Low(NextRandom(_state)) >= _slot.Get(SlotByte::Aggression))
  {
    return;
  }
  if (CheckSafeZoneHoldFire(_state, _slot).hold || FiringBlocked(_state) != 0)
  {
    return;
  }
  // Each shot calms the ship by 5, unless that would take it below 20.
  const auto calmer = static_cast<std::uint8_t>(_slot.Get(SlotByte::Aggression) - CALMER_STEP);
  if (calmer >= CALMEST)
  {
    _slot.Set(SlotByte::Aggression, calmer);
  }
  // VectorWithinBox on x = 0, the yaw error and the pitch error.
  const auto yaw = static_cast<std::int16_t>(_yawError);
  const auto pitch = static_cast<std::int16_t>(_pitchError);
  if (!VectorWithinBox(Vector{0, yaw, pitch}, LASER_GRAZE_BOX))
  {
    return;
  }
  _state.Set(DS.playerHitBy, _slot.Offset());
  _state.Set(DS.playerHitPending, HIT_GRAZED);
  const std::uint8_t depth = _slot.Get(SlotByte::CameraZHigh);
  _state.Set(DS.playerHitByDepth, depth);
  // Again within 70, on the registers the first test leaves: x is AX, the depth MOV AL,[DI+3Ch] put over its 0, and the
  // errors are their magnitudes, which the test takes again.
  if (VectorWithinBox(Vector{depth, yaw, pitch}, LASER_HIT_BOX))
  {
    _state.Set(DS.playerHitPending, HIT_SQUARE);
  }
}

void TryLaunchMissileAtPlayer(GameState& _state, ObjectSlot _slot, std::uint16_t _odds, bool _backward)
{
  if (_state.Get(DS.killCount) < MISSILE_KILLS_NEEDED || (_slot.Get(SlotByte::Flags) & FLAG_HOSTILE) == 0)
  {
    return;
  }
  if (CheckSafeZoneHoldFire(_state, _slot).hold || _slot.Get(SlotByte::Missiles) == 0 || FiringBlocked(_state) != 0)
  {
    return;
  }
  if (NextRandom(_state) >= _odds)
  {
    return;
  }
  if (LaunchShipFromObject(_state, _slot, MISSILE_LAUNCH, _backward))
  {
    _slot.Set(SlotByte::Missiles, static_cast<std::uint8_t>(_slot.Get(SlotByte::Missiles) - 1));
  }
}

void TryLaunchThargon(GameState& _state, ObjectSlot _slot, bool _backward)
{
  if (!IsThargoidType(_slot) || _slot.Get(SlotByte::Thargons) == 0)
  {
    return;
  }
  if (NextRandom(_state) >= THARGON_ODDS)
  {
    return;
  }
  if (LaunchShipFromObject(_state, _slot, THARGON_LAUNCH, _backward))
  {
    _slot.Set(SlotByte::Thargons, static_cast<std::uint8_t>(_slot.Get(SlotByte::Thargons) - 1));
  }
}

CrosshairTarget FindShipInCrosshairs(GameState& _state)
{
  // MOV CL,objectSlotCount / SUB CL,2 / XOR CH,CH, then LOOP: a count of 0 runs 65,536 times. BP is the nearest z so far.
  CrosshairTarget target{std::nullopt, 0, 0};
  std::uint16_t nearestZ = 0xFFFF;
  std::uint16_t slot = DS.stationSlot.offset;
  const auto slots = static_cast<std::uint8_t>(_state.Get(DS.objectSlotCount) - 2);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    const ObjectSlot object(_state, slot);
    const std::uint8_t type = object.Get(SlotByte::Type);
    target.bx = WithLow(target.bx, type);
    if ((type & (SLOT_DRAWN | SLOT_ACTIVE)) == (SLOT_DRAWN | SLOT_ACTIVE) &&
        (object.Get(SlotByte::Flags) & CROSSHAIR_IGNORED) != CROSSHAIR_IGNORED)
    {
      // AND BX,3Eh / MOV AX,[BX+shipTargetRadius] / XCHG AH,AL / XOR DX,DX / DIV [DI+14h] / MOV BX,AX / ADD BX,2: the radius *
      // 256 / z, plus 2, against |x| * 256 / z and |y| * 256 / z.
      target.bx = static_cast<std::uint16_t>(type & (TYPE_MASK << 1));
      const std::uint16_t radius = Swap(_state.Word(At(DS.shipTargetRadius.offset, target.bx)));
      target.bx = Offset(DivideWord(_state, radius, object.Get(SlotWord::ViewZ), target.bx).quotient, 2);
      if (ScaledWithinRadius(_state, object, SlotWord::ViewX, target.bx) &&
          ScaledWithinRadius(_state, object, SlotWord::ViewY, target.bx) && object.Get(SlotWord::ViewZ) < nearestZ)
      {
        target.slot = slot;
        nearestZ = object.Get(SlotWord::ViewZ);
      }
    }
    slot = At(slot, SLOT_BYTES);
  }
  // INC BP / JE: none found while BP is still FFFFh; else MOV DI,SI, the nearest.
  target.di = target.slot.value_or(slot);
  return target;
}

bool ResolveLaserFire(GameState& _state, Hardware& _hardware, bool _backward)
{
  if (_state.Get(DS.laserFiring) != 1)
  {
    return false;
  }
  if (const CrosshairTarget target = FindShipInCrosshairs(_state); target.slot)
  {
    const ObjectSlot hit(_state, *target.slot);
    if (HitTarget(_state, _hardware, hit))
    {
      return DestroyTarget(_state, _hardware, hit, _backward, target.bx);
    }
  }
  const bool filled = DrawLaserBeams(_state);
  if (StartLaserSound(_state))
  {
    _hardware.EnableInterrupts();
  }
  _state.Set(DS.miningLaserOnAsteroid, 0);
  _state.Set(DS.laserFiring, 0);
  return filled;
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

KillCredit CreditKill(GameState& _state, ObjectSlot _slot)
{
  const std::uint8_t bounty = _slot.Get(SlotByte::Bounty);
  KillCredit credit{bounty, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt};
  if (bounty == 0)
  {
    return credit;
  }
  // INC, then CMP 0FFh / JB / MOV 0FEh: a count that reaches FFh is written and then FEh over it; one that wraps to 0 stays there.
  const auto kills = static_cast<std::uint8_t>(_state.Get(DS.killCount) + 1);
  _state.Set(DS.killCount, kills);
  if (kills == 0xFF)
  {
    _state.Set(DS.killCount, MOST_KILLS);
  }
  std::uint16_t tenths = bounty;
  if (bounty == NO_BOUNTY_TYPE)
  {
    if (!IsThargoidType(_slot))
    {
      // Killing what carries no bounty is a crime: 4 for a police Viper anywhere, 2 for anything else inside the safe zone.
      credit.zone = InSafeZone(_state);
      const bool police = IsPoliceViper(_slot);
      if (!credit.zone->inside && !police)
      {
        return credit;
      }
      credit.crime = police ? CRIME_AGAINST_POLICE : CRIME;
      AddSaturating(_state, DS.legalStatus, *credit.crime);
      return credit;
    }
    tenths = THARGOID_BOUNTY;
  }
  credit.paidTenths = tenths;
  const PaidBounty paid = PayBounty(_state, _slot, tenths);
  credit.killed = paid.killed;
  credit.repaired = paid.repaired;
  return credit;
}

bool ApplyEnemyLaserHit(GameState& _state, Hardware& _hardware)
{
  if (_state.Get(DS.playerHitPending) == 0)
  {
    return false;
  }
  // StartPlayerHitSound's CLI round its writes, which nothing interrupts in native code, then its STI.
  StartPlayerHitSound(_state);
  _hardware.EnableInterrupts();
  bool filled = false;
  const ObjectSlot attacker(_state, _state.Get(DS.playerHitBy));
  if ((attacker.Get(SlotByte::Type) & SLOT_DRAWN) != 0)
  {
    // A beam from the attacker on screen, ProjectToScreen's x moved to DX and its row left in BX, to a random point on an edge of
    // the view, x in CX and the row in AX: the top edge, the bottom, the left or the right, by four ranges of the random word.
    const Vector view{static_cast<std::int16_t>(attacker.Get(SlotWord::ViewX)), static_cast<std::int16_t>(attacker.Get(SlotWord::ViewY)),
                      static_cast<std::int16_t>(attacker.Get(SlotWord::ViewZ))};
    const ScreenPoint from = ProjectToScreen(_state, view);
    const std::uint16_t random = NextRandom(_state);
    std::uint16_t edgeX = 0;
    std::uint16_t edgeRow = 0;
    if (random < EDGE_LEFT_BELOW)
    {
      edgeX = Low(random);
    }
    else if (random < EDGE_RIGHT_BELOW)
    {
      edgeX = Low(random);
      edgeRow = EDGE_ROW_MASK;
    }
    else if (random < EDGE_TOP_BELOW)
    {
      edgeRow = static_cast<std::uint16_t>(random & EDGE_ROW_MASK);
    }
    else
    {
      edgeRow = static_cast<std::uint16_t>(random & EDGE_ROW_MASK);
      edgeX = EDGE_RIGHT_X;
    }
    _state.Set(DS.drawColor, ENEMY_BEAM_COLOR);
    filled = DrawClippedLine(_state, static_cast<std::uint16_t>(from.x), static_cast<std::uint16_t>(from.y), edgeX, edgeRow);
  }
  _state.Set(DS.playerHitPending, 0);
  // Bit 7 of the attacker's depth byte: behind, so the aft shield takes it. SUB [shield],0Fh; on a borrow, MOV AL,[shield] and 0
  // written over it: what the shield could not take, NEG AL / CBW, comes off the energy, SUB and then 0 over it on a borrow.
  const DataField<std::uint8_t> shield = (_state.Get(DS.playerHitByDepth) & 0x80) != 0 ? DS.aftShield : DS.foreShield;
  const std::uint8_t strength = _state.Get(shield);
  _state.Set(shield, static_cast<std::uint8_t>(strength - SHIELD_HIT));
  if (strength >= SHIELD_HIT)
  {
    return filled;
  }
  const std::uint8_t wrapped = _state.Get(shield);
  _state.Set(shield, 0);
  const std::uint16_t excess = SignExtend(Negate(wrapped));
  const std::uint16_t energy = _state.Get(DS.playerEnergy);
  _state.Set(DS.playerEnergy, static_cast<std::uint16_t>(energy - excess));
  if (energy >= excess)
  {
    return filled;
  }
  _state.Set(DS.playerEnergy, 0);
  _state.Set(DS.playerDead, 1);
  return filled;
}

std::optional<std::uint8_t> TakeDamage(GameState& _state, std::uint16_t _damage)
{
  if (_state.Get(DS.escapePodFrames) != 0)
  {
    return std::nullopt;
  }
  std::uint16_t excess = 0;
  if (_damage >= OVERWHELMING_DAMAGE)
  {
    // MOV BL,foreShield / XOR BH,BH / SUB AX,BX: the shield takes all it holds, and the energy the rest.
    excess = static_cast<std::uint16_t>(_damage - _state.Get(DS.foreShield));
    _state.Set(DS.foreShield, 0);
  }
  else
  {
    // MOV AH,AL / MOV AL,foreShield / SUB AL,AH, written back; on a borrow 0 over it, and NEG AL / CBW: the excess, sign-extended.
    const std::uint8_t damage = Low(_damage);
    const std::uint8_t shield = _state.Get(DS.foreShield);
    const auto left = static_cast<std::uint8_t>(shield - damage);
    _state.Set(DS.foreShield, left);
    if (shield >= damage)
    {
      return std::nullopt;
    }
    _state.Set(DS.foreShield, 0);
    excess = SignExtend(Negate(left));
  }
  const std::uint16_t energy = _state.Get(DS.playerEnergy);
  _state.Set(DS.playerEnergy, static_cast<std::uint16_t>(energy - excess));
  if (energy >= excess)
  {
    return std::nullopt;
  }
  const std::optional<std::uint8_t> stepLength = KillPlayer(_state);
  _state.Set(DS.playerEnergy, 0);
  return stepLength;
}

Detonation DetonateEnergyBomb(GameState& _state, Hardware& _hardware, bool _backward)
{
  if (InSafeZone(_state).inside)
  {
    AddSaturating(_state, DS.legalStatus, LEGAL_STATUS_PER_BOMB);
  }
  Detonation left{std::nullopt, std::nullopt};
  // Every active ship with a blip explodes, its cargo emptied first: MOV CL,objectSlotCount / SUB CL,3 / XOR CH,CH, then LOOP, a
  // count of 0 running 65,536 times; PUSH DI and PUSH CX round each explosion.
  std::uint16_t slot = DS.firstShipSlot.offset;
  const auto slots = static_cast<std::uint8_t>(_state.Get(DS.objectSlotCount) - FIRST_SHIP_SLOT);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    ObjectSlot ship(_state, slot);
    if ((ship.Get(SlotByte::Type) & SLOT_ACTIVE) != 0 && (ship.Get(SlotByte::Flags) & FLAG_BLIP_DRAWN) != 0)
    {
      ship.Set(SlotByte::Cargo, 0);
      // With no cargo the barrel count is never divided, so no BX reaches the divide trap's save.
      const Explosion exploded = ExplodeObject(_state, _hardware, ship, _backward, 0);
      if (exploded.si)
      {
        left.si = exploded.si;
      }
      if (exploded.es)
      {
        left.es = exploded.es;
      }
    }
    slot = At(slot, SLOT_BYTES);
  }
  return left;
}

void SpawnPlayerWreckage(GameState& _state)
{
  const Vector drift = ComputeDeathDebrisVector(_state);
  _state.Set(DS.wreckDriftX, static_cast<std::uint16_t>(drift.x));
  _state.Set(DS.wreckDriftY, static_cast<std::uint16_t>(drift.y));
  _state.Set(DS.wreckDriftZ, static_cast<std::uint16_t>(drift.z));
  _state.Set(DS.playerVelocityX, 0);
  _state.Set(DS.playerVelocityY, 0);
  _state.Set(DS.playerVelocityZ, 0);
  _state.Set(DS.playerSpeed, WRECK_SPEED);
  _state.Set(DS.velocityDirty, 1);
  _state.Set(DS.viewLocked, 1);
  // Six splinters: LOOP from CX = 6, PUSH CX and POP CX round each.
  for (std::uint16_t splinters = WRECK_SPLINTERS; splinters != 0; --splinters)
  {
    const std::uint16_t slot = FindDebrisSlot(_state);
    ClearObjectSlot(_state, slot);
    ObjectSlot splinter(_state, slot);
    splinter.Set(SlotByte::State, 0);
    const std::uint16_t random = NextRandom(_state);
    splinter.Set(SlotWord::Spin, random);
    // Each velocity byte from its own bits of the random word: x from BL, y from BH, z from BL shifted right twice.
    splinter.Set(SlotByte::VelocityX,
                 Low(WreckScatter(Low(random), SPLINTER_SCATTER_MASK, SPLINTER_SCATTER_CENTER, _state.Get(DS.wreckDriftX))));
    splinter.Set(SlotByte::VelocityY,
                 Low(WreckScatter(High(random), SPLINTER_SCATTER_MASK, SPLINTER_SCATTER_CENTER, _state.Get(DS.wreckDriftY))));
    splinter.Set(SlotByte::VelocityZ, Low(WreckScatter(static_cast<std::uint8_t>(Low(random) >> 2), SPLINTER_SCATTER_MASK,
                                                       SPLINTER_SCATTER_CENTER, _state.Get(DS.wreckDriftZ))));
    splinter.Set(SlotByte::Class, DEBRIS_CLASS);
    splinter.Set(SlotByte::Lifetime, WRECK_LIFETIME);
    splinter.Set(SlotByte::Type, SPLINTER_ACTIVE);
    (void)UpdateDebrisAi(_state, splinter);
  }
  if (_state.Get(DS.cargoUsedTonnes) == 0)
  {
    return;
  }
  // The cargo's barrel: four drifts ahead, drifting at a quarter of the drift and a little, turned to face along it.
  const SlotSearch free = FindFreeShipSlot(_state);
  const std::uint16_t slot = free.found ? free.slot : ReclaimShipSlot(_state).slot;
  ClearObjectSlot(_state, slot);
  ObjectSlot barrel(_state, slot);
  const std::array<DataField<std::uint16_t>, 3> wreckDrift = {DS.wreckDriftX, DS.wreckDriftY, DS.wreckDriftZ};
  for (std::size_t axis = 0; axis < wreckDrift.size(); ++axis)
  {
    const std::uint16_t axisDrift = _state.Get(wreckDrift[axis]);
    const std::uint16_t velocity = Sar(WreckScatter(Low(NextRandom(_state)), BARREL_SCATTER_MASK, BARREL_SCATTER_CENTER, axisDrift), 2);
    barrel.Set(VELOCITY[axis], Low(velocity));
    SetCoordinate(barrel, axis, static_cast<std::uint16_t>(axisDrift << 2));
  }
  InitCargoBarrel(_state, barrel);
  const Angles heading = ConvertVectorToAngles(_state, GetObjectPosition(barrel));
  barrel.Set(SlotWord::Pitch, heading.first);
  barrel.Set(SlotWord::Yaw, heading.second);
  // The same random -31..32 added to x and to y: AND AX,3Fh / SUB AX,1Fh / CWD, then ADD and ADC; then BX likewise.
  const std::uint16_t random = NextRandom(_state);
  const auto offset = static_cast<std::int16_t>((random & BARREL_OFFSET_MASK) - BARREL_OFFSET_CENTER);
  AddToCoordinate(barrel, 0, offset);
  AddToCoordinate(barrel, 1, offset);
}

std::optional<std::uint8_t> KillPlayer(GameState& _state)
{
  if (_state.Get(DS.escapePodFrames) != 0)
  {
    return std::nullopt;
  }
  _state.Set(DS.playerDead, 1);
  return StartPlayerDeathSound(_state);
}

void InitMissile(GameState& _state, ObjectSlot _slot)
{
  InitObjectFromTemplate(_state, _slot, DS.spawnTemplates.At(MISSILE_TEMPLATE), 0);
  _slot.Set(SlotByte::Class, MISSILE_CLASS);
}

std::optional<DashboardPixel> RemoveAllMissiles(GameState& _state)
{
  // MOV CL,objectSlotCount / XOR CH,CH, then LOOP: a count of 0 runs 65,536 times.
  std::optional<DashboardPixel> lastErased;
  std::uint16_t slot = DS.shipSlots.offset;
  for (std::uint32_t count = LoopCount(_state.Get(DS.objectSlotCount)); count != 0; --count)
  {
    const ObjectSlot object(_state, slot);
    if ((object.Get(SlotByte::Type) & ObjectSlot::ACTIVE) != 0 && TypeOf(object) == TYPE_MISSILE)
    {
      if (const std::optional<DashboardPixel> erased = RemoveObject(_state, object))
      {
        lastErased = erased;
      }
    }
    slot = Offset(slot, ObjectSlot::BYTES);
  }
  return lastErased;
}

void LaunchPlayerMissile(GameState& _state, std::uint16_t _source, bool _backward)
{
  // A free slot, or one ReclaimShipSlot takes; one it evicts is left in DI too, so XCHG SI,DI copies that slot onto itself.
  std::uint16_t source = _source;
  std::uint16_t slot = 0;
  if (const SlotSearch free = FindFreeShipSlot(_state); free.found)
  {
    slot = free.slot;
  }
  else
  {
    const ReclaimedSlot reclaimed = ReclaimShipSlot(_state);
    slot = reclaimed.slot;
    if (reclaimed.evicted)
    {
      source = reclaimed.slot;
    }
  }
  CopyObject(_state, source, slot, _backward);
  ObjectSlot missile(_state, slot);
  InitMissile(_state, missile);
  // 100 along the player's nose: (0, 100, 0) turned by the player's angles, negated.
  (void)SetSinCos(_state, 0, Negate(_state.Get(DS.playerPitchAngle)));
  (void)SetSinCos(_state, 1, Negate(_state.Get(DS.playerYawAngle)));
  (void)SetSinCos(_state, 2, Negate(_state.Get(DS.playerRollAngle)));
  const Vector position = RotateRollYawPitch(_state, Vector{0, static_cast<std::int16_t>(MISSILE_LAUNCH_DISTANCE), 0});
  SetCoordinate(missile, 0, static_cast<std::uint16_t>(position.x));
  SetCoordinate(missile, 1, static_cast<std::uint16_t>(position.y));
  SetCoordinate(missile, 2, static_cast<std::uint16_t>(position.z));
  // Locked on missileTarget, and aimed at where it is.
  const std::uint16_t target = _state.Get(DS.missileTarget);
  missile.Set(SlotWord::Target, target);
  const Angles heading = ConvertVectorToAngles(_state, GetObjectPosition(ObjectSlot(_state, target)));
  missile.Set(SlotWord::Pitch, heading.first);
  missile.Set(SlotWord::Yaw, heading.second);
  (void)ComputeVelocity(_state, missile);
  (void)MoveObject(_state, missile);
  (void)MoveObject(_state, missile);
}

std::optional<std::uint16_t> LaunchShipFromObject(GameState& _state, const ObjectSlot& _launcher, std::uint8_t _kind, bool _backward)
{
  const SlotSearch free = FindFreeShipSlot(_state);
  if (!free.found)
  {
    return std::nullopt;
  }
  if (_kind != MISSILE_LAUNCH && _kind != ESCAPE_POD_LAUNCH && _kind != THARGON_LAUNCH && _kind != KRAIT_LAUNCH)
  {
    return std::nullopt;
  }
  // A copy of the launcher in the free slot, made what _kind names, and moved clear of it.
  CopyObject(_state, _launcher.Offset(), free.slot, _backward);
  ObjectSlot ship(_state, free.slot);
  switch (_kind)
  {
  case MISSILE_LAUNCH:
    InitMissile(_state, ship);
    (void)MoveObject(_state, ship);
    (void)MoveObject(_state, ship);
    (void)MoveObject(_state, ship);
    ship.Set(SlotWord::Target, 0); // at the player
    break;
  case ESCAPE_POD_LAUNCH:
    InitEscapePod(_state, ship);
    RandomizeOrientation(_state, ship);
    (void)ComputeVelocity(_state, ship);
    (void)MoveObject(_state, ship);
    (void)MoveObject(_state, ship);
    (void)MoveObject(_state, ship);
    break;
  case THARGON_LAUNCH:
    InitThargon(_state, ship);
    (void)ComputeVelocity(_state, ship);
    (void)MoveObject(_state, ship);
    (void)MoveObject(_state, ship);
    ship.Set(SlotWord::Owner, _launcher.Offset());
    break;
  default:
    InitKraitHunter(_state, ship);
    (void)ComputeVelocity(_state, ship);
    (void)MoveObject(_state, ship);
    (void)MoveObject(_state, ship);
    break;
  }
  return free.slot;
}

MissileFlight UpdateMissileAi(GameState& _state, Hardware& _hardware, ObjectSlot _missile, bool _backward, std::uint16_t _bx,
                              std::uint16_t _dx)
{
  _state.Set(DS.incomingMissileAlert, 0);
  _missile.Set(SlotWord::Roll, Offset(_missile.Get(SlotWord::Roll), MISSILE_SPIN));
  const std::uint16_t target = _missile.Get(SlotWord::Target);
  Vector toTarget{};
  if (target == 0)
  {
    // At the player.
    toTarget = GetVectorToPlayer(_missile);
    _state.Set(DS.incomingMissileAlert, 1);
  }
  else
  {
    const ObjectSlot aimed(_state, target);
    if ((aimed.Get(SlotByte::Type) & SLOT_ACTIVE) == 0)
    {
      // JMP ExplodeObject: its target is gone.
      const Explosion exploded = ExplodeObject(_state, _hardware, _missile, _backward, _bx);
      return MissileFlight{exploded.slot, exploded.dx.value_or(_dx)};
    }
    const auto difference = [&aimed, &_missile](SlotWord _axis)
    { return static_cast<std::int16_t>(aimed.Get(_axis) - _missile.Get(_axis)); };
    toTarget = Vector{difference(SlotWord::X), difference(SlotWord::Y), difference(SlotWord::Z)};
  }
  // MOV DX,0C8h / VectorWithinBox, with AX, BX and CX, the vector, pushed and popped round it.
  if (!VectorWithinBox(toTarget, MISSILE_HIT_BOX))
  {
    (void)TurnTowardAngles(_missile, ConvertVectorToAngles(_state, toTarget));
    (void)ComputeVelocity(_state, _missile);
    return MissileFlight{_missile.Offset(), MoveObject(_state, _missile).dx};
  }
  // It explodes, with BX the vector's y; then MOV SI,[DI+29h] from where that leaves DI.
  const Explosion exploded = ExplodeObject(_state, _hardware, _missile, _backward, static_cast<std::uint16_t>(toTarget.y));
  const std::uint16_t dx = exploded.dx.value_or(MISSILE_HIT_BOX);
  const std::uint16_t hit = _state.Word(At(exploded.slot, SLOT_TARGET));
  if (hit == 0)
  {
    // The player, with TakeDamage's STI when it kills.
    if (TakeDamage(_state, MISSILE_DAMAGE))
    {
      _hardware.EnableInterrupts();
    }
    return MissileFlight{exploded.slot, dx};
  }
  ObjectSlot struck(_state, hit);
  if (!IsStation(struck).station)
  {
    // MOV DI,SI: a ship, credited, and exploded unless indestructible, with BX 0 once CreditKill paid.
    const KillCredit credit = CreditKill(_state, struck);
    if ((struck.Get(SlotByte::Flags) & FLAG_INDESTRUCTIBLE) != 0)
    {
      return MissileFlight{hit, dx};
    }
    const Explosion destroyed = ExplodeObject(_state, _hardware, struck, _backward, credit.paidTenths ? std::uint16_t{0} : exploded.bx);
    return MissileFlight{destroyed.slot, destroyed.dx.value_or(dx)};
  }
  if (_state.Get(DS.thargoidInvasionActive) != 1)
  {
    AddSaturating(_state, DS.legalStatus, MISSILE_AT_STATION_CRIME);
    return MissileFlight{exploded.slot, dx};
  }
  // MOV DI,SI: an invaded station loses energy to each missile, the SUB written, and goes when it runs out.
  const std::uint8_t energy = struck.Get(SlotByte::Energy);
  struck.Set(SlotByte::Energy, static_cast<std::uint8_t>(energy - INVADED_STATION_HIT));
  if (energy >= INVADED_STATION_HIT)
  {
    return MissileFlight{hit, dx};
  }
  const Explosion destroyed = ExplodeObject(_state, _hardware, struck, _backward, exploded.bx);
  _state.Set(DS.invadedStationDestroyed, 1);
  _state.Set(DS.thargoidInvasionActive, 0);
  return MissileFlight{destroyed.slot, destroyed.dx.value_or(dx)};
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
// KillPlayer's: DrawSunOrPlanet goes on with the AX StartPlayerDeathSound leaves, and UpdateMissileAi's contract compares it
// (KillPlayerEntry).
constexpr Machine::NativeContract KILLS_PLAYER{0, 0};
constexpr Machine::NativeContract FIRES_LASER{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
// TakeDamage's: UpdateMissileAi's contract compares the AX and BX it leaves (TakeDamageEntry).
constexpr Machine::NativeContract TAKES_DAMAGE{0, 0};
// RemoveAllMissiles': UpdateStationAi's contract compares the DI and ES it leaves (RemoveAllMissilesEntry).
constexpr Machine::NativeContract REMOVES_MISSILES{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr Machine::NativeContract CLOBBERS_ALL{REGISTER_ALL, 0};
// LaunchPlayerMissile's, SpawnPlayerWreckage's and ApplyEnemyLaserHit's: all but DS, which the original leaves alone and the flight
// loop goes on with.
constexpr Machine::NativeContract CLOBBERS_ALL_BUT_DS{static_cast<std::uint16_t>(REGISTER_ALL & ~Machine::REGISTER_DS), 0};
constexpr Machine::NativeContract LAUNCHES{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_BP | REGISTER_ES,
                                           0};
constexpr Machine::NativeContract LAUNCHES_SHIP{LAUNCHES.clobbers, FLAG_CARRY};
constexpr Machine::NativeContract LASER_BEAMS{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP | REGISTER_DI, 0};
// UpdateMissileAi's: DI, and DX, of which UpdateObjectsAndSpawn's next handler takes DL (UpdateMissileAiEntry).
constexpr Machine::NativeContract MISSILE_FLIGHT{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI | REGISTER_BP | REGISTER_ES, 0};
// DetonateEnergyBomb's: SI, DI and ES as the original leaves them, and DS (DetonateEnergyBombEntry).
constexpr Machine::NativeContract BOMB_DETONATED{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP, 0};
constexpr Machine::NativeContract CROSSHAIRS{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_BP | REGISTER_SI, FLAG_CARRY};

} // namespace

void DrawLaserBeamsEntry(Guest& _guest)
{
  // DrawLine's ES = DS and CLD, once a beam was a horizontal line: the contract compares ES.
  DrawLineOut(_guest, DrawLaserBeams(_guest.State()));
  _guest.Clobber(LASER_BEAMS);
}

void SpawnPlayerWreckageEntry(Guest& _guest)
{
  SpawnPlayerWreckage(_guest.State());
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
}

void LaunchPlayerMissileEntry(Guest& _guest)
{
  // The 64 bytes at DI, copied as REP MOVSW copies them, backwards when DF is set.
  LaunchPlayerMissile(_guest.State(), _guest.Regs().di, _guest.Flag(FLAG_DIRECTION));
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
}

void LaunchShipFromObjectEntry(Guest& _guest)
{
  const std::uint16_t launcher = _guest.Regs().di;
  const std::uint8_t kind = Low(_guest.Regs().dx);
  const std::optional<std::uint16_t> launched =
    LaunchShipFromObject(_guest.State(), ObjectSlot(_guest.State(), launcher), kind, _guest.Flag(FLAG_DIRECTION));
  // PUSH DI and POP DI keep the launcher in DI, which the contract compares.
  _guest.SetFlag(FLAG_CARRY, launched.has_value());
  if (launched && kind == KRAIT_LAUNCH)
  {
    // POP DI takes the launcher this path pushed, and RET the one pushed before it: the launch returns to CS:launcher, which
    // is not code. Only the split at 56B1, which never runs, passes DL=5.
    _guest.Push(launcher);
  }
  _guest.Clobber(LAUNCHES_SHIP);
}

void TryLaunchMissileAtPlayerEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  TryLaunchMissileAtPlayer(_guest.State(), ObjectSlot(_guest.State(), regs.di), regs.bx, _guest.Flag(FLAG_DIRECTION));
  _guest.Clobber(LAUNCHES);
}

void TryLaunchThargonEntry(Guest& _guest)
{
  TryLaunchThargon(_guest.State(), ObjectSlot(_guest.State(), _guest.Regs().di), _guest.Flag(FLAG_DIRECTION));
  _guest.Clobber(LAUNCHES);
}

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

void TakeDamageEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t damage = regs.ax;
  const bool podFlies = _guest.Get(DS.escapePodFrames) != 0;
  const std::uint8_t shield = _guest.Get(DS.foreShield);
  const std::optional<std::uint8_t> stepLength = TakeDamage(_guest.State(), damage);
  // What the original leaves in AX and BX, which UpdateMissileAi's contract compares after it: from 100h on, BX the shield and AX
  // what it could not take; below, AH the hit and AL the shield's byte less it, or, on a borrow, the excess, sign-extended. Then
  // KillPlayer's AL, the death sound's step length, and its STI.
  if (!podFlies)
  {
    if (damage >= OVERWHELMING_DAMAGE)
    {
      regs.bx = shield;
      regs.ax = static_cast<std::uint16_t>(damage - shield);
    }
    else
    {
      const auto left = static_cast<std::uint8_t>(shield - Low(damage));
      regs.ax = shield >= Low(damage) ? Join(Low(damage), left) : SignExtend(Negate(left));
    }
  }
  if (stepLength)
  {
    SetLow(regs.ax, *stepLength);
    _guest.Devices().EnableInterrupts();
  }
  _guest.Clobber(TAKES_DAMAGE);
}

void KillPlayerEntry(Guest& _guest)
{
  // StartPlayerDeathSound's STI, and the step length it leaves in AL.
  if (const std::optional<std::uint8_t> stepLength = KillPlayer(_guest.State()))
  {
    SetLow(_guest.Regs().ax, *stepLength);
    _guest.Devices().EnableInterrupts();
  }
  _guest.Clobber(KILLS_PLAYER);
}

void RemoveAllMissilesOut(Guest& _guest, bool _erased)
{
  // DI past the slots it looked at, by the count it loaded, which nothing it does changes, and ES on the video memory once
  // RemoveObject's EraseScannerBlip has erased a blip.
  Machine::Registers& regs = _guest.Regs();
  if (_erased)
  {
    regs.es = GameState::VIDEO_SEGMENT;
  }
  regs.di = static_cast<std::uint16_t>(DS.shipSlots.offset + LoopCount(_guest.Get(DS.objectSlotCount)) * ObjectSlot::BYTES);
}

void RemoveAllMissilesEntry(Guest& _guest)
{
  // UpdateStationAi's contract compares the DI and ES it leaves after it.
  RemoveAllMissilesOut(_guest, RemoveAllMissiles(_guest.State()).has_value());
  _guest.Clobber(REMOVES_MISSILES);
}

void DetonateEnergyBombEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // DI past the slots it looked at, by the count it loaded, and SI and ES as its explosions leave them: DrawSunOrPlanet goes on
  // with DI, and its contract compares SI and ES.
  const auto slots = static_cast<std::uint8_t>(_guest.Get(DS.objectSlotCount) - FIRST_SHIP_SLOT);
  const Detonation left = DetonateEnergyBomb(_guest.State(), _guest.Devices(), _guest.Flag(FLAG_DIRECTION));
  regs.di = static_cast<std::uint16_t>(DS.firstShipSlot.offset + LoopCount(slots) * ObjectSlot::BYTES);
  regs.si = left.si.value_or(regs.si);
  regs.es = left.es.value_or(regs.es);
  _guest.Clobber(BOMB_DETONATED);
}

void ExplodeObjectEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  (void)ExplodeObject(_guest.State(), _guest.Devices(), ObjectSlot(_guest.State(), regs.di), _guest.Flag(FLAG_DIRECTION), regs.bx);
  _guest.Clobber(CLOBBERS_ALL);
}

void UpdateMissileAiEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const MissileFlight flight =
    UpdateMissileAi(_guest.State(), _guest.Devices(), ObjectSlot(_guest.State(), regs.di), _guest.Flag(FLAG_DIRECTION), regs.bx, regs.dx);
  // DI, and DX, of which UpdateObjectsAndSpawn's next handler takes DL for its range's box.
  regs.di = flight.slot;
  regs.dx = flight.dx;
  _guest.Clobber(MISSILE_FLIGHT);
}

void FindShipInCrosshairsEntry(Guest& _guest)
{
  const CrosshairTarget target = FindShipInCrosshairs(_guest.State());
  _guest.Regs().di = target.di;
  _guest.SetFlag(FLAG_CARRY, target.slot.has_value());
  _guest.Clobber(CROSSHAIRS);
}

void ResolveLaserFireEntry(Guest& _guest)
{
  // DrawLine's CLD, once a beam was a horizontal line: the direction flag is no register the contract can leave to it.
  DrawLineOut(_guest, ResolveLaserFire(_guest.State(), _guest.Devices(), _guest.Flag(FLAG_DIRECTION)));
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
}

void ApplyEnemyLaserHitEntry(Guest& _guest)
{
  // DrawLine's ES = DS and CLD, once the beam was a horizontal line: the direction flag is no register the contract can leave to it,
  // and ResolveLaserFire, next in the flight loop, copies by it.
  DrawLineOut(_guest, ApplyEnemyLaserHit(_guest.State(), _guest.Devices()));
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
}

void TryFireLaserAtPlayerEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  TryFireLaserAtPlayer(_guest.State(), ObjectSlot(_guest.State(), regs.di), regs.ax, regs.bx);
  _guest.Clobber(FIRES_LASER);
}

void CreditKillEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const KillCredit credit = CreditKill(_guest.State(), ObjectSlot(_guest.State(), regs.di));
  // The registers the original leaves, which the contract compares, as UpdateMissileAi's does after it. MOV AL,[DI+31h] first.
  SetLow(regs.ax, credit.bounty);
  if (credit.paidTenths)
  {
    // PayBounty's: POP AX brings the tenths back; BX the 0 AddCredits takes for the high word, CX as ShowBountyMessage leaves it
    // and SI as FormatCredits does; then AL is the type Routine8C51 finds, the repair it takes, or AX the repair's message. PUSH
    // DI / POP DI keep DI.
    regs.ax = *credit.paidTenths;
    regs.bx = 0;
    regs.cx = BlankedBountyZeros(_guest.State()).triesLeft;
    regs.si = DS.creditBalanceText.offset;
    if (credit.killed)
    {
      SetLow(regs.ax, credit.killed->type);
      if (credit.killed->thargoidOrThargon)
      {
        SetLow(regs.ax, credit.killed->thargoid ? THARGOID_REPAIR : THARGON_REPAIR);
      }
    }
    if (credit.repaired)
    {
      regs.ax = DS.navCompRepairedMessage.offset;
    }
  }
  else if (credit.zone)
  {
    // MOV AX,500 before the JE that would pay it, then AL as InSafeZone leaves it, or the crime added.
    regs.ax = Join(High(THARGOID_BOUNTY), credit.crime.value_or(credit.zone->rest));
  }
  _guest.Clobber(PRESERVES_ALL);
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

constexpr std::array ENTRIES = {
  NativeEntry{0x0630, "DrawLaserSights", &DrawLaserSightsEntry, LASER_SIGHTS},
  NativeEntry{0x066C, "GetViewLaser", &GetViewLaserEntry, VIEW_LASER},
  NativeEntry{0x0A9A, "DrawLaserBeams", &DrawLaserBeamsEntry, LASER_BEAMS},
  NativeEntry{0x2C9B, "TakeDamage", &TakeDamageEntry, TAKES_DAMAGE},
  NativeEntry{0x2ED6, "DetonateEnergyBomb", &DetonateEnergyBombEntry, BOMB_DETONATED},
  NativeEntry{0x2FE3, "SpawnPlayerWreckage", &SpawnPlayerWreckageEntry, CLOBBERS_ALL_BUT_DS},
  NativeEntry{0x3115, "KillPlayer", &KillPlayerEntry, KILLS_PLAYER},
  NativeEntry{0x4C8C, "InitMissile", &InitMissileEntry, CLOBBERS_AX_BX},
  NativeEntry{0x4F9F, "RemoveAllMissiles", &RemoveAllMissilesEntry, REMOVES_MISSILES},
  NativeEntry{0x4FC1, "ExplodeObject", &ExplodeObjectEntry, CLOBBERS_ALL},
  NativeEntry{0x50FE, "TallyMaskMissionKill", &TallyMaskMissionKillEntry, PRESERVES_ALL},
  NativeEntry{0x518B, "TryFireLaserAtPlayer", &TryFireLaserAtPlayerEntry, FIRES_LASER},
  NativeEntry{0x5242, "LaunchPlayerMissile", &LaunchPlayerMissileEntry, CLOBBERS_ALL_BUT_DS},
  NativeEntry{0x534E, "LaunchShipFromObject", &LaunchShipFromObjectEntry, LAUNCHES_SHIP},
  NativeEntry{0x543A, "TryLaunchMissileAtPlayer", &TryLaunchMissileAtPlayerEntry, LAUNCHES},
  NativeEntry{0x5471, "TryLaunchThargon", &TryLaunchThargonEntry, LAUNCHES},
  NativeEntry{0x54F2, "UpdateMissileAi", &UpdateMissileAiEntry, MISSILE_FLIGHT},
  NativeEntry{0x8A46, "FindShipInCrosshairs", &FindShipInCrosshairsEntry, CROSSHAIRS},
  NativeEntry{0x8AC2, "ResolveLaserFire", &ResolveLaserFireEntry, CLOBBERS_ALL_BUT_DS},
  NativeEntry{0x8B8B, "CheckMissileTargetDestroyed", &CheckMissileTargetDestroyedEntry, PRESERVES_ALL},
  NativeEntry{0x8BC6, "CreditKill", &CreditKillEntry, PRESERVES_ALL},
  NativeEntry{0x8C51, "Routine8C51", &Routine8C51Entry, THARGOID_TEST},
  NativeEntry{0x8C8E, "ApplyEnemyLaserHit", &ApplyEnemyLaserHitEntry, CLOBBERS_ALL_BUT_DS},
  NativeEntry{0x8ECF, "UseMaskingDevice", &UseMaskingDeviceEntry, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> CombatEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
