// GameLogic/EquipView.h
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Elite
{

/// The rows the equipment list has at most: fuel, missiles, the cargo bay extension, ECM, the pulse and beam lasers, fuel scoops,
/// the escape pod, the energy bomb, the energy unit, the docking computer, the galactic hyperdrive, and the mining and military
/// lasers.
inline constexpr std::size_t EQUIP_ROWS_MOST = 14;

/// The laser mounts, as the game numbers them: the original's fore, aft, right and left, 0 to 3.
inline constexpr std::size_t LASER_MOUNT_COUNT = 4;

/// The mount picker's keys 1 to 4 (D104), and the order D03 draws and names the mounts in: front, left, right and rear, each the
/// original's mount of that name.
inline constexpr std::array<std::uint8_t, LASER_MOUNT_COUNT> MOUNT_OF_PICKER_KEY = {0, 3, 2, 1};

/// What the message strip of D02 says after a trade or a refused one (D11's Equipment column): the kind, and the figures its words
/// take. The shell words it.
enum class EquipNotice : std::uint8_t
{
  None,
  Bought,            ///< row bought for tenths; fuel's fills the tank to fuelTenths
  Sold,              ///< row sold for tenths
  LaserFitted,       ///< row's laser fitted to mount for tenths, net of the laser it traded in when tradedIn
  LaserSold,         ///< row's laser taken off mount and sold for tenths
  FuelFull,          ///< the tanks hold fuelTenths
  NoFuel,            ///< none on sale during the first mission
  FourMissiles,      ///< all four pylons loaded
  AlreadyFitted,     ///< row is fitted, and one is all a ship takes
  FourLasers,        ///< every mount carries row's laser already
  ScoopsNeeded,      ///< a mining laser needs fuel scoops
  TooMuchCargo,      ///< the extension sells only once tonnes are unloaded
  MiningLaserFitted, ///< the fuel scoops cannot be sold while a mining laser is fitted
  NotEnoughCredits,  ///< tenths needed, creditsTenths available
  FuelNotBought,     ///< fuel is never bought back
  NoneFitted,        ///< none of row to sell
  MountTaken,        ///< mount carries row's laser already (D107)
  MountWithout,      ///< mount carries no laser of row's type to sell
};

/// A message strip and its figures.
struct EquipMessage
{
  EquipNotice notice = EquipNotice::None;
  std::uint8_t row = 0;            ///< the list's row, from 1, fuel's
  std::uint8_t mount = 0;          ///< a laser's mount, 0 to 3
  bool tradedIn = false;           ///< LaserFitted: a laser came off the mount
  std::uint8_t tradedRow = 0;      ///< and its row
  std::uint32_t tenths = 0;        ///< what a trade cost or earned, or what it needed
  std::uint32_t creditsTenths = 0; ///< the credits there were
  std::uint8_t fuelTenths = 0;     ///< light years, in tenths
  std::uint8_t tonnes = 0;         ///< TooMuchCargo: the tonnes to unload
};

/// A control of D02 or D03 a click lands on (D83). The shell hit-tests in design units and tells the game which control, and for a
/// row or a mount which one; the game acts on it as on the key that does the same.
enum class EquipControl : std::uint8_t
{
  Row,    ///< an item's row: selects it
  Buy,    ///< [B] BUY
  Sell,   ///< [S] SELL
  Mount,  ///< a mount's box in the picker
  Cancel, ///< the picker's ESC CANCEL
};

/// A click on a control: for Row the row, from 0, and for Mount the mount, 0 to 3.
struct EquipClick
{
  EquipControl control = EquipControl::Row;
  std::uint8_t index = 0;
};

/// A row of the list as D02 shows it.
struct EquipRowView
{
  std::uint16_t buyTenths = 0;  ///< the price; fuel's a light year's
  std::uint16_t sellTenths = 0; ///< what the shipyard pays for one; 0 for fuel, which it never buys
  std::uint8_t count = 0;       ///< fitted: the fuel's units, the missiles, a laser's count, or another item's member
  std::uint8_t mounts = 0;      ///< a laser's: the mounts that carry it, a bit each (MOUNT_FORE...)
};

/// A mount as the picker shows it.
struct MountView
{
  bool laser = false;    ///< a laser is on it
  std::uint8_t type = 0; ///< and its type: 0 pulse, 1 beam, 2 mining, 3 military
  bool allowed = false;  ///< picking it would happen
};

/// The mount picker of D03, over D02, while it is open.
struct PickerView
{
  bool open = false;
  bool selling = false; ///< SELL … — CHOOSE MOUNT (D103), otherwise BUY
  std::uint8_t row = 0; ///< the laser's row, from 1
  std::array<MountView, LASER_MOUNT_COUNT> mounts{};
};

/// What D02 and D03 show, for the shell (R9): plain values, nothing of the engine's and nothing of the game's state. The game
/// publishes one each time the screen changes (Host::ShowEquip).
struct EquipView
{
  std::uint64_t sequence = 0; ///< from 1, one more for each view published
  std::string systemName;     ///< the current system's name, as the game stores it
  bool dodo = false;          ///< the station is a Dodo, not a Coriolis
  std::uint32_t creditsTenths = 0;
  std::uint8_t techLevel = 0; ///< as the screen shows it, one more than the system data's
  std::uint8_t rows = 0;      ///< the items on sale, the list's first rows
  std::array<EquipRowView, EQUIP_ROWS_MOST> items{};
  std::uint8_t fuelTenths = 0; ///< the fuel, in tenths of a light year
  std::uint8_t selected = 0;   ///< the selected row, from 0
  bool buyAllowed = false;     ///< buying the selected item would happen, on some mount for a laser
  bool sellAllowed = false;    ///< selling it would
  PickerView picker;
  EquipMessage message; ///< the strip, or EquipNotice::None for none
};

} // namespace Elite
