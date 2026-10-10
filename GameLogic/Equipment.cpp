#include "pch.h"

#include "Equipment.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
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
constexpr std::uint16_t CLEAR_OBJECT_SLOT = 0x2FD8;
constexpr std::uint16_t FORMAT_DECIMAL5 = 0x3407;
constexpr std::uint16_t BLANK_LEADING_ZEROS = 0x3432;
constexpr std::uint16_t INIT_ABANDONED_COBRA = 0x4CA6;
constexpr std::uint16_t RECLAIM_SHIP_SLOT = 0x51FD;
constexpr std::uint16_t PRINT_TEXT_MODE_STRING = 0x60D2;
constexpr std::uint16_t RUN_EQUIP_SHIP_MENU = 0x6111;
constexpr std::uint16_t TOGGLE_MENU_ROW_HIGHLIGHT = 0x6328;
constexpr std::uint16_t SELECT_LASER_TYPE = 0x633B;
constexpr std::uint16_t DRAW_LASER_MOUNT_MENU = 0x6367;
constexpr std::uint16_t CHOOSE_MOUNT_TO_FIT_LASER = 0x63B2;
constexpr std::uint16_t CHOOSE_MOUNT_TO_REMOVE_LASER = 0x646F;
constexpr std::uint16_t REDRAW_EQUIP_HELP_TEXT = 0x653F;
constexpr std::uint16_t CLEAR_DOCKED_MESSAGE_LINE = 0x6553;
constexpr std::uint16_t PAINT_LASER_MOUNT_BOX = 0x6564;
constexpr std::uint16_t SWAP_TEXT_ATTRIBUTE_NIBBLES = 0x6580;
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

constexpr std::uint16_t SCREEN_PRICES = 0x823B; // four bytes a row: the price, then the resale price, in tenths

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

// 63C3 and 6490: the mount box moved left, from FORE round to LEFT.
void MoveMountBoxLeft(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(PAINT_LASER_MOUNT_BOX);
  regs.si = static_cast<std::uint16_t>(regs.si - MOUNT_BOX_STEP);
  const auto mount = static_cast<std::uint8_t>(_guest.Get(DS.selectedLaserMount) - 1);
  _guest.Set(DS.selectedLaserMount, mount);
  if ((mount & 0x80) != 0)
  {
    _guest.Set(DS.selectedLaserMount, LAST_MOUNT);
    regs.si = LAST_MOUNT_BOX;
  }
  _guest.Set(DS.textAttribute, MOUNT_HIGHLIGHT_ATTRIBUTE);
  _guest.Call(PAINT_LASER_MOUNT_BOX);
}

// 63E1 and 64AE: the mount box moved right, from LEFT round to FORE.
void MoveMountBoxRight(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(PAINT_LASER_MOUNT_BOX);
  regs.si = static_cast<std::uint16_t>(regs.si + MOUNT_BOX_STEP);
  const auto mount = static_cast<std::uint8_t>(_guest.Get(DS.selectedLaserMount) + 1);
  _guest.Set(DS.selectedLaserMount, mount);
  if (mount == MOUNT_COUNT)
  {
    _guest.Set(DS.selectedLaserMount, 0);
    regs.si = FIRST_MOUNT_BOX;
  }
  _guest.Set(DS.textAttribute, MOUNT_HIGHLIGHT_ATTRIBUTE);
  _guest.Call(PAINT_LASER_MOUNT_BOX);
}

// 6438 and 6505: a mount message on the message line, SI kept.
void PrintMountMessage(Guest& _guest, std::uint16_t _text, std::uint16_t _jumpedTo)
{
  Registers& regs = _guest.Regs();
  _guest.Push(regs.si);
  regs.si = _text;
  if (_jumpedTo != 0)
  {
    _guest.JumpBack(_jumpedTo);
  }
  regs.di = MESSAGE_OFFSET;
  _guest.Set(DS.textAttribute, MENU_ATTRIBUTE);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = _guest.Pop();
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

// 642A: Space in ChooseMountToFitLaser. A free mount gets the bought laser; true once it has.
bool FitLaserOnMount(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const MountBit bit = ShiftOutMountBit(_guest.State());
  regs.cx = Join(bit.mountsAbove, bit.shiftCount);
  if (bit.fitted)
  {
    PrintMountMessage(_guest, MOUNT_OCCUPIED_TEXT, 0);
    return false;
  }
  SetLow(regs.cx, _guest.Get(DS.selectedLaserMount));
  SetLow(regs.ax, static_cast<std::uint8_t>(1u << Low(regs.cx)));
  _guest.Set(DS.laserMountsFitted, static_cast<std::uint8_t>(_guest.Get(DS.laserMountsFitted) | Low(regs.ax)));
  // The mount's two bits of laserMountTypes: 3 and the type, shifted up together as AX.
  regs.ax = Join(_guest.Get(DS.selectedLaserType), MOUNT_TYPE_BITS);
  SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) << 1));
  regs.ax = static_cast<std::uint16_t>(regs.ax << Low(regs.cx));
  SetLow(regs.ax, static_cast<std::uint8_t>(~Low(regs.ax)));
  _guest.Set(DS.laserMountTypes, static_cast<std::uint8_t>((_guest.Get(DS.laserMountTypes) & Low(regs.ax)) | High(regs.ax)));
  _guest.Call(REDRAW_EQUIP_HELP_TEXT);
  return true;
}

// 64F7: Space in ChooseMountToRemoveLaser. A mount holding the sold laser's type is freed; true once it is.
bool RemoveLaserFromMount(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  const MountBit bit = ShiftOutMountBit(_guest.State());
  regs.cx = Join(bit.mountsAbove, bit.shiftCount);
  if (!bit.fitted)
  {
    PrintMountMessage(_guest, NO_LASER_ON_MOUNT_TEXT, 0);
    return false;
  }
  SetLow(regs.ax, _guest.Get(DS.laserMountTypes));
  SetLow(regs.cx, static_cast<std::uint8_t>((Low(regs.cx) - 1) << 1));
  SetLow(regs.ax, static_cast<std::uint8_t>((Low(regs.ax) >> Low(regs.cx)) & MOUNT_TYPE_BITS));
  if (Low(regs.ax) != _guest.Get(DS.selectedLaserType))
  {
    PrintMountMessage(_guest, WRONG_LASER_TYPE_TEXT, REMOVE_MOUNT_MESSAGE);
    return false;
  }
  SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) >> 1));
  SetLow(regs.ax, static_cast<std::uint8_t>(~(1u << Low(regs.cx))));
  _guest.Set(DS.laserMountsFitted, static_cast<std::uint8_t>(_guest.Get(DS.laserMountsFitted) & Low(regs.ax)));
  _guest.Call(REDRAW_EQUIP_HELP_TEXT);
  return true;
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

constexpr MountChooser FIT_CHOOSER{0x63B5, 0x63C3, 0x63E1, 0x6405, &FitLaserOnMount};
constexpr MountChooser REMOVE_CHOOSER{0x6482, 0x6490, 0x64AE, 0x64D2, &RemoveLaserFromMount};

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
        MoveMountBoxLeft(_guest);
      }
      else
      {
        MoveMountBoxRight(_guest);
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
        MoveMountBoxLeft(_guest);
        continue;
      }
      if (key == SCAN_RIGHT)
      {
        _guest.JumpBack(_chooser.right);
        MoveMountBoxRight(_guest);
        continue;
      }
      _guest.JumpBack(_chooser.steer);
      break;
    }
  }
}

} // namespace

void LaunchEscapePod(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Set(DS.hyperspaceCountdown, 0);
  _guest.Set(DS.escapePodFrames, ESCAPE_POD_FRAMES);
  FindFreeShipSlotEntry(_guest);
  if (!_guest.Flag(FLAG_CARRY))
  {
    _guest.Call(RECLAIM_SHIP_SLOT);
  }
  regs.di = regs.si;
  _guest.Call(CLEAR_OBJECT_SLOT);
  _guest.Call(INIT_ABANDONED_COBRA);
  _guest.Set(DS.playerPitchAngle, static_cast<std::uint16_t>(_guest.Get(DS.playerPitchAngle) + HALF_TURN));
  _guest.Set(DS.viewAngle, HALF_TURN);
  _guest.Set(DS.viewLocked, 1);
  _guest.Set(DS.playerSpeed, ESCAPE_POD_SPEED);
  _guest.Set(DS.velocityDirty, 1);
  _guest.Set(DS.escapePodFitted, 0);
  UpdatePlayerVelocity(_guest);
  for (regs.cx = ESCAPE_POD_MOVES; regs.cx != 0; --regs.cx)
  {
    const std::uint16_t count = regs.cx;
    MoveObjectsByVelocityEntry(_guest);
    regs.cx = count;
  }
  // MOV [DI],CH, which is 0 throughout: the 17 amounts held emptied.
  regs.di = DS.cargoHold.offset;
  for (regs.cx = CARGO_KINDS; regs.cx != 0; --regs.cx)
  {
    _guest.SetByte(regs.di, High(regs.cx));
    regs.di = static_cast<std::uint16_t>(regs.di + 2);
  }
}

void TryScoopObject(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  std::uint8_t space = _guest.Get(DS.largeCargoBayFitted) == 1 ? LARGE_CARGO_BAY_TONNES : CARGO_BAY_TONNES;
  space = static_cast<std::uint8_t>(space - _guest.Get(DS.cargoUsedTonnes));
  SetLow(regs.dx, space);
  _guest.Set(DS.freeCargoTonnes, space);
  // The box: y from 30 to 229, x and z within 149 either side.
  if ((regs.bx & 0x8000) != 0)
  {
    return;
  }
  const auto above = static_cast<std::uint16_t>(regs.bx - SCOOP_LOWEST);
  regs.dx = above;
  if (regs.bx < SCOOP_LOWEST)
  {
    return;
  }
  regs.dx = static_cast<std::uint16_t>(above - SCOOP_HEIGHT);
  if (above >= SCOOP_HEIGHT)
  {
    return;
  }
  regs.dx = Magnitude(regs.ax);
  if (regs.dx >= SCOOP_HALF_WIDTH)
  {
    return;
  }
  regs.dx = Magnitude(regs.cx);
  if (regs.dx >= SCOOP_HALF_WIDTH)
  {
    return;
  }

  const std::uint16_t x = regs.ax;
  const std::uint16_t y = regs.bx;
  const std::uint16_t z = regs.cx;
  const auto type = static_cast<std::uint8_t>((_guest.Byte(regs.di) >> 1) & TYPE_MASK);
  const std::uint8_t flags = _guest.Byte(static_cast<std::uint16_t>(regs.di + SLOT_FLAGS));
  const bool full = _guest.Get(DS.freeCargoTonnes) == 0;
  std::uint16_t message = DS.scoopRetrievalInactiveText.offset;
  if (type == TYPE_CARGO_BARREL)
  {
    if ((flags & FLAG_MASKING_DEVICE) != 0)
    {
      RemoveObject(_guest);
      _guest.Set(DS.maskingDeviceRecovered, 1);
      message = DS.maskingDeviceText.offset;
    }
    else if (full)
    {
      message = DS.cargoBayFullText.offset;
    }
    else
    {
      // A random product 0-10, furs for slaves, into the hold, and its name for the message.
      RemoveObject(_guest);
      NextRandomEntry(_guest);
      regs.ax = Low(regs.ax);
      SetLow(regs.bx, RANDOM_PRODUCT_DIVISOR);
      DivideByte(_guest, RANDOM_PRODUCT_DIVISOR);
      if (Low(regs.ax) == PRODUCT_SLAVES)
      {
        SetLow(regs.ax, PRODUCT_FURS);
      }
      regs.ax = Low(regs.ax);
      regs.bx = static_cast<std::uint16_t>(regs.ax << 1);
      IncrementByte(_guest.State(), static_cast<std::uint16_t>(regs.bx + DS.cargoHold.offset));
      _guest.Set(DS.cargoUsedTonnes, static_cast<std::uint8_t>(_guest.Get(DS.cargoUsedTonnes) + 1));
      regs.bx = static_cast<std::uint16_t>(regs.ax * PRODUCT_NAME_BYTES + DS.productNames.offset);
      const std::uint16_t slot = regs.di;
      regs.di = DS.scoopedCargoText.offset;
      for (regs.cx = SCOOPED_NAME_BYTES; regs.cx != 0; --regs.cx)
      {
        SetLow(regs.ax, _guest.Byte(regs.bx));
        ++regs.bx;
        _guest.SetByte(regs.di, Low(regs.ax));
        ++regs.di;
      }
      regs.di = slot;
      message = DS.scoopedCargoText.offset;
    }
  }
  else if (type == TYPE_SPLINTER && (flags & FLAG_PRECIOUS) != 0)
  {
    // Gems, gold and platinum always; minerals or alloys too if there is room.
    RemoveObject(_guest);
    NextRandomEntry(_guest);
    SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) & 7));
    AddPrecious(_guest.State(), DS.cargoGemStonesGrams, Low(regs.ax));
    SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) & 3));
    AddPrecious(_guest.State(), DS.cargoGoldKg, Low(regs.ax));
    NextRandomEntry(_guest);
    SetHigh(regs.ax, static_cast<std::uint8_t>((High(regs.ax) & 3) + 1));
    AddPrecious(_guest.State(), DS.cargoPlatinumKg, High(regs.ax));
    if (_guest.Get(DS.freeCargoTonnes) != 0)
    {
      NextRandomEntry(_guest);
      if (Low(regs.ax) >= MINERALS_FROM)
      {
        IncrementByte(_guest.State(), DS.cargoMineralsTonnes.offset);
      }
      else
      {
        IncrementByte(_guest.State(), DS.cargoAlloysTonnes.offset);
      }
      _guest.Set(DS.cargoUsedTonnes, static_cast<std::uint8_t>(_guest.Get(DS.cargoUsedTonnes) + 1));
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
      RemoveObject(_guest);
      const DataField<std::uint8_t> held = type == TYPE_SPLINTER     ? DS.cargoAlloysTonnes
                                           : type == TYPE_ESCAPE_POD ? DS.cargoSlavesTonnes
                                                                     : DS.cargoAlienItemsTonnes;
      IncrementByte(_guest.State(), held.offset);
      _guest.Set(DS.cargoUsedTonnes, static_cast<std::uint8_t>(_guest.Get(DS.cargoUsedTonnes) + 1));
      message = type == TYPE_SPLINTER     ? DS.metalAlloysText.offset
                : type == TYPE_ESCAPE_POD ? DS.escapePodRetrievedText.offset
                                          : DS.alienItemsText.offset;
    }
  }
  regs.ax = message;
  _guest.Set(DS.messagePointer, regs.ax);
  _guest.Set(DS.messageFrames, SCOOP_MESSAGE_FRAMES);
  regs.cx = z;
  regs.bx = y;
  regs.ax = x;
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
  StartMenu(_guest);
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
        MoveMenuCursorUp(_guest);
        continue;
      }
      if (key == SCAN_DOWN)
      {
        _guest.JumpBack(EQUIP_CURSOR_DOWN);
        MoveMenuCursorDown(_guest);
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

void DrawLaserMountMenu(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Set(DS.textAttribute, HELP_ATTRIBUTE);
  regs.si = MOUNT_HEADER_TEXT;
  regs.di = MOUNT_HEADER_OFFSET;
  PrintCountedTextLinesEntry(_guest);
  // Each mount's laser, or Free: its bit of laserMountsFitted in DH, its two bits of laserMountTypes in DL.
  regs.di = MOUNT_NAMES_OFFSET;
  regs.dx = Join(_guest.Get(DS.laserMountsFitted), _guest.Get(DS.laserMountTypes));
  for (regs.cx = MOUNT_COUNT; regs.cx != 0; --regs.cx)
  {
    const bool fitted = (High(regs.dx) & 1) != 0;
    SetHigh(regs.dx, static_cast<std::uint8_t>(High(regs.dx) >> 1));
    regs.si = FREE_MOUNT_TEXT;
    if (fitted)
    {
      regs.bx = DS.laserTypeNames.At(Low(regs.dx) & MOUNT_TYPE_BITS);
      regs.si = _guest.Word(regs.bx);
    }
    PrintTextModeStringEntry(_guest);
    SetLow(regs.dx, static_cast<std::uint8_t>(Low(regs.dx) >> 2));
  }
  regs.si = FIRST_MOUNT_BOX;
  _guest.Set(DS.textAttribute, MOUNT_HIGHLIGHT_ATTRIBUTE);
  PaintLaserMountBoxEntry(_guest);
  _guest.Set(DS.selectedLaserMount, 0);
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

void RedrawEquipHelpText(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Set(DS.textAttribute, HELP_ATTRIBUTE);
  regs.di = HELP_TEXT_OFFSET;
  regs.si = DS.equipHelpText.offset;
  PrintCountedTextLinesEntry(_guest);
  _guest.Set(DS.textAttribute, MENU_ATTRIBUTE);
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

void PayForEquipmentItem(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t row = _guest.Get(DS.menuSelectedRow);
  regs.bx = WithLow(regs.bx, row);
  if (row == FUEL_ROW)
  {
    // What fills the tank: (255-fuel) * the price's low byte / 36, at least 1.
    const auto missing = static_cast<std::uint8_t>(~_guest.Get(DS.fuel));
    regs.dx = _guest.Get(DS.data823F);
    regs.ax = static_cast<std::uint16_t>(missing * Low(regs.dx));
    regs.dx = WithLow(regs.dx, FUEL_UNITS_PER_TENTH);
    DivideByte(_guest, FUEL_UNITS_PER_TENTH);
    if (Low(regs.ax) == 0)
    {
      regs.ax = WithLow(regs.ax, 1);
    }
    regs.ax = Low(regs.ax);
  }
  else
  {
    regs.bx = PriceSlot(row);
    regs.ax = _guest.Word(regs.bx);
  }
  regs.bx = 0;
  SubtractCreditsEntry(_guest);
}

void ClearEquipmentSellPrice(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  regs.si = NO_RESALE_TEXT;
  SellColumnOut(regs, PrintEquipmentSellColumn(_guest.State(), regs.si));
}

void ShowEquipmentSellPrice(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = PriceSlot(_guest.Get(DS.menuSelectedRow));
  regs.ax = _guest.Word(regs.bx);
  _guest.Set(DS.resalePriceInput, regs.ax);
  const std::uint16_t slot = regs.bx;
  _guest.Call(COMPUTE_RESALE_PRICE);
  regs.bx = slot;
  _guest.SetWord(static_cast<std::uint16_t>(regs.bx + 2), regs.ax);
  FormatTenthsEntry(_guest);
  regs.si = DS.priceText.offset;
  SellColumnOut(regs, PrintEquipmentSellColumn(_guest.State(), regs.si));
}

void StartMenu(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(SWAP_TEXT_ATTRIBUTE_NIBBLES);
  _guest.Call(PRINT_CREDITS_ON_MESSAGE_LINE);
  _guest.Call(SWAP_TEXT_ATTRIBUTE_NIBBLES);
  regs.si = _guest.Get(DS.menuFirstRowAttr);
  _guest.Set(DS.menuSelectedRow, 1);
  _guest.Call(TOGGLE_MENU_ROW_HIGHLIGHT);
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
    MoveMenuCursorUp(_guest);
  }
  else
  {
    MoveMenuCursorDown(_guest);
  }
}

void MoveMenuCursorUp(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(PRINT_CREDITS_ON_MESSAGE_LINE);
  _guest.Call(TOGGLE_MENU_ROW_HIGHLIGHT);
  const auto row = static_cast<std::uint8_t>(_guest.Get(DS.menuSelectedRow) - 1);
  _guest.Set(DS.menuSelectedRow, row);
  if (row == 0)
  {
    // To the last row: count*80 bytes on, as count*256/4 and that /4 again.
    regs.si = _guest.Get(DS.menuFirstRowAttr);
    const std::uint8_t count = _guest.Get(DS.menuRowCount);
    _guest.Set(DS.menuSelectedRow, count);
    regs.ax = static_cast<std::uint16_t>(Join(count, 0) >> 2);
    regs.si = Offset(regs.si, regs.ax);
    regs.ax = static_cast<std::uint16_t>(regs.ax >> 2);
    regs.si = Offset(regs.si, regs.ax);
  }
  regs.si = static_cast<std::uint16_t>(regs.si - ROW_BYTES);
  _guest.Call(TOGGLE_MENU_ROW_HIGHLIGHT);
}

void MoveMenuCursorDown(Guest& _guest)
{
  Registers& regs = _guest.Regs();
  _guest.Call(PRINT_CREDITS_ON_MESSAGE_LINE);
  _guest.Call(TOGGLE_MENU_ROW_HIGHLIGHT);
  const auto row = static_cast<std::uint8_t>(_guest.Get(DS.menuSelectedRow) + 1);
  _guest.Set(DS.menuSelectedRow, row);
  SetLow(regs.ax, _guest.Get(DS.menuRowCount));
  if (Low(regs.ax) < row)
  {
    regs.si = static_cast<std::uint16_t>(_guest.Get(DS.menuFirstRowAttr) - ROW_BYTES);
    _guest.Set(DS.menuSelectedRow, 1);
  }
  regs.si = static_cast<std::uint16_t>(regs.si + ROW_BYTES);
  _guest.Call(TOGGLE_MENU_ROW_HIGHLIGHT);
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
constexpr NativeContract RETURNS_ZERO{0, FLAG_ZERO};

} // namespace

void SelectLaserTypeEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_ZERO, SelectLaserType(_guest.State()));
  _guest.Clobber(RETURNS_ZERO);
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

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x2F0F, "LaunchEscapePod", &LaunchEscapePod, CLOBBERS_ALL},
  NativeEntry{0x4401, "TryScoopObject", &TryScoopObject, PRESERVES_ALL},
  NativeEntry{0x5BF2, "ShowEquipShipScreen", &ShowEquipShipScreen, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x6111, "RunEquipShipMenu", &RunEquipShipMenu, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x633B, "SelectLaserType", &SelectLaserTypeEntry, RETURNS_ZERO},
  NativeEntry{0x6367, "DrawLaserMountMenu", &DrawLaserMountMenu, PRESERVES_ALL},
  NativeEntry{0x63B2, "ChooseMountToFitLaser", &ChooseMountToFitLaser, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x646F, "ChooseMountToRemoveLaser", &ChooseMountToRemoveLaser, CLOBBERS_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x653F, "RedrawEquipHelpText", &RedrawEquipHelpText, PRESERVES_ALL},
  NativeEntry{0x6564, "PaintLaserMountBox", &PaintLaserMountBoxEntry, PRESERVES_ALL},
  NativeEntry{0x65A3, "PayForEquipmentItem", &PayForEquipmentItem, NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x6946, "ClearEquipmentSellPrice", &ClearEquipmentSellPrice, PRESERVES_ALL},
  NativeEntry{0x6972, "ShowEquipmentSellPrice", &ShowEquipmentSellPrice, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> EquipmentEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
