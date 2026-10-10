#include "pch.h"

#include "Equipment.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Docked.h"
#include "Flight.h"
#include "Market.h"
#include "Maths.h"
#include "Ships.h"
#include "Text.h"
#include "Timer.h"

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_ZERO;
using Machine::Registers;

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
constexpr std::uint16_t FIRST_ITEM_OFFSET = 0x326;    // the first item's name
constexpr std::uint16_t EQUIP_MENU_FIRST_ROW = 0x325; // and its row's first attribute byte
constexpr std::uint16_t PRICE_COLUMN = 0x2A;
constexpr std::uint16_t RESALE_COLUMN = 0x38;
constexpr std::uint16_t SELL_PRICE_COLUMN = 0x39;

constexpr std::uint8_t SELL_PRICE_ATTRIBUTE = 0x79;
constexpr std::uint8_t MENU_ATTRIBUTE = 0x1E;
constexpr std::uint8_t HELP_ATTRIBUTE = 0x1F;
constexpr std::uint8_t MOUNT_HIGHLIGHT_ATTRIBUTE = 0x70;

// The equipment menu's messages, 19 characters each, and the mount menu's texts.
constexpr std::uint16_t MOUNT_HEADER_TEXT = 0x811D;
constexpr std::uint16_t SELL_LASER_TEXT = 0x81AE;
constexpr std::uint16_t FREE_MOUNT_TEXT = 0x81EA;
constexpr std::uint16_t NO_RESALE_TEXT = 0x821B;
constexpr std::uint16_t MISSILE_PURCHASED_TEXT = 0x828B;
constexpr std::uint16_t FOUR_MISSILES_ONLY_TEXT = 0x829F;
constexpr std::uint16_t FUEL_FULL_TEXT = 0x82B3;
constexpr std::uint16_t FUEL_PURCHASED_TEXT = 0x82C7;
constexpr std::uint16_t ALREADY_FITTED_TEXT = 0x82DB;
constexpr std::uint16_t ITEM_PURCHASED_TEXT = 0x82EF;
constexpr std::uint16_t NOT_ENOUGH_CREDITS_TEXT = 0x8303;
constexpr std::uint16_t NO_ITEM_TO_SELL_TEXT = 0x8317;
constexpr std::uint16_t ITEM_SOLD_TEXT = 0x832B;
constexpr std::uint16_t NO_MISSILE_TO_SELL_TEXT = 0x833F;
constexpr std::uint16_t MISSILE_SOLD_TEXT = 0x8353;
constexpr std::uint16_t FUEL_SALE_ILLEGAL_TEXT = 0x8367;
constexpr std::uint16_t TOO_MUCH_CARGO_TEXT = 0x837B;
constexpr std::uint16_t SCOOP_NEEDED_TEXT = 0x838F;
constexpr std::uint16_t SELL_MINING_LASER_TEXT = 0x83A3;
constexpr std::uint16_t NO_FUEL_AVAILABLE_TEXT = 0x83B7;
constexpr std::uint16_t FOUR_LASERS_TEXT = 0x8DDE;
constexpr std::uint16_t MOUNT_OCCUPIED_TEXT = 0x8DF2;
constexpr std::uint16_t NO_LASER_ON_MOUNT_TEXT = 0x8E06;
constexpr std::uint16_t WRONG_LASER_TYPE_TEXT = 0x8E1A;

constexpr std::uint16_t SCREEN_PRICES = 0x823B;   // four bytes a row: the price, then the resale price, in tenths
constexpr std::uint8_t TENTHS_DIGITS_BLANKED = 3; // the leading zeros FormatTenths blanks
constexpr std::uint8_t RESALE_DIGITS_BLANKED = 3; // and those the equipment screen blanks in a resale price

// The equipment menu's rows, from 1. Row r's fitted count is the byte at FITTED_BEFORE_FUEL + r: fuel's is row 1.
constexpr std::uint8_t FUEL_ROW = 1;
constexpr std::uint8_t FIRST_FITTED_ROW = 3;
constexpr std::uint8_t CARGO_BAY_ROW = 3;
constexpr std::uint8_t FUEL_SCOOPS_ROW = 7;
constexpr std::uint8_t MINING_LASER_ROW = 0x0D;
constexpr std::uint16_t FITTED_BEFORE_FUEL = 0x75E1;
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

[[nodiscard]] std::uint16_t PriceSlot(std::uint8_t _row) noexcept
{
  return static_cast<std::uint16_t>(SCREEN_PRICES + _row * 4);
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

// INC BYTE PTR [_offset].
void IncrementByte(GameState& _state, std::uint16_t _offset)
{
  _state.SetByte(_offset, static_cast<std::uint8_t>(_state.Byte(_offset) + 1));
}

// ADD [_field],_amount, held at 250 (FAh) from 251 up: gems, gold and platinum.
void AddPrecious(GameState& _state, DataField<std::uint8_t> _field, std::uint8_t _amount)
{
  _state.Set(_field, static_cast<std::uint8_t>(_state.Get(_field) + _amount));
  if (_state.Get(_field) > PRECIOUS_MOST)
  {
    _state.Set(_field, PRECIOUS_MOST);
  }
}

// What TryScoopObject's box test (4414-443F) finds: y from 30 to 229, then x and z within 149 either side.
struct ScoopBoxTest
{
  bool inside;
  std::optional<std::uint16_t> lastMeasure; ///< what it measured last, which the original leaves in DX: none when y is negative
};

[[nodiscard]] ScoopBoxTest TestScoopBox(const Vector& _view) noexcept
{
  const auto y = static_cast<std::uint16_t>(_view.y);
  if ((y & 0x8000) != 0)
  {
    return ScoopBoxTest{false, std::nullopt};
  }
  const auto above = static_cast<std::uint16_t>(y - SCOOP_LOWEST);
  if (y < SCOOP_LOWEST)
  {
    return ScoopBoxTest{false, above};
  }
  if (above >= SCOOP_HEIGHT)
  {
    return ScoopBoxTest{false, static_cast<std::uint16_t>(above - SCOOP_HEIGHT)};
  }
  const std::uint16_t across = Magnitude(static_cast<std::uint16_t>(_view.x));
  if (across >= SCOOP_HALF_WIDTH)
  {
    return ScoopBoxTest{false, across};
  }
  const std::uint16_t deep = Magnitude(static_cast<std::uint16_t>(_view.z));
  return ScoopBoxTest{deep < SCOOP_HALF_WIDTH, deep};
}

// PrintEquipmentSellColumn (0x6949): the text at DS:_text in the resale column of menuSelectedRow's row, in the resale
// price's attribute, and the menu's attribute put back.
PrintedText PrintEquipmentSellColumn(GameState& _state, std::uint16_t _text)
{
  _state.Set(DS.textAttribute, SELL_PRICE_ATTRIBUTE);
  // (row-1)*80 bytes, as (row-1)*256/4 and that /4 again.
  const auto quarter = static_cast<std::uint16_t>((static_cast<std::uint8_t>(_state.Get(DS.menuSelectedRow) - 1) << 8) >> 2);
  const auto cell = static_cast<std::uint16_t>(_state.Get(DS.menuFirstRowAttr) + quarter + (quarter >> 2) + SELL_PRICE_COLUMN);
  const PrintedText printed = PrintTextModeString(_state, _text, cell);
  _state.Set(DS.textAttribute, MENU_ATTRIBUTE);
  return printed;
}

// What PrintEquipmentSellColumn leaves in the registers: PrintTextModeString's, the resale price's attribute in AH.
void SellColumnOut(Registers& _regs, PrintedText _printed) noexcept
{
  _regs.si = _printed.end;
  _regs.di = _printed.nextCell;
  _regs.es = Guest::VIDEO_SEGMENT;
  _regs.ax = Join(SELL_PRICE_ATTRIBUTE, 0);
}

// The place of the laser type name's pointer for mount _mount, from its two bits of laserMountTypes _types.
[[nodiscard]] std::uint16_t MountLaserName(std::uint8_t _types, std::uint16_t _mount) noexcept
{
  return DS.laserTypeNames.At(static_cast<std::uint8_t>(_types >> (_mount * 2)) & MOUNT_TYPE_BITS);
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
  const std::uint8_t saved = _state.Get(DS.textAttribute);
  _state.Set(DS.textAttribute, MENU_ATTRIBUTE);
  return saved;
}

// 6214: the message at DS:_text on the message line, and textAttribute put back to _saved, which the original pops into AL
// before its jump back to the steering.
void ShowEquipMessage(GameState& _state, std::uint16_t _text, std::uint8_t _saved)
{
  PrintTextModeString(_state, _text, MESSAGE_OFFSET);
  _state.Set(DS.textAttribute, _saved);
}

// The jump back to 6214 that ends each of B's and S's paths, with the message it shows.
[[nodiscard]] std::uint16_t ToEquipMessage(Hardware& _hardware, std::uint16_t _text)
{
  _hardware.LoopTurn(EQUIP_MESSAGE, {});
  return _text;
}

// 6241, reached by a jump back: NOT ENOUGH CREDITS!, and the jump back to 6214.
[[nodiscard]] std::uint16_t ReportNotEnoughCredits(Hardware& _hardware)
{
  _hardware.LoopTurn(EQUIP_NOT_ENOUGH_CREDITS, {});
  return ToEquipMessage(_hardware, NOT_ENOUGH_CREDITS_TEXT);
}

// 6228: an item or a laser paid for and counted at DS:_fitted, which the original holds in BX and pushes round the payment,
// its resale price shown, and a laser's mount chosen. Returns the message.
[[nodiscard]] std::uint16_t BuyFittedItem(GameState& _state, Hardware& _hardware, std::uint16_t _fitted)
{
  if (!PayForEquipmentItem(_state, _fitted).paid)
  {
    return ToEquipMessage(_hardware, NOT_ENOUGH_CREDITS_TEXT);
  }
  IncrementByte(_state, _fitted);
  ShowEquipmentSellPrice(_state);
  if (SelectLaserType(_state))
  {
    ChooseMountToFitLaser(_state, _hardware);
  }
  return ToEquipMessage(_hardware, ITEM_PURCHASED_TEXT);
}

// 6246: B on fuel (a full tank, never in mission 1) or a missile (up to four), _row 1 or 2. The original holds the row less 1
// in BX, which PayForEquipmentItem's divide trap saves with the row in BL. Returns the message.
[[nodiscard]] std::uint16_t BuyFuelOrMissile(GameState& _state, Hardware& _hardware, std::uint8_t _row)
{
  const auto bx = static_cast<std::uint16_t>(static_cast<std::uint8_t>(_row - 1));
  if (bx != 0)
  {
    if (_state.Get(DS.missileCount) == MISSILE_LIMIT)
    {
      return ToEquipMessage(_hardware, FOUR_MISSILES_ONLY_TEXT);
    }
    if (!PayForEquipmentItem(_state, bx).paid)
    {
      return ReportNotEnoughCredits(_hardware);
    }
    _state.Set(DS.missileCount, static_cast<std::uint8_t>(_state.Get(DS.missileCount) + 1));
    ShowEquipmentSellPrice(_state);
    return ToEquipMessage(_hardware, MISSILE_PURCHASED_TEXT);
  }
  if (_state.Get(DS.missionNumber) == NO_FUEL_MISSION)
  {
    return ToEquipMessage(_hardware, NO_FUEL_AVAILABLE_TEXT);
  }
  if (_state.Get(DS.fuel) >= FUEL_NEARLY_FULL)
  {
    return ToEquipMessage(_hardware, FUEL_FULL_TEXT);
  }
  if (!PayForEquipmentItem(_state, bx).paid)
  {
    return ReportNotEnoughCredits(_hardware);
  }
  _state.Set(DS.fuel, FUEL_FULL);
  return ToEquipMessage(_hardware, FUEL_PURCHASED_TEXT);
}

// 61DA: B on the equipment menu, once OpenEquipMessage has saved the attribute. Returns the message for ShowEquipMessage, after
// the jumps back the original takes on the way.
[[nodiscard]] std::uint16_t BuyEquipment(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t row = _state.Get(DS.menuSelectedRow);
  if (row < FIRST_FITTED_ROW)
  {
    return BuyFuelOrMissile(_state, _hardware, row);
  }
  const auto fitted = static_cast<std::uint16_t>(row + FITTED_BEFORE_FUEL);
  if (_state.Byte(fitted) != 0)
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
  if (_state.Get(DS.laserMountsFitted) == ALL_MOUNTS)
  {
    return FOUR_LASERS_TEXT;
  }
  if (_state.Get(DS.menuSelectedRow) == MINING_LASER_ROW && _state.Get(DS.fuelScoopsFitted) != 1)
  {
    return SCOOP_NEEDED_TEXT;
  }
  return BuyFittedItem(_state, _hardware, fitted);
}

// 62FD: S on fuel (never) or a missile, for the missile's resale price, _row 1 or 2. Returns the message.
[[nodiscard]] std::uint16_t SellFuelOrMissile(GameState& _state, Hardware& _hardware, std::uint8_t _row)
{
  if (_row == FUEL_ROW)
  {
    return ToEquipMessage(_hardware, FUEL_SALE_ILLEGAL_TEXT);
  }
  if (_state.Get(DS.missileCount) == 0)
  {
    _hardware.LoopTurn(EQUIP_SOLD_MESSAGE, {});
    return ToEquipMessage(_hardware, NO_MISSILE_TO_SELL_TEXT);
  }
  const auto left = static_cast<std::uint8_t>(_state.Get(DS.missileCount) - 1);
  _state.Set(DS.missileCount, left);
  if (left == 0)
  {
    ClearEquipmentSellPrice(_state);
  }
  AddCredits(_state, _state.Get(DS.data8245));
  return ToEquipMessage(_hardware, MISSILE_SOLD_TEXT);
}

// 6295: S on the equipment menu, once OpenEquipMessage has saved the attribute, for the resale price. Returns the message for
// ShowEquipMessage, after the jumps back the original takes on the way.
[[nodiscard]] std::uint16_t SellEquipment(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t row = _state.Get(DS.menuSelectedRow);
  if (row < FIRST_FITTED_ROW)
  {
    return SellFuelOrMissile(_state, _hardware, row);
  }
  if (row == CARGO_BAY_ROW && _state.Get(DS.cargoUsedTonnes) >= CARGO_BAY_SALE_TONNES)
  {
    return ToEquipMessage(_hardware, TOO_MUCH_CARGO_TEXT);
  }
  const auto fitted = static_cast<std::uint16_t>(row + FITTED_BEFORE_FUEL);
  if (_state.Byte(fitted) == 0)
  {
    return ToEquipMessage(_hardware, NO_ITEM_TO_SELL_TEXT);
  }
  if (_state.Get(DS.menuSelectedRow) == FUEL_SCOOPS_ROW && _state.Get(DS.miningLaserCount) != 0)
  {
    return ToEquipMessage(_hardware, SELL_MINING_LASER_TEXT);
  }
  const auto left = static_cast<std::uint8_t>(_state.Byte(fitted) - 1);
  _state.SetByte(fitted, left);
  if (left == 0)
  {
    ClearEquipmentSellPrice(_state);
  }
  AddCredits(_state, _state.Word(Offset(PriceSlot(_state.Get(DS.menuSelectedRow)), 2)));
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
  const auto mount = static_cast<std::uint8_t>(_state.Get(DS.selectedLaserMount) - 1);
  _state.Set(DS.selectedLaserMount, mount);
  if ((mount & 0x80) != 0)
  {
    _state.Set(DS.selectedLaserMount, LAST_MOUNT);
    box = LAST_MOUNT_BOX;
  }
  _state.Set(DS.textAttribute, MOUNT_HIGHLIGHT_ATTRIBUTE);
  PaintLaserMountBox(_state, box);
  return box;
}

// 63E1 and 64AE: the mount box at _box painted in textAttribute, then the one right of it, from LEFT round to FORE,
// highlighted. Returns that box.
std::uint16_t MoveMountBoxRight(GameState& _state, std::uint16_t _box)
{
  PaintLaserMountBox(_state, _box);
  auto box = static_cast<std::uint16_t>(_box + MOUNT_BOX_STEP);
  const auto mount = static_cast<std::uint8_t>(_state.Get(DS.selectedLaserMount) + 1);
  _state.Set(DS.selectedLaserMount, mount);
  if (mount == MOUNT_COUNT)
  {
    _state.Set(DS.selectedLaserMount, 0);
    box = FIRST_MOUNT_BOX;
  }
  _state.Set(DS.textAttribute, MOUNT_HIGHLIGHT_ATTRIBUTE);
  PaintLaserMountBox(_state, box);
  return box;
}

// 6438 and 6505: the mount message at DS:_text on the message line, in the menu's attribute. _jumpedTo, when not 0, is
// where the original jumps back to on the way: 6509, from 653D, the tail the two messages share (ADR-015). The PUSH SI /
// POP SI round it keep the box, which the routine never touches.
PrintedText PrintMountMessage(GameState& _state, Hardware& _hardware, std::uint16_t _text, std::uint16_t _jumpedTo)
{
  if (_jumpedTo != 0)
  {
    _hardware.LoopTurn(_jumpedTo, {});
  }
  _state.Set(DS.textAttribute, MENU_ATTRIBUTE);
  return PrintTextModeString(_state, _text, MESSAGE_OFFSET);
}

// SHR CH,CL with CL the selected mount + 1 and CH laserMountsFitted: whether the mount's bit, which it shifts out into the
// carry, is set.
[[nodiscard]] bool IsMountFitted(const GameState& _state)
{
  return ((_state.Get(DS.laserMountsFitted) >> _state.Get(DS.selectedLaserMount)) & 1) != 0;
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
  const std::uint8_t mount = _state.Get(DS.selectedLaserMount);
  _state.Set(DS.laserMountsFitted, static_cast<std::uint8_t>(_state.Get(DS.laserMountsFitted) | (1u << mount)));
  // 3 and the type, shifted up together as AX to the mount's two bits.
  const auto bits = static_cast<std::uint16_t>(Join(_state.Get(DS.selectedLaserType), MOUNT_TYPE_BITS) << (mount * 2u));
  _state.Set(DS.laserMountTypes, static_cast<std::uint8_t>(_state.Get(DS.laserMountTypes) & ~Low(bits)));
  _state.Set(DS.laserMountTypes, static_cast<std::uint8_t>(_state.Get(DS.laserMountTypes) | High(bits)));
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
  const std::uint8_t mount = _state.Get(DS.selectedLaserMount);
  if (((_state.Get(DS.laserMountTypes) >> (mount * 2u)) & MOUNT_TYPE_BITS) != _state.Get(DS.selectedLaserType))
  {
    PrintMountMessage(_state, _hardware, WRONG_LASER_TYPE_TEXT, REMOVE_MOUNT_MESSAGE);
    return false;
  }
  _state.Set(DS.laserMountsFitted, static_cast<std::uint8_t>(_state.Get(DS.laserMountsFitted) & ~(1u << mount)));
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
// in _start. Each turn to the steering carries the box (SI) and AL, which ReadSteering fires the stick with and GetKey shifts each
// key's Shift into; each turn to a box's move carries the box.
void RunMountChooser(GameState& _state, Hardware& _hardware, const MountChooser& _chooser, MenuLoop _start)
{
  MenuLoop loop = _start;
  for (;;)
  {
    const std::uint8_t roll = ReadSteering(_state, _hardware, loop.al).roll;
    loop.al = roll;
    if (roll != 0)
    {
      _state.Set(DS.textAttribute, HELP_ATTRIBUTE);
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
      _state.Set(DS.textAttribute, HELP_ATTRIBUTE);
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
  _state.Set(DS.hyperspaceCountdown, 0);
  _state.Set(DS.escapePodFrames, ESCAPE_POD_FRAMES);
  const SlotSearch free = FindFreeShipSlot(_state);
  const std::uint16_t slot = free.found ? free.slot : ReclaimShipSlot(_state).slot;
  ClearObjectSlot(_state, slot);
  InitAbandonedCobra(_state, ObjectSlot(_state, slot));
  _state.Set(DS.playerPitchAngle, static_cast<std::uint16_t>(_state.Get(DS.playerPitchAngle) + HALF_TURN));
  _state.Set(DS.viewAngle, HALF_TURN);
  _state.Set(DS.viewLocked, 1);
  _state.Set(DS.playerSpeed, ESCAPE_POD_SPEED);
  _state.Set(DS.velocityDirty, 1);
  _state.Set(DS.escapePodFitted, 0);
  UpdatePlayerVelocity(_state);
  for (std::uint16_t move = 0; move < ESCAPE_POD_MOVES; ++move)
  {
    MoveObjectsByVelocity(_state);
  }
  // MOV [DI],CH, which is 0 throughout: the 17 amounts held emptied.
  for (std::uint16_t kind = 0; kind < CARGO_KINDS; ++kind)
  {
    _state.SetByte(DS.cargoHold.At(kind), 0);
  }
}

Scoop TryScoopObject(GameState& _state, ObjectSlot _slot, const Vector& _view)
{
  std::uint8_t space = _state.Get(DS.largeCargoBayFitted) == 1 ? LARGE_CARGO_BAY_TONNES : CARGO_BAY_TONNES;
  space = static_cast<std::uint8_t>(space - _state.Get(DS.cargoUsedTonnes));
  _state.Set(DS.freeCargoTonnes, space);
  if (!TestScoopBox(_view).inside)
  {
    return Scoop{false, {}};
  }

  const auto type = static_cast<std::uint8_t>((_slot.Get(SlotByte::Type) >> 1) & TYPE_MASK);
  const std::uint8_t flags = _slot.Get(SlotByte::Flags);
  const bool full = _state.Get(DS.freeCargoTonnes) == 0;
  Scoop scoop{true, {}};
  std::uint16_t message = DS.scoopRetrievalInactiveText.offset;
  if (type == TYPE_CARGO_BARREL)
  {
    if ((flags & FLAG_MASKING_DEVICE) != 0)
    {
      scoop.removedBlip = RemoveObject(_state, _slot);
      _state.Set(DS.maskingDeviceRecovered, 1);
      message = DS.maskingDeviceText.offset;
    }
    else if (full)
    {
      message = DS.cargoBayFullText.offset;
    }
    else
    {
      // A random product 0-10, furs for slaves, into the hold, and its name for the message. The divide cannot overflow; the
      // BX its trap would save is y's high byte over the divisor.
      scoop.removedBlip = RemoveObject(_state, _slot);
      const std::uint8_t random = Low(NextRandom(_state));
      const auto y = static_cast<std::uint16_t>(_view.y);
      std::uint8_t product = DivideByte(_state, random, RANDOM_PRODUCT_DIVISOR, Join(High(y), RANDOM_PRODUCT_DIVISOR)).quotient;
      if (product == PRODUCT_SLAVES)
      {
        product = PRODUCT_FURS;
      }
      IncrementByte(_state, DS.cargoHold.At(product));
      _state.Set(DS.cargoUsedTonnes, static_cast<std::uint8_t>(_state.Get(DS.cargoUsedTonnes) + 1));
      const auto name = static_cast<std::uint16_t>(product * PRODUCT_NAME_BYTES + DS.productNames.offset);
      for (std::uint16_t letter = 0; letter < SCOOPED_NAME_BYTES; ++letter)
      {
        _state.SetByte(Offset(DS.scoopedCargoText.offset, letter), _state.Byte(Offset(name, letter)));
      }
      message = DS.scoopedCargoText.offset;
    }
  }
  else if (type == TYPE_SPLINTER && (flags & FLAG_PRECIOUS) != 0)
  {
    // Gems, gold and platinum always; minerals or alloys too if there is room. Gold gets the gems' AL, as the original adds it.
    scoop.removedBlip = RemoveObject(_state, _slot);
    const std::uint8_t gems = Low(NextRandom(_state)) & 7;
    AddPrecious(_state, DS.cargoGemStonesGrams, gems);
    AddPrecious(_state, DS.cargoGoldKg, gems);
    AddPrecious(_state, DS.cargoPlatinumKg, static_cast<std::uint8_t>((High(NextRandom(_state)) & 3) + 1));
    if (_state.Get(DS.freeCargoTonnes) != 0)
    {
      IncrementByte(_state, Low(NextRandom(_state)) >= MINERALS_FROM ? DS.cargoMineralsTonnes.offset : DS.cargoAlloysTonnes.offset);
      _state.Set(DS.cargoUsedTonnes, static_cast<std::uint8_t>(_state.Get(DS.cargoUsedTonnes) + 1));
    }
    message = DS.preciousMetalsText.offset;
  }
  else if (type == TYPE_SPLINTER || type == TYPE_ESCAPE_POD || type == TYPE_THARGON)
  {
    if (full)
    {
      message = DS.cargoBayFullText.offset;
    }
    else
    {
      scoop.removedBlip = RemoveObject(_state, _slot);
      const DataField<std::uint8_t> held = type == TYPE_SPLINTER     ? DS.cargoAlloysTonnes
                                           : type == TYPE_ESCAPE_POD ? DS.cargoSlavesTonnes
                                                                     : DS.cargoAlienItemsTonnes;
      IncrementByte(_state, held.offset);
      _state.Set(DS.cargoUsedTonnes, static_cast<std::uint8_t>(_state.Get(DS.cargoUsedTonnes) + 1));
      message = type == TYPE_SPLINTER     ? DS.metalAlloysText.offset
                : type == TYPE_ESCAPE_POD ? DS.escapePodRetrievedText.offset
                                          : DS.alienItemsText.offset;
    }
  }
  _state.Set(DS.messagePointer, message);
  _state.Set(DS.messageFrames, SCOOP_MESSAGE_FRAMES);
  return scoop;
}

ScreenKey ShowEquipShipScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.equipShipFrame.offset, _backward), TITLE_OFFSET);
  PrintCountedTextLines(_state, DS.equipHelpText.offset, HELP_TEXT_OFFSET);

  // equipmentList: a count, then for each item its tech level, name, government and economy factors and base price. The list is
  // sorted by tech level, so the first item beyond currentTechLevel+1 ends it. Each turn carries the count, the item and the row's
  // place.
  std::uint16_t item = DS.equipmentList.offset;
  std::uint16_t left = _state.Byte(item);
  item = Offset(item, 1);
  const auto techLimit = static_cast<std::uint8_t>(_state.Get(DS.currentTechLevel) + 2);
  _state.Set(DS.menuRowCount, 0);
  std::uint16_t cell = FIRST_ITEM_OFFSET;
  while (_state.Byte(item) < techLimit)
  {
    const auto row = static_cast<std::uint8_t>(_state.Get(DS.menuRowCount) + 1);
    _state.Set(DS.menuRowCount, row);
    // The price: the government's and the economy's factors, each by IMUL, and the base price.
    std::uint16_t factor = Offset(PrintTextModeString(_state, Offset(item, 1), cell).end, 1);
    const std::uint16_t government = SignedProduct(_state.Byte(factor), _state.Get(DS.currentGovernment));
    factor = Offset(factor, 1);
    const std::uint16_t economy = SignedProduct(_state.Byte(factor), _state.Get(DS.currentEconomy));
    factor = Offset(factor, 1);
    const auto price = static_cast<std::uint16_t>(economy + government + _state.Word(factor));
    _state.SetWord(PriceSlot(row), price);
    _state.SetWord(Offset(PriceSlot(row), 2), 0);
    // A resale price only for an item fitted, and never for fuel.
    const bool resold = row != FUEL_ROW && _state.Byte(static_cast<std::uint16_t>(row + FITTED_BEFORE_FUEL)) != 0;
    _state.Set(DS.resalePriceInput, resold ? price : std::uint16_t{0});
    FormatTenths(_state, price);
    PrintTextModeString(_state, DS.priceText.offset, Offset(cell, PRICE_COLUMN));
    const std::uint16_t resale = ComputeResalePrice(_state);
    std::uint16_t resaleText = NO_RESALE_TEXT;
    if (resale != 0)
    {
      _state.SetWord(Offset(PriceSlot(_state.Get(DS.menuRowCount)), 2), resale);
      FormatDecimal5(_state, resale, DS.priceText.offset);
      BlankLeadingZeros(_state, DS.priceText.offset, RESALE_DIGITS_BLANKED);
      // Tenths: the last digit moved right one for the point.
      _state.Set(DS.data8041, _state.Get(DS.data8040));
      _state.Set(DS.data8040, '.');
      resaleText = DS.priceText.offset;
    }
    PrintTextModeString(_state, resaleText, Offset(cell, RESALE_COLUMN));
    cell = Offset(cell, ROW_BYTES);
    item = Offset(factor, 2);
    if (--left == 0)
    {
      break;
    }
    _hardware.LoopTurn(EQUIPMENT_LIST_ROW, {left, item, cell});
  }
  return RunEquipShipMenu(_state, _hardware, EQUIP_MENU_FIRST_ROW);
}

bool SelectLaserType(GameState& _state)
{
  const std::uint8_t row = _state.Get(DS.menuSelectedRow);
  std::uint8_t type = 0;
  _state.Set(DS.selectedLaserType, type);
  for (;;)
  {
    const bool laser = row == LASER_ROWS[type];
    if (laser || type + 1u == LASER_ROWS.size())
    {
      return laser;
    }
    ++type;
    _state.Set(DS.selectedLaserType, type);
  }
}

ScreenKey RunEquipShipMenu(GameState& _state, Hardware& _hardware, std::uint16_t _firstRow)
{
  _state.Set(DS.menuFirstRowAttr, _firstRow);
  // StartMenu's last ToggleMenuRowHighlight leaves AL the attribute it toggled the row to, which it left in textAttribute.
  MenuLoop loop{StartMenu(_state).row, 0};
  loop.al = _state.Get(DS.textAttribute);
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
        loop.al = _state.Get(DS.textAttribute);
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

PrintedText DrawLaserMountMenu(GameState& _state)
{
  _state.Set(DS.textAttribute, HELP_ATTRIBUTE);
  PrintCountedTextLines(_state, MOUNT_HEADER_TEXT, MOUNT_HEADER_OFFSET);
  // Each mount's laser, or Free, one after the other: its bit of laserMountsFitted, its two bits of laserMountTypes.
  const std::uint8_t types = _state.Get(DS.laserMountTypes);
  const std::uint8_t fitted = _state.Get(DS.laserMountsFitted);
  PrintedText printed{FREE_MOUNT_TEXT, MOUNT_NAMES_OFFSET};
  for (std::uint16_t mount = 0; mount < MOUNT_COUNT; ++mount)
  {
    std::uint16_t name = FREE_MOUNT_TEXT;
    if (((fitted >> mount) & 1) != 0)
    {
      name = _state.Word(MountLaserName(types, mount));
    }
    printed = PrintTextModeString(_state, name, printed.nextCell);
  }
  _state.Set(DS.textAttribute, MOUNT_HIGHLIGHT_ATTRIBUTE);
  PaintLaserMountBox(_state, FIRST_MOUNT_BOX);
  _state.Set(DS.selectedLaserMount, 0);
  return printed;
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
  _state.Set(DS.textAttribute, HELP_ATTRIBUTE);
  PrintTextModeString(_state, SELL_LASER_TEXT, SELL_LASER_OFFSET);
  // SI, pushed and popped round the print, on FORE's box; AL the print's NUL.
  RunMountChooser(_state, _hardware, REMOVE_CHOOSER, MenuLoop{FIRST_MOUNT_BOX, 0});
}

PrintedLines RedrawEquipHelpText(GameState& _state)
{
  _state.Set(DS.textAttribute, HELP_ATTRIBUTE);
  const PrintedLines printed = PrintCountedTextLines(_state, DS.equipHelpText.offset, HELP_TEXT_OFFSET);
  _state.Set(DS.textAttribute, MENU_ATTRIBUTE);
  return printed;
}

void PaintLaserMountBox(GameState& _state, std::uint16_t _box)
{
  const std::uint8_t attribute = _state.Get(DS.textAttribute);
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

EquipmentPayment PayForEquipmentItem(GameState& _state, std::uint16_t _bx)
{
  const std::uint8_t row = _state.Get(DS.menuSelectedRow);
  std::uint16_t tenths = 0;
  if (row == FUEL_ROW)
  {
    // What fills the tank: (255-fuel) * the price's low byte / 36, at least 1, by a DIV that can overflow into the game's trap,
    // which saves BX with the row in BL.
    const auto missing = static_cast<std::uint8_t>(~_state.Get(DS.fuel));
    const auto product = static_cast<std::uint16_t>(missing * Low(_state.Get(DS.data823F)));
    std::uint8_t units = DivideByte(_state, product, FUEL_UNITS_PER_TENTH, WithLow(_bx, row)).quotient;
    if (units == 0)
    {
      units = 1;
    }
    tenths = units;
  }
  else
  {
    tenths = _state.Word(PriceSlot(row));
  }
  return EquipmentPayment{SubtractCredits(_state, tenths), tenths};
}

PrintedText ClearEquipmentSellPrice(GameState& _state)
{
  return PrintEquipmentSellColumn(_state, NO_RESALE_TEXT);
}

PrintedText ShowEquipmentSellPrice(GameState& _state)
{
  // The price's slot in screenPrices, and the resale price after it. The original keeps the slot in BX on the stack
  // round ComputeResalePrice.
  const std::uint16_t slot = PriceSlot(_state.Get(DS.menuSelectedRow));
  _state.Set(DS.resalePriceInput, _state.Word(slot));
  const std::uint16_t resale = ComputeResalePrice(_state);
  _state.SetWord(Offset(slot, 2), resale);
  FormatTenths(_state, resale);
  return PrintEquipmentSellColumn(_state, DS.priceText.offset);
}

MenuCursor StartMenu(GameState& _state)
{
  SwapTextAttributeNibbles(_state);
  const PrintedText credits = PrintCreditsOnMessageLine(_state);
  SwapTextAttributeNibbles(_state);
  const std::uint16_t row = _state.Get(DS.menuFirstRowAttr);
  _state.Set(DS.menuSelectedRow, 1);
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, row);
  return MenuCursor{row, credits};
}

MenuCursor MoveMenuCursorUp(GameState& _state, std::uint16_t _row)
{
  const PrintedText credits = PrintCreditsOnMessageLine(_state);
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, _row);
  std::uint16_t row = _row;
  const auto selected = static_cast<std::uint8_t>(_state.Get(DS.menuSelectedRow) - 1);
  _state.Set(DS.menuSelectedRow, selected);
  if (selected == 0)
  {
    // To the last row: count*80 bytes on, as count*256/4 and that /4 again.
    row = _state.Get(DS.menuFirstRowAttr);
    const std::uint8_t count = _state.Get(DS.menuRowCount);
    _state.Set(DS.menuSelectedRow, count);
    const std::uint16_t quarter = MenuRowsQuarter(count);
    row = Offset(Offset(row, quarter), static_cast<std::uint16_t>(quarter >> 2));
  }
  row = static_cast<std::uint16_t>(row - ROW_BYTES);
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, row);
  return MenuCursor{row, credits};
}

MenuCursor MoveMenuCursorDown(GameState& _state, std::uint16_t _row)
{
  const PrintedText credits = PrintCreditsOnMessageLine(_state);
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, _row);
  std::uint16_t row = _row;
  const auto selected = static_cast<std::uint8_t>(_state.Get(DS.menuSelectedRow) + 1);
  _state.Set(DS.menuSelectedRow, selected);
  if (_state.Get(DS.menuRowCount) < selected)
  {
    row = static_cast<std::uint16_t>(_state.Get(DS.menuFirstRowAttr) - ROW_BYTES);
    _state.Set(DS.menuSelectedRow, 1);
  }
  row = static_cast<std::uint16_t>(row + ROW_BYTES);
  ToggleMenuRowHighlight(_state, GameState::VIDEO_SEGMENT, row);
  return MenuCursor{row, credits};
}

bool IsScreenKey(std::uint8_t _key) noexcept
{
  return _key == SCAN_ESCAPE || (_key >= SCAN_F1 && _key < SCAN_PAST_F10);
}

MenuLoop SteerMenuCursor(GameState& _state, Hardware& _hardware, MenuLoop _loop)
{
  const Steering steering = ReadSteering(_state, _hardware, _loop.al);
  // NEG AH: the pitch negated, and up when that is negative.
  const std::uint8_t pitch = Negate(steering.pitch);
  if (pitch == 0)
  {
    return MenuLoop{_loop.row, steering.roll};
  }
  const MenuCursor cursor = (pitch & 0x80) != 0 ? MoveMenuCursorUp(_state, _loop.row) : MoveMenuCursorDown(_state, _loop.row);
  return MenuLoop{cursor.row, _state.Get(DS.textAttribute)};
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

// ── The entries of the de-assembled routines ──

namespace
{

using Machine::NativeContract;
using Machine::NativeReturn;
using Machine::NativeWait;
using Machine::REGISTER_ALL;

// Symbols.tsv's "clobbers all", but for DS, which the original keeps and its caller goes on with.
constexpr NativeContract CLOBBERS_ALL_BUT_DS{static_cast<std::uint16_t>(REGISTER_ALL & ~Machine::REGISTER_DS), 0};
constexpr NativeContract RETURNS_ZERO{0, FLAG_ZERO};
constexpr NativeContract RETURNS_CARRY{0, FLAG_CARRY};
// The menus' and the equipment screen's: all but AX, the closing key in AH, and DS.
constexpr NativeContract SHOWS_SCREEN{static_cast<std::uint16_t>(REGISTER_ALL & ~Machine::REGISTER_AX & ~Machine::REGISTER_DS), 0};

} // namespace

void LaunchEscapePodEntry(Guest& _guest)
{
  LaunchEscapePod(_guest.State());
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
}

void TryScoopObjectEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const std::uint16_t x = regs.ax;
  const std::uint16_t y = regs.bx;
  const std::uint16_t z = regs.cx;
  const Vector view{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(z)};
  const ObjectSlot slot(_guest.State(), regs.di);
  const Scoop scoop = TryScoopObject(_guest.State(), slot, view);
  // The contract keeps every register, so the entry leaves what the original does: DX with the free tonnes in DL, then what the
  // box's test measured last, and once RemoveObject erases a blip, EraseScannerBlip's DX and ES. AX, BX and CX are pushed and
  // popped round the scoop.
  regs.dx = TestScoopBox(view).lastMeasure.value_or(WithLow(regs.dx, _guest.Get(DS.freeCargoTonnes)));
  if (scoop.removedBlip)
  {
    EraseScannerBlipOut(_guest, slot, scoop.removedBlip);
    regs.ax = x;
    regs.bx = y;
    regs.cx = z;
  }
  _guest.Clobber(PRESERVES_ALL);
}

void PayForEquipmentItemEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const EquipmentPayment payment = PayForEquipmentItem(_guest.State(), regs.bx);
  // AX the price and BX 0, as SubtractCredits takes them, and for fuel DX the price word with the divisor in DL; SI where
  // FormatCredits leaves it once they are paid.
  if (_guest.Get(DS.menuSelectedRow) == FUEL_ROW)
  {
    regs.dx = WithLow(_guest.Get(DS.data823F), FUEL_UNITS_PER_TENTH);
  }
  regs.ax = payment.tenths;
  regs.bx = 0;
  if (payment.paid)
  {
    regs.si = DS.creditBalanceText.offset;
  }
  _guest.SetFlag(FLAG_CARRY, !payment.paid);
  _guest.Clobber(RETURNS_CARRY);
}

void SelectLaserTypeEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_ZERO, SelectLaserType(_guest.State()));
  _guest.Clobber(RETURNS_ZERO);
}

void DrawLaserMountMenuEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const PrintedText names = DrawLaserMountMenu(_guest.State());
  // The contract keeps every register, so the entry leaves what the original does: the last PrintTextModeString's DI
  // and ES, and AH the attribute it printed in; BX the place of the last fitted mount's name, when one is; DH what the
  // four SHR DH,1 leave of laserMountsFitted; and PaintLaserMountBox's AL, the highlight, DL and CX counted down to 0,
  // and SI on FORE's box.
  const std::uint8_t types = _guest.Get(DS.laserMountTypes);
  const std::uint8_t fitted = _guest.Get(DS.laserMountsFitted);
  for (std::uint16_t mount = 0; mount < MOUNT_COUNT; ++mount)
  {
    if (((fitted >> mount) & 1) != 0)
    {
      regs.bx = MountLaserName(types, mount);
    }
  }
  regs.di = names.nextCell;
  regs.es = Guest::VIDEO_SEGMENT;
  regs.ax = Join(HELP_ATTRIBUTE, MOUNT_HIGHLIGHT_ATTRIBUTE);
  regs.dx = Join(static_cast<std::uint8_t>(fitted >> MOUNT_COUNT), 0);
  regs.cx = 0;
  regs.si = FIRST_MOUNT_BOX;
  _guest.Clobber(PRESERVES_ALL);
}

void RedrawEquipHelpTextEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const PrintedLines printed = RedrawEquipHelpText(_guest.State());
  regs.si = printed.end;
  regs.di = printed.nextLine;
  regs.es = Guest::VIDEO_SEGMENT;
  // PrintCountedTextLines' AX, the help's attribute with the NUL in AL, and its LOOP's CX = 0.
  regs.ax = Join(HELP_ATTRIBUTE, 0);
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

void PaintLaserMountBoxEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  PaintLaserMountBox(_guest.State(), regs.si);
  // The contract keeps every register, so the entry leaves what the original's loops do: AL the attribute, DL and CX
  // counted down to 0. SI comes back as it was.
  SetLow(regs.ax, _guest.Get(DS.textAttribute));
  SetLow(regs.dx, 0);
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

void ClearEquipmentSellPriceEntry(Guest& _guest)
{
  SellColumnOut(_guest.Regs(), ClearEquipmentSellPrice(_guest.State()));
  _guest.Clobber(PRESERVES_ALL);
}

void ShowEquipmentSellPriceEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const PrintedText printed = ShowEquipmentSellPrice(_guest.State());
  // BX the price's slot, and CX as FormatTenths' BlankLeadingZeros leaves it; the print's SI, DI, ES and AX.
  regs.bx = PriceSlot(_guest.Get(DS.menuSelectedRow));
  regs.cx = BlankedLeadingZeros(_guest.State(), DS.priceText.offset, TENTHS_DIGITS_BLANKED).triesLeft;
  SellColumnOut(regs, printed);
  _guest.Clobber(PRESERVES_ALL);
}

void ShowEquipShipScreenEntry(Guest& _guest)
{
  const ScreenKey key = ShowEquipShipScreen(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Regs().ax = Join(key.scanCode, key.al);
  _guest.Clobber(SHOWS_SCREEN);
}

void RunEquipShipMenuEntry(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const ScreenKey key = RunEquipShipMenu(_guest.State(), _guest.Devices(), regs.si);
  regs.ax = Join(key.scanCode, key.al);
  _guest.Clobber(SHOWS_SCREEN);
}

void ChooseMountToFitLaserEntry(Guest& _guest)
{
  ChooseMountToFitLaser(_guest.State(), _guest.Devices());
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
}

void ChooseMountToRemoveLaserEntry(Guest& _guest)
{
  ChooseMountToRemoveLaser(_guest.State(), _guest.Devices());
  _guest.Clobber(CLOBBERS_ALL_BUT_DS);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x2F0F, "LaunchEscapePod", &LaunchEscapePodEntry, CLOBBERS_ALL_BUT_DS},
  NativeEntry{0x4401, "TryScoopObject", &TryScoopObjectEntry, PRESERVES_ALL},
  NativeEntry{0x5BF2, "ShowEquipShipScreen", &ShowEquipShipScreenEntry, SHOWS_SCREEN, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x6111, "RunEquipShipMenu", &RunEquipShipMenuEntry, SHOWS_SCREEN, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x633B, "SelectLaserType", &SelectLaserTypeEntry, RETURNS_ZERO},
  NativeEntry{0x6367, "DrawLaserMountMenu", &DrawLaserMountMenuEntry, PRESERVES_ALL},
  NativeEntry{0x63B2, "ChooseMountToFitLaser", &ChooseMountToFitLaserEntry, CLOBBERS_ALL_BUT_DS, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x646F, "ChooseMountToRemoveLaser", &ChooseMountToRemoveLaserEntry, CLOBBERS_ALL_BUT_DS, NativeReturn::Near, 0,
              NativeWait::Always},
  NativeEntry{0x653F, "RedrawEquipHelpText", &RedrawEquipHelpTextEntry, PRESERVES_ALL},
  NativeEntry{0x6564, "PaintLaserMountBox", &PaintLaserMountBoxEntry, PRESERVES_ALL},
  NativeEntry{0x65A3, "PayForEquipmentItem", &PayForEquipmentItemEntry, RETURNS_CARRY},
  NativeEntry{0x6946, "ClearEquipmentSellPrice", &ClearEquipmentSellPriceEntry, PRESERVES_ALL},
  NativeEntry{0x6972, "ShowEquipmentSellPrice", &ShowEquipmentSellPriceEntry, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> EquipmentEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
