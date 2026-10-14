// GameLogic/Equipment.h
#pragma once

#include "Docked.h"
#include "Flight.h"
#include "GameState.h"
#include "Hardware.h"
#include "Input.h"
#include "Market.h"
#include "Text.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Elite
{

// The reference's equipment routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): buying and fitting equipment. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

/// Whether _key ends a menu screen: Esc, or F1 to F10.
[[nodiscard]] bool IsScreenKey(std::uint8_t _key) noexcept;

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// TryScoopObject (CS:4401): freeCargoTonnes, and the object in _slot scooped into the hold if its view position _view is in
/// the scoop's box: a barrel's random product or its masking device, a splinter's alloys or precious metals, an escape pod's
/// slaves, a Thargon's alien items, each removed (RemoveObject) with its message, or the message that the hold is full or the
/// object cannot be scooped.
void TryScoopObject(GameState& _state, SpaceObject& _slot, const Vector& _view);

/// PayForEquipmentItem (CS:65A3): menuSelectedRow's price off creditsTenths (SubtractCredits): fuel's by the tank's emptiness.
/// Returns whether the credits were enough, and are now less the price.
[[nodiscard]] bool PayForEquipmentItem(GameState& _state);

/// LaunchEscapePod (CS:2F0F): an abandoned Cobra in a free ship slot, or one ReclaimShipSlot makes, the player turned away
/// at speed 20 and moved 12 frames, the escape pod and the cargo hold gone.
void LaunchEscapePod(GameState& _state);

/// SelectLaserType (CS:633B): selectedLaserType 0-3 for the laser at menuSelectedRow (LaserTypeOfRow), or 3, where the
/// original's count up a type at a time stops, for a row that is no laser's. Returns whether the row is a laser's.
[[nodiscard]] bool SelectLaserType(GameState& _state);

/// ShowEquipShipScreen (CS:5BF2): the F4 screen, each item up to the system's tech level with its price into screenPrices and
/// its resale price, then RunEquipShipMenu from the first item's row, which returns the key that closes it. _backward is the
/// direction flag, which DrawDockedFrame goes by. Waits for keys as a rule (ADR-015).
ScreenKey ShowEquipShipScreen(GameState& _state, Hardware& _hardware, bool _backward);

/// RunEquipShipMenu (CS:6111), with OpenEquipMessage, ShowEquipMessage, ReportNotEnoughCredits and the buying and selling it
/// runs into as one unit (ADR-012 item 14): the equipment menu from row _firstRow of the text page (menuFirstRowAttr), the
/// cursor moved by the steering and the arrow keys, and B and S to buy and sell, each with its message. Returns Esc or the
/// F-key, other than F4, that ends it. Waits for keys as a rule (ADR-015).
ScreenKey RunEquipShipMenu(GameState& _state, Hardware& _hardware, std::uint16_t _firstRow);

/// ChooseMountToFitLaser (CS:63B2): the mount menu, and the mount a bought laser goes on, chosen with the steering or the
/// arrow keys and Space. Waits for keys as a rule (ADR-015).
void ChooseMountToFitLaser(GameState& _state, Hardware& _hardware);

/// ChooseMountToRemoveLaser (CS:646F): the mount menu, and the mount a sold laser comes off, as ChooseMountToFitLaser.
void ChooseMountToRemoveLaser(GameState& _state, Hardware& _hardware);

/// DrawLaserMountMenu (CS:6367): the mount header, each of the four mounts' laser or "Free" on the line after it, and
/// FORE's box highlighted; selectedLaserMount = 0.
void DrawLaserMountMenu(GameState& _state);

/// RedrawEquipHelpText (CS:653F): the equipment screen's help lines again, over the mount menu, and textAttribute put
/// back to the menu's.
void RedrawEquipHelpText(GameState& _state);

/// PaintLaserMountBox (CS:6564): textAttribute over the 8x3 cells of the mount box whose first attribute byte is at
/// B800:_box.
void PaintLaserMountBox(GameState& _state, std::uint16_t _box);

/// ClearEquipmentSellPrice (CS:6946): "-" in menuSelectedRow's resale column, once the last of an item is sold.
void ClearEquipmentSellPrice(GameState& _state);

/// ShowEquipmentSellPrice (CS:6972): menuSelectedRow's resale price computed by ComputeResalePrice, stored after its
/// price in screenPrices, and printed in its row's resale column.
void ShowEquipmentSellPrice(GameState& _state);

// What the menus that wait share, at each one's own addresses: no hook, and no entry.

/// Where the menu's cursor is, after StartMenu or a move.
struct MenuCursor
{
  std::uint16_t row; ///< the selected row's first attribute byte on the text page
};

/// The offset on the text page of the menu's first row's highlighted attribute, from the row menuFirstRowAttr holds, where
/// the original holds that offset.
[[nodiscard]] std::uint16_t MenuFirstRowCell(const GameState& _state) noexcept;

/// The menus' start: the credits on the message line, then row 1 selected and highlighted from menuFirstRowAttr.
MenuCursor StartMenu(GameState& _state);

/// The cursor up a row from the row at _row, from the first round to the last.
MenuCursor MoveMenuCursorUp(GameState& _state, std::uint16_t _row);

/// The cursor down a row from the row at _row, from the last round to the first.
MenuCursor MoveMenuCursorDown(GameState& _state, std::uint16_t _row);

/// What a menu that waits holds in registers from one turn of its loop to the next: where its cursor is, in SI, and AL, which
/// ReadSteering fires the stick with and GetKey shifts each key's Shift into (AlAfterKey).
struct MenuLoop
{
  std::uint16_t row; ///< the selected row's first attribute byte on the text page
  std::uint8_t al;
};

/// ReadSteering, fired with _loop's AL, and the cursor moved up or down a row if its pitch says so. Returns the cursor, and AL as
/// the original leaves it: the roll, or once the cursor moved, the attribute ToggleMenuRowHighlight left.
MenuLoop SteerMenuCursor(GameState& _state, Hardware& _hardware, MenuLoop _loop);

/// 100 timer ticks, then GetKey. Returns the key.
KeyPress PollMenuKey(GameState& _state, Hardware& _hardware);

// ── The shipyard's rules (D53, D74): values in, values out, no state and no screen ──
//
// What the routines above decide, apart from what they draw and where the game keeps it (ADR-043), as the market's rules are
// (Market.h). The equipment list's rows count from 1, fuel's, in the list's order, which is by tech level.

/// The items of the equipment list: fuel, missiles, the cargo bay extension, ECM, the pulse and beam lasers, fuel scoops, the
/// escape pod, the energy bomb, the energy unit, the docking computer, the galactic hyperdrive, and the mining and military
/// lasers.
inline constexpr std::uint8_t EQUIPMENT_LIST_ROWS = 14;

/// The equipment list's rows the rules name.
inline constexpr std::uint8_t FUEL_ROW = 1;
inline constexpr std::uint8_t MISSILE_ROW = 2;
inline constexpr std::uint8_t CARGO_BAY_ROW = 3;
inline constexpr std::uint8_t FUEL_SCOOPS_ROW = 7;
inline constexpr std::uint8_t MINING_LASER_ROW = 13;

/// A full tank, which is seven light years.
inline constexpr std::uint8_t FUEL_FULL = 0xFF;

/// An item of the equipment list, as the list holds it.
struct EquipmentItem
{
  std::uint8_t techLevel;       ///< on sale where the system's tech level is at least one less
  std::string_view name;        ///< padded with spaces to the price column
  std::int8_t governmentFactor; ///< tenths of a credit per step of the government
  std::int8_t economyFactor;    ///< tenths of a credit per step of the economy
  std::uint16_t basePriceTenths;
};

/// What the shipyard's rules read of the ship and the commander.
struct Fitting
{
  std::array<std::uint8_t, EQUIPMENT_LIST_ROWS> counts; ///< each row's count fitted, from fuel's: the commander's bytes from fuel on
  std::uint8_t laserMountsFitted;                       ///< a bit for each mount, fore's the lowest
  std::uint8_t cargoUsedTonnes;
  std::uint8_t missionNumber;
};

/// Why the shipyard will not fit or take back an item. NotEnoughCredits is the price's, found once the rest allow it.
enum class EquipmentRefusal : std::uint8_t
{
  None,
  NoFuelAvailable,  ///< in the first mission, the supernova's
  FuelFull,         ///< the tank holds FDh or more
  FourMissilesOnly, ///< four already
  AlreadyFitted,    ///< one of a kind
  FourLasers,       ///< every mount has one
  ScoopNeeded,      ///< a mining laser without fuel scoops
  NotEnoughCredits, ///< more than the commander can pay for
  FuelSaleIllegal,  ///< fuel is never sold back
  NoMissileToSell,  ///< none fitted
  TooMuchCargo,     ///< the extension, with 21 tonnes or more aboard
  NoItemToSell,     ///< none fitted
  SellMiningLaser,  ///< fuel scoops, with a mining laser fitted
};

/// The laser mounts, as the commander's two bytes hold them.
struct LaserMounts
{
  std::uint8_t fitted; ///< a bit for each mount, fore's the lowest
  std::uint8_t types;  ///< two bits for each mount, its laser's type, fore's the lowest
};

/// Why a laser cannot be fitted to a mount, or taken from it.
enum class MountRefusal : std::uint8_t
{
  None,
  Occupied,  ///< to fit: a laser is on it
  NoLaser,   ///< to take: none is on it
  WrongType, ///< to take: a laser of another type is on it
};

/// Row _row of the equipment list, from 1 to EQUIPMENT_LIST_ROWS.
[[nodiscard]] EquipmentItem EquipmentItemAt(std::uint8_t _row) noexcept;

/// How many of the list's rows are on sale at tech level _techLevel: every item up to the first whose tech level is not below
/// _techLevel + 2, in 8 bits.
[[nodiscard]] std::uint8_t EquipmentRowsOnSale(std::uint8_t _techLevel) noexcept;

/// ShowEquipShipScreen's rule: _item's price in the shipyard of _place, its base price and each factor times its step, the
/// factors signed and every sum in 16 bits.
[[nodiscard]] std::uint16_t EquipmentPriceTenths(const EquipmentItem& _item, const MarketPlace& _place) noexcept;

/// PayForEquipmentItem's rule for fuel: what filling a tank holding _fuel costs at _fuelPriceTenths a light year, by the low
/// byte of the price, at least a tenth.
[[nodiscard]] std::uint16_t FuelPriceTenths(std::uint8_t _fuel, std::uint16_t _fuelPriceTenths);

/// SelectLaserType's rule: the laser type row _row sells, 0 to 3, pulse, beam, mining and military, or nothing.
[[nodiscard]] std::optional<std::uint8_t> LaserTypeOfRow(std::uint8_t _row) noexcept;

/// Why row _row cannot be fitted to _fitting, before it is paid for. EquipmentRefusal::None when it can.
[[nodiscard]] EquipmentRefusal RefuseToFit(std::uint8_t _row, const Fitting& _fitting) noexcept;

/// Why row _row cannot be sold back from _fitting. EquipmentRefusal::None when it can.
[[nodiscard]] EquipmentRefusal RefuseToRemove(std::uint8_t _row, const Fitting& _fitting) noexcept;

/// Row _row's count fitted, _count, once it is bought: a full tank for fuel, one more of anything else, in 8 bits.
[[nodiscard]] std::uint8_t CountAfterFitting(std::uint8_t _row, std::uint8_t _count) noexcept;

/// Whether a laser is on mount _mount, 0 to 3, fore, aft, left and right.
[[nodiscard]] bool MountHoldsLaser(const LaserMounts& _mounts, std::uint8_t _mount) noexcept;

/// The type of the laser on mount _mount, by its two bits.
[[nodiscard]] std::uint8_t LaserTypeOnMount(const LaserMounts& _mounts, std::uint8_t _mount) noexcept;

/// Why a laser cannot be fitted to mount _mount. MountRefusal::None when it can.
[[nodiscard]] MountRefusal RefuseToFitMount(const LaserMounts& _mounts, std::uint8_t _mount) noexcept;

/// Why a laser of type _type cannot be taken from mount _mount. MountRefusal::None when it can.
[[nodiscard]] MountRefusal RefuseToClearMount(const LaserMounts& _mounts, std::uint8_t _mount, std::uint8_t _type) noexcept;

/// _mounts with a laser of type _type on mount _mount: its bit set, and its two bits cleared and then set to the type.
[[nodiscard]] LaserMounts WithLaserFitted(const LaserMounts& _mounts, std::uint8_t _mount, std::uint8_t _type) noexcept;

/// _mounts with mount _mount's bit cleared; its two type bits are left as they were.
[[nodiscard]] LaserMounts WithLaserCleared(const LaserMounts& _mounts, std::uint8_t _mount) noexcept;

} // namespace Elite
