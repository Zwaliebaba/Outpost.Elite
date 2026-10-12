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

// Where the original's backward jumps land, for the turns of its loops (Hardware::LoopTurn).
constexpr std::uint16_t EQUIPMENT_LIST_ROW = 0x5C1E;
constexpr std::uint16_t EQUIP_STEER = 0x612F;
constexpr std::uint16_t EQUIP_CURSOR_UP = 0x613A;
constexpr std::uint16_t EQUIP_CURSOR_DOWN = 0x6169;
constexpr std::uint16_t EQUIP_TICK_LOOP = 0x6191;
constexpr std::uint16_t EQUIP_LASER_CHECKS = 0x61F3;
constexpr std::uint16_t EQUIP_MESSAGE = 0x6214;
constexpr std::uint16_t EQUIP_NOT_ENOUGH_CREDITS = 0x6241;
constexpr std::uint16_t EQUIP_SOLD_MESSAGE = 0x62FA;
constexpr std::uint16_t REMOVE_MOUNT_MESSAGE = 0x6509;

// Scan codes the menus read.
constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_S = 0x1F;
constexpr std::uint8_t SCAN_B = 0x30;
constexpr std::uint8_t SCAN_SPACE = 0x39;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F4 = 0x3E;
constexpr std::uint8_t SCAN_PAST_F10 = 0x45;
constexpr std::uint8_t SCAN_UP = 0x48;
constexpr std::uint8_t SCAN_LEFT = 0x4B;
constexpr std::uint8_t SCAN_RIGHT = 0x4D;
constexpr std::uint8_t SCAN_DOWN = 0x50;

// The text page: offsets in B800, 80 bytes a row.
constexpr std::uint16_t ROW_BYTES = 0x50;
constexpr std::uint16_t TITLE_OFFSET = 0x54;
constexpr std::uint16_t MESSAGE_OFFSET = 0x78;
constexpr std::uint16_t HELP_TEXT_OFFSET = 0xF4;
constexpr std::uint16_t MOUNT_HEADER_OFFSET = 0x144;
constexpr std::uint16_t SELL_LASER_OFFSET = 0x174;
constexpr std::uint16_t MOUNT_NAMES_OFFSET = 0x284;
constexpr std::uint16_t FIRST_ITEM_OFFSET = 0x326; // the first item's name
constexpr std::uint16_t EQUIP_MENU_FIRST_ROW = 10; // on the text page
constexpr std::uint16_t PRICE_COLUMN = 0x2A;
constexpr std::uint16_t RESALE_COLUMN = 0x38;
constexpr std::uint16_t SELL_PRICE_COLUMN = 0x39;
constexpr std::uint16_t MENU_ROW_ATTRIBUTE = 5; // a menu row's first byte the highlight changes: its third character's attribute

constexpr std::uint8_t SELL_PRICE_ATTRIBUTE = 0x79;
constexpr std::uint8_t MENU_ATTRIBUTE = 0x1E;
constexpr std::uint8_t HELP_ATTRIBUTE = 0x1F;
constexpr std::uint8_t MOUNT_HIGHLIGHT_ATTRIBUTE = 0x70;

// The equipment menu's messages, 19 characters each, and the mount menu's texts.

constexpr std::size_t PRICE_SLOT_BYTES = 4;       // screenPrices' four bytes a row: the price, then the resale price, in tenths
constexpr std::uint8_t RESALE_DIGITS_BLANKED = 3; // and those the equipment screen blanks in a resale price

// The equipment menu's rows, from 1. Row r's fitted count is the byte at FITTED_BEFORE_FUEL + r: fuel's is row 1.
constexpr std::uint8_t FUEL_ROW = 1;
constexpr std::uint8_t FIRST_FITTED_ROW = 3;
constexpr std::uint8_t CARGO_BAY_ROW = 3;
constexpr std::uint8_t FUEL_SCOOPS_ROW = 7;
constexpr std::uint8_t MINING_LASER_ROW = 0x0D;
constexpr std::size_t FITTED_ROWS = 0x0E; // fitted counts from fuel's, row 1's, up
constexpr std::uint8_t FUEL_UNITS_PER_TENTH = 0x24;
constexpr std::array<std::uint8_t, 4> LASER_ROWS = {0x05, 0x06, 0x0D, 0x0E}; // pulse, beam, mining, military

constexpr std::uint8_t MISSILE_LIMIT = 4;
constexpr std::uint8_t FUEL_NEARLY_FULL = 0xFD;
constexpr std::uint8_t FUEL_FULL = 0xFF;
constexpr std::uint8_t NO_FUEL_MISSION = 1;
constexpr std::uint8_t CARGO_BAY_SALE_TONNES = 0x15; // the extension sells only with less than this aboard
constexpr std::uint8_t ALL_MOUNTS = 0x0F;

constexpr std::uint16_t TICKS_PER_POLL = 100;

// The laser mount menu: four boxes of 8x3 cells, 12h bytes apart.
constexpr std::uint16_t MOUNT_COUNT = 4;
constexpr std::uint16_t FIRST_MOUNT_BOX = 0x1E5;
constexpr std::uint16_t LAST_MOUNT_BOX = 0x21B;
constexpr std::uint16_t MOUNT_BOX_STEP = 0x12;
constexpr std::uint8_t LAST_MOUNT = 3;
constexpr std::uint8_t MOUNT_TYPE_BITS = 3; // a mount's two bits of laserMountTypes
constexpr std::uint8_t MOUNT_BOX_ROWS = 3;
constexpr std::uint16_t MOUNT_BOX_COLUMNS = 8;
constexpr std::uint16_t MOUNT_BOX_ROW_SKIP = 0x40;

// LaunchEscapePod.
constexpr std::uint8_t ESCAPE_POD_FRAMES = 0x50;
constexpr std::uint16_t HALF_TURN = 0x400;
constexpr std::uint16_t ESCAPE_POD_SPEED = 0x14;
constexpr std::uint16_t ESCAPE_POD_MOVES = 12;
constexpr std::uint16_t CARGO_KINDS = 0x11;

// TryScoopObject: the scoop's box in view coordinates, the hold's size, and what is scooped.
constexpr std::uint8_t CARGO_BAY_TONNES = 0x14;
constexpr std::uint8_t LARGE_CARGO_BAY_TONNES = 0x23;
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
constexpr std::uint8_t PRECIOUS_MOST = 0xFA;
constexpr std::uint8_t MINERALS_FROM = 0x28;

// screenPrices: row r's price at 4r, and its resale price after it.
[[nodiscard]] std::span<Counted<std::uint8_t>> ScreenPrices(GameState& _state)
{
  return BytesOf(_state.Data().menus.screenPrices);
}

// The index in screenPrices of row _row's price.
[[nodiscard]] std::size_t PriceSlot(std::uint8_t _row) noexcept
{
  return std::size_t{_row} * PRICE_SLOT_BYTES;
}

// Row _row's fitted count in the commander's block, from 1, fuel's.
[[nodiscard]] Counted<std::uint8_t>& Fitted(GameState& _state, std::uint8_t _row)
{
  return BytesWithin(BytesOf(_state.Data().commander), COMMANDER_STATE_OFFSET(fuel), FITTED_ROWS)[std::size_t{_row} - 1];
}

// The text in _table, a table of the reference's bytes, from its byte _at up to its NUL.
[[nodiscard]] std::string_view TextInTable(std::span<const std::uint8_t> _table, std::size_t _at) noexcept
{
  const std::string_view table(reinterpret_cast<const char*>(_table.data()), _table.size());
  return table.substr(_at, table.find('\0', _at) - _at);
}

// IMUL r/m8: AL times _factor, both signed, into AX.
[[nodiscard]] std::uint16_t SignedProduct(std::uint8_t _al, std::uint8_t _factor) noexcept
{
  return static_cast<std::uint16_t>(static_cast<std::int8_t>(_al) * static_cast<std::int8_t>(_factor));
}

// AND r16,r16 / JNS / NEG r16.
[[nodiscard]] std::uint16_t Magnitude(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0 ? Negate(_value) : _value;
}

// INC BYTE PTR [_byte].
void IncrementByte(Counted<std::uint8_t>& _byte)
{
  _byte = static_cast<std::uint8_t>(_byte + 1);
}

// ADD [_field],_amount, held at 250 (FAh) from 251 up: gems, gold and platinum.
void AddPrecious(Counted<std::uint8_t>& _field, std::uint8_t _amount)
{
  _field = static_cast<std::uint8_t>(_field + _amount);
  if (_field > PRECIOUS_MOST)
  {
    _field = PRECIOUS_MOST;
  }
}

// TryScoopObject's box test (4414-443F): whether _view is in the scoop's box: y from 30 to 229, then x and z within 149 either
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

// PrintEquipmentSellColumn (0x6949): _text in the resale column of menuSelectedRow's row, in the resale price's attribute, and
// the menu's attribute put back. Returns the cell after its last character.
std::uint16_t PrintEquipmentSellColumn(GameState& _state, std::string_view _text)
{
  _state.Data().drawing.textAttribute = SELL_PRICE_ATTRIBUTE;
  // (row-1)*80 bytes, as (row-1)*256/4 and that /4 again.
  const auto quarter = static_cast<std::uint16_t>((static_cast<std::uint8_t>(_state.Data().menus.menuSelectedRow - 1) << 8) >> 2);
  const auto cell = static_cast<std::uint16_t>(MenuFirstRowCell(_state) + quarter + (quarter >> 2) + SELL_PRICE_COLUMN);
  const std::uint16_t printed = PrintTextModeString(_state, _text, cell);
  _state.Data().drawing.textAttribute = MENU_ATTRIBUTE;
  return printed;
}

// The name of the laser type on mount _mount, from its two bits of laserMountTypes _types.
[[nodiscard]] std::string_view MountLaserName(std::uint8_t _types, std::uint16_t _mount) noexcept
{
  return LASER_TYPE_NAMES[static_cast<std::uint8_t>(_types >> (_mount * 2)) & MOUNT_TYPE_BITS];
}

// A quarter of _rows rows of the text page, in bytes, as the menus measure it: _rows*256, shifted right twice.
[[nodiscard]] std::uint16_t MenuRowsQuarter(std::uint8_t _rows) noexcept
{
  return static_cast<std::uint16_t>(Join(_rows, 0) >> 2);
}

// 61CF and 628A: B and S push CX, SI and textAttribute for 6214 to pop after the message, and set the message's attribute.
// Returns the attribute, which the original pushes in AL.
[[nodiscard]] std::uint8_t OpenEquipMessage(GameState& _state)
{
  const std::uint8_t saved = _state.Data().drawing.textAttribute;
  _state.Data().drawing.textAttribute = MENU_ATTRIBUTE;
  return saved;
}

// 6214: the message _text on the message line, and textAttribute put back to _saved, which the original pops into AL before its
// jump back to the steering.
void ShowEquipMessage(GameState& _state, std::string_view _text, std::uint8_t _saved)
{
  PrintTextModeString(_state, _text, MESSAGE_OFFSET);
  _state.Data().drawing.textAttribute = _saved;
}

// The jump back to 6214 that ends each of B's and S's paths, with the message it shows.
[[nodiscard]] std::string_view ToEquipMessage(Hardware& _hardware, std::string_view _text)
{
  _hardware.LoopTurn(EQUIP_MESSAGE, {});
  return _text;
}

// 6241, reached by a jump back: NOT ENOUGH CREDITS!, and the jump back to 6214.
[[nodiscard]] std::string_view ReportNotEnoughCredits(Hardware& _hardware)
{
  _hardware.LoopTurn(EQUIP_NOT_ENOUGH_CREDITS, {});
  return ToEquipMessage(_hardware, NOT_ENOUGH_CREDITS_TEXT);
}

// 6228: an item or a laser paid for and counted in _fitted, its resale price shown, and a laser's mount chosen. Returns the
// message.
[[nodiscard]] std::string_view BuyFittedItem(GameState& _state, Hardware& _hardware, Counted<std::uint8_t>& _fitted)
{
  if (!PayForEquipmentItem(_state))
  {
    return ToEquipMessage(_hardware, NOT_ENOUGH_CREDITS_TEXT);
  }
  IncrementByte(_fitted);
  ShowEquipmentSellPrice(_state);
  if (SelectLaserType(_state))
  {
    ChooseMountToFitLaser(_state, _hardware);
  }
  return ToEquipMessage(_hardware, ITEM_PURCHASED_TEXT);
}

// 6246: B on fuel (a full tank, never in mission 1) or a missile (up to four), _row 1 or 2, by the row less 1, which the
// original holds in BX. Returns the message.
[[nodiscard]] std::string_view BuyFuelOrMissile(GameState& _state, Hardware& _hardware, std::uint8_t _row)
{
  const auto bx = static_cast<std::uint16_t>(static_cast<std::uint8_t>(_row - 1));
  if (bx != 0)
  {
    if (_state.Data().commander.missileCount == MISSILE_LIMIT)
    {
      return ToEquipMessage(_hardware, FOUR_MISSILES_ONLY_TEXT);
    }
    if (!PayForEquipmentItem(_state))
    {
      return ReportNotEnoughCredits(_hardware);
    }
    _state.Data().commander.missileCount = static_cast<std::uint8_t>(_state.Data().commander.missileCount + 1);
    ShowEquipmentSellPrice(_state);
    return ToEquipMessage(_hardware, MISSILE_PURCHASED_TEXT);
  }
  if (_state.Data().commander.missionNumber == NO_FUEL_MISSION)
  {
    return ToEquipMessage(_hardware, NO_FUEL_AVAILABLE_TEXT);
  }
  if (_state.Data().commander.fuel >= FUEL_NEARLY_FULL)
  {
    return ToEquipMessage(_hardware, FUEL_FULL_TEXT);
  }
  if (!PayForEquipmentItem(_state))
  {
    return ReportNotEnoughCredits(_hardware);
  }
  _state.Data().commander.fuel = FUEL_FULL;
  return ToEquipMessage(_hardware, FUEL_PURCHASED_TEXT);
}

// 61DA: B on the equipment menu, once OpenEquipMessage has saved the attribute. Returns the message for ShowEquipMessage, after
// the jumps back the original takes on the way.
[[nodiscard]] std::string_view BuyEquipment(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t row = _state.Data().menus.menuSelectedRow;
  if (row < FIRST_FITTED_ROW)
  {
    return BuyFuelOrMissile(_state, _hardware, row);
  }
  Counted<std::uint8_t>& fitted = Fitted(_state, row);
  if (fitted != 0)
  {
    if (!SelectLaserType(_state))
    {
      return ALREADY_FITTED_TEXT;
    }
  }
  else
  {
    if (!SelectLaserType(_state))
    {
      return BuyFittedItem(_state, _hardware, fitted);
    }
    _hardware.LoopTurn(EQUIP_LASER_CHECKS, {});
  }
  // A laser: a free mount, and a mining laser only with fuel scoops.
  if (_state.Data().commander.laserMountsFitted == ALL_MOUNTS)
  {
    return FOUR_LASERS_TEXT;
  }
  if (_state.Data().menus.menuSelectedRow == MINING_LASER_ROW && _state.Data().commander.fuelScoopsFitted != 1)
  {
    return SCOOP_NEEDED_TEXT;
  }
  return BuyFittedItem(_state, _hardware, fitted);
}

// 62FD: S on fuel (never) or a missile, for the missile's resale price, _row 1 or 2. Returns the message.
[[nodiscard]] std::string_view SellFuelOrMissile(GameState& _state, Hardware& _hardware, std::uint8_t _row)
{
  if (_row == FUEL_ROW)
  {
    return ToEquipMessage(_hardware, FUEL_SALE_ILLEGAL_TEXT);
  }
  if (_state.Data().commander.missileCount == 0)
  {
    _hardware.LoopTurn(EQUIP_SOLD_MESSAGE, {});
    return ToEquipMessage(_hardware, NO_MISSILE_TO_SELL_TEXT);
  }
  const auto left = static_cast<std::uint8_t>(_state.Data().commander.missileCount - 1);
  _state.Data().commander.missileCount = left;
  if (left == 0)
  {
    ClearEquipmentSellPrice(_state);
  }
  AddCredits(_state, _state.Data().menus.screenPrices.data8245);
  return ToEquipMessage(_hardware, MISSILE_SOLD_TEXT);
}

// 6295: S on the equipment menu, once OpenEquipMessage has saved the attribute, for the resale price. Returns the message for
// ShowEquipMessage, after the jumps back the original takes on the way.
[[nodiscard]] std::string_view SellEquipment(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t row = _state.Data().menus.menuSelectedRow;
  if (row < FIRST_FITTED_ROW)
  {
    return SellFuelOrMissile(_state, _hardware, row);
  }
  if (row == CARGO_BAY_ROW && _state.Data().commander.cargoHold.cargoUsedTonnes >= CARGO_BAY_SALE_TONNES)
  {
    return ToEquipMessage(_hardware, TOO_MUCH_CARGO_TEXT);
  }
  Counted<std::uint8_t>& fitted = Fitted(_state, row);
  if (fitted == 0)
  {
    return ToEquipMessage(_hardware, NO_ITEM_TO_SELL_TEXT);
  }
  if (_state.Data().menus.menuSelectedRow == FUEL_SCOOPS_ROW && _state.Data().commander.miningLaserCount != 0)
  {
    return ToEquipMessage(_hardware, SELL_MINING_LASER_TEXT);
  }
  const auto left = static_cast<std::uint8_t>(fitted - 1);
  fitted = left;
  if (left == 0)
  {
    ClearEquipmentSellPrice(_state);
  }
  AddCredits(_state, WordIn(ScreenPrices(_state), PriceSlot(_state.Data().menus.menuSelectedRow) + 2));
  if (SelectLaserType(_state))
  {
    ChooseMountToRemoveLaser(_state, _hardware);
  }
  return ToEquipMessage(_hardware, ITEM_SOLD_TEXT);
}

// 63C3 and 6490: the mount box at _box painted in textAttribute, then the one left of it, from FORE round to LEFT,
// highlighted. Returns that box.
std::uint16_t MoveMountBoxLeft(GameState& _state, std::uint16_t _box)
{
  PaintLaserMountBox(_state, _box);
  auto box = static_cast<std::uint16_t>(_box - MOUNT_BOX_STEP);
  const auto mount = static_cast<std::uint8_t>(_state.Data().equipment.selectedLaserMount - 1);
  _state.Data().equipment.selectedLaserMount = mount;
  if ((mount & 0x80) != 0)
  {
    _state.Data().equipment.selectedLaserMount = LAST_MOUNT;
    box = LAST_MOUNT_BOX;
  }
  _state.Data().drawing.textAttribute = MOUNT_HIGHLIGHT_ATTRIBUTE;
  PaintLaserMountBox(_state, box);
  return box;
}

// 63E1 and 64AE: the mount box at _box painted in textAttribute, then the one right of it, from LEFT round to FORE,
// highlighted. Returns that box.
std::uint16_t MoveMountBoxRight(GameState& _state, std::uint16_t _box)
{
  PaintLaserMountBox(_state, _box);
  auto box = static_cast<std::uint16_t>(_box + MOUNT_BOX_STEP);
  const auto mount = static_cast<std::uint8_t>(_state.Data().equipment.selectedLaserMount + 1);
  _state.Data().equipment.selectedLaserMount = mount;
  if (mount == MOUNT_COUNT)
  {
    _state.Data().equipment.selectedLaserMount = 0;
    box = FIRST_MOUNT_BOX;
  }
  _state.Data().drawing.textAttribute = MOUNT_HIGHLIGHT_ATTRIBUTE;
  PaintLaserMountBox(_state, box);
  return box;
}

// 6438 and 6505: the mount message _text on the message line, in the menu's attribute. _jumpedTo, when not 0, is
// where the original jumps back to on the way: 6509, from 653D, the tail the two messages share (ADR-015). The PUSH SI /
// POP SI round it keep the box, which the routine never touches.
void PrintMountMessage(GameState& _state, Hardware& _hardware, std::string_view _text, std::uint16_t _jumpedTo)
{
  if (_jumpedTo != 0)
  {
    _hardware.LoopTurn(_jumpedTo, {});
  }
  _state.Data().drawing.textAttribute = MENU_ATTRIBUTE;
  PrintTextModeString(_state, _text, MESSAGE_OFFSET);
}

// SHR CH,CL with CL the selected mount + 1 and CH laserMountsFitted: whether the mount's bit, which it shifts out into the
// carry, is set.
[[nodiscard]] bool IsMountFitted(const GameState& _state)
{
  return ((_state.Data().commander.laserMountsFitted >> _state.Data().equipment.selectedLaserMount) & 1) != 0;
}

// 642A: Space in ChooseMountToFitLaser. A free mount gets the bought laser: its bit of laserMountsFitted set, and its two bits
// of laserMountTypes cleared and then set to the type, and the help text redrawn over the mount menu. Returns whether it did;
// otherwise it says the mount is occupied.
bool FitLaserOnMount(GameState& _state, Hardware& _hardware)
{
  if (IsMountFitted(_state))
  {
    PrintMountMessage(_state, _hardware, MOUNT_OCCUPIED_TEXT, 0);
    return false;
  }
  const std::uint8_t mount = _state.Data().equipment.selectedLaserMount;
  _state.Data().commander.laserMountsFitted = static_cast<std::uint8_t>(_state.Data().commander.laserMountsFitted | (1u << mount));
  // 3 and the type, shifted up together as AX to the mount's two bits.
  const auto bits = static_cast<std::uint16_t>(Join(_state.Data().equipment.selectedLaserType, MOUNT_TYPE_BITS) << (mount * 2u));
  _state.Data().commander.laserMountTypes = static_cast<std::uint8_t>(_state.Data().commander.laserMountTypes & ~Low(bits));
  _state.Data().commander.laserMountTypes = static_cast<std::uint8_t>(_state.Data().commander.laserMountTypes | High(bits));
  RedrawEquipHelpText(_state);
  return true;
}

// 64F7: Space in ChooseMountToRemoveLaser. A mount holding the sold laser's type is freed: its bit of laserMountsFitted cleared,
// and the help text redrawn. Returns whether it was; otherwise it says why. "Wrong laser type!" jumps back into the message code
// the other message runs on into (653D to 6509).
bool RemoveLaserFromMount(GameState& _state, Hardware& _hardware)
{
  if (!IsMountFitted(_state))
  {
    PrintMountMessage(_state, _hardware, NO_LASER_ON_MOUNT_TEXT, 0);
    return false;
  }
  const std::uint8_t mount = _state.Data().equipment.selectedLaserMount;
  if (((_state.Data().commander.laserMountTypes >> (mount * 2u)) & MOUNT_TYPE_BITS) != _state.Data().equipment.selectedLaserType)
  {
    PrintMountMessage(_state, _hardware, WRONG_LASER_TYPE_TEXT, REMOVE_MOUNT_MESSAGE);
    return false;
  }
  _state.Data().commander.laserMountsFitted = static_cast<std::uint8_t>(_state.Data().commander.laserMountsFitted & ~(1u << mount));
  RedrawEquipHelpText(_state);
  return true;
}

// The two mount choosers are one loop at two addresses: where each backward jump lands, and what Space does.
struct MountChooser
{
  std::uint16_t steer;                  // CALL ReadSteering, where every other key goes back to
  std::uint16_t left;                   // the box moved left
  std::uint16_t right;                  // the box moved right
  std::uint16_t tickLoop;               // the poll's LOOP
  bool (*space)(GameState&, Hardware&); // Space's work on the selected mount: true once it is done
};

constexpr MountChooser FIT_CHOOSER{0x63B5, 0x63C3, 0x63E1, 0x6405, &FitLaserOnMount};
constexpr MountChooser REMOVE_CHOOSER{0x6482, 0x6490, 0x64AE, 0x64D2, &RemoveLaserFromMount};

// 63B5-6448 and 6482-6515: the box moved by the steering's roll or the arrow keys until Space does its work, from the box and AL
// in _start. Each turn to the steering carries the box (SI) and AL, which the original fires the stick with and GetKey shifts
// each key's Shift into; each turn to a box's move carries the box.
void RunMountChooser(GameState& _state, Hardware& _hardware, const MountChooser& _chooser, MenuLoop _start)
{
  MenuLoop loop = _start;
  for (;;)
  {
    const std::uint8_t roll = ReadSteering(_state, _hardware).roll;
    loop.al = roll;
    if (roll != 0)
    {
      _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
      loop.row = (roll & 0x80) != 0 ? MoveMountBoxLeft(_state, loop.row) : MoveMountBoxRight(_state, loop.row);
      loop.al = MOUNT_HIGHLIGHT_ATTRIBUTE; // the second PaintLaserMountBox's AL
    }
    for (;;)
    {
      const KeyPress key = PollMenuKey(_state, _hardware, _chooser.tickLoop);
      loop.al = AlAfterKey(loop.al, key);
      const std::uint8_t code = key.scanCode;
      if (code == 0)
      {
        _hardware.LoopTurn(_chooser.steer, {loop.row, loop.al});
        break;
      }
      // PUSH AX and POP AX round it.
      ClearDockedMessageLine(_state, GameState::VIDEO_SEGMENT);
      if (code == SCAN_SPACE)
      {
        if (_chooser.space(_state, _hardware))
        {
          return;
        }
        loop.al = 0; // the message's NUL
        _hardware.LoopTurn(_chooser.steer, {loop.row, loop.al});
        break;
      }
      _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
      if (code == SCAN_LEFT || code == SCAN_RIGHT)
      {
        _hardware.LoopTurn(code == SCAN_LEFT ? _chooser.left : _chooser.right, {loop.row});
        loop.row = code == SCAN_LEFT ? MoveMountBoxLeft(_state, loop.row) : MoveMountBoxRight(_state, loop.row);
        loop.al = MOUNT_HIGHLIGHT_ATTRIBUTE;
        continue;
      }
      _hardware.LoopTurn(_chooser.steer, {loop.row, loop.al});
      break;
    }
  }
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
  // MOV [DI],CH, which is 0 throughout: the 17 amounts held emptied.
  const std::span<Counted<std::uint8_t>> hold = BytesOf(_state.Data().commander.cargoHold);
  for (std::uint16_t kind = 0; kind < CARGO_KINDS; ++kind)
  {
    hold[std::size_t{kind} * 2] = 0;
  }
}

void TryScoopObject(GameState& _state, SpaceObject& _slot, const Vector& _view)
{
  std::uint8_t space = _state.Data().commander.largeCargoBayFitted == 1 ? LARGE_CARGO_BAY_TONNES : CARGO_BAY_TONNES;
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
      (void)RemoveObject(_state, _slot);
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
      (void)RemoveObject(_state, _slot);
      const std::uint8_t random = Low(NextRandom(_state));
      std::uint8_t product = DivideByte(random, RANDOM_PRODUCT_DIVISOR).quotient;
      if (product == PRODUCT_SLAVES)
      {
        product = PRODUCT_FURS;
      }
      IncrementByte(BytesOf(_state.Data().commander.cargoHold)[std::size_t{product} * 2]);
      _state.Data().commander.cargoHold.cargoUsedTonnes = static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + 1);
      // The product's name, its first 13 bytes, over the message's.
      const std::span<const Counted<std::uint8_t>> name = std::span<const Counted<std::uint8_t>>(_state.Data().docked.productNames)
                                                            .subspan(std::size_t{product} * PRODUCT_NAME_BYTES, SCOOPED_NAME_BYTES);
      const std::span<Counted<std::uint8_t>> scooped =
        BytesWithin(BytesOf(_state.Data().equipment.scoopedCargoText), 0, SCOOPED_NAME_BYTES);
      for (std::size_t letter = 0; letter < SCOOPED_NAME_BYTES; ++letter)
      {
        scooped[letter] = std::uint8_t{name[letter]};
      }
      message = Message::ScoopedCargo;
    }
  }
  else if (type == TYPE_SPLINTER && (flags & FLAG_PRECIOUS) != 0)
  {
    // Gems, gold and platinum always; minerals or alloys too if there is room. Gold gets the gems' AL, as the original adds it.
    (void)RemoveObject(_state, _slot);
    const std::uint8_t gems = Low(NextRandom(_state)) & 7;
    AddPrecious(_state.Data().commander.cargoHold.cargoGemStonesGrams, gems);
    AddPrecious(_state.Data().commander.cargoHold.cargoGoldKg, gems);
    AddPrecious(_state.Data().commander.cargoHold.cargoPlatinumKg, static_cast<std::uint8_t>((High(NextRandom(_state)) & 3) + 1));
    if (_state.Data().equipment.freeCargoTonnes != 0)
    {
      CommanderState::CargoHold& hold = _state.Data().commander.cargoHold;
      IncrementByte(Low(NextRandom(_state)) >= MINERALS_FROM ? hold.cargoMineralsTonnes : hold.cargoAlloysTonnes);
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
      (void)RemoveObject(_state, _slot);
      CommanderState::CargoHold& hold = _state.Data().commander.cargoHold;
      Counted<std::uint8_t>& held = type == TYPE_SPLINTER     ? hold.cargoAlloysTonnes
                                    : type == TYPE_ESCAPE_POD ? hold.cargoSlavesTonnes
                                                              : hold.cargoAlienItemsTonnes;
      held = static_cast<std::uint8_t>(held + 1);
      _state.Data().commander.cargoHold.cargoUsedTonnes = static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + 1);
      message = type == TYPE_SPLINTER ? Message::MetalAlloys : type == TYPE_ESCAPE_POD ? Message::EscapePodRetrieved : Message::AlienItems;
    }
  }
  _state.Data().messages.messagePointer = message;
  _state.Data().messages.messageFrames = SCOOP_MESSAGE_FRAMES;
}

ScreenKey ShowEquipShipScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, EQUIP_SHIP_FRAME, _backward), TITLE_OFFSET);
  PrintCountedTextLines(_state, EQUIP_HELP_TEXT, HELP_TEXT_OFFSET);

  // equipmentList: a count, then for each item its tech level, name, government and economy factors and base price. The list is
  // sorted by tech level, so the first item beyond currentTechLevel+1 ends it, or else the count does, after the last item and
  // before the byte past the table. Each turn carries the count, the item's place, SI, as its byte in the table (D38), and the
  // row's place.
  const std::span<const std::uint8_t> list(EQUIPMENT_LIST);
  std::uint16_t left = list[0];
  std::size_t item = 1;
  const auto techLimit = static_cast<std::uint8_t>(_state.Data().commander.currentTechLevel + 2);
  _state.Data().menus.menuRowCount = 0;
  std::uint16_t cell = FIRST_ITEM_OFFSET;
  const std::span<Counted<std::uint8_t>> prices = ScreenPrices(_state);
  const std::span<Counted<std::uint8_t>> priceText = BytesOf(_state.Data().menus.priceText);
  while (list[item] < techLimit)
  {
    const auto row = static_cast<std::uint8_t>(_state.Data().menus.menuRowCount + 1);
    _state.Data().menus.menuRowCount = row;
    // The price: the government's and the economy's factors, each by IMUL, and the base price.
    const std::string_view name = TextInTable(list, item + 1);
    PrintTextModeString(_state, name, cell);
    std::size_t factor = item + 1 + name.size() + 1;
    const std::uint16_t government = SignedProduct(list[factor], _state.Data().commander.currentGovernment);
    ++factor;
    const std::uint16_t economy = SignedProduct(list[factor], _state.Data().commander.currentEconomy);
    ++factor;
    const auto price = static_cast<std::uint16_t>(economy + government + WordIn(list, factor));
    SetWordIn(prices, PriceSlot(row), price);
    SetWordIn(prices, PriceSlot(row) + 2, 0);
    // A resale price only for an item fitted, and never for fuel.
    const bool resold = row != FUEL_ROW && Fitted(_state, row) != 0;
    _state.Data().menus.resalePriceInput = resold ? price : std::uint16_t{0};
    FormatTenths(_state, price);
    PrintTextModeString(_state, TextIn(priceText), Offset(cell, PRICE_COLUMN));
    const std::uint16_t resale = ComputeResalePrice(_state);
    std::string_view resaleText = NO_RESALE_TEXT;
    if (resale != 0)
    {
      SetWordIn(prices, PriceSlot(_state.Data().menus.menuRowCount) + 2, resale);
      FormatDecimal5(resale, priceText);
      BlankLeadingZeros(priceText, RESALE_DIGITS_BLANKED);
      // Tenths: the last digit moved right one for the point.
      _state.Data().menus.priceText.data8041 = std::uint8_t{_state.Data().menus.priceText.data8040};
      _state.Data().menus.priceText.data8040 = '.';
      resaleText = TextIn(priceText);
    }
    PrintTextModeString(_state, resaleText, Offset(cell, RESALE_COLUMN));
    cell = Offset(cell, ROW_BYTES);
    item = factor + 2;
    if (--left == 0)
    {
      break;
    }
    _hardware.LoopTurn(EQUIPMENT_LIST_ROW, {left, static_cast<std::uint16_t>(item), cell});
  }
  return RunEquipShipMenu(_state, _hardware, EQUIP_MENU_FIRST_ROW);
}

bool SelectLaserType(GameState& _state)
{
  const std::uint8_t row = _state.Data().menus.menuSelectedRow;
  std::uint8_t type = 0;
  _state.Data().equipment.selectedLaserType = type;
  for (;;)
  {
    const bool laser = row == LASER_ROWS[type];
    if (laser || type + 1u == LASER_ROWS.size())
    {
      return laser;
    }
    ++type;
    _state.Data().equipment.selectedLaserType = type;
  }
}

ScreenKey RunEquipShipMenu(GameState& _state, Hardware& _hardware, std::uint16_t _firstRow)
{
  _state.Data().menus.menuFirstRowAttr = _firstRow;
  // StartMenu's last ToggleMenuRowHighlight leaves AL the attribute it toggled the row to, which it left in textAttribute.
  MenuLoop loop{StartMenu(_state).row, 0};
  loop.al = _state.Data().drawing.textAttribute;
  for (;;)
  {
    // 612F: the steering; then the keys, each turn to the steering carrying the cursor and AL, each to a cursor's move the
    // cursor.
    loop = SteerMenuCursor(_state, _hardware, loop);
    for (;;)
    {
      const KeyPress key = PollMenuKey(_state, _hardware, EQUIP_TICK_LOOP);
      loop.al = AlAfterKey(loop.al, key);
      const std::uint8_t code = key.scanCode;
      if (code == 0)
      {
        _hardware.LoopTurn(EQUIP_STEER, {loop.row, loop.al});
        break;
      }
      // PUSH AX and POP AX round it.
      PrintCreditsOnMessageLine(_state);
      if (code == SCAN_B || code == SCAN_S)
      {
        // B buys and S sells the row's item, and its message; then AL is the attribute OpenEquipMessage saved, popped back.
        const std::uint8_t saved = OpenEquipMessage(_state);
        ShowEquipMessage(_state, code == SCAN_B ? BuyEquipment(_state, _hardware) : SellEquipment(_state, _hardware), saved);
        loop.al = saved;
        _hardware.LoopTurn(EQUIP_STEER, {loop.row, loop.al});
        break;
      }
      if (code == SCAN_UP || code == SCAN_DOWN)
      {
        _hardware.LoopTurn(code == SCAN_UP ? EQUIP_CURSOR_UP : EQUIP_CURSOR_DOWN, {loop.row});
        loop.row = (code == SCAN_UP ? MoveMenuCursorUp(_state, loop.row) : MoveMenuCursorDown(_state, loop.row)).row;
        loop.al = _state.Data().drawing.textAttribute;
        continue;
      }
      // The screen's own F4 does nothing here.
      if (code != SCAN_F4 && IsScreenKey(code))
      {
        return ScreenKey{code, loop.al, std::nullopt};
      }
      _hardware.LoopTurn(EQUIP_STEER, {loop.row, loop.al});
      break;
    }
  }
}

void DrawLaserMountMenu(GameState& _state)
{
  _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
  PrintCountedTextLines(_state, MOUNT_HEADER_LINES, MOUNT_HEADER_OFFSET);
  // Each mount's laser, or Free, one after the other: its bit of laserMountsFitted, its two bits of laserMountTypes.
  const std::uint8_t types = _state.Data().commander.laserMountTypes;
  const std::uint8_t fitted = _state.Data().commander.laserMountsFitted;
  std::uint16_t printed = MOUNT_NAMES_OFFSET;
  for (std::uint16_t mount = 0; mount < MOUNT_COUNT; ++mount)
  {
    printed = PrintTextModeString(_state, ((fitted >> mount) & 1) != 0 ? MountLaserName(types, mount) : FREE_MOUNT_TEXT, printed);
  }
  _state.Data().drawing.textAttribute = MOUNT_HIGHLIGHT_ATTRIBUTE;
  PaintLaserMountBox(_state, FIRST_MOUNT_BOX);
  _state.Data().equipment.selectedLaserMount = 0;
}

void ChooseMountToFitLaser(GameState& _state, Hardware& _hardware)
{
  DrawLaserMountMenu(_state);
  // DrawLaserMountMenu leaves SI on FORE's box, and AL the highlight its last PaintLaserMountBox painted.
  RunMountChooser(_state, _hardware, FIT_CHOOSER, MenuLoop{FIRST_MOUNT_BOX, MOUNT_HIGHLIGHT_ATTRIBUTE});
}

void ChooseMountToRemoveLaser(GameState& _state, Hardware& _hardware)
{
  DrawLaserMountMenu(_state);
  _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
  PrintTextModeString(_state, SELL_LASER_TEXT, SELL_LASER_OFFSET);
  // SI, pushed and popped round the print, on FORE's box; AL the print's NUL.
  RunMountChooser(_state, _hardware, REMOVE_CHOOSER, MenuLoop{FIRST_MOUNT_BOX, 0});
}

void RedrawEquipHelpText(GameState& _state)
{
  _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
  PrintCountedTextLines(_state, EQUIP_HELP_TEXT, HELP_TEXT_OFFSET);
  _state.Data().drawing.textAttribute = MENU_ATTRIBUTE;
}

void PaintLaserMountBox(GameState& _state, std::uint16_t _box)
{
  const std::uint8_t attribute = _state.Data().drawing.textAttribute;
  std::uint16_t cell = _box;
  for (std::uint8_t row = 0; row < MOUNT_BOX_ROWS; ++row)
  {
    for (std::uint16_t column = 0; column < MOUNT_BOX_COLUMNS; ++column)
    {
      _state.SetVideoByte(cell, attribute);
      cell = Offset(cell, 2);
    }
    cell = Offset(cell, MOUNT_BOX_ROW_SKIP);
  }
}

bool PayForEquipmentItem(GameState& _state)
{
  const std::uint8_t row = _state.Data().menus.menuSelectedRow;
  std::uint16_t tenths = 0;
  if (row == FUEL_ROW)
  {
    // What fills the tank: (255-fuel) * the price's low byte / 36, at least 1, by a DIV that can overflow into the game's trap.
    const auto missing = static_cast<std::uint8_t>(~_state.Data().commander.fuel);
    const auto product = static_cast<std::uint16_t>(missing * Low(_state.Data().menus.screenPrices.data823F));
    std::uint8_t units = DivideByte(product, FUEL_UNITS_PER_TENTH).quotient;
    if (units == 0)
    {
      units = 1;
    }
    tenths = units;
  }
  else
  {
    tenths = WordIn(ScreenPrices(_state), PriceSlot(row));
  }
  return SubtractCredits(_state, tenths);
}

void ClearEquipmentSellPrice(GameState& _state)
{
  PrintEquipmentSellColumn(_state, NO_RESALE_TEXT);
}

void ShowEquipmentSellPrice(GameState& _state)
{
  // The price's slot in screenPrices, and the resale price after it. The original keeps the slot in BX on the stack
  // round ComputeResalePrice.
  const std::size_t slot = PriceSlot(_state.Data().menus.menuSelectedRow);
  _state.Data().menus.resalePriceInput = WordIn(ScreenPrices(_state), slot);
  const std::uint16_t resale = ComputeResalePrice(_state);
  SetWordIn(ScreenPrices(_state), slot + 2, resale);
  FormatTenths(_state, resale);
  PrintEquipmentSellColumn(_state, TextWithin(BytesOf(_state.Data().menus.priceText), 0));
}

std::uint16_t MenuFirstRowCell(const GameState& _state) noexcept
{
  return static_cast<std::uint16_t>(_state.Data().menus.menuFirstRowAttr * ROW_BYTES + MENU_ROW_ATTRIBUTE);
}

MenuCursor StartMenu(GameState& _state)
{
  SwapTextAttributeNibbles(_state);
  PrintCreditsOnMessageLine(_state);
  SwapTextAttributeNibbles(_state);
  const std::uint16_t row = MenuFirstRowCell(_state);
  _state.Data().menus.menuSelectedRow = 1;
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, row);
  return MenuCursor{row};
}

MenuCursor MoveMenuCursorUp(GameState& _state, std::uint16_t _row)
{
  PrintCreditsOnMessageLine(_state);
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, _row);
  std::uint16_t row = _row;
  const auto selected = static_cast<std::uint8_t>(_state.Data().menus.menuSelectedRow - 1);
  _state.Data().menus.menuSelectedRow = selected;
  if (selected == 0)
  {
    // To the last row: count*80 bytes on, as count*256/4 and that /4 again.
    row = MenuFirstRowCell(_state);
    const std::uint8_t count = _state.Data().menus.menuRowCount;
    _state.Data().menus.menuSelectedRow = count;
    const std::uint16_t quarter = MenuRowsQuarter(count);
    row = Offset(Offset(row, quarter), static_cast<std::uint16_t>(quarter >> 2));
  }
  row = static_cast<std::uint16_t>(row - ROW_BYTES);
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, row);
  return MenuCursor{row};
}

MenuCursor MoveMenuCursorDown(GameState& _state, std::uint16_t _row)
{
  PrintCreditsOnMessageLine(_state);
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, _row);
  std::uint16_t row = _row;
  const auto selected = static_cast<std::uint8_t>(_state.Data().menus.menuSelectedRow + 1);
  _state.Data().menus.menuSelectedRow = selected;
  if (_state.Data().menus.menuRowCount < selected)
  {
    row = static_cast<std::uint16_t>(MenuFirstRowCell(_state) - ROW_BYTES);
    _state.Data().menus.menuSelectedRow = 1;
  }
  row = static_cast<std::uint16_t>(row + ROW_BYTES);
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, row);
  return MenuCursor{row};
}

bool IsScreenKey(std::uint8_t _key) noexcept
{
  return _key == SCAN_ESCAPE || (_key >= SCAN_F1 && _key < SCAN_PAST_F10);
}

MenuLoop SteerMenuCursor(GameState& _state, Hardware& _hardware, MenuLoop _loop)
{
  const Steering steering = ReadSteering(_state, _hardware);
  // NEG AH: the pitch negated, and up when that is negative.
  const std::uint8_t pitch = Negate(steering.pitch);
  if (pitch == 0)
  {
    return MenuLoop{_loop.row, steering.roll};
  }
  const MenuCursor cursor = (pitch & 0x80) != 0 ? MoveMenuCursorUp(_state, _loop.row) : MoveMenuCursorDown(_state, _loop.row);
  return MenuLoop{cursor.row, _state.Data().drawing.textAttribute};
}

KeyPress PollMenuKey(GameState& _state, Hardware& _hardware, std::uint16_t _tickLoop)
{
  for (std::uint16_t ticks = TICKS_PER_POLL;;)
  {
    WaitForTimerTick(_state, _hardware);
    if (--ticks == 0)
    {
      break;
    }
    _hardware.LoopTurn(_tickLoop, {ticks});
  }
  return GetKey(_state, _hardware);
}

} // namespace Elite
