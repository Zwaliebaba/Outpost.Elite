#include "pch.h"

#include "Combat.h"

#include "Ai.h"
#include "Arithmetic.h"
#include "CombatTables.h"
#include "Docking.h"
#include "Market.h"
#include "Maths.h"
#include "Scene.h"
#include "Ships.h"
#include "Sound.h"
#include "StateWriter.h"
#include "Text.h"
#include "Video.h"

#include <span>

namespace Elite
{

namespace
{

// The laser sights: a 16x16 sprite at the centre of the space view's 64-byte rows, 128 bytes a laser type, from row 56, which is
// row 55 of the space view, which starts a row into the drawing buffer.
constexpr std::uint16_t VIEW_ROW_BYTES = 64;
constexpr std::uint16_t SIGHTS_OFFSET = 55 * VIEW_ROW_BYTES + 30;
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

// The 24-bit position, by axis: the low words and the high bytes; and the velocity, signed bytes.
constexpr std::array<ObjectWord, 3> POSITION_LOW = {&SpaceObject::x, &SpaceObject::y, &SpaceObject::z};
constexpr std::array<ObjectByte, 3> POSITION_HIGH = {&SpaceObject::xHigh, &SpaceObject::yHigh, &SpaceObject::zHigh};
constexpr std::array<ObjectByte, 3> VELOCITY = {&SpaceObject::velocityX, &SpaceObject::velocityY, &SpaceObject::velocityZ};

// _value as the 24-bit coordinate _axis (0 x, 1 y, 2 z) of _slot: the low word, then its sign extended into the high byte.
void SetCoordinate(SpaceObject& _slot, std::size_t _axis, std::uint16_t _value)
{
  _slot.*POSITION_LOW[_axis] = _value;
  _slot.*POSITION_HIGH[_axis] = Low(SignWord(_value));
}

// SAR r/m8,1.
[[nodiscard]] std::uint8_t HalveSigned(std::uint8_t _value) noexcept
{
  return static_cast<std::uint8_t>(static_cast<std::int8_t>(_value) >> 1);
}

// gameOverFrames | escapePodFrames | maskingBackgroundColor, ORed together as the original does: none of them may run when a ship
// fires, so it may not unless this is 0.
[[nodiscard]] std::uint8_t FiringBlocked(const GameState& _state) noexcept
{
  return static_cast<std::uint8_t>(_state.Data().flight.gameOverFrames | _state.Data().playerShip.escapePodFrames |
                                   _state.Data().drawing.maskingBackgroundColor);
}

// The slot's type: bits 1-5 of its type byte.
[[nodiscard]] std::uint8_t TypeOf(const SpaceObject& _slot) noexcept
{
  return static_cast<std::uint8_t>((_slot.type >> 1) & TYPE_MASK);
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

// One beam: DrawLine from (_x, the bottom row) to _target, its x in the low byte and its row in the high.
void DrawBeam(GameState& _state, std::uint8_t _x, std::uint16_t _target)
{
  DrawLine(_state, Low(_target), High(_target), _x, BEAM_ROW);
}

// The crosshairs' test on one axis: |the view coordinate _axis of _slot| * 256 over its view z, below _radius.
[[nodiscard]] bool ScaledWithinRadius(const SpaceObject& _slot, ObjectWord _axis, std::uint16_t _radius)
{
  std::uint16_t magnitude = _slot.*_axis;
  if ((magnitude & 0x8000) != 0)
  {
    magnitude = Negate(magnitude);
  }
  // The 32-bit dividend: the magnitude * 256, with its sign in the top byte.
  const std::uint32_t dividend = (std::uint32_t{Join(High(SignWord(magnitude)), High(magnitude))} << 16) | Join(Low(magnitude), 0);
  return DivideWord(dividend, _slot.viewZ).quotient < _radius;
}

// _tenths shown (ShowBountyMessage) and added to the credits (AddCredits); then, while a mis-jump's countdown runs, the
// kill of a Thargon or a Thargoid counts it down by 5 or 35, the SUB written before the 1 written over a borrow or a 0, and
// at 1 the Nav-Comp's repair is announced.
void PayBounty(GameState& _state, const SpaceObject& _slot, std::uint16_t _tenths)
{
  ShowBountyMessage(_state, _tenths);
  AddCredits(_state, _tenths);
  if (_state.Data().commander.witchspaceCountdown == 0)
  {
    return;
  }
  const ThargoidTest killed = Routine8C51(_slot);
  if (!killed.thargoidOrThargon)
  {
    return;
  }
  const std::uint8_t repair = killed.thargoid ? THARGOID_REPAIR : THARGON_REPAIR;
  const std::uint8_t countdown = _state.Data().commander.witchspaceCountdown;
  const auto left = static_cast<std::uint8_t>(countdown - repair);
  _state.Data().commander.witchspaceCountdown = left;
  if (countdown < repair || left == 0)
  {
    _state.Data().commander.witchspaceCountdown = 1;
  }
  if (_state.Data().commander.witchspaceCountdown != 1)
  {
    return;
  }
  _state.Data().messages.messagePointer = Message::NavCompRepaired;
  _state.Data().messages.messageFrames = REPAIR_MESSAGE_FRAMES;
}

// The fragments of an exploding object: _count of them, at least one, each a Splinter in a debris slot copied from _object
// (CopyObject), its velocity halved and scattered by a random -15..16 on each axis (halved again for an asteroid the mining laser hit), and
// run for a frame, or for eleven when the station explodes.
void SpawnFragments(GameState& _state, const SpaceObject& _object, std::uint16_t _count)
{
  for (std::uint16_t fragments = _count; fragments != 0; --fragments)
  {
    SpaceObject& debris = _state.Slot(FindDebrisSlot(_state));
    CopyObject(_object, debris);
    debris.aiState = 0;
    const std::uint16_t spin = NextRandom(_state);
    debris.SpinRoll() = Low(spin);
    debris.SpinPitch() = High(spin);
    // Each scatter is five bits of the random word less 0Fh: x from bits 0-4, y from bits 8-12, z from bits 3-7.
    const auto scatter = static_cast<std::uint16_t>(spin & FRAGMENT_SCATTER_MASK);
    auto scatterX = static_cast<std::uint8_t>(Low(scatter) - FRAGMENT_SCATTER_CENTER);
    auto scatterY = static_cast<std::uint8_t>(High(scatter) - FRAGMENT_SCATTER_CENTER);
    debris.velocityX = HalveSigned(debris.velocityX);
    debris.velocityY = HalveSigned(debris.velocityY);
    if (_state.Data().combat.miningLaserOnAsteroid == 1)
    {
      scatterX = HalveSigned(scatterX);
      scatterY = HalveSigned(scatterY);
    }
    debris.velocityX = static_cast<std::uint8_t>(debris.velocityX + scatterX);
    debris.velocityY = static_cast<std::uint8_t>(debris.velocityY + scatterY);
    auto scatterZ = static_cast<std::uint8_t>((Low(static_cast<std::uint16_t>(spin >> 3)) & 0x1F) - FRAGMENT_SCATTER_CENTER);
    debris.velocityZ = HalveSigned(debris.velocityZ);
    if (_state.Data().combat.miningLaserOnAsteroid == 1)
    {
      scatterZ = HalveSigned(scatterZ);
    }
    debris.velocityZ = static_cast<std::uint8_t>(debris.velocityZ + scatterZ);
    debris.flags = FLAG_RESTING;
    if (_state.Data().combat.miningLaserOnAsteroid == 1 && NextRandom(_state) < MINERALS_ODDS)
    {
      debris.flags = static_cast<std::uint8_t>(debris.flags | FLAG_MINERALS);
    }
    debris.energy = 0;
    debris.cargo = 0;
    debris.fragments = 0;
    debris.age = 0;
    debris.behaviorClass = DEBRIS_CLASS;
    auto lifetime = static_cast<std::uint8_t>(Low(NextRandom(_state)) & FRAGMENT_LIFETIME_MASK);
    if (_state.Data().combat.miningLaserOnAsteroid == 1)
    {
      lifetime = static_cast<std::uint8_t>(lifetime + MINED_FRAGMENT_LIFETIME);
    }
    debris.lifetime = static_cast<std::uint8_t>(lifetime + FRAGMENT_LIFETIME);
    debris.type = SPLINTER_ACTIVE;
    (void)UpdateDebrisAi(debris);
    if (_state.Data().combat.explodingStation == 1)
    {
      // A station's fragments are flung ten frames further at once.
      for (std::uint16_t steps = STATION_FRAGMENT_STEPS; steps != 0; --steps)
      {
        (void)UpdateDebrisAi(debris);
      }
    }
  }
}

// DropCargo, the tail ExplodeObject ends in: the barrels _object leaves, one when it carries the device, else a random byte /
// (255 / (cargo + 1) + 1), its first divide by 0 into the game's trap for a cargo of FFh. Each barrel goes in a free ship slot, copied
// from _object (CopyObject), with a random heading, its velocity set and moved; once no slot is free, none is for the rest.
void DropCargo(GameState& _state, const SpaceObject& _object)
{
  std::uint16_t barrels = 1;
  if ((_object.flags & FLAG_DEVICE) == 0)
  {
    // No cargo, no barrels; else FFh over (cargo + 1), and the random byte over that + 1, each an 8-bit divide.
    const std::uint8_t cargo = _object.cargo;
    if (cargo == 0)
    {
      return;
    }
    const std::uint8_t share = DivideByte(0x00FF, static_cast<std::uint8_t>(cargo + 1)).quotient;
    barrels = DivideByte(Low(NextRandom(_state)), static_cast<std::uint8_t>(share + 1)).quotient;
    if (barrels == 0)
    {
      return;
    }
  }
  // A barrel at a time, until the count runs out.
  for (; barrels != 0; --barrels)
  {
    const SlotSearch free = FindFreeShipSlot(_state);
    if (!free.found)
    {
      return;
    }
    SpaceObject& barrel = _state.Slot(free.slot);
    // _object's flags, kept from before the copy and the barrel's template: the device's bit (5) becomes the barrel's bit 6.
    const std::uint8_t flags = _object.flags;
    CopyObject(_object, barrel);
    InitCargoBarrel(barrel);
    barrel.flags = FLAG_RESTING;
    barrel.flags = static_cast<std::uint8_t>(barrel.flags | ((flags & FLAG_DEVICE) << 1));
    const std::uint16_t heading = NextRandom(_state);
    barrel.pitch = heading;
    barrel.yaw = Swap(heading);
    (void)ComputeVelocity(_state, barrel);
    (void)MoveObject(barrel);
  }
}

// _target destroyed by the player's laser: credited (CreditKill), a missile locked on it unlocked
// (CheckMissileTargetDestroyed), and exploded (ExplodeObject); then the beams, and the laser's flags cleared.
void DestroyTarget(GameState& _state, Host& _host, SpaceObject& _target)
{
  (void)CreditKill(_state, _target);
  CheckMissileTargetDestroyed(_state, _target);
  ExplodeObject(_state, _target);
  DrawLaserBeams(_state, _host);
  _state.Data().combat.miningLaserOnAsteroid = 0;
  _state.Data().playerShip.laserFiring = 0;
}

// The player's laser hits _target, which is marked hostile, with the impact's sound; the damage is the
// laser's type + 1, and a mining laser marks an asteroid mined. A station's hit cancels the docking computer and is a crime of
// 28h, added to legalStatus up to FFh, or in an invasion does half the damage, rounded up (negated, halved, negated back). The
// damage goes on the aggression, up to FFh, and off the energy, each written and then FFh or nothing over it. Returns whether
// the target is to be destroyed (DestroyTarget): it ran out of energy and is not indestructible, or is an invaded station; an
// indestructible one is left at 0, and a station's is a crime of 28h, which cancels the docking computer only when it carries.
[[nodiscard]] bool HitTarget(GameState& _state, Host& _host, SpaceObject& _target)
{
  _target.flags = static_cast<std::uint8_t>(_target.flags | FLAG_HOSTILE);
  StartImpactSound(_state);
  const std::uint8_t laser = _state.Data().flight.firingLaserType;
  if (laser == MINING_LASER && TypeOf(_target) == TYPE_ASTEROID)
  {
    _state.Data().combat.miningLaserOnAsteroid = 1;
  }
  auto damage = static_cast<std::uint8_t>(laser + 1);
  const bool invasion = _state.Data().commander.thargoidInvasionActive == 1;
  if (IsStation(_target).station)
  {
    CancelDockingComputer(_state, _host);
    if (invasion)
    {
      damage = static_cast<std::uint8_t>(0u - HalveSigned(static_cast<std::uint8_t>(0u - damage)));
    }
    else
    {
      AddSaturating(_state.Data().commander.legalStatus, LEGAL_STATUS_PER_STATION_HIT);
    }
  }
  AddSaturating(_target.aggression, damage);
  const std::uint8_t energy = _target.energy;
  _target.energy = static_cast<std::uint8_t>(energy - damage);
  if (energy >= damage)
  {
    return false;
  }
  if ((_target.flags & FLAG_INDESTRUCTIBLE) == 0)
  {
    return true;
  }
  _target.energy = 0;
  if (!IsStation(_target).station)
  {
    return false;
  }
  if (_state.Data().commander.thargoidInvasionActive == 1)
  {
    _state.Data().commander.invadedStationDestroyed = 1;
    _state.Data().commander.thargoidInvasionActive = 0;
    return true;
  }
  // The sum written, then FFh over it when it carries, and only an add that carries cancels the docking computer.
  const unsigned status = _state.Data().commander.legalStatus + unsigned{LEGAL_STATUS_PER_STATION_HIT};
  _state.Data().commander.legalStatus = static_cast<std::uint8_t>(status);
  if (status > 0xFF)
  {
    _state.Data().commander.legalStatus = 0xFF;
    CancelDockingComputer(_state, _host);
  }
  return false;
}

// The random byte masked by _mask, less _center, sign-extended and added to _drift: a random velocity about the wreck's drift on one axis.
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
  // The laser type times 128: 128 bytes a laser type.
  const std::span<std::uint8_t> view = BytesOf(_state.Data().drawing.drawingBuffer).subspan(VIEW_ROW_BYTES);
  std::size_t row = SIGHTS_OFFSET;
  const std::span<const std::uint8_t> sprites(LASER_SIGHTS);
  std::size_t sprite = std::size_t{*laser} << 7;
  for (std::uint16_t rows = SIGHTS_ROWS; rows != 0; --rows)
  {
    // The buffer's words are big-endian pixels: byte-swapped before the mask and the ink, and back after.
    for (int half = 0; half < 2; ++half)
    {
      const std::size_t pixels = row + 2 * static_cast<std::size_t>(half);
      const std::uint16_t andMask = WordIn(sprites, sprite + 2 * static_cast<std::size_t>(half));
      const std::uint16_t orMask = WordIn(sprites, sprite + 2 * static_cast<std::size_t>(half) + 4);
      SetWordIn(view, pixels, Swap(static_cast<std::uint16_t>((Swap(WordIn(view, pixels)) & andMask) | orMask)));
    }
    row += VIEW_ROW_BYTES;
    sprite += SIGHTS_ROW_BYTES;
  }
}

std::optional<std::uint8_t> GetViewLaser(const GameState& _state)
{
  // The view, 0-3, from viewAngle's high byte; the carry starts clear.
  const auto view = static_cast<std::uint16_t>((High(_state.Data().playerShip.viewAngle) >> 1) & 3);
  const std::uint8_t mount = VIEW_LASER_MOUNT[view];
  // laserMountsFitted rotated right through that clear carry, by the mount, a bit at a time as the 8088 does: the carry ends as
  // bit mount-1 of laserMountsFitted.
  std::uint8_t fitted = _state.Data().commander.laserMountsFitted;
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
  // laserMountTypes shifted right by (mount - 1) * 2, two bits kept: the mount's 2-bit field.
  const auto shift = static_cast<std::uint8_t>((mount - 1) * 2);
  const std::uint8_t types = _state.Data().commander.laserMountTypes;
  return static_cast<std::uint8_t>((shift >= 8 ? 0 : types >> shift) & 3);
}

void DrawLaserBeams(GameState& _state, Host& _host)
{
  // The point the beams end at, a random 0-3 on 7Eh for x and on 3Eh for the row: its x in the low byte and its row in the high.
  const auto scatter = static_cast<std::uint16_t>(NextRandom(_state) & BEAM_SCATTER_MASK);
  const std::uint16_t target =
    Join(static_cast<std::uint8_t>(High(scatter) + BEAM_TARGET_ROW), static_cast<std::uint8_t>(Low(scatter) + BEAM_TARGET_X));
  // Each beam watched for the space view, which the renderer draws in its place (ADR-035); no byte of the state is written.
  const auto beam = [&_state, &_host, target](std::uint8_t _x)
  {
    _host.BeginSpaceViewObject();
    DrawBeam(_state, _x, target);
    _host.EndSpaceViewRendered();
    _host.NoteSpaceViewBeam(SnapshotBeam{.fromX = std::int16_t{_x},
                                         .fromRow = std::int16_t{BEAM_ROW},
                                         .toX = std::int16_t{Low(target)},
                                         .toRow = std::int16_t{High(target)},
                                         .color = std::uint8_t{_state.Data().drawing.drawColor}});
  };
  const std::uint8_t type = _state.Data().flight.firingLaserType;
  if ((type & 1) != 0)
  {
    if ((type & 2) != 0)
    {
      // Type 3: all four, the inner pair in the toggled colour, the outer in the other.
      _state.Data().drawing.drawColor = std::uint8_t{_state.Data().combat.laserBeamColorToggle};
      _state.Data().combat.laserBeamColorToggle = static_cast<std::uint8_t>(_state.Data().combat.laserBeamColorToggle ^ BEAM_TOGGLE);
      beam(BEAM_INNER_LEFT);
      beam(BEAM_INNER_RIGHT);
      _state.Data().drawing.drawColor = static_cast<std::uint8_t>(_state.Data().drawing.drawColor ^ BEAM_TOGGLE);
    }
    else
    {
      // Type 1: the outer pair.
      const std::uint8_t color = NextBeamColor(_state.Data().combat.laserBeamColor, true);
      _state.Data().combat.laserBeamColor = color;
      _state.Data().drawing.drawColor = color;
    }
    beam(BEAM_OUTER_LEFT);
    beam(BEAM_OUTER_RIGHT);
    return;
  }
  // Types 0 and 2: one side a shot, alternately.
  const std::uint8_t color = NextBeamColor(_state.Data().combat.laserBeamColor, (type & 2) != 0);
  _state.Data().combat.laserBeamColor = color;
  _state.Data().drawing.drawColor = color;
  const auto side = static_cast<std::uint8_t>(_state.Data().combat.laserBeamSide ^ 1);
  _state.Data().combat.laserBeamSide = side;
  if (side != 0)
  {
    beam(BEAM_OUTER_LEFT);
    beam(BEAM_INNER_LEFT);
    return;
  }
  beam(BEAM_OUTER_RIGHT);
  beam(BEAM_INNER_RIGHT);
}

void ExplodeObject(GameState& _state, SpaceObject& _object)
{
  TallyMaskMissionKill(_state, _object);
  const bool drawn = (_object.type & SLOT_DRAWN) != 0;
  // RemoveObject, whether or not it was drawn.
  (void)RemoveObject(_object);
  if (!drawn)
  {
    return;
  }
  StartExplosionSound(_state);
  _state.Data().combat.explodingStation = 1;
  if (!IsStation(_object).station)
  {
    _state.Data().combat.explodingStation = 0;
  }
  // Its fragments, when it has any; then the cargo it drops.
  if (const std::uint8_t fragments = _object.fragments; fragments != 0)
  {
    SpawnFragments(_state, _object, fragments);
  }
  DropCargo(_state, _object);
}

void TallyMaskMissionKill(GameState& _state, const SpaceObject& _slot)
{
  if (_state.Data().commander.maskMissionShipsLeft == 0)
  {
    return;
  }
  if (TypeOf(_slot) != TYPE_ASP || _slot.bounty != MASK_SHIP_BOUNTY)
  {
    return;
  }
  _state.Data().commander.maskMissionShipsLeft = static_cast<std::uint8_t>(_state.Data().commander.maskMissionShipsLeft - 1);
  if ((_slot.flags & FLAG_DEVICE) != 0)
  {
    _state.Data().commander.maskShipDestroyed = 1;
  }
}

void TryFireLaserAtPlayer(GameState& _state, SpaceObject& _slot, std::uint16_t _pitchError, std::uint16_t _yawError)
{
  if ((_slot.flags & FLAG_BLIP_DRAWN) == 0)
  {
    return;
  }
  if (Low(NextRandom(_state)) >= _slot.aggression)
  {
    return;
  }
  if (CheckSafeZoneHoldFire(_state, _slot) || FiringBlocked(_state) != 0)
  {
    return;
  }
  // Each shot calms the ship by 5, unless that would take it below 20.
  const auto calmer = static_cast<std::uint8_t>(_slot.aggression - CALMER_STEP);
  if (calmer >= CALMEST)
  {
    _slot.aggression = calmer;
  }
  // VectorWithinBox on x = 0, the yaw error and the pitch error.
  const auto yaw = static_cast<std::int16_t>(_yawError);
  const auto pitch = static_cast<std::int16_t>(_pitchError);
  if (!VectorWithinBox(Vector{0, yaw, pitch}, LASER_GRAZE_BOX))
  {
    return;
  }
  _state.Data().combat.playerHitBy = _state.IndexOf(_slot);
  _state.Data().combat.playerHitPending = HIT_GRAZED;
  const std::uint8_t depth = _slot.cameraZHigh;
  _state.Data().combat.playerHitByDepth = depth;
  // Again within 70, on what the first test leaves: x is now the depth, cameraZHigh, where it was 0, and the errors are their
  // magnitudes, which the test takes again.
  if (VectorWithinBox(Vector{depth, yaw, pitch}, LASER_HIT_BOX))
  {
    _state.Data().combat.playerHitPending = HIT_SQUARE;
  }
}

void TryLaunchMissileAtPlayer(GameState& _state, SpaceObject& _slot, std::uint16_t _odds)
{
  if (_state.Data().commander.killCount < MISSILE_KILLS_NEEDED || (_slot.flags & FLAG_HOSTILE) == 0)
  {
    return;
  }
  if (CheckSafeZoneHoldFire(_state, _slot) || _slot.missiles == 0 || FiringBlocked(_state) != 0)
  {
    return;
  }
  if (NextRandom(_state) >= _odds)
  {
    return;
  }
  if (LaunchShipFromObject(_state, _slot, MISSILE_LAUNCH))
  {
    _slot.missiles = static_cast<std::uint8_t>(_slot.missiles - 1);
  }
}

void TryLaunchThargon(GameState& _state, SpaceObject& _slot)
{
  if (!IsThargoidType(_slot) || _slot.thargons == 0)
  {
    return;
  }
  if (NextRandom(_state) >= THARGON_ODDS)
  {
    return;
  }
  if (LaunchShipFromObject(_state, _slot, THARGON_LAUNCH))
  {
    _slot.thargons = static_cast<std::uint8_t>(_slot.thargons - 1);
  }
}

CrosshairTarget FindShipInCrosshairs(GameState& _state)
{
  // objectSlotCount less 2 slots from the station, a count of 0 running 65,536 times. nearestZ is the nearest z so far.
  CrosshairTarget target{std::nullopt};
  std::uint16_t nearestZ = 0xFFFF;
  SlotIndex slot = SlotIndex::Station;
  const auto slots = static_cast<std::uint8_t>(_state.Data().flight.objectSlotCount - 2);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    const SpaceObject& object = _state.Slot(slot);
    const std::uint8_t type = object.type;
    if ((type & (SLOT_DRAWN | SLOT_ACTIVE)) == (SLOT_DRAWN | SLOT_ACTIVE) && (object.flags & CROSSHAIR_IGNORED) != CROSSHAIR_IGNORED)
    {
      // The type's shipTargetRadius word, its bytes swapped (the radius * 256), over z, plus 2: against |x| * 256 / z and
      // |y| * 256 / z.
      const std::uint16_t radius = Swap(SHIP_TARGET_RADIUS[(type & (TYPE_MASK << 1)) / 2]);
      const std::uint16_t within = Offset(DivideWord(radius, object.viewZ).quotient, 2);
      if (ScaledWithinRadius(object, &SpaceObject::viewX, within) && ScaledWithinRadius(object, &SpaceObject::viewY, within) &&
          object.viewZ < nearestZ)
      {
        target.slot = slot;
        nearestZ = object.viewZ;
      }
    }
    slot = SlotAfter(slot);
  }
  return target;
}

void ResolveLaserFire(GameState& _state, Host& _host)
{
  if (_state.Data().playerShip.laserFiring != 1)
  {
    return;
  }
  if (const CrosshairTarget target = FindShipInCrosshairs(_state); target.slot)
  {
    SpaceObject& hit = _state.Slot(*target.slot);
    if (HitTarget(_state, _host, hit))
    {
      DestroyTarget(_state, _host, hit);
      return;
    }
  }
  DrawLaserBeams(_state, _host);
  StartLaserSound(_state);
  _state.Data().combat.miningLaserOnAsteroid = 0;
  _state.Data().playerShip.laserFiring = 0;
}

void CheckMissileTargetDestroyed(GameState& _state, const SpaceObject& _slot)
{
  if (_state.Data().playerShip.missileState != MISSILE_LOCKED || _state.IndexOf(_slot) != _state.Data().flight.missileTarget)
  {
    return;
  }
  _state.Data().messages.messagePointer = Message::MissileTargetDestroyed;
  _state.Data().messages.messageFrames = MISSILE_MESSAGE_FRAMES;
  _state.Data().playerShip.missileState = 0;
}

KillCredit CreditKill(GameState& _state, SpaceObject& _slot)
{
  const std::uint8_t bounty = _slot.bounty;
  KillCredit credit{std::nullopt, std::nullopt, std::nullopt};
  if (bounty == 0)
  {
    return credit;
  }
  // One more, in 8 bits: a count that reaches FFh is written and then FEh over it; one that wraps to 0 stays there.
  const auto kills = static_cast<std::uint8_t>(_state.Data().commander.killCount + 1);
  _state.Data().commander.killCount = kills;
  if (kills == 0xFF)
  {
    _state.Data().commander.killCount = MOST_KILLS;
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
      const std::uint8_t crime = police ? CRIME_AGAINST_POLICE : CRIME;
      credit.crime = crime;
      AddSaturating(_state.Data().commander.legalStatus, crime);
      return credit;
    }
    tenths = THARGOID_BOUNTY;
  }
  credit.paidTenths = tenths;
  PayBounty(_state, _slot, tenths);
  return credit;
}

void ApplyEnemyLaserHit(GameState& _state, Host& _host)
{
  if (_state.Data().combat.playerHitPending == 0)
  {
    return;
  }
  StartPlayerHitSound(_state);
  const SpaceObject& attacker = _state.Slot(_state.Data().combat.playerHitBy);
  if ((attacker.type & SLOT_DRAWN) != 0)
  {
    // A beam from the attacker on screen, where ProjectToScreen puts it, to a random point on an edge of the view: the top edge,
    // the bottom, the left or the right, by four ranges of the random word.
    const Vector view{static_cast<std::int16_t>(attacker.viewX), static_cast<std::int16_t>(attacker.viewY),
                      static_cast<std::int16_t>(attacker.viewZ)};
    const ScreenPoint from = ProjectToScreen(view);
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
    _state.Data().drawing.drawColor = ENEMY_BEAM_COLOR;
    // Watched for the space view, which the renderer draws in its place (ADR-035); no byte of the state is written.
    _host.BeginSpaceViewObject();
    DrawClippedLine(_state, static_cast<std::uint16_t>(from.x), static_cast<std::uint16_t>(from.y), edgeX, edgeRow);
    _host.EndSpaceViewRendered();
    _host.NoteSpaceViewBeam(SnapshotBeam{.fromX = static_cast<std::int16_t>(from.x),
                                         .fromRow = static_cast<std::int16_t>(from.y),
                                         .toX = static_cast<std::int16_t>(edgeX),
                                         .toRow = static_cast<std::int16_t>(edgeRow),
                                         .color = ENEMY_BEAM_COLOR});
  }
  _state.Data().combat.playerHitPending = 0;
  // Bit 7 of the attacker's depth byte: behind, so the aft shield takes it. 0Fh comes off the shield, written; on a borrow 0 is
  // written over it, and what the shield could not take, the wrapped byte negated and sign-extended, comes off the energy,
  // written and then 0 over it on a borrow.
  std::uint8_t& shield =
    (_state.Data().combat.playerHitByDepth & 0x80) != 0 ? _state.Data().playerShip.aftShield : _state.Data().playerShip.foreShield;
  const std::uint8_t strength = shield;
  shield = static_cast<std::uint8_t>(strength - SHIELD_HIT);
  if (strength >= SHIELD_HIT)
  {
    return;
  }
  const std::uint8_t wrapped = shield;
  shield = 0;
  const std::uint16_t excess = SignExtend(Negate(wrapped));
  const std::uint16_t energy = _state.Data().playerShip.playerEnergy;
  _state.Data().playerShip.playerEnergy = static_cast<std::uint16_t>(energy - excess);
  if (energy >= excess)
  {
    return;
  }
  _state.Data().playerShip.playerEnergy = 0;
  _state.Data().playerShip.playerDead = 1;
}

void TakeDamage(GameState& _state, std::uint16_t _damage)
{
  if (_state.Data().playerShip.escapePodFrames != 0)
  {
    return;
  }
  std::uint16_t excess = 0;
  if (_damage >= OVERWHELMING_DAMAGE)
  {
    // The shield takes all it holds, and the energy the rest.
    excess = static_cast<std::uint16_t>(_damage - _state.Data().playerShip.foreShield);
    _state.Data().playerShip.foreShield = 0;
  }
  else
  {
    // foreShield less the damage's low byte, in 8 bits, written back; on a borrow 0 over it, and the excess is the wrapped byte
    // negated and sign-extended.
    const std::uint8_t damage = Low(_damage);
    const std::uint8_t shield = _state.Data().playerShip.foreShield;
    const auto left = static_cast<std::uint8_t>(shield - damage);
    _state.Data().playerShip.foreShield = left;
    if (shield >= damage)
    {
      return;
    }
    _state.Data().playerShip.foreShield = 0;
    excess = SignExtend(Negate(left));
  }
  const std::uint16_t energy = _state.Data().playerShip.playerEnergy;
  _state.Data().playerShip.playerEnergy = static_cast<std::uint16_t>(energy - excess);
  if (energy >= excess)
  {
    return;
  }
  static_cast<void>(KillPlayer(_state));
  _state.Data().playerShip.playerEnergy = 0;
}

void DetonateEnergyBomb(GameState& _state)
{
  if (InSafeZone(_state).inside)
  {
    AddSaturating(_state.Data().commander.legalStatus, LEGAL_STATUS_PER_BOMB);
  }
  // Every active ship with a blip explodes, its cargo emptied first: objectSlotCount less 3 slots from the first ship, a count
  // of 0 running 65,536 times.
  SlotIndex slot = SlotIndex::FirstShip;
  const auto slots = static_cast<std::uint8_t>(_state.Data().flight.objectSlotCount - FIRST_SHIP_SLOT);
  for (std::uint32_t count = LoopCount(slots); count != 0; --count)
  {
    SpaceObject& ship = _state.Slot(slot);
    if ((ship.type & SLOT_ACTIVE) != 0 && (ship.flags & FLAG_BLIP_DRAWN) != 0)
    {
      ship.cargo = 0;
      ExplodeObject(_state, ship);
    }
    slot = SlotAfter(slot);
  }
}

void SpawnPlayerWreckage(GameState& _state)
{
  const Vector drift = ComputeDeathDebrisVector(_state);
  _state.Data().combat.wreckDriftX = static_cast<std::uint16_t>(drift.x);
  _state.Data().combat.wreckDriftY = static_cast<std::uint16_t>(drift.y);
  _state.Data().combat.wreckDriftZ = static_cast<std::uint16_t>(drift.z);
  _state.Data().playerShip.playerVelocityX = 0;
  _state.Data().playerShip.playerVelocityY = 0;
  _state.Data().playerShip.playerVelocityZ = 0;
  _state.Data().playerShip.playerSpeed = WRECK_SPEED;
  _state.Data().playerShip.velocityDirty = 1;
  _state.Data().playerShip.viewLocked = 1;
  // Six splinters.
  for (std::uint16_t splinters = WRECK_SPLINTERS; splinters != 0; --splinters)
  {
    SpaceObject& splinter = _state.Slot(FindDebrisSlot(_state));
    ClearObjectSlot(splinter);
    splinter.aiState = 0;
    const std::uint16_t random = NextRandom(_state);
    splinter.SpinRoll() = Low(random);
    splinter.SpinPitch() = High(random);
    // Each velocity byte from its own bits of the random word: x from the low byte, y from the high, z from the low byte shifted
    // right twice.
    splinter.velocityX = Low(WreckScatter(Low(random), SPLINTER_SCATTER_MASK, SPLINTER_SCATTER_CENTER, _state.Data().combat.wreckDriftX));
    splinter.velocityY = Low(WreckScatter(High(random), SPLINTER_SCATTER_MASK, SPLINTER_SCATTER_CENTER, _state.Data().combat.wreckDriftY));
    splinter.velocityZ = Low(WreckScatter(static_cast<std::uint8_t>(Low(random) >> 2), SPLINTER_SCATTER_MASK, SPLINTER_SCATTER_CENTER,
                                          _state.Data().combat.wreckDriftZ));
    splinter.behaviorClass = DEBRIS_CLASS;
    splinter.lifetime = WRECK_LIFETIME;
    splinter.type = SPLINTER_ACTIVE;
    (void)UpdateDebrisAi(splinter);
  }
  if (_state.Data().commander.cargoHold.cargoUsedTonnes == 0)
  {
    return;
  }
  // The cargo's barrel: four drifts ahead, drifting at a quarter of the drift and a little, turned to face along it.
  const SlotSearch free = FindFreeShipSlot(_state);
  SpaceObject& barrel = _state.Slot(free.found ? free.slot : ReclaimShipSlot(_state).slot);
  ClearObjectSlot(barrel);
  constexpr std::array<std::uint16_t CombatState::*, 3> WRECK_DRIFT = {&CombatState::wreckDriftX, &CombatState::wreckDriftY,
                                                                       &CombatState::wreckDriftZ};
  for (std::size_t axis = 0; axis < WRECK_DRIFT.size(); ++axis)
  {
    const std::uint16_t axisDrift = _state.Data().combat.*WRECK_DRIFT[axis];
    const std::uint16_t velocity = Sar(WreckScatter(Low(NextRandom(_state)), BARREL_SCATTER_MASK, BARREL_SCATTER_CENTER, axisDrift), 2);
    barrel.*VELOCITY[axis] = Low(velocity);
    SetCoordinate(barrel, axis, static_cast<std::uint16_t>(axisDrift << 2));
  }
  InitCargoBarrel(barrel);
  const Angles heading = ConvertVectorToAngles(_state, GetObjectPosition(barrel));
  barrel.pitch = heading.first;
  barrel.yaw = heading.second;
  // The same random -31..32, sign-extended to 24 bits, added to x and to y, the carry into the high byte.
  const std::uint16_t random = NextRandom(_state);
  const auto offset = static_cast<std::int16_t>((random & BARREL_OFFSET_MASK) - BARREL_OFFSET_CENTER);
  AddToCoordinate(barrel, 0, offset);
  AddToCoordinate(barrel, 1, offset);
}

std::optional<std::uint8_t> KillPlayer(GameState& _state)
{
  if (_state.Data().playerShip.escapePodFrames != 0)
  {
    return std::nullopt;
  }
  _state.Data().playerShip.playerDead = 1;
  return StartPlayerDeathSound(_state);
}

void InitMissile(SpaceObject& _slot)
{
  InitObjectFromTemplate(_slot, MISSILE_TEMPLATE, 0);
  _slot.behaviorClass = MISSILE_CLASS;
}

std::optional<DashboardPixel> RemoveAllMissiles(GameState& _state)
{
  // objectSlotCount slots from the sun, a count of 0 running 65,536 times.
  std::optional<DashboardPixel> lastErased;
  SlotIndex slot = SlotIndex::Sun;
  for (std::uint32_t count = LoopCount(_state.Data().flight.objectSlotCount); count != 0; --count)
  {
    SpaceObject& object = _state.Slot(slot);
    if ((object.type & SpaceObject::ACTIVE) != 0 && TypeOf(object) == TYPE_MISSILE)
    {
      if (const std::optional<DashboardPixel> erased = RemoveObject(object))
      {
        lastErased = erased;
      }
    }
    slot = SlotAfter(slot);
  }
  return lastErased;
}

void LaunchPlayerMissile(GameState& _state)
{
  // A free slot, or one ReclaimShipSlot takes.
  const SlotSearch free = FindFreeShipSlot(_state);
  SpaceObject& missile = _state.Slot(free.found ? free.slot : ReclaimShipSlot(_state).slot);
  missile = {};
  InitMissile(missile);
  // 100 along the player's nose: (0, 100, 0) turned by the player's angles, negated.
  (void)SetSinCos(_state, 0, Negate(_state.Data().playerShip.playerPitchAngle));
  (void)SetSinCos(_state, 1, Negate(_state.Data().playerShip.playerYawAngle));
  (void)SetSinCos(_state, 2, Negate(_state.Data().playerShip.playerRollAngle));
  const Vector position = RotateRollYawPitch(_state, Vector{0, static_cast<std::int16_t>(MISSILE_LAUNCH_DISTANCE), 0});
  SetCoordinate(missile, 0, static_cast<std::uint16_t>(position.x));
  SetCoordinate(missile, 1, static_cast<std::uint16_t>(position.y));
  SetCoordinate(missile, 2, static_cast<std::uint16_t>(position.z));
  // Locked on missileTarget, and aimed at where it is.
  const SlotIndex target = _state.Data().flight.missileTarget;
  missile.target = target;
  const Angles heading = ConvertVectorToAngles(_state, GetObjectPosition(_state.Slot(target)));
  missile.pitch = heading.first;
  missile.yaw = heading.second;
  (void)ComputeVelocity(_state, missile);
  (void)MoveObject(missile);
  (void)MoveObject(missile);
}

std::optional<SlotIndex> LaunchShipFromObject(GameState& _state, const SpaceObject& _launcher, std::uint8_t _kind)
{
  const SlotSearch free = FindFreeShipSlot(_state);
  if (!free.found)
  {
    return std::nullopt;
  }
  // Any other _kind launches nothing. The original also takes 15h, an escape pod, and 5, a Krait, which
  // no caller passes: those branches were deleted with InitEscapePod and InitKraitHunter (D22).
  if (_kind != MISSILE_LAUNCH && _kind != THARGON_LAUNCH)
  {
    return std::nullopt;
  }
  // A copy of the launcher in the free slot, made what _kind names, and moved clear of it.
  SpaceObject& ship = _state.Slot(free.slot);
  CopyObject(_launcher, ship);
  if (_kind == MISSILE_LAUNCH)
  {
    InitMissile(ship);
    (void)MoveObject(ship);
    (void)MoveObject(ship);
    (void)MoveObject(ship);
    ship.target = SlotIndex::Player;
  }
  else
  {
    InitThargon(ship);
    (void)ComputeVelocity(_state, ship);
    (void)MoveObject(ship);
    (void)MoveObject(ship);
    ship.owner = _state.IndexOf(_launcher);
  }
  return free.slot;
}

void UpdateMissileAi(GameState& _state, SpaceObject& _missile)
{
  _state.Data().playerShip.incomingMissileAlert = 0;
  _missile.roll = Offset(_missile.roll, MISSILE_SPIN);
  const SlotIndex target = _missile.target;
  Vector toTarget{};
  if (target == SlotIndex::Player)
  {
    // At the player.
    toTarget = GetVectorToPlayer(_missile);
    _state.Data().playerShip.incomingMissileAlert = 1;
  }
  else
  {
    const SpaceObject& aimed = _state.Slot(target);
    if ((aimed.type & SLOT_ACTIVE) == 0)
    {
      // It explodes: its target is gone.
      ExplodeObject(_state, _missile);
      return;
    }
    const auto difference = [&aimed, &_missile](ObjectWord _axis) { return static_cast<std::int16_t>(aimed.*_axis - _missile.*_axis); };
    toTarget = Vector{difference(&SpaceObject::x), difference(&SpaceObject::y), difference(&SpaceObject::z)};
  }
  // Outside the hit box (C8h) round its target, it turns toward the target and flies on.
  if (!VectorWithinBox(toTarget, MISSILE_HIT_BOX))
  {
    (void)TurnTowardAngles(_missile, ConvertVectorToAngles(_state, toTarget));
    (void)ComputeVelocity(_state, _missile);
    (void)MoveObject(_missile);
    return;
  }
  // It explodes, and strikes its target.
  ExplodeObject(_state, _missile);
  const SlotIndex struckSlot = target;
  if (struckSlot == SlotIndex::Player)
  {
    // The player.
    TakeDamage(_state, MISSILE_DAMAGE);
    return;
  }
  SpaceObject& struck = _state.Slot(struckSlot);
  if (!IsStation(struck).station)
  {
    // A ship, credited, and exploded unless indestructible.
    (void)CreditKill(_state, struck);
    if ((struck.flags & FLAG_INDESTRUCTIBLE) != 0)
    {
      return;
    }
    ExplodeObject(_state, struck);
    return;
  }
  if (_state.Data().commander.thargoidInvasionActive != 1)
  {
    AddSaturating(_state.Data().commander.legalStatus, MISSILE_AT_STATION_CRIME);
    return;
  }
  // An invaded station loses energy to each missile, the difference written, and goes when it runs out.
  const std::uint8_t energy = struck.energy;
  struck.energy = static_cast<std::uint8_t>(energy - INVADED_STATION_HIT);
  if (energy >= INVADED_STATION_HIT)
  {
    return;
  }
  ExplodeObject(_state, struck);
  _state.Data().commander.invadedStationDestroyed = 1;
  _state.Data().commander.thargoidInvasionActive = 0;
}

ThargoidTest Routine8C51(const SpaceObject& _slot)
{
  const std::uint8_t type = TypeOf(_slot);
  return ThargoidTest{type == TYPE_THARGON || type == TYPE_THARGOID, type == TYPE_THARGOID};
}

void UseMaskingDevice(GameState& _state)
{
  // The difference written, then 0 over it on a borrow: two writes.
  const std::uint16_t energy = _state.Data().playerShip.playerEnergy;
  _state.Data().playerShip.playerEnergy = static_cast<std::uint16_t>(energy - MASKING_ENERGY);
  if (energy < MASKING_ENERGY)
  {
    _state.Data().playerShip.playerEnergy = 0;
  }
  _state.Data().drawing.maskingBackgroundColor = MASKING_COLOR;
  // Every ship forgets its state and that the player hit it, and calms a little: objectSlotCount slots from the sun.
  SlotIndex slot = SlotIndex::Sun;
  for (std::uint32_t count = LoopCount(_state.Data().flight.objectSlotCount); count != 0; --count)
  {
    SpaceObject& ship = _state.Slot(slot);
    ship.aiState = 0;
    ship.flags = static_cast<std::uint8_t>(ship.flags & ~FLAG_HOSTILE);
    const std::uint8_t aggression = ship.aggression;
    ship.aggression = static_cast<std::uint8_t>(aggression - MASKING_CALMING);
    if (aggression < MASKING_CALMING)
    {
      ship.aggression = 0;
    }
    slot = SlotAfter(slot);
  }
}

} // namespace Elite
