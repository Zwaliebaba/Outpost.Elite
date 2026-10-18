#include "pch.h"

#include "Equipment.h"

#include "Arithmetic.h"
#include "Docked.h"
#include "EquipmentTables.h"
#include "Flight.h"
#include "Market.h"
#include "Maths.h"
#include "Ships.h"
#include "Text.h"
#include "Timer.h"

#include <span>
#include <string_view>

namespace Elite
{

namespace
{

// The equipment list's rows, from 1 (EQUIPMENT_LIST_ROWS, Equipment.h). Row r's fitted count is the commander's equipment item
// r - 1 (EQUIPMENT_ITEMS): fuel's is row 1.
constexpr std::uint8_t FUEL_UNITS_PER_TENTH = 0x24;
constexpr std::array<std::uint8_t, 4> LASER_ROWS = {0x05, 0x06, 0x0D, 0x0E}; // pulse, beam, mining, military

constexpr std::uint8_t MISSILE_LIMIT = 4;
constexpr std::uint8_t FUEL_NEARLY_FULL = 0xFD;
constexpr std::uint8_t NO_FUEL_MISSION = 1;
constexpr std::uint8_t CARGO_BAY_SALE_TONNES = 0x15; // the extension sells only with less than this aboard
constexpr std::uint8_t ALL_MOUNTS = 0x0F;
constexpr std::uint8_t MOUNT_TYPE_BITS = 3; // a mount's two bits of laserMountTypes

// LaunchEscapePod.
constexpr std::uint8_t ESCAPE_POD_FRAMES = 0x50;
constexpr std::uint16_t HALF_TURN = 0x400;
constexpr std::uint16_t ESCAPE_POD_SPEED = 0x14;
constexpr std::uint16_t ESCAPE_POD_MOVES = 12;
constexpr std::uint16_t CARGO_KINDS = 0x11;

// TryScoopObject: the scoop's box in view coordinates, and what is scooped.
constexpr std::uint16_t SCOOP_LOWEST = 0x1E;
constexpr std::uint16_t SCOOP_HEIGHT = 0xC8;
constexpr std::uint16_t SCOOP_HALF_WIDTH = 0x96;
constexpr std::uint8_t TYPE_CARGO_BARREL = 0x11;
constexpr std::uint8_t TYPE_ESCAPE_POD = 0x15;
constexpr std::uint8_t FLAG_MASKING_DEVICE = 0x40;
constexpr std::uint8_t FLAG_PRECIOUS = 0x10;
constexpr std::uint16_t SCOOP_MESSAGE_FRAMES = 0x14;
constexpr std::uint8_t RANDOM_PRODUCT_DIVISOR = 0x18; // 0-255 into products 0-10
constexpr std::uint8_t PRODUCT_SLAVES = 3;
constexpr std::uint8_t PRODUCT_FURS = 0x0B;
constexpr std::uint16_t PRODUCT_NAME_BYTES = 0x11;
constexpr std::uint16_t SCOOPED_NAME_BYTES = 0x0D;
constexpr std::uint8_t MINERALS_FROM = 0x28;

// The text in _table, a table of the reference's bytes, from its byte _at up to its NUL.
[[nodiscard]] std::string_view TextInTable(std::span<const std::uint8_t> _table, std::size_t _at) noexcept
{
  const std::string_view table(reinterpret_cast<const char*>(_table.data()), _table.size());
  return table.substr(_at, table.find('\0', _at) - _at);
}

// IMUL r/m8: _al times _factor, both signed, into a word.
[[nodiscard]] std::uint16_t SignedProduct(std::uint8_t _al, std::uint8_t _factor) noexcept
{
  return static_cast<std::uint16_t>(static_cast<std::int8_t>(_al) * static_cast<std::int8_t>(_factor));
}

// The word negated when it is negative.
[[nodiscard]] std::uint16_t Magnitude(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0 ? Negate(_value) : _value;
}

// One more, in 8 bits: FFh wraps to 0.
void IncrementByte(std::uint8_t& _byte)
{
  _byte = static_cast<std::uint8_t>(_byte + 1);
}

// _amount added to _field in 8 bits, then held at 250 (FAh) from 251 up: gems, gold and platinum.
void AddPrecious(std::uint8_t& _field, std::uint8_t _amount)
{
  _field = static_cast<std::uint8_t>(_field + _amount);
  if (_field > PRECIOUS_HELD_MOST)
  {
    _field = PRECIOUS_HELD_MOST;
  }
}

// TryScoopObject's box test: whether _view is in the scoop's box: y from 30 to 229, then x and z within 149 either
// side.
[[nodiscard]] bool TestScoopBox(const Vector& _view) noexcept
{
  const auto y = static_cast<std::uint16_t>(_view.y);
  if ((y & 0x8000) != 0 || y < SCOOP_LOWEST || static_cast<std::uint16_t>(y - SCOOP_LOWEST) >= SCOOP_HEIGHT)
  {
    return false;
  }
  return Magnitude(static_cast<std::uint16_t>(_view.x)) < SCOOP_HALF_WIDTH &&
         Magnitude(static_cast<std::uint16_t>(_view.z)) < SCOOP_HALF_WIDTH;
}

} // namespace

void LaunchEscapePod(GameState& _state)
{
  _state.Data().jump.hyperspaceCountdown = 0;
  _state.Data().playerShip.escapePodFrames = ESCAPE_POD_FRAMES;
  const SlotSearch free = FindFreeShipSlot(_state);
  SpaceObject& pod = _state.Slot(free.found ? free.slot : ReclaimShipSlot(_state).slot);
  ClearObjectSlot(pod);
  InitAbandonedCobra(pod);
  _state.Data().playerShip.playerPitchAngle = static_cast<std::uint16_t>(_state.Data().playerShip.playerPitchAngle + HALF_TURN);
  _state.Data().playerShip.viewAngle = HALF_TURN;
  _state.Data().playerShip.viewLocked = 1;
  _state.Data().playerShip.playerSpeed = ESCAPE_POD_SPEED;
  _state.Data().playerShip.velocityDirty = 1;
  _state.Data().commander.escapePodFitted = 0;
  UpdatePlayerVelocity(_state);
  for (std::uint16_t move = 0; move < ESCAPE_POD_MOVES; ++move)
  {
    MoveObjectsByVelocity(_state);
  }
  // The 17 amounts held emptied.
  for (CommanderState::CargoHold::Lot& lot : _state.Data().commander.cargoHold.lots)
  {
    lot.held = 0;
  }
}

void TryScoopObject(GameState& _state, SpaceObject& _slot, const Vector& _view)
{
  std::uint8_t space = CargoBayTonnes(_state.Data().commander.largeCargoBayFitted == 1);
  space = static_cast<std::uint8_t>(space - _state.Data().commander.cargoHold.cargoUsedTonnes);
  _state.Data().equipment.freeCargoTonnes = space;
  if (!TestScoopBox(_view))
  {
    return;
  }

  const auto type = static_cast<std::uint8_t>((_slot.type >> 1) & TYPE_MASK);
  const std::uint8_t flags = _slot.flags;
  const bool full = _state.Data().equipment.freeCargoTonnes == 0;
  Message message = Message::ScoopRetrievalInactive;
  if (type == TYPE_CARGO_BARREL)
  {
    if ((flags & FLAG_MASKING_DEVICE) != 0)
    {
      (void)RemoveObject(_slot);
      _state.Data().commander.maskingDeviceRecovered = 1;
      message = Message::MaskingDevice;
    }
    else if (full)
    {
      message = Message::CargoBayFull;
    }
    else
    {
      // A random product 0-10, furs for slaves, into the hold, and its name for the message. The divide cannot overflow.
      (void)RemoveObject(_slot);
      const std::uint8_t random = Low(NextRandom(_state));
      std::uint8_t product = DivideByte(random, RANDOM_PRODUCT_DIVISOR).quotient;
      if (product == PRODUCT_SLAVES)
      {
        product = PRODUCT_FURS;
      }
      IncrementByte(_state.Data().commander.cargoHold.lots[product].held);
      _state.Data().commander.cargoHold.cargoUsedTonnes = static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + 1);
      // The product's name, its first 13 bytes, over the message's.
      const std::span<const std::uint8_t> name = std::span<const std::uint8_t>(_state.Data().docked.productNames)
                                                   .subspan(std::size_t{product} * PRODUCT_NAME_BYTES, SCOOPED_NAME_BYTES);
      const std::span<std::uint8_t> scooped = BytesWithin(std::span(_state.Data().equipment.scoopedCargoText), 0, SCOOPED_NAME_BYTES);
      for (std::size_t letter = 0; letter < SCOOPED_NAME_BYTES; ++letter)
      {
        scooped[letter] = std::uint8_t{name[letter]};
      }
      message = Message::ScoopedCargo;
    }
  }
  else if (type == TYPE_SPLINTER && (flags & FLAG_PRECIOUS) != 0)
  {
    // Gems, gold and platinum always; minerals or alloys too if there is room. Gold gets the gems' amount, as the original adds it.
    (void)RemoveObject(_slot);
    const std::uint8_t gems = Low(NextRandom(_state)) & 7;
    AddPrecious(_state.Data().commander.cargoHold.lots[COMMODITY_GEM_STONES].held, gems);
    AddPrecious(_state.Data().commander.cargoHold.lots[COMMODITY_GOLD].held, gems);
    AddPrecious(_state.Data().commander.cargoHold.lots[COMMODITY_PLATINUM].held,
                static_cast<std::uint8_t>((High(NextRandom(_state)) & 3) + 1));
    if (_state.Data().equipment.freeCargoTonnes != 0)
    {
      CommanderState::CargoHold& hold = _state.Data().commander.cargoHold;
      IncrementByte(Low(NextRandom(_state)) >= MINERALS_FROM ? hold.lots[COMMODITY_MINERALS].held : hold.lots[COMMODITY_ALLOYS].held);
      _state.Data().commander.cargoHold.cargoUsedTonnes = static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + 1);
    }
    message = Message::PreciousMetals;
  }
  else if (type == TYPE_SPLINTER || type == TYPE_ESCAPE_POD || type == TYPE_THARGON)
  {
    if (full)
    {
      message = Message::CargoBayFull;
    }
    else
    {
      (void)RemoveObject(_slot);
      CommanderState::CargoHold& hold = _state.Data().commander.cargoHold;
      std::uint8_t& held = type == TYPE_SPLINTER     ? hold.lots[COMMODITY_ALLOYS].held
                           : type == TYPE_ESCAPE_POD ? hold.lots[COMMODITY_SLAVES].held
                                                     : hold.lots[COMMODITY_ALIEN_ITEMS].held;
      held = static_cast<std::uint8_t>(held + 1);
      _state.Data().commander.cargoHold.cargoUsedTonnes = static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + 1);
      message = type == TYPE_SPLINTER ? Message::MetalAlloys : type == TYPE_ESCAPE_POD ? Message::EscapePodRetrieved : Message::AlienItems;
    }
  }
  _state.Data().messages.messagePointer = message;
  _state.Data().messages.messageFrames = SCOOP_MESSAGE_FRAMES;
}

// ── The shipyard's rules ──

EquipmentItem EquipmentItemAt(std::uint8_t _row) noexcept
{
  // equipmentList: a count, then for each item its tech level, its name to a NUL, its government's and economy's factors and its
  // base price, a word.
  const std::span<const std::uint8_t> list(EQUIPMENT_LIST);
  std::size_t at = 1;
  for (std::uint8_t row = 1;; ++row)
  {
    const std::string_view name = TextInTable(list, at + 1);
    const std::size_t factors = at + 1 + name.size() + 1;
    if (row == _row)
    {
      return EquipmentItem{.techLevel = list[at],
                           .name = name,
                           .governmentFactor = static_cast<std::int8_t>(list[factors]),
                           .economyFactor = static_cast<std::int8_t>(list[factors + 1]),
                           .basePriceTenths = WordIn(list, factors + 2)};
    }
    at = factors + 4;
  }
}

std::uint8_t EquipmentRowsOnSale(std::uint8_t _techLevel) noexcept
{
  // The list is sorted by tech level: the first item beyond the system's level + 1 ends it, or else the count does.
  const auto limit = static_cast<std::uint8_t>(_techLevel + 2);
  std::uint8_t rows = 0;
  while (rows < EQUIPMENT_LIST_ROWS && EquipmentItemAt(static_cast<std::uint8_t>(rows + 1)).techLevel < limit)
  {
    ++rows;
  }
  return rows;
}

std::uint16_t EquipmentPriceTenths(const EquipmentItem& _item, const MarketPlace& _place) noexcept
{
  // The government's and the economy's factors, each a signed byte product, and the base price.
  const std::uint16_t government = SignedProduct(static_cast<std::uint8_t>(_item.governmentFactor), _place.government);
  const std::uint16_t economy = SignedProduct(static_cast<std::uint8_t>(_item.economyFactor), _place.economy);
  return static_cast<std::uint16_t>(economy + government + _item.basePriceTenths);
}

std::uint16_t FuelPriceTenths(std::uint8_t _fuel, std::uint16_t _fuelPriceTenths)
{
  // What fills the tank: (255-fuel) * the price's low byte / 36, at least 1, by a DIV that can overflow into the game's trap.
  const auto missing = static_cast<std::uint8_t>(~_fuel);
  const auto product = static_cast<std::uint16_t>(missing * Low(_fuelPriceTenths));
  const std::uint8_t units = DivideByte(product, FUEL_UNITS_PER_TENTH).quotient;
  return units == 0 ? std::uint16_t{1} : std::uint16_t{units};
}

std::optional<std::uint8_t> LaserTypeOfRow(std::uint8_t _row) noexcept
{
  for (std::size_t type = 0; type < LASER_ROWS.size(); ++type)
  {
    if (_row == LASER_ROWS[type])
    {
      return static_cast<std::uint8_t>(type);
    }
  }
  return std::nullopt;
}

EquipmentRefusal RefuseToFit(std::uint8_t _row, const Fitting& _fitting) noexcept
{
  const std::uint8_t count = _fitting.counts[std::size_t{_row} - 1];
  if (_row == FUEL_ROW)
  {
    if (_fitting.missionNumber == NO_FUEL_MISSION)
    {
      return EquipmentRefusal::NoFuelAvailable;
    }
    return count >= FUEL_NEARLY_FULL ? EquipmentRefusal::FuelFull : EquipmentRefusal::None;
  }
  if (_row == MISSILE_ROW)
  {
    return count == MISSILE_LIMIT ? EquipmentRefusal::FourMissilesOnly : EquipmentRefusal::None;
  }
  if (!LaserTypeOfRow(_row))
  {
    return count != 0 ? EquipmentRefusal::AlreadyFitted : EquipmentRefusal::None;
  }
  // A laser: a free mount, and a mining laser only with fuel scoops.
  if (_fitting.laserMountsFitted == ALL_MOUNTS)
  {
    return EquipmentRefusal::FourLasers;
  }
  if (_row == MINING_LASER_ROW && _fitting.counts[FUEL_SCOOPS_ROW - 1] != 1)
  {
    return EquipmentRefusal::ScoopNeeded;
  }
  return EquipmentRefusal::None;
}

EquipmentRefusal RefuseToRemove(std::uint8_t _row, const Fitting& _fitting) noexcept
{
  const std::uint8_t count = _fitting.counts[std::size_t{_row} - 1];
  if (_row == FUEL_ROW)
  {
    return EquipmentRefusal::FuelSaleIllegal;
  }
  if (_row == MISSILE_ROW)
  {
    return count == 0 ? EquipmentRefusal::NoMissileToSell : EquipmentRefusal::None;
  }
  if (_row == CARGO_BAY_ROW && _fitting.cargoUsedTonnes >= CARGO_BAY_SALE_TONNES)
  {
    return EquipmentRefusal::TooMuchCargo;
  }
  if (count == 0)
  {
    return EquipmentRefusal::NoItemToSell;
  }
  if (_row == FUEL_SCOOPS_ROW && _fitting.counts[MINING_LASER_ROW - 1] != 0)
  {
    return EquipmentRefusal::SellMiningLaser;
  }
  return EquipmentRefusal::None;
}

std::uint8_t CountAfterFitting(std::uint8_t _row, std::uint8_t _count) noexcept
{
  return _row == FUEL_ROW ? FUEL_FULL : static_cast<std::uint8_t>(_count + 1);
}

bool MountHoldsLaser(const LaserMounts& _mounts, std::uint8_t _mount) noexcept
{
  // The mount's bit of fitted, which the original shifts out into the carry.
  return ((_mounts.fitted >> _mount) & 1) != 0;
}

std::uint8_t LaserTypeOnMount(const LaserMounts& _mounts, std::uint8_t _mount) noexcept
{
  return static_cast<std::uint8_t>((_mounts.types >> (_mount * 2u)) & MOUNT_TYPE_BITS);
}

MountRefusal RefuseToFitMount(const LaserMounts& _mounts, std::uint8_t _mount) noexcept
{
  return MountHoldsLaser(_mounts, _mount) ? MountRefusal::Occupied : MountRefusal::None;
}

MountRefusal RefuseToClearMount(const LaserMounts& _mounts, std::uint8_t _mount, std::uint8_t _type) noexcept
{
  if (!MountHoldsLaser(_mounts, _mount))
  {
    return MountRefusal::NoLaser;
  }
  return LaserTypeOnMount(_mounts, _mount) != _type ? MountRefusal::WrongType : MountRefusal::None;
}

LaserMounts WithLaserFitted(const LaserMounts& _mounts, std::uint8_t _mount, std::uint8_t _type) noexcept
{
  // 3 and the type, shifted up together as a word to the mount's two bits: its low byte the bits to clear, its high byte the type
  // to set.
  const auto bits = static_cast<std::uint16_t>(Join(_type, MOUNT_TYPE_BITS) << (_mount * 2u));
  return LaserMounts{.fitted = static_cast<std::uint8_t>(_mounts.fitted | (1u << _mount)),
                     .types = static_cast<std::uint8_t>((_mounts.types & ~Low(bits)) | High(bits))};
}

LaserMounts WithLaserCleared(const LaserMounts& _mounts, std::uint8_t _mount) noexcept
{
  return LaserMounts{.fitted = static_cast<std::uint8_t>(_mounts.fitted & ~(1u << _mount)), .types = _mounts.types};
}

} // namespace Elite
