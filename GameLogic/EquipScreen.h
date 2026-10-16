// GameLogic/EquipScreen.h
#pragma once

#include "Docked.h"
#include "EquipView.h"
#include "Equipment.h"
#include "Task.h"

#include <array>
#include <cstdint>
#include <optional>

namespace Elite
{

class GameState;
class Host;

/// The shipyard and the ship as D02 reads them: values, from the game's state (EquipGoodsOf) or from a test.
struct EquipGoods
{
  std::uint8_t rows = 0;                                        ///< the items on sale, the list's first rows
  std::array<std::uint16_t, EQUIPMENT_LIST_ROWS> priceTenths{}; ///< each row's price here, listed or not; fuel's a light year's
  Fitting fitting{};
  LaserMounts mounts{};
  std::uint32_t creditsTenths = 0;
};

/// What a trade leaves, for the game to store: the counts fitted, the mounts and the credits.
struct EquipTrade
{
  std::array<std::uint8_t, EQUIPMENT_LIST_ROWS> counts{};
  LaserMounts mounts{};
  std::uint32_t creditsTenths = 0;
};

/// The mount picker of D03 while it is open: for which laser, and whether it buys or sells.
struct MountPicker
{
  std::uint8_t row = 0; ///< the laser's row, from 1
  bool selling = false;
};

/// D02's own state, which the original screen has no counterpart for in this shape: the row, the picker, and the strip the last
/// action left.
struct EquipScreen
{
  std::uint8_t selected = 0; ///< from 0
  std::optional<MountPicker> picker;
  EquipMessage message;
};

/// What a key or a click asks D02 to do.
enum class EquipAction : std::uint8_t
{
  Up,     ///< the row above
  Down,   ///< the row below
  Select, ///< the row a click names
  Buy,    ///< the selected item, or the picker for a laser
  Sell,   ///< the same, selling
  Mount,  ///< in the picker: the mount the key or the click names
  Cancel, ///< in the picker: close it, trading nothing
};

struct EquipInput
{
  EquipAction action = EquipAction::Up;
  std::uint8_t index = 0; ///< Select's row, from 0; Mount's mount, 0 to 3
};

/// What an action did: the screen after it, and what a trade left.
struct EquipStep
{
  EquipScreen screen;
  std::optional<EquipTrade> traded;
};

/// What buying row _row costs in _goods: fuel's fills the tank (FuelPriceTenths), anything else's is its price.
[[nodiscard]] std::uint32_t BuyingCostTenths(const EquipGoods& _goods, std::uint8_t _row);

/// What row _row sells back for: ResaleTenths of its price; nothing for fuel.
[[nodiscard]] std::uint32_t SellingPriceTenths(const EquipGoods& _goods, std::uint8_t _row) noexcept;

/// Why buying row _row would not happen before a mount is picked: ADR-043's refusals, but for a laser every mount carrying it
/// already (D82), and the price for anything but a laser, whose cost depends on its mount. EquipNotice::None when nothing stops it.
[[nodiscard]] EquipMessage RefuseToBuyEquipment(const EquipGoods& _goods, std::uint8_t _row);

/// Why fitting row _row's laser on mount _mount would not happen: the mount carries it already (D107), or the credits, with the
/// laser on it traded in at its sell price (D106), do not pay for it.
[[nodiscard]] EquipMessage RefuseToFitOnMount(const EquipGoods& _goods, std::uint8_t _row, std::uint8_t _mount);

/// Why selling row _row's laser off mount _mount would not happen: the mount carries no laser of its type.
[[nodiscard]] EquipMessage RefuseToSellOffMount(const EquipGoods& _goods, std::uint8_t _row, std::uint8_t _mount);

/// What _input does to _screen over _goods. A trade that happens leaves EquipStep::traded and its strip; one that is refused leaves
/// the refusal's strip; anything else clears the strip. B and S on a laser open the picker (D103); in it a mount trades or says why
/// not and keeps it open, and Cancel closes it. A move or a new row closes it.
[[nodiscard]] EquipStep StepEquip(const EquipScreen& _screen, const EquipInput& _input, const EquipGoods& _goods);

/// The action of the key with scan code _scanCode on D02, with or without the picker open (_picking): ↑ and ↓, B and S, and in
/// the picker 1 to 4 and Esc. Nothing for any other key.
[[nodiscard]] std::optional<EquipInput> EquipKey(std::uint8_t _scanCode, bool _picking) noexcept;

/// The action of a click on _click's control.
[[nodiscard]] EquipInput EquipClickInput(const EquipClick& _click) noexcept;

/// What D02 shows of _screen over _goods, unnumbered: the shell's EquipView.
[[nodiscard]] EquipView EquipViewOf(const EquipScreen& _screen, const EquipGoods& _goods);

/// The shipyard of the current system and the ship in _state.
[[nodiscard]] EquipGoods EquipGoodsOf(const GameState& _state);

/// D02 and D03, the shipyard (D82), which F3 opens while docked in place of the original's Equip Ship. Each tick a key, and the
/// clicks the shell sent, act on the screen (StepEquip), a trade is stored, and the view is published (Host::ShowEquip). Returns
/// the screen key that closes it: Esc with the picker closed, or an F-key other than F3.
[[nodiscard]] Task<ScreenKey> ShowEquipScreen(GameState& _state, Host& _host);

} // namespace Elite
