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

namespace Elite
{

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_ZERO;
using Machine::Registers;

// The routines these call by their entries: whatever is hooked there runs, so each work routine that a routine that
// waits calls is compared on its own (ADR-010 item 8).
constexpr std::uint16_t FORMAT_DECIMAL5 = 0x3407;
constexpr std::uint16_t BLANK_LEADING_ZEROS = 0x3432;
constexpr std::uint16_t PRINT_TEXT_MODE_STRING = 0x60D2;
constexpr std::uint16_t RUN_EQUIP_SHIP_MENU = 0x6111;
constexpr std::uint16_t SELECT_LASER_TYPE = 0x633B;
constexpr std::uint16_t DRAW_LASER_MOUNT_MENU = 0x6367;
constexpr std::uint16_t CHOOSE_MOUNT_TO_FIT_LASER = 0x63B2;
constexpr std::uint16_t CHOOSE_MOUNT_TO_REMOVE_LASER = 0x646F;
constexpr std::uint16_t CLEAR_DOCKED_MESSAGE_LINE = 0x6553;
constexpr std::uint16_t PRINT_CREDITS_ON_MESSAGE_LINE = 0x658F;
constexpr std::uint16_t PAY_FOR_EQUIPMENT_ITEM = 0x65A3;
constexpr std::uint16_t ADD_CREDITS = 0x65EE;
constexpr std::uint16_t PRINT_COUNTED_TEXT_LINES = 0x65FA;
constexpr std::uint16_t CLEAR_EQUIPMENT_SELL_PRICE = 0x6946;
constexpr std::uint16_t SHOW_EQUIPMENT_SELL_PRICE = 0x6972;
constexpr std::uint16_t COMPUTE_RESALE_PRICE = 0x6995;
constexpr std::uint16_t FORMAT_TENTHS = 0x69B3;
constexpr std::uint16_t READ_STEERING = 0x7536;
constexpr std::uint16_t GET_KEY = 0x7616;
constexpr std::uint16_t WAIT_FOR_TIMER_TICK = 0x7772;
constexpr std::uint16_t DRAW_DOCKED_FRAME = 0x7C88;

// Where the original's backward jumps land, for the turns of its loops (JumpBack).
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

// What StartMenu and the cursor's moves leave in the registers: SI on the row, PrintCreditsOnMessageLine's DI and ES,
// and AX as the last ToggleMenuRowHighlight leaves it, AL the attribute it toggled the row to and AH the one the
// credits were printed in, its nibbles swapped.
void MenuCursorOut(Registers& _regs, const GameState& _state, MenuCursor _cursor)
{
  const std::uint8_t attribute = _state.Get(DS.textAttribute);
  _regs.si = _cursor.row;
  _regs.di = _cursor.credits.nextCell;
  _regs.es = Guest::VIDEO_SEGMENT;
  _regs.ax = Join(static_cast<std::uint8_t>((attribute >> 4) | (attribute << 4)), attribute);
}

// 61CF and 628A: B and S save CX, SI and textAttribute, for 6214 to put back after the message.
void OpenEquipMessage(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Push(regs.cx);
  _guest.Push(regs.si);
  regs.ax = WithLow(regs.ax, _guest.Get(DS.textAttribute));
  _guest.Push(regs.ax);
  _guest.Set(DS.textAttribute, MENU_ATTRIBUTE);
}

// 6214: the message at SI on the message line, and what OpenEquipMessage saved put back.
void ShowEquipMessage(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.di = MESSAGE_OFFSET;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.ax = _guest.Pop();
  _guest.Set(DS.textAttribute, Low(regs.ax));
  regs.si = _guest.Pop();
  regs.cx = _guest.Pop();
}

// 6241, reached by a jump back: NOT ENOUGH CREDITS!, and the jump back to 6214.
void ReportNotEnoughCredits(Guest& _guest)
{
  _guest.JumpBack(EQUIP_NOT_ENOUGH_CREDITS);
  _guest.Regs().si = NOT_ENOUGH_CREDITS_TEXT;
  _guest.JumpBack(EQUIP_MESSAGE);
}

// 6228: an item or a laser paid for and counted, its resale price shown, and a laser's mount chosen. BX is the
// row's fitted count.
void BuyFittedItem(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Push(regs.bx);
  _guest.Call(PAY_FOR_EQUIPMENT_ITEM);
  regs.bx = _guest.Pop();
  if (_guest.Flag(FLAG_CARRY))
  {
    regs.si = NOT_ENOUGH_CREDITS_TEXT;
    _guest.JumpBack(EQUIP_MESSAGE);
    return;
  }
  IncrementByte(_guest.State(), regs.bx);
  _guest.Call(SHOW_EQUIPMENT_SELL_PRICE);
  _guest.Call(SELECT_LASER_TYPE);
  if (_guest.Flag(FLAG_ZERO))
  {
    _guest.Call(CHOOSE_MOUNT_TO_FIT_LASER);
  }
  regs.si = ITEM_PURCHASED_TEXT;
  _guest.JumpBack(EQUIP_MESSAGE);
}

// 6246: B on fuel (a full tank, never in mission 1) or a missile (up to four). BL is the row.
void BuyFuelOrMissile(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) - 1));
  if (Low(regs.bx) != 0)
  {
    regs.si = FOUR_MISSILES_ONLY_TEXT;
    if (_guest.Get(DS.missileCount) == MISSILE_LIMIT)
    {
      _guest.JumpBack(EQUIP_MESSAGE);
      return;
    }
    _guest.Call(PAY_FOR_EQUIPMENT_ITEM);
    if (_guest.Flag(FLAG_CARRY))
    {
      ReportNotEnoughCredits(_guest);
      return;
    }
    _guest.Set(DS.missileCount, static_cast<std::uint8_t>(_guest.Get(DS.missileCount) + 1));
    _guest.Call(SHOW_EQUIPMENT_SELL_PRICE);
    regs.si = MISSILE_PURCHASED_TEXT;
    _guest.JumpBack(EQUIP_MESSAGE);
    return;
  }
  if (_guest.Get(DS.missionNumber) == NO_FUEL_MISSION)
  {
    regs.si = NO_FUEL_AVAILABLE_TEXT;
    _guest.JumpBack(EQUIP_MESSAGE);
    return;
  }
  regs.si = FUEL_FULL_TEXT;
  if (_guest.Get(DS.fuel) >= FUEL_NEARLY_FULL)
  {
    _guest.JumpBack(EQUIP_MESSAGE);
    return;
  }
  _guest.Call(PAY_FOR_EQUIPMENT_ITEM);
  if (_guest.Flag(FLAG_CARRY))
  {
    ReportNotEnoughCredits(_guest);
    return;
  }
  regs.si = FUEL_PURCHASED_TEXT;
  _guest.Set(DS.fuel, FUEL_FULL);
  _guest.JumpBack(EQUIP_MESSAGE);
}

// 61CF: B on the equipment menu. Leaves SI at the message for 6214, after the jumps back the original takes on the way.
void BuyEquipment(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  OpenEquipMessage(_guest);
  regs.bx = _guest.Get(DS.menuSelectedRow);
  if (Low(regs.bx) < FIRST_FITTED_ROW)
  {
    BuyFuelOrMissile(_guest);
    return;
  }
  regs.bx = static_cast<std::uint16_t>(regs.bx + FITTED_BEFORE_FUEL);
  if (_guest.Byte(regs.bx) != 0)
  {
    _guest.Call(SELECT_LASER_TYPE);
    if (!_guest.Flag(FLAG_ZERO))
    {
      regs.si = ALREADY_FITTED_TEXT;
      return;
    }
  }
  else
  {
    _guest.Call(SELECT_LASER_TYPE);
    if (!_guest.Flag(FLAG_ZERO))
    {
      BuyFittedItem(_guest);
      return;
    }
    _guest.JumpBack(EQUIP_LASER_CHECKS);
  }
  // A laser: a free mount, and a mining laser only with fuel scoops.
  regs.si = FOUR_LASERS_TEXT;
  if (_guest.Get(DS.laserMountsFitted) == ALL_MOUNTS)
  {
    return;
  }
  if (_guest.Get(DS.menuSelectedRow) == MINING_LASER_ROW && _guest.Get(DS.fuelScoopsFitted) != 1)
  {
    regs.si = SCOOP_NEEDED_TEXT;
    return;
  }
  BuyFittedItem(_guest);
}

// 62FD: S on fuel (never) or a missile, for the missile's resale price. BL is the row.
void SellFuelOrMissile(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) - 1));
  if (Low(regs.bx) == 0)
  {
    regs.si = FUEL_SALE_ILLEGAL_TEXT;
    _guest.JumpBack(EQUIP_MESSAGE);
    return;
  }
  regs.si = NO_MISSILE_TO_SELL_TEXT;
  if (_guest.Get(DS.missileCount) == 0)
  {
    _guest.JumpBack(EQUIP_SOLD_MESSAGE);
    _guest.JumpBack(EQUIP_MESSAGE);
    return;
  }
  const auto left = static_cast<std::uint8_t>(_guest.Get(DS.missileCount) - 1);
  _guest.Set(DS.missileCount, left);
  if (left == 0)
  {
    _guest.Call(CLEAR_EQUIPMENT_SELL_PRICE);
  }
  regs.ax = _guest.Get(DS.data8245);
  regs.bx = 0;
  _guest.Call(ADD_CREDITS);
  regs.si = MISSILE_SOLD_TEXT;
  _guest.JumpBack(EQUIP_MESSAGE);
}

// 628A: S on the equipment menu, for the resale price. Leaves SI at the message for 6214, after the jumps back the
// original takes on the way.
void SellEquipment(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  OpenEquipMessage(_guest);
  regs.bx = _guest.Get(DS.menuSelectedRow);
  const std::uint8_t row = Low(regs.bx);
  if (row < FIRST_FITTED_ROW)
  {
    SellFuelOrMissile(_guest);
    return;
  }
  if (row == CARGO_BAY_ROW && _guest.Get(DS.cargoUsedTonnes) >= CARGO_BAY_SALE_TONNES)
  {
    regs.si = TOO_MUCH_CARGO_TEXT;
    _guest.JumpBack(EQUIP_MESSAGE);
    return;
  }
  regs.bx = static_cast<std::uint16_t>(regs.bx + FITTED_BEFORE_FUEL);
  if (_guest.Byte(regs.bx) == 0)
  {
    regs.si = NO_ITEM_TO_SELL_TEXT;
    _guest.JumpBack(EQUIP_MESSAGE);
    return;
  }
  if (_guest.Get(DS.menuSelectedRow) == FUEL_SCOOPS_ROW && _guest.Get(DS.miningLaserCount) != 0)
  {
    regs.si = SELL_MINING_LASER_TEXT;
    _guest.JumpBack(EQUIP_MESSAGE);
    return;
  }
  const auto left = static_cast<std::uint8_t>(_guest.Byte(regs.bx) - 1);
  _guest.SetByte(regs.bx, left);
  if (left == 0)
  {
    _guest.Call(CLEAR_EQUIPMENT_SELL_PRICE);
  }
  regs.bx = static_cast<std::uint16_t>(PriceSlot(_guest.Get(DS.menuSelectedRow)) + 2);
  regs.ax = _guest.Word(regs.bx);
  regs.bx = 0;
  _guest.Call(ADD_CREDITS);
  _guest.Call(SELECT_LASER_TYPE);
  if (_guest.Flag(FLAG_ZERO))
  {
    _guest.Call(CHOOSE_MOUNT_TO_REMOVE_LASER);
  }
  regs.si = ITEM_SOLD_TEXT;
  _guest.JumpBack(EQUIP_MESSAGE);
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

// What the last PaintLaserMountBox leaves in the registers: SI on _box, AL the attribute it painted, and DL and CX
// counted down to 0.
void MountBoxOut(Registers& _regs, const GameState& _state, std::uint16_t _box)
{
  _regs.si = _box;
  SetLow(_regs.ax, _state.Get(DS.textAttribute));
  SetLow(_regs.dx, 0);
  _regs.cx = 0;
}

// The mount box moved from the one at SI, and the registers as the original leaves them.
void MoveMountBoxLeftOnRegisters(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  MountBoxOut(regs, _guest.State(), MoveMountBoxLeft(_guest.State(), regs.si));
}

void MoveMountBoxRightOnRegisters(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  MountBoxOut(regs, _guest.State(), MoveMountBoxRight(_guest.State(), regs.si));
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

// What PrintMountMessage leaves in the registers: SI kept, and PrintTextModeString's DI, ES and AX, the attribute it
// printed in with the NUL in AL.
void MountMessageOut(Registers& _regs, PrintedText _printed) noexcept
{
  _regs.di = _printed.nextCell;
  _regs.es = Guest::VIDEO_SEGMENT;
  _regs.ax = Join(MENU_ATTRIBUTE, 0);
}

// What SHR CH,CL leaves, with CL the selected mount + 1 and CH laserMountsFitted.
struct MountBit
{
  bool fitted;              ///< the mount's bit of laserMountsFitted, which the shift leaves in the carry
  std::uint8_t shiftCount;  ///< CL: the selected mount + 1
  std::uint8_t mountsAbove; ///< CH: laserMountsFitted shifted past the mount's bit
};

[[nodiscard]] MountBit ShiftOutMountBit(const GameState& _state)
{
  const std::uint8_t mount = _state.Get(DS.selectedLaserMount);
  const std::uint8_t fitted = _state.Get(DS.laserMountsFitted);
  return MountBit{((fitted >> mount) & 1) != 0, static_cast<std::uint8_t>(mount + 1), static_cast<std::uint8_t>(fitted >> (mount + 1u))};
}

// What Space did on a mount chooser's mount (642A and 64F7).
struct MountSpace
{
  bool done;           ///< the laser is fitted or removed, and the help text redrawn over the mount menu
  PrintedText message; ///< when it is not, where the message saying why stopped
  PrintedLines help;   ///< when it is, where RedrawEquipHelpText stopped
};

// 642A: Space in ChooseMountToFitLaser. A free mount gets the bought laser: its bit of laserMountsFitted set, and its two bits
// of laserMountTypes cleared and then set to the type.
MountSpace FitLaserOnMount(GameState& _state, Hardware& _hardware)
{
  if (ShiftOutMountBit(_state).fitted)
  {
    return MountSpace{false, PrintMountMessage(_state, _hardware, MOUNT_OCCUPIED_TEXT, 0), {}};
  }
  const std::uint8_t mount = _state.Get(DS.selectedLaserMount);
  _state.Set(DS.laserMountsFitted, static_cast<std::uint8_t>(_state.Get(DS.laserMountsFitted) | (1u << mount)));
  // 3 and the type, shifted up together as AX to the mount's two bits.
  const auto bits = static_cast<std::uint16_t>(Join(_state.Get(DS.selectedLaserType), MOUNT_TYPE_BITS) << (mount * 2u));
  _state.Set(DS.laserMountTypes, static_cast<std::uint8_t>(_state.Get(DS.laserMountTypes) & ~Low(bits)));
  _state.Set(DS.laserMountTypes, static_cast<std::uint8_t>(_state.Get(DS.laserMountTypes) | High(bits)));
  return MountSpace{true, {}, RedrawEquipHelpText(_state)};
}

// 64F7: Space in ChooseMountToRemoveLaser. A mount holding the sold laser's type is freed: its bit of laserMountsFitted
// cleared. "Wrong laser type!" jumps back into the message code the other message runs on into (653D to 6509).
MountSpace RemoveLaserFromMount(GameState& _state, Hardware& _hardware)
{
  if (!ShiftOutMountBit(_state).fitted)
  {
    return MountSpace{false, PrintMountMessage(_state, _hardware, NO_LASER_ON_MOUNT_TEXT, 0), {}};
  }
  const std::uint8_t mount = _state.Get(DS.selectedLaserMount);
  if (((_state.Get(DS.laserMountTypes) >> (mount * 2u)) & MOUNT_TYPE_BITS) != _state.Get(DS.selectedLaserType))
  {
    return MountSpace{false, PrintMountMessage(_state, _hardware, WRONG_LASER_TYPE_TEXT, REMOVE_MOUNT_MESSAGE), {}};
  }
  _state.Set(DS.laserMountsFitted, static_cast<std::uint8_t>(_state.Get(DS.laserMountsFitted) & ~(1u << mount)));
  return MountSpace{true, {}, RedrawEquipHelpText(_state)};
}

// What Space leaves in the registers for the chooser's loop: when the laser is fitted or removed, RedrawEquipHelpText's
// print, AX the help's attribute with the NUL in AL, and its LOOP's CX = 0; otherwise _count, CX as the shifts leave it, and
// the message's DI, ES and AX, SI kept.
void MountSpaceOut(Registers& _regs, const MountSpace& _space, std::uint16_t _count)
{
  if (_space.done)
  {
    _regs.si = _space.help.end;
    _regs.di = _space.help.nextLine;
    _regs.es = Guest::VIDEO_SEGMENT;
    _regs.ax = Join(HELP_ATTRIBUTE, 0);
    _regs.cx = 0;
    return;
  }
  _regs.cx = _count;
  MountMessageOut(_regs, _space.message);
}

// FitLaserOnMount, and the registers as the original leaves them: CL the selected mount + 1 and CH what SHR CH,CL left.
bool FitLaserOnMountOnRegisters(Guest& _guest)
{
  const MountBit bit = ShiftOutMountBit(_guest.State());
  const MountSpace space = FitLaserOnMount(_guest.State(), _guest.Devices());
  MountSpaceOut(_guest.Regs(), space, Join(bit.mountsAbove, bit.shiftCount));
  return space.done;
}

// RemoveLaserFromMount, and the registers as the original leaves them: CL as FitLaserOnMountOnRegisters leaves it, or, once
// the mount is found fitted with the wrong laser, its two bits' shift.
bool RemoveLaserFromMountOnRegisters(Guest& _guest)
{
  const MountBit bit = ShiftOutMountBit(_guest.State());
  const MountSpace space = RemoveLaserFromMount(_guest.State(), _guest.Devices());
  const std::uint8_t count = bit.fitted ? static_cast<std::uint8_t>((bit.shiftCount - 1) << 1) : bit.shiftCount;
  MountSpaceOut(_guest.Regs(), space, Join(bit.mountsAbove, count));
  return space.done;
}

// The two mount choosers are one loop at two addresses: where each backward jump lands, and what Space does.
struct MountChooser
{
  std::uint16_t steer;    // CALL ReadSteering, where every other key goes back to
  std::uint16_t left;     // the box moved left
  std::uint16_t right;    // the box moved right
  std::uint16_t tickLoop; // the poll's LOOP
  bool (*space)(Guest&);  // true once the laser is fitted or removed
};

constexpr MountChooser FIT_CHOOSER{0x63B5, 0x63C3, 0x63E1, 0x6405, &FitLaserOnMountOnRegisters};
constexpr MountChooser REMOVE_CHOOSER{0x6482, 0x6490, 0x64AE, 0x64D2, &RemoveLaserFromMountOnRegisters};

// 63B5-6448 and 6482-6515: the box moved by the steering or the arrow keys until Space does its work.
void RunMountChooser(Guest& _guest, const MountChooser& _chooser)
{
  Registers& regs = _guest.Regs();
  for (;;)
  {
    _guest.Call(READ_STEERING);
    const std::uint8_t roll = Low(regs.ax);
    if (roll != 0)
    {
      _guest.Set(DS.textAttribute, HELP_ATTRIBUTE);
      if ((roll & 0x80) != 0)
      {
        MoveMountBoxLeftOnRegisters(_guest);
      }
      else
      {
        MoveMountBoxRightOnRegisters(_guest);
      }
    }
    for (;;)
    {
      if (!PollMenuKey(_guest, _chooser.tickLoop))
      {
        _guest.JumpBack(_chooser.steer);
        break;
      }
      _guest.Push(regs.ax);
      _guest.Call(CLEAR_DOCKED_MESSAGE_LINE);
      regs.ax = _guest.Pop();
      const std::uint8_t key = High(regs.ax);
      if (key == SCAN_SPACE)
      {
        if (_chooser.space(_guest))
        {
          return;
        }
        _guest.JumpBack(_chooser.steer);
        break;
      }
      _guest.Set(DS.textAttribute, HELP_ATTRIBUTE);
      if (key == SCAN_LEFT)
      {
        _guest.JumpBack(_chooser.left);
        MoveMountBoxLeftOnRegisters(_guest);
        continue;
      }
      if (key == SCAN_RIGHT)
      {
        _guest.JumpBack(_chooser.right);
        MoveMountBoxRightOnRegisters(_guest);
        continue;
      }
      _guest.JumpBack(_chooser.steer);
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

void ShowEquipShipScreen(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.si = DS.equipShipFrame.offset;
  _guest.Call(DRAW_DOCKED_FRAME);
  regs.di = TITLE_OFFSET;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = DS.equipHelpText.offset;
  regs.di = HELP_TEXT_OFFSET;
  _guest.Call(PRINT_COUNTED_TEXT_LINES);

  // equipmentList: a count, then for each item its tech level, name, government and economy factors and base price.
  // The list is sorted by tech level, so the first item beyond currentTechLevel+1 ends it.
  regs.si = DS.equipmentList.offset;
  regs.cx = _guest.Byte(regs.si);
  ++regs.si;
  SetLow(regs.dx, static_cast<std::uint8_t>(_guest.Get(DS.currentTechLevel) + 2));
  _guest.Set(DS.menuRowCount, 0);
  regs.di = FIRST_ITEM_OFFSET;
  while (_guest.Byte(regs.si) < Low(regs.dx))
  {
    _guest.Push(regs.cx);
    _guest.Push(regs.di);
    const auto row = static_cast<std::uint8_t>(_guest.Get(DS.menuRowCount) + 1);
    _guest.Set(DS.menuRowCount, row);
    ++regs.si;
    _guest.Call(PRINT_TEXT_MODE_STRING);
    ++regs.si;
    regs.ax = SignedProduct(_guest.Byte(regs.si), _guest.Get(DS.currentGovernment));
    ++regs.si;
    regs.bx = regs.ax;
    regs.ax = SignedProduct(_guest.Byte(regs.si), _guest.Get(DS.currentEconomy));
    ++regs.si;
    regs.ax = static_cast<std::uint16_t>(regs.ax + regs.bx + _guest.Word(regs.si));
    regs.bx = static_cast<std::uint16_t>(row * 4);
    _guest.SetWord(PriceSlot(row), regs.ax);
    _guest.SetWord(static_cast<std::uint16_t>(PriceSlot(row) + 2), 0);
    // A resale price only for an item fitted, and never for fuel.
    std::uint16_t resale = 0;
    if (row != FUEL_ROW)
    {
      regs.bx = static_cast<std::uint16_t>(row + FITTED_BEFORE_FUEL);
      if (_guest.Byte(regs.bx) != 0)
      {
        resale = regs.ax;
      }
    }
    _guest.Set(DS.resalePriceInput, resale);
    _guest.Call(FORMAT_TENTHS);
    regs.di = _guest.Pop();
    _guest.Push(regs.si);
    _guest.Push(regs.di);
    regs.si = DS.priceText.offset;
    regs.di = static_cast<std::uint16_t>(regs.di + PRICE_COLUMN);
    _guest.Call(PRINT_TEXT_MODE_STRING);
    _guest.Call(COMPUTE_RESALE_PRICE);
    if (_guest.Flag(FLAG_ZERO))
    {
      regs.si = NO_RESALE_TEXT;
    }
    else
    {
      regs.bx = static_cast<std::uint16_t>(PriceSlot(_guest.Get(DS.menuRowCount)) + 2);
      _guest.SetWord(regs.bx, regs.ax);
      regs.di = DS.priceText.offset;
      _guest.Call(FORMAT_DECIMAL5);
      regs.di = DS.priceText.offset;
      regs.cx = 3;
      _guest.Call(BLANK_LEADING_ZEROS);
      // Tenths: the last digit moved right one for the point.
      SetLow(regs.ax, _guest.Get(DS.data8040));
      _guest.Set(DS.data8041, Low(regs.ax));
      _guest.Set(DS.data8040, '.');
      regs.si = DS.priceText.offset;
    }
    regs.di = _guest.Pop();
    _guest.Push(regs.di);
    regs.di = static_cast<std::uint16_t>(regs.di + RESALE_COLUMN);
    _guest.Call(PRINT_TEXT_MODE_STRING);
    regs.di = static_cast<std::uint16_t>(_guest.Pop() + ROW_BYTES);
    regs.si = static_cast<std::uint16_t>(_guest.Pop() + 2);
    regs.cx = static_cast<std::uint16_t>(_guest.Pop() - 1);
    if (regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(EQUIPMENT_LIST_ROW);
  }
  regs.si = EQUIP_MENU_FIRST_ROW;
  _guest.Call(RUN_EQUIP_SHIP_MENU);
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

void RunEquipShipMenu(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  _guest.Set(DS.menuFirstRowAttr, regs.si);
  StartMenuOnRegisters(_guest);
  for (;;)
  {
    SteerMenuCursor(_guest);
    for (;;)
    {
      if (!PollMenuKey(_guest, EQUIP_TICK_LOOP))
      {
        _guest.JumpBack(EQUIP_STEER);
        break;
      }
      _guest.Push(regs.ax);
      _guest.Call(PRINT_CREDITS_ON_MESSAGE_LINE);
      regs.ax = _guest.Pop();
      const std::uint8_t key = High(regs.ax);
      if (key == SCAN_B || key == SCAN_S)
      {
        if (key == SCAN_B)
        {
          BuyEquipment(_guest);
        }
        else
        {
          SellEquipment(_guest);
        }
        ShowEquipMessage(_guest);
        _guest.JumpBack(EQUIP_STEER);
        break;
      }
      if (key == SCAN_UP)
      {
        _guest.JumpBack(EQUIP_CURSOR_UP);
        MoveMenuCursorUpOnRegisters(_guest);
        continue;
      }
      if (key == SCAN_DOWN)
      {
        _guest.JumpBack(EQUIP_CURSOR_DOWN);
        MoveMenuCursorDownOnRegisters(_guest);
        continue;
      }
      if (key != SCAN_F4 && IsScreenKey(key))
      {
        return;
      }
      _guest.JumpBack(EQUIP_STEER);
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

void ChooseMountToFitLaser(Guest& _guest)
{
  _guest.Call(DRAW_LASER_MOUNT_MENU);
  RunMountChooser(_guest, FIT_CHOOSER);
}

void ChooseMountToRemoveLaser(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(DRAW_LASER_MOUNT_MENU);
  _guest.Push(regs.si);
  regs.si = SELL_LASER_TEXT;
  regs.di = SELL_LASER_OFFSET;
  _guest.Set(DS.textAttribute, HELP_ATTRIBUTE);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = _guest.Pop();
  RunMountChooser(_guest, REMOVE_CHOOSER);
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

void StartMenuOnRegisters(Guest& _guest)
{
  MenuCursorOut(_guest.Regs(), _guest.State(), StartMenu(_guest.State()));
}

void SteerMenuCursor(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(READ_STEERING);
  const std::uint8_t pitch = Negate(High(regs.ax));
  SetHigh(regs.ax, pitch);
  if (pitch == 0)
  {
    return;
  }
  if ((pitch & 0x80) != 0)
  {
    MoveMenuCursorUpOnRegisters(_guest);
  }
  else
  {
    MoveMenuCursorDownOnRegisters(_guest);
  }
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

void MoveMenuCursorUpOnRegisters(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const bool wraps = _guest.Get(DS.menuSelectedRow) == 1;
  MenuCursorOut(regs, _guest.State(), MoveMenuCursorUp(_guest.State(), regs.si));
  if (wraps)
  {
    // AH from the SHRs that measure the way to the last row: menuRowCount*256/16.
    SetHigh(regs.ax, High(static_cast<std::uint16_t>(MenuRowsQuarter(_guest.Get(DS.menuRowCount)) >> 2)));
  }
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

void MoveMenuCursorDownOnRegisters(Guest& _guest)
{
  MenuCursorOut(_guest.Regs(), _guest.State(), MoveMenuCursorDown(_guest.State(), _guest.Regs().si));
}

bool PollMenuKey(Guest& _guest, std::uint16_t _tickLoop)
{
  Registers& regs = _guest.Regs();
  regs.cx = TICKS_PER_POLL;
  for (;;)
  {
    _guest.Call(WAIT_FOR_TIMER_TICK);
    --regs.cx;
    if (regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(_tickLoop);
  }
  _guest.Call(GET_KEY);
  return !_guest.Flag(FLAG_ZERO);
}

bool IsScreenKey(std::uint8_t _key) noexcept
{
  return _key == SCAN_ESCAPE || (_key >= SCAN_F1 && _key < SCAN_PAST_F10);
}

// ── The entries of the de-assembled routines ──

namespace
{

using Machine::NativeContract;
using Machine::NativeReturn;
using Machine::NativeWait;
using Machine::REGISTER_ALL;

constexpr NativeContract CLOBBERS_ALL{REGISTER_ALL, 0};
// Symbols.tsv's "clobbers all", but for DS, which the original keeps and its caller goes on with.
constexpr NativeContract CLOBBERS_ALL_BUT_DS{static_cast<std::uint16_t>(REGISTER_ALL & ~Machine::REGISTER_DS), 0};
constexpr NativeContract RETURNS_ZERO{0, FLAG_ZERO};
constexpr NativeContract RETURNS_CARRY{0, FLAG_CARRY};

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

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x2F0F, "LaunchEscapePod", &LaunchEscapePodEntry, CLOBBERS_ALL_BUT_DS},
  NativeEntry{0x4401, "TryScoopObject", &TryScoopObjectEntry, PRESERVES_ALL},
  NativeEntry{0x5BF2, "ShowEquipShipScreen", &ShowEquipShipScreen, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x6111, "RunEquipShipMenu", &RunEquipShipMenu, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x633B, "SelectLaserType", &SelectLaserTypeEntry, RETURNS_ZERO},
  NativeEntry{0x6367, "DrawLaserMountMenu", &DrawLaserMountMenuEntry, PRESERVES_ALL},
  NativeEntry{0x63B2, "ChooseMountToFitLaser", &ChooseMountToFitLaser, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x646F, "ChooseMountToRemoveLaser", &ChooseMountToRemoveLaser, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
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
